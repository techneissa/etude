# App icon

The icon is a treble clef in pale gold with a soft glow over five staff lines, on a gradient. It comes
in eight colours, one for each player title (Beginner wine, Student teal, Performer petrol, Soloist
cobalt, Pianist midnight, Concert Pianist aubergine, Maestro graphite, Virtuoso black); Settings >
About offers the ones a profile's title has unlocked.

| Path | What it is |
| --- | --- |
| `src/bravura-gclef-E050.txt` | The clef's outline: the `gClef` glyph (SMuFL U+E050) of the Bravura music font, in font units, as Verovio ships it in `data/Bravura/E050.xml`. |
| `svg/<colour>.svg` | The 1024 px source of each colour (a squircle). `<colour>-small.svg` is the same with a heavier clef outline and staff, used for the 48 px PNG only. |
| `<colour>/etude-<px>.png` | Each colour's PNGs at 16, 24, 32, 48, 64, 128, 256, 512 and 1024 px. 16, 24 and 32 px are drawn for the pixel grid by `scripts/icon_small.py` (the clef thickened and set on whole pixels, no staff lines or two at 32 px, more contrast); 48 px is `<colour>-small.svg` rendered; 64 px and up are the 1024 px art scaled. The binary embeds the 256 px and 64 px ones; `deploy/pi/setup.sh` installs 16 to 512 as the hicolor icons `etude-<colour>`. |
| `etude.icns` | The macOS icon of the default colour (wine). |

`scripts/make-icons.py` writes the SVGs (`svg`), renders every PNG, the `.icns` and the README's
`docs-public/icon.png` from them (`render`, needs inkscape or rsvg-convert) and makes a contact
sheet (`sheet`; `compare` sets the 16 and 32 px icons against an earlier set). The 16, 24 and 32 px icons are drawn for the pixel grid by `scripts/icon_small.py`: a thickened clef on whole pixels, no staff lines (two at 32 px) and more contrast, since the big art scaled down turns to mush. The colours live in that script and in `core/include/etude/icon_colour.hpp`;
`tests/icon_colour_test.cpp` checks that every colour's PNG set exists at every size.

The clef is Bravura's, a music font by Steinberg Media Technologies GmbH under the SIL Open Font
License 1.1 (https://github.com/steinbergmedia/bravura, https://openfontlicense.org). The icon as a
whole is etude's and follows its GPL-3.0 licence, the glyph outline keeping its own.
