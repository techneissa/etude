// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: prepared (split from evaluator_test.cpp).
#include "etude/evaluator.hpp"
#include "alloc_count.hpp"
#include "etude/keyboard.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include "evaluator_test_support.hpp"

using namespace etude;
using OpKind = TransportOp::Kind;
using namespace evaluator_test_support;

// ---------------------------------------------------------------------------------------------
// Tables made ahead (FlowEvaluator::prepare, WaitEvaluator::prepare)

namespace {

// Plays `played` through a Transport into an evaluator that already holds the song, a 60 FPS frame
// at a time, and returns what it graded.
EvaluationSummary play_flow(FlowEvaluator& eval, const PlayedSong& played) {
    Transport transport;
    transport.set_song(played.song);
    transport.set_speed_percent(sec(1.0), 100);
    transport.play(sec(1.0));
    std::vector<TransportOp> ops;
    std::size_t next = 0;
    std::int64_t now = sec(1.0);
    const std::int64_t end = sec(1.0 + played.song.duration_s);
    while (now < end) {
        now += 16'666'667;
        transport.update(now);
        ops.clear();
        transport.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
        while (next < played.presses.size() && played.presses[next].time_ns <= now) {
            eval.apply(played.presses[next++]);
        }
        eval.update(now);
    }
    eval.finish();
    return eval.summary();
}

EvaluationSummary play_wait(WaitEvaluator& eval, const PlayedSong& played) {
    Transport transport;
    transport.set_song(played.song);
    transport.set_speed_percent(sec(1.0), 100);
    transport.play(sec(1.0));
    std::vector<TransportOp> ops;
    std::size_t next = 0;
    std::int64_t now = sec(1.0);
    const std::int64_t end = sec(1.0 + played.song.duration_s);
    const auto take = [&] {
        ops.clear();
        transport.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
    };
    take();
    while (now < end) {
        now += 16'666'667;
        while (next < played.presses.size() && played.presses[next].time_ns <= now) {
            eval.apply(played.presses[next++]);
        }
        transport.update(now);
        const WaitDirective d = eval.update(now);
        if (d.hold_now) {
            transport.hold(now);
        }
        if (d.release) {
            transport.release(now);
        }
        if (d.arm_hold) {
            transport.hold_at(now, d.hold_at_s);
        }
        take();
    }
    return eval.summary();
}

bool same_results(const EvaluationSummary& a, const EvaluationSummary& b) {
    if (a.counts.perfect != b.counts.perfect || a.counts.good != b.counts.good ||
        a.counts.early != b.counts.early || a.counts.late != b.counts.late ||
        a.counts.missed != b.counts.missed || a.counts.played != b.counts.played ||
        a.wrong != b.wrong || a.longest_combo != b.longest_combo ||
        a.notes.size() != b.notes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.notes.size(); ++i) {
        if (a.notes[i].note_index != b.notes[i].note_index || a.notes[i].grade != b.notes[i].grade ||
            a.notes[i].expected_pitch != b.notes[i].expected_pitch) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("flow: a song prepared ahead, even on another thread, grades exactly like a plain one") {
    const PlayedSong played = played_song();
    FlowEvaluator plain;
    plain.set_song(played.song, FlowOptions{});
    const EvaluationSummary expected = play_flow(plain, played);

    FlowEvaluator::Prepared prepared;
    std::thread maker([&] {
        prepared = FlowEvaluator::prepare(played.song, HandFilter::Both, Transposition{});
    });
    maker.join();
    CHECK(prepared.note_count() == played.song.notes.size());
    FlowEvaluator taken;
    taken.set_song(played.song, FlowOptions{}, std::move(prepared));
    const EvaluationSummary got = play_flow(taken, played);
    CHECK(got.notes.size() > 100);
    CHECK(same_results(got, expected));
}

TEST_CASE("flow: tables made for other hands or another key are not used") {
    const PlayedSong played = played_song();
    FlowEvaluator plain;
    plain.set_song(played.song, FlowOptions{});
    const EvaluationSummary expected = play_flow(plain, played);

    FlowEvaluator wrong_hands;
    wrong_hands.set_song(played.song, FlowOptions{},
                         FlowEvaluator::prepare(played.song, HandFilter::Left, Transposition{}));
    const EvaluationSummary hands = play_flow(wrong_hands, played);
    CHECK(same_results(hands, expected)); // both hands graded, not the left only

    FlowEvaluator wrong_key;
    wrong_key.set_song(played.song, FlowOptions{},
                       FlowEvaluator::prepare(played.song, HandFilter::Both,
                                              Transposition(2, KeyRange{36, 96})));
    CHECK(same_results(play_flow(wrong_key, played), expected));

    // Tables of another song (a different number of notes) are not used either.
    const Song other = song_of({{1.0, 60}, {2.0, 62}});
    FlowEvaluator wrong_song;
    wrong_song.set_song(played.song, FlowOptions{},
                        FlowEvaluator::prepare(other, HandFilter::Both, Transposition{}));
    CHECK(same_results(play_flow(wrong_song, played), expected));

    // Tables of another song with the same number of notes and bars are not used either: the
    // song's own content decides, not its size.
    Song shifted = played.song;
    for (Note& n : shifted.notes) {
        n.pitch = static_cast<std::uint8_t>(n.pitch + 1);
    }
    FlowEvaluator same_count;
    same_count.set_song(played.song, FlowOptions{},
                        FlowEvaluator::prepare(shifted, HandFilter::Both, Transposition{}));
    CHECK(same_results(play_flow(same_count, played), expected));

    // And the key the evaluator follows decides, as in set_song without tables.
    TranspositionSource source;
    source.set(Transposition(2, KeyRange{36, 96}), 0);
    FlowEvaluator following;
    following.follow(&source);
    following.set_song(played.song, FlowOptions{},
                       FlowEvaluator::prepare(played.song, HandFilter::Both, Transposition{}));
    FlowEvaluator following_plain;
    following_plain.follow(&source);
    following_plain.set_song(played.song, FlowOptions{});
    CHECK(same_results(play_flow(following, played), play_flow(following_plain, played)));
}

TEST_CASE("flow: taking tables made ahead over builds nothing on the calling thread") {
    const PlayedSong played = played_song();
    FlowEvaluator::Prepared prepared =
        FlowEvaluator::prepare(played.song, HandFilter::Both, Transposition{});
    FlowEvaluator taken;
    etude::test::start_counting_allocations();
    taken.set_song(played.song, FlowOptions{}, std::move(prepared));
    const int took = etude::test::stop_counting_allocations();

    FlowEvaluator plain;
    etude::test::start_counting_allocations();
    plain.set_song(played.song, FlowOptions{});
    const int built = etude::test::stop_counting_allocations();
    CAPTURE(took);
    CAPTURE(built);
    // The plain way allocates a list of notes for each key press; taking over, a few buffers.
    CHECK(built > static_cast<int>(played.song.notes.size()) / 4);
    CHECK(took < 64);
}

TEST_CASE("wait: a song prepared ahead grades exactly like a plain one, and answers the same keys") {
    const PlayedSong played = played_song(true);
    WaitEvaluator plain;
    plain.set_song(played.song, WaitOptions{});
    const std::vector<int> first_plain = keys_of(plain.waiting_keys());
    const EvaluationSummary expected = play_wait(plain, played);

    WaitEvaluator::Prepared prepared;
    std::thread maker([&] {
        prepared = WaitEvaluator::prepare(played.song, HandFilter::Both, Transposition{});
    });
    maker.join();
    WaitEvaluator taken;
    taken.set_song(played.song, WaitOptions{}, std::move(prepared));
    CHECK(keys_of(taken.waiting_keys()) == first_plain);
    CHECK_FALSE(first_plain.empty());
    CHECK(same_results(play_wait(taken, played), expected));
    CHECK(expected.counts.played == static_cast<int>(played.song.notes.size()));
}

TEST_CASE("wait: tables made for other hands, another key or another song are not used") {
    const PlayedSong played = played_song(true);
    WaitEvaluator plain;
    plain.set_song(played.song, WaitOptions{});
    const EvaluationSummary expected = play_wait(plain, played);

    WaitEvaluator wrong_hands;
    wrong_hands.set_song(played.song, WaitOptions{},
                         WaitEvaluator::prepare(played.song, HandFilter::Right, Transposition{}));
    CHECK(same_results(play_wait(wrong_hands, played), expected));

    WaitEvaluator wrong_key;
    wrong_key.set_song(played.song, WaitOptions{},
                       WaitEvaluator::prepare(played.song, HandFilter::Both,
                                              Transposition(-3, KeyRange{36, 96})));
    CHECK(same_results(play_wait(wrong_key, played), expected));

    WaitEvaluator wrong_song;
    wrong_song.set_song(
        played.song, WaitOptions{},
        WaitEvaluator::prepare(song_of({{1.0, 60}}), HandFilter::Both, Transposition{}));
    CHECK(same_results(play_wait(wrong_song, played), expected));

    // Another song with the same number of notes: its own content decides, not its size.
    Song shifted = played.song;
    for (Note& n : shifted.notes) {
        n.pitch = static_cast<std::uint8_t>(n.pitch + 1);
    }
    WaitEvaluator same_count;
    same_count.set_song(played.song, WaitOptions{},
                        WaitEvaluator::prepare(shifted, HandFilter::Both, Transposition{}));
    CHECK(same_results(play_wait(same_count, played), expected));
}

namespace {

const Transposition kKeyUp2(2, KeyRange{36, 96});

// An evaluator following `source`, over the played song, with `offer` made for it (built on another
// thread, from the evaluator's own notes) and the source moved to `to` afterwards. Returns what
// grading the song gives, and whether the offered targets were the ones swapped in.
struct OfferedFlow {
    EvaluationSummary results;
    bool taken = false;
};

OfferedFlow flow_with_offer(const PlayedSong& played, HandFilter offered_hands,
                            const Transposition& offered_key, const Transposition& to,
                            bool foreign_notes = false) {
    TranspositionSource source;
    FlowEvaluator eval;
    eval.follow(&source);
    eval.set_song(played.song, FlowOptions{});
    FlowEvaluator::Prepared offer;
    std::thread maker([&] {
        offer = foreign_notes
                    ? FlowEvaluator::prepare(played.song, offered_hands, offered_key)
                    : FlowEvaluator::prepare(eval.note_table(), eval.measure_table(), offered_hands,
                                             offered_key, song_fingerprint(played.song));
    });
    maker.join();
    eval.offer_targets(std::move(offer));
    const std::uint64_t built = eval.builds();
    source.set(to, 0);
    eval.update(1); // picks the change up
    OfferedFlow out;
    out.taken = eval.builds() == built; // nothing was built here: the offered targets went in
    out.results = play_flow(eval, played);
    return out;
}

EvaluationSummary flow_following(const PlayedSong& played, const Transposition& to) {
    TranspositionSource source;
    FlowEvaluator eval;
    eval.follow(&source);
    eval.set_song(played.song, FlowOptions{});
    source.set(to, 0);
    eval.update(1);
    return play_flow(eval, played);
}

} // namespace

TEST_CASE("flow: targets offered for a change of key are swapped in, and grade like built ones") {
    const PlayedSong played = played_song();
    const EvaluationSummary expected = flow_following(played, kKeyUp2);
    const OfferedFlow got = flow_with_offer(played, HandFilter::Both, kKeyUp2, kKeyUp2);
    CHECK(got.taken);
    CHECK(same_results(got.results, expected));
    CHECK(got.results.notes.size() > 100);
}

TEST_CASE("flow: targets offered for another key, other hands or other notes are never used") {
    const PlayedSong played = played_song();
    const EvaluationSummary expected = flow_following(played, kKeyUp2);

    // Built for a key other than the one the source moved to.
    OfferedFlow got = flow_with_offer(played, HandFilter::Both, Transposition(3, KeyRange{36, 96}),
                                      kKeyUp2);
    CHECK_FALSE(got.taken);
    CHECK(same_results(got.results, expected));
    // Built for other hands than the evaluator grades.
    got = flow_with_offer(played, HandFilter::Left, kKeyUp2, kKeyUp2);
    CHECK_FALSE(got.taken);
    CHECK(same_results(got.results, expected));
    // Built from a copy of the song: the same content, but not the table the evaluator holds.
    got = flow_with_offer(played, HandFilter::Both, kKeyUp2, kKeyUp2, true);
    CHECK_FALSE(got.taken);
    CHECK(same_results(got.results, expected));
}

TEST_CASE("flow: an offer does not outlive a new song") {
    const PlayedSong played = played_song();
    TranspositionSource source;
    FlowEvaluator eval;
    eval.follow(&source);
    eval.set_song(played.song, FlowOptions{});
    eval.offer_targets(FlowEvaluator::prepare(eval.note_table(), eval.measure_table(),
                                              HandFilter::Both, kKeyUp2,
                                              song_fingerprint(played.song)));
    eval.set_song(played.song, FlowOptions{}); // a new table of notes
    const std::uint64_t built = eval.builds();
    source.set(kKeyUp2, 0);
    eval.update(1);
    CHECK(eval.builds() == built + 1); // the old offer was dropped with the song: built afresh
    CHECK(same_results(play_flow(eval, played), flow_following(played, kKeyUp2)));
}

TEST_CASE("flow: new options with the tables kept grade like a rebuilt evaluator") {
    const PlayedSong played = played_song();
    FlowOptions wide;
    wide.window_ms = 140.0;
    FlowEvaluator rebuilt;
    rebuilt.set_song(played.song, wide);
    const EvaluationSummary expected = play_flow(rebuilt, played);

    FlowEvaluator kept;
    kept.set_song(played.song, FlowOptions{});
    const FlowEvaluator::NoteTable table = kept.note_table();
    CHECK(kept.reconfigure(wide));
    CHECK(kept.note_table() == table); // nothing copied again
    CHECK(kept.options().window_ms == 140.0);
    CHECK(same_results(play_flow(kept, played), expected));

    // Other hands need other tables: refused, and nothing changed.
    FlowOptions left = wide;
    left.hands = HandFilter::Left;
    FlowEvaluator refusing;
    refusing.set_song(played.song, FlowOptions{});
    CHECK_FALSE(refusing.reconfigure(left));
    CHECK(refusing.options().hands == HandFilter::Both);
    CHECK(refusing.options().window_ms == FlowOptions{}.window_ms);
}

TEST_CASE("wait: chords offered for a change of key are swapped in, and only when they fit") {
    const PlayedSong played = played_song(true);
    auto following = [&](const Transposition& to) {
        TranspositionSource source;
        WaitEvaluator eval;
        eval.follow(&source);
        eval.set_song(played.song, WaitOptions{});
        source.set(to, 0);
        eval.update(1);
        return std::pair{keys_of(eval.waiting_keys()), play_wait(eval, played)};
    };
    auto offered = [&](HandFilter hands, const Transposition& made_for, const Transposition& to,
                       bool foreign) {
        TranspositionSource source;
        WaitEvaluator eval;
        eval.follow(&source);
        eval.set_song(played.song, WaitOptions{});
        WaitEvaluator::Prepared offer;
        std::thread maker([&] {
            offer = foreign ? WaitEvaluator::prepare(played.song, hands, made_for)
                            : WaitEvaluator::prepare(eval.note_table(), hands, made_for,
                                                     song_fingerprint(played.song));
        });
        maker.join();
        eval.offer_targets(std::move(offer));
        const std::uint64_t built = eval.builds();
        source.set(to, 0);
        eval.update(1);
        struct Out {
            std::vector<int> first;
            EvaluationSummary results;
            bool taken;
        };
        const bool taken = eval.builds() == built;
        return Out{keys_of(eval.waiting_keys()), play_wait(eval, played), taken};
    };
    const auto expected = following(kKeyUp2);
    CHECK_FALSE(expected.first.empty());

    auto got = offered(HandFilter::Both, kKeyUp2, kKeyUp2, false);
    CHECK(got.taken);
    CHECK(got.first == expected.first);
    CHECK(same_results(got.results, expected.second));

    got = offered(HandFilter::Both, Transposition(3, KeyRange{36, 96}), kKeyUp2, false);
    CHECK_FALSE(got.taken);
    CHECK(got.first == expected.first);
    CHECK(same_results(got.results, expected.second));
    got = offered(HandFilter::Left, kKeyUp2, kKeyUp2, false);
    CHECK_FALSE(got.taken);
    CHECK(got.first == expected.first);
    got = offered(HandFilter::Both, kKeyUp2, kKeyUp2, true);
    CHECK_FALSE(got.taken);
    CHECK(got.first == expected.first);
    CHECK(same_results(got.results, expected.second));
}

TEST_CASE("wait: the keys waited for are a view that is not rebuilt as a vector") {
    WaitRig rig(song_of({{1.0, 67}, {1.0, 60}, {1.0, 64}, {2.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    const std::span<const int> keys = rig.eval.waiting_keys();
    CHECK(keys.size() == 3);
    CHECK(keys_of(keys) == std::vector<int>{60, 64, 67}); // in key order
    etude::test::start_counting_allocations();
    for (int i = 0; i < 100; ++i) {
        CHECK(rig.eval.waiting_keys().size() == 3);
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    // Nothing left to wait for: an empty view, not a stale one.
    rig.send(note_on(2.5, 60));
    rig.send(note_on(2.6, 64));
    rig.send(note_on(2.7, 67));
    rig.run_until(2.9);
    CHECK(keys_of(rig.eval.waiting_keys()) == std::vector<int>{72});
}
