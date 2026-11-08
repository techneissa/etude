#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Renders every note style and `--shot-pattern all` headless twice and fails when two runs of the
# same binary give different PNG bytes (issue #326): a refactor's "nothing changed on screen" is
# only provable while a shot depends on its arguments alone. `just test` runs it. BIN overrides the
# app (default build/app/etude).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/app/etude}
[ -x "$BIN" ] || { echo "no $BIN: run just build first" >&2; exit 2; }
out=$(mktemp -d "${TMPDIR:-/tmp}/etude-shots.XXXXXX")
trap 'rm -rf "$out"' EXIT
status=0
for run in a b; do
    mkdir "$out/$run"
    for style in solid outline glass neon candy soft pixel ink; do
        "$BIN" --headless --size 960x540 --style "$style" --screenshot "$out/$run/$style.png" >/dev/null
    done
    "$BIN" --headless --size 960x540 --shot-pattern all --screenshot "$out/$run/all" >/dev/null
done
for f in "$out"/a/*.png; do
    name=$(basename "$f")
    if ! cmp -s "$f" "$out/b/$name"; then
        echo "shot differs between two runs: $name" >&2
        status=1
    fi
done
[ "$status" = 0 ] && echo "shots: $(ls "$out"/a | wc -l | tr -d ' ') identical across two runs"
exit $status
