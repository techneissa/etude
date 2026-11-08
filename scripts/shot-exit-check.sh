#!/bin/bash
# Checks that a headless --shot without --quit-frame ends the run by itself (exit 0) and that
# --size sets the pixels of the PNG. No window opens.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/app/etude}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
status=0
for size in 800x480 1366x768; do
    out=$WORK/shot-$size.png
    if ! timeout 60 "$BIN" --headless --settings-dir "$WORK" --shot "45:$out" --size "$size" > /dev/null; then
        echo "FAIL $size: did not exit 0 within 60 s"
        status=1
        continue
    fi
    w=$(sips -g pixelWidth "$out" | awk '/pixelWidth/ {print $2}')
    h=$(sips -g pixelHeight "$out" | awk '/pixelHeight/ {print $2}')
    if [ "${w}x${h}" != "$size" ]; then
        echo "FAIL $size: the PNG is ${w}x${h}"
        status=1
    else
        echo "ok $size: exit 0, PNG ${w}x${h}"
    fi
done
# --size without --headless must be an error, not another size.
if "$BIN" --size 800x480 --shot "45:$WORK/x.png" 2> "$WORK/err" > /dev/null || ! grep -q -- "--size needs" "$WORK/err"; then
    echo "FAIL: --size without --headless was not refused"
    status=1
else
    echo "ok: --size without --headless is refused"
fi
exit $status
