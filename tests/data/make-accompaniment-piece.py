#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Writes tests/data/accompaniment-trio.mid, made for this repo and CC0: a short waltz for the piano
# (two staves) with a violin and a choir part over it, so the song has two accompaniment parts of
# different kinds. The accompaniment button's screenshots and the icon audit read it. Run it from
# anywhere:
#   python3 -I tests/data/make-accompaniment-piece.py
import os
import struct

OUT = os.path.dirname(os.path.abspath(__file__))
TPQ = 480
BARS = 8


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def ev(delta, *data):
    return vlq(delta) + bytes(data)


def track(name, channel, program, notes):
    """notes: (start_tick, pitch, length_ticks, velocity)."""
    raw = []
    for start, pitch, length, vel in notes:
        raw.append((start, 1, pitch, vel))
        raw.append((start + length, 0, pitch, 0))
    raw.sort()
    body = ev(0, 0xFF, 0x03, len(name), *name.encode()) + ev(0, 0xC0 | channel, program)
    now = 0
    for tick, on, pitch, vel in raw:
        body += ev(tick - now, (0x90 if on else 0x80) | channel, pitch, vel)
        now = tick
    body += ev(0, 0xFF, 0x2F, 0x00)
    return b"MTrk" + struct.pack(">I", len(body)) + body


def conductor(title, bpm):
    us = round(60_000_000 / bpm)
    body = (ev(0, 0xFF, 0x03, len(title), *title.encode()) + ev(0, 0xFF, 0x51, 3, us >> 16, (us >> 8) & 255, us & 255)
            + ev(0, 0xFF, 0x58, 4, 3, 2, 24, 8) + ev(0, 0xFF, 0x59, 2, 0, 0) + ev(0, 0xFF, 0x2F, 0x00))
    return b"MTrk" + struct.pack(">I", len(body)) + body


# C, G, Am, F in 3/4: the right hand a tune of quarter notes, the left a bass and two chord notes.
CHORDS = [(48, (64, 67)), (43, (62, 67)), (45, (64, 69)), (41, (65, 69))]
TUNE = [72, 74, 76, 77, 76, 74, 72, 71, 72, 69, 71, 72, 74, 72, 71, 69]

right, left, violin, choir = [], [], [], []
for bar in range(BARS):
    base = bar * 3 * TPQ
    root, (a, b) = CHORDS[bar % 4]
    left.append((base, root, TPQ - 40, 70))
    left.append((base + TPQ, a, TPQ - 40, 55))
    left.append((base + 2 * TPQ, b, TPQ - 40, 55))
    for beat in range(3):
        right.append((base + beat * TPQ, TUNE[(bar * 3 + beat) % len(TUNE)], TPQ - 60, 85))
    violin.append((base, 60 + (bar % 4) * 2, 3 * TPQ - 60, 70))
    choir.append((base, 55 + (bar % 4), 3 * TPQ - 60, 65))

tracks = [
    conductor("Accompaniment trio", 96),
    track("Right Hand", 0, 0, right),
    track("Left Hand", 0, 0, left),
    track("Violin", 1, 40, violin),
    track("Choir", 2, 52, choir),
]
data = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), TPQ) + b"".join(tracks)
with open(os.path.join(OUT, "accompaniment-trio.mid"), "wb") as f:
    f.write(data)
