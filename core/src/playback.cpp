// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/playback.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace etude {
namespace {

constexpr int kBendCentre = 8192;
constexpr int kBankMsb = 0;
constexpr int kBankLsb = 32;
constexpr int kSustain = 64;
constexpr int kResetControllers = 121;
constexpr int kAllNotesOff = 123;
// Controllers from 120 up are channel mode messages, not settings a seek could restore.
constexpr int kFirstModeController = 120;

// General MIDI power-on values.
int controller_default(int controller) {
    switch (controller) {
    case 7:
        return 100;
    case 10:
        return 64;
    case 11:
        return 127;
    case 91:
        return 40;
    default:
        return 0;
    }
}

constexpr double kNever = std::numeric_limits<double>::infinity();

} // namespace

// Applies one file event to what the file has set.
void Playback::apply_file_event(const ChannelEvent& e, FileState& state) {
    FileChannel& w = state[e.channel & 0x0F];
    switch (e.kind) {
    case ChannelEventKind::Program:
        w.program = static_cast<std::int16_t>(e.value & 0x7F);
        break;
    case ChannelEventKind::PitchBend:
        w.bend = static_cast<std::int16_t>(e.value & 0x3FFF);
        break;
    case ChannelEventKind::Control:
        if (e.number < kFirstModeController) {
            w.cc[e.number] = static_cast<std::int8_t>(e.value & 0x7F);
        }
        break;
    }
}

Playback::~Playback() {
    if (teardown_.check != nullptr && needs_silence()) {
        teardown_.check(*this);
    }
}

bool Playback::needs_silence() const {
    if (!active_.empty() || used_channels_ != 0) {
        return true;
    }
    return std::any_of(sent_.begin(), sent_.end(),
                       [](const ChannelState& c) { return c.cc[kSustain] > 0; });
}

std::shared_ptr<const Playback::Tables> Playback::build_tables(const Song& song,
                                                               std::uint64_t fingerprint) {
    auto t = std::make_shared<Tables>();
    t->fingerprint = fingerprint;
    t->parts = std::min<std::size_t>(song.parts.size(), kNoPart);
    std::vector<Item>& notes = t->notes;
    notes.reserve(song.notes.size());
    for (const Note& n : song.notes) {
        if (!std::isfinite(n.start_s)) {
            continue; // nowhere to put it
        }
        std::uint16_t part = kNoPart;
        if (n.accompaniment) {
            for (std::size_t i = 0; i < song.parts.size() && i < kNoPart; ++i) {
                if (song.parts[i].track == n.track && song.parts[i].channel == n.channel) {
                    part = static_cast<std::uint16_t>(i);
                    break;
                }
            }
        }
        double end_s = n.start_s + n.duration_s;
        if (!(end_s > n.start_s) || !std::isfinite(end_s)) {
            end_s = n.start_s + kMinNoteSeconds;
        }
        notes.push_back({n.start_s, end_s, static_cast<std::uint8_t>(n.channel & 0x0F),
                         static_cast<std::uint8_t>(n.pitch & 0x7F),
                         static_cast<std::uint8_t>(std::max<int>(1, n.velocity & 0x7F)), n.hand,
                         n.drum, n.accompaniment, part});
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const Item& a, const Item& b) { return a.start_s < b.start_s; });
    t->by_end.resize(notes.size());
    std::iota(t->by_end.begin(), t->by_end.end(), std::uint32_t{0});
    std::stable_sort(t->by_end.begin(), t->by_end.end(),
                     [&notes](std::uint32_t a, std::uint32_t b) {
                         return notes[a].end_s < notes[b].end_s;
                     });
    t->events = song.events;
    std::stable_sort(
        t->events.begin(), t->events.end(),
        [](const ChannelEvent& a, const ChannelEvent& b) { return a.time_s < b.time_s; });
    FileState state;
    for (std::size_t i = 0; i <= t->events.size(); ++i) {
        if (i % kCheckpointEvents == 0) {
            t->checkpoints.push_back(state);
        }
        if (i < t->events.size()) {
            apply_file_event(t->events[i], state);
        }
    }
    return t;
}

const std::shared_ptr<const Playback::Tables>& Playback::empty_tables() {
    static const std::shared_ptr<const Tables> empty = [] {
        auto t = std::make_shared<Tables>();
        t->checkpoints.emplace_back(); // a song with no events has the one, empty, checkpoint
        t->fingerprint = song_fingerprint(Song{});
        return std::shared_ptr<const Tables>(std::move(t));
    }();
    return empty;
}

void Playback::adopt(std::shared_ptr<const Tables> tables) {
    t_ = std::move(tables);
    next_on_ = next_off_ = next_event_ = 0;
    active_.clear();
    running_ = false;
    position_s_ = 0.0;
    used_channels_ = 0;
    part_muted_.clear(); // indices of the old song's parts
}

void Playback::load(const Song& song) { adopt(build_tables(song, song_fingerprint(song))); }

std::size_t Playback::Prepared::note_count() const {
    return tables_ ? tables_->notes.size() : 0;
}

Playback::Prepared Playback::prepare(const Song& song) {
    return prepare(song, song_fingerprint(song));
}

Playback::Prepared Playback::prepare(const Song& song, std::uint64_t fingerprint) {
    Prepared p;
    p.tables_ = build_tables(song, fingerprint);
    return p;
}

void Playback::set_song(const Song& song, std::int64_t now_ns, std::vector<PlaybackMessage>& out) {
    silence(now_ns, out);
    load(song);
}

void Playback::set_song(const Song& song, std::int64_t now_ns, std::vector<PlaybackMessage>& out,
                        const Prepared& prepared, std::uint64_t fingerprint) {
    silence(now_ns, out);
    if (prepared.tables_ && prepared.tables_->fingerprint == fingerprint) {
        adopt(prepared.tables_);
    } else {
        adopt(build_tables(song, fingerprint));
    }
}

void Playback::drop_song(std::int64_t now_ns, std::vector<PlaybackMessage>& out) {
    silence(now_ns, out);
    adopt(empty_tables());
}

void Playback::send(PlaybackMessage::Kind kind, std::uint8_t channel, std::uint8_t data,
                    std::uint16_t value, std::int64_t at_ns, double song_s,
                    std::vector<PlaybackMessage>& out) {
    out.push_back({kind, channel, data, value, at_ns, song_s});
    ChannelState& state = sent_[channel & 0x0F];
    switch (kind) {
    case PlaybackMessage::Kind::ProgramChange:
        state.program = data;
        break;
    case PlaybackMessage::Kind::PitchBend:
        state.bend = value;
        break;
    case PlaybackMessage::Kind::ControlChange:
        if (data < kFirstModeController) {
            state.cc[data] = value;
        }
        break;
    default:
        break;
    }
}

void Playback::send_event(const ChannelEvent& e, std::int64_t at_ns,
                          std::vector<PlaybackMessage>& out) {
    const auto channel = static_cast<std::uint8_t>(e.channel & 0x0F);
    switch (e.kind) {
    case ChannelEventKind::Program:
        send(PlaybackMessage::Kind::ProgramChange, channel,
             static_cast<std::uint8_t>(e.value & 0x7F), 0, at_ns, e.time_s, out);
        break;
    case ChannelEventKind::PitchBend:
        send(PlaybackMessage::Kind::PitchBend, channel, 0,
             static_cast<std::uint16_t>(e.value & 0x3FFF), at_ns, e.time_s, out);
        break;
    case ChannelEventKind::Control: {
        const auto number = static_cast<std::uint8_t>(e.number & 0x7F);
        send(PlaybackMessage::Kind::ControlChange, channel, number,
             static_cast<std::uint16_t>(e.value & 0x7F), at_ns, e.time_s, out);
        if (number == kResetControllers) {
            // The piano put its controllers back to their defaults, bank and program excepted.
            ChannelState& state = sent_[channel];
            for (int n = 1; n < kFirstModeController; ++n) {
                if (n != kBankLsb && state.cc[static_cast<std::size_t>(n)] >= 0) {
                    state.cc[static_cast<std::size_t>(n)] = controller_default(n);
                }
            }
            if (state.bend >= 0) {
                state.bend = kBendCentre;
            }
        }
        break;
    }
    }
}

bool Playback::muted(const Item& n) const {
    return n.drum            ? drums_muted_
           : n.accompaniment ? accompaniment_muted_ || accompaniment_volume_ <= 0 ||
                                   part_muted(n.part)
                             : hand_muted_[static_cast<std::size_t>(n.hand)];
}

int Playback::sounded_pitch(const Item& n) const {
    return transposition_.map_note(n.pitch, n.drum).pitch;
}

void Playback::release(std::size_t active_slot, std::int64_t at_ns, double song_s,
                       std::vector<PlaybackMessage>& out) {
    const Item note = t_->notes[active_[active_slot]];
    const int key = sounded_pitch(note); // only notes with a key are ever active
    active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(active_slot));
    const bool still_sounding = std::any_of(active_.begin(), active_.end(), [&](std::uint32_t i) {
        return t_->notes[i].channel == note.channel && sounded_pitch(t_->notes[i]) == key;
    });
    if (!still_sounding) {
        send(PlaybackMessage::Kind::NoteOff, note.channel, static_cast<std::uint8_t>(key), 64,
             at_ns, song_s, out);
    }
}

// Stops everything: a note-off per sounding note, the pedal up on every channel where it is down (a
// pedal held down would keep the notes ringing), and all-notes-off on every channel that has played
// since the last panic.
void Playback::panic(std::int64_t at_ns, double song_s, std::vector<PlaybackMessage>& out) {
    while (!active_.empty()) {
        release(active_.size() - 1, at_ns, song_s, out);
    }
    for (std::uint8_t ch = 0; ch < 16; ++ch) {
        // Any channel, not only those with notes: the file may hold the pedal on a channel whose
        // notes come later, and a half-down pedal still sustains.
        if (sent_[ch].cc[kSustain] > 0) {
            send(PlaybackMessage::Kind::ControlChange, ch, kSustain, 0, at_ns, song_s, out);
        }
        if ((used_channels_ & (1u << ch)) != 0) {
            send(PlaybackMessage::Kind::ControlChange, ch, kAllNotesOff, 0, at_ns, song_s, out);
        }
    }
    used_channels_ = 0;
}

void Playback::silence(std::int64_t now_ns, std::vector<PlaybackMessage>& out) {
    panic(now_ns, position_s_, out);
}

// Sends what differs between what the piano has and what the file says at `position_s`.
void Playback::restore_state(double position_s, std::int64_t at_ns,
                             std::vector<PlaybackMessage>& out) {
    // Events at or before the position: start from the checkpoint before them.
    const auto end = static_cast<std::size_t>(
        std::upper_bound(t_->events.begin(), t_->events.end(), position_s,
                         [](double t, const ChannelEvent& e) { return t < e.time_s; }) -
        t_->events.begin());
    const std::size_t first = end / kCheckpointEvents * kCheckpointEvents;
    FileState want = t_->checkpoints[end / kCheckpointEvents];
    for (std::size_t i = first; i < end; ++i) {
        apply_file_event(t_->events[i], want);
    }
    last_restore_replayed_ = end - first;
    // What to send: the file's value, or the default for a setting the piano has been given that
    // the file only sets later; nothing for a setting nobody has touched.
    auto goal = [](int wanted, int have, int fallback) {
        return wanted >= 0 ? wanted : (have >= 0 ? fallback : -1);
    };
    using Kind = PlaybackMessage::Kind;
    for (std::uint8_t ch = 0; ch < 16; ++ch) {
        const FileChannel& w = want[ch];
        auto control = [&](int n) {
            int g = goal(w.cc[static_cast<std::size_t>(n)],
                         sent_[ch].cc[static_cast<std::size_t>(n)], controller_default(n));
            if (g >= 0 && g != sent_[ch].cc[static_cast<std::size_t>(n)]) {
                send(Kind::ControlChange, ch, static_cast<std::uint8_t>(n),
                     static_cast<std::uint16_t>(g), at_ns, position_s, out);
                return true;
            }
            return false;
        };
        // A new bank only takes effect with the program change after it.
        const bool msb_sent = control(kBankMsb);
        const bool bank_changed = control(kBankLsb) || msb_sent;
        const int program = goal(w.program, sent_[ch].program, 0);
        if (program >= 0 && (program != sent_[ch].program || bank_changed)) {
            send(Kind::ProgramChange, ch, static_cast<std::uint8_t>(program), 0, at_ns, position_s,
                 out);
        }
        for (int n = 1; n < kFirstModeController; ++n) {
            if (n != kBankLsb) {
                control(n);
            }
        }
        const int bend = goal(w.bend, sent_[ch].bend, kBendCentre);
        if (bend >= 0 && bend != sent_[ch].bend) {
            send(Kind::PitchBend, ch, 0, static_cast<std::uint16_t>(bend), at_ns, position_s, out);
        }
    }
}

// Starts playing from `position_s`: the file's settings there, and the cursors just ahead of it.
// Notes that began earlier are not started.
void Playback::resync(double position_s, std::int64_t at_ns, std::vector<PlaybackMessage>& out) {
    restore_state(position_s, at_ns, out);
    position_s_ = position_s;
    next_on_ = static_cast<std::size_t>(
        std::lower_bound(t_->notes.begin(), t_->notes.end(), position_s,
                         [](const Item& n, double t) { return n.start_s < t; }) -
        t_->notes.begin());
    next_off_ = static_cast<std::size_t>(
        std::lower_bound(t_->by_end.begin(), t_->by_end.end(), position_s,
                         [this](std::uint32_t i, double t) { return t_->notes[i].end_s < t; }) -
        t_->by_end.begin());
    // Events at the position were just restored.
    next_event_ = static_cast<std::size_t>(
        std::upper_bound(t_->events.begin(), t_->events.end(), position_s,
                         [](double t, const ChannelEvent& e) { return t < e.time_s; }) -
        t_->events.begin());
}

void Playback::rewind(double position_s) {
    active_.erase(std::remove_if(active_.begin(), active_.end(),
                                 [&](std::uint32_t i) { return t_->notes[i].start_s >= position_s; }),
                  active_.end());
    position_s_ = position_s;
    next_on_ = static_cast<std::size_t>(
        std::lower_bound(t_->notes.begin(), t_->notes.end(), position_s,
                         [](const Item& n, double t) { return n.start_s < t; }) -
        t_->notes.begin());
    next_off_ = static_cast<std::size_t>(
        std::lower_bound(t_->by_end.begin(), t_->by_end.end(), position_s,
                         [this](std::uint32_t i, double t) { return t_->notes[i].end_s < t; }) -
        t_->by_end.begin());
    // Events at the position are sent again: a pedal press among them was cancelled with the notes.
    next_event_ = static_cast<std::size_t>(
        std::lower_bound(t_->events.begin(), t_->events.end(), position_s,
                         [](const ChannelEvent& e, double t) { return e.time_s < t; }) -
        t_->events.begin());
}

void Playback::play_span(const TransportOp& move, std::vector<PlaybackMessage>& out) {
    const double from = move.from_s;
    const double to = move.to_s;
    if (!(to > from)) {
        return;
    }
    const auto span_ns = static_cast<double>(move.end_ns - move.at_ns);
    auto ns_of = [&](double t) {
        return move.at_ns + std::llround((t - from) / (to - from) * span_ns);
    };
    for (;;) {
        const double t_off = next_off_ < t_->by_end.size() ? t_->notes[t_->by_end[next_off_]].end_s : kNever;
        const double t_event = next_event_ < t_->events.size() ? t_->events[next_event_].time_s : kNever;
        const double t_on = next_on_ < t_->notes.size() ? t_->notes[next_on_].start_s : kNever;
        const double t = std::min({t_off, t_event, t_on});
        if (!(t < to)) {
            break;
        }
        if (t_off == t) {
            const std::uint32_t index = t_->by_end[next_off_++];
            auto slot = std::find(active_.begin(), active_.end(), index);
            if (slot != active_.end()) {
                release(static_cast<std::size_t>(slot - active_.begin()), ns_of(t), t, out);
            }
        } else if (t_event == t) {
            send_event(t_->events[next_event_++], ns_of(t), out);
        } else {
            const auto index = static_cast<std::uint32_t>(next_on_++);
            const Item& n = t_->notes[index];
            const int key = sounded_pitch(n);
            if (muted(n) || key < 0) {
                continue;
            }
            active_.push_back(index);
            used_channels_ = static_cast<std::uint16_t>(used_channels_ | (1u << n.channel));
            std::uint16_t velocity = n.velocity;
            if (n.accompaniment && accompaniment_volume_ < 100) {
                velocity = static_cast<std::uint16_t>(
                    std::max(1, (static_cast<int>(velocity) * accompaniment_volume_ + 50) / 100));
            }
            send(PlaybackMessage::Kind::NoteOn, n.channel, static_cast<std::uint8_t>(key),
                 velocity, ns_of(t), t, out);
        }
    }
    position_s_ = to;
}

void Playback::click(const TransportOp& op, std::vector<PlaybackMessage>& out) {
    if (!click_.enabled) {
        return;
    }
    const std::uint8_t note = op.accent ? click_.accent_note : click_.note;
    const std::uint8_t velocity = op.accent  ? click_.accent_velocity
                                  : op.sub > 0 ? click_.sub_velocity
                                               : click_.velocity;
    const std::size_t first = out.size();
    send(PlaybackMessage::Kind::NoteOn, click_.channel, note, velocity, op.at_ns, op.to_s, out);
    send(PlaybackMessage::Kind::NoteOff, click_.channel, note, 64, op.at_ns + click_.length_ns,
         op.to_s, out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].click = true;
    }
}

void Playback::follow(const TranspositionSource* source) {
    source_ = source;
    source_synced_ = false;
}

void Playback::sync_transposition(std::int64_t at_ns, std::vector<PlaybackMessage>& out) {
    if (source_ == nullptr || (source_synced_ && source_version_ == source_->version())) {
        return;
    }
    source_synced_ = true;
    source_version_ = source_->version();
    set_transposition(source_->value(), at_ns, out);
}

void Playback::apply(const TransportOp& op, std::vector<PlaybackMessage>& out) {
    using Kind = TransportOp::Kind;
    sync_transposition(op.at_ns, out);
    switch (op.kind) {
    case Kind::Play:
        panic(op.at_ns, op.to_s, out);
        running_ = true;
        resync(op.to_s, op.at_ns, out);
        break;
    case Kind::Pause:
    case Kind::End:
        panic(op.at_ns, op.kind == Kind::End ? op.to_s : position_s_, out);
        running_ = false;
        break;
    case Kind::Seek:
        panic(op.at_ns, op.from_s, out);
        position_s_ = op.to_s;
        if (running_) {
            resync(op.to_s, op.at_ns, out);
        }
        break;
    case Kind::Wrap:
        panic(op.at_ns, op.from_s, out);
        if (running_) {
            resync(op.to_s, op.at_ns, out);
        }
        break;
    case Kind::Move:
        if (running_) {
            play_span(op, out);
        }
        break;
    case Kind::Click:
        click(op, out);
        break;
    case Kind::Hold:
    case Kind::Release:
        break;
    }
}

void Playback::set_transposition(const Transposition& transposition, std::int64_t now_ns,
                                 std::vector<PlaybackMessage>& out) {
    if (transposition == transposition_) {
        return;
    }
    // The note-offs must name the keys the notes were started on, so stop them before the change.
    panic(now_ns, position_s_, out);
    transposition_ = transposition;
    if (running_) {
        restore_state(position_s_, now_ns, out); // the panic lifted the pedal the file holds down
    }
}

void Playback::set_hand_muted(Hand hand, bool muted_now, std::int64_t now_ns,
                              std::vector<PlaybackMessage>& out) {
    hand_muted_[static_cast<std::size_t>(hand)] = muted_now;
    if (!muted_now) {
        return;
    }
    for (std::size_t i = active_.size(); i-- > 0;) {
        const Item& n = t_->notes[active_[i]];
        if (!n.drum && !n.accompaniment && n.hand == hand) {
            release(i, now_ns, position_s_, out);
        }
    }
}

void Playback::set_drums_muted(bool muted_now, std::int64_t now_ns,
                               std::vector<PlaybackMessage>& out) {
    drums_muted_ = muted_now;
    if (!muted_now) {
        return;
    }
    for (std::size_t i = active_.size(); i-- > 0;) {
        if (t_->notes[active_[i]].drum) {
            release(i, now_ns, position_s_, out);
        }
    }
}

void Playback::set_accompaniment_muted(bool muted_now, std::int64_t now_ns,
                                       std::vector<PlaybackMessage>& out) {
    accompaniment_muted_ = muted_now;
    if (!muted_now) {
        return;
    }
    for (std::size_t i = active_.size(); i-- > 0;) {
        const Item& n = t_->notes[active_[i]];
        if (!n.drum && n.accompaniment) {
            release(i, now_ns, position_s_, out);
        }
    }
}

void Playback::set_part_muted(std::size_t part, bool muted_now, std::int64_t now_ns,
                              std::vector<PlaybackMessage>& out) {
    if (part >= t_->parts) {
        return; // the song has no such part
    }
    if (part >= part_muted_.size()) {
        if (!muted_now) {
            return; // never muted: nothing to undo
        }
        part_muted_.resize(part + 1, false);
    }
    part_muted_[part] = muted_now;
    if (!muted_now) {
        return;
    }
    for (std::size_t i = active_.size(); i-- > 0;) {
        const Item& n = t_->notes[active_[i]];
        if (n.part == part) {
            release(i, now_ns, position_s_, out);
        }
    }
}

void Playback::set_accompaniment_volume(int percent, std::int64_t now_ns,
                                        std::vector<PlaybackMessage>& out) {
    accompaniment_volume_ = std::clamp(percent, 0, 100);
    if (accompaniment_volume_ > 0) {
        return;
    }
    for (std::size_t i = active_.size(); i-- > 0;) {
        const Item& n = t_->notes[active_[i]];
        if (!n.drum && n.accompaniment) {
            release(i, now_ns, position_s_, out);
        }
    }
}

} // namespace etude
