// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/session_state.hpp"

#include "etude/content_hash.hpp"
#include "etude/melody_added.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>

namespace etude {

namespace {

using Names = std::span<const std::string_view>;

constexpr std::array<std::string_view, 3> kHandNames{"both", "left", "right"};
constexpr std::array<std::string_view, 5> kPianoNames{"nothing", "other_hand", "left", "right",
                                                      "both"};
constexpr std::array<std::string_view, 3> kModeNames{"free_play", "wait", "flow"};
constexpr std::array<std::string_view, 3> kKeyboardNames{"auto", "profile", "manual"};
constexpr std::array<std::string_view, 3> kBarStyleNames{"strip", "groups", "dock"};
constexpr std::array<std::string_view, kBrowseSortCount> kLibrarySortNames{
    "name", "composer", "difficulty", "progress", "length", "recent", "added"};
constexpr std::array<std::string_view, kBrowseGroupCount> kLibraryGroupNames{
    "none", "composer", "difficulty", "progress", "genre", "my_groups"};
constexpr std::array<std::string_view, kBrowseFilterCount> kLibraryFilterNames{
    "all", "favourites", "recent", "in_progress"};
constexpr std::array<std::string_view, kBrowseHandsCount> kLibraryHandsNames{"any", "both",
                                                                             "right", "left"};
constexpr std::array<std::string_view, kBrowseViewCount> kLibraryViewNames{"grid", "list",
                                                                           "shelves"};
constexpr std::array<std::string_view, 2> kLearnPickerNames{"ring", "keys"};
constexpr std::array<std::string_view, 3> kLearnHandsNames{"both", "right", "left"};
constexpr std::array<std::string_view, 3> kLearnDirectionNames{"up", "down", "up_down"};
constexpr std::array<std::string_view, 4> kLearnEarModeNames{"note", "interval_up", "interval_down",
                                                             "together"};
constexpr std::array<std::string_view, 24> kLearnChordNames{
    "none",          "major",       "minor",          "diminished",   "augmented",
    "sus2",          "sus4",        "dominant7",      "major7",       "minor7",
    "half_diminished7", "diminished7", "minor_major7", "major6",      "minor6",
    "add9",          "dominant9",   "major9",         "minor9",       "power",
    "dominant7_sus4", "dominant7_flat9", "major69",   "dominant13"};
constexpr std::array<std::string_view, 2> kBadgeWallViewNames{"category", "rows"};
constexpr std::array<std::string_view, 3> kRunsChartViewNames{"bars", "line", "ranked"};
constexpr std::array<std::string_view, 14> kLearnScaleNames{
    "major",    "natural_minor",    "harmonic_minor",   "melodic_minor", "dorian",
    "phrygian", "lydian",           "mixolydian",       "locrian",       "major_pentatonic",
    "minor_pentatonic", "blues",    "chromatic",        "whole_tone"};
constexpr std::array<std::string_view, 3> kInputModeNames{"midi", "microphone", "both"};
constexpr std::array<std::string_view, 3> kViewModeNames{"falling", "sheet", "both"};
constexpr std::array<std::string_view, 3> kRendererNames{"basic", "gpu", "gl"};
constexpr std::array<std::string_view, kIconColourCount> kIconColourNames = [] {
    std::array<std::string_view, kIconColourCount> keys{};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        keys[i] = kIconColours[i].key;
    }
    return keys;
}();
constexpr std::array<std::string_view, 14> kScaleTypeNames{
    "major",     "natural_minor",    "harmonic_minor",   "melodic_minor",
    "dorian",    "phrygian",         "lydian",           "mixolydian",
    "locrian",   "major_pentatonic", "minor_pentatonic", "blues",
    "chromatic", "whole_tone"};
constexpr std::array<std::string_view, 3> kClickVoiceNames{"wood", "click", "beep"};
constexpr std::array<std::string_view, 3> kSubdivisionNames{"off", "divide", "triplets"};

constexpr int kMinHitWindowMs = 20;
constexpr int kMaxHitWindowMs = 250;
constexpr int kMaxLatencyCorrectionMs = 250;
constexpr int kMaxDailyGoalMinutes = 720;
constexpr int kMinSkipSeconds = 1;
constexpr int kMaxSkipSeconds = 30;
constexpr int kDefaultKeyboardLowest = 36;
constexpr int kDefaultKeyboardHighest = 96;

// Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
bool valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        auto b0 = static_cast<unsigned char>(s[i]);
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        std::uint32_t min = 0;
        if (b0 < 0x80) {
            ++i;
            continue;
        } else if (b0 >= 0xC2 && b0 <= 0xDF) {
            extra = 1;
            cp = b0 & 0x1F;
            min = 0x80;
        } else if (b0 >= 0xE0 && b0 <= 0xEF) {
            extra = 2;
            cp = b0 & 0x0F;
            min = 0x800;
        } else if (b0 >= 0xF0 && b0 <= 0xF4) {
            extra = 3;
            cp = b0 & 0x07;
            min = 0x10000;
        } else {
            return false;
        }
        if (i + extra >= s.size()) {
            return false; // cut off in the middle of a character
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (b & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

bool has_control(std::string_view s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7f;
    });
}

bool valid_name(std::string_view s, std::size_t max_bytes) {
    return s.size() <= max_bytes && valid_utf8(s) && !has_control(s);
}

// Library-relative: not rooted, no drive letter, no ".." segment (either slash).
bool valid_song_path(std::string_view s) {
    if (!valid_name(s, kMaxSongPathBytes)) {
        return false;
    }
    if (s.empty()) {
        return true;
    }
    if (s.front() == '/' || s.front() == '\\' || (s.size() >= 2 && s[1] == ':')) {
        return false;
    }
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = s.size();
        }
        if (s.substr(start, end - start) == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

// Rooted ('/' or '\\' first, or a drive letter and colon), no ".." segment.
bool valid_abs_song_path(std::string_view s) {
    if (!valid_name(s, kMaxSongPathBytes)) {
        return false;
    }
    if (s.empty()) {
        return true;
    }
    const bool rooted = s.front() == '/' || s.front() == '\\' ||
                        (s.size() >= 3 && s[1] == ':' && (s[2] == '/' || s[2] == '\\'));
    if (!rooted) {
        return false;
    }
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = s.size();
        }
        if (s.substr(start, end - start) == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

enum class TextKind { Name, SongPath, AbsSongPath };

bool valid_text(std::string_view s, std::size_t max_bytes, TextKind kind) {
    switch (kind) {
    case TextKind::SongPath:
        return valid_song_path(s);
    case TextKind::AbsSongPath:
        return valid_abs_song_path(s);
    case TextKind::Name:
        break;
    }
    return valid_name(s, max_bytes);
}

int clamp_int(int v, int lo, int hi, int step) {
    v = std::clamp(v, lo, hi);
    if (step > 1) {
        v = std::clamp((v + step / 2) / step * step, lo, hi);
    }
    return v;
}

// A value that is 0 ("off", "auto") or in lo..hi: under lo but not 0 is not a size, it counts as 0.
int clamp_zero_or(int v, int lo, int hi) { return v < lo ? 0 : std::min(v, hi); }

double clamp_real(double v, double lo, double hi) {
    if (std::isnan(v)) {
        return lo;
    }
    v = std::clamp(v, lo, hi);
    return v == 0.0 ? 0.0 : v; // no negative zero
}

// ---- The three visitors over the field list: clamp in place, read from a document, write to one.
// One list per struct, so the key names, ranges and order cannot differ between reading, writing
// and clamping.

struct Clamp {
    void integer(std::string_view, int& v, int lo, int hi, int step = 1) const {
        v = clamp_int(v, lo, hi, step);
    }
    void whole_percent(std::string_view, int& v, int lo, int hi) const { v = clamp_int(v, lo, hi, 1); }
    void zero_or(std::string_view, int& v, int lo, int hi) const { v = clamp_zero_or(v, lo, hi); }
    void real(std::string_view, double& v, double lo, double hi) const {
        v = clamp_real(v, lo, hi);
    }
    void boolean(std::string_view, bool&) const {}
    void text(std::string_view, std::string& v, std::size_t max, TextKind kind) const {
        if (!valid_text(v, max, kind)) {
            v.clear();
        }
    }
    void hash(std::string_view, std::uint64_t&) const {}
    void recent(std::string_view, RecentColours&) const {}
    template <class E> void choice(std::string_view, E& v, Names names) const {
        if (static_cast<std::size_t>(v) >= names.size()) {
            v = E{};
        }
    }
};

class Reader {
  public:
    Reader(const SettingsDocument& doc, std::string_view section,
           std::vector<SettingsIssue>* issues)
        : section_name_(section), issues_(issues), section_(doc.section(section)) {}

    void integer(std::string_view key, int& v, int lo, int hi, int step = 1) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        int parsed = 0;
        auto [ptr, ec] =
            std::from_chars(e->value.data(), e->value.data() + e->value.size(), parsed);
        if (ec != std::errc() || ptr != e->value.data() + e->value.size()) {
            bad(*e, "expected a whole number");
            return;
        }
        int fixed = clamp_int(parsed, lo, hi, step);
        if (fixed != parsed) {
            report(SettingsIssueKind::OutOfRange, *e,
                   "outside " + std::to_string(lo) + ".." + std::to_string(hi) + ", using " +
                       std::to_string(fixed));
        }
        v = fixed;
    }

    // 0, or a whole number in lo..hi: 1..lo-1 is not a value and is read as 0.
    void zero_or(std::string_view key, int& v, int lo, int hi) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        int parsed = 0;
        auto [ptr, ec] =
            std::from_chars(e->value.data(), e->value.data() + e->value.size(), parsed);
        if (ec != std::errc() || ptr != e->value.data() + e->value.size()) {
            bad(*e, "expected a whole number");
            return;
        }
        const int fixed = clamp_zero_or(parsed, lo, hi);
        if (fixed != parsed) {
            report(SettingsIssueKind::OutOfRange, *e,
                   "not 0 or " + std::to_string(lo) + ".." + std::to_string(hi) + ", using " +
                       std::to_string(fixed));
        }
        v = fixed;
    }

    // A whole percent, written as a number: a hand-edited 87.5 is read and rounded (88), where a
    // whole number is exact. Only text that is not a number keeps the default.
    void whole_percent(std::string_view key, int& v, int lo, int hi) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        double parsed = 0.0;
        auto [ptr, ec] =
            std::from_chars(e->value.data(), e->value.data() + e->value.size(), parsed);
        if (ec != std::errc() || ptr != e->value.data() + e->value.size() ||
            !std::isfinite(parsed)) {
            bad(*e, "expected a number");
            return;
        }
        const double rounded = std::round(clamp_real(parsed, static_cast<double>(lo),
                                                     static_cast<double>(hi)));
        const int fixed = static_cast<int>(rounded);
        if (static_cast<double>(fixed) != parsed) {
            report(SettingsIssueKind::OutOfRange, *e,
                   "not a whole percent inside " + std::to_string(lo) + ".." + std::to_string(hi) +
                       ", using " + std::to_string(fixed));
        }
        v = fixed;
    }

    // A key an older version wrote and this one reads under another name: known, so it is not
    // reported as unknown.
    void accept(std::string_view key) { known_.emplace_back(key); }

    void real(std::string_view key, double& v, double lo, double hi) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        double parsed = 0.0;
        auto [ptr, ec] =
            std::from_chars(e->value.data(), e->value.data() + e->value.size(), parsed);
        if (ec != std::errc() || ptr != e->value.data() + e->value.size() ||
            !std::isfinite(parsed)) {
            bad(*e, "expected a number");
            return;
        }
        double fixed = clamp_real(parsed, lo, hi);
        if (fixed != parsed) {
            report(SettingsIssueKind::OutOfRange, *e, "outside the valid range, clamped");
        }
        v = fixed;
    }

    void boolean(std::string_view key, bool& v) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        std::string lower;
        for (char c : e->value) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower == "true" || lower == "yes" || lower == "on" || lower == "1") {
            v = true;
        } else if (lower == "false" || lower == "no" || lower == "off" || lower == "0") {
            v = false;
        } else {
            bad(*e, "expected true or false");
        }
    }

    void text(std::string_view key, std::string& v, std::size_t max, TextKind kind) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        std::string parsed;
        if (!settings_unquote(e->value, parsed)) {
            bad(*e, "bad quoting");
            return;
        }
        if (!valid_text(parsed, max, kind)) {
            report(SettingsIssueKind::OutOfRange, *e,
                   kind == TextKind::SongPath      ? "not a library-relative path, ignored"
                   : kind == TextKind::AbsSongPath ? "not an absolute path, ignored"
                                                   : "too long or not UTF-8 text, ignored");
            v.clear();
            return;
        }
        v = std::move(parsed);
    }

    // "#RRGGBB" words, newest first. A word that is not a colour is dropped and reported; the
    // rest count, to `kRecentColours`.
    void recent(std::string_view key, RecentColours& v) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        std::string text;
        if (!settings_unquote(e->value, text)) {
            bad(*e, "bad quoting");
            return;
        }
        std::vector<RGBA> found;
        bool dropped = false;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t end = std::min(text.find(' ', pos), text.size());
            if (end > pos) {
                RGBA c;
                if (found.size() < kRecentColours && parse_hex(text.substr(pos, end - pos), &c)) {
                    found.push_back(c);
                } else {
                    dropped = true;
                }
            }
            pos = end + 1;
        }
        if (dropped) {
            report(SettingsIssueKind::BadValue, *e,
                   "some words are not colours or are past the sixteenth, ignored");
        }
        RecentColours out;
        for (auto it = found.rbegin(); it != found.rend(); ++it) {
            out.add(*it);
        }
        v = out;
    }

    void hash(std::string_view key, std::uint64_t& v) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        std::uint64_t parsed = 0;
        if (!parse_content_hash_hex(e->value, parsed)) {
            bad(*e, "expected up to 16 hex digits");
            return;
        }
        v = parsed;
    }

    template <class E> void choice(std::string_view key, E& v, Names names) {
        const SettingsEntry* e = find(key);
        if (e == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == e->value) {
                v = static_cast<E>(i);
                return;
            }
        }
        bad(*e, "not one of the known names");
    }

    // After the fields: report keys and lines nothing claimed.
    void finish() {
        for (const SettingsEntry& e : section_.entries) {
            if (std::find(known_.begin(), known_.end(), e.key) == known_.end()) {
                report(SettingsIssueKind::UnknownKey, e, "unknown key, kept as it is");
            }
        }
        for (int line : section_.malformed_lines) {
            if (issues_ != nullptr) {
                issues_->push_back({SettingsIssueKind::MalformedLine, line,
                                    std::string(section_name_), "", "not a key = value line"});
            }
        }
    }

  private:
    // The last entry with this key; a repeated key is reported once.
    const SettingsEntry* find(std::string_view key) {
        known_.emplace_back(key);
        const SettingsEntry* found = nullptr;
        int count = 0;
        for (const SettingsEntry& e : section_.entries) {
            if (e.key == key) {
                found = &e;
                ++count;
            }
        }
        if (count > 1) {
            report(SettingsIssueKind::DuplicateKey, *found, "repeated key, the last one counts");
        }
        return found;
    }
    void bad(const SettingsEntry& e, const std::string& what) {
        report(SettingsIssueKind::BadValue, e, what + ", keeping the default");
    }
    void report(SettingsIssueKind kind, const SettingsEntry& e, std::string detail) {
        if (issues_ != nullptr) {
            issues_->push_back(
                {kind, e.line, std::string(section_name_), e.key, std::move(detail)});
        }
    }

    std::string_view section_name_;
    std::vector<SettingsIssue>* issues_;
    SettingsSection section_;
    std::vector<std::string> known_;
};

class Writer {
  public:
    Writer(SettingsDocument& doc, std::string_view section) : doc_(doc), section_(section) {}

    void integer(std::string_view key, const int& v, int, int, int = 1) {
        doc_.set(section_, key, std::to_string(v));
    }
    void zero_or(std::string_view key, const int& v, int, int) {
        doc_.set(section_, key, std::to_string(v));
    }
    void whole_percent(std::string_view key, const int& v, int, int) {
        doc_.set(section_, key, std::to_string(v));
    }
    void real(std::string_view key, const double& v, double, double) {
        char buf[64];
        auto r = std::to_chars(buf, buf + sizeof buf, v);
        doc_.set(section_, key, std::string_view(buf, static_cast<std::size_t>(r.ptr - buf)));
    }
    void boolean(std::string_view key, const bool& v) {
        doc_.set(section_, key, v ? "true" : "false");
    }
    void text(std::string_view key, const std::string& v, std::size_t, TextKind) {
        doc_.set(section_, key, settings_quote(v));
    }
    void recent(std::string_view key, const RecentColours& v) {
        std::string text;
        for (const RGBA c : v.items()) {
            text += text.empty() ? "" : " ";
            text += to_hex(c);
        }
        doc_.set(section_, key, settings_quote(text));
    }
    void hash(std::string_view key, const std::uint64_t& v) {
        doc_.set(section_, key, content_hash_hex(v));
    }
    template <class E> void choice(std::string_view key, const E& v, Names names) {
        doc_.set(section_, key, names[static_cast<std::size_t>(v)]);
    }

  private:
    SettingsDocument& doc_;
    std::string_view section_;
};

template <class V, class S> void visit_session(V& v, S& s) {
    v.text("song", s.song_path, kMaxSongPathBytes, TextKind::SongPath);
    v.text("song_abs", s.song_abs_path, kMaxSongPathBytes, TextKind::AbsSongPath);
    v.hash("song_hash", s.song_hash);
    v.real("position_s", s.position_s, 0.0, kMaxSessionSeconds);
    v.whole_percent("speed_percent", s.speed_percent, kMinSpeedPercent, kMaxSpeedPercent);
    v.choice("hands", s.practice_hands, Names(kHandNames));
    v.choice("piano_plays", s.piano_plays, Names(kPianoNames));
    v.real("loop_start_s", s.loop_start_s, 0.0, kMaxSessionSeconds);
    v.real("loop_end_s", s.loop_end_s, 0.0, kMaxSessionSeconds);
    v.boolean("loop_enabled", s.loop_enabled);
    v.boolean("auto_speed_enabled", s.auto_speed_enabled);
    v.integer("auto_speed_start_percent", s.auto_speed_start_percent, kMinSpeedPercent,
              kMaxSpeedPercent, kSpeedStepPercent);
    v.integer("auto_speed_step_percent", s.auto_speed_step_percent, kSpeedStepPercent, 25,
              kSpeedStepPercent);
    v.integer("auto_speed_max_percent", s.auto_speed_max_percent, kMinSpeedPercent,
              kMaxSpeedPercent, kSpeedStepPercent);
    v.integer("count_in_bars", s.count_in_bars, 0, kMaxCountInBars);
    v.choice("mode", s.mode, Names(kModeNames));
    v.integer("transpose_semitones", s.transpose_semitones, -12, 12);
    v.integer("section_id", s.section_id, 0, kMaxSessionSectionId);
    v.integer("plan_item", s.plan_item, 0, kMaxSessionPlanItems);
    v.choice("keyboard", s.keyboard, Names(kKeyboardNames));
    v.text("keyboard_name", s.keyboard_name, kMaxNameBytes, TextKind::Name);
    v.integer("keyboard_lowest", s.keyboard_lowest, 0, 127);
    v.integer("keyboard_highest", s.keyboard_highest, 0, 127);
}

template <class V, class S> void visit_app(V& v, S& s) {
    v.integer("hit_window_ms", s.hit_window_ms, kMinHitWindowMs, kMaxHitWindowMs);
    v.integer("latency_correction_ms", s.latency_correction_ms, -kMaxLatencyCorrectionMs,
              kMaxLatencyCorrectionMs);
    v.text("midi_in_port", s.midi_in_port, kMaxNameBytes, TextKind::Name);
    v.text("midi_out_port", s.midi_out_port, kMaxNameBytes, TextKind::Name);
    v.integer("daily_goal_minutes", s.daily_goal_minutes, 0, kMaxDailyGoalMinutes);
    v.integer("resume_countdown_s", s.resume_countdown_s, kMinResumeCountdownS,
              kMaxResumeCountdownS);
    v.integer("skip_back_seconds", s.skip_back_seconds, kMinSkipSeconds, kMaxSkipSeconds);
    v.integer("skip_forward_seconds", s.skip_forward_seconds, kMinSkipSeconds, kMaxSkipSeconds);
    v.choice("bar_style", s.bar_style, Names(kBarStyleNames));
    v.boolean("metronome", s.metronome);
    v.choice("metronome_sound", s.metronome_sound, Names(kClickVoiceNames));
    v.integer("metronome_volume", s.metronome_volume, kMinClickVolume, kMaxClickVolume);
    v.boolean("metronome_accent", s.metronome_accent);
    v.choice("metronome_subdivision", s.metronome_subdivision, Names(kSubdivisionNames));
    v.boolean("metronome_pulse", s.metronome_pulse);
    v.boolean("accompaniment", s.accompaniment);
    v.integer("accompaniment_volume", s.accompaniment_volume, 0, kMaxAccompanimentVolume);
    v.boolean("bar_pinned", s.bar_pinned);
    v.boolean("band_difficulty", s.band_difficulty);
    v.boolean("weighted_score", s.weighted_score);
    v.integer("run_events_kept", s.run_events_kept, 0, kMaxRunEventsKept);
    v.choice("view_mode", s.view_mode, Names(kViewModeNames));
    v.choice("renderer", s.renderer, Names(kRendererNames));
    v.choice("input_source", s.input_mode, Names(kInputModeNames));
    v.text("mic_device", s.mic_device, kMaxNameBytes, TextKind::Name);
    v.boolean("mic_learn_tuning", s.mic_learn_tuning);
    v.integer("mic_a4_tenths", s.mic_a4_tenths, kMinMicA4Tenths, kMaxMicA4Tenths);
    v.integer("mic_noise_floor_ppm", s.mic_noise_floor_ppm, 0, kMaxMicNoiseFloorPpm);
    v.boolean("abc_chords", s.abc_chords);
    v.zero_or("screen_width_mm", s.screen_width_mm, kMinScreenWidthMm, kMaxScreenWidthMm);
    v.boolean("scale_overlay", s.scale_overlay);
    v.integer("scale_root", s.scale_root, 0, 11);
    v.choice("scale_type", s.scale_type, Names(kScaleTypeNames));
    v.boolean("scale_fingers", s.scale_fingers);
    v.boolean("device_check_done", s.device_check_done);
    v.text("device_check_seen", s.device_check_seen, kMaxDeviceCheckSeenBytes, TextKind::Name);
    v.boolean("update_auto_check", s.update_auto_check);
    v.choice("icon_colour", s.icon_colour, Names(kIconColourNames));
    v.integer("active_profile", s.active_profile, 1, kMaxActiveProfile);
    v.recent("recent_colors", s.recent_colors);
    v.choice("library_sort", s.library.sort, Names(kLibrarySortNames));
    v.boolean("library_sort_reversed", s.library.reversed);
    v.choice("library_group", s.library.group, Names(kLibraryGroupNames));
    v.choice("library_filter", s.library.filter, Names(kLibraryFilterNames));
    v.integer("library_levels", s.library.levels, 0, kLevelBits);
    v.choice("library_hands", s.library.hands, Names(kLibraryHandsNames));
    v.choice("library_view", s.library_view, Names(kLibraryViewNames));
    v.integer("library_scroll", s.library_scroll, 0, kMaxLibraryScroll);
    v.boolean("get_songs_online", s.get_songs_online);
    v.boolean("group_versions", s.group_versions);
    v.choice("learn_picker", s.learn_picker, Names(kLearnPickerNames));
    v.integer("learn_root", s.learn_root, 0, 11);
    v.choice("learn_scale", s.learn_scale, Names(kLearnScaleNames));
    v.choice("learn_hands", s.learn_hands, Names(kLearnHandsNames));
    v.integer("learn_tempo", s.learn_tempo, 40, 200);
    v.choice("learn_direction", s.learn_direction, Names(kLearnDirectionNames));
    v.integer("learn_octaves", s.learn_octaves, 1, 2);
    v.boolean("learn_arpeggio", s.learn_arpeggio);
    v.choice("learn_ear_mode", s.learn_ear_mode, Names(kLearnEarModeNames));
    v.integer("learn_ear_octave", s.learn_ear_octave, 1, 6);
    v.boolean("learn_ear_any_octave", s.learn_ear_any_octave);
    v.integer("learn_ear_max", s.learn_ear_max, 2, 12);
    v.integer("learn_chord_root", s.learn_chord_root, 0, 11);
    v.choice("learn_chord", s.learn_chord, Names(kLearnChordNames));
    v.integer("learn_inversion", s.learn_inversion, 0, 4);
    v.text("melody_added", s.melody_added, kMaxMelodyAddedBytes, TextKind::Name);
    v.choice("badge_wall_view", s.badge_wall_view, Names(kBadgeWallViewNames));
    v.choice("runs_chart_view", s.runs_chart_view, Names(kRunsChartViewNames));
}

} // namespace

SessionState SessionState::clamped() const {
    SessionState s = *this;
    Clamp clamp;
    visit_session(clamp, s);

    if (!s.has_song()) {
        s.song_hash = 0;
        s.position_s = 0.0;
        s.loop_start_s = s.loop_end_s = 0.0;
        s.section_id = 0;
        s.plan_item = 0;
    }
    if (s.loop_start_s > s.loop_end_s) {
        std::swap(s.loop_start_s, s.loop_end_s);
    }
    if (!s.has_loop()) {
        s.loop_start_s = s.loop_end_s = 0.0;
        s.loop_enabled = false;
    }
    s.auto_speed_max_percent = std::max(s.auto_speed_max_percent, s.auto_speed_start_percent);

    int lowest = s.keyboard_lowest;
    int highest = s.keyboard_highest;
    if (lowest > highest) {
        std::swap(lowest, highest);
    }
    if (is_black_key(lowest)) {
        --lowest; // notes 0 and 127 are white, so a black key has a white neighbour inside 0..127
    }
    if (is_black_key(highest)) {
        ++highest;
    }
    if (highest - lowest + 1 < kMinKeyboardKeys) {
        lowest = kDefaultKeyboardLowest;
        highest = kDefaultKeyboardHighest;
        if (s.keyboard == KeyboardChoice::Manual) {
            s.keyboard = KeyboardChoice::Auto;
        }
    }
    s.keyboard_lowest = lowest;
    s.keyboard_highest = highest;
    if (s.keyboard == KeyboardChoice::Profile && s.keyboard_name.empty()) {
        s.keyboard = KeyboardChoice::Auto;
    }
    return s;
}

AppSettings AppSettings::clamped() const {
    AppSettings s = *this;
    Clamp clamp;
    visit_app(clamp, s);
    if (s.learn_chord == ChordQuality::None) {
        s.learn_chord = ChordQuality::Major; // the names have "none" to keep the enum's places
    }
    return s;
}

namespace {

// Older versions wrote `metronome` and `count_in_beats` in [session]. The metronome is now an [app]
// setting and the count-in counts bars. They are read once from the old place when the new one has
// nothing, never reported as unknown keys, and dropped on the next save (write_session).
constexpr std::string_view kLegacyMetronome = "metronome";
constexpr std::string_view kLegacyCountInBeats = "count_in_beats";
constexpr int kLegacyMaxCountInBeats = 16;

bool has_key(const SettingsDocument& doc, std::string_view section, std::string_view key) {
    for (const SettingsEntry& e : doc.section(section).entries) {
        if (e.key == key) {
            return true;
        }
    }
    return false;
}

} // namespace

SessionState read_session(const SettingsDocument& doc, std::vector<SettingsIssue>* issues) {
    SessionState s;
    Reader reader(doc, kSessionSection, issues);
    visit_session(reader, s);
    // The count-in was in beats, 0 for off: any beats become one bar, 0 stays off.
    if (!has_key(doc, kSessionSection, "count_in_bars") &&
        has_key(doc, kSessionSection, kLegacyCountInBeats)) {
        int beats = 0;
        reader.integer(kLegacyCountInBeats, beats, 0, kLegacyMaxCountInBeats);
        s.count_in_bars = beats == 0 ? 0 : 1;
    }
    reader.accept(kLegacyCountInBeats);
    reader.accept(kLegacyMetronome);
    reader.finish();
    SessionState fixed = s.clamped();
    if (!(fixed == s) && issues != nullptr && doc.has_section(kSessionSection)) {
        issues->push_back({SettingsIssueKind::OutOfRange, 0, std::string(kSessionSection), "",
                           "some values clash with each other and were adjusted"});
    }
    return fixed;
}

AppSettings read_app_settings(const SettingsDocument& doc, std::vector<SettingsIssue>* issues) {
    AppSettings s;
    Reader reader(doc, kAppSection, issues);
    visit_app(reader, s);
    reader.finish();
    // The metronome was a [session] setting: take it from there when [app] has none yet.
    if (!has_key(doc, kAppSection, "metronome") && has_key(doc, kSessionSection, kLegacyMetronome)) {
        Reader old(doc, kSessionSection, nullptr);
        old.boolean(kLegacyMetronome, s.metronome);
    }
    return s.clamped();
}

void write_session(SettingsDocument& doc, const SessionState& state) {
    const SessionState s = state.clamped();
    Writer writer(doc, kSessionSection);
    visit_session(writer, s);
    doc.remove(kSessionSection, kLegacyMetronome);
    doc.remove(kSessionSection, kLegacyCountInBeats);
}

void write_app_settings(SettingsDocument& doc, const AppSettings& settings) {
    const AppSettings s = settings.clamped();
    Writer writer(doc, kAppSection);
    visit_app(writer, s);
}

std::string LoadedSettings::serialize() const {
    SettingsDocument out = document;
    out.ensure_format();
    write_app_settings(out, app);
    write_session(out, session);
    return out.serialize();
}

LoadedSettings load_settings(std::string_view text,
                             std::span<const std::string_view> other_known_sections) {
    LoadedSettings loaded;
    loaded.document = SettingsDocument::parse(text, &loaded.issues);
    loaded.app = read_app_settings(loaded.document, &loaded.issues);
    loaded.session = read_session(loaded.document, &loaded.issues);
    for (const std::string& name : loaded.document.section_names()) {
        bool known = name == kAppSection || name == kSessionSection ||
                     std::find(other_known_sections.begin(), other_known_sections.end(), name) !=
                         other_known_sections.end();
        if (!known) {
            loaded.issues.push_back({SettingsIssueKind::UnknownSection,
                                     loaded.document.section(name).header_line, name, "",
                                     "unknown section, kept as it is"});
        }
    }
    return loaded;
}

} // namespace etude
