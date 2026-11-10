#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run a command through scripts/lib/cli-ui.sh (progress spinner + compact diagnostics).
# Usage: scripts/ui-run.sh [--dir DIR] cmd [args...]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
export UI_REPO_ROOT="$ROOT"
# shellcheck source=lib/cli-ui.sh
source "$ROOT/scripts/lib/cli-ui.sh"

if [[ "${1:-}" == "--dir" ]]; then
    shift
    [[ $# -ge 1 ]] || { ui_err "ui-run --dir needs a directory"; exit 2; }
    cd "$1"
    shift
fi

[[ $# -ge 1 ]] || { ui_err "ui-run needs a command"; exit 2; }
# Run from the real folder: CMake records the path it is started in, and ccache shares objects
# between checkouts only when that path has no symlink in it.
cd -P .
# A build folder first configured through a symlink keeps the symlinked path in its cache, and
# CMake would never change it: drop that cache file once so the next configure records the real path.
here="$PWD"
prev=""
for arg in "$@"; do
    if [[ "$prev" == "-B" && -f "$arg/CMakeCache.txt" ]]; then
        home="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$arg/CMakeCache.txt")"
        if [[ -n "$home" && "$home" != "$here" ]]; then
            ui_info "removing $arg/CMakeCache.txt: it was configured from $home, not the real path $here"
            rm -f "$arg/CMakeCache.txt"
        fi
    fi
    prev="$arg"
done
ui_run "$@"
