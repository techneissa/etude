#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Writes the MusicXML twins of the abc-*.abc test tunes (CC0, written for this repo).

Each tune is typed out a second time here, bar by bar, as (pitch, divisions) pairs read off the
music rather than off the ABC text, so tests/abc_test.cpp can check that the ABC loader and the
MusicXML loader hear the same notes in the same bars. Run as `python3 -I tests/data/make-abc-twins.py`
from the repo root.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def pitch_xml(name):
    """'F#4' -> <pitch>, 'Bb4' -> flat, 'C5' -> natural."""
    step, rest = name[0], name[1:]
    alter = 0
    if rest[0] == "#":
        alter, rest = 1, rest[1:]
    elif rest[0] == "b":
        alter, rest = -1, rest[1:]
    out = "<pitch><step>%s</step>" % step
    if alter:
        out += "<alter>%d</alter>" % alter
    return out + "<octave>%s</octave></pitch>" % rest


def note(name, dur, *, chord=False, tie=None, tuplet=False, voice=1, staff=None):
    """tie: 'start', 'stop' or 'both'."""
    x = "<note>"
    if chord:
        x += "<chord/>"
    x += pitch_xml(name) + "<duration>%d</duration>" % dur
    if tie in ("stop", "both"):
        x += '<tie type="stop"/>'
    if tie in ("start", "both"):
        x += '<tie type="start"/>'
    x += "<voice>%d</voice>" % voice
    if tuplet:
        x += "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>"
    if staff:
        x += "<staff>%d</staff>" % staff
    if tie:
        x += "<notations>"
        if tie in ("stop", "both"):
            x += '<tied type="stop"/>'
        if tie in ("start", "both"):
            x += '<tied type="start"/>'
        x += "</notations>"
    return x + "</note>"


def measure(number, body, *, attrs="", left="", right="", implicit=False):
    head = '<measure number="%s"%s>' % (number, ' implicit="yes"' if implicit else "")
    return head + attrs + left + body + right + "</measure>"


def score(title, part_name, measures, program=1):
    return (
        '<?xml version="1.0" encoding="UTF-8"?><score-partwise version="3.1"><work><work-title>%s</work-title></work>'
        "<identification><rights>CC0 1.0: written for this repo, no rights reserved</rights></identification>"
        '<part-list><score-part id="P1"><part-name>%s</part-name><midi-instrument id="P1-I1"><midi-program>%d</midi-program>'
        '</midi-instrument></score-part></part-list><part id="P1">%s</part></score-partwise>\n'
        % (title, part_name, program, "".join(measures))
    )


def attributes(divisions, fifths, beats, beat_type, staves=None, tempo=None):
    x = "<attributes><divisions>%d</divisions><key><fifths>%d</fifths></key>" % (divisions, fifths)
    x += "<time><beats>%d</beats><beat-type>%d</beat-type></time>" % (beats, beat_type)
    if staves:
        x += "<staves>%d</staves>" % staves
        x += '<clef number="1"><sign>G</sign><line>2</line></clef><clef number="2"><sign>F</sign><line>4</line></clef>'
    x += "</attributes>"
    if tempo:
        x += '<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>%s</per-minute></metronome></direction-type>' % tempo
        x += '<sound tempo="%s"/></direction>' % tempo
    return x


FORWARD = '<barline location="left"><bar-style>heavy-light</bar-style><repeat direction="forward"/></barline>'
BACKWARD = '<barline location="right"><bar-style>light-heavy</bar-style><repeat direction="backward"/></barline>'


def ending_start(n):
    return '<barline location="left"><ending number="%d" type="start"/></barline>' % n


def ending_stop(n, kind="stop", repeat=False):
    x = '<barline location="right">'
    if repeat:
        x += "<bar-style>light-heavy</bar-style>"
    x += '<ending number="%d" type="%s"/>' % (n, kind)
    if repeat:
        x += '<repeat direction="backward"/>'
    return x + "</barline>"


def seq(pairs, **kw):
    return "".join(note(p, d, **kw) for p, d in pairs)


# ---- Hillside Reel: G major, 4/4, eighth = 1 division, repeats with first and second endings.
def reel():
    bars = [
        [("G4", 1), ("A4", 1), ("B4", 1), ("C5", 1), ("D5", 2), ("B4", 2)],
        [("C5", 2), ("E5", 2), ("E5", 4)],
        [("D5", 1), ("B4", 1), ("G4", 1), ("B4", 1), ("D5", 2), ("G5", 2)],
        [("F#5", 2), ("D5", 2), ("D5", 4)],
        [("G4", 1), ("A4", 1), ("B4", 1), ("C5", 1), ("D5", 2), ("B4", 2)],
        [("C5", 2), ("E5", 2), ("G5", 4)],
        [("F#5", 1), ("D5", 1), ("A4", 1), ("C5", 1), ("B4", 4)],
        [("F#5", 1), ("D5", 1), ("A4", 1), ("C5", 1), ("G4", 4)],
        [("B4", 1), ("E4", 1), ("E4", 1), ("E4", 1), ("G4", 2), ("E4", 2)],
        [("A4", 1), ("A4", 1), ("A4", 1), ("C5", 1), ("E5", 2), ("C5", 2)],
        [("D5", 1), ("C5", 1), ("A4", 1), ("F#4", 1), ("A4", 2), ("D5", 2)],
        [("G5", 2), ("D5", 2), ("B4", 4)],
    ]
    left = {1: FORWARD, 7: ending_start(1), 8: ending_start(2), 9: FORWARD}
    right = {7: ending_stop(1, repeat=True), 8: ending_stop(2, "discontinue"), 12: BACKWARD}
    out = []
    for i, bar in enumerate(bars, 1):
        attrs = attributes(2, 1, 4, 4, tempo="112") if i == 1 else ""
        out.append(measure(i, seq(bar), attrs=attrs, left=left.get(i, ""), right=right.get(i, "")))
    return score("Hillside Reel", "Piano", out)


# ---- Dorian Dawn: D dorian (no key signature), 6/8, eighth = 1 division, a one-eighth pickup, and a
# last bar that completes it. The final C is tied across the barline.
def dorian():
    bars = [
        ("0", [("A4", 1)], True),
        ("1", [("D4", 2), ("F4", 1), ("A4", 2), ("C5", 1)], False),
        ("2", [("D5", 2), ("C5", 1), ("A4", 2), ("F4", 1)], False),
        ("3", [("G4", 2), ("E4", 1), ("F#4", 2), ("D4", 1)], False),
        ("4", [("F4", 3), ("F#4", 1), ("F#4", 2)], False),
        ("5", [("E4", 2), ("D4", 1), ("E4", 2), ("A4", 1)], False),
        ("6", [("B4", 2), ("C5", 1), ("D5", 2), ("E5", 1)], False),
    ]
    out = []
    for number, bar, implicit in bars:
        attrs = attributes(2, 0, 6, 8, tempo="90") if number == "0" else ""
        out.append(measure(number, seq(bar), attrs=attrs, implicit=implicit))
    out.append(measure("7", seq([("F5", 2), ("E5", 1), ("D5", 2)]) + note("C5", 1, tie="start")))
    out.append(measure("8", note("C5", 4, tie="stop") + note("D4", 1), implicit=True))
    return score("Dorian Dawn", "Piano", out)


def main():
    files = {
        "abc-reel-repeats.musicxml": reel(),
        "abc-dorian.musicxml": dorian(),
    }
    for name, text in files.items():
        with open(os.path.join(HERE, name), "w", encoding="utf-8") as f:
            f.write(text)
        print("wrote", name, len(text), "bytes")


if __name__ == "__main__":
    sys.exit(main())
