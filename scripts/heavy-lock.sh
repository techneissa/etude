#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs a heavy check under one lock shared by every checkout on this machine, so two of them never
# run at once and slow each other into frame-time stalls. Only the run phase takes the lock:
# compiles stay parallel.
#
#   scripts/heavy-lock.sh CHECK COMMAND [ARGS...]
#
# CHECK names the check in the waiting line ("icon-audit", "bench", "ci-linux"). While
# another checkout's check holds the lock this prints one line naming it and its pid, then waits.
# The lock is /tmp/etude-heavy.lock (ETUDE_HEAVY_LOCK overrides); the holder writes who it is
# to <lock>.owner. macOS has lockf, Linux has flock; both release the lock when COMMAND ends, even
# when it is killed. A check started from inside another one (ETUDE_HEAVY_HELD is set) does not
# lock again.
set -uo pipefail

check=${1:?usage: heavy-lock.sh CHECK COMMAND [ARGS...]}
shift
[ $# -ge 1 ] || { echo "heavy-lock: no command given" >&2; exit 2; }

if [ -n "${ETUDE_HEAVY_HELD:-}" ]; then
    exec "$@"
fi

lock=${ETUDE_HEAVY_LOCK:-/tmp/etude-heavy.lock}
owner="$lock.owner"

# macOS has lockf (BSD), Linux has flock; chosen by system, not by what happens to be installed.
# -k keeps the lock file: without it lockf deletes the file on exit, and a waiter and a new
# holder could then lock two different files.
if [ "$(uname -s)" = Darwin ]; then
    use_lockf=1
else
    use_lockf=0
fi

# Take the lock without waiting; exit status 0 only when it was free.
try_lock() {
    if [ "$use_lockf" = 1 ]; then
        /usr/bin/lockf -k -s -t 0 "$lock" true
    else
        flock -n "$lock" true
    fi
}
# Run the rest of the arguments holding the lock; the holder's pid ($$ of the bash, then exec'd
# into the command) and the checkout go to the owner file first.
locked() {
    if [ "$use_lockf" = 1 ]; then
        /usr/bin/lockf -k "$lock" "$@"
    else
        flock "$lock" "$@"
    fi
}

if ! try_lock; then
    held_check=check held_pid=""
    for _ in 1 2 3 4 5; do
        if [ -r "$owner" ]; then
            read -r held_check held_pid _ < "$owner" || true
            if [ -n "$held_pid" ] && kill -0 "$held_pid" 2>/dev/null; then
                break
            fi
            held_pid=""
        fi
        sleep 0.3
    done
    echo "waiting for another worktree's ${held_check} (pid ${held_pid:-unknown})…" >&2
fi

export ETUDE_HEAVY_HELD=1 ETUDE_HEAVY_OWNER_FILE=$owner ETUDE_HEAVY_CHECK=$check
locked bash -c '
    printf "%s %s %s\n" "$ETUDE_HEAVY_CHECK" "$$" "$(basename "$PWD")" > "$ETUDE_HEAVY_OWNER_FILE"
    exec "$@"
' heavy-lock "$@"
