#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Draws every settings page headless (a hidden window, nothing on screen) at three
# window sizes and fails when a setting is drawn on two pages, on a page that is not its home, or on
# none (`etude --check-settings`, app/ui/settings_catalogue.hpp). `just test` runs it after the
# unit tests. BIN overrides the app (default build/app/etude).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/app/etude}
[ -x "$BIN" ] || { echo "no $BIN: run just build first" >&2; exit 2; }
status=0
for size in 800x480 1366x768 1920x1080; do
    printf '%s: ' "$size"
    SDL_AUDIO_DRIVER=dummy "$BIN" --check-settings --headless --window "$size" \
        --song tests/data/edge-pickup.mid || status=1
done
exit $status
