#!/bin/bash
# Checks that a headless --shot is the pixels of a visible-window --shot of the same frame, at
# 800x480 and 1366x768 (within 1 level per channel). The visible half opens a real window for a
# second or two per size, so run it with nothing else using the screen; everything else uses --headless.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/app/etude}
SONG=tests/data/satie-gymnopedie-1.mid
WORK=${KEEP:-$(mktemp -d)}
mkdir -p "$WORK"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$WORK"' EXIT
printf '[app]\nbar_style = strip\nbar_pinned = true\n' > "$WORK/settings.conf"
status=0
for size in 800x480 1366x768; do
    # Frame 45 is a settled frame: the song is paused, the toast is still whole. A cold start can
    # still catch the song library mid-scan in one of the two, so a size gets three tries.
    ok=1
    for try in 1 2 3; do
        "$BIN" --headless --settings-dir "$WORK" --window "$size" --song "$SONG" \
            --shot "45:$WORK/headless-$size.png" --quit-frame 46 > /dev/null
        "$BIN" --settings-dir "$WORK" --window "$size" --song "$SONG" \
            --shot "45:$WORK/visible-$size.png" --quit-frame 46 > /dev/null
        echo -n "$size: "
        if "$BIN" --png-diff "$WORK/headless-$size.png" "$WORK/visible-$size.png" 1; then
            ok=0
            break
        fi
    done
    status=$((status | ok))
done
exit $status
