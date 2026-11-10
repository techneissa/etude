// SPDX-License-Identifier: GPL-3.0-or-later
// The stall log: a frame that took longer than kFrameStallNs prints one line with its time split by
// phase. The phases partition the frame; the details are work that happened inside one of them
// (a text or texture rebuild, a save, a library load) and are shown beside them. Switched on by
// --frame-log, --mic-log and --bench; while it is off a detail scope costs one pointer test.
// Each phase is also timed in the frame thread's CPU time, beside the wall clock: a frame that was
// descheduled while other programs ran has a long wall time and a short CPU time, a real
// regression has both.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace etude::app {

// A frame longer than this prints a line.
inline constexpr std::int64_t kFrameStallNs = 25'000'000;

// The frame's pieces in order; together they are the whole frame.
enum class FramePhase : std::uint8_t {
    Wait,    // the pacing sleep and the wait for a free back buffer, before the frame's work
    Events,  // the OS events
    Update,  // MIDI, microphone, song, layout: everything before drawing
    Scene,   // the falling notes and the keyboard
    Ui,      // the top row, bar and settings
    Tail,    // the settings save check and the scripted checks, between the UI and the present
    Present, // the present call
    Count,
};

// Work inside the phases above that is known to be slow now and then.
enum class FrameDetail : std::uint8_t {
    Text,     // a label rasterised again
    Texture,  // a cached texture built again
    Atlas,    // the UI font atlas grown or uploaded
    Save,     // the settings or the practice history written
    Library,  // the song library
    OpenSong, // a song opened (also counted in Library when the library's own step opened it)
    Shot,     // a scripted screenshot saved
    Ports,    // the MIDI ports looked at: inputs opened or closed, the piano's output port
    Window,   // a scripted window resize, maximise or fullscreen call, which the window system
              // answers
    Recovery, // the renderer made again after a lost device (a scripted --device-lost pretends one)
    Count,
};

// The calling thread's CPU time so far, in ns: one clock_gettime, no allocation (about a
// microsecond on macOS, less on Linux). Where the platform has no per-thread CPU clock it is the
// wall clock, so every frame's CPU time equals its wall time and nothing is excused.
inline std::int64_t thread_cpu_ns() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
        return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
    }
#endif
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

const char* frame_phase_name(FramePhase phase);
const char* frame_detail_name(FrameDetail detail);

class FrameLog {
  public:
    explicit FrameLog(std::FILE* out = stdout) : out_(out) {}

    // The frame began at `now_ns` (the end of the one before, so no time falls between frames).
    // The CPU time begins where the last frame's ended; the first frame starts it now.
    void start(std::int64_t now_ns);
    void start(std::int64_t now_ns, std::int64_t cpu_ns);
    // The time since the last mark (or the start) belongs to `phase`, in wall time and in the
    // frame thread's CPU time (read here unless given).
    void mark(FramePhase phase, std::int64_t now_ns);
    void mark(FramePhase phase, std::int64_t now_ns, std::int64_t cpu_ns);
    void add(FrameDetail detail, std::int64_t ns) {
        details_[static_cast<std::size_t>(detail)] += ns;
    }
    // The frame ended. Prints its line when it was a stall; returns whether it was.
    bool finish(std::int64_t now_ns, std::int64_t frame_index);
    bool finish(std::int64_t now_ns, std::int64_t cpu_ns, std::int64_t frame_index);

    // One line: "stall frame N: 568.1 ms  wait 0.2 events ... | in: text 0.0 ...".
    std::string line(std::int64_t frame_index) const;
    void print_summary() const;

    std::int64_t frames() const { return frames_; }
    std::int64_t stalls() const { return stalls_; }
    std::int64_t worst_ns() const { return worst_ns_; }

  private:
    std::FILE* out_;
    std::int64_t start_ns_ = 0;
    std::int64_t cursor_ns_ = 0;
    std::int64_t period_ns_ = 0;
    std::array<std::int64_t, static_cast<std::size_t>(FramePhase::Count)> phases_{};
    std::array<std::int64_t, static_cast<std::size_t>(FrameDetail::Count)> details_{};
    std::int64_t cpu_start_ns_ = 0;
    std::int64_t cpu_cursor_ns_ = 0;
    std::int64_t cpu_end_ns_ = 0; // where the last frame's CPU time ended; 0 before the first
    std::int64_t cpu_period_ns_ = 0;
    std::array<std::int64_t, static_cast<std::size_t>(FramePhase::Count)> phase_cpu_{};
    std::int64_t frames_ = 0;
    std::int64_t stalls_ = 0;
    std::int64_t worst_ns_ = 0;
};

// The log the details report to; null when the stall log is off.
FrameLog* active_frame_log();
void set_active_frame_log(FrameLog* log);

inline std::int64_t frame_clock_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Times its scope into the active log as `detail`. Reads no clock while the log is off.
class FrameDetailScope {
  public:
    // `enabled` false makes the scope do nothing.
    explicit FrameDetailScope(FrameDetail detail, bool enabled = true)
        : log_(enabled ? active_frame_log() : nullptr), detail_(detail) {
        if (log_ != nullptr) {
            start_ns_ = frame_clock_ns();
        }
    }
    ~FrameDetailScope() {
        if (log_ != nullptr) {
            log_->add(detail_, frame_clock_ns() - start_ns_);
        }
    }
    FrameDetailScope(const FrameDetailScope&) = delete;
    FrameDetailScope& operator=(const FrameDetailScope&) = delete;

  private:
    FrameLog* log_;
    FrameDetail detail_;
    std::int64_t start_ns_ = 0;
};

} // namespace etude::app
