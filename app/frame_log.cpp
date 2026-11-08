// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_log.hpp"

#include <chrono>

namespace etude::app {

namespace {
// Only the main thread reads or writes the frame log.
FrameLog* g_active = nullptr;

double ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }
} // namespace

const char* frame_phase_name(FramePhase phase) {
    switch (phase) {
    case FramePhase::Wait:
        return "wait";
    case FramePhase::Events:
        return "events";
    case FramePhase::Update:
        return "update";
    case FramePhase::Scene:
        return "scene";
    case FramePhase::Ui:
        return "ui";
    case FramePhase::Tail:
        return "tail";
    case FramePhase::Present:
        return "present";
    case FramePhase::Count:
        break;
    }
    return "?";
}

const char* frame_detail_name(FrameDetail detail) {
    switch (detail) {
    case FrameDetail::Text:
        return "text";
    case FrameDetail::Texture:
        return "texture";
    case FrameDetail::Atlas:
        return "atlas";
    case FrameDetail::Save:
        return "save";
    case FrameDetail::Library:
        return "library";
    case FrameDetail::OpenSong:
        return "song";
    case FrameDetail::Shot:
        return "shot";
    case FrameDetail::Ports:
        return "ports";
    case FrameDetail::Window:
        return "window";
    case FrameDetail::Recovery:
        return "recovery";
    case FrameDetail::Count:
        break;
    }
    return "?";
}

FrameLog* active_frame_log() { return g_active; }
void set_active_frame_log(FrameLog* log) { g_active = log; }

void FrameLog::start(std::int64_t now_ns) {
    // Like the wall clock, the CPU time begins where the last frame's ended, so the pacing wait
    // in between belongs to the Wait phase; the very first frame begins now.
    start(now_ns, cpu_end_ns_ != 0 ? cpu_end_ns_ : thread_cpu_ns());
}

void FrameLog::start(std::int64_t now_ns, std::int64_t cpu_ns) {
    start_ns_ = now_ns;
    cursor_ns_ = now_ns;
    cpu_start_ns_ = cpu_ns;
    cpu_cursor_ns_ = cpu_ns;
    phases_.fill(0);
    details_.fill(0);
    phase_cpu_.fill(0);
}

void FrameLog::mark(FramePhase phase, std::int64_t now_ns) { mark(phase, now_ns, thread_cpu_ns()); }

void FrameLog::mark(FramePhase phase, std::int64_t now_ns, std::int64_t cpu_ns) {
    phases_[static_cast<std::size_t>(phase)] += now_ns - cursor_ns_;
    phase_cpu_[static_cast<std::size_t>(phase)] += cpu_ns - cpu_cursor_ns_;
    cursor_ns_ = now_ns;
    cpu_cursor_ns_ = cpu_ns;
}

bool FrameLog::finish(std::int64_t now_ns, std::int64_t frame_index) {
    return finish(now_ns, thread_cpu_ns(), frame_index);
}

bool FrameLog::finish(std::int64_t now_ns, std::int64_t cpu_ns, std::int64_t frame_index) {
    period_ns_ = now_ns - start_ns_;
    cpu_period_ns_ = cpu_ns - cpu_start_ns_;
    cpu_end_ns_ = cpu_ns;
    ++frames_;
    worst_ns_ = period_ns_ > worst_ns_ ? period_ns_ : worst_ns_;
    if (period_ns_ <= kFrameStallNs) {
        return false;
    }
    ++stalls_;
    const std::string text = line(frame_index);
    std::fprintf(out_, "%s\n", text.c_str());
    std::fflush(out_);
    return true;
}

std::string FrameLog::line(std::int64_t frame_index) const {
    std::string out = "stall frame " + std::to_string(frame_index) + ": ";
    char piece[64];
    std::snprintf(piece, sizeof piece, "%.1f ms ", ms(period_ns_));
    out += piece;
    for (std::size_t i = 0; i < phases_.size(); ++i) {
        std::snprintf(piece, sizeof piece, " %s %.1f", frame_phase_name(static_cast<FramePhase>(i)),
                      ms(phases_[i]));
        out += piece;
    }
    out += "  | inside:";
    for (std::size_t i = 0; i < details_.size(); ++i) {
        std::snprintf(piece, sizeof piece, " %s %.1f",
                      frame_detail_name(static_cast<FrameDetail>(i)), ms(details_[i]));
        out += piece;
    }
    // The frame thread's own CPU time, to tell a descheduled frame (wall long, CPU short) from a
    // slow one.
    std::snprintf(piece, sizeof piece, "  | cpu %.1f ms:", ms(cpu_period_ns_));
    out += piece;
    for (std::size_t i = 0; i < phase_cpu_.size(); ++i) {
        std::snprintf(piece, sizeof piece, " %s %.1f", frame_phase_name(static_cast<FramePhase>(i)),
                      ms(phase_cpu_[i]));
        out += piece;
    }
    return out;
}

void FrameLog::print_summary() const {
    std::fprintf(out_, "frame log: %lld frames, %lld over %.0f ms, worst %.1f ms\n",
                 static_cast<long long>(frames_), static_cast<long long>(stalls_),
                 ms(kFrameStallNs), ms(worst_ns_));
    std::fflush(out_);
}

} // namespace etude::app
