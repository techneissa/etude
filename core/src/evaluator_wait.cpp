// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/evaluator.hpp"

#include "evaluator_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace etude {

using namespace eval_detail;

void WaitEvaluator::new_pass(double start_s, std::int64_t ns) {
    detail::PassState next;
    next.start_s = start_s;
    next.start_ns = ns;
    next.moved = true;
    passes_.push(next);
}

void WaitEvaluator::forget_presses() {
    struck_start_.fill(kNoStrike);
    wrong_looking_.fill(false);
}

bool WaitEvaluator::press_pending(int key) const {
    return std::any_of(inbox_.begin(), inbox_.end(), [key](const Inbound& in) {
        const MidiEvent& e = in.ev;
        return e.type == MidiEventType::NoteOn && e.data2 != 0 && e.data1 == key;
    });
}

bool WaitEvaluator::press_looks_wrong(int key) const {
    return key >= 0 && key < 128 && wrong_looking_[static_cast<std::size_t>(key)];
}

// A press is judged against the state it was played under: the transposition, the position and the
// song in force at its own stamp. A press still in the inbox when one of those changes (the settle
// window and the input delay keep it there a few frames) is therefore settled first, against the
// old state, and only then is the state changed; it is never left to be graded against the new one,
// which would call a right note wrong. Presses stamped after the change wait for the new state.
// Flow drops what is undecided at the same points (`drop_undecided`) and gives the grace window;
// Wait can keep the verdict, since a press is judged the moment it is processed. The points are a
// transposition change, a seek, a loop wrap and a song switch (whose results are then reset, which
// leaves the key state right).
void WaitEvaluator::settle(std::int64_t up_to_ns) {
    std::size_t used = 0;
    while (used < inbox_.size() && inbox_[used].ev.time_ns <= up_to_ns) {
        process(inbox_[used].ev, inbox_[used].lookahead_s);
        ++used;
    }
    inbox_.erase(inbox_.begin(), inbox_.begin() + static_cast<std::ptrdiff_t>(used));
}

void WaitEvaluator::restart_at(double song_s, std::int64_t ns) {
    forget_presses();
    auto it = std::lower_bound(chords_.begin(), chords_.end(), song_s - kEpsilonS,
                               [](const Chord& c, double s) { return c.start_s < s; });
    next_ = static_cast<std::size_t>(it - chords_.begin());
    obligations_.clear();
    sounding_.clear();
    held_since_ns_ = ns;
    restart_ns_ = std::max(restart_ns_, ns);
    for (Key& k : keys_) {
        k.consumed = true; // every key down now must be pressed again
    }
}

void WaitEvaluator::apply(const TransportOp& op) {
    if (op.kind == Kind::Click) {
        return;
    }
    sync_transposition();
    if (op.kind == Kind::Seek || op.kind == Kind::Wrap) {
        settle(op.at_ns); // before the position, the pass and the next chord change
    }
    timeline_.apply(op);
    last_op_ns_ = std::max(last_op_ns_, op.kind == Kind::Move ? op.end_ns : op.at_ns);
    detail::PassState& pass = passes_.back();
    switch (op.kind) {
    case Kind::Play:
        close_dead(dead_, op.at_ns);
        playing_ = true;
        position_s_ = op.to_s;
        if (!pass.moved) {
            pass.start_s = op.to_s;
            pass.start_ns = op.at_ns;
        }
        break;
    case Kind::Pause:
    case Kind::End:
        open_dead(dead_, op.at_ns);
        playing_ = false;
        position_s_ = op.to_s;
        break;
    case Kind::Hold:
        held_ = true;
        held_since_ns_ = op.at_ns;
        position_s_ = op.to_s;
        break;
    case Kind::Release:
        held_ = false;
        position_s_ = op.to_s;
        break;
    case Kind::Move: {
        pass.moved = true;
        summary_.unplayable += count_in(unplayable_s_, op.from_s, op.to_s);
        position_s_ = op.to_s;
        const double slack_s = static_cast<double>(slack_ns_) * 1e-9 * op.speed_percent / 100.0;
        obligations_.erase(std::remove_if(obligations_.begin(), obligations_.end(),
                                          [&](const Obligation& o) {
                                              return !o.needs_repress &&
                                                     position_s_ >= o.end_s - slack_s;
                                          }),
                           obligations_.end());
        sounding_.erase(std::remove_if(sounding_.begin(), sounding_.end(),
                                       [&](const Sounding& s) { return position_s_ >= s.end_s; }),
                        sounding_.end());
        break;
    }
    case Kind::Wrap:
        pass.ended = true;
        pass.end_s = op.from_s;
        pass.end_ns = op.at_ns;
        pass.loop_start_s = op.to_s;
        new_pass(op.to_s, op.at_ns);
        position_s_ = op.to_s;
        restart_at(op.to_s, op.at_ns);
        break;
    case Kind::Seek:
        if (pass.moved) {
            drop_pass(pass, op.from_s, op.at_ns);
            new_pass(op.to_s, op.at_ns);
        } else {
            pass.start_s = op.to_s;
            pass.start_ns = op.at_ns;
        }
        position_s_ = op.to_s;
        restart_at(op.to_s, op.at_ns);
        break;
    case Kind::Click:
        break;
    }
}

void WaitEvaluator::release_all(std::int64_t now_ns) {
    MidiEvent ev;
    ev.time_ns = now_ns;
    ev.type = MidiEventType::ControlChange;
    ev.data1 = kCcAllNotesOff;
    ev.synthetic = true; // not the player's doing
    apply(ev);
}

void WaitEvaluator::apply(const MidiEvent& ev) {
    const auto later =
        std::upper_bound(inbox_.begin(), inbox_.end(), ev.time_ns,
                         [](std::int64_t t, const Inbound& in) { return t < in.ev.time_ns; });
    inbox_.insert(later, {ev, options_.lookahead_s});
    // No sync of a followed transposition here: it happens at the next update() or op, when every
    // event of the batch is queued, so a change made after the batch's earlier presses settles
    // all of them, not only the first one inserted.
}

void WaitEvaluator::process(const MidiEvent& ev, double lookahead_s) {
    switch (ev.type) {
    case MidiEventType::NoteOn:
    case MidiEventType::NoteOff: {
        if (ev.data1 > 127) {
            return;
        }
        Key& k = keys_[ev.data1];
        if (k.seen && ev.time_ns < k.last_ns) {
            return; // older than what this key already showed
        }
        k.seen = true;
        k.last_ns = ev.time_ns;
        if (ev.type == MidiEventType::NoteOn && ev.data2 != 0) {
            press(ev.data1, ev.time_ns, ev.data2, lookahead_s);
        } else {
            release_key(ev.data1, ev.time_ns, true);
        }
        return;
    }
    case MidiEventType::ControlChange:
        if (ev.data1 == kCcAllNotesOff || ev.data1 == kCcAllSoundOff) {
            for (int key = 0; key < 128; ++key) {
                release_key(key, ev.time_ns, !ev.synthetic);
            }
        }
        return; // the sustain pedal holds nothing
    case MidiEventType::Other:
        return;
    }
}

bool WaitEvaluator::expected_press(int key, double song_s, double early_s,
                                   double lookahead_s) const {
    const double ahead = std::max(early_s, lookahead_s);
    for (std::size_t i = next_; i < chords_.size(); ++i) {
        const Chord& c = chords_[i];
        if (i != next_ && c.start_s > song_s + ahead + kEpsilonS) {
            break;
        }
        for (const ChordKey& k : c.keys) {
            if (k.key == key) {
                return true;
            }
        }
    }
    for (const Sounding& s : sounding_) {
        if (s.key == key && s.end_s > song_s) {
            return true;
        }
    }
    for (const Obligation& o : obligations_) {
        if (o.key == key) {
            return true;
        }
    }
    return false;
}

void WaitEvaluator::press(int key, std::int64_t ns, int velocity, double lookahead_s) {
    Key& k = keys_[static_cast<std::size_t>(key)];
    if (k.down) {
        release_key(key, ns, false); // its key-up was lost: this is a new strike
    }
    k.down = true;
    k.press_ns = ns;
    k.press_s = timeline_.song_at(ns);
    k.press_rate = timeline_.rate_at(ns);
    k.velocity = velocity;
    k.lookahead_s = lookahead_s;
    k.consumed = ns < restart_ns_;
    struck_start_[static_cast<std::size_t>(key)] = kNoStrike;
    wrong_looking_[static_cast<std::size_t>(key)] = false;
    for (Obligation& o : obligations_) {
        if (o.key == key && o.needs_repress) {
            o.needs_repress = false;
            k.consumed = true;
        }
    }
    if (in_dead(dead_, ns)) {
        k.consumed = true; // pressed while paused or before play: press it again after play
        return;
    }
    if (playing_) {
        const double early_s = static_cast<double>(early_ns_) * 1e-9 * k.press_rate;
        // The chord holding this key that starts nearest the press, within the early margin.
        auto chord =
            std::lower_bound(chords_.begin(), chords_.end(), k.press_s - early_s - kEpsilonS,
                             [](const Chord& c, double s) { return c.start_s < s; });
        double nearest = std::numeric_limits<double>::infinity();
        for (; chord != chords_.end() && chord->start_s <= k.press_s + early_s + kEpsilonS;
             ++chord) {
            const double distance = std::fabs(chord->start_s - k.press_s);
            const bool holds = std::any_of(chord->keys.begin(), chord->keys.end(),
                                           [key](const ChordKey& c) { return c.key == key; });
            if (holds && distance < nearest) {
                nearest = distance;
                struck_start_[static_cast<std::size_t>(key)] = chord->start_s;
            }
        }
        if (!expected_press(key, k.press_s, early_s, k.lookahead_s)) {
            int measure = 1;
            if (next_ < chords_.size()) {
                measure = chords_[next_].measure;
            } else if (!chords_.empty()) {
                measure = chords_.back().measure;
            }
            record_wrong(summary_, measure);
            // A wrong press struck nothing, whatever chord was near.
            struck_start_[static_cast<std::size_t>(key)] = kNoStrike;
            wrong_looking_[static_cast<std::size_t>(key)] = true;
            if (wrong_presses_.size() < kMaxKeptWrongPresses) {
                wrong_presses_.push_back({key, ns, velocity, k.press_s});
            } else {
                ++dropped_wrong_presses_;
            }
            ++passes_.back().result.wrong;
            passes_.back().combo = 0;
        }
    }
    try_play(ns);
}

void WaitEvaluator::release_key(int key, std::int64_t ns, bool by_player) {
    Key& k = keys_[static_cast<std::size_t>(key)];
    if (!k.down) {
        return;
    }
    k.down = false;
    if (!by_player || !options_.hold_full_length) {
        return;
    }
    const double song_s = timeline_.song_at(ns);
    const double slack_s = static_cast<double>(slack_ns_) * 1e-9 * timeline_.rate_at(ns);
    for (Obligation& o : obligations_) {
        if (o.key != key || o.needs_repress || song_s >= o.end_s - slack_s) {
            continue;
        }
        o.needs_repress = true;
        for (std::size_t i = 0; i < o.result_count; ++i) {
            summary_.notes[o.result + i].released_early = true;
        }
        passes_.back().result.released_early = true;
    }
}

void WaitEvaluator::try_play(std::int64_t ns) {
    while (next_ < chords_.size()) {
        const Chord& c = chords_[next_];
        for (const ChordKey& ck : c.keys) {
            const Key& k = keys_[static_cast<std::size_t>(ck.key)];
            const double early_s = static_cast<double>(early_ns_) * 1e-9 * k.press_rate;
            if (!k.down || k.consumed || k.press_s < c.start_s - early_s - kEpsilonS) {
                return;
            }
        }
        const bool reached = held_ && position_s_ >= c.start_s - kEpsilonS;
        const std::int64_t arrival = reached ? held_since_ns_ : ns;
        for (const ChordKey& ck : c.keys) {
            keys_[static_cast<std::size_t>(ck.key)].consumed = true;
            double end_s = c.start_s;
            const std::size_t first = summary_.notes.size();
            for (std::uint32_t idx : ck.notes) {
                const Note& n = (*notes_)[idx];
                end_s = std::max(end_s, n.start_s + n.duration_s);
                NoteResult r;
                r.note_index = idx;
                r.pass = passes_.back().result.pass;
                r.grade = NoteGrade::Played;
                r.expected_pitch = ck.key;
                r.hand = n.hand;
                r.measure = n.measure;
                r.song_s = n.start_s;
                r.arrival_ns = arrival;
                r.press_ns = ns;
                r.error_ns = std::max<std::int64_t>(0, ns - arrival);
                const Key& pressed = keys_[static_cast<std::size_t>(ck.key)];
                r.velocity = pressed.velocity;
                r.press_song_s = pressed.press_s;
                r.timing_error_ns = static_cast<std::int64_t>(
                    std::llround((pressed.press_s - n.start_s) * 1e9 / pressed.press_rate));
                record(summary_, r);
                ++passes_.back().result.graded;
                tally(passes_.back(), r.grade);
            }
            sounding_.push_back({ck.key, end_s});
            if (options_.hold_full_length) {
                obligations_.push_back({ck.key, end_s, first, ck.notes.size(), false});
            }
        }
        ++next_;
    }
}

WaitDirective WaitEvaluator::update(std::int64_t now_ns) {
    sync_transposition();
    const std::int64_t edge = now_ns - settle_ns_;
    std::size_t used = 0;
    while (used < inbox_.size() && inbox_[used].ev.time_ns <= edge) {
        process(inbox_[used].ev, inbox_[used].lookahead_s);
        ++used;
    }
    inbox_.erase(inbox_.begin(), inbox_.begin() + static_cast<std::ptrdiff_t>(used));
    passes_.report([&](const detail::PassState& p) { return p.end_ns <= edge; });
    timeline_.forget_before(edge - kHistoryNs);
    prune_dead(dead_, edge - kHistoryNs);

    WaitDirective d;
    if (!playing_) {
        return d;
    }
    const bool repress = std::any_of(obligations_.begin(), obligations_.end(),
                                     [](const Obligation& o) { return o.needs_repress; });
    if (repress) {
        d.hold_now = !held_;
        return d;
    }
    if (next_ < chords_.size()) {
        const double start = chords_[next_].start_s;
        if (position_s_ >= start - kEpsilonS) {
            d.hold_now = !held_;
        } else {
            d.release = held_;
            d.arm_hold = true;
            d.hold_at_s = start;
        }
    } else {
        d.release = held_;
    }
    return d;
}

void WaitEvaluator::take_passes(std::vector<PassResult>& out) {
    passes_.take([&](const detail::PassState& p) {
        PassResult r = p.result;
        r.start_s = p.start_s;
        r.end_s = p.end_s;
        r.clean = !p.interrupted && p.start_s <= p.loop_start_s + kEpsilonS && r.missed == 0 &&
                  r.wrong == 0 && !r.released_early;
        out.push_back(r);
    });
}

int WaitEvaluator::take_dropped_wrong_presses() { return std::exchange(dropped_wrong_presses_, 0); }

void WaitEvaluator::take_wrong_presses(std::vector<WrongPress>& out) {
    out.insert(out.end(), wrong_presses_.begin(), wrong_presses_.end());
    wrong_presses_.clear();
}

std::optional<double> WaitEvaluator::struck_start(int key) const {
    return key >= 0 && key < 128 ? strike_or_none(struck_start_[static_cast<std::size_t>(key)])
                                 : std::nullopt;
}

} // namespace etude
