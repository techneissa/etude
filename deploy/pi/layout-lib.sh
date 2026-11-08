# SPDX-License-Identifier: GPL-3.0-or-later
# Sourced by setup.sh (and by scripts/update-helper-test.sh): how /opt/etude is laid out for
# updates. Every version of the app lives in <prefix>/versions/<X.Y.Z>/ (bin/, share/etude/), and
# <prefix>/current is a relative link to the one that runs, so the units start
# <prefix>/current/bin/etude and an update only has to move the link. An older install, with
# bin/ and share/ straight under <prefix>, is moved into versions/ by migrate_old_layout: through a
# temporary folder and one rename, so a run that is cut short is finished by the next one.
# It needs the helpers of install-lib.sh (ok, change, warn, run) and its callers' DRY_RUN.

VERSION_PATTERN='^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'

# project_version REPO: the version in CMakeLists.txt, the one a build reports.
project_version() {
    sed -n 's/^project(etude VERSION \([0-9][0-9.]*\).*/\1/p' "$1/CMakeLists.txt" | head -n 1
}

# point_current PREFIX VERSION: PREFIX/current -> versions/VERSION, replaced by one rename.
point_current() {
    local prefix="$1" version="$2"
    if [[ "$(readlink "$prefix/current" 2>/dev/null || true)" == "versions/$version" ]]; then
        ok "$prefix/current already points at versions/$version"
        return 0
    fi
    change "pointing $prefix/current at versions/$version"
    run ln -sfn "versions/$version" "$prefix/.current.new"
    run mv -T -f "$prefix/.current.new" "$prefix/current"
}

# old_version BIN USER: the version an old binary reports, asked as USER (the app's own user, never
# root: an old install is not code to run with root's rights), or 0.0.0 when it cannot be asked or
# does not answer in the X.Y.Z form.
old_version() {
    local bin="$1" who="$2" said=""
    if ((EUID != 0)); then
        said="$(timeout 20 "$bin" --version 2>/dev/null || true)"
    elif id "$who" >/dev/null 2>&1; then
        said="$(runuser -u "$who" -- timeout 20 "$bin" --version 2>/dev/null || true)"
    fi
    said="$(printf '%s\n' "$said" | sed -n 's/^etude //p' | head -n 1)"
    [[ "$said" =~ $VERSION_PATTERN ]] || said=0.0.0
    printf '%s' "$said"
}

# migrate_old_layout PREFIX [USER]: an install with PREFIX/bin/etude and no PREFIX/current moves
# bin/ and share/ into PREFIX/versions/<the version it reports>/ and points current at it. The old
# binary is asked for its version as USER (default etude). The parts are renamed into a temporary
# folder inside versions/ and that folder is renamed to the version in one step, with the version
# written beside them first, so a run that stops anywhere is finished by running it again.
migrate_old_layout() {
    local prefix="$1" who="${2:-etude}" tmp old part dest
    tmp="$prefix/versions/.migrate.new"
    if [[ -L "$prefix/current" || -e "$prefix/current" ]]; then
        ok "$prefix/current exists: nothing to move"
        return 0
    fi
    if [[ ! -e "$prefix/bin/etude" && ! -e "$tmp/bin/etude" ]]; then
        ok "no old install under $prefix"
        return 0
    fi
    run mkdir -p "$tmp"
    if [[ -f "$tmp/.version" ]]; then
        old="$(cat "$tmp/.version")"
    else
        old="$(old_version "$prefix/bin/etude" "$who")"
        change "the old install reports version $old"
        if ((!${DRY_RUN:-0})); then
            printf '%s\n' "$old" >"$tmp/.version"
        fi
    fi
    [[ "$old" =~ $VERSION_PATTERN ]] || old=0.0.0
    dest="$prefix/versions/$old"
    [[ ! -e "$dest" ]] || die "$dest already exists: not moving the old install over it"
    change "moving the old install under $prefix into versions/$old"
    for part in bin share; do
        if [[ -e "$prefix/$part" ]]; then
            run mv "$prefix/$part" "$tmp/$part"
        fi
    done
    run mv -T "$tmp" "$dest"
    run rm -f "$dest/.version"
    point_current "$prefix" "$old"
}

# remove_safe_mode_leftovers UNIT_DIR DATA_DIR: what an install from before the crash screen still has
# of safe mode: its two units (a crash now starts the crash screen, and nothing starts those) and the
# `safe-mode` marker file it left in the app's data folder. Sets REMOVED to 1 when it removed a unit, so
# the caller reloads systemd. The folder was checked by setup.sh (no link of the app user's in its path).
REMOVED=0
remove_safe_mode_leftovers() {
    local unit_dir="$1" data_dir="$2" unit
    REMOVED=0
    for unit in etude-safe-mode.service etude-labwc-safe-mode.service; do
        if [[ -e "$unit_dir/$unit" || -L "$unit_dir/$unit" || -d "$unit_dir/$unit.d" ]]; then
            change "removing the old $unit (a crash starts the crash screen now)"
            run systemctl disable --now "$unit" 2>/dev/null || true
            run rm -rf -- "$unit_dir/$unit" "$unit_dir/$unit.d"
            REMOVED=1
        fi
    done
    if [[ -f "$data_dir/safe-mode" || -L "$data_dir/safe-mode" ]]; then
        change "removing the old safe-mode marker in $data_dir"
        run rm -f -- "$data_dir/safe-mode"
    fi
}
