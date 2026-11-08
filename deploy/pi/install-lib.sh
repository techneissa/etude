# SPDX-License-Identifier: GPL-3.0-or-later
# Sourced by setup.sh (installs on the Pi) and scripts/release-pack.sh (installs into a release's
# staging folder), so both put the same files in the same places. It holds the printing helpers,
# install_file, and install_app_tree: the binaries and assets of a build under <prefix>.
# The caller sets SCRIPT_NAME (for die's messages) and DRY_RUN (1: print the commands, change
# nothing) before it calls anything here.

step() { printf '\n== %s\n' "$*"; }
ok() { printf '  [ok]     %s\n' "$*"; }
change() { printf '  [change] %s\n' "$*"; }
warn() { printf '  [warn]   %s\n' "$*" >&2; }
die() {
    printf '%s: %s\n' "${SCRIPT_NAME:-install}" "$*" >&2
    exit 1
}

# Prints the command and runs it, unless this is a dry run.
run() {
    printf '  +'
    printf ' %q' "$@"
    printf '\n'
    if ((${DRY_RUN:-0})); then
        return 0
    fi
    "$@"
}

CHANGED=0 # set by install_file: 1 when it wrote the destination

# install_file SRC DEST MODE: copies only when the content differs.
install_file() {
    local src="$1" dest="$2" mode="$3"
    CHANGED=0
    if [[ ! -e "$src" ]]; then
        if ((${DRY_RUN:-0})); then
            change "would install $dest (not built yet)"
            CHANGED=1
            return 0
        fi
        die "missing $src: build first, or drop --no-build"
    fi
    if [[ -f "$dest" ]] && cmp -s "$src" "$dest"; then
        ok "$dest unchanged"
        return 0
    fi
    change "installing $dest"
    CHANGED=1
    run install -D -m "$mode" "$src" "$dest"
}

# install_app_tree BUILD_DIR PREFIX: etude, display-spike and midi-ports into PREFIX/bin, the
# built-in piano samples and sheet music's fonts into PREFIX/share/etude.
install_app_tree() {
    local build="$1" prefix="$2" verovio_dest
    install_file "$build/app/etude" "$prefix/bin/etude" 0755
    install_file "$build/tools/display-spike" "$prefix/bin/display-spike" 0755
    install_file "$build/tools/midi-ports" "$prefix/bin/midi-ports" 0755
    # The built-in piano samples, made by the build from FluidR3_GM (see cmake/PianoAsset.cmake).
    if [[ -f "$build/app/etude-piano.sf2" ]] || ((${DRY_RUN:-0})); then
        install_file "$build/app/etude-piano.sf2" "$prefix/share/etude/etude-piano.sf2" 0644
    else
        warn "no built-in piano samples in the build; taps and typing will be silent"
    fi
    # Sheet music's fonts (Verovio's Bravura, its Leipzig fallback and the text metrics), which the
    # build copies beside the app.
    if [[ -d "$build/app/verovio-data" ]] || ((${DRY_RUN:-0})); then
        verovio_dest="$prefix/share/etude/verovio-data"
        if [[ -d "$verovio_dest" ]] && diff -rq "$build/app/verovio-data" "$verovio_dest" >/dev/null 2>&1; then
            ok "$verovio_dest is current"
        else
            change "installing $verovio_dest"
            run rm -rf "$verovio_dest"
            run mkdir -p "$prefix/share/etude"
            run cp -R "$build/app/verovio-data" "$verovio_dest"
        fi
    else
        warn "no verovio-data in the build; sheet music will not show"
    fi
}

# check_app_home HOME USER: the app user's home as the helper's sandbox may write it (the update units'
# ReadWritePaths, deploy/pi/etude-update-sandbox.conf). The app owns what is inside its home and can
# turn it into a link, but it cannot swap the home itself while it is the user's own folder, below
# folders only root can change, and not a system folder the sandbox must never open for writing. Prints
# the home with every link resolved and returns 0, or says why on stderr and returns 1.
check_app_home() {
    local home="$1" user="$2" resolved owner parent uid mode
    resolved="$(realpath -e -- "$home" 2>/dev/null)" || {
        echo "the home folder $home of $user does not exist" >&2
        return 1
    }
    case "$resolved" in
    / | /usr | /usr/* | /var | /etc | /etc/* | /root | /root/*)
        echo "the home folder of $user is $resolved ($home), a system folder: the update helper's sandbox may not write it" >&2
        return 1
        ;;
    esac
    owner="$(stat -c %U -- "$resolved" 2>/dev/null)"
    if [[ "$owner" != "$user" ]]; then
        echo "the home folder $resolved of $user belongs to ${owner:-nobody}, not to $user" >&2
        return 1
    fi
    parent="$resolved"
    while [[ "$parent" != / ]]; do
        parent="$(dirname -- "$parent")"
        read -r uid mode < <(stat -c '%u %a' -- "$parent" 2>/dev/null || echo "bad bad")
        if [[ "$uid" != 0 ]] || ! [[ "$mode" =~ ^[0-7]+$ ]] || (((8#$mode & 8#022) != 0)); then
            echo "the folder $parent above the home $resolved of $user must be root's and closed to writes by group and others (it is $uid:$mode): whoever can rename a folder above the home can point the update helper's sandbox somewhere else" >&2
            return 1
        fi
    done
    printf '%s\n' "$resolved"
}
