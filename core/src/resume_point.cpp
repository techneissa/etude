// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/resume_point.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <sstream>

namespace etude {

namespace {

constexpr const char* kScreens[] = {"play",         "library",   "learn",     "learn-scales", "learn-chords",
                                    "learn-ear",    "learn-melodies", "notes", "effects",  "view",
                                    "practice",     "input",     "presets",   "add-songs", "about"};

const char* hands_name(HandFilter h) {
    return h == HandFilter::Left ? "left" : h == HandFilter::Right ? "right" : "both";
}

const char* plays_name(PianoPlays p) {
    switch (p) {
    case PianoPlays::Nothing:
        return "nothing";
    case PianoPlays::OtherHand:
        return "other";
    case PianoPlays::Left:
        return "left";
    case PianoPlays::Right:
        return "right";
    case PianoPlays::Both:
        return "both";
    }
    return "nothing";
}

std::int64_t to_ms(double seconds) {
    return static_cast<std::int64_t>(std::llround(std::clamp(seconds, 0.0, kMaxSessionSeconds) * 1000.0));
}

bool number(std::string_view value, std::int64_t& out) {
    const auto [end, err] = std::from_chars(value.data(), value.data() + value.size(), out);
    return err == std::errc() && end == value.data() + value.size();
}

bool plain_text(std::string_view value) {
    return value.size() <= kMaxSongPathBytes &&
           std::none_of(value.begin(), value.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

ResumeParse bad(int line, const std::string& why) {
    ResumeParse r;
    r.error = "line " + std::to_string(line) + ": " + why;
    return r;
}

} // namespace

bool resume_screen_known(std::string_view name) {
    return std::any_of(std::begin(kScreens), std::end(kScreens), [&](const char* s) { return name == s; });
}

std::string format_resume_point(const ResumePoint& p) {
    const SessionState& s = p.session;
    std::ostringstream out;
    out << "etude-resume " << kResumeVersion << "\n"
        << "ran_ms=" << p.ran_ms << "\n"
        << "clean=" << (p.clean ? 1 : 0) << "\n"
        << "screen=" << p.screen << "\n"
        << "song=" << s.song_path << "\n"
        << "song_abs=" << s.song_abs_path << "\n"
        << "hash=" << s.song_hash << "\n"
        << "position_ms=" << to_ms(s.position_s) << "\n"
        << "speed=" << s.speed_percent << "\n"
        << "hands=" << hands_name(s.practice_hands) << "\n"
        << "plays=" << plays_name(s.piano_plays) << "\n"
        << "loop_start_ms=" << to_ms(s.loop_start_s) << "\n"
        << "loop_end_ms=" << to_ms(s.loop_end_s) << "\n"
        << "loop=" << (s.loop_enabled ? 1 : 0) << "\n"
        << "section=" << s.section_id << "\n"
        << "plan_item=" << s.plan_item << "\n"
        << "count_in=" << s.count_in_bars << "\n"
        << "transpose=" << s.transpose_semitones << "\n"
        << "end\n";
    return out.str();
}

namespace {

enum class Field : std::uint8_t { Set, Invalid, Unknown };

bool flag(std::string_view value, bool& into) {
    std::int64_t n = 0;
    const bool good = number(value, n) && (n == 0 || n == 1);
    into = n == 1;
    return good;
}

bool plays(std::string_view value, PianoPlays& into) {
    if (value == "nothing") into = PianoPlays::Nothing;
    else if (value == "other") into = PianoPlays::OtherHand;
    else if (value == "left") into = PianoPlays::Left;
    else if (value == "right") into = PianoPlays::Right;
    else if (value == "both") into = PianoPlays::Both;
    else return false;
    return true;
}

// One `key=value` line of the file into the point.
Field set_field(ResumePoint& p, std::string_view key, std::string_view value) {
    SessionState& s = p.session;
    std::int64_t n = 0;
    const auto count = [&](std::int64_t lo, std::int64_t hi, auto& into) {
        if (!number(value, n) || n < lo || n > hi) {
            return false;
        }
        into = static_cast<std::remove_reference_t<decltype(into)>>(n);
        return true;
    };
    constexpr auto kMaxMs = static_cast<std::int64_t>(kMaxSessionSeconds * 1000.0);
    bool good = true;
    if (key == "ran_ms") {
        good = count(0, 1LL << 40, p.ran_ms);
    } else if (key == "clean") {
        good = flag(value, p.clean);
    } else if (key == "screen") {
        good = resume_screen_known(value);
        p.screen = std::string(value);
    } else if (key == "song") {
        good = plain_text(value);
        s.song_path = std::string(value);
    } else if (key == "song_abs") {
        good = plain_text(value);
        s.song_abs_path = std::string(value);
    } else if (key == "hash") {
        const auto [e, err] = std::from_chars(value.data(), value.data() + value.size(), s.song_hash);
        good = err == std::errc() && e == value.data() + value.size();
    } else if (key == "position_ms") {
        good = count(0, kMaxMs, n);
        s.position_s = static_cast<double>(n) / 1000.0;
    } else if (key == "speed") {
        good = count(25, 200, s.speed_percent);
    } else if (key == "hands") {
        good = value == "both" || value == "left" || value == "right";
        s.practice_hands = value == "left" ? HandFilter::Left : value == "right" ? HandFilter::Right : HandFilter::Both;
    } else if (key == "plays") {
        good = plays(value, s.piano_plays);
    } else if (key == "loop_start_ms") {
        good = count(0, kMaxMs, n);
        s.loop_start_s = static_cast<double>(n) / 1000.0;
    } else if (key == "loop_end_ms") {
        good = count(0, kMaxMs, n);
        s.loop_end_s = static_cast<double>(n) / 1000.0;
    } else if (key == "loop") {
        good = flag(value, s.loop_enabled);
    } else if (key == "section") {
        good = count(0, kMaxSessionSectionId, s.section_id);
    } else if (key == "plan_item") {
        good = count(0, kMaxSessionPlanItems, s.plan_item);
    } else if (key == "count_in") {
        good = count(0, kMaxCountInBars, s.count_in_bars);
    } else if (key == "transpose") {
        good = count(-12, 12, s.transpose_semitones);
    } else {
        return Field::Unknown;
    }
    return good ? Field::Set : Field::Invalid;
}

} // namespace

ResumeParse parse_resume_point(std::string_view text) {
    if (text.size() > kMaxResumeBytes) {
        return bad(1, "the file is too big");
    }
    ResumePoint p;
    bool ended = false;
    bool saw_header = false;
    int number_of_line = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            return bad(number_of_line + 1, "the last line has no end");
        }
        const std::string_view line = text.substr(at, end - at);
        at = end + 1;
        ++number_of_line;
        if (ended) {
            return bad(number_of_line, "text after the end");
        }
        if (!saw_header) {
            if (line != "etude-resume " + std::to_string(kResumeVersion)) {
                return bad(number_of_line, "not a resume point of this version");
            }
            saw_header = true;
            continue;
        }
        if (line == "end") {
            ended = true;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            return bad(number_of_line, "not a key=value line");
        }
        const std::string_view key = line.substr(0, eq);
        switch (set_field(p, key, line.substr(eq + 1))) {
        case Field::Set:
            break;
        case Field::Invalid:
            return bad(number_of_line, std::string(key) + " is not valid");
        case Field::Unknown:
            return bad(number_of_line, "unknown key " + std::string(key));
        }
    }
    if (!saw_header || !ended) {
        return bad(number_of_line + 1, "the file ends early");
    }
    ResumeParse r;
    r.point = std::move(p);
    r.ok = true;
    return r;
}

namespace {

// The components of a '/' path (a doubled or a trailing slash makes none); false when one is "." or "..".
bool components(std::string_view path, std::vector<std::string_view>& out) {
    std::size_t at = 0;
    while (at <= path.size()) {
        const std::size_t end = std::min(path.find('/', at), path.size());
        const std::string_view part = path.substr(at, end - at);
        if (part == "." || part == "..") {
            return false;
        }
        if (!part.empty()) {
            out.push_back(part);
        }
        at = end + 1;
    }
    return true;
}

bool under(std::string_view path, std::string_view root) {
    std::vector<std::string_view> p;
    std::vector<std::string_view> r;
    if (root.empty() || root.front() != '/' || path.empty() || path.front() != '/' || !components(root, r) || !components(path, p) || r.empty()) {
        return false;
    }
    return p.size() > r.size() && std::equal(r.begin(), r.end(), p.begin());
}

} // namespace

bool resume_song_allowed(const SessionState& session, const std::vector<std::string>& roots) {
    if (!session.song_path.empty()) {
        std::vector<std::string_view> parts;
        if (session.song_path.front() == '/' || session.song_path.find('\\') != std::string::npos ||
            !components(session.song_path, parts) || parts.empty()) {
            return false;
        }
    }
    if (!session.song_abs_path.empty()) {
        return std::any_of(roots.begin(), roots.end(),
                           [&](const std::string& root) { return under(session.song_abs_path, root); });
    }
    return true;
}

ResumeAction plan_resume(const ResumeParse* read, const std::vector<std::string>& roots) {
    if (read == nullptr || !read->ok) {
        return ResumeAction::Fresh;
    }
    const ResumePoint& p = read->point;
    if (!p.clean && p.ran_ms < std::int64_t{kResumeGuardSeconds} * 1000) {
        return ResumeAction::OpenSongList;
    }
    return resume_song_allowed(p.session, roots) ? ResumeAction::Resume : ResumeAction::SongRefused;
}

SessionState session_with_resume(const SessionState& settings, const ResumePoint& point) {
    SessionState s = settings;
    const SessionState& r = point.session;
    s.song_path = r.song_path;
    s.song_abs_path = r.song_abs_path;
    s.song_hash = r.song_hash;
    s.position_s = r.position_s;
    s.speed_percent = r.speed_percent;
    s.practice_hands = r.practice_hands;
    s.piano_plays = r.piano_plays;
    s.loop_start_s = r.loop_start_s;
    s.loop_end_s = r.loop_end_s;
    s.loop_enabled = r.loop_enabled;
    s.section_id = r.section_id;
    s.plan_item = r.plan_item;
    s.count_in_bars = r.count_in_bars;
    s.transpose_semitones = r.transpose_semitones;
    return s.clamped();
}

SessionState session_without_song(const SessionState& settings) {
    SessionState s = settings;
    const SessionState defaults;
    s.song_path.clear();
    s.song_abs_path.clear();
    s.song_hash = defaults.song_hash;
    s.position_s = defaults.position_s;
    s.speed_percent = defaults.speed_percent;
    s.count_in_bars = defaults.count_in_bars;
    s.practice_hands = defaults.practice_hands;
    s.piano_plays = defaults.piano_plays;
    s.loop_start_s = defaults.loop_start_s;
    s.loop_end_s = defaults.loop_end_s;
    s.loop_enabled = defaults.loop_enabled;
    s.transpose_semitones = defaults.transpose_semitones;
    s.section_id = defaults.section_id;
    s.plan_item = defaults.plan_item;
    return s.clamped();
}

bool ResumePacer::due(std::int64_t now_ms, bool changed, bool screen_changed) {
    bool write = false;
    if (!started_) {
        started_ = true;
        started_ms_ = now_ms;
        write = true;
    } else if (screen_changed) {
        write = now_ms - last_ms_ >= kResumeScreenGapMs;
    } else if (now_ms - started_ms_ < kResumeBeatMs) {
        write = now_ms - last_ms_ >= kResumeBeatEveryMs;
    } else {
        write = changed && now_ms - last_ms_ >= kResumeEveryMs;
    }
    if (write) {
        last_ms_ = now_ms;
    }
    return write;
}

} // namespace etude
