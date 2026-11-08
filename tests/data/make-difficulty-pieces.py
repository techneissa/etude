#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Writes the three pieces the difficulty calibration reads (tests/data/difficulty-*.mid), made for
# this repo and CC0. Run it from anywhere:
#   python3 -I tests/data/make-difficulty-pieces.py
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


def conductor(bpm, fifths):
    us = round(60_000_000 / bpm)
    body = (ev(0, 0xFF, 0x03, 9, *b"Conductor") + ev(0, 0xFF, 0x51, 3, us >> 16, (us >> 8) & 255, us & 255)
            + ev(0, 0xFF, 0x58, 4, 4, 2, 24, 8) + ev(0, 0xFF, 0x59, 2, fifths & 255, 0) + ev(0, 0xFF, 0x2F, 0x00))
    return b"MTrk" + struct.pack(">I", len(body)) + body


def write(name, bpm, fifths, right, left):
    tracks = [conductor(bpm, fifths), track("Right Hand", right)]
    if left:
        tracks.append(track("Left Hand", left))
    data = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), TPQ) + b"".join(tracks)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)


BAR = 4 * TPQ

# C major scale, two octaves up and down in quarter notes at 100 BPM, right hand only.
scale = [60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 84]
walk = scale + scale[-2::-1]
write("difficulty-scale.mid", 100, 0,
      [(i * TPQ, p, TPQ - 40, 80) for i, p in enumerate(walk)], [])

# Broken chords C, Am, F, G7 twice at 90 BPM: the right hand plays 1-3-5-8-5-3 in eighths, the left
# a whole-note root.
chords = [(48, (0, 4, 7)), (45, (0, 3, 7)), (53, (0, 4, 7)), (43, (0, 4, 7, 10))] * 2
right, left = [], []
for bar, (root, iv) in enumerate(chords):
    t0 = bar * BAR
    left.append((t0, root, BAR - 40, 70))
    tones = [root + 12 + i for i in iv]
    cycle = [tones[0], tones[1], tones[2], tones[0] + 12, tones[2], tones[1]]
    for j in range(8):
        right.append((t0 + j * TPQ // 2, cycle[j % len(cycle)], TPQ // 2 - 30, 78))
write("difficulty-arpeggio.mid", 90, 0, right, left)

# A Chopin-like etude in D flat major at 168 BPM: sixteenth-note figuration in the right hand
# (broken chords over three octaves, a chromatic run, turns), a stride and arpeggio left hand with
# leaps of more than an octave, chords on the last beats. Twelve bars.
S = TPQ // 4
DB = 61  # D flat 4
major = [0, 2, 4, 5, 7, 9, 11]


def deg(n):  # n-th scale degree above D flat 4 (0 = D flat 4), any octave
    return DB + 12 * (n // 7) + major[n % 7]


figs = {
    "arp": [deg(d) for d in (0, 2, 4, 7, 9, 11, 14, 11, 9, 7, 4, 2, 0, 2, 4, 7)],
    "chrom": [DB + 3 + i for i in range(16)],
    "turn": [deg(d) for d in (5, 6, 7, 6, 5, 4, 5, 7, 9, 8, 7, 6, 5, 4, 3, 2)],
    "down": [deg(d) for d in (14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1)],
}
plan = ["arp", "arp", "chrom", "turn", "down", "arp", "turn", "chrom", "down", "arp", "turn", "arp"]
harm = [0, 0, 3, 4, 0, 3, 4, 0, 3, 4, 0, 0]  # chord roots as scale degrees: I IV V
right, left = [], []
for bar, name in enumerate(plan):
    t0 = bar * BAR
    fig = figs[name]
    if bar == 11:  # the last bar: a fast run then a spread chord
        fig = fig[:12]
    for j, p in enumerate(fig):
        right.append((t0 + j * S, p, S - 14, 84))
    if bar == 11:
        for p in (deg(0) + 12, deg(2) + 12, deg(4) + 12, deg(7) + 12):
            right.append((t0 + 12 * S, p, 4 * S, 100))
    root = deg(harm[bar]) - 24
    if bar % 2 == 0:  # stride: bass note, then a chord, leaps of 15 to 19 semitones
        for beat in range(4):
            if beat % 2 == 0:
                left.append((t0 + beat * TPQ, root - (5 if beat == 2 else 0), TPQ - 30, 80))
            else:
                for iv in (7, 12, 16):
                    left.append((t0 + beat * TPQ, root + 12 + iv, TPQ - 30, 72))
    else:  # arpeggio over two octaves in eighths
        for j, iv in enumerate((0, 7, 12, 16, 19, 16, 12, 7)):
            left.append((t0 + j * TPQ // 2, root + iv, TPQ // 2 - 20, 74))
write("difficulty-etude.mid", 168, -5, right, left)

# A toccata in F sharp major at 192 BPM, harder than the etude: the right hand plays sixteenth-note
# triads that jump up to a seventeenth between onsets over three octaves, the left hand eighth-note
# triplets (three against four) across three octaves, on black keys and chromatic passing notes.
# Twelve bars.
FS = 6  # F sharp
fs_major = [0, 2, 4, 5, 7, 9, 11]
right, left = [], []
for bar in range(12):
    t0 = bar * BAR
    for j in range(16):
        step = (j * 7 + bar * 5) % 24  # leaps of a fifth, folded over two octaves
        low = 54 + FS - 6 + fs_major[step % 7] + 12 * (step // 7) + (1 if j % 5 == 4 else 0)
        for iv in (0, 5, 12):
            right.append((t0 + j * S, low + iv, S - 10, 96))
    for j in range(12):
        step = (j * 5 + bar * 3) % 15
        root = 30 + FS + fs_major[step % 7] + 12 * (step // 7)
        left.append((t0 + j * (TPQ // 3), root, TPQ // 3 - 10, 90))
write("difficulty-toccata.mid", 192, 6, right, left)
