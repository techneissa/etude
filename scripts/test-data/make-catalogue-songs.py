#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Writes the stand-in songs the catalogue sites serve (scripts/test-data/catalogue/),
# where a real site's file would be. Run it from anywhere:
#   python3 -I scripts/test-data/make-catalogue-songs.py
# Everything here is made for this repo and is CC0; no note comes from the pieces the listings name.
import os
import struct

SITES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "catalogue")
PPQ = 480


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def meta(kind, data):
    return bytes([0xFF, kind]) + vlq(len(data)) + data


def track(name, notes, channel=0):
    """notes: (start tick, length in ticks, pitch, velocity)."""
    events = []
    for start, length, pitch, vel in notes:
        events.append((start, 1, bytes([0x90 | channel, pitch, vel])))
        events.append((start + length, 0, bytes([0x80 | channel, pitch, 0])))
    events.sort(key=lambda e: (e[0], e[1]))
    body = vlq(0) + meta(0x03, name.encode())
    now = 0
    for tick, _, data in events:
        body += vlq(tick - now) + data
        now = tick
    body += vlq(0) + meta(0x2F, b"")
    return b"MTrk" + struct.pack(">I", len(body)) + body


def song(path, title, bpm, num, den, tracks):
    tempo = vlq(0) + meta(0x03, title.encode())
    tempo += vlq(0) + meta(0x58, bytes([num, {2: 1, 4: 2, 8: 3}[den], 24, 8]))
    tempo += vlq(0) + meta(0x51, (60_000_000 // bpm).to_bytes(3, "big"))
    tempo += vlq(0) + meta(0x2F, b"")
    data = b"MThd" + struct.pack(">IHHH", 6, 1, 1 + len(tracks), PPQ)
    data += b"MTrk" + struct.pack(">I", len(tempo)) + tempo
    for name, notes in tracks:
        data += track(name, notes)
    full = os.path.join(SITES, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(data)
    print(f"{len(data):6d} {path}")


# Broken chords, eight sixteenths a half bar, over a slow bass: a study in C.
STUDY = [
    (48, [60, 64, 67]), (48, [62, 65, 69]), (47, [62, 67, 71]), (48, [64, 67, 72]),
    (45, [64, 69, 72]), (50, [65, 69, 74]), (43, [62, 65, 71]), (48, [64, 67, 72]),
    (41, [65, 69, 72]), (43, [65, 71, 74]), (45, [64, 67, 72]), (40, [62, 67, 71]),
    (41, [60, 65, 69]), (43, [62, 65, 71]), (48, [64, 67, 72]), (48, [60, 64, 67]),
]


def study(bars):
    right, left = [], []
    sixteenth = PPQ // 4
    for bar in range(bars):
        bass, chord = STUDY[bar % len(STUDY)]
        at = bar * 4 * PPQ
        for half in range(2):
            h = at + half * 2 * PPQ
            left.append((h, 2 * PPQ - 10, bass, 70))
            figure = chord + [chord[0] + 12, chord[1] + 12] + chord[::-1][:3]
            for i, pitch in enumerate(figure[:8]):
                right.append((h + i * sixteenth, sixteenth - 10, pitch, 80 if i == 0 else 64))
    return [("Right hand", right), ("Left hand", left)]


# A slow waltz: a bass on one, a chord on two and three, a long tune above.
WALTZ = [
    (43, [59, 62, 66], [78, 76]), (38, [57, 61, 66], [74, 76]), (43, [59, 62, 66], [78, 79]),
    (38, [57, 61, 66], [76, 74]), (40, [59, 64, 67], [71, 72]), (45, [57, 60, 64], [74, 72]),
    (43, [59, 62, 67], [71, 69]), (38, [57, 62, 66], [67, 66]),
]


def waltz(bars):
    right, left = [], []
    beat = PPQ
    for bar in range(bars):
        bass, chord, tune = WALTZ[bar % len(WALTZ)]
        at = bar * 3 * beat
        left.append((at, beat - 20, bass, 60))
        for b in (1, 2):
            for pitch in chord:
                left.append((at + b * beat, beat - 20, pitch, 48))
        right.append((at, 2 * beat - 20, tune[0], 72))
        right.append((at + 2 * beat, beat - 20, tune[1], 66))
    return [("Right hand", right), ("Left hand", left)]


song("www.mutopiaproject.org/ftp/BachJS/BWV846/prelude/prelude.mid", "Study in C", 72, 4, 4,
     study(32))
for path in ("www.mutopiaproject.org/ftp/SatieE/gymnopedie1/gymnopedie1.mid",
             "www.mutopiaproject.org/ftp/ChopinFF/O28/chopin-28-4/chopin-28-4.mid",
             "www.piano-midi.de/midis/chopin/chpn_op28_4.mid"):
    song(path, "Slow waltz", 66, 3, 4, waltz(32))
# Two more versions of the first listing's piece (Get songs groups versions of one piece): a simple
# one, the right hand's broken chords alone and slower, and a fuller one, faster.
song("www.mutopiaproject.org/ftp/BachJS/BWV846/prelude-easy/prelude-easy.mid", "Study in C (easy)",
     56, 4, 4, study(16)[:1])
song("www.mutopiaproject.org/ftp/BachJS/BWV846/prelude-advanced/prelude-advanced.mid",
     "Study in C (advanced)", 84, 4, 4, study(48))
