#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Writes the small edge-case files the import tests read (tests/data/edge-*). Run it from anywhere:
#   python3 -I tests/data/make-edge-cases.py
# The files are written next to this script. Everything here is made for this repo and is CC0.
import os
import struct

OUT = os.path.dirname(os.path.abspath(__file__))


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def ev(delta, *data):
    return vlq(delta) + bytes(data)


def trk(*events):
    body = b"".join(events)
    return b"MTrk" + struct.pack(">I", len(body)) + body


EOT = ev(0, 0xFF, 0x2F, 0x00)


def smf(tracks, division=480):
    fmt = 1 if len(tracks) > 1 else 0
    return b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), division) + b"".join(tracks)


def sig(delta, numerator, denominator):
    power = {1: 0, 2: 1, 4: 2, 8: 3}[denominator]
    return ev(delta, 0xFF, 0x58, 4, numerator, power, 24, 8)


def write(name, data):
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(os.path.join(OUT, name), mode) as f:
        f.write(data)


# Eight 32nd notes at 300 BPM, 25 ms each: under 30 ms, but real notes.
events = [ev(0, 0xFF, 0x51, 3, 0x03, 0x0D, 0x40)]
for i in range(8):
    events += [ev(0, 0x90, 60 + i % 3, 64), ev(60, 0x80, 60 + i % 3, 0)]
write("edge-fast-32nds.mid", smf([trk(*events, EOT)]))

# The signatures live in a conductor track; the notes in a second one.
# A full 3/4 bar, then 4/4 from tick 1440. Notes C4 at 0, D4 at 1440, E4 at 3360: bars 1, 2, 3.
write("edge-3-4-then-4-4.mid", smf([
    trk(sig(0, 3, 4), sig(1440, 4, 4), EOT),
    trk(ev(0, 0x90, 60, 64), ev(480, 0x80, 60, 0),
        ev(960, 0x90, 62, 64), ev(480, 0x80, 62, 0),
        ev(1440, 0x90, 64, 64), ev(480, 0x80, 64, 0), EOT)]))

# 4/4 from tick 0, 3/4 from tick 960 (beat 3 of the first bar). C4 at 0, D4 at 1920.
write("edge-sig-midbar.mid", smf([
    trk(sig(0, 4, 4), sig(960, 3, 4), EOT),
    trk(ev(0, 0x90, 60, 64), ev(480, 0x80, 60, 0),
        ev(1440, 0x90, 62, 64), ev(480, 0x80, 62, 0), EOT)]))

# A one-beat pickup written as 1/4 at tick 0, then 4/4 from tick 480.
# G4 at 0, C4 at 480, D4 at 2400, like edge-pickup.musicxml.
write("edge-pickup.mid", smf([
    trk(sig(0, 1, 4), sig(480, 4, 4), EOT),
    trk(ev(0, 0x90, 67, 64), ev(480, 0x80, 67, 0),
        ev(0, 0x90, 60, 64), ev(1920, 0x80, 60, 0),
        ev(0, 0x90, 62, 64), ev(480, 0x80, 62, 0), EOT)]))


def note(step, octave, dur, voice=1, tie=None, staff=None):
    x = "<note><pitch><step>%s</step><octave>%d</octave></pitch><duration>%d</duration>" % (step, octave, dur)
    for t in tie or []:
        x += '<tie type="%s"/>' % t
    x += "<voice>%d</voice>" % voice
    if staff:
        x += "<staff>%d</staff>" % staff
    return x + "</note>"


def attrs(divisions=None, beats=4, beat_type=4, staves=None):
    x = "<attributes>"
    if divisions is not None:
        x += "<divisions>%d</divisions>" % divisions
    if staves:
        x += "<staves>%d</staves>" % staves
    if beats is not None:
        x += "<time><beats>%d</beats><beat-type>%d</beat-type></time>" % (beats, beat_type)
    return x + "</attributes>"


def measure(number, content, implicit=False):
    return '<measure number="%d"%s>%s</measure>' % (number, ' implicit="yes"' if implicit else "", content)


def score(parts):
    """parts: list of (name, [measure strings])."""
    out = '<?xml version="1.0" encoding="UTF-8"?>\n<score-partwise version="3.1"><part-list>'
    for i, (name, _) in enumerate(parts):
        out += '<score-part id="P%d"><part-name>%s</part-name></score-part>' % (i + 1, name)
    out += "</part-list>"
    for i, (_, measures) in enumerate(parts):
        out += '<part id="P%d">%s</part>' % (i + 1, "".join(measures))
    return out + "</score-partwise>\n"


# A C4 tied from staff 1 into staff 2 across the barline.
write("edge-tie-cross-staff.musicxml", score([("Piano", [
    measure(1, attrs(1, staves=2) + note("C", 4, 4, tie=["start"], staff=1)),
    measure(2, note("C", 4, 2, tie=["stop"], staff=2) + note("D", 4, 2, staff=1)),
])]))

# Sixteen one-note parts, all C4 on the first beat: part 16 shares MIDI channel 1 with part 1.
write("edge-sixteen-parts.musicxml", score([
    ("Part %d" % (i + 1), [measure(1, attrs(1) + note("C", 4, 4))]) for i in range(16)]))

# No <divisions> anywhere: eight eighth notes (duration 1) fill a 4/4 bar, so the file means 2.
write("edge-no-divisions.musicxml", score([("Piano", [
    measure(1, attrs(None) + "".join(note("C", 4, 1) for _ in range(8))),
    measure(2, note("D", 4, 8)),
])]))

# A full 3/4 bar, then 4/4 bars: no pickup. C4 at 0, D4 at 1440, E4 at 3360, like the MIDI file.
write("edge-3-4-then-4-4.musicxml", score([("Piano", [
    measure(1, attrs(1, 3, 4) + note("C", 4, 3)),
    measure(2, attrs(None, 4, 4) + note("D", 4, 4)),
    measure(3, note("E", 4, 4)),
])]))

# A one-beat pickup (implicit first bar), then 4/4. G4 at 0, C4 at 480, D4 at 2400, like the MIDI file.
write("edge-pickup.musicxml", score([("Piano", [
    measure(1, attrs(1) + note("G", 4, 1), implicit=True),
    measure(2, note("C", 4, 4)),
    measure(3, note("D", 4, 1)),
])]))
