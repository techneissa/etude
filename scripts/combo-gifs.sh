#!/usr/bin/env bash
# The combo GIFs (#88), made from real presses: a made-up song of quarter notes, played key by key from
# the computer keyboard in the real app on a fake clock (50 Hz, `--fake-clock-hz`), so every frame is the
# same every run; a wrong key is pressed at the 31st note to break the combo. Writes
# OUT/{appear,pulse,break}.gif (1366x768 at scale 1.0, 50 fps, real time) and the PNG frames in OUT/frames.
# Usage: scripts/combo-gifs.sh OUT [path/to/etude]   (needs ffmpeg and python3)
set -euo pipefail
OUT=${1:?usage: combo-gifs.sh OUT [etude]}
BIN=${2:-build-release/app/etude}
mkdir -p "$OUT/frames/a" "$OUT/frames/b" "$OUT/frames/c"
SONG="$OUT/combo-demo.mid"
python3 -I - "$SONG" <<'PY'
import struct, sys
ppq = 480
notes = [60, 62, 64, 65, 67, 69, 71, 72]
def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))
tr = bytearray()
tr += vlq(0) + b'\xff\x51\x03' + struct.pack('>I', 500000)[1:]
tr += vlq(0) + b'\xff\x03' + bytes([10]) + b'Combo demo'
last = 0
for i in range(56):
    on = ppq * 2 + i * ppq
    tr += vlq(on - last) + bytes([0x90, notes[i % 8], 80]); last = on
    off = on + ppq // 2
    tr += vlq(off - last) + bytes([0x80, notes[i % 8], 0]); last = off
tr += vlq(0) + b'\xff\x2f\x00'
open(sys.argv[1], 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, ppq) + b'MTrk' + struct.pack('>I', len(tr)) + bytes(tr))
PY
SETTINGS="$OUT/settings"
mkdir -p "$SETTINGS"
printf '[app]\nbar_pinned = true\n' > "$SETTINGS/settings.conf"
# Space at frame 60 starts the 2 s count-in; the song's first note is at 1 s, a quarter note every 0.5 s,
# so note i is due at frame 210 + 25 i. A key A S D F G H J K per note; the 31st gets a wrong key (L).
keys=(A S D F G H J K)
args=()
for i in $(seq 0 39); do
    k=${keys[$((i % 8))]}
    [ "$i" -ne 30 ] || k=L
    args+=(--key "$((210 + 25 * i)):$k:3")
done
"$BIN" --headless --settings-dir "$SETTINGS" --song "$SONG" --fake-clock-hz 50 --resize 3:1366x768 \
    --key 60:Space "${args[@]}" \
    --shots 375:600:"$OUT/frames/a" --shots 700:900:"$OUT/frames/b" --shots 900:1060:"$OUT/frames/c" \
    --quit-frame 1065 > "$OUT/run.log" 2>&1
# The render is 2732x1536 (a hidden window has density 2); halve it to 1366x768.
for pair in a:appear b:pulse c:break; do
    d=${pair%%:*}; name=${pair##*:}
    ffmpeg -y -loglevel error -framerate 50 -i "$OUT/frames/$d/f%04d.png" \
        -vf "scale=1366:768:flags=lanczos,split[x][y];[x]palettegen=max_colors=64:stats_mode=diff[p];[y][p]paletteuse=dither=none:diff_mode=rectangle" \
        -loop 0 "$OUT/$name.gif"
done
grep Results "$OUT/run.log" || true
