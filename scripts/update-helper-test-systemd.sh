#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The last section of scripts/update-helper-test.sh, sourced by it when it runs under a real systemd
# (PID 1): the real units run an update, a rollback, a stop, an unfinished restore, a crash that starts
# the crash screen (and Restart that starts the app), and the helper's sandbox and watchdog. It uses the
# functions, the variables and the counters of the script that sources it.
echo "== real systemd (PID 1 here): the real units run an update, a rollback, a stop and an unfinished restore"
# The stand-in service manager is out of the way; from here systemctl is the real one.
"$WORK/systemctl" stop etude.service
pkill -9 -f "$WORK/svc-run" 2>/dev/null
PREFIX=/opt/etude
STATE=/var/lib/etude-update
CONF=/etc/etude-update.conf
rm -rf "$PREFIX" "$STATE" /opt/etude
UNITDIR=/etc/systemd/system
for unit in etude-update.service etude-update.path etude-update-resume.service; do
    sed -e "s|@PREFIX@|$PREFIX|g" -e "s|@DATA_DIR@|$DATA|g" "$REPO/deploy/pi/$unit" >"$UNITDIR/$unit"
done
# The sandbox is one list, installed as a drop-in of both update units (deploy/pi/setup.sh does the
# same); the helper's `sandboxed` modes read the update service's copy. The test adds one folder to it
# for the files its stand-in systemctl writes (everything else under / is read-only to the helper).
mkdir -p /run/uh-test
for unit in etude-update.service etude-update-resume.service; do
    mkdir -p "$UNITDIR/$unit.d"
    sed -e "s|@PREFIX@|$PREFIX|g" -e "s|@APP_HOME@|/home/etude|g" "$REPO/deploy/pi/etude-update-sandbox.conf" >"$UNITDIR/$unit.d/sandbox.conf"
done
echo "ReadWritePaths=/run/uh-test" >>"$UNITDIR/etude-update.service.d/sandbox.conf"
# The app's unit as shipped, less what needs a console, a PAM session and the groups of a Pi (the
# count before each start, the time limits, the restart and the user stay as they are).
sed -E -e '/^(ConditionPathExists=\/dev\/tty0|SupplementaryGroups=|PAMName=|StandardInput=|TTYPath=|TTYReset=|TTYVHangup=|TTYVTDisallocate=|UtmpIdentifier=|UtmpMode=|ExecStartPost=|WatchdogSec=)/d' \
    -e "s|@PREFIX@|$PREFIX|g" "$REPO/deploy/pi/etude.service" >"$UNITDIR/etude.service"
# The helper's systemctl is a thin wrapper, so a test can make `show` slow (the count's time limit).
cat >/usr/local/bin/sd-systemctl <<'WRAP'
#!/bin/sh
# Under the sandbox (a `show` is only asked in a going back, which `count` does) it writes down what it
# can see of it: whether it may write under /etc, whether it may gain privileges, its capabilities.
if [ "$1" = show ]; then
    unit="$(cut -d: -f3 /proc/self/cgroup | sed 's|.*/||')"
    {
        if touch /etc/uh-sandbox-probe 2>/dev/null; then echo "etc: writable"; rm -f /etc/uh-sandbox-probe; else echo "etc: read-only"; fi
        grep -E '^(NoNewPrivs|CapBnd|Uid):' /proc/self/status
        echo "unit: $unit"
    } >/run/uh-test/probe 2>&1
    # Every `show` is written down with the unit it ran in: a count in place would say etude.service.
    echo "$unit" >>/run/uh-test/shows
    # The helper that asked is made to crash, as an app's input might make it.
    if [ -e /run/uh-crash-show ]; then kill -SEGV $PPID; sleep 5; fi
fi
if [ "$1" = show ] && [ -e /run/uh-slow-show ]; then echo "$*" >>/run/uh-test/slow-calls; sleep 30; fi
exec /usr/bin/systemctl "$@"
WRAP
chmod 0755 /usr/local/bin/sd-systemctl
# The crash screen's unit as shipped, less the same (its ExecCondition and its count stay as they are).
for unit in etude-crash-screen.service; do
    sed -E -e '/^(ConditionPathExists=\/dev\/tty0|SupplementaryGroups=|PAMName=|StandardInput=|TTYPath=|TTYReset=|TTYVHangup=|TTYVTDisallocate=|UtmpIdentifier=|UtmpMode=|ExecStartPost=|WatchdogSec=)/d' \
        -e "s|@PREFIX@|$PREFIX|g" "$REPO/deploy/pi/$unit" >"$UNITDIR/$unit"
done
# The labwc units as shipped, less the same, with a stand-in compositor that ends 0 whatever its session
# command did (as labwc does) and the session script installed where the units look for it.
install -D -m 0755 "$REPO/deploy/pi/etude-labwc-session" /usr/local/lib/etude/etude-labwc-session
cat >/usr/local/bin/labwc-stand-in <<'LABWC'
#!/bin/sh
# labwc -S COMMAND: runs COMMAND (split as labwc's parser does) and ends 0 whatever its status was.
[ "$1" = -S ] || exit 2
eval "set -- $2"
"$@"
exit 0
LABWC
chmod 0755 /usr/local/bin/labwc-stand-in
for unit in etude-labwc.service etude-labwc-crash-screen.service; do
    sed -E -e '/^(ConditionPathExists=\/dev\/tty0|SupplementaryGroups=|PAMName=|StandardInput=|TTYPath=|TTYReset=|TTYVHangup=|TTYVTDisallocate=|UtmpIdentifier=|UtmpMode=|ExecStartPost=|WatchdogSec=)/d' \
        -e "s|@PREFIX@|$PREFIX|g" -e 's|^\[Service\]$|[Service]\nEnvironment=ETUDE_LABWC=/usr/local/bin/labwc-stand-in|' "$REPO/deploy/pi/$unit" >"$UNITDIR/$unit"
done
printf 'PREFIX="%s"\nAPP_USER="etude"\nSYSTEMCTL="/usr/local/bin/sd-systemctl"\nSERVICE_TIMEOUT=8\nREADY_TIMEOUT=5\nMAX_STARTS=2\n' "$PREFIX" >"$CONF"
chmod 0644 "$CONF"
systemctl daemon-reload
expect "the units load (systemctl cat finds the update service)" systemctl cat etude-update.service
APP_UNIT=etude.service # the labwc section changes these two
CRASH_UNIT=etude-crash-screen.service
sd_alive() { systemctl is-active --quiet "$APP_UNIT"; }
alive() { sd_alive; }
# Stops units for good. A crash screen stopped by hand counts as a success, so its OnSuccess= would start
# the app while the test goes on to rebuild the prefix, and an app that was still starting can end in a
# crash screen after the stop: a later wait for ready then sees that stale run. So OnSuccess= is cleared
# for the length of the stop, and the stop is repeated until the units are inactive (or failed) with no
# job queued, twice in a row.
sd_stop() { # sd_stop [UNIT...] (default: every unit of the chain)
    local -a want=("$@")
    ((${#want[@]})) || want=(etude-update.service etude-crash-screen.service etude-labwc-crash-screen.service etude.service etude-labwc.service)
    local unit settled looks=0 tries=0
    for unit in etude-crash-screen.service etude-labwc-crash-screen.service; do
        mkdir -p "$UNITDIR/$unit.d"
        printf '[Unit]\nOnSuccess=\n' >"$UNITDIR/$unit.d/zz-no-onsuccess.conf"
    done
    systemctl daemon-reload
    while ((looks < 2 && tries++ < 60)); do
        systemctl stop "${want[@]}" 2>/dev/null
        settled=1
        for unit in "${want[@]}"; do
            case "$(systemctl is-active "$unit" 2>/dev/null)" in inactive | failed) ;; *) settled=0 ;; esac
        done
        [[ -z "$(systemctl list-jobs --no-legend 2>/dev/null)" ]] || settled=0
        if ((settled)); then looks=$((looks + 1)); else looks=0; fi
        sleep 0.3
    done
    for unit in etude-crash-screen.service etude-labwc-crash-screen.service; do
        rm -f "$UNITDIR/$unit.d/zz-no-onsuccess.conf"
        rmdir "$UNITDIR/$unit.d" 2>/dev/null
    done
    systemctl daemon-reload
    ((looks >= 2)) || flunk "sd_stop: ${want[*]} did not settle"
}
sd_baseline() { # sd_baseline [BEHAVIOUR of the baseline version, default ok]
    local behaviour="${1:-ok}"
    sd_stop
    systemctl reset-failed 2>/dev/null
    umount "$DATA" 2>/dev/null
    rm -rf "$PREFIX" "$STATE" "$SVC"/* "$DATA" "$WORK/pack"
    : >"$SVC/started" >"$SVC/started-at" >"$SVC/crash-screen-started" >"$SVC/press-restart" >"$SVC/crashed-once"
    chmod 0666 "$SVC/started" "$SVC/started-at" "$SVC/crash-screen-started" "$SVC/press-restart" "$SVC/crashed-once"
    mkdir -p "$PREFIX/versions" "$PREFIX/update" "$DATA" "$WORK/pack"
    chmod 0755 "$PREFIX" "$PREFIX/versions"
    chown etude: "$PREFIX/update"
    make_version "$PREFIX/versions/0.1.0" 0.1.0 "$behaviour"
    chown -R root:root "$PREFIX/versions"
    ln -s versions/0.1.0 "$PREFIX/current"
    echo "schema=0.1.0" >"$DATA/history.db"
    echo "pins" >"$DATA/settings.conf"
    chown -R etude: /home/etude
    [[ -z "${SD_SCREEN_CRASHES:-}" ]] || { echo 1 >"$SVC/crash-screen-crashes"; }
    systemctl start "$APP_UNIT"
    [[ "$behaviour" != ok ]] || wait_until 30 ready_is 0.1.0 || {
        flunk "the baseline app (0.1.0) did not say ready under systemd"
        systemctl status "$APP_UNIT" --no-pager 2>&1 | head -20
        journalctl -u "$APP_UNIT" --no-pager -n 30 2>&1
    }
}
crash_screens() { wc -l <"$SVC/crash-screen-started"; }
crash_screen_up() { systemctl is-active --quiet "$CRASH_UNIT"; }
press_restart() { echo 1 >"$SVC/press-restart"; } # the button: the stand-in screen exits 0 and systemd starts the app
app_starts() { wc -l <"$SVC/started"; }
screens_are() { [[ "$(crash_screens)" == "$1" ]] && crash_screen_up; }
going_back_unfinished() { # a going back whose data is never put back: the new version's data, no backup
    cat >"$STATE/state" <<STATE
phase=rolling-back
from=0.1.0
to=0.5.0
previous=
unit=etude.service
starts=2
backup=yes
reason=a test
STATE
    chmod 0600 "$STATE/state"
    mkdir -p "$PREFIX/versions/0.5.0/bin"
    rm -rf "$DATA/update-backup"
}
sd_baseline
expect "the app runs under systemd as the etude user" test "$(ps -o user= -p "$(systemctl show -p MainPID --value etude.service)" | tr -d ' ')" = etude
systemctl enable --now etude-update.path >/dev/null 2>&1
expect "the path unit is waiting for a request" systemctl is-active --quiet etude-update.path
# What the sandbox is, as systemd reports it for the running unit.
expect_equal "the update service is sandboxed: ProtectSystem=strict" "strict" "$(systemctl show -p ProtectSystem --value etude-update.service)"
expect_equal "the update service may not gain privileges" "yes" "$(systemctl show -p NoNewPrivileges --value etude-update.service)"
expect_equal "its time limit covers the helper's worst case (3201 s at the maxima): 1 h" "1h" "$(systemctl show -p TimeoutStartUSec --value etude-update.service)"

# The app leaves its files; the path unit starts the update service; nothing else is run by hand.
wait_status() { # wait_status STATE SECONDS
    wait_until "$2" status_is "$1"
}
leave 0.2.0 ok
expect "the update goes through, started by the path unit alone" wait_status ok 180
expect_equal "the update service ended well" "success" "$(systemctl show -p Result --value etude-update.service)"
expect_equal "current points at the new version" "versions/0.2.0" "$(current)"
expect "the new version runs under systemd and says ready" wait_until 20 ready_is 0.2.0
expect "the app service is active" sd_alive
expect_equal "the new version is the one that started last" "0.2.0" "$(last_started)"
expect_equal "the old data is backed up as the app's user" "etude" "$(stat -c %U "$DATA/update-backup/0.1.0/history.db" 2>/dev/null)"
expect_contains "the journal has the helper's account of it" "updated 0.1.0 to 0.2.0" "$(journalctl -u etude-update.service --no-pager 2>/dev/null)"
expect "nothing is on trial" test "$(state_phase)" = none
expect_equal "the helper was not killed by its sandbox (no SIGSYS or seccomp stop in the journal)" "0" "$(journalctl -u etude-update.service --no-pager 2>/dev/null | grep -ciE 'status=31/SYS|seccomp|Bad system call|Permission denied|Operation not permitted')"

leave 0.3.0 hang
expect "a version that never says ready: rolled back by the helper under systemd" wait_status rolled-back 240
expect_equal "current is back at the version before" "versions/0.2.0" "$(current)"
expect "the version before runs and says ready" wait_until 30 ready_is 0.2.0
expect_equal "its data is back" "schema=0.2.0" "$(cat "$DATA/history.db")"
expect "the failed version is removed" test ! -e "$PREFIX/versions/0.3.0"
expect_equal "it was started twice, no more" "2" "$(grep -c '^0.3.0$' "$SVC/started")"
expect_equal "the update service ended well (it went back, as designed)" "success" "$(systemctl show -p Result --value etude-update.service)"
expect "the app service is active" sd_alive
expect "nothing is on trial" test "$(state_phase)" = none

leave 0.4.0 hang
expect "an update on trial is under way" wait_until 60 phase_is pending
sleep 1
systemctl stop etude-update.service
expect "stopping the update service (SIGTERM from systemd) goes back" wait_status rolled-back 120
expect_contains "  the status says it was interrupted" "interrupted" "$(status reason)"
expect_equal "  current is the version before" "versions/0.2.0" "$(current)"
expect "  that version runs and says ready" wait_until 30 ready_is 0.2.0
expect "  the update service is stopped" test "$(systemctl is-active etude-update.service)" != active
expect_equal "  the data is back" "schema=0.2.0" "$(cat "$DATA/history.db")"
expect "  nothing is on trial" test "$(state_phase)" = none

# A going back whose data cannot be put back: the app does not start on the new version's data. Its
# unit fails (count ends 75), nothing restarts it, and the crash screen shows, with the reason.
systemctl stop etude.service
cat >"$STATE/state" <<STATE
phase=rolling-back
from=0.2.0
to=0.5.0
previous=
unit=etude.service
starts=2
backup=yes
reason=a test
STATE
chmod 0600 "$STATE/state"
mkdir -p "$PREFIX/versions/0.5.0/bin"
rm -rf "$DATA/update-backup/0.2.0"
started_before="$(wc -l <"$SVC/started")"
: >"$SVC/crash-screen-started"
systemctl start etude.service >/dev/null 2>&1
expect "the crash screen shows, not the app (count ended with 75, and nothing restarts the unit)" wait_until 60 crash_screen_up
expect_equal "  the app was not started while its restore is unfinished" "$started_before" "$(wc -l <"$SVC/started")"
expect_equal "  the app's unit failed" "failed" "$(systemctl is-active etude.service)"
expect "  the state waits for the data" test "$(state_phase)" = rolling-back
expect "  the status names the reason for the screen" test -n "$(status data)"
sleep 6
expect_equal "  nothing restarts the app: still not started" "$started_before" "$(wc -l <"$SVC/started")"
expect_equal "  and one crash screen, no loop" "1" "$(crash_screens)"
mkdir -p "$DATA/update-backup/0.2.0"
echo "schema=0.2.0" >"$DATA/update-backup/0.2.0/history.db"
echo "version=0.2.0" >"$DATA/update-backup/0.2.0/.complete"
chown -R etude: "$DATA"
press_restart # Try again
expect "with the data there, Try again starts the app" wait_until 60 sd_alive
expect "  and the app says ready" wait_until 30 ready_is 0.2.0
expect "  and nothing is pending" test "$(state_phase)" = none
expect "  and the status no longer names a reason" test -z "$(status data)"
expect "  the crash screen is gone" test "$(systemctl is-active etude-crash-screen.service)" != active

# --- a crash: the crash screen and not the app; Restart starts the app --------------------------------
echo "  a crash starts the crash screen, never the app; Restart starts the app"
expect_equal "etude.service does not restart by itself" "no" "$(systemctl show -p Restart --value etude.service)"
expect_contains "  and a failure starts the crash screen" "etude-crash-screen.service" "$(systemctl show -p OnFailure --value etude.service)"
sd_baseline crash-once
expect "a crash starts the crash screen" wait_until 40 crash_screen_up
expect_equal "  the app was started once" "1" "$(app_starts)"
expect_equal "  its unit is failed" "failed" "$(systemctl is-active etude.service)"
sleep 8
expect_equal "  nothing starts it again: no restart, no loop" "1" "$(app_starts)"
expect_equal "  one crash screen" "1" "$(crash_screens)"
expect_equal "  the screen runs the same binary as the app's user" "etude" "$(ps -o user= -p "$(systemctl show -p MainPID --value etude-crash-screen.service)" | tr -d ' ')"
press_restart
expect "Restart starts the app" wait_until 40 sd_alive
expect "  and it says ready" wait_until 30 ready_is 0.1.0
expect_equal "  the app was started twice in all" "2" "$(app_starts)"
expect "  the crash screen is gone" test "$(systemctl is-active etude-crash-screen.service)" != active
expect_equal "  and ended well (it exited 0 for Restart)" "success" "$(systemctl show -p Result --value etude-crash-screen.service)"

# An update asked for while the crash screen is up: there is no app service to try it under, so the
# helper refuses it and nothing moves.
sd_baseline crash
expect "the crash screen is up" wait_until 40 crash_screen_up
rm -f "$PREFIX/update/status"
printf 'version=9.9.9\n' >"$PREFIX/update/request"
chown etude: "$PREFIX/update/request"
expect "an update asked for while the crash screen is up is refused" wait_status failed 60
expect_contains "  and says there is no app service to try it under" "not running from its own service" "$(status reason)"
expect_equal "  nothing was switched" "versions/0.1.0" "$(current)"
expect "  and the crash screen is still up" crash_screen_up

# A crash in the crash screen just stops: nothing starts the app or the screen again.
SD_SCREEN_CRASHES=1 sd_baseline crash
screens_at_least() { (( $(crash_screens) >= $1 )); }
expect "a crash in the crash screen: the screen was started" wait_until 40 screens_at_least 1
sleep 8
expect_equal "  it just stops: one screen, no more" "1" "$(crash_screens)"
expect_equal "  and the app was not started again" "1" "$(app_starts)"
expect "  the screen is not running and not restarting" test "$(systemctl is-active etude-crash-screen.service)" != active
expect_equal "  it ended with its own failure" "exit-code" "$(systemctl show -p Result --value etude-crash-screen.service)"
expect_equal "  etude.service is failed, not restarted" "failed" "$(systemctl is-active etude.service)"

echo "  a new version that crashes on trial is rolled back at once, with no question, and then the crash screen shows"
sd_baseline
leave 0.8.0 crash
expect "the trial of a version that crashes at once ends in a rollback" wait_status rolled-back 100
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the crash screen shows" wait_until 40 crash_screen_up
expect_equal "  the crashing version was started once: nothing restarted it" "1" "$(grep -c '^0.8.0$' "$SVC/started")"
expect_equal "  the data is back" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "  the status names no reason for the screen (the data is back)" test -z "$(status data)"
expect "  nothing is on trial" test "$(state_phase)" = none
sleep 4
expect_equal "  the old version was not started on its own (only the baseline start)" "1" "$(grep -c '^0.1.0$' "$SVC/started")"
press_restart
expect "  Restart starts the previous version" wait_until 40 sd_alive
expect "  and it says ready" wait_until 30 ready_is 0.1.0
expect "  the unit did not hit any start limit" test -z "$(journalctl -u etude.service --no-pager 2>/dev/null | grep 'repeated too quickly')"

# --- under labwc: the compositor ends 0 whatever the app did; the session script passes the app's status on ---
echo "  under labwc (a compositor that ends 0 whatever its session did): the app's status still decides"
APP_UNIT=etude-labwc.service
CRASH_UNIT=etude-labwc-crash-screen.service
expect_equal "etude-labwc.service does not restart by itself" "no" "$(systemctl show -p Restart --value $APP_UNIT)"
sd_baseline crash-once
expect "a crash under labwc starts the labwc crash screen (the unit failed, though the compositor ended 0)" wait_until 40 crash_screen_up
expect_equal "  the app was started once" "1" "$(app_starts)"
expect_equal "  the unit is failed" "failed" "$(systemctl is-active $APP_UNIT)"
expect_equal "  with the app's own exit status" "1" "$(systemctl show -p ExecMainStatus --value $APP_UNIT)"
expect "  and the unit's runtime folder (the session script's status file) went with it" test ! -e /run/etude-labwc
sleep 6
expect_equal "  nothing starts the app again" "1" "$(app_starts)"
expect_equal "  and one crash screen" "1" "$(crash_screens)"
press_restart
expect "Restart (the labwc crash screen exits 0) starts the labwc app" wait_until 40 sd_alive
expect "  and it says ready" wait_until 30 ready_is 0.1.0
expect_equal "  the app was started twice in all" "2" "$(app_starts)"
expect_equal "  the labwc crash screen ended well" "success" "$(systemctl show -p Result --value $CRASH_UNIT)"
expect "  and is gone" test "$(systemctl is-active $CRASH_UNIT)" != active

# A crash of the labwc crash screen is a crash: the compositor ends 0, the unit must not, or OnSuccess=
# would start the app on a screen nobody pressed Restart on.
SD_SCREEN_CRASHES=1 sd_baseline crash
expect "a crash in the labwc crash screen: the screen was started" wait_until 40 screens_at_least 1
sleep 8
expect_equal "  it just stops: one screen" "1" "$(crash_screens)"
expect_equal "  and the app was not started again (the screen's exit 1 is not a Restart)" "1" "$(app_starts)"
expect_equal "  it ended with its own failure, not well" "exit-code" "$(systemctl show -p Result --value $CRASH_UNIT)"

# A version on trial that crashes by a signal: the helper's watch sees the failed unit and goes back at once.
sd_baseline
leave 0.7.0 segv
expect "a trial that dies of a SEGV under labwc is rolled back" wait_status rolled-back 100
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the labwc crash screen shows" wait_until 40 crash_screen_up
expect_equal "  the crashing version was started once" "1" "$(grep -c '^0.7.0$' "$SVC/started")"
expect_equal "  the data is back" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect_equal "  the unit failed with the shell's status for the signal" "139" "$(systemctl show -p ExecMainStatus --value $APP_UNIT)"
press_restart
expect "  Restart starts the old version" wait_until 40 sd_alive
expect "  and it says ready" wait_until 30 ready_is 0.1.0
sd_stop etude-labwc.service etude-labwc-crash-screen.service
APP_UNIT=etude.service
CRASH_UNIT=etude-crash-screen.service

# --- a trial that hangs to its last start, with a restore that fails: the helper's own start of the app
# ends 75, the crash screen that OnFailure= starts is held back while the helper acts, so the helper starts it
echo "  a trial that hangs to its last start, with a restore that cannot finish, ends on the crash screen with the reason"
sd_baseline
leave 0.6.0 hang
expect "an update on trial is under way" wait_until 60 phase_is pending
rm -rf "$DATA/update-backup" # the data of the old version cannot be put back
expect "the crash screen shows (the helper started it after its own start of the app failed)" wait_until 240 crash_screen_up
expect "  the status names the reason" test -n "$(status data)"
expect "  the data waits to be put back" test "$(state_phase)" = rolling-back
expect_equal "  the old version was not started on the new version's data" "1" "$(grep -c '^0.1.0$' "$SVC/started")"
# The screen's unit is active the moment it is forked; its stand-in writes its line a few ms later.
expect "  one crash screen" wait_until 10 screens_are 1
sleep 6
expect_equal "  and still one, and no app" "1" "$(crash_screens)"
mkdir -p "$DATA/update-backup/0.1.0"
echo "schema=0.1.0" >"$DATA/update-backup/0.1.0/history.db"
echo "version=0.1.0" >"$DATA/update-backup/0.1.0/.complete"
chown -R etude: "$DATA"
press_restart
expect "  with the data there, Try again starts the old version" wait_until 60 sd_alive
expect "  and it says ready" wait_until 30 ready_is 0.1.0
expect_equal "  its data is back" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "  and nothing is pending" test "$(state_phase)" = none

# --- a stop that really hangs: the unit is deactivating, the helper kills it and goes on -----------
mkdir -p /etc/systemd/system/etude.service.d /etc/systemd/system/etude-crash-screen.service.d
printf '[Service]\nTimeoutStartSec=8\nTimeoutStopSec=40\n' >/etc/systemd/system/etude.service.d/test.conf
printf '[Service]\nTimeoutStartSec=8\n' >/etc/systemd/system/etude-crash-screen.service.d/test.conf
systemctl daemon-reload
sd_baseline
sd_mark="$(date '+%Y-%m-%d %H:%M:%S')"
: >"$SVC/states"
( while :; do systemctl is-active etude.service >>"$SVC/states" 2>/dev/null; sleep 1; done ) &
SAMPLER=$!
leave 0.9.0 deaf
expect "a version that ignores SIGTERM: the trial ends in a rollback although its stop hangs" wait_status rolled-back 400
kill "$SAMPLER" 2>/dev/null
expect_contains "  the unit was really deactivating while it hung" "deactivating" "$(cat "$SVC/states")"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the old version runs and says ready" wait_until 40 ready_is 0.1.0
expect_equal "  the data is restored" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "  the app service is active" sd_alive
expect_equal "  nothing is on trial" "none" "$(state_phase)"
expect_contains "  the failed unit did start the crash screen, and its condition skipped it (the helper was acting)" "exec-condition" "$(journalctl -u etude-crash-screen.service --no-pager --since "$sd_mark" 2>/dev/null)"
expect_equal "  the crash screen did not show in the middle of the going back" "0" "$(crash_screens)"
expect "  and it is not running now" test "$(systemctl is-active etude-crash-screen.service)" != active

# --- the count's own time limit (made short here: 8 s instead of 600) ------------------------------
sd_baseline
systemctl stop etude.service
mkdir -p "$DATA/update-backup/0.1.0"
echo "schema=0.1.0" >"$DATA/update-backup/0.1.0/history.db"
echo "version=0.1.0" >"$DATA/update-backup/0.1.0/.complete"
echo "schema=0.2.0" >"$DATA/history.db" # the new version's data, to be put back
chown -R etude: "$DATA"
cat >"$STATE/state" <<STATE
phase=rolling-back
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=2
backup=yes
reason=a test
STATE
chmod 0600 "$STATE/state"
mkdir -p "$PREFIX/versions/0.2.0/bin"
touch /run/uh-slow-show # the count's `show` takes 30 s, longer than its unit's 8
started_before="$(wc -l <"$SVC/started")"
systemctl start etude.service >/dev/null 2>&1
expect "the count that outlasts its time limit fails the start" test "$(systemctl is-active etude.service)" != active
expect_equal "  and the app was not started" "$started_before" "$(wc -l <"$SVC/started")"
rm -f /run/uh-slow-show
expect "  the crash screen shows (nothing else starts the app)" wait_until 40 crash_screen_up
press_restart
expect "  with the count quick again, Restart finishes the restore and starts the app" wait_until 90 sd_alive
expect_equal "  the data is the old version's" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "  nothing is pending" test "$(state_phase)" = none
rm -rf /etc/systemd/system/etude.service.d /etc/systemd/system/etude-crash-screen.service.d
systemctl daemon-reload

# --- the helper's own trouble never keeps the app from starting (the unit's outcome, not only the exit code)
echo "  count's own trouble: the app unit starts"
sd_baseline
systemctl stop etude.service
cp "$CONF" "$WORK/conf-good"
for trouble in world-writable no-such-user; do
    cp "$WORK/conf-good" "$CONF"
    if [[ "$trouble" == world-writable ]]; then
        chmod 0666 "$CONF"
    else
        sed -i 's/^APP_USER=.*/APP_USER="nobody-by-that-name"/' "$CONF"
    fi
    since_conf="$(date '+%Y-%m-%d %H:%M:%S')"
    systemctl reset-failed etude.service 2>/dev/null
    systemctl start etude.service >/dev/null 2>&1
    expect "settings the helper cannot use ($trouble): the app unit still starts" wait_until 30 sd_alive
    expect_contains "  and the helper said why in the journal" "refusing to run" "$(journalctl -u etude.service --no-pager --since "$since_conf" 2>/dev/null)"
    systemctl stop etude.service
done
cp "$WORK/conf-good" "$CONF"
chmod 0644 "$CONF"

# --- a restore that stays impossible: the crash screen at every start the player asks for, the app never,
# and no limit on the attempts
echo "  a restore that stays impossible: the crash screen each time, never the app, no limit"
sd_baseline
systemctl stop etude.service
going_back_unfinished
: >"$SVC/started"
: >"$SVC/crash-screen-started"
since_lockout="$(date '+%Y-%m-%d %H:%M:%S')"
systemctl start etude.service >/dev/null 2>&1
for attempt in 1 2 3 4; do
    expect "attempt $attempt: the crash screen shows" wait_until 60 screens_are "$attempt"
    expect "  the status names the reason" test -n "$(status data)"
    expect "  and $attempt attempts are counted" grep -q "^restores=$attempt\$" "$STATE/state"
    press_restart
done
expect_contains "  the status says why, in the person's words" "Couldn't put your data back" "$(status reason)"
expect "  the data was never put back: the record stays" test "$(state_phase)" = rolling-back
expect_equal "  the app was never started on the new version's data" "0" "$(wc -l <"$SVC/started")"
expect_contains "  count ended 75 at each start" "status=75" "$(journalctl -u etude.service --no-pager --since "$since_lockout" 2>/dev/null)"
expect_equal "  and the unit never hit its start limit" "0" "$(journalctl -u etude.service --no-pager --since "$since_lockout" 2>/dev/null | grep -c 'repeated too quickly')"

# --- a restore that is slow as well as failing: one attempt for each start the player asks for, no loop
echo "  a slow failed restore costs one slow attempt for each start asked for, and no more"
sd_baseline
sd_stop etude-crash-screen.service etude.service
systemctl reset-failed 2>/dev/null
going_back_unfinished
: >"$SVC/crash-screen-started"
rm -f /run/uh-test/slow-calls
touch /run/uh-slow-show # each restore waits for `show`, which takes 30 s against the helper's 8
systemctl start etude.service >/dev/null 2>&1 &
expect "the crash screen shows after the slow attempt" wait_until 200 screens_are 1
expect_equal "  the slow restore was tried once, not in a loop" "1" "$(wc -l </run/uh-test/slow-calls)"
sleep 12
expect_equal "  and still once a while later" "1" "$(wc -l </run/uh-test/slow-calls)"
press_restart
expect "  Try again makes the second attempt, and the screen shows again" wait_until 200 screens_are 2
expect_equal "  two attempts, two slow calls" "2" "$(wc -l </run/uh-test/slow-calls)"
rm -f /run/uh-slow-show
sd_stop etude-crash-screen.service

# --- count and idle run in the sandbox of the update service: one list, a transient unit, the same status ----
echo "== count and idle run as transient services under the update service's sandbox"
# The last `show` in the going back above was asked by the helper's `count`, from inside its sandbox.
probe="$(cat /run/uh-test/probe 2>/dev/null)"
expect_contains "the count ran in a transient unit of its own" "etude-helper-count-" "$probe"
expect_contains "  the file system is read-only to it (ProtectSystem=strict)" "etc: read-only" "$probe"
expect_contains "  it cannot gain privileges (NoNewPrivileges)" "NoNewPrivs:	1" "$probe"
expect_contains "  its capabilities are the four of the list (DAC_OVERRIDE, KILL, SETGID, SETUID)" "CapBnd:	00000000000000e2" "$probe"
for unit in etude-update.service etude-update-resume.service; do
    expect_equal "$unit has the same sandbox list: ProtectSystem" "strict" "$(systemctl show -p ProtectSystem --value $unit)"
    expect_equal "  ProtectHome" "read-only" "$(systemctl show -p ProtectHome --value $unit)"
    expect_contains "  ReadWritePaths" "/var/lib/etude-update" "$(systemctl show -p ReadWritePaths --value $unit)"
    expect_equal "  CapabilityBoundingSet" "$(systemctl show -p CapabilityBoundingSet --value etude-update.service)" "$(systemctl show -p CapabilityBoundingSet --value $unit)"
    expect_equal "  SystemCallFilter" "$(systemctl show -p SystemCallFilter --value etude-update.service)" "$(systemctl show -p SystemCallFilter --value $unit)"
done
no_helper_units() { test -z "$(systemctl list-units --all --no-legend 'etude-helper-*' 2>/dev/null)"; }
wait_until 60 no_helper_units # a run that is still ending (the crash screen's, a moment ago) is not left behind
expect_equal "no transient unit of the helper is left behind (--collect)" "" "$(systemctl list-units --all --no-legend 'etude-helper-*' 2>/dev/null)"
# A going back that is recorded, so the next count asks systemd about the app's data folder (`show`).
going_back_state() {
    cat >"$STATE/state" <<GOINGBACK
phase=rolling-back
from=0.1.0
to=0.5.0
previous=
unit=etude.service
starts=2
backup=yes
reason=a test
GOINGBACK
    chmod 0600 "$STATE/state"
    mkdir -p "$PREFIX/versions/0.5.0/bin"
}
only_in_the_sandbox() { # only_in_the_sandbox DESCRIPTION: the one `show` of the last start ran in the transient unit
    expect_equal "$1: the helper ran once" "1" "$(wc -l </run/uh-test/shows)"
    expect_contains "  and in a transient unit, never in the app unit" "etude-helper-count-" "$(cat /run/uh-test/shows)"
}
echo "  a sandboxed count that crashes, and one that hangs past its limit, each run once and never in place"
sd_baseline
systemctl stop etude.service
going_back_state
: >/run/uh-test/shows
touch /run/uh-crash-show
since_crash="$(date '+%Y-%m-%d %H:%M:%S')"
systemctl reset-failed etude.service 2>/dev/null
systemctl start etude.service >/dev/null 2>&1
rm -f /run/uh-crash-show
expect "the app unit starts after a count that crashed (the helper's own trouble)" wait_until 30 sd_alive
only_in_the_sandbox "a count that crashes"
expect_contains "  and the journal says it is not run again" "it is not run again" "$(journalctl -u etude.service --no-pager --since "$since_crash" 2>/dev/null)"
systemctl stop etude.service
echo "RuntimeMaxSec=6" >>"$UNITDIR/etude-update.service.d/sandbox.conf" # a later line wins over the helper's own limit
sd_baseline
systemctl stop etude.service
going_back_state
: >/run/uh-test/shows
touch /run/uh-slow-show # the count's `show` takes 30 s, longer than the 6 s it may run
since_hang="$(date '+%Y-%m-%d %H:%M:%S')"
systemctl reset-failed etude.service 2>/dev/null
systemctl start etude.service >/dev/null 2>&1
rm -f /run/uh-slow-show
sed -i '/^RuntimeMaxSec=/d' "$UNITDIR/etude-update.service.d/sandbox.conf"
expect "the app unit starts after a count that was ended at its limit" wait_until 60 sd_alive
only_in_the_sandbox "a count that hangs past its limit"
expect_contains "  and the journal says it is not run again" "it is not run again" "$(journalctl -u etude.service --no-pager --since "$since_hang" 2>/dev/null)"
systemctl stop etude.service
echo "  a data folder the app made a link (in a loop, or to /etc) does not switch the sandbox off"
for kind in loop etc; do
    sd_baseline
    systemctl stop etude.service
    rm -rf "$DATA"
    if [[ "$kind" == loop ]]; then ln -s "$DATA" "$DATA"; else ln -s /etc "$DATA"; fi
    chown -h etude: "$DATA"
    going_back_state
    : >/run/uh-test/shows
    rm -f /run/uh-test/probe
    systemctl reset-failed etude.service 2>/dev/null
    systemctl start etude.service >/dev/null 2>&1 &
    # The going back cannot finish on such a folder (the helper refuses a path through a link it did not
    # make), so the app unit retries it a few times: every one of those counts runs in the sandbox.
    expect "the count was asked about the data folder ($kind)" wait_until 60 test -s /run/uh-test/shows
    sleep 3
    systemctl stop etude.service
    expect_equal "  and every run was in a transient unit, never in the app unit" "0" "$(grep -vc '^etude-helper-count-' /run/uh-test/shows)"
    expect_contains "  /etc is read-only to the count" "etc: read-only" "$(cat /run/uh-test/probe 2>/dev/null)"
    expect_contains "  and its own file system is the sandbox's" "NoNewPrivs:	1" "$(cat /run/uh-test/probe 2>/dev/null)"
    rm -f "$DATA"
done
echo "  without the sandbox file the count runs in place and says so, and the app starts"
sd_baseline
systemctl stop etude.service
mv "$UNITDIR/etude-update.service.d/sandbox.conf" /run/uh-test/sandbox.conf.away
since_nosandbox="$(date '+%Y-%m-%d %H:%M:%S')"
systemctl reset-failed etude.service 2>/dev/null
systemctl start etude.service >/dev/null 2>&1
expect "the app unit still starts" wait_until 30 sd_alive
expect_contains "  and the journal says there was no sandbox" "no sandbox" "$(journalctl -u etude.service --no-pager --since "$since_nosandbox" 2>/dev/null)"
mv /run/uh-test/sandbox.conf.away "$UNITDIR/etude-update.service.d/sandbox.conf"
systemctl stop etude.service

# --- a sandbox that cannot start: what needs no sandbox is done in place, and never the data -------------------
echo "== a sandbox that cannot start (an unknown property, as a refused mount would be): counting and going back still happen, in place, without the data"
current_is_old() { [[ "$(current)" == versions/0.1.0 ]]; }
restores_are() { grep -q "^restores=$1\$" "$STATE/state" 2>/dev/null; }
sd_baseline
systemctl stop etude.service
make_version "$PREFIX/versions/0.2.0" 0.2.0 crash
chown -R root:root "$PREFIX/versions"
ln -sfn versions/0.2.0 "$PREFIX/current"
ln -sfn versions/0.1.0 "$PREFIX/previous"
mkdir -p "$DATA/update-backup/0.1.0"
echo "schema=0.1.0" >"$DATA/update-backup/0.1.0/history.db"
echo "version=0.1.0" >"$DATA/update-backup/0.1.0/.complete"
echo "schema=0.2.0" >"$DATA/history.db"
chown -R etude: "$DATA"
cat >"$STATE/state" <<STATE
phase=pending
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=0
backup=yes
reason=
STATE
chmod 0600 "$STATE/state"
echo "BogusProperty=1" >>"$UNITDIR/etude-update.service.d/sandbox.conf" # systemd-run refuses it: no unit ever starts
: >/run/uh-test/shows
since_nosandbox="$(date '+%Y-%m-%d %H:%M:%S')"
home_snapshot() { find /home/etude -printf '%p %y %s %m %T@\n' 2>/dev/null | sort | md5sum; }
home_before="$(home_snapshot)"
systemctl reset-failed etude.service 2>/dev/null
systemctl start etude.service >/dev/null 2>&1 &
expect "the version on trial that crashes is gone back from, with no sandbox: current is the old version" wait_until 120 current_is_old
expect "  and the crash screen shows, not the app" wait_until 60 crash_screen_up
expect "  the start was counted (once)" grep -q '^starts=1$' "$STATE/state"
expect "  the data restore is left pending: the record still says going back" test "$(state_phase)" = rolling-back
expect_equal "  the data is exactly as the crashing version left it" "schema=0.2.0" "$(cat "$DATA/history.db")"
expect_equal "  the crashing version was started once" "1" "$(grep -c '^0.2.0$' "$SVC/started")"
expect_equal "  systemd was never asked about the data folder" "0" "$(grep -c . /run/uh-test/shows)"
expect_contains "  the journal says what was done" "the sandbox could not be started" "$(journalctl -u etude-crash-screen.service --no-pager --since "$since_nosandbox" 2>/dev/null)"
press_restart
expect "Restart: the app is kept from starting on the new version's data (the attempt is counted)" wait_until 60 restores_are 1
expect "  and the crash screen shows again, with the reason" wait_until 60 screens_are 2
expect "  the status names it" test -n "$(status data)"
expect_equal "  the data is still the crashing version's" "schema=0.2.0" "$(cat "$DATA/history.db")"
expect_equal "  the crashing version was not started again" "1" "$(grep -c '^0.2.0$' "$SVC/started")"
expect_equal "  nothing under the app's home was read for write or changed by the in-place runs" "$home_before" "$(home_snapshot)"
expect_contains "  the journal says the count could not use its sandbox either" "the sandbox could not be started" "$(journalctl -u etude.service --no-pager --since "$since_nosandbox" 2>/dev/null)"
echo "  idle in place: busy while the helper is acting on the app, free when it is not"
cat >/run/uh-test/bogus-sandbox.conf <<'BOGUS'
[Service]
BogusProperty=1
BOGUS
chmod 0644 /run/uh-test/bogus-sandbox.conf
sd_stop etude-crash-screen.service etude.service
systemctl reset-failed 2>/dev/null
( flock -x "$STATE/acting.lock" sleep 4 ) &
sleep 1
"$HELPER" --config "$CONF" --sandbox /run/uh-test/bogus-sandbox.conf sandboxed idle >/dev/null 2>&1
expect_equal "idle with no sandbox and the helper acting: busy (the crash screen does not show for its own stop)" "1" "$?"
wait
"$HELPER" --config "$CONF" --sandbox /run/uh-test/bogus-sandbox.conf sandboxed idle >/dev/null 2>&1
expect_equal "  and with the helper not acting: free" "0" "$?"
sed -i '/^BogusProperty=/d' "$UNITDIR/etude-update.service.d/sandbox.conf"
rm -f /run/uh-test/bogus-sandbox.conf

# --- the watchdog: a hung app is ended by systemd and the crash screen shows; a hung trial is gone back from ----
echo "== the watchdog (shortened here to 4 s; the shipped units say 60)"
for unit in etude.service etude-labwc.service; do
    expect "the shipped $unit has WatchdogSec=60" grep -qx 'WatchdogSec=60' "$REPO/deploy/pi/$unit"
done
expect "the shipped etude.service lets only its main process answer" grep -qx 'NotifyAccess=main' "$REPO/deploy/pi/etude.service"
mkdir -p /etc/systemd/system/etude.service.d
printf '[Service]\nWatchdogSec=4\n' >/etc/systemd/system/etude.service.d/watchdog.conf
systemctl daemon-reload
expect_equal "the unit says only its main process may answer the watchdog" "main" "$(systemctl show -p NotifyAccess --value etude.service)"
sd_baseline pings
expect_equal "the running unit's watchdog is armed" "4s" "$(systemctl show -p WatchdogUSec --value etude.service)"
sleep 14 # three periods of the watchdog
expect_equal "an app that answers the watchdog from its loop is not restarted" "1" "$(wc -l <"$SVC/started")"
expect "  and runs" sd_alive
sd_baseline stalls
watchdog_mark="$(date '+%Y-%m-%d %H:%M:%S')"
expect "an app whose frame hangs (it stops answering) is ended by the watchdog, and the crash screen shows" wait_until 40 crash_screen_up
expect_contains "  the journal says it was the watchdog" "Watchdog timeout" "$(journalctl -u etude.service --no-pager --since "$watchdog_mark" 2>/dev/null)"
expect_equal "  the app was not restarted by systemd" "1" "$(app_starts)"
sd_stop etude-crash-screen.service etude.service
systemctl reset-failed 2>/dev/null

echo "  a new version on trial that hangs before ready, with the helper gone: the watchdog ends it, the crash screen goes back from it at once"
sd_baseline pings
systemctl stop etude.service
make_version "$PREFIX/versions/0.2.0" 0.2.0 hang
chown -R root:root "$PREFIX/versions"
ln -sfn versions/0.2.0 "$PREFIX/current"
ln -sfn versions/0.1.0 "$PREFIX/previous"
mkdir -p "$DATA/update-backup/0.1.0"
echo "schema=0.1.0" >"$DATA/update-backup/0.1.0/history.db"
echo "version=0.1.0" >"$DATA/update-backup/0.1.0/.complete"
echo "schema=0.2.0" >"$DATA/history.db"
chown -R etude: "$DATA"
cat >"$STATE/state" <<STATE
phase=pending
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=0
backup=yes
reason=
STATE
chmod 0600 "$STATE/state"
: >"$SVC/started"
: >"$SVC/crash-screen-started"
watchdog_mark="$(date '+%Y-%m-%d %H:%M:%S')"
systemctl start etude.service >/dev/null 2>&1 &
expect "the version that hangs is ended by the watchdog and the crash screen goes back from it" wait_status rolled-back 120
expect "  and shows" wait_until 40 crash_screen_up
expect_equal "  it was started once: nothing restarted it" "1" "$(grep -c '^0.2.0$' "$SVC/started")"
expect_contains "  the journal says the watchdog ended it" "Watchdog timeout" "$(journalctl -u etude.service --no-pager --since "$watchdog_mark" 2>/dev/null)"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect_equal "  its data is back" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "  nothing is on trial" test "$(state_phase)" = none
expect "  the app is not running: the player chooses" test "$(systemctl is-active etude.service)" != active
press_restart
expect "  Restart starts the old version" wait_until 40 sd_alive
sd_stop etude.service etude-crash-screen.service
rm -rf /etc/systemd/system/etude.service.d
systemctl daemon-reload
systemctl disable --now etude-update.path >/dev/null 2>&1
systemctl stop etude.service 2>/dev/null
echo "  (systemd-analyze security, for the record)"
systemd-analyze security etude-update.service --no-pager 2>/dev/null | tail -3 | sed 's/^/    /'
