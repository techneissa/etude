#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs a Release build for the Pi into the files of a GitHub release:
#
#   scripts/release-pack.sh [--build-dir DIR] [--out DIR] [--no-build] [--jobs N]
#                           [--os NAME] [--min-os VERSION] [--arch ARCH]
#                           [--expect-version X.Y.Z] [--cmake-arg ARG]...
#
# It builds etude, display-spike and midi-ports in Release (unless --no-build), installs them into
# a staging folder laid out like /opt/etude (the same install step as deploy/pi/setup.sh: bin/,
# share/etude/ with the piano samples and sheet music's fonts) with the systemd units and the
# licence, and writes into --out (default build/release):
#
#   etude-<version>-<os>-<arch>.tar.gz   one top folder, etude-<version>/, holding the staging tree
#   etude-<version>-<os>-<arch>.tar.gz.sha256   the usual "<hash>  <file>" line
#   release.json                            version, os, arch, min_os, file, sha256, size
#
# The version is the one the built app reports (`etude --version`, the project version in
# CMakeLists.txt); --expect-version X.Y.Z (a release tag without its "v") stops the run when they
# differ. --os and --min-os default to the build machine's /etc/os-release (VERSION_CODENAME and
# VERSION_ID), --arch to its dpkg architecture: a release built on Pi OS trixie says "trixie"
# and "13", and runs on that system and newer ones.
#
# It does not sign: scripts/release-sign.sh does that, in a job of its own that runs none of the
# built code. A version is plain X.Y.Z (the project version carries no prerelease part).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
SCRIPT_NAME=release-pack.sh
DRY_RUN=0
# step, ok, change, warn, die, run, install_file and install_app_tree, shared with setup.sh.
# shellcheck source=../deploy/pi/install-lib.sh
source "$REPO/deploy/pi/install-lib.sh"

BUILD_DIR="$REPO/build/release-pack"
OUT_DIR="$REPO/build/release"
DO_BUILD=1
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
OS_NAME=""
MIN_OS=""
ARCH=""
EXPECT_VERSION=""
CMAKE_ARGS=()

usage() {
    sed -n '3,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

need_value() { [[ -n "${2:-}" ]] || die "$1 needs a value"; }

while (($#)); do
    case "$1" in
    --build-dir) need_value "$@"; BUILD_DIR="$2"; shift ;;
    --out) need_value "$@"; OUT_DIR="$2"; shift ;;
    --no-build) DO_BUILD=0 ;;
    --jobs) need_value "$@"; JOBS="$2"; shift ;;
    --os) need_value "$@"; OS_NAME="$2"; shift ;;
    --min-os) need_value "$@"; MIN_OS="$2"; shift ;;
    --arch) need_value "$@"; ARCH="$2"; shift ;;
    --expect-version) need_value "$@"; EXPECT_VERSION="$2"; shift ;;
    --cmake-arg) need_value "$@"; CMAKE_ARGS+=("$2"); shift ;;
    -h | --help) usage; exit 0 ;;
    *) die "unknown option $1 (try --help)" ;;
    esac
    shift
done

# A value of /etc/os-release, or nothing.
os_release() {
    [[ -r /etc/os-release ]] || return 0
    (. /etc/os-release && printf '%s' "${!1:-}")
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

step "Checking the inputs"
[[ -n "$OS_NAME" ]] || OS_NAME="$(os_release VERSION_CODENAME)"
[[ -n "$MIN_OS" ]] || MIN_OS="$(os_release VERSION_ID)"
if [[ -z "$ARCH" ]]; then
    ARCH="$(dpkg --print-architecture 2>/dev/null || true)"
    case "${ARCH:-$(uname -m)}" in aarch64 | arm64) ARCH=arm64 ;; esac
fi
[[ "$OS_NAME" =~ ^[a-z0-9_]+$ ]] || die "the target system's name is '$OS_NAME': pass --os (for example trixie)"
[[ "$MIN_OS" =~ ^[0-9]+(\.[0-9]+)*$ ]] || die "the minimum system version is '$MIN_OS': pass --min-os (for example 13)"
[[ "$ARCH" =~ ^[a-z0-9_]+$ ]] || die "the architecture is '$ARCH': pass --arch (for example arm64)"
ok "target: $OS_NAME (version $MIN_OS and newer), $ARCH"

# --- 1. build ----------------------------------------------------------------------------------

step "1. Build (Release)"
if ((DO_BUILD)); then
    change "configuring and building in $BUILD_DIR"
    cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DETUDE_BUILD_TESTS=OFF \
        ${CMAKE_ARGS[@]+"${CMAKE_ARGS[@]}"}
    cmake --build "$BUILD_DIR" --parallel "$JOBS" --target etude display-spike midi-ports
else
    ok "skipped (--no-build)"
fi
[[ -x "$BUILD_DIR/app/etude" ]] || die "no $BUILD_DIR/app/etude: build first, or drop --no-build"

VERSION_LINE="$("$BUILD_DIR/app/etude" --version)"
VERSION="${VERSION_LINE#etude }"
[[ "$VERSION_LINE" == "etude $VERSION" && "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "'etude --version' printed '$VERSION_LINE', not 'etude X.Y.Z'"
if [[ -n "$EXPECT_VERSION" && "$EXPECT_VERSION" != "$VERSION" ]]; then
    die "this build is version $VERSION but $EXPECT_VERSION was expected: change the project version in CMakeLists.txt, or tag the right commit"
fi
ok "version $VERSION"

# --- 2. stage ----------------------------------------------------------------------------------

NAME="etude-$VERSION-$OS_NAME-$ARCH.tar.gz"
TOP="etude-$VERSION"
STAGE="$OUT_DIR/stage"
step "2. Staging $TOP"
run rm -rf "$STAGE" "$OUT_DIR/$NAME" "$OUT_DIR/$NAME.sha256" "$OUT_DIR/release.json"
run mkdir -p "$STAGE/$TOP"
install_app_tree "$BUILD_DIR" "$STAGE/$TOP"
for unit in etude.service etude-labwc.service etude-crash-screen.service etude-labwc-crash-screen.service pam-etude; do
    install_file "$REPO/deploy/pi/$unit" "$STAGE/$TOP/systemd/$unit" 0644
done
install_file "$REPO/deploy/pi/etude-labwc-session" "$STAGE/$TOP/systemd/etude-labwc-session" 0755
install_file "$REPO/LICENSE" "$STAGE/$TOP/LICENSE" 0644
# A release without its samples or fonts would install a silent or text-less app: stop here.
for needed in share/etude/etude-piano.sf2 share/etude/verovio-data bin/etude; do
    [[ -e "$STAGE/$TOP/$needed" ]] || die "the build has no $needed: a release needs it"
done

# --- 3. archive --------------------------------------------------------------------------------

step "3. Archive"
TAR_FLAGS=()
if tar --version 2>/dev/null | grep -q GNU; then # the same bytes for the same files
    TAR_FLAGS=(--sort=name --owner=0 --group=0 --numeric-owner --mtime="@${SOURCE_DATE_EPOCH:-0}")
else
    warn "not GNU tar: the archive is not byte-for-byte reproducible"
fi
(cd "$STAGE" && tar ${TAR_FLAGS[@]+"${TAR_FLAGS[@]}"} -cf - "$TOP") | gzip -n -9 >"$OUT_DIR/$NAME"
HASH="$(sha256_of "$OUT_DIR/$NAME")"
SIZE="$(wc -c <"$OUT_DIR/$NAME" | tr -d ' ')"
printf '%s  %s\n' "$HASH" "$NAME" >"$OUT_DIR/$NAME.sha256"
printf '{\n  "version": "%s",\n  "os": "%s",\n  "arch": "%s",\n  "min_os": "%s",\n  "file": "%s",\n  "sha256": "%s",\n  "size": %s\n}\n' \
    "$VERSION" "$OS_NAME" "$ARCH" "$MIN_OS" "$NAME" "$HASH" "$SIZE" >"$OUT_DIR/release.json"
rm -rf "$STAGE"
ok "$OUT_DIR/$NAME ($SIZE bytes, sha256 $HASH)"

step "Done"
ls -l "$OUT_DIR"
