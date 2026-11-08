// SPDX-License-Identifier: GPL-3.0-or-later
// Ties the UI's model to the settings file: reads it at startup, notices changes and saves them
// a moment after the last one, and once more on quit. The sections are [app], [session], [look]
// and [user_presets]; any other section or key in the file is written back untouched.
//
// A save is made on a thread of its own: the frame thread only hands over a copy of the model
// (into a buffer kept between saves), and the text is built and written there, so a frame never
// waits for the disk and a save allocates next to nothing on the frame thread. update() takes the
// outcome of a finished write on a later frame.
#pragma once

#include "settings_store.hpp"
#include "ui_model.hpp"

#include "etude/session_state.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace etude::app {

// Wait this long after the last change before writing, so dragging a slider is one write.
inline constexpr std::int64_t kSaveDebounceNs = 500'000'000;
inline constexpr std::int64_t kRetryDelayNs = 5'000'000'000; // after a failed write

// The renderer path the settings file in `directory` asks for (Basic when it holds none). Read
// before the window exists, so the renderer can be made for it; touches nothing on disk.
RendererPath saved_renderer(const std::filesystem::path& directory);

class Persistence {
  public:
    explicit Persistence(std::filesystem::path directory);
    // Finishes a save that is queued or under way, then ends the thread.
    ~Persistence();
    Persistence(const Persistence&) = delete;
    Persistence& operator=(const Persistence&) = delete;

    // Reads the settings file into `model` (the defaults for anything missing or damaged) and
    // remembers it as the saved state. Returns a short sentence for the status line, empty when all
    // was well. Never throws and never blocks startup.
    std::string load(UiModel& model);

    // Takes `model` as the saved state without writing: for startup overrides that are not meant
    // to reach the file by themselves.
    void adopt(const UiModel& model) {
        saved_ = model;
        dirty_ = false;
    }

    // Call every frame. Notices a change to `model` and starts writing it once `kSaveDebounceNs`
    // passed without another. Returns a message when a write that has finished failed, else empty.
    std::string update(const UiModel& model, std::int64_t now_ns);

    // Writes at once if anything is unsaved (on quit), and waits for it. Returns a message on
    // failure.
    std::string flush(const UiModel& model);

    // Waits until no write is under way and returns the message of one that failed, else empty
    // (the tests; flush() does it too). A failure is retried `kRetryDelayNs` after `now_ns`.
    std::string wait(std::int64_t now_ns = 0);

    const SettingsStore& store() const { return store_; }

    // The profile's part of the settings `model` holds (core profile_settings_text): what the
    // history keeps for the person who is leaving. Waits for a save under way.
    std::string profile_text(const UiModel& model);

    // Switches `model` to another person's settings: every per-profile key and section is taken
    // out and those in `text` (the profile's stored part; empty for a profile that never saved
    // any) are put in, so a key the profile does not hold is back at its default. The per-device
    // settings stay as they are. The next update() writes the file with them. Waits for a save under
    // way.
    void apply_profile(UiModel& model, std::string_view text);

    // A write has finished and update() or wait() has not taken its outcome yet.
    bool uncollected() const;

  private:
    friend struct PersistenceProbe; // the tests hand a model over at an exact moment
    // Hands `model` to the writer. False when the writer is still busy with the one before.
    bool submit(const UiModel& model);
    // The outcome of a write that finished since the last call: takes what it wrote as saved, or
    // schedules the retry. Returns the message for a failure not yet reported.
    std::string collect(std::int64_t now_ns);
    bool writing() const;
    void run();

    SettingsStore store_; // the writer's alone once the first save is submitted
    LoadedSettings loaded_; // the document as read, so unknown sections survive; read by the writer
    UiModel saved_;         // what the file holds as far as we know
    UiModel pending_;       // the unsaved state the debounce is waiting on
    bool dirty_ = false;
    std::int64_t changed_at_ns_ = 0;
    bool heal_ = false;      // the main file was missing or damaged and the backup was used
    bool read_only_ = false; // a newer major version: never write over it
    bool warned_failure_ = false;

    // The writer. `job_` is the model to write, kept between saves; the frame thread touches it
    // only while no write is queued or under way.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    UiModel job_;
    bool has_job_ = false;
    bool busy_ = false;
    bool stop_ = false;
    bool result_ready_ = false;
    bool result_ok_ = false;
    std::string result_error_;
    std::thread thread_; // started by the first save
};

// The text of the whole file for `model`, written into `base`: [app], [session], [look],
// [user_presets] replaced, everything else kept. Exposed for the tests.
std::string serialize_model(const LoadedSettings& base, const UiModel& model);
// The model a file holds; the defaults for what is missing.
UiModel model_from(const LoadedSettings& loaded);

// The profile in use that the settings file in `directory` names (the default profile when it
// names none or the file is missing); read before the history opens, touches nothing on disk.
std::int64_t saved_active_profile(const std::filesystem::path& directory);

} // namespace etude::app
