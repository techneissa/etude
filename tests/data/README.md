# Test data

Real MIDI files the loader tests read. Both are public domain.

| File | Piece | Source | License |
| --- | --- | --- | --- |
| `bach-bwv846-prelude.mid` | J. S. Bach, Prelude in C major, BWV 846 | Mutopia Project, `https://www.mutopiaproject.org/ftp/BachJS/BWV846/wtk1-prelude1/wtk1-prelude1.mid` | Public Domain (`copyright = "Public Domain"` in the LilyPond source) |
| `satie-gymnopedie-1.mid` | Erik Satie, Gymnopédie No. 1 | Mutopia Project, `https://www.mutopiaproject.org/ftp/SatieE/gymnopedie_1/gymnopedie_1.mid` | Public Domain (`license = "Public Domain"` in the LilyPond source) |

Downloaded 2026-10-12. LilyPond wrote both, with one track per staff named after its staff.

## Score files (MusicXML, MuseScore)

One real piece, in the three score formats the loaders read. Source: the OpenScore Lieder corpus
(`https://github.com/OpenScore/Lieder`, commit `38c5db510224d9facdc4b08d741fc788cfb58ea8`, folder
`scores/Beethoven,_Ludwig_van/6_Lieder,_Op.48/4_Die_Ehre_Gottes_aus_der_Natur/`), a transcription of
IMSLP #26415 by DanielR. Ludwig van Beethoven's music is in the public domain, and OpenScore
releases its transcriptions under CC0 1.0 (`<rights>OpenScore (CC0)</rights>` in the file). The
piece is for voice and piano: 716 notes, 43 bars, 80 BPM in 2/2, the piano on two staves.

| File | What it is | Made by |
| --- | --- | --- |
| `beethoven-op48-4-ehre-gottes.mxl` | compressed MusicXML (`lc5121134.mxl` in the corpus) | MuseScore 4.5.2 |
| `beethoven-op48-4-ehre-gottes-musescore3.mscz` | MuseScore 3 file (`lc5121134.mscz` in the corpus, unchanged) | MuseScore 3.6.2 |
| `beethoven-op48-4-ehre-gottes-musescore4.mscz` | the same score saved as a MuseScore 4 file | MuseScore 4.7.3: `mscore -o out.mscz beethoven-op48-4-ehre-gottes.mxl` |
| `beethoven-op48-4-ehre-gottes-musescore-export.mid` | MuseScore's own MIDI playback export of the `.mxl`, the independent reference for the notes | MuseScore 4.7.3: `mscore -o out.mid beethoven-op48-4-ehre-gottes.mxl` |

Downloaded 2026-10-12. `tests/musicxml_real_test.cpp` checks that every note of the `.mxl` has the
pitch, start tick and (within MuseScore's export gate) end tick of the MIDI export, and that both
`.mscz` files give the same notes as the `.mxl`. The MIDI export differs in two known ways: it
ends notes early (about 5%, half for staccato) and it plays one ornament in bar 6 as extra notes.


## Repeats and voltas

`repeats-voltas.musicxml` is a 7-bar exercise written for this repo (no existing piece, so no licence
to carry: CC0), with a repeat that has first and second endings, then a repeat played three times
(`times="3"`) whose first and second passes share an ending (`1, 2`) before a third ending. Played
through, its bars run 1 2 3, 1 2 4, 5 6, 5 6, 5 7. The other three are MuseScore 4.7.3's own
conversions of it, run as `mscore -o OUT repeats-voltas.musicxml`:

| File | What it is |
| --- | --- |
| `repeats-voltas.mxl` | compressed MusicXML |
| `repeats-voltas-musescore4.mscz` | MuseScore 4 file (repeat barlines and voltas as MuseScore stores them) |
| `repeats-voltas-musescore-export.mid` | MuseScore's MIDI playback export, the independent reference for the play order |

`repeats-tie-into-ending.musicxml` (also written for this repo, CC0) is a 3-bar piece whose first ending ends on a tied G and whose second ending starts on the other half of the tie.

`tests/musicxml_repeats_test.cpp` checks that all three score files play the same notes, at the same
ticks, as the MIDI export.

## ABC notation

Five tunes in ABC 2.1 and four of them typed out a second time as MusicXML. All are original pieces written for this repo (CC0), not transcriptions, so they carry no licence to track; the traditional-tune idioms they use (a reel with a first and second ending, a 6/8 tune in D dorian, a jig with triplets and dotted pairs, a two-hand piece) are common to the whole repertoire. `make-abc-twins.py` writes the `.musicxml` files (`python3 -I tests/data/make-abc-twins.py` from the repo root); it types each bar again as (pitch, divisions) pairs read off the music, not off the ABC text, so `tests/abc_test.cpp` can check that the ABC loader and the MusicXML loader hear the same notes, bar numbers and play order.

| File | What it holds | MusicXML twin |
| --- | --- | --- |
| `abc-reel-repeats.abc` | Hillside Reel, G major, 4/4, 12 written bars: a repeat with first and second endings, then a repeated B part; chord symbols on every bar (for the left-hand option) | `abc-reel-repeats.musicxml` |
| `abc-dorian.abc` | Dorian Dawn, D dorian (`K:Ddor`), 6/8, a one-eighth pickup, an F sharp that is carried through its bar, a tie across the last barline that completes the pickup | `abc-dorian.musicxml` |
| `abc-triplets.abc` | Skipping Stones, F major, 4/4: `(3abc`, `(3:2:3abc`, `A>B` and `A<B` | `abc-triplets.musicxml` |
| `abc-two-voices.abc` | Two Hands, C major, 3/4: voices `V:1` (treble, "Right hand") and `V:2` (bass, "Left hand"), chords in the left hand, the whole piece repeated | `abc-two-voices.musicxml` |
| `abc-collection.abc` | three tunes (`X:1`, `X:2`, `X:10`) after a file header that sets the default note length | none |

## Import edge cases

Small files for `tests/import_edges_test.cpp`, all written for this repo (CC0). `make-edge-cases.py` writes the `.mid` files and the generated `.musicxml` files (`python3 -I tests/data/make-edge-cases.py`); `edge-tie-cross-staff.mscx` is written by hand.

| File | What it holds |
| --- | --- |
| `edge-tie-cross-staff.musicxml`, `.mscx` | a C4 whole note on staff 1 tied into a half note on staff 2 across the barline: one held note |
| `edge-fast-32nds.mid` | eight 32nd notes at 300 BPM, 25 ms each: real notes, not ghosts |
| `edge-sixteen-parts.musicxml` | sixteen one-note parts, all C4 on the first beat (part 16 shares MIDI channel 1 with part 1) |
| `edge-3-4-then-4-4.mid`, `.musicxml` | a full 3/4 bar, then 4/4: bars 1, 2, 3 in both |
| `edge-pickup.mid`, `.musicxml` | a one-beat pickup (1/4 in the MIDI file, an implicit bar in the MusicXML file), then 4/4: bars 0, 1, 2 in both |
| `edge-sig-midbar.mid` | 4/4 with a 3/4 signature at beat 3 of the first bar: that cut-short first bar is the pickup (bar 0), the 3/4 bar that starts there is bar 1 |
| `edge-no-divisions.musicxml` | no `<divisions>` anywhere: eight units fill a 4/4 bar, so a unit is an eighth note |


## Difficulty calibration

`difficulty-scale.mid` (a C major scale, two octaves up and down in quarter notes, one hand), `difficulty-arpeggio.mid` (broken chords C, Am, F, G7 over a held bass) and `difficulty-etude.mid` (a Chopin-like etude in D flat major at 168 BPM: sixteenth-note figuration, a stride and arpeggio left hand, chords) and `difficulty-toccata.mid` (a toccata in F sharp major at 192 BPM: sixteenth-note triads leaping over three octaves against eighth-note triplets in the left hand) are written for this repo (CC0) by `make-difficulty-pieces.py` (`python3 -I tests/data/make-difficulty-pieces.py`). With the Gymnopedie and the prelude above they give `tests/difficulty_test.cpp` its order: scale, arpeggio, Gymnopedie, prelude, etude, toccata.


## Versions of one piece

`lantern-waltz.mid`, `lantern-waltz-easy.mid`, `lantern-waltz-in-d.mid` and `harbour-march.mid` are written for this repo (CC0) by `make-version-pieces.py` (`python3 -I tests/data/make-version-pieces.py`): an original waltz tune in C (the first eight bars are what the version grouping reads) with its ornaments and a left hand, the same tune simplified (longer notes, no passing notes, a bass note a bar, titled "Lantern Waltz (Easy)"), the original a tone up in D under another name ("Lantern Waltz in D"), and an unrelated march. `tests/song_versions_test.cpp` and `library_binding_test.cpp` group them: the easy arrangement and the original by title and melody, the D copy by melody alone, the march with neither.

## Accompaniment

`accompaniment-trio.mid` is a short waltz made for this repo (CC0) by `make-accompaniment-piece.py`:
the piano on two staves (`Right Hand`, `Left Hand`) with a `Violin` and a `Choir` part over it, so
the song has two accompaniment parts of different kinds. The accompaniment button's screenshots and
the icon audit's `accompaniment` scenarios read it.
