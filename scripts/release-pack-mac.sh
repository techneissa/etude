#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs a Release build for the Mac into the files of a GitHub release, the same steps as the release
# workflow's Mac job:
#
#   scripts/release-pack-mac.sh [--build-dir DIR] [--out DIR] [--no-build] [--jobs N]
#                               [--min-os VERSION] [--expect-version X.Y.Z] [--cmake-arg ARG]...
#
# It builds etude in Release for arm64 (unless --no-build), assembles etude.app (the program in
# Contents/MacOS, the piano samples and sheet music's fonts in Contents/Resources, the icon, an
# Info.plist with the version), signs the bundle ad hoc (`codesign -s -`: no developer account), checks
# it with `codesign --verify --deep --strict`, zips it with `ditto -c -k --keepParent`, and writes into
# --out (default build/release-mac):
#
#   etude-<version>-macos-arm64.zip         one top folder, etude.app
#   etude-<version>-macos-arm64.zip.sha256  the usual "<hash>  <file>" line
#   release-macos.json                        version, os, arch, min_os, file, sha256, size
#
# The version is the one the built app reports (`etude --version`); --expect-version X.Y.Z (a release
# tag without its "v") stops the run when they differ. --min-os is the oldest macOS the program runs
# on, by default the one the binary was built for (its LC_BUILD_VERSION), else this Mac's major version.
#
# It does not sign the zip: scripts/release-sign.sh does that with minisign, in the job that holds the
# key (`release-sign.sh DIR KEY --info release-macos.json`).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
SCRIPT_NAME=release-pack-mac.sh
DRY_RUN=0
# step, ok, change, warn, die and run, shared with the Pi scripts.
# shellcheck source=../deploy/pi/install-lib.sh
source "$REPO/deploy/pi/install-lib.sh"

BUILD_DIR="$REPO/build/release-pack-mac"
OUT_DIR="$REPO/build/release-mac"
DO_BUILD=1
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
MIN_OS=""
EXPECT_VERSION=""
CMAKE_ARGS=()

usage() {
    sed -n '3,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

need_value() { [[ -n "${2:-}" ]] || die "$1 needs a value"; }

while (($#)); do
    case "$1" in
    --build-dir) need_value "$@"; BUILD_DIR="$2"; shift ;;
    --out) need_value "$@"; OUT_DIR="$2"; shift ;;
    --no-build) DO_BUILD=0 ;;
    --jobs) need_value "$@"; JOBS="$2"; shift ;;
    --min-os) need_value "$@"; MIN_OS="$2"; shift ;;
    --expect-version) need_value "$@"; EXPECT_VERSION="$2"; shift ;;
    --cmake-arg) need_value "$@"; CMAKE_ARGS+=("$2"); shift ;;
    -h | --help) usage; exit 0 ;;
    *) die "unknown option $1 (try --help)" ;;
    esac
    shift
done

[[ "$(uname -s)" == Darwin ]] || die "this packs a Mac app: run it on a Mac"
for tool in codesign ditto shasum /usr/libexec/PlistBuddy; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is not available"
done

# put SRC DEST MODE: copies a file into the bundle (the Pi scripts' install_file uses GNU install).
put() {
    [[ -f "$1" ]] || die "missing $1: build first, or drop --no-build"
    run mkdir -p "$(dirname "$2")"
    run cp "$1" "$2"
    run chmod "$3" "$2"
}

# --- 1. build ----------------------------------------------------------------------------------

step "1. Build (Release, arm64)"
if ((DO_BUILD)); then
    change "configuring and building in $BUILD_DIR"
    cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DETUDE_BUILD_TESTS=OFF \
        -DCMAKE_OSX_ARCHITECTURES=arm64 ${CMAKE_ARGS[@]+"${CMAKE_ARGS[@]}"}
    cmake --build "$BUILD_DIR" --parallel "$JOBS" --target etude
else
    ok "skipped (--no-build)"
fi
PROGRAM="$BUILD_DIR/app/etude"
[[ -x "$PROGRAM" ]] || die "no $PROGRAM: build first, or drop --no-build"

VERSION_LINE="$("$PROGRAM" --version)"
VERSION="${VERSION_LINE#etude }"
[[ "$VERSION_LINE" == "etude $VERSION" && "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "'etude --version' printed '$VERSION_LINE', not 'etude X.Y.Z'"
if [[ -n "$EXPECT_VERSION" && "$EXPECT_VERSION" != "$VERSION" ]]; then
    die "this build is version $VERSION but $EXPECT_VERSION was expected: change the project version in CMakeLists.txt, or tag the right commit"
fi
ok "version $VERSION"

if [[ -z "$MIN_OS" ]]; then
    MIN_OS="$(xcrun vtool -show-build "$PROGRAM" 2>/dev/null | awk '$1 == "minos" { print $2; exit }' || true)"
fi
[[ -n "$MIN_OS" ]] || MIN_OS="$(sw_vers -productVersion | cut -d. -f1)"
[[ "$MIN_OS" =~ ^[0-9]+(\.[0-9]+)*$ ]] || die "the minimum macOS version is '$MIN_OS': pass --min-os (for example 14)"
ARCH=arm64
OS_NAME=macos
ok "target: macOS $MIN_OS and newer, $ARCH"

# --- 2. bundle ---------------------------------------------------------------------------------

NAME="etude-$VERSION-$OS_NAME-$ARCH.zip"
STAGE="$OUT_DIR/stage"
APP="$STAGE/etude.app"
step "2. Bundle etude.app"
run rm -rf "$STAGE" "$OUT_DIR/$NAME" "$OUT_DIR/$NAME.sha256" "$OUT_DIR/release-macos.json"
run mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
put "$PROGRAM" "$APP/Contents/MacOS/etude" 0755
# The piano samples and sheet music's fonts: SDL_GetBasePath is Contents/Resources in a bundle, where
# the app looks first (app/audio_out.cpp, app/sheet/sheet_job.cpp).
put "$BUILD_DIR/app/etude-piano.sf2" "$APP/Contents/Resources/etude-piano.sf2" 0644
[[ -d "$BUILD_DIR/app/verovio-data" ]] || die "no $BUILD_DIR/app/verovio-data: a release needs the fonts for sheet music"
run cp -R "$BUILD_DIR/app/verovio-data" "$APP/Contents/Resources/verovio-data"
put "$REPO/app/assets/icon/etude.icns" "$APP/Contents/Resources/etude.icns" 0644
put "$REPO/LICENSE" "$APP/Contents/Resources/LICENSE" 0644

PLIST="$APP/Contents/Info.plist"
run cp "$REPO/app/macos/Info.plist" "$PLIST"
chmod 0644 "$PLIST"
plist_add() { /usr/libexec/PlistBuddy -c "Add :$1 $2 $3" "$PLIST" >/dev/null; }
plist_add CFBundleShortVersionString string "$VERSION"
plist_add CFBundleVersion string "$VERSION"
plist_add CFBundleIconFile string etude
plist_add LSMinimumSystemVersion string "$MIN_OS"
plist_add NSHighResolutionCapable bool true
plist_add LSApplicationCategoryType string public.app-category.education
for needed in Contents/MacOS/etude Contents/Resources/etude-piano.sf2 Contents/Resources/verovio-data Contents/Info.plist; do
    [[ -e "$APP/$needed" ]] || die "the bundle has no $needed: a release needs it"
done

# --- 3. sign and check -------------------------------------------------------------------------

step "3. Sign ad hoc and check"
# No developer account: the signature only seals the bundle, so a changed file is noticed. Trust comes
# from the minisign signature of the zip, which the app checks before it opens anything.
run codesign --force --deep --sign - "$APP"
run codesign --verify --deep --strict "$APP"
ok "etude.app verifies"

# --- 4. archive --------------------------------------------------------------------------------

step "4. Archive"
mkdir -p "$OUT_DIR"
# ditto keeps the links and attributes the seal depends on, which zip(1) does not.
run ditto -c -k --keepParent "$APP" "$OUT_DIR/$NAME"
HASH="$(shasum -a 256 "$OUT_DIR/$NAME" | cut -d' ' -f1)"
SIZE="$(wc -c <"$OUT_DIR/$NAME" | tr -d ' ')"
printf '%s  %s\n' "$HASH" "$NAME" >"$OUT_DIR/$NAME.sha256"
printf '{\n  "version": "%s",\n  "os": "%s",\n  "arch": "%s",\n  "min_os": "%s",\n  "file": "%s",\n  "sha256": "%s",\n  "size": %s\n}\n' \
    "$VERSION" "$OS_NAME" "$ARCH" "$MIN_OS" "$NAME" "$HASH" "$SIZE" >"$OUT_DIR/release-macos.json"
rm -rf "$STAGE"
ok "$OUT_DIR/$NAME ($SIZE bytes, sha256 $HASH)"

step "Done"
ls -l "$OUT_DIR"
