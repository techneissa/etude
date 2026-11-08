# Adding a piece to Melodies

A piece is a MIDI file in this folder and one line in `manifest.txt`. There is no code to change: the
build embeds every file the manifest lists, the Learn screen reads the manifest, and the tests check
the new line and the file.

1. **Check that it may be bundled.** Only public-domain pieces and tunes written out for this app
   (dedicated CC0). A piece from Mutopia or HymnWiki is public domain when its page says so: read the
   page the day you add it and keep its address for the source field.
2. **Put the file here.** A name in lower case with hyphens, ending in `.mid` (`ode-to-joy.mid`). Give
   the right and left hand a track each, named for the hand, so no hand split has to guess; a one-hand
   tune has only a right-hand track. No note may overlap itself or have no length (a test reads the
   file's bytes for that). Short is better: a tile is a piece to learn in one go.
3. **Add one `file` line to `manifest.txt`**, fields apart by ` | `, none empty:

   ```
   file | NAME.mid | Title | Composer | step | source | licence
   ```

   - step: 1 (right hand, a few keys) to 5 (both hands, a wide range). The library's rating and the
     keys each hand covers show on the tile, so the step is for the order of the lessons.
   - source: where the notes come from and what was changed (a page address, "transposed a fifth
     down", "the soprano alone as the right hand").
   - licence: `CC0-1.0` for a tune we wrote out, or `Public Domain (...)` naming the page that says so.

   A line with the source or the licence missing is refused: the manifest does not load and the
   Melodies page says the pieces could not be read.
4. **Put the piece in a path**: add a `goal` line under its path (`goal | path id | NAME.mid | one-line
   goal`), where its step keeps the path from stepping down in grade. A path holds at most 16 pieces,
   and there are at most 6 paths.
5. **Look at it.** Open the piece from the Melodies page and play a few bars: the right hand should be
   the right hand. Then run `just test`: the manifest, the file and its licence are checked.

## An easier version of a piece

A simplified arrangement of a piece already here is a piece of its own: a file, a `file` line and a `goal`
line, listed before the original in its path, with the same title and composer. Each of the two files has
a `version | NAME.mid | tag` line (`Easy` and `Original`), and its tile wears that tag as a small pill under
its title, so the two tiles can be told apart. The tag is at most 12 characters and is only for files
listed under one title; every other piece has none. The step is the file's own (a right-hand-only step is
1 or 2, both hands from 3). `scripts/make-melodies.py` makes `brahms-lullaby-easy.mid` from the bundled
original (`EASY`): the same right hand and one left-hand note a bar, each a note the original plays in that
bar; `melody_book_test.cpp` holds both.

The "From your library" row and "Add to Melodies" are the other two ways in; they need no file here.
