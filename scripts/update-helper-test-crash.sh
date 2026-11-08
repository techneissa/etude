#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The last section of scripts/update-helper-test.sh after the real-systemd one, sourced by it: the crash
# chain with the real app. A real Linux build is installed by deploy/pi/setup.sh (built in the container,
# its build folder kept between runs in the Docker volume at $UH_BUILD_CACHE, so only the first run
# builds everything) and run by the shipped KMSDRM units with SDL's offscreen video driver and the dummy
# audio driver: no screen, no sound card, no compositor (the labwc units' own chain is checked in the
# systemd section with a stand-in compositor). Then each step of the chain runs for real: the app
# crashes on purpose or is ended by the watchdog, systemd starts the crash screen and not the app, the
# screen's own button is pressed by the developer flag --crash-screen-press, and the app starts again.
# It uses the functions, the variables and the counters of the scripts that source it.
echo "== the crash chain with the real app (setup.sh's own install, SDL's offscreen video driver)"
PREFIX=/opt/etude
STATE=/var/lib/etude-update
CC_CACHE="${UH_BUILD_CACHE:-/cache}"
CC_SRC=/work/etude
CC_CTL=/run/uh-crash
CC_APP=etude.service
CC_SCREEN=etude-crash-screen.service
CC_LOG="$WORK/crash-chain.log"
: >"$CC_LOG"

# --- the machine: what a Pi OS Lite image already has (git, python3), then setup.sh ----------------

sd_stop
systemctl disable etude-update.path etude-update-resume.service >/dev/null 2>&1
rm -rf /opt/etude /var/lib/etude-update "$DATA"
cc_installed=0
if apt-get install -y -qq --no-install-recommends git python3 rsync >/dev/null 2>&1 &&
    mkdir -p "$CC_CACHE/build" "$CC_CACHE/dotcache" "$CC_SRC" /root/.cache &&
    rsync -a --delete --exclude /build --exclude /.git "$REPO"/ "$CC_SRC"/ &&
    mkdir -p "$CC_SRC/build/pi-release" &&
    mount --bind "$CC_CACHE/build" "$CC_SRC/build/pi-release" &&
    mount --bind "$CC_CACHE/dotcache" /root/.cache; then
    echo "  building and installing the app (the first run builds everything and takes a while)"
    if bash "$CC_SRC/deploy/pi/setup.sh" --enable kmsdrm >"$WORK/setup-real.log" 2>&1; then
        cc_installed=1
    else
        tail -30 "$WORK/setup-real.log" >&2
    fi
fi
expect "deploy/pi/setup.sh builds and installs the real app" test "$cc_installed" = 1
if ((!cc_installed)); then
    echo "  (the crash chain does not run: no installed app)"
else
# The helper setup.sh built carries the release key: the one made for this run goes in its place, so the
# update step can be signed here (the same swap the other sections make).
install -D -o root -g root -m 0755 /build/helper/etude-update-helper "$HELPER"
expect "the installed app is the real build (a 30 MB program, setup.sh's version folder)" test "$(stat -c %s "$PREFIX/versions/0.1.0/bin/etude")" -gt 10000000
expect "the shipped units are installed by setup.sh (the app's, the crash screen's, the session script's)" test -e /etc/systemd/system/etude.service -a -e /etc/systemd/system/etude-crash-screen.service -a -e /usr/local/lib/etude/etude-labwc-session
expect "setup.sh enabled the KMSDRM unit" systemctl is-enabled --quiet etude.service

# What this machine lacks of a Pi, and nothing else: no console to take over, no PAM session or hardware
# groups, no tty, no watchdog change; the video driver is the offscreen one and the audio the dummy.
# `uh-launch` starts the real program and adds the arguments the test left for this start (a crash on
# purpose, a button of the crash screen to press): once-only ones go with the first start that finds them.
install -d -m 0777 "$CC_CTL"
cat >/usr/local/bin/uh-launch <<'LAUNCH'
#!/bin/sh
# uh-launch KIND PROGRAM [ARGS...]: KIND is app or screen.
kind=$1
shift
dir=/run/uh-crash
extra=
if [ -f "$dir/$kind-once" ]; then
    extra=$(cat "$dir/$kind-once")
    mv -f "$dir/$kind-once" "$dir/$kind-once.used"
fi
[ -f "$dir/$kind-always" ] && extra="$extra $(cat "$dir/$kind-always")"
echo "$kind $(date +%s) $extra" >>"$dir/launches"
[ -f "$dir/$kind-delay" ] && sleep "$(cat "$dir/$kind-delay")"
exec "$@" $extra
LAUNCH
chmod 0755 /usr/local/bin/uh-launch
cc_dropin() { # cc_dropin UNIT EXECSTART...
    local unit="$1"
    shift
    mkdir -p "/etc/systemd/system/$unit.d"
    cat >"/etc/systemd/system/$unit.d/zz-container.conf" <<DROPIN
[Unit]
ConditionPathExists=
[Service]
SupplementaryGroups=
PAMName=
StandardInput=null
TTYPath=
TTYReset=no
TTYVHangup=no
TTYVTDisallocate=no
UtmpIdentifier=
ExecStartPost=
Environment=SDL_VIDEO_DRIVER=offscreen
Environment=SDL_AUDIO_DRIVER=dummy
ExecStart=
ExecStart=$*
DROPIN
}
cc_dropin "$CC_APP" /usr/local/bin/uh-launch app "$PREFIX/current/bin/etude"
cc_dropin "$CC_SCREEN" /usr/local/bin/uh-launch screen "$PREFIX/current/bin/etude" --crash-screen
systemctl daemon-reload
expect "the shipped units, with the container's drop-in, still verify" systemd-analyze verify "/etc/systemd/system/$CC_APP" "/etc/systemd/system/$CC_SCREEN"
expect_equal "the app's unit runs the offscreen driver here" "SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy" "$(systemctl show -p Environment --value "$CC_APP" | sed 's/.*\(SDL_VIDEO_DRIVER=offscreen\).*\(SDL_AUDIO_DRIVER=dummy\).*/\1 \2/')"
expect_equal "  and it never restarts by itself" "no" "$(systemctl show -p Restart --value "$CC_APP")"
expect_equal "  and a crash starts the crash screen" "etude-crash-screen.service" "$(systemctl show -p OnFailure --value "$CC_APP")"

# --- helpers ------------------------------------------------------------------------------------------

# One line of the journal's story, as the step reports it: the app's own words and systemd's and the helper's.
cc_story() { # cc_story MARK
    journalctl --no-pager -o short-precise --since "$1" 2>/dev/null |
        grep -E "etude: (crashing on purpose|resuming|the last run|no crash report|the crash report|crash screen|the resume)|etude(-crash-screen)?\.service: (Main process exited|Failed with result|Watchdog|Triggering OnFailure|Succeeded|Deactivated)|Started etude|Stopped etude|Starting etude|etude-update-helper|update-helper|OnFailure|crashed|rolled" |
        sed -E 's/^[A-Z][a-z]{2} [0-9 ]{2} //; s/ [^ ]+ ((etude|systemd)[^ ]*)/ \1/' | cut -c1-210 | sed 's/^/      journal: /' | tee -a "$CC_LOG"
}
cc_now() { date '+%Y-%m-%d %H:%M:%S'; }
cc_launches() { grep -c "^$1 " "$CC_CTL/launches" 2>/dev/null; }
cc_launched() { [[ "$(cc_launches "$1")" == "$2" ]]; } # cc_launched KIND N, for wait_until
cc_reports_are() { [[ "$(cc_report_count)" == "$1" ]]; }
cc_app_up() { systemctl is-active --quiet "$CC_APP"; }
cc_screen_up() { systemctl is-active --quiet "$CC_SCREEN"; }
cc_app_failed() { [[ "$(systemctl is-active "$CC_APP")" == failed ]]; }
cc_ready() { ready_is 0.1.0; }
cc_report_count() { find "$DATA/crashes" -maxdepth 1 -name '[0-9]*-[0-9]*.txt' 2>/dev/null | wc -l; }
cc_newest_report() { find "$DATA/crashes" -maxdepth 1 -name '[0-9]*-[0-9]*.txt' 2>/dev/null | sort | tail -n 1; }
cc_main_pid() { systemctl show -p MainPID --value "$CC_APP"; }
cc_app_log() { journalctl --no-pager -o cat -u "$CC_APP" --since "$1" 2>/dev/null; }
cc_screen_log() { journalctl --no-pager -o cat -u "$CC_SCREEN" --since "$1" 2>/dev/null; }
cc_settings() { printf 'format = 1.0\n[look]\nversion = 1\nnote_style = neon\n'; }
# A point left by a run that went on for a minute: a song of the player's, practised slowly with the left hand.
cc_resume() {
    cat <<POINT
etude-resume 2
ran_ms=60000
clean=0
screen=play
song=scale.mid
song_abs=$DATA/Songs/scale.mid
hash=0
position_ms=0
speed=75
hands=left
plays=nothing
loop_start_ms=0
loop_end_ms=0
loop=0
section=0
plan_item=0
count_in=1
transpose=0
end
POINT
}
# A small MIDI file (eight notes of a scale), made here: the song the point names.
cc_make_song() {
    python3 -I - "$1" <<'PY'
import struct, sys
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))
ev = b""
for i, key in enumerate([60, 62, 64, 65, 67, 69, 71, 72]):
    ev += vlq(0 if i == 0 else 240) + bytes([0x90, key, 80]) + vlq(240) + bytes([0x80, key, 0])
ev += vlq(0) + b"\xff\x2f\x00"
track = b"MTrk" + struct.pack(">I", len(ev)) + ev
open(sys.argv[1], "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + track)
PY
}

# cc_baseline [resume]: the real 0.1.0 installed, the player's data in place (a setting of theirs and, when
# asked, where they left off), no crash reports, nothing pending, nothing running.
cc_baseline() {
    sd_stop
    systemctl reset-failed 2>/dev/null
    rm -rf "$STATE"/* "$DATA"
    find "$PREFIX/versions" -mindepth 1 -maxdepth 1 ! -name 0.1.0 -exec rm -rf {} +
    ln -sfn versions/0.1.0 "$PREFIX/current"
    rm -f "$PREFIX/previous" "$PREFIX/update"/*
    rm -f "$CC_CTL"/*
    : >"$CC_CTL/launches"
    chmod 0666 "$CC_CTL/launches"
    mkdir -p "$DATA"
    cc_settings >"$DATA/settings.conf"
    if [[ "${1:-}" == resume ]]; then
        mkdir -p "$DATA/Songs"
        cc_make_song "$DATA/Songs/scale.mid"
        cc_resume >"$DATA/resume.txt"
    fi
    chown -R etude: /home/etude
}
# cc_start_app: starts the app and waits until it says it is ready, as the helper's trials do.
cc_start_app() {
    systemctl start "$CC_APP" >/dev/null 2>&1
    wait_until 90 cc_ready || {
        flunk "the app did not say ready under systemd"
        journalctl -u "$CC_APP" --no-pager -n 20 2>&1
    }
}

# --- 1. --crash-test segv: no restart, the crash screen starts, a report is left ----------------------------

echo "  --crash-test segv: the app crashes at its start; the crash screen starts, the app is not restarted"
cc_baseline
mark="$(cc_now)"
echo "--crash-test segv" >"$CC_CTL/app-once"
systemctl start "$CC_APP" >/dev/null 2>&1
expect "the crash screen is started (OnFailure=)" wait_until 60 cc_screen_up
sleep 8
expect_equal "  the app was started once and not again" "1" "$(cc_launches app)"
expect_equal "  the screen was started once" "1" "$(cc_launches screen)"
expect "  the app's unit is failed, not running and not restarting" cc_app_failed
expect_equal "  it ended with the signal" "signal" "$(systemctl show -p Result --value "$CC_APP")"
expect "  the crash screen is still up (it waits for the player)" cc_screen_up
expect_equal "  one report is in crashes/" "1" "$(cc_report_count)"
expect_contains "  the report names the signal and the version" "SIGSEGV" "$(cat "$(cc_newest_report)" 2>/dev/null | head -12)"
expect_contains "  the app said it crashed on purpose" "crashing on purpose (segv)" "$(cc_app_log "$mark")"
cc_story "$mark"

# --- 2. Restart: the app starts normally, with the player's settings, where they left off ---------------------

echo "  Restart: the app starts again normally, with the player's settings, where they left off (a crash after 14 s of use)"
cc_baseline resume
echo "--crash-screen-press restart" >"$CC_CTL/screen-always"
mark="$(cc_now)"
cc_start_app
expect_contains "  the app resumed the screen the player left" "resuming the play screen and its song" "$(cc_app_log "$mark")"
sleep 14 # past the guard: the resume point says this run lasted more than 10 s
expect_contains "  the resume point says the run had lasted" "ran_ms=" "$(cat "$DATA/resume.txt")"
expect "  and it did not close cleanly" grep -qx 'clean=0' "$DATA/resume.txt"
mark="$(cc_now)"
kill -SEGV "$(cc_main_pid)"
expect "the crash screen shows (the app ran 14 s: a crash like any other)" wait_until 60 cc_launched screen 1
expect "  and its Restart is pressed, the app starts again" wait_until 60 cc_launched app 2
expect "  it says ready" wait_until 90 cc_ready
expect_equal "  the screen ended well (Restart exits 0)" "success" "$(systemctl show -p Result --value "$CC_SCREEN")"
expect_contains "  the screen pressed Restart" "pressing restart" "$(cc_screen_log "$mark")"
expect_contains "  the app started on the screen and the song the player left" "resuming the play screen and its song" "$(cc_app_log "$mark")"
expect "  with no card about the crash (Restart told it)" grep -q 'no crash report to tell' < <(cc_app_log "$mark")
expect "  their setting is as they left it" grep -qx 'note_style = neon' "$DATA/settings.conf"
expect "  the song is still the one" grep -qx 'song=scale.mid' "$DATA/resume.txt"
expect "  at the speed they chose" grep -qx 'speed=75' "$DATA/resume.txt"
expect "  with the hand they practise" grep -qx 'hands=left' "$DATA/resume.txt"
expect "  the crash was told as seen" test -s "$DATA/crashes/seen"
cc_story "$mark"

# --- 3. Restart and send report: the app starts with the report sheet open ------------------------------

echo "  Restart and send report: the app starts with the report sheet open"
cc_baseline
echo "--crash-screen-press restart-send" >"$CC_CTL/screen-always"
mark="$(cc_now)"
echo "--crash-test segv" >"$CC_CTL/app-once"
systemctl start "$CC_APP" >/dev/null 2>&1
expect "the crash screen shows and presses Restart and send report, the app starts again" wait_until 60 cc_launched app 2
expect "  and says ready" wait_until 90 cc_ready
expect_contains "  the screen pressed it" "pressing restart-send" "$(cc_screen_log "$mark")"
expect_contains "  the app opened the report sheet" "the crash report's sheet opens" "$(cc_app_log "$mark")"
expect "  the marker was taken (the sheet opens once)" test ! -e "$DATA/crashes/send-report"
cc_story "$mark"

# --- 4. abort, throw and a watchdog kill end the same way ----------------------------------------------------

for how in abort throw; do
    echo "  --crash-test $how: ends the same way"
    cc_baseline
    mark="$(cc_now)"
    echo "--crash-test $how" >"$CC_CTL/app-once"
    systemctl start "$CC_APP" >/dev/null 2>&1
    expect "$how: the crash screen starts" wait_until 60 cc_screen_up
    sleep 6
    expect_equal "  the app was not restarted" "1" "$(cc_launches app)"
    expect "  the app's unit is failed" cc_app_failed
    expect_equal "  it ended with a signal (the handler runs the default action again)" "signal" "$(systemctl show -p Result --value "$CC_APP")"
    expect_equal "  one report is in crashes/" "1" "$(cc_report_count)"
    want_signal=SIGABRT
    [[ "$how" != throw ]] || want_signal="uncaught C++ exception"
    expect_contains "  and it names how it ended" "$want_signal" "$(head -12 "$(cc_newest_report)" 2>/dev/null)"
    cc_story "$mark"
done

echo "  a watchdog kill (the watchdog shortened to 4 s here, 60 on a Pi): ends the same way"
cc_baseline
mkdir -p "/etc/systemd/system/$CC_APP.d"
printf '[Service]\nWatchdogSec=4\n' >"/etc/systemd/system/$CC_APP.d/zz-watchdog.conf"
systemctl daemon-reload
cc_start_app
sleep 12 # three periods of the watchdog
expect "the real app answers the watchdog from its frame loop and is left alone" cc_app_up
expect_equal "  it was started once" "1" "$(cc_launches app)"
mark="$(cc_now)"
kill -STOP "$(cc_main_pid)" # the frame loop stops: no more pings
expect "an app that stops answering is ended by the watchdog and the crash screen starts" wait_until 60 cc_screen_up
sleep 4
expect_equal "  the app was not restarted" "1" "$(cc_launches app)"
expect "  the app's unit is failed" cc_app_failed
expect_contains "  the journal says it was the watchdog" "Watchdog timeout" "$(journalctl -u "$CC_APP" --no-pager --since "$mark" 2>/dev/null)"
expect_equal "  the unit's result" "watchdog" "$(systemctl show -p Result --value "$CC_APP")"
cc_story "$mark"
rm -f "/etc/systemd/system/$CC_APP.d/zz-watchdog.conf"
systemctl daemon-reload

# --- 5. a new version on trial that crashes: rolled back first, then the crash screen -----------------------------

echo "  a new version (0.2.0, the same real build) on trial crashes: rolled back first, then the crash screen shows"
cc_baseline
cc_pack_real() { rm -rf "$1" && cp -a "$PREFIX/versions/0.1.0" "$1" && : >"$1/LICENSE"; }
systemctl enable --now etude-update.path >/dev/null 2>&1
cc_start_app
history_before="$(sha256sum "$DATA/history.db" 2>/dev/null | cut -d' ' -f1)"
mark="$(cc_now)"
echo "--crash-test segv" >"$CC_CTL/app-once" # the first start that finds it: the new version's
PACK_HOOK=cc_pack_real leave 0.2.0 ok
expect "the update is installed, the new version crashes on trial, and the old one comes back" wait_status rolled-back 200
expect "  the crash screen shows" wait_until 60 cc_screen_up
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  nothing is on trial any more" test "$(state_phase)" = none
expect_equal "  the app was started twice: the old one, then the new one on trial; nothing after" "2" "$(cc_launches app)"
expect "  the screen was started once" wait_until 10 cc_launched screen 1
expect "  the app is not running (the player chooses)" test "$(systemctl is-active "$CC_APP")" != active
expect_equal "  their data is back (the history is the one the old version left)" "$history_before" "$(sha256sum "$DATA/history.db" 2>/dev/null | cut -d' ' -f1)"
expect "  and their setting" grep -qx 'note_style = neon' "$DATA/settings.conf"
cc_story "$mark"
systemctl disable --now etude-update.path >/dev/null 2>&1

# --- 6. the reports are scrubbed and pruned to 10 ----------------------------------------------------------------------

echo "  the reports are scrubbed (by the crash screen, before it shows) and pruned to the newest 10 (at the app's next start)"
cc_baseline
mkdir -p "$DATA/crashes"
for n in $(seq 1 12); do
    printf 'etude crash report\nVersion: 0.0.%s\nSignal: SIGSEGV\n' "$n" >"$DATA/crashes/20260101-0000$(printf %02d "$n").txt"
done
chown -R etude: /home/etude
echo "--crash-screen-press restart" >"$CC_CTL/screen-always"
echo 4 >"$CC_CTL/screen-delay" # the screen waits, so the report is read as the handler left it
mark="$(cc_now)"
echo "--crash-test segv" >"$CC_CTL/app-once"
systemctl start "$CC_APP" >/dev/null 2>&1
expect "the app crashed and left its report beside the twelve old ones" wait_until 30 cc_reports_are 13
raw="$(cat "$(cc_newest_report)")"
expect_contains "  as the handler left it, the trace names the system's own libraries by path" "/lib/" "$raw"
expect "the crash screen reads it and the app starts again on Restart" wait_until 90 cc_launched app 2
expect "  and says ready" wait_until 90 cc_ready
scrubbed="$(cat "$(cc_newest_report)")"
expect_equal "  after the screen, no path of the system is left in the report" "0" "$(printf '%s\n' "$scrubbed" | grep -cE '(^|[ (])/(usr|lib|etc|proc|home)/' )"
expect_contains "  they read <path> instead" "<path>" "$scrubbed"
expect_contains "  the program's own frames keep their names" "crash_on_purpose" "$scrubbed"
expect_equal "  13 reports at the crash, the newest 10 after the next start" "10" "$(cc_report_count)"
expect "  the oldest were the ones removed" test ! -e "$DATA/crashes/20260101-000001.txt" -a ! -e "$DATA/crashes/20260101-000003.txt" -a -e "$DATA/crashes/20260101-000004.txt"
expect "  and the new report is kept" test -e "$(cc_newest_report)" -a "$(basename "$(cc_newest_report)")" != 20260101-000012.txt
cc_story "$mark"

# --- 7. a crash within 10 s of a start: the song list at the next start ----------------------------------------------------

echo "  a crash within 10 s of a start: the next start opens the song list, with the settings kept"
cc_baseline resume
echo "--crash-screen-press restart" >"$CC_CTL/screen-always"
mark="$(cc_now)"
echo "--crash-test segv" >"$CC_CTL/app-once"
systemctl start "$CC_APP" >/dev/null 2>&1
expect "the app crashes at its start and Restart starts it again" wait_until 60 cc_launched app 2
expect "  and it says ready" wait_until 90 cc_ready
expect_contains "  the second start says it opened the song list" "ended within 10 s of its start: opening the song list" "$(cc_app_log "$mark")"
expect "  the song of the point was not reopened by that start" grep -qx 'song=' "$DATA/resume.txt"
expect "  their setting is kept" grep -qx 'note_style = neon' "$DATA/settings.conf"
expect "  the point now says the song list" grep -qx 'screen=library' "$DATA/resume.txt"
cc_story "$mark"

# --- the machine as the systemd section left it ----------------------------------------------------------------------------------

sd_stop
rm -rf "/etc/systemd/system/$CC_APP.d" "/etc/systemd/system/$CC_SCREEN.d" "$CC_CTL"
systemctl daemon-reload
systemctl reset-failed 2>/dev/null
fi
