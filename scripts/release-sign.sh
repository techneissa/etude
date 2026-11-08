#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Signs the file that scripts/release-pack.sh (the Pi's tarball) or scripts/release-pack-mac.sh (the
# Mac's zip) made, with minisign.
#
#   MINISIGN_PASSWORD=... scripts/release-sign.sh RELEASE_DIR SECRET_KEY_FILE [--info release-macos.json]
#
# RELEASE_DIR holds the file, its .sha256 and the info file that names it: release.json for the Pi,
# release-macos.json for the Mac (--info; default release.json). This script runs sha256sum and
# minisign and nothing the build produced, so it can hold the signing key while the build job, which
# compiles downloaded code, never sees it. It checks the file against its .sha256 and against the info
# file, signs with the trusted comment "etude <version> <os> <arch>" (the text the app
# expects, core/include/etude/release_info.hpp), writes <file>.minisig beside it, and checks
# that signature against the public key built into the app (core/src/update_key.cpp), so a release
# cannot go out signed with another key, or while that key is still the placeholder. The password,
# if the key has one, comes from MINISIGN_PASSWORD and goes to minisign on its standard input.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEY_SOURCE="$SCRIPT_DIR/../core/src/update_key.cpp"
KEY_PLACEHOLDER="MINISIGN-PUBLIC-KEY-NOT-SET"

die() {
    printf 'release-sign.sh: %s\n' "$*" >&2
    exit 1
}

INFO=release.json
if [[ $# -eq 4 && "$3" == --info ]]; then
    INFO="$4"
    set -- "$1" "$2"
fi
[[ $# -eq 2 && "$INFO" =~ ^release[a-z0-9.-]*\.json$ ]] ||
    die "usage: release-sign.sh RELEASE_DIR SECRET_KEY_FILE [--info release-macos.json]"
DIR="$1"
KEY="$2"
[[ -d "$DIR" ]] || die "no folder $DIR"
[[ -f "$KEY" ]] || die "no secret key file $KEY"
command -v minisign >/dev/null 2>&1 || die "minisign is not installed (apt-get install minisign)"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is not installed"

# The quoted text on the kUpdatePublicKey line of the source.
PUBLIC_KEY="$(sed -n 's/^constexpr std::string_view kUpdatePublicKey = "\([^"]*\)".*/\1/p' "$KEY_SOURCE")"
[[ -n "$PUBLIC_KEY" ]] || die "no public key found in $KEY_SOURCE"
[[ "$PUBLIC_KEY" != "$KEY_PLACEHOLDER" ]] ||
    die "the update key in core/src/update_key.cpp is still the placeholder: put the public key of the signing key there first"

# The info file names the one file to sign; its .sha256 must be beside it.
[[ -f "$DIR/$INFO" ]] || die "$DIR needs $INFO"
# $INFO is written one "key": "value" to a line by the pack scripts.
json_value() { sed -n "s/^  \"$1\": \"\{0,1\}\([^\",]*\)\"\{0,1\},\{0,1\}\$/\\1/p" "$DIR/$INFO"; }
NAME="$(json_value file)"
VERSION="$(json_value version)"
OS_NAME="$(json_value os)"
ARCH="$(json_value arch)"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && "$OS_NAME" =~ ^[a-z0-9_]+$ && "$ARCH" =~ ^[a-z0-9_]+$ ]] ||
    die "$INFO has no plain version, os and arch"
# The Mac's file is a zip of the app bundle, every other system's a tarball.
EXT=tar.gz
[[ "$OS_NAME" != macos ]] || EXT=zip
[[ "$NAME" == "etude-$VERSION-$OS_NAME-$ARCH.$EXT" ]] || die "the file name in $INFO is not etude-$VERSION-$OS_NAME-$ARCH.$EXT"
# One file of this kind in the folder, and it is the one the info file names: a second tarball (or
# zip) there would be signed by nobody or taken for this one.
shopt -s nullglob
files=("$DIR"/etude-*."$EXT")
[[ ${#files[@]} -eq 1 ]] || die "expected one etude-*.$EXT in $DIR, found ${#files[@]}"
TARBALL="$DIR/$NAME"
[[ -f "$TARBALL" && -f "$DIR/$NAME.sha256" ]] || die "$DIR needs $NAME and $NAME.sha256"

(cd "$DIR" && sha256sum --check --strict --status "$NAME.sha256") ||
    die "$NAME does not match its .sha256"
HASH="$(cut -d' ' -f1 "$DIR/$NAME.sha256")"
[[ "$(json_value sha256)" == "$HASH" ]] || die "$INFO names another SHA-256 than $NAME.sha256"

COMMENT="etude $VERSION $OS_NAME $ARCH"
rm -f "$TARBALL.minisig"
printf '%s\n' "${MINISIGN_PASSWORD:-}" |
    minisign -S -s "$KEY" -m "$TARBALL" -x "$TARBALL.minisig" -t "$COMMENT" >/dev/null
if ! minisign -V -P "$PUBLIC_KEY" -m "$TARBALL" -x "$TARBALL.minisig" >/dev/null; then
    rm -f "$TARBALL.minisig"
    die "the signature does not verify with the public key in core/src/update_key.cpp: the secret key is another one"
fi
# The installer checks the signature from a hash of the tarball, so it has to be the prehashed kind
# (minisign's default, "ED", whose base64 starts "RU"); the old kind ("Ed", `minisign -l`, "RW") signs
# the bytes and is refused by the helper.
[[ "$(sed -n 2p "$TARBALL.minisig" | cut -c1-2)" == "RU" ]] || {
    rm -f "$TARBALL.minisig"
    die "the signature is not the prehashed kind (do not pass -l to minisign): the update helper refuses it"
}
printf '  [ok]     %s.minisig verifies with the app'"'"'s public key (trusted comment: %s)\n' "$NAME" "$COMMENT"
