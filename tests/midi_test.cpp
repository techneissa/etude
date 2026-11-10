// SPDX-License-Identifier: GPL-3.0-or-later
// MIDI files, the writer, live MIDI bytes and the queues they travel through.
#include "etude/key_state.hpp"
#include "etude/midi_event.hpp"
#include "etude/midi_file.hpp"
#include "etude/midi_writer.hpp"
#include "etude/recorder.hpp"
#include "etude/song_builder.hpp"
#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::test;

namespace {
MidiLoadResult load(const Bytes& bytes, const MidiLoadOptions& options = {}) {
    return load_midi(bytes.data(), bytes.size(), options);
}

// A file with one track chunk whose header says `declared` bytes, followed by `body`.
Bytes cut_track_file(const Bytes& body, std::size_t declared) {
    Bytes file = header(0, 1, 480);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    put_be(file, static_cast<std::uint32_t>(declared), 4);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

Bytes read_test_file(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("tempo changes move note times") {
    // 480 ticks per quarter. 120 BPM from tick 0, 60 BPM from 960, 240 BPM from 1920.
    // Hand-computed: tick 960 = 1.0 s, 1440 = 2.0 s, 1920 = 3.0 s, 2400 = 3.25 s.
    Bytes file = smf({Track().bpm(0, 120).bpm(960, 60).bpm(960, 240).end(),
                      Track()
                          .on(0, 0, 60)
                          .off(480, 0, 60) // 0 .. 0.5 s
                          .on(480, 0, 62)  // tick 960
                          .off(480, 0, 62) // 1.0 .. 2.0 s
                          .on(0, 0, 64)    // tick 1440, held across the change at 1920
                          .off(960, 0, 64) // 2.0 .. 3.25 s
                          .end()});
    MidiLoadResult r = load(file);
    REQUIRE(r.ok);
    const auto& n = r.song.notes;
    REQUIRE(n.size() == 3);
    CHECK(n[1].start_s == doctest::Approx(1.0));
    CHECK(n[1].duration_s == doctest::Approx(1.0));
    CHECK(n[2].start_s == doctest::Approx(2.0));
    CHECK(n[2].duration_s == doctest::Approx(1.25));
    CHECK(r.song.tempo.seconds_at(1200) == doctest::Approx(1.5));
    CHECK(r.song.tempo.tick_at(3.25) == doctest::Approx(2400.0));
}

TEST_CASE("running status, velocity-0 note-off and the pedal read as keyboards write them") {
    Track t;
    t.raw(0, {0x90, 60, 100})
        .raw(0, {64, 90})  // running status: another note-on
        .cc(0, 0, 64, 127) // the pedal never lengthens a note
        .raw(480, {0x90, 60, 0})
        .raw(0, {64, 0}) // velocity 0 ends the note
        .cc(0, 0, 64, 0)
        .end();
    MidiLoadResult r = load(smf({t}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[1].pitch == 64);
    CHECK(r.song.notes[1].velocity == 90);
    for (const Note& n : r.song.notes) {
        CHECK(n.duration_s == doctest::Approx(0.5));
    }
    CHECK(r.stats.overlaps_split == 0);
}

TEST_CASE("overlapping notes of one pitch split cleanly, never with zero or negative length") {
    SUBCASE("a second note-on ends the first; the extra note-off is ignored") {
        MidiLoadResult r =
            load(smf({Track().on(0, 0, 60).on(240, 0, 60).off(240, 0, 60).off(240, 0, 60).end()}));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 2);
        CHECK(r.song.notes[0].end_tick == 240);
        CHECK(r.song.notes[1].start_tick == 240);
        CHECK(r.song.notes[1].end_tick == 480);
    }
    SUBCASE("a re-strike written note-on before note-off at one tick keeps both notes") {
        MidiLoadResult r =
            load(smf({Track().on(0, 0, 60).on(480, 0, 60).off(0, 0, 60).off(480, 0, 60).end()}));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 2);
        CHECK(r.song.notes[0].end_tick == 480);
        CHECK(r.song.notes[1].end_tick == 960);
    }
    SUBCASE("a note-off at the note-on tick drops the note") {
        MidiLoadResult r =
            load(smf({Track().on(0, 0, 60).off(0, 0, 60).on(480, 0, 62).off(480, 0, 62).end()}));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 1);
        CHECK(r.song.notes[0].pitch == 62);
    }
    SUBCASE("the same pitch on another channel or another track never cuts the first") {
        MidiLoadResult r =
            load(smf({Track().on(0, 0, 60).on(240, 1, 60).off(240, 0, 60).off(240, 1, 60).end(),
                      Track().on(120, 0, 60).off(600, 0, 60).end()}));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 3);
        CHECK(r.stats.overlaps_split == 0);
        CHECK(r.song.notes[0].start_tick == 0);
        CHECK(r.song.notes[0].end_tick == 480); // its own note-off
    }
}

TEST_CASE("notes on channel 10 are flagged as drums") {
    MidiLoadResult r =
        load(smf({Track().on(0, 9, 36).off(240, 9, 36).on(0, 0, 60).off(240, 0, 60).end()}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].drum);
    CHECK_FALSE(r.song.notes[1].drum);
}

TEST_CASE(
    "ghost notes: under 30 ms and shorter than a 128th note are dropped, unless they are many") {
    // 480 ticks per quarter at 120 BPM: a 128th note is 15 ticks (15.6 ms). `notes` real notes of
    // 480 ticks, then `ghosts` notes of `ticks` ticks each. When more than a tenth of the notes
    // would go, the filter leaves them all.
    struct Row {
        int notes, ghosts, ticks, kept;
    };
    for (Row r : {Row{40, 1, 14, 40}, Row{40, 1, 15, 41}, Row{40, 1, 16, 41}, Row{18, 2, 10, 18},
                  Row{17, 2, 10, 19}}) {
        Track t;
        for (int i = 0; i < r.notes; ++i) {
            t.on(0, 0, 40 + i % 40).off(480, 0, 40 + i % 40);
        }
        for (int i = 0; i < r.ghosts; ++i) {
            t.on(0, 0, 50 + i % 40).off(static_cast<std::uint32_t>(r.ticks), 0, 50 + i % 40);
        }
        const Bytes file = smf({t.end()});
        CAPTURE(r.ghosts);
        CAPTURE(r.ticks);
        const MidiLoadResult loaded = load(file);
        REQUIRE(loaded.ok);
        CHECK(loaded.song.notes.size() == static_cast<std::size_t>(r.kept));
        CHECK(load(file, {.keep_ghost_notes = true}).song.notes.size() ==
              static_cast<std::size_t>(r.notes + r.ghosts));
    }
    // A short drum hit is never a ghost.
    Track drums;
    for (int i = 0; i < 30; ++i) {
        drums.on(0, 0, 60 + i % 12).off(480, 0, 60 + i % 12);
    }
    drums.on(0, 9, 42).off(5, 9, 42);
    CHECK(load(smf({drums.end()})).song.notes.size() == 31);
}

TEST_CASE("a note-on with no note-off ends at the end of its track") {
    MidiLoadResult r = load(smf({Track().on(0, 0, 60).on(480, 0, 64).end(480)}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].end_tick == 960);
    CHECK(r.song.notes[1].end_tick == 960);
}

TEST_CASE("bar numbers come from time signatures, with the pickup bar as 0") {
    SUBCASE("4/4 then 3/4 from bar 3") {
        // Bars start at 0, 1920, 3840 (3/4 from here), 5280.
        Bytes file =
            smf({Track().time_sig(0, 4, 4).time_sig(3840, 3, 4).end(), Track()
                                                                           .on(0, 0, 60)
                                                                           .off(1920, 0, 60)
                                                                           .on(0, 0, 61) // 1920
                                                                           .off(1920, 0, 61)
                                                                           .on(0, 0, 62) // 3840
                                                                           .off(1440, 0, 62)
                                                                           .on(0, 0, 63) // 5280
                                                                           .off(480, 0, 63)
                                                                           .end()});
        MidiLoadResult r = load(file);
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 4);
        CHECK(r.song.notes[2].measure == 3);
        CHECK(r.song.notes[3].measure == 4);
        CHECK(r.song.measures[3].start_tick == 5280);
        CHECK(r.song.measures[3].start_s == doctest::Approx(5.5));
    }
    SUBCASE("a first bar shorter than its time signature is bar 0, exactly as long is bar 1") {
        // The first bar is `first` ticks long, then a time signature starts the next one.
        struct Row {
            int first;
            int next_numerator;
            int first_bar;
        };
        for (Row r :
             {Row{480, 4, 0}, Row{960, 4, 0}, Row{960, 3, 0}, Row{1919, 4, 0}, Row{1920, 4, 1}}) {
            Track t;
            t.on(0, 0, 67).off(static_cast<std::uint32_t>(r.first), 0, 67);
            t.time_sig(0, r.next_numerator, 4).on(0, 0, 60).off(480, 0, 60).end();
            Track conductor;
            conductor.time_sig(0, 4, 4).end();
            MidiLoadResult loaded = load(smf({conductor, t}));
            CAPTURE(r.first);
            REQUIRE(loaded.ok);
            CHECK(loaded.song.notes[0].measure == r.first_bar);
            CHECK(loaded.song.notes[1].measure == r.first_bar + 1);
        }
    }
}

TEST_CASE("unknown chunks such as Yamaha XF are skipped") {
    Bytes file = header(1, 2, 480);
    put_chunk(file, "MTrk", Track().bpm(0, 60).end().data());
    put_chunk(file, "XFIH", {1, 2, 3, 4, 5});
    put_chunk(file, "MTrk", Track().on(0, 0, 60).off(480, 0, 60).end().data());
    put_chunk(file, "XFKM", {9});
    MidiLoadResult r = load(file);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(1.0));
}

TEST_CASE("a damaged file is refused or read up to the damage, never crashes") {
    Bytes smpte = header(0, 1, 0xE728); // -25 fps
    put_chunk(smpte, "MTrk", Track().on(0, 0, 60).off(40, 0, 60).end().data());
    Bytes format2 = header(2, 1, 480);
    put_chunk(format2, "MTrk", Track().on(0, 0, 60).off(480, 0, 60).end().data());
    // A 1/4 bar at 1 tick per quarter over 2^27 ticks would be 134 million bars.
    Bytes huge = header(0, 1, 1);
    put_chunk(huge, "MTrk",
              Track().time_sig(0, 1, 4).on(0, 0, 60).off(0x7FFFFFF, 0, 60).end().data());
    Bytes missing_track = header(1, 2, 480);
    put_chunk(missing_track, "MTrk", Track().bpm(0, 120).end().data());
    missing_track.insert(missing_track.end(), {'M', 'T', 'r'});
    Bytes huge_meta = header(0, 1, 480);
    put_chunk(huge_meta, "MTrk", {0x00, 0xFF, 0x03, 0x8F, 0xFF, 0xFF, 0x7F, 'x'});
    Bytes data_first = header(0, 1, 480);
    put_chunk(data_first, "MTrk", Track().raw(0, {60, 100}).end().data());
    Bytes no_notes = smf({Track().bpm(0, 90).time_sig(0, 3, 4).end()});
    Track over_cap; // one note more than any song may hold
    for (std::size_t i = 0; i < kMaxSongNotes + 1; ++i) {
        over_cap.on(0, 0, 40 + static_cast<int>(i % 40)).off(96, 0, 40 + static_cast<int>(i % 40));
    }
    struct Refused {
        const char* what;
        Bytes file;
        MidiFileError error;
    };
    const Refused refused[] = {
        {"SMPTE time division", smpte, MidiFileError::SmpteDivision},
        {"SMF format 2", format2, MidiFileError::UnsupportedFormat},
        {"absurdly many bars", huge, MidiFileError::TooLarge},
        {"a track missing", missing_track, MidiFileError::Truncated},
        {"a meta event longer than the file", huge_meta, MidiFileError::Corrupt},
        {"a data byte with no status", data_first, MidiFileError::Corrupt},
        {"no bytes", {}, MidiFileError::NotMidi},
        {"no notes", no_notes, MidiFileError::NoNotes},
        {"over the note cap", smf({over_cap.end()}), MidiFileError::TooLarge},
    };
    for (const Refused& r : refused) {
        CAPTURE(r.what);
        MidiLoadResult result = load(r.file);
        CHECK_FALSE(result.ok);
        CHECK(result.error == r.error);
        CHECK_FALSE(result.message.empty());
        CHECK(result.song.notes.empty());
    }

    SUBCASE("a track chunk longer than the file is read up to the end of its data") {
        Bytes body = Track().on(0, 0, 60).on(480, 0, 62).off(480, 0, 62).data();
        body.insert(body.end(), {0x81, 0x00, 0x90, 60}); // a note-on cut before its velocity
        MidiLoadResult r = load(cut_track_file(body, body.size() + 50));
        REQUIRE(r.ok);
        CHECK(r.stats.tracks_truncated == 1);
        REQUIRE(r.song.notes.size() == 2);
        // The unfinished first note ends at the last whole event, not at the cut one.
        CHECK(r.song.notes[0].end_tick == 960);
    }
    SUBCASE("every cut of a file, and bytes flipped in it, give an error or well-formed notes") {
        Track a;
        a.bpm(0, 100).time_sig(0, 3, 4).end();
        Track b;
        b.raw(0, {0xF0, 0x03, 0x7E, 0x7F, 0xF7})
            .on(0, 0, 60)
            .raw(0, {64, 80})
            .cc(100, 0, 64, 127)
            .raw(380, {0x90, 60, 0})
            .raw(0, {64, 0})
            .on(500, 0, 67)
            .end(480);
        const Bytes good = smf({a, b}, 96);
        REQUIRE(load(good).ok);
        // Cut at a few places and with garbage bytes in the middle: an error or well-formed notes.
        std::vector<Bytes> damaged;
        for (const std::size_t len : {std::size_t{5}, std::size_t{14}, good.size() / 2, good.size() - 1}) {
            damaged.emplace_back(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len));
        }
        Bytes noisy = good;
        for (std::size_t i = 14; i < noisy.size(); i += 3) {
            noisy[i] = static_cast<std::uint8_t>(0xFF - i);
        }
        damaged.push_back(noisy);
        for (const Bytes& bad : damaged) {
            const MidiLoadResult r = load(bad);
            if (!r.ok) {
                CHECK(r.error != MidiFileError::None);
            }
            for (const Note& n : r.song.notes) {
                CHECK(n.end_tick > n.start_tick);
            }
        }
    }
}

TEST_CASE("a real file: Bach, Prelude in C major") {
    MidiLoadResult r = load(read_test_file("bach-bwv846-prelude.mid"));
    REQUIRE(r.ok);
    const Song& song = r.song;
    CHECK(song.measures.size() == 35);
    CHECK(song.measures.front().number == 1);
    // Bar 1 is C4 E4 | G4 C5 E5 G4 C5 E5, played twice.
    REQUIRE(song.notes.size() > 16);
    const int bar1[] = {60, 64, 67, 72, 76, 67, 72, 76};
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(song.notes[i].pitch == bar1[i]);
        CHECK(song.notes[i].measure == 1);
    }
    CHECK(song.notes[16].measure == 2);
    CHECK(song.notes.back().measure == 35);
}

TEST_CASE("midi_writer: a real file written back reads as the same notes, velocity never 0") {
    MidiLoadOptions keep;
    keep.keep_ghost_notes = true;
    const Bytes original = read_test_file("bach-bwv846-prelude.mid");
    const MidiLoadResult first = load_midi(original, keep);
    REQUIRE(first.ok);
    for (bool running : {false, true}) {
        MidiWriteOptions opt;
        opt.running_status = running;
        const MidiWriteResult w = write_midi(first.song, opt);
        REQUIRE(w.ok);
        const MidiLoadResult second = load_midi(w.bytes, keep);
        REQUIRE(second.ok);
        REQUIRE(second.song.notes.size() == first.song.notes.size());
        for (std::size_t i = 0; i < first.song.notes.size(); ++i) {
            CHECK(second.song.notes[i].pitch == first.song.notes[i].pitch);
            CHECK(second.song.notes[i].start_tick == first.song.notes[i].start_tick);
            CHECK(second.song.notes[i].end_tick == first.song.notes[i].end_tick);
        }
        CHECK(second.song.measures.size() == first.song.measures.size());
    }

    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    Note quiet;
    quiet.start_tick = 0;
    quiet.end_tick = 100;
    quiet.pitch = 60;
    quiet.velocity = 0; // a note-on with velocity 0 would be read as a note-off
    s.notes = {quiet};
    const MidiWriteResult w = write_midi(s);
    REQUIRE(w.ok);
    const MidiLoadResult r = load_midi(w.bytes, keep);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].velocity == 1);
}

TEST_CASE("live bytes: notes, velocity-0 note-off and controllers decode; anything else is Other") {
    auto decode = [](std::initializer_list<std::uint8_t> bytes) {
        return decode_midi(bytes.begin(), bytes.size(), 123);
    };
    MidiEvent on = decode({0x93, 60, 100});
    CHECK(on.type == MidiEventType::NoteOn);
    CHECK(on.channel == 3);
    CHECK(on.data1 == 60);
    CHECK(on.data2 == 100);
    CHECK(on.time_ns == 123);
    CHECK(decode({0x85, 72, 40}).type == MidiEventType::NoteOff);
    CHECK(decode({0x90, 60, 0}).type == MidiEventType::NoteOff);
    CHECK(decode({0xB2, 64, 127}).type == MidiEventType::ControlChange);

    const std::vector<std::vector<std::uint8_t>> other = {
        {},     // empty
        {0x90}, // cut messages
        {0x90, 60},
        {0xB0, 64},
        {60, 100},                            // a data byte first
        {0x90, 0x80 | 60, 100},               // a data byte with the high bit set
        {0x90, 60, 100, 62, 100},             // too long
        {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7}, // SysEx
        {0xF0, 60, 100},                      // SysEx whose bytes look like a note
        {0xF8},
        {0xFE},
        {0xF2, 60, 100}, // clock, active sensing, system common
        {0xE0, 0, 64},
        {0xC0, 5},
        {0xA0, 60, 50},
        {0xD0, 50}, // messages the trainer ignores
    };
    for (const auto& bytes : other) {
        CAPTURE(bytes.size());
        CHECK(decode_midi(bytes.data(), bytes.size(), 0).type == MidiEventType::Other);
    }
}

TEST_CASE("live bytes: a stream becomes whole messages, however it is cut into reads") {
    using Chunks = std::vector<Bytes>;
    auto split_chunks = [](const Chunks& chunks) {
        std::vector<Bytes> out;
        MidiStreamSplitter splitter;
        ChannelMessage msg;
        for (const Bytes& chunk : chunks) {
            splitter.feed(chunk);
            while (splitter.next(msg)) {
                out.emplace_back(msg.bytes.begin(),
                                 msg.bytes.begin() + static_cast<long>(msg.size));
            }
        }
        return out;
    };
    struct Row {
        const char* what;
        Chunks chunks;
        std::vector<Bytes> messages;
    };
    const Row rows[] = {
        {"running status",
         {{0x90, 60, 100, 64, 90, 67, 80, 60, 0}},
         {{0x90, 60, 100}, {0x90, 64, 90}, {0x90, 67, 80}, {0x90, 60, 0}}},
        {"two-byte messages", {{0xC0, 5, 0xD2, 40}}, {{0xC0, 5}, {0xD2, 40}}},
        {"clock and active sensing, even inside a message",
         {{0xF8, 0x90, 60, 0xFE, 100, 0xF8}},
         {{0x90, 60, 100}}},
        {"realtime between running-status messages keeps the status",
         {{0x90, 60, 100, 0xF8, 64, 90}},
         {{0x90, 60, 100}, {0x90, 64, 90}}},
        {"SysEx whose bytes look like a note",
         {{0xF0, 0x43, 0x10, 0x4C, 0xF7, 0x90, 60, 100}},
         {{0x90, 60, 100}}},
        {"a SysEx continuation with no status", {{0x10, 0x4C, 0x00, 0xF7}}, {}},
        {"a SysEx cut off by a channel status",
         {{0xF0, 0x43, 0x10, 0x90, 60, 100}},
         {{0x90, 60, 100}}},
        {"a stray F7 before a note", {{0xF7, 0x90, 60, 100}}, {{0x90, 60, 100}}},
        {"a message cut by a new status byte", {{0x90, 60, 0x80, 60, 0}}, {{0x80, 60, 0}}},
        {"a message cut by the end of the chunk", {{0x90, 60, 100, 0x90, 64}}, {{0x90, 60, 100}}},
        {"system common cancels running status",
         {{0x90, 60, 100, 0xF2, 1, 2, 64, 90}},
         {{0x90, 60, 100}}},
        {"a note-on split across reads", {{0x90, 60}, {100}}, {{0x90, 60, 100}}},
        {"running status across reads, cut between data bytes",
         {{0x90, 60, 100, 60}, {0}},
         {{0x90, 60, 100}, {0x90, 60, 0}}},
        {"realtime inside a split message",
         {{0x90, 0xF8}, {60, 0xFE}, {0xF8, 100}},
         {{0x90, 60, 100}}},
        {"SysEx split across reads never becomes a note",
         {{0x90, 60, 100, 0xF0, 0x43, 0x10}, {0x4C, 0x00, 0x7F}, {0x10, 0xF7}, {0x90, 62, 80}},
         {{0x90, 60, 100}, {0x90, 62, 80}}},
        {"data after a SysEx's F7 has no status",
         {{0x90, 60, 100, 0xF0, 1}, {2, 0xF7}, {64, 90}},
         {{0x90, 60, 100}}},
        {"a message cut by a status byte in the next read",
         {{0x90, 60}, {0x80, 60, 0}},
         {{0x80, 60, 0}}},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        CHECK(split_chunks(row.chunks) == row.messages);
    }

    // Every way of cutting a stream into two reads, and one byte per read, gives the whole result.
    const Bytes stream = {0x90, 60, 100,  0xF0, 0x43, 0x10, 0x4C, 0xF8, 0xF7, 0x90, 62,   0, 0xB0,
                          123,  0,  0xF2, 1,    2,    64,   90,   0x90, 64,   0,    0xC0, 5};
    const auto whole = split_chunks({stream});
    for (std::size_t i = 0; i <= stream.size(); ++i) {
        CHECK(split_chunks({Bytes(stream.begin(), stream.begin() + static_cast<long>(i)),
                            Bytes(stream.begin() + static_cast<long>(i), stream.end())}) == whole);
    }
    Chunks singles;
    for (auto byte : stream) {
        singles.push_back({byte});
    }
    CHECK(split_chunks(singles) == whole);
}

TEST_CASE("live bytes: velocity 0, a repeated note-on, the pedal and All Notes Off") {
    auto event = [](MidiEventType type, int a, int b = 0) {
        return MidiEvent{0, type, 0, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)};
    };
    KeyState keys;
    keys.apply(event(MidiEventType::NoteOn, 60, 90));
    keys.apply(event(MidiEventType::NoteOn, 60, 90)); // not a second press
    keys.apply(event(MidiEventType::NoteOn, 64, 90));
    CHECK(keys.held_notes() == std::vector<std::uint8_t>{60, 64});
    keys.apply(event(MidiEventType::NoteOn, 60, 0)); // velocity 0 releases
    CHECK(keys.held_notes() == std::vector<std::uint8_t>{64});
    keys.apply(event(MidiEventType::NoteOff, 99));           // a release of a key that is up
    keys.apply(event(MidiEventType::ControlChange, 64, 63)); // 63 is pedal up, 64 is down
    CHECK_FALSE(keys.sustain_down());
    keys.apply(event(MidiEventType::ControlChange, 64, 64)); // pedal down holds nothing
    CHECK(keys.sustain_down());
    keys.apply(event(MidiEventType::NoteOff, 64));
    CHECK(keys.held_count() == 0);
    keys.apply(event(MidiEventType::NoteOn, 60, 90));
    keys.apply(event(MidiEventType::ControlChange, 123, 0));
    CHECK(keys.held_count() == 0);
}

TEST_CASE("input queue: a dropped note-off never leaves a key lit") {
    auto note = [](std::int64_t t, MidiEventType type, std::uint8_t pitch) {
        return MidiEvent{t, type, 0, pitch,
                         type == MidiEventType::NoteOn ? std::uint8_t{100} : std::uint8_t{0}};
    };
    auto pop_all = [](auto& q) {
        std::vector<MidiEvent> out;
        MidiEvent ev;
        while (q.pop(ev)) {
            out.push_back(ev);
        }
        return out;
    };
    MidiEventQueue<4> q; // 3 events fit, the last slot is kept for the resync
    q.push(note(1, MidiEventType::NoteOn, 60));
    q.push(note(2, MidiEventType::NoteOn, 62));
    q.push(note(3, MidiEventType::NoteOn, 64));
    q.push(note(4, MidiEventType::NoteOff, 60)); // dropped
    q.push(note(5, MidiEventType::NoteOff, 62)); // dropped too: one resync for the whole gap
    CHECK(q.dropped() == 2);
    CHECK(q.resyncs() == 1);
    KeyState keys;
    const auto out = pop_all(q);
    REQUIRE(out.size() == 4);
    CHECK(out[3].synthetic);
    CHECK(out[3].time_ns == 4); // stamped with the first dropped event
    for (const auto& ev : out) {
        keys.apply(ev);
    }
    CHECK(keys.held_count() == 0);
    // After the gap events apply normally again.
    q.push(note(6, MidiEventType::NoteOn, 67));
    for (const auto& ev : pop_all(q)) {
        keys.apply(ev);
    }
    CHECK(keys.held_notes() == std::vector<std::uint8_t>{67});
}

TEST_CASE("input queue: events from several ports come out in time order, ties by port") {
    auto on = [](std::int64_t t, std::uint8_t pitch) {
        return MidiEvent{t, MidiEventType::NoteOn, 0, pitch, 100};
    };
    std::vector<std::vector<MidiEvent>> sources = {
        {on(10, 60), on(40, 60)}, {on(5, 62), on(10, 62)}, {}, {on(20, 64)}};
    std::vector<std::size_t> next(8, 0);
    std::vector<std::pair<std::int64_t, std::size_t>> out;
    merge_by_time<8>(
        [&](std::size_t i, MidiEvent& ev) {
            if (i >= sources.size() || next[i] >= sources[i].size()) {
                return false;
            }
            ev = sources[i][next[i]++];
            return true;
        },
        [&](const MidiEvent& ev, std::size_t source) { out.emplace_back(ev.time_ns, source); },
        1000);
    using Out = std::vector<std::pair<std::int64_t, std::size_t>>;
    CHECK(out == Out{{5, 1}, {10, 0}, {10, 1}, {20, 3}, {40, 0}});
}

// ---- Recording a played performance to a .mid file

namespace {
constexpr std::int64_t kSec = 1'000'000'000;
constexpr std::int64_t kMs = 1'000'000;

MidiEvent played(std::int64_t time_ns, MidiEventType type, int data1, int data2 = 0) {
    return MidiEvent{time_ns, type, 0, static_cast<std::uint8_t>(data1),
                     static_cast<std::uint8_t>(data2)};
}
} // namespace

TEST_CASE("recorder: a velocity-0 note-on and a repeated pitch end the sounding note, the file "
          "reads back the same") {
    RecorderOptions opt;
    opt.lead_s = 0.0;
    Recorder rec(opt); // 120 BPM, 480 ticks a quarter: a second is 960 ticks
    rec.feed(played(0, MidiEventType::NoteOn, 60, 70));
    rec.feed(played(kSec, MidiEventType::NoteOn, 60, 0)); // velocity 0 is a note-off
    rec.feed(played(2 * kSec, MidiEventType::NoteOn, 62, 80));
    rec.feed(played(3 * kSec, MidiEventType::NoteOn, 62, 80)); // ends the first 62 there
    rec.feed(played(4 * kSec, MidiEventType::NoteOff, 62));
    rec.feed(played(4 * kSec, MidiEventType::ControlChange, kCcSustain, 127));
    const RecordedSong r = rec.finish(5 * kSec);
    REQUIRE(r.has_notes);
    REQUIRE(r.song.notes.size() == 3);
    CHECK(r.song.notes[0].end_tick == 960);
    CHECK(r.song.notes[1].end_tick == 2880);
    CHECK(r.song.notes[2].start_tick == 2880);

    const MidiWriteResult written = write_midi(r.song);
    REQUIRE(written.ok);
    MidiLoadOptions keep;
    keep.keep_ghost_notes = true;
    const MidiLoadResult back = load_midi(written.bytes, keep);
    REQUIRE(back.ok);
    REQUIRE(back.song.notes.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(back.song.notes[i].pitch == r.song.notes[i].pitch);
        CHECK(back.song.notes[i].start_tick == r.song.notes[i].start_tick);
        CHECK(back.song.notes[i].end_tick == r.song.notes[i].end_tick);
    }
    REQUIRE(back.song.events.size() == 1);
    CHECK(back.song.events[0].number == kCcSustain);
}

TEST_CASE("recorder: a note-off a few ms late does not end the note struck again") {
    RecorderOptions opt;
    opt.lead_s = 0.0;
    Recorder rec(opt);
    rec.feed(played(0, MidiEventType::NoteOn, 60, 80));
    // The new note-on (1.002 s) reaches the recorder before the old note-off (1.000 s).
    rec.feed(played(kSec + 2 * kMs, MidiEventType::NoteOn, 60, 80));
    rec.feed(played(kSec, MidiEventType::NoteOff, 60));
    rec.feed(played(2 * kSec, MidiEventType::NoteOff, 60));
    const RecordedSong r = rec.finish(3 * kSec);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].end_tick == 960);
    CHECK(r.song.notes[1].start_tick == 962);
    CHECK(r.song.notes[1].end_tick == 1920);
}
