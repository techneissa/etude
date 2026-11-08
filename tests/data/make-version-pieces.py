#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Writes the pieces the version-grouping tests read (tests/data/lantern-waltz*.mid and
# harbour-march.mid), made for this repo and CC0: an original tune in three arrangements and a
# second, unrelated tune. Run it from anywhere:
#   python3 -I tests/data/make-version-pieces.py
import os
import struct

OUT = os.path.dirname(os.path.abspath(__file__))
TPQ = 480


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def ev(delta, *data):
    return vlq(delta) + bytes(data)


def track(name, notes, extra=()):
    """notes: (start_tick, pitch, length_ticks, velocity)."""
    raw = []
    for start, pitch, length, vel in notes:
        raw.append((start, 1, pitch, vel))
        raw.append((start + length, 0, pitch, 0))
    raw.sort()
    body = ev(0, 0xFF, 0x03, len(name), *name.encode()) + b"".join(extra)
    now = 0
    for tick, on, pitch, vel in raw:
        body += ev(tick - now, 0x90 if on else 0x80, pitch, vel)
        now = tick
    body += ev(0, 0xFF, 0x2F, 0x00)
    return b"MTrk" + struct.pack(">I", len(body)) + body


def conductor(title, bpm, fifths, beats):
    """The first track names the piece (the library reads its title there)."""
    us = round(60_000_000 / bpm)
    body = (ev(0, 0xFF, 0x03, len(title), *title.encode()) + ev(0, 0xFF, 0x51, 3, us >> 16, (us >> 8) & 255, us & 255)
            + ev(0, 0xFF, 0x58, 4, beats, 2, 24, 8) + ev(0, 0xFF, 0x59, 2, fifths & 255, 0) + ev(0, 0xFF, 0x2F, 0x00))
    return b"MTrk" + struct.pack(">I", len(body)) + body


def write(name, title, bpm, fifths, beats, right, left, shift=0):
    shifted = [(t, p + shift, ln, v) for t, p, ln, v in right]
    shifted_left = [(t, p + shift, ln, v) for t, p, ln, v in left]
    tracks = [conductor(title, bpm, fifths, beats), track("Right Hand", shifted)]
    if shifted_left:
        tracks.append(track("Left Hand", shifted_left))
    data = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), TPQ) + b"".join(tracks)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)


def melody(bars, beats_per_bar, vel=84):
    """bars: one list per bar of (pitch, beats); a pitch of None is a rest."""
    notes = []
    for bar, items in enumerate(bars):
        t = bar * beats_per_bar * TPQ
        for pitch, beats in items:
            length = round(beats * TPQ)
            if pitch is not None:
                notes.append((t, pitch, max(length - 20, length // 2), vel))
            t += length
    return notes


# ---- Lantern Waltz: 3/4 in C, a tune of twelve bars. The first eight bars are what the version
# grouping reads.
G4, B4, C5, D5, E5, F5, G5, A4 = 67, 71, 72, 74, 76, 77, 79, 69
SKELETON = [
    [(G4, 1), (C5, 1), (E5, 1)],
    [(D5, 2), (B4, 1)],
    [(C5, 1), (A4, 1), (F5, 1)],
    [(E5, 3)],
    [(A4, 1), (D5, 1), (F5, 1)],
    [(E5, 2), (C5, 1)],
    [(B4, 1), (G5, 1), (D5, 1)],
    [(C5, 3)],
    [(E5, 1), (D5, 1), (C5, 1)],
    [(B4, 2), (A4, 1)],
    [(G4, 1), (B4, 1), (D5, 1)],
    [(C5, 3)],
]

# The original: the tune with its ornaments (a lower neighbour before the F in bar 3, a trill on the
# E of bar 4, a turn at the end of bar 6, a grace note before the last C of the first strain).
ORIGINAL = [
    [(G4, 1), (C5, 1), (E5, 1)],
    [(D5, 2), (B4, 1)],
    [(C5, 1), (A4, 1), (E5, 0.25), (F5, 0.75)],
    [(E5, 1.5), (F5, 0.25), (E5, 0.25), (F5, 0.25), (E5, 0.75)],
    [(A4, 1), (D5, 1), (F5, 1)],
    [(E5, 1.5), (D5, 0.25), (E5, 0.25), (C5, 1)],
    [(B4, 1), (G5, 1), (D5, 1)],
    [(D5, 0.25), (C5, 2.75)],
    [(E5, 1), (D5, 1), (C5, 1)],
    [(B4, 2), (A4, 1)],
    [(G4, 1), (B4, 1), (D5, 1)],
    [(C5, 3)],
]

# The easy arrangement: the same tune with the passing notes left out and longer notes.
EASY = [
    [(G4, 1), (C5, 1), (E5, 1)],
    [(D5, 3)],
    [(C5, 1), (A4, 1), (F5, 1)],
    [(E5, 3)],
    [(A4, 1), (D5, 1), (F5, 1)],
    [(E5, 3)],
    [(B4, 1), (G5, 2)],
    [(C5, 3)],
    [(E5, 1), (D5, 1), (C5, 1)],
    [(B4, 3)],
    [(G4, 1), (B4, 1), (D5, 1)],
    [(C5, 3)],
]

BASS = [48, 43, 41, 36, 41, 48, 43, 48, 48, 43, 43, 48]  # one root a bar


def waltz_left(full):
    notes = []
    for bar, root in enumerate(BASS):
        t0 = bar * 3 * TPQ
        notes.append((t0, root - 12, 3 * TPQ - 40, 70) if not full else (t0, root - 12, TPQ - 30, 72))
        if full:
            for beat in (1, 2):
                for iv in (4, 7):
                    notes.append((t0 + beat * TPQ, root + iv, TPQ - 30, 62))
    return notes


write("lantern-waltz.mid", "Lantern Waltz", 108, 0, 3, melody(ORIGINAL, 3), waltz_left(True))
write("lantern-waltz-easy.mid", "Lantern Waltz (Easy)", 100, 0, 3, melody(EASY, 3), waltz_left(False))
# The original again a tone up, in D, under another name.
write("lantern-waltz-in-d.mid", "Lantern Waltz in D", 108, 2, 3, melody(ORIGINAL, 3), waltz_left(True), shift=2)

# ---- Harbour March: 4/4 in C, another tune.
HARBOUR = [
    [(67, 1), (74, 1), (71, 1), (67, 1)],
    [(69, 1), (76, 1), (72, 1), (69, 1)],
    [(71, 2), (74, 1), (77, 1)],
    [(76, 2), (72, 2)],
    [(74, 1), (71, 1), (67, 1), (62, 1)],
    [(64, 1), (69, 1), (72, 1), (76, 1)],
    [(77, 1), (76, 1), (74, 1), (71, 1)],
    [(72, 4)],
]
left = []
for bar in range(8):
    t0 = bar * 4 * TPQ
    for beat in range(4):
        left.append((t0 + beat * TPQ, 48 if beat % 2 == 0 else 55, TPQ - 40, 70))
write("harbour-march.mid", "Harbour March", 112, 0, 4, melody(HARBOUR, 4), left)
