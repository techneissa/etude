#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The batch gate (`just gate`): the checks that prove what a batch changed on main, in order: the
# icon audit of the scenarios named with --audit (none named: skipped), the shot determinism check
# (`just shots-check`) when --shots is given (default: skipped), ci-linux when --ci-linux is given
# (default: skipped) and the publish leak guard (`just publish-check`), always. Everything that will run is built first, in parallel with whatever else runs; then the
# checks run one after the other holding the heavy-check lock (scripts/heavy-lock.sh) the whole
# time, so no other checkout's audit or bench slips in between. All that were asked for run
# even when one fails. Each prints nothing while it runs; the gate prints one line per check (PASS
# or FAIL, its time, its own summary line) and one SKIP line for each it was not asked to run, and
# keeps the full output in build/gate/<check>.log, with the last lines of a failed one printed
# under its line. Exit status 1 when any check failed.
#
#   scripts/gate.sh [--audit "SCENARIO SCENARIO ..."] [--shots] [--ci-linux]
#     --audit  the icon audit's scenarios (spaces or commas between names), at every size and
#              scale: the screens the batch changed. A name that is no scenario, or a scenario that
#              measured nothing, fails the audit.
#     --shots  also run the shot determinism check (about 2.5 minutes)
#     --ci-linux  also run the Linux CI job: when the batch changed CMake, a dependency or platform code
set -uo pipefail
cd "$(dirname "$0")/.."

AUDIT=""
SHOTS=0
CI_LINUX=0
ARGS=("$@")
while [ $# -gt 0 ]; do
    case "$1" in
        --audit)
            [ $# -ge 2 ] && [ -n "${2// /}" ] || { echo "gate: --audit wants the scenarios to run, e.g. --audit \"page-view dock\"" >&2; exit 2; }
            AUDIT=$2; shift 2 ;;
        --shots) SHOTS=1; shift ;;
        --ci-linux) CI_LINUX=1; shift ;;
        *) echo "usage: scripts/gate.sh [--audit \"SCENARIO ...\"] [--shots] [--ci-linux]  (unknown option: $1)" >&2; exit 2 ;;
    esac
done

if [ -z "${ETUDE_HEAVY_HELD:-}" ]; then
    scripts/ui-run.sh cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_COMPILE_WARNING_AS_ERROR=ON || exit 1
    scripts/ui-run.sh cmake --build build || exit 1
    exec scripts/heavy-lock.sh gate scripts/gate.sh ${ARGS[@]+"${ARGS[@]}"}
fi

logs=build/gate
mkdir -p "$logs"
failed=0
now() { perl -MTime::HiRes=time -e 'printf "%.0f", time'; }

# gate_check NAME SUMMARY_PATTERN COMMAND...: runs the command with its output in the log, prints
# the last two lines of the log that match SUMMARY_PATTERN (the first line of the log when nothing
# matches) as the check's summary, followed by GATE_NOTE when it is set.
gate_check() {
    local name=$1 pattern=$2 began status took line log=$logs/$1.log
    shift 2
    began=$(now)
    "$@" > "$log" 2>&1
    status=$?
    took=$(($(now) - began))
    line=$(grep -E "$pattern" "$log" | tail -2 | paste -sd'/' - | sed 's|/|  /  |')
    [ -n "$line" ] || line=$(head -1 "$log")
    if [ "$status" -eq 0 ]; then
        printf 'gate %-13s PASS %4d s  %s%s\n' "$name" "$took" "$line" "${GATE_NOTE:+  ($GATE_NOTE)}"
    else
        failed=$((failed + 1))
        printf 'gate %-13s FAIL %4d s  exit %d, full output in %s\n' "$name" "$took" "$status" "$log"
        tail -8 "$log" | sed 's/^/    /'
    fi
}

# gate_skip NAME WHY: a check this run was not asked for.
skipped=()
gate_skip() {
    skipped+=("$1")
    printf 'gate %-13s SKIP  %s\n' "$1" "$2"
}

ran=()
if [ -n "$AUDIT" ]; then
    ran+=(icon-audit)
    gate_check icon-audit '^icon audit(:| timing)' scripts/icon-audit.sh --only "$AUDIT"
else
    gate_skip icon-audit "no --audit (no screen changed)"
fi
if [ "$SHOTS" = 1 ]; then
    ran+=(shots)
    gate_check shots '^shots:' scripts/shot-determinism-check.sh
else
    gate_skip shots "no --shots (no drawing code changed)"
fi
if [ "$CI_LINUX" = 1 ]; then
    ran+=(ci-linux)
    # ctest runs the whole unit suite here; a PR's `just test` runs only the tests its edit touches.
    GATE_NOTE="the full unit suite" gate_check ci-linux 'tests passed|tests failed' scripts/ci-linux.sh
else
    gate_skip ci-linux "no --ci-linux (no CMake, dependency or platform change)"
fi
ran+=(publish-check)
gate_check publish-check '.' just publish-check
skipped_text=""
[ "${#skipped[@]}" -eq 0 ] || skipped_text="; skipped: ${skipped[*]}"
if [ "$failed" -gt 0 ]; then
    echo "gate FAILED: $failed of ${#ran[@]} checks$skipped_text"
    exit 1
fi
echo "gate PASSED: ${ran[*]}$skipped_text"
