// SPDX-License-Identifier: GPL-3.0-or-later
#include "persistence.hpp"
#include "crash_thread.hpp"

#include "etude/sections_store.hpp"

#include "etude/look.hpp"
#include "etude/profile.hpp"
#include "etude/profile_settings.hpp"
#include "etude/settings_doc.hpp"

#include <array>
#include <string_view>

namespace etude::app {

namespace {

constexpr std::array<std::string_view, 2> kOwnedSections{"look", "user_presets"};

// The `slot_N_...` lines of a look document; the look's own lines live in [look].
std::string slot_lines(const UserPresets& presets) {
    LookDocument doc;
    doc.user_presets = presets;
    const std::string text = serialize_document(doc);
    std::string out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string_view line(text.data() + start, end - start);
        if (line.substr(0, 5) == "slot_") {
            out.append(line);
            out.push_back('\n');
        }
        start = end + 1;
    }
    return out;
}

} // namespace

UiModel model_from(const LoadedSettings& loaded) {
    UiModel model;
    model.app = loaded.app;
    model.session = loaded.session;
    model.look = parse_look(loaded.document.section_text("look"));
    model.user_presets =
        parse_look_document(loaded.document.section_text("user_presets")).user_presets;
    model.sections = read_sections(loaded.document);
    return model;
}

std::string serialize_model(const LoadedSettings& base, const UiModel& model) {
    LoadedSettings out = base;
    out.app = model.app;
    out.session = model.session;
    out.document.set_section_text("look", serialize(model.look));
    out.document.set_section_text("user_presets", slot_lines(model.user_presets));
    write_sections(out.document, model.sections);
    return out.serialize();
}

RendererPath saved_renderer(const std::filesystem::path& directory) {
    SettingsStore store(directory);
    const LoadResult result = store.load();
    if (result.source == LoadSource::None) {
        return RendererPath::Basic;
    }
    // Sections other than [app] are not read here, whatever their names.
    const SettingsDocument doc = SettingsDocument::parse(result.text);
    return read_app_settings(doc, nullptr).renderer;
}

std::int64_t saved_active_profile(const std::filesystem::path& directory) {
    SettingsStore store(directory);
    const LoadResult result = store.load();
    if (result.source == LoadSource::None) {
        return kDefaultProfileId;
    }
    return read_app_settings(SettingsDocument::parse(result.text), nullptr).active_profile;
}

Persistence::Persistence(std::filesystem::path directory) : store_(std::move(directory)) {}

Persistence::~Persistence() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::string Persistence::load(UiModel& model) {
    const LoadResult result = store_.load();
    std::string message = result.message;
    if (result.source == LoadSource::None) {
        loaded_ = load_settings("", kOwnedSections);
    } else {
        // The song sections (`sections.<hash>`) belong to the sections layer: known, not ours.
        std::vector<std::string> names =
            sections_section_names(SettingsDocument::parse(result.text));
        std::vector<std::string_view> known(std::begin(kOwnedSections), std::end(kOwnedSections));
        known.insert(known.end(), names.begin(), names.end());
        loaded_ = load_settings(result.text, known);
        if (loaded_.read_only_recommended()) {
            read_only_ = true;
            message = "Settings are from a newer version: changes are not saved";
        }
    }
    model = model_from(loaded_);
    model.app = model.app.clamped();
    model.session = model.session.clamped();
    model.look = model.look.clamped();
    saved_ = model;
    dirty_ = false;
    // Restored from the backup: write the main file again on the first frame, so the next start
    // does not depend on the backup. When nothing usable was found the defaults are not written
    // until the first change, and whatever damaged file is there stays for now.
    heal_ = result.source == LoadSource::Backup && !read_only_;
    return message;
}

std::string Persistence::profile_text(const UiModel& model) {
    wait();
    return profile_settings_text(SettingsDocument::parse(serialize_model(loaded_, model)));
}

void Persistence::apply_profile(UiModel& model, std::string_view text) {
    wait(); // the writer is done with loaded_
    SettingsDocument doc = SettingsDocument::parse(serialize_model(loaded_, model));
    apply_profile_settings(doc, text);
    std::vector<std::string> names = sections_section_names(doc);
    std::vector<std::string_view> known(std::begin(kOwnedSections), std::end(kOwnedSections));
    known.insert(known.end(), names.begin(), names.end());
    loaded_ = load_settings(doc.serialize(), known);
    model = model_from(loaded_);
    model.app = model.app.clamped();
    model.session = model.session.clamped();
    model.look = model.look.clamped();
}

bool Persistence::uncollected() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return result_ready_;
}

// A write is queued, under way, or finished with its outcome not yet taken: `job_` still belongs to
// it until collect() has read it.
bool Persistence::writing() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return has_job_ || busy_ || result_ready_;
}

bool Persistence::submit(const UiModel& model) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (has_job_ || busy_ || result_ready_) {
            return false;
        }
        job_ = model; // assigned over the last one, so its buffers are reused
        has_job_ = true;
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
    return true;
}

void Persistence::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stop_ || has_job_; });
        if (!has_job_) {
            return; // stopped with nothing queued
        }
        has_job_ = false;
        busy_ = true;
        lock.unlock();
        // job_ and loaded_ are the writer's while busy_ is set.
        const SaveResult result = store_.save(serialize_model(loaded_, job_));
        lock.lock();
        busy_ = false;
        result_ok_ = result.ok;
        result_error_ = result.error;
        result_ready_ = true;
        idle_.notify_all();
    }
}

std::string Persistence::collect(std::int64_t now_ns) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!result_ready_) {
        return {};
    }
    result_ready_ = false;
    if (result_ok_) {
        warned_failure_ = false;
        saved_ = job_;
        return {};
    }
    // Try again in five seconds, and say so once.
    dirty_ = true;
    pending_ = job_;
    changed_at_ns_ = now_ns + kRetryDelayNs - kSaveDebounceNs;
    if (!warned_failure_) {
        warned_failure_ = true;
        return "Could not save settings: " + result_error_;
    }
    return {};
}

std::string Persistence::update(const UiModel& model, std::int64_t now_ns) {
    std::string message = collect(now_ns);
    if (writing()) {
        return message; // one write at a time: this model is looked at again next frame
    }
    if (heal_) {
        if (read_only_) {
            heal_ = false;
        } else if (submit(model)) {
            heal_ = false;
        }
        return message;
    }
    if (model == saved_) {
        dirty_ = false; // nothing to write, or changed and changed back
        return message;
    }
    if (!dirty_ || !(model == pending_)) {
        pending_ = model;
        changed_at_ns_ = now_ns;
        dirty_ = true;
    }
    if (now_ns - changed_at_ns_ >= kSaveDebounceNs) {
        if (read_only_) {
            dirty_ = false;
        } else if (submit(model)) {
            dirty_ = false; // else the writer finished just now: the next frame tries again
        }
    }
    return message;
}

std::string Persistence::wait(std::int64_t now_ns) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return !has_job_ && !busy_; });
    }
    return collect(now_ns);
}

std::string Persistence::flush(const UiModel& model) {
    std::string message = wait(changed_at_ns_);
    if (model == saved_ || read_only_) {
        return message;
    }
    submit(model);
    const std::string failure = wait(changed_at_ns_);
    return message.empty() ? failure : message;
}

} // namespace etude::app
