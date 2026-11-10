// SPDX-License-Identifier: GPL-3.0-or-later
#include "mac_relaunch_script.hpp"

namespace etude::app {

namespace {

// POSIX sh, run by /bin/sh. It is written to the update folder by the app and started in a session
// of its own, so it outlives the app it replaces. Every file it needs is named in its arguments.
constexpr std::string_view kScript = R"SH(#!/bin/sh
# Replaces the running app bundle with a checked new one, starts it, and puts the old one back when
# the new one does not reach its first frame in time.
#
#   relaunch.sh APP_PID BUNDLE NEW_BUNDLE PREVIOUS READY STATUS VERSION FROM WAIT_SECONDS
#
#   APP_PID       the running app, which has been asked to quit
#   BUNDLE        the .app the app runs from; afterwards it holds the new version
#   NEW_BUNDLE    the unpacked, checked new version, in a folder beside BUNDLE
#   PREVIOUS      where the old bundle is kept
#   READY         the file the new version writes (version=X.Y.Z) once its first frame is on screen
#   STATUS        the file this script writes the verdict to (state, version, from, reason, time)
#   VERSION FROM  the new version and the one it replaces
#   WAIT_SECONDS  how long the new version has to write READY
#
# ETUDE_OPEN replaces /usr/bin/open, ETUDE_QUIT_WAIT (seconds) the wait for the app to close,
# ETUDE_SWAP the command that exchanges two folders (a program and its arguments before the two
# paths), and, for tests only, ETUDE_TICK (seconds, default 0.2): the pause between two looks. It
# scales every wait here, since the counts of looks assume 0.2 s.
set -u
[ $# -eq 9 ] || { echo "relaunch.sh: 9 arguments needed" >&2; exit 2; }
app_pid=$1; bundle=$2; new_bundle=$3; previous=$4; ready=$5; status=$6
version=$7; from=$8; wait_seconds=$9
open_cmd=${ETUDE_OPEN:-/usr/bin/open}
quit_wait=${ETUDE_QUIT_WAIT:-60}
tick=${ETUDE_TICK:-0.2}
parent=$(dirname "$bundle")
staging=$(dirname "$new_bundle")
log=$(dirname "$status")/relaunch.log

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >>"$log" 2>/dev/null; }

# write_status STATE REASON: whole file or none, like the app's own files.
write_status() {
    {
        printf 'state=%s\nversion=%s\nfrom=%s\n' "$1" "$version" "$from"
        [ -z "$2" ] || printf 'reason=%s\n' "$2"
        printf 'time=%s\n' "$(date +%s)"
    } >"$status.part" 2>/dev/null && mv -f "$status.part" "$status"
}

launch() { "$open_cmd" "$1" >>"$log" 2>&1; }

# The new version's process, by its program's path (the app's PID changes with every launch).
stop_new() {
    pkill -TERM -f "$bundle/Contents/MacOS/" 2>/dev/null
    n=0
    while pgrep -f "$bundle/Contents/MacOS/" >/dev/null 2>&1 && [ $n -lt 25 ]; do
        sleep "$tick"; n=$((n + 1))
    done
    pkill -KILL -f "$bundle/Contents/MacOS/" 2>/dev/null
    return 0
}

# 1. The app closes first: its history is flushed and nothing runs from the bundle being moved.
n=0
while kill -0 "$app_pid" 2>/dev/null; do
    if [ $n -ge $((quit_wait * 5)) ]; then
        log "the app did not close in ${quit_wait} s; nothing was changed"
        write_status failed "Etude did not close, so nothing was changed"
        rm -rf "$staging"
        exit 1
    fi
    sleep "$tick"; n=$((n + 1))
done

# 2. The swap is one atomic exchange of two folders (renamex_np with RENAME_SWAP, done by the new
# version's own program in its --swap-bundle mode, which does nothing else), so the bundle's path
# holds an app at every moment. The program is copied out first: the rollback uses the same copy, and
# the bundle it came from moves.
update_dir=$(dirname "$status")
swap_tool="$update_dir/swap-tool"
swap() {
    if [ -n "${ETUDE_SWAP:-}" ]; then
        $ETUDE_SWAP "$1" "$2" >>"$log" 2>&1
    else
        "$swap_tool" --swap-bundle "$1" "$2" >>"$log" 2>&1
    fi
}

# Stops with the app at its path, old or new, saying why in the log and in the status the next start
# shows on the About page, and opens the app that is there.
stop_here() {
    log "$1"
    write_status failed "$2"
    rm -rf "$staging"
    launch "$bundle"
    exit 1
}

rm -f "$ready"
rm -rf "$previous"
if [ -z "${ETUDE_SWAP:-}" ]; then
    rm -f "$swap_tool"
    cp "$new_bundle/Contents/MacOS/etude" "$swap_tool" 2>>"$log" && chmod 0700 "$swap_tool" ||
        stop_here "could not copy the swap program out of the new bundle" \
            "the new version could not be prepared, so nothing was changed"
fi
swap "$new_bundle" "$bundle" ||
    stop_here "the swap of $new_bundle and $bundle failed" "the new version could not be put in place, so nothing was changed"
# The old bundle now sits where the new one was staged. It goes to its kept name; if that fails it
# stays where it is, still a whole bundle, and the rollback finds it there.
old_at=$new_bundle
if mv "$new_bundle" "$previous" 2>>"$log"; then
    old_at=$previous
    rmdir "$staging" 2>/dev/null
else
    log "the old version could not be moved to $previous; it stays at $old_at"
fi
write_status applying ""

# 3. The new version has wait_seconds to reach its first frame.
launch "$bundle"
n=0
good=0
while [ $n -lt $((wait_seconds * 5)) ]; do
    if [ -f "$ready" ] && grep -qx "version=$version" "$ready" 2>/dev/null; then
        good=1
        break
    fi
    sleep "$tick"; n=$((n + 1))
done

if [ $good -eq 1 ]; then
    # 4a. It runs. The previous version stays; older ones go.
    for old in "$parent"/.etude-*.previous; do
        [ -e "$old" ] || continue
        [ "$old" = "$old_at" ] || rm -rf "$old"
    done
    write_status ok ""
    log "version $version is running; $from kept as $old_at"
    exit 0
fi

# 4b. It did not: stop it, exchange the two back the same way, start the old one.
log "version $version did not reach its first frame in ${wait_seconds} s; putting $from back"
stop_new
if swap "$old_at" "$bundle"; then
    rm -rf "$old_at" 2>>"$log" || log "the new version's folder $old_at could not be removed"
    rmdir "$staging" 2>/dev/null
    write_status rolled-back "the new version did not reach its first frame, so version $from was put back"
    launch "$bundle"
    exit 1
fi
log "the old version could not be put back; version $version stays and $from is kept at $old_at"
write_status failed "version $version did not start and version $from could not be put back; it is kept at $old_at"
launch "$bundle"
exit 1
)SH";

} // namespace

std::string_view mac_relaunch_script() {
    return kScript;
}

} // namespace etude::app
