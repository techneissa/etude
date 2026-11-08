# Contributing

Thanks for helping. This page says what to do before, during and after a change.

## Before you start

- Open an issue before a large change, so we can agree on the approach first.
- For a small fix, a pull request is enough.
- The code is in `core/` (the music logic) and `app/` (the app).

## Build and test

You need the tools listed under Getting started in the [README](README.md).

```
just build     # configure and build everything
just run       # build and start the app
just test      # build and run the unit tests your changes touch
just format    # format the code with clang-format (.clang-format)
```

## Code rules

- `core/` holds the music logic and has no SDL, MIDI, audio, file-system or clock code.
  - Its public headers use plain structs and value types, and no exception crosses the API.
  - Every behaviour in `core/` has a doctest test in `tests/`.
- Code works for any keyboard range, not only the 61-key reference keyboard (`KeyRange`).
- New source files start with `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Dependencies are release tarballs pinned by SHA-256 in `cmake/Dependencies.cmake`, with a GPL-3.0-compatible licence.
  - An offline tool that makes files we check in (glslang and SPIRV-Cross, run by `scripts/build-shaders.sh`) is not a build dependency: it needs no pin, only a GPL-3.0-compatible licence and its version written in the script.
- Comments state the rule they follow; they don't point at documents outside the repository.

## Pull requests

A good pull request has:

- one change, with a short description of what it does and why;
- tests for the behaviour it adds or fixes, and `just test` passing;
- `just format` run;
- a link to the issue it closes, if there is one.

## Licence of contributions

By contributing you agree to license your work under the GPL-3.0-or-later.
