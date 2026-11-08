// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_service.hpp"
#include "crash_thread.hpp"
#include "log_ring.hpp"

#include "loop_wake.hpp"
#include "update_service_shared.hpp"
#include "update_files.hpp"
#include "update_status.hpp"

#include "etude/update_key.hpp"

#include "etude/release_info.hpp"
#include "etude/semver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace etude::app {

namespace {

constexpr std::size_t kNotesLines = 4;
constexpr std::size_t kNotesChars = 72;

std::string read_text(const std::filesystem::path& path, std::size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    std::string text(max_bytes, '\0');
    in.read(text.data(), static_cast<std::streamsize>(max_bytes));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}

std::int64_t system_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::filesystem::path update_dir_for(const std::filesystem::path& exe) {
    namespace fs = std::filesystem;
    // .../<prefix>/versions/<version>/bin/etude
    const fs::path bin = exe.parent_path();
    const fs::path version = bin.parent_path();
    const fs::path versions = version.parent_path();
    const std::string name = version.filename().string();
    const std::optional<SemVer> parsed = parse_semver(name);
    if (exe.filename() != "etude" || bin.filename() != "bin" || versions.filename() != "versions" ||
        !exe.is_absolute() || !parsed || !parsed->prerelease.empty() ||
        name.find('+') != std::string::npos) {
        return {};
    }
    return versions.parent_path() / "update";
}

MachineOsText read_machine_os() {
    MachineOsText machine;
#if defined(__linux__)
    machine.arch = build_debian_arch(); // the userland's, never the kernel's
    parse_os_release(read_text("/etc/os-release", 16 * 1024), machine);
    machine.debian_version = first_line(read_text("/etc/debian_version", 256));
#elif defined(__APPLE__)
    machine.arch = build_debian_arch(); // arm64 on Apple silicon; an Intel Mac has no release
    machine.id = std::string(kMacOsName);
    char version[64] = {};
    std::size_t size = sizeof version;
    if (::sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0) {
        machine.version_id = version;
    }
#endif
    return machine;
}

UpdateService::Shared::Shared(UpdateOptions o) : options(std::move(o)) {
    if (!options.clock) {
        options.clock = system_seconds;
    }
    if (!options.verify_digest) {
        options.verify_digest = [](const Digest512& digest, std::string_view signature,
                                   std::string_view comment) {
            return verify_release_digest(digest, signature, comment);
        };
    }
    if (!options.verify) {
        options.verify = [](std::span<const std::uint8_t> data, std::string_view signature,
                            std::string_view comment) {
            return verify_release_signature(data, signature, comment);
        };
    }
}

void UpdateService::Shared::publish(ui::UpdateView next) {
    {
        std::lock_guard lock(mutex);
        latest = std::move(next);
        ++serial;
    }
    wake_loop(WakeSource::Update);
}

ui::UpdateView UpdateService::Shared::current() {
    std::lock_guard lock(mutex);
    return latest;
}

bool UpdateService::Shared::stopping() {
    std::lock_guard lock(mutex);
    return quit;
}

void UpdateService::Shared::save_state() const {
    if (options.state_file.empty()) {
        return;
    }
    std::string text = "last_checked=" + std::to_string(last_ok_s) + "\n" +
                       "last_tried=" + std::to_string(last_try_s) + "\n";
    if (last_was_current) {
        text += "up_to_date_for=" + options.current_version + "\n";
    } else if (last_was_available) {
        text += "available_for=" + options.current_version + "\n";
    }
    (void)write_file_atomic(options.state_file, text);
}

// Reads the stored times as they are: a time in the future is shown as unknown and not trusted by
// the daily check, but never rewritten here; the next check writes a new one.
void UpdateService::Shared::load_state() {
    if (options.state_file.empty()) {
        return;
    }
    std::istringstream in(read_text(options.state_file, 1024));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("last_checked=", 0) == 0) {
            last_ok_s = std::atoll(line.c_str() + 13);
        } else if (line.rfind("last_tried=", 0) == 0) {
            last_try_s = std::atoll(line.c_str() + 11);
        } else if (line == "up_to_date_for=" + options.current_version) {
            last_was_current = true;
        } else if (line == "available_for=" + options.current_version) {
            last_was_available = true;
        }
    }
}

// Reads the release's `release.json` and, when the release holds files for this machine, keeps them
// as the offer and says whether "Update" installs; else says why not on `next`.
UpdateService::Shared::Offer UpdateService::Shared::attach_offer(const LatestRelease& release,
                                                                 const FetchProgress& wanted,
                                                                 ui::UpdateView& next,
                                                                 FailureText& why) {
    next.installable = false;
    next.detail = "The files for this system are not on the release yet";
    const ReleaseAsset* info_asset =
        find_release_asset(release, release_info_asset_for(options.machine.view()));
    if (info_asset == nullptr) {
        return Offer::Done;
    }
    const FetchResult info_file = options.fetcher->fetch(info_asset->url, update_fetch_hosts(),
                                                         kMaxReleaseInfoBytes, wanted);
    if (info_file.cancelled) {
        return Offer::Cancelled;
    }
    // Only a 404 says the files are not there; no answer is a failed check, which Retry repeats.
    if (!info_file.ok && info_file.status != 404) {
        why = fetch_failure_text(info_file);
        return Offer::Failed;
    }
    const ReleaseInfoResult info =
        info_file.ok ? parse_release_info(info_file.body) : ReleaseInfoResult{};
    if (!info_file.ok || !info.ok()) {
        return Offer::Done;
    }
    const AssetPick pick = pick_release_assets(release, info.info, options.machine.view());
    if (pick.result == PickResult::NotThisMachine) {
        next.detail = "This release is not built for this system";
    } else if (pick.result == PickResult::Ok) {
        next.installable = options.installs && !options.update_dir.empty();
        next.detail.clear();
        if (!options.mac.blocked.empty()) {
            next.detail = "Can't install here: " + options.mac.blocked + ". Update opens the release page";
        }
        if (next.installable) {
            std::lock_guard lock(mutex);
            found = UpdateOffer{true, release, info.info, pick};
        }
    }
    return Offer::Done;
}

void UpdateService::Shared::run_check() {
    ui::UpdateView before = current();
    ui::UpdateView next = before;
    next.phase = ui::UpdatePhase::Checking;
    next.failure.clear();
    next.failure_summary.clear();
    publish(next);
    last_try_s = options.clock();
    save_state(); // an attempt counts for the daily check even when it fails, or the app quits
    const auto still_wanted = [this](std::uint64_t, std::uint64_t) { return !stopping(); };
    const FetchResult feed = options.fetcher->fetch(kLatestReleaseUrl, update_fetch_hosts(),
                                                    kMaxFeedBytes, still_wanted);
    if (feed.cancelled) {
        publish(before);
        return;
    }
    const auto fail = [&](FailureText why) {
        before.phase = ui::UpdatePhase::Failed;
        before.failure = std::move(why.full);
        before.failure_summary = std::move(why.summary);
        before.retry_is_check = true;
        publish(before);
    };
    if (!feed.ok) {
        fail(fetch_failure_text(feed));
        return;
    }
    const LatestReleaseResult latest_release = parse_latest_release(feed.body);
    if (!latest_release.ok()) {
        fail({"GitHub's answer could not be read", "Unreadable answer"});
        return;
    }
    const VersionOrder order = compare_versions(latest_release.release.version, options.current_version);
    if (order == VersionOrder::Malformed) {
        fail({"The release's version could not be read", "Unreadable version"});
        return;
    }
    const std::int64_t now = options.clock();
    next = ui::UpdateView{};
    next.last_checked_s = now;
    next.release_page = latest_release.release.page_url;
    {
        std::lock_guard lock(mutex);
        found = UpdateOffer{};
    }
    if (order != VersionOrder::Newer) {
        next.phase = ui::UpdatePhase::UpToDate;
        last_ok_s = now;
        last_was_current = true;
        last_was_available = false;
        save_state();
        publish(next);
        return;
    }
    next.phase = ui::UpdatePhase::Available;
    next.version = latest_release.release.version;
    next.notes = release_notes_lines(latest_release.release.notes, kNotesLines, kNotesChars);
    FailureText why;
    switch (attach_offer(latest_release.release, still_wanted, next, why)) {
    case Offer::Cancelled:
        publish(before);
        return;
    case Offer::Failed:
        fail(std::move(why));
        return;
    case Offer::Done:
        break;
    }
    last_ok_s = now;
    last_was_current = false;
    last_was_available = true;
    save_state();
    publish(next);
}

void UpdateService::Shared::work() {
    std::unique_lock lock(mutex);
    cv.wait_for(lock, std::chrono::milliseconds(options.startup_delay_ms),
                [this] { return quit || check_wanted || install_wanted || ready_wanted; });
    while (!quit) {
        if (ready_wanted) {
            ready_wanted = false;
            lock.unlock();
            write_ready();
            lock.lock();
            continue;
        }
        if (install_wanted) {
            install_wanted = false;
            cancel_wanted = false;
            lock.unlock();
            run_install();
            lock.lock();
            continue;
        }
        const std::int64_t now = options.clock();
        const bool due = options.key_ready &&
                         auto_check_due(auto_check, now, last_ok_s, last_try_s);
        if (options.key_ready && (check_wanted || due)) {
            check_wanted = false;
            lock.unlock();
            run_check();
            lock.lock();
            continue;
        }
        check_wanted = false;
        cv.wait_for(lock, std::chrono::milliseconds(options.poll_ms),
                    [this] { return quit || check_wanted || install_wanted || ready_wanted; });
    }
}

UpdateService::UpdateService(UpdateOptions options, LinkOpener opener)
    : shared_(std::make_shared<Shared>(std::move(options))), opener_(std::move(opener)) {
    opens_browser_ = shared_->options.opens_browser && static_cast<bool>(opener_);
    shared_->load_state();
    ui::UpdateView first;
    if (!shared_->options.key_ready) {
        first.phase = ui::UpdatePhase::NotSetUp;
    } else if (shared_->last_was_current) {
        // The stored answer was "up to date" for this very version: show it with its time.
        first.phase = ui::UpdatePhase::UpToDate;
        first.last_checked_s = shared_->last_ok_s;
    } else if (shared_->last_was_available) {
        // An update was out. Its notes and files are not stored, so the card has no answer, but it
        // does not say "Not checked yet" over a time that is stored: it shows that time.
        first.last_checked_s = shared_->last_ok_s;
    }
    // Any other stored answer is not kept, so the card does not claim a time it has no answer for.
    // A version that starts after the helper went back to the old one says why.
    if (FailureText failure = shared_->last_update_failure(); !failure.full.empty()) {
        first.phase = ui::UpdatePhase::Failed;
        first.failure = std::move(failure.full);
        first.failure_summary = std::move(failure.summary);
    }
    shared_->latest = first;
    worker_ = spawn_thread([shared = shared_] { shared->work(); });
}

UpdateService::~UpdateService() {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->quit = true;
    }
    shared_->cv.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

const ui::UpdateView& UpdateService::update() {
    std::lock_guard lock(shared_->mutex);
    if (shown_version_ != shared_->serial) {
        shown_ = shared_->latest;
        shown_version_ = shared_->serial;
    }
    shown_.busy = shared_->busy;
    return shown_;
}

void UpdateService::check_now() {
    if (!shared_->options.key_ready) {
        return;
    }
    {
        std::lock_guard lock(shared_->mutex);
        shared_->check_wanted = true;
    }
    shared_->cv.notify_all();
}

void UpdateService::update_now() {
    const ui::UpdateView view = shared_->current();
    const bool retry = view.phase == ui::UpdatePhase::Failed && !view.retry_is_check;
    if (view.phase != ui::UpdatePhase::Available && !retry) {
        return;
    }
    if (view.phase == ui::UpdatePhase::Available && !view.installable) {
        if (!view.release_page.empty()) {
            open_link(view.release_page);
        }
        return;
    }
    {
        std::lock_guard lock(shared_->mutex);
        // Only from the idle screen, and only for what the last check offered.
        if (!shared_->found.valid || shared_->busy) {
            return;
        }
        shared_->install_wanted = true;
    }
    shared_->cv.notify_all();
}

void UpdateService::cancel() {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->cancel_wanted = true;
    }
    shared_->cv.notify_all();
}

void UpdateService::set_busy(bool busy) {
    bool changed = false;
    {
        std::lock_guard lock(shared_->mutex);
        changed = shared_->busy != busy;
        shared_->busy = busy;
    }
    if (changed) {
        shared_->cv.notify_all();
    }
}

void UpdateService::startup_progress(bool frame_shown, HistoryOpen history) {
    if (ready_gate_.settled() || !ready_gate_.update(frame_shown, history)) {
        if (ready_gate_.failed() && !ready_failure_logged_) {
            ready_failure_logged_ = true;
            log_printf( "update: the history did not open or migrate: not ready\n");
        }
        return;
    }
    {
        std::lock_guard lock(shared_->mutex);
        shared_->ready_wanted = true;
    }
    shared_->cv.notify_all();
}

void UpdateService::set_auto_check(bool on) {
    std::lock_guard lock(shared_->mutex);
    shared_->auto_check = on;
}

bool UpdateService::open_link(const std::string& url) {
    if (opens_browser_ && opener_(url)) {
        return true;
    }
    sheet_ = url;
    return false;
}

} // namespace etude::app
