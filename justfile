set shell := ["bash", "-euo", "pipefail", "-c"]

build_dir := "build"

# List recipes
default:
    @just --list

# Configure (first run fetches dependencies) and build everything
build:
    @scripts/ui-run.sh cmake -S . -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
    @scripts/ui-run.sh cmake --build {{build_dir}}

# Build and launch the app
run: build
    {{build_dir}}/app/etude

# Build, then run the unit-test files the diff against origin/main touches (scripts/test-scope.py: name matches, direct includers, and the files that name a changed path; `--since REF` diffs against REF instead, so a fix round tests its own edits) and the settings check when settings code changed, each waiting for any other checkout's heavy check; the size checks run every time. The scope falls back to everything when it cannot be worked out (no origin/main, a CMake change beyond file names in a list, or a shared header changed). `just test --all` runs the full suite (the batch gate's ci-linux runs it too)
[positional-arguments]
test *args: build
    #!/usr/bin/env bash
    set -euo pipefail
    full=0
    since=()
    while [ $# -gt 0 ]; do
        a=$1; shift
        case "$a" in
            --since) [ $# -gt 0 ] || { echo "just test: --since needs a ref" >&2; exit 2; }; since=(--since "$1"); shift ;;
            --all) full=1 ;;
            *) echo "just test: unknown argument '$a' (--all runs the full suite, --since REF scopes to the diff against REF)" >&2; exit 2 ;;
        esac
    done
    scripts/ui-run.sh python3 -I scripts/size-check.py
    # Exit 3: a test file is not in tests/CMakeLists.txt (it names it on stderr); anything else
    # that goes wrong runs everything.
    rc=0
    scope=$(python3 -I scripts/test-scope.py ${since[@]+"${since[@]}"}) || rc=$?
    [ "$rc" != 3 ] || exit 3
    [ "$rc" = 0 ] || scope=$'all\n\nsettings\nall test files: test-scope.py failed'
    mode=$(sed -n 1p <<<"$scope"); filter=$(sed -n 2p <<<"$scope")
    settings=$(sed -n 3p <<<"$scope"); why=$(sed -n 4p <<<"$scope")
    if [ "$full" = 1 ]; then
        mode=all settings=settings why="all test files: --all"
    fi
    if [ "$mode" = all ]; then
        cases=$({{build_dir}}/tests/etude_tests --count | sed -n 's/.*filters: //p')
        echo "test: $why ($cases test cases)"
        scripts/heavy-lock.sh test scripts/ui-run.sh {{build_dir}}/tests/etude_tests
    else
        cases=$({{build_dir}}/tests/etude_tests --count --source-file="$filter" | sed -n 's/.*filters: //p')
        echo "test: $why ($cases test cases)"
        scripts/heavy-lock.sh test scripts/ui-run.sh {{build_dir}}/tests/etude_tests --source-file="$filter"
    fi
    if [ "$settings" = settings ]; then
        scripts/heavy-lock.sh test scripts/ui-run.sh scripts/settings-check.sh
    fi

# The full unit suite and the settings check (the same as `just test --all`)
test-all: (test "--all")

# Export the committed tree the way a public publish would and fail on any leak-guard hit (internal wording, personal paths, secrets, CRLF) after the guard's own self-test; pushes nothing. About 15 s, so it runs in the batch gate, not in `just test` (arg: a git ref, default HEAD)
publish-check ref="HEAD":
    #!/usr/bin/env bash
    set -euo pipefail
    .publish/publish.sh --self-test >/dev/null
    out="$(mktemp -d "${TMPDIR:-/tmp}/etude-publish-check.XXXXXX")"
    trap 'rm -rf "$out"' EXIT
    .publish/publish.sh --dry-run --ref {{ref}} --out "$out" >/dev/null

# Render every note style and `--shot-pattern all` headless twice and fail when two runs give different PNG bytes (about 2.5 min, so it runs in the batch gate only with `just gate --shots`, not in `just test`)
shots-check: build
    @scripts/ui-run.sh scripts/shot-determinism-check.sh

# The Linux CI job on this machine (Docker, Ubuntu 24.04 arm64, GCC 13, warnings as errors): no GitHub minutes; waits for any other checkout's heavy check
ci-linux:
    scripts/ci-linux.sh

# The batch gate on main: build, then under the heavy-check lock the icon audit of the scenarios in --audit "NAMES" (none: skipped), the shot check with --shots (default: skipped), ci-linux with --ci-linux (default: skipped) and publish-check; one summary line per check, a SKIP line for each skipped (logs in build/gate/)
[positional-arguments]
gate *args:
    scripts/gate.sh "$@"

# Where every icon sits in its button, measured from pixels over sizes, UI scales and screens, headless: no window opens; waits for any other checkout's heavy check (args pass through: --only dock --sizes 800x480)
[positional-arguments]
icon-audit *args: build
    scripts/icon-audit.sh "$@"

# Launch the app and print note-on to frame latency every 5 s
latency-log: build
    {{build_dir}}/app/etude --latency-log

# Play a scale, a pedalled arpeggio and a chord on the built-in piano, no window (args pass through)
synth-test *args: build
    {{build_dir}}/app/etude --synth-test {{args}}

# Extract the piano presets from FluidR3_GM into build/etude-piano.sf2 (downloads 148 MB once)
piano-sf2 *args:
    python3 -I tools/extract_piano_sf2.py --out {{build_dir}}/etude-piano.sf2 {{args}}

# Release build, then frame times of all eight note styles at 1080p with 200 notes (args pass through): a hand-run tool that prints numbers and never fails on a bound
bench *args:
    #!/usr/bin/env bash
    set -euo pipefail
    scripts/ui-run.sh cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    scripts/ui-run.sh cmake --build build-release --target etude
    scripts/heavy-lock.sh bench build-release/app/etude --bench-all --headless {{args}}

# Release build, then the library search timed a keystroke at a time over 5,000 made-up songs
# (fails at 5 ms or more a keystroke, or any allocation; args pass through: --songs N)
bench-search *args:
    @scripts/ui-run.sh cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    @scripts/ui-run.sh cmake --build build-release --target etude
    scripts/heavy-lock.sh bench-search build-release/app/etude --bench-search {{args}}

# Release build, then the pitch detector over the synthetic piano set (args pass through: --help)
pitch-eval *args:
    @scripts/ui-run.sh cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    @scripts/ui-run.sh cmake --build build-release --target pitch-eval
    build-release/tools/pitch-eval {{args}}

# Release build, then score the hand split against the reference hands of the song files under the given paths (args pass through: tests/data --detail satie, --keep-parts, --time)
hand-eval *args:
    @scripts/ui-run.sh cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    @scripts/ui-run.sh cmake --build build-release --target hand-eval
    build-release/tools/hand-eval {{args}}

# List connected MIDI ports and matching keyboard profiles
midi-ports: build
    {{build_dir}}/tools/midi-ports

# Play a test pattern into a virtual MIDI port (run the app first)
midi-demo *args: build
    {{build_dir}}/tools/midi-demo {{args}}

# Print live MIDI events and key changes without a window
midi-log: build
    {{build_dir}}/app/etude --midi-log

# Display, renderer and touch spike; args pass through (just display-spike --list)
display-spike *args: build
    {{build_dir}}/tools/display-spike {{args}}

# The root side of app updates in a Debian 13 container (Docker): the helper built there with a test key, signed tarballs, a stand-in service manager and app; installs, updates, rollbacks, reboots and power cuts, and the attacks and failures it must survive
update-helper-test:
    scripts/update-helper-test.sh

# Raspberry Pi OS only: install packages, build in Release, install binaries and systemd units
pi-setup *args:
    @[ "$(uname -s)" = Linux ] || { echo "pi-setup only runs on Linux (Raspberry Pi OS); this is $(uname -s). Run it on the Pi." >&2; exit 1; }
    sudo deploy/pi/setup.sh {{args}}

# Rebuild the app icons from their SVG sources: `just icons svg`, `just icons render` (needs inkscape), `just icons sheet --out FILE.png`, or all (the default)
icons *args:
    python3 scripts/make-icons.py {{args}}

# Format all C++ sources with clang-format
format:
    @find core app tools tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Delete the build directory
clean:
    rm -rf {{build_dir}}
