#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Writes the MIDI files of the Learn > Melodies pieces that we make ourselves.

    python3 -I scripts/make-melodies.py [OUT_DIR] [--from SRC_DIR]

OUT_DIR defaults to app/assets/melodies. Two kinds of piece are made here, each on two tracks
named "Right hand" and "Left hand" (one for a right-hand piece) so the loader knows the hands:

  * PIECES: a public-domain tune written out by hand below, checked against a printed score or a
    public-domain MIDI file (the manifest names it).
  * DERIVED: a piece taken note by note from a public-domain MIDI file (Mutopia, HymnWiki),
    with the hands, the key and the cut that the step asks for. It needs the downloaded files:
    SRC_DIR is searched, below, for each file's name, so unpack each download into a folder of
    its own (Mutopia pieces 75, 109, 528 and 1037; hymnwiki.org/Amazing_Grace). Without
    --from the derived pieces are left as they are.

A third kind, EASY, is a simplification of a piece already in OUT_DIR: the same right hand note
for note, byte for byte, and a left hand of one note a bar. It needs no download.

The files are dedicated CC0 where we wrote the notes and "public domain" where the notes are
a source's (see app/assets/melodies/manifest.txt). The other Mutopia pieces in the manifest are
not made here: they are downloaded and bundled as they come.

A piece is one string of bars per hand, bars separated by "|". A note is its name with the
octave (C4 is middle C, "F#4", "Bb3"), then its length: w whole, h half, q quarter, e eighth,
s sixteenth, a dot after it lengthens by half. "r" and a length is a rest. Notes joined by "+"
sound together ("E3+G3q"). The script checks that every bar of both hands has the length of
the time signature (the first bar may be a shorter pickup, the same in both hands).

    python3 -I scripts/make-melodies.py --cut IN.mid OUT.mid TICK

cuts a Standard MIDI File at TICK (notes that start before it are kept and end there; nothing
after it stays). It made fur-elise.mid from Mutopia piece 931, whose first section ends at tick
13536 of its 384 ticks a quarter.
"""
import os
import re
import struct
import sys

PPQ = 480
GAP = 24  # ticks a note is cut short, so a repeated note is heard again

LENGTH = {"w": 4.0, "h": 2.0, "q": 1.0, "e": 0.5, "s": 0.25}
NAME = re.compile(r"^([A-G])([#b]?)(-?\d)$")
STEP = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def pitch(name):
    m = NAME.match(name)
    if not m:
        raise ValueError("bad note name " + name)
    letter, accidental, octave = m.groups()
    shift = 1 if accidental == "#" else -1 if accidental == "b" else 0
    return 12 * (int(octave) + 1) + STEP[letter] + shift


def parse_token(token):
    """(list of pitches, beats): an empty list for a rest."""
    m = re.match(r"^(.*?)([whqes])(\.?)$", token)
    if not m:
        raise ValueError("bad token " + token)
    names, code, dot = m.groups()
    beats = LENGTH[code] * (1.5 if dot else 1.0)
    if names == "r":
        return [], beats
    return [pitch(n) for n in names.split("+")], beats


def parse_hand(text, bar_beats):
    bars = []
    for bar in text.split("|"):
        tokens = [parse_token(t) for t in bar.split()]
        bars.append(tokens)
    for i, bar in enumerate(bars):
        total = sum(beats for _, beats in bar)
        want = bar_beats
        if i == 0 and total < want - 1e-9:
            continue  # a shorter pickup: checked against the other hand below
        if abs(total - want) > 1e-9:
            raise ValueError("bar %d has %g beats, not %g: %s" % (i + 1, total, want, bar))
    return bars


def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def track(events):
    """events: (tick, order, bytes); the track's bytes with delta times and the end mark."""
    data = bytearray()
    last = 0
    for tick, _, payload in sorted(events, key=lambda e: (e[0], e[1])):
        data += vlq(tick - last) + payload
        last = tick
    data += vlq(0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + bytes(data)


def meta_text(kind, text):
    raw = text.encode("ascii")
    return b"\xff" + bytes([kind]) + vlq(len(raw)) + raw


def hand_track(name, bars, channel):
    events = [(0, 0, meta_text(3, name)), (0, 1, bytes([0xC0 | channel, 0]))]
    tick = 0
    for bar in bars:
        for pitches, beats in bar:
            length = round(beats * PPQ)
            for p in pitches:
                events.append((tick, 3, bytes([0x90 | channel, p, 84])))
                events.append((tick + length - GAP, 2, bytes([0x80 | channel, p, 0])))
            tick += length
    return track(events), tick


def write_file(path, spec, hands):
    """The file: a conductor track (name, tempo, time signature, key) and `hands`, the hand tracks."""
    num, den = spec["ts"]
    sf, minor = spec["key"]
    conductor = [
        (0, 0, meta_text(3, spec["title"])),
        (0, 1, b"\xff\x51\x03" + struct.pack(">I", round(60_000_000 / spec["bpm"]))[1:]),
        (0, 2, b"\xff\x58\x04" + bytes([num, den.bit_length() - 1, 24, 8])),
        (0, 3, b"\xff\x59\x02" + struct.pack(">bB", sf, minor)),
    ]
    chunks = [track(conductor)] + hands
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    with open(path, "wb") as f:
        f.write(header + b"".join(chunks))


def write_piece(path, spec):
    num, den = spec["ts"]
    bar_beats = num * 4.0 / den
    right = parse_hand(spec["rh"], bar_beats)
    left = parse_hand(spec["lh"], bar_beats) if spec.get("lh") else None
    if left is not None:
        if len(left) != len(right):
            raise ValueError("%s: %d bars right, %d left" % (spec["title"], len(right), len(left)))
        a = sum(b for _, b in right[0])
        b = sum(b for _, b in left[0])
        if a != b:
            raise ValueError("%s: pickups differ (%g and %g)" % (spec["title"], a, b))
    hands = [hand_track("Right hand", right, 0)[0]]
    if left is not None:
        hands.append(hand_track("Left hand", left, 1)[0])
    write_file(path, spec, hands)


def read_events(data, pos):
    """The events of the track at `pos`: (tick, kind, bytes), kind "on", "off", "end" or "other";
    and the position after the track. Bytes are the event as written, without its delta time."""
    assert data[pos:pos + 4] == b"MTrk"
    end = pos + 8 + struct.unpack(">I", data[pos + 4:pos + 8])[0]
    pos += 8
    tick = 0
    running = 0
    events = []
    while pos < end:
        delta = 0
        while True:
            b = data[pos]
            pos += 1
            delta = (delta << 7) | (b & 0x7F)
            if not b & 0x80:
                break
        tick += delta
        first = data[pos]
        if first == 0xFF:
            length, n = 0, pos + 2
            while True:
                b = data[n]
                n += 1
                length = (length << 7) | (b & 0x7F)
                if not b & 0x80:
                    break
            kind = "end" if data[pos + 1] == 0x2F else "other"
            events.append((tick, kind, data[pos:n + length]))
            pos = n + length
            continue
        if first in (0xF0, 0xF7):
            length, n = 0, pos + 1
            while True:
                b = data[n]
                n += 1
                length = (length << 7) | (b & 0x7F)
                if not b & 0x80:
                    break
            events.append((tick, "other", data[pos:n + length]))
            pos = n + length
            continue
        if first & 0x80:
            running = first
            pos += 1
        size = 1 if running & 0xF0 in (0xC0, 0xD0) else 2
        body = bytes([running]) + data[pos:pos + size]
        pos += size
        status = running & 0xF0
        kind = "other"
        if status == 0x90 and body[2] > 0:
            kind = "on"
        elif status == 0x80 or status == 0x90:
            kind = "off"
        events.append((tick, kind, body))
    return events, end


def cut_midi(src, dst, cut):
    data = open(src, "rb").read()
    assert data[:4] == b"MThd"
    fmt, count, division = struct.unpack(">HHH", data[8:14])
    pos = 14
    tracks = []
    for _ in range(count):
        events, pos = read_events(data, pos)
        kept = []
        sounding = {}
        # At one tick a track's other events come first, then the notes that end, then the notes that
        # start, so a repeated pitch ends before it is struck again.
        for tick, kind, raw in events:
            if kind == "end" or tick >= cut and kind != "off":
                continue
            if kind == "on":
                sounding[(raw[0] & 0x0F, raw[1])] = sounding.get((raw[0] & 0x0F, raw[1]), 0) + 1
            if kind == "off":
                key = (raw[0] & 0x0F, raw[1])
                if sounding.get(key, 0) > 0:
                    sounding[key] -= 1
                    kept.append((min(tick, cut), 1, raw))
                continue
            kept.append((tick, 2 if kind == "on" else 0, raw))
        for (channel, note), times in sounding.items():
            kept.extend([(cut, 1, bytes([0x80 | channel, note, 0]))] * times)
        tracks.append(track(kept))
    header = b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), division)
    with open(dst, "wb") as f:
        f.write(header + b"".join(tracks))

# ---- Pieces taken from a public-domain MIDI file ---------------------------------------------------

GRID = 0.25  # beats: the notes of a derived piece start and end on this grid
GRACE = 0.125  # beats: a note shorter than this is an ornament, left out


def read_notes(path):
    """The tracks of a Standard MIDI File: [(name, [(start beat, end beat, pitch)])]."""
    data = open(path, "rb").read()
    assert data[:4] == b"MThd"
    count, division = struct.unpack(">HH", data[10:14])
    pos = 14
    tracks = []
    for _ in range(count):
        events, pos = read_events(data, pos)
        name, sounding, notes = "", {}, []
        for tick, kind, raw in events:
            if kind == "other" and raw[:2] == b"\xff\x03":
                name = raw[3:3 + raw[2]].decode("latin-1")
            elif kind == "on":
                sounding[(raw[0] & 0x0F, raw[1])] = tick
            elif kind == "off" and (raw[0] & 0x0F, raw[1]) in sounding:
                start = sounding.pop((raw[0] & 0x0F, raw[1]))
                notes.append((start / division, tick / division, raw[1]))
        tracks.append((name, sorted(notes)))
    return tracks


def quantise(notes):
    """Notes on the grid, ornaments dropped, a repeated pitch ending where the next begins."""
    out = []
    for start, end, pitch in notes:
        if end - start < GRACE:
            continue
        s = round(start / GRID) * GRID
        out.append([s, max(round(end / GRID) * GRID, s + GRID), pitch])
    out.sort(key=lambda n: (n[2], n[0]))
    for a, b in zip(out, out[1:]):
        if a[2] == b[2] and a[1] > b[0]:
            a[1] = b[0]
            assert a[1] > a[0], "two notes of one pitch begin together"
    return sorted(out)


def pick(tracks, how, which):
    """The notes of track `which` (a name, or the number among the tracks that have notes): all of
    them ("all"), the highest of each start ("top") or the others ("rest")."""
    played = [(name, n) for name, n in tracks if n]
    notes = played[which][1] if isinstance(which, int) else dict(played)[which]
    notes = quantise(notes)
    if how == "all":
        return notes
    # A note is the melody when nothing higher is sounding as it starts: the voice that holds a
    # note while a lower voice moves keeps it.
    melody = []
    for note in notes:
        higher = [m for m in notes if m[0] < note[0] < m[1] and m[2] > note[2]]
        same = [m for m in notes if m[0] == note[0] and m[2] > note[2]]
        if not higher and not same:
            melody.append(note)
    return [n for n in notes if any(n is m for m in melody) == (how == "top")]


def derived_hand(name, notes, spec, channel):
    """The track of `notes` for the piece `spec`: cut to its start and end, moved by its shift."""
    start, end = spec.get("start", 0.0), spec.get("end", 1e9)
    events = [(0, 0, meta_text(3, name)), (0, 1, bytes([0xC0 | channel, 0]))]
    kept = []
    for s, e, pitch in notes:
        if s < start or s >= end:
            continue
        kept.append((s - start, min(e, end) - start, pitch + spec.get("shift", 0)))
    for s, e, pitch in kept:
        assert 0 <= pitch < 128
        events.append((round(s * PPQ), 3, bytes([0x90 | channel, pitch, 84])))
        events.append((round(e * PPQ) - GAP, 2, bytes([0x80 | channel, pitch, 0])))
    return track(events), kept


def write_derived(path, spec, src):
    tracks = read_notes(src)
    hands = []
    for channel, (name, how, which) in enumerate(spec["hands"]):
        data, kept = derived_hand(name, pick(tracks, how, which), spec, channel)
        hands.append(data)
        pitches = [p for _, _, p in kept]
        print("  %s: %d notes, MIDI %d-%d, ends at beat %g" %
              (name, len(kept), min(pitches), max(pitches), max(e for _, e, _ in kept)))
    write_file(path, spec, hands)


DERIVED = {
    # Mutopia 75 (Menuet, Notebook for Anna Magdalena Bach, BWV Anh. 114): both strains, played
    # twice as the file has them, both hands as Mutopia has them; the grace note in bar 7 is left out.
    "minuet-in-g": dict(
        title="Minuet in G", bpm=112, ts=(3, 4), key=(1, 0), source="anna-magdalena-04.mid",
        hands=[("Right hand", "all", "one:"), ("Left hand", "all", "two:")],
    ),
    # Mutopia 528 (Ode to Joy, a hymn in four parts): the highest voice, a fifth down so that it
    # sits on the white keys.
    "ode-to-joy": dict(
        title="Ode to Joy", bpm=100, ts=(4, 4), key=(0, 0), source="ode.mid", shift=-7,
        hands=[("Right hand", "top", "upper")],
    ),
    # Mutopia 109 (Greensleeves, lute): the verse (bars 1 to 16, to where the refrain begins); the
    # melody is the highest note of each beat, the rest is the bass.
    "greensleeves": dict(
        title="Greensleeves", bpm=100, ts=(3, 4), key=(0, 1), source="Greensleaves.mid", end=49.0,
        hands=[("Right hand", "top", 0), ("Left hand", "rest", 0)],
    ),
    # Mutopia 1037 (Wiegenlied, Op. 49 No. 4): from the voice's first note (the piano's two bars of
    # introduction are left out); the voice and the piano's bass; a minor third down, from E-flat to C.
    "brahms-lullaby": dict(
        title="Brahms' Lullaby", bpm=80, ts=(3, 4), key=(0, 0), source="Wiegenlied.mid",
        start=5.5, shift=-3,
        hands=[("Right hand", "all", "mel"), ("Left hand", "all", "lower")],
    ),
    # HymnWiki, Amazing Grace, tune New Britain with parts by E. O. Excell (1900): the soprano.
    "amazing-grace": dict(
        title="Amazing Grace", bpm=72, ts=(3, 4), key=(1, 0), source="Amazing_Grace.mid",
        hands=[("Right hand", "top", 0)],
    ),
}

# ---- A simplified arrangement of a piece in OUT_DIR ------------------------------------------------

EASY = {
    # brahms-lullaby.mid with its right hand kept as it is and a left hand of one note a bar, held
    # for the three beats, from the first bar (the pickup has none). Each note is the root of that
    # bar's chord and one the original's own left hand plays in the bar:
    #   bars 1-3, 8, 10, 12, 14, 16: C3 (the original has C2 on every downbeat; C3 is in its range);
    #   bars 4-7, 11, 15: G2, the dominant (the melody holds F, D, B or A over it; the original plays
    #     G2 on the third beat of each of these bars and keeps C2 under it);
    #   bars 9 and 13: F2, the subdominant (the melody is C, A, F; the original plays F2 and F3).
    # F2, G2 and C3 lie in one five-finger position, a fifth wide (MIDI 41 to 48).
    "brahms-lullaby-easy": dict(
        of="brahms-lullaby", title="Brahms' Lullaby (Easy)",
        lh="C3 C3 C3 G2 G2 G2 G2 C3 F2 C3 G2 C3 F2 C3 G2 C3",
    ),
}


def write_easy(path, spec, original):
    """The file: the original's conductor and right-hand tracks as they are (the title changed),
    and the left hand `spec["lh"]`, a note for each bar after the pickup."""
    data = open(original, "rb").read()
    assert data[:4] == b"MThd"
    count, division = struct.unpack(">HH", data[10:14])
    assert division == PPQ
    pos = 14
    chunks = []
    for _ in range(count):
        events, pos = read_events(data, pos)
        chunks.append(events)
    order = {"other": 0, "off": 2, "on": 3}

    def rebuilt(events, retitle=None):
        out = []
        for tick, kind, raw in events:
            if kind == "end":
                continue
            if retitle and raw[:2] == b"\xff\x03":
                raw = meta_text(3, retitle)
            out.append((tick, order[kind], raw))
        return out

    conductor = rebuilt(chunks[0], spec["title"])
    right = rebuilt(chunks[1])
    assert any(e[2] == meta_text(3, "Right hand") for e in right)
    bar = 3 * PPQ
    pickup = PPQ  # the original's voice starts with a one-beat pickup
    end = max(tick for tick, kind, _ in chunks[1] if kind == "off") + GAP
    bars = spec["lh"].split()
    left = [(0, 0, meta_text(3, "Left hand")), (0, 1, bytes([0xC1, 0]))]
    for i, name in enumerate(bars):
        start = pickup + i * bar
        stop = min(start + bar, end) - GAP
        left.append((start, 3, bytes([0x91, pitch(name), 84])))
        left.append((stop, 2, bytes([0x81, pitch(name), 0])))
    assert pickup + (len(bars) - 1) * bar < end, "the last bar of the left hand starts after the music ends"
    header = b"MThd" + struct.pack(">IHHH", 6, 1, 3, PPQ)
    with open(path, "wb") as f:
        f.write(header + track(conductor) + track(right) + track(left))
    # The right hand's track is the original's, byte for byte.
    mine = open(path, "rb").read()
    assert track_bytes(mine, 1) == track_bytes(data, 1), "the right hand changed"
    print("  %s: %d left-hand notes, MIDI %d-%d" %
          (os.path.basename(path), len(bars), min(pitch(n) for n in bars), max(pitch(n) for n in bars)))


def track_bytes(data, index):
    """The bytes of the `index`th track chunk of a Standard MIDI File."""
    pos = 14
    for _ in range(index):
        pos += 8 + struct.unpack(">I", data[pos + 4:pos + 8])[0]
    return data[pos:pos + 8 + struct.unpack(">I", data[pos + 4:pos + 8])[0]]


def find_source(root, name):
    for folder, _, files in os.walk(root):
        if name in files:
            return os.path.join(folder, name)
    raise SystemExit("no %s under %s" % (name, root))


PIECES = {
    "hot-cross-buns": dict(
        title="Hot Cross Buns", bpm=90, ts=(4, 4), key=(0, 0),
        rh="E4q D4q C4h | E4q D4q C4h | C4e C4e C4e C4e D4e D4e D4e D4e | E4q D4q C4h",
    ),
    "twinkle-twinkle-little-star": dict(
        title="Twinkle Twinkle Little Star", bpm=96, ts=(4, 4), key=(0, 0),
        rh="C4q C4q G4q G4q | A4q A4q G4h | F4q F4q E4q E4q | D4q D4q C4h"
           " | G4q G4q F4q F4q | E4q E4q D4h | G4q G4q F4q F4q | E4q E4q D4h"
           " | C4q C4q G4q G4q | A4q A4q G4h | F4q F4q E4q E4q | D4q D4q C4h",
    ),
    # The Baltimore print of about 1850 (Duke University scan, on Wikimedia Commons), in its key: the
    # pickup, the eight bars of the verse and the eight of the chorus; bar 4 and bar 12 hold the A a
    # beat and rest the half beat before the next pickup, as the print has them. The chorus ends on
    # a half-note G.
    "oh-susanna": dict(
        title="Oh! Susanna", bpm=88, ts=(2, 4), key=(1, 0),
        rh="G4s A4s | B4e D5e D5e. E5s | D5e B4e G4e. A4s | B4e B4e A4e G4e | A4q re G4s A4s"
           " | B4e D5e D5e. E5s | D5e B4e G4e. A4s | B4e B4e A4e A4e | G4q rq"
           " | C5q C5q | E5e E5q E5e | D5e D5e B4e G4e | A4q re G4s A4s"
           " | B4e D5e D5e. E5s | D5e B4e G4e. A4s | B4e B4e A4e A4e | G4h",
    ),
    # The familiar chorus (the 1857 print has another), after the public-domain refrain on Wikimedia
    # Commons (Jingle Bells refrain vector, in G), a fifth lower and every note twice as long; the
    # phrase ends the print's way: a note, then a rest.
    "jingle-bells": dict(
        title="Jingle Bells", bpm=120, ts=(4, 4), key=(0, 0),
        rh="E4q E4q E4h | E4q E4q E4h | E4q G4q C4q. D4e | E4h rh"
           " | F4q F4q F4q. F4e | F4q E4q E4q E4e E4e | E4q D4q D4q E4q | D4h G4h"
           " | E4q E4q E4h | E4q E4q E4h | E4q G4q C4q. D4e | E4h rh"
           " | F4q F4q F4q. F4e | F4q E4q E4q E4e E4e | G4q G4q F4q D4q | C4h. rq",
    ),
}


def main():
    args = sys.argv[1:]
    if len(args) == 4 and args[0] == "--cut":
        cut_midi(args[1], args[2], int(args[3]))
        return
    source = None
    if "--from" in args:
        i = args.index("--from")
        source = args[i + 1]
        del args[i:i + 2]
    here = os.path.dirname(os.path.abspath(__file__))
    out = args[0] if args else os.path.join(here, "..", "app", "assets", "melodies")
    os.makedirs(out, exist_ok=True)
    for name, spec in PIECES.items():
        write_piece(os.path.join(out, name + ".mid"), spec)
        print("wrote", name + ".mid")
    for name, spec in DERIVED.items():
        if source is None:
            print("left", name + ".mid (no --from)")
            continue
        print("wrote", name + ".mid")
        write_derived(os.path.join(out, name + ".mid"), spec, find_source(source, spec["source"]))
    for name, spec in EASY.items():
        write_easy(os.path.join(out, name + ".mid"), spec, os.path.join(out, spec["of"] + ".mid"))
        print("wrote", name + ".mid")


if __name__ == "__main__":
    main()
