#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Sets up a Raspberry Pi OS Lite machine to build and run etude.
#
#   sudo deploy/pi/setup.sh [--dry-run] [--no-build] [--enable kmsdrm|labwc] [--prefix DIR]
#                           [--share | --share-only] [--share-interface IF]... [--share-path DIR]
#                           [--reset-share-password]
#
# What it does, in order (each step says [ok] when nothing needed doing):
#   1. apt: installs the packages the build needs (the CI list plus KMSDRM and labwc extras).
#   2. user: creates the `etude` user and adds it to video, render, input and audio.
#      The login user that ran sudo is added to the same groups, so display-spike runs from it.
#   3. build: Release build in <repo>/build/pi-release, as the login user (not root).
#   4. install: etude, display-spike and midi-ports into <prefix>/versions/<version>/bin (default
#      prefix /opt/etude), the piano samples and sheet music's fonts into
#      <prefix>/versions/<version>/share/etude, and <prefix>/current points at that version. An
#      older install (bin/ and share/ straight under the prefix) is moved into versions/ first.
#   5. icon: the hicolor PNGs of every icon colour (etude-<colour>, and etude for the default,
#      wine) and an etude.desktop entry under /usr/local/share, then refreshes the icon cache when
#      gtk-update-icon-cache is there. The app switches the user's own copy of the entry to the colour
#      chosen on the About page, so no root is needed after this step.
#   6a. app updates, first: the root helper (/usr/local/lib/etude/etude-update-helper, a C++ program
#      built in step 3 and installed root-owned), its settings in /etc/etude-update.conf (a file of
#      KEY=value lines, read as data, root's alone), the sandbox it and the update units run under (one
#      drop-in list, etude-update-sandbox.conf), its folder /var/lib/etude-update (root, 0700) and
#      <prefix>/update, the folder the app drops its downloaded update and request in. This comes before
#      any unit, because the app units run the helper before every start: a step that fails stops the
#      script here, and no unit is left enabled that points at no helper.
#   6. systemd: installs both app units, the two crash-screen units (never enabled) and the PAM file, each
#      rendered for the prefix (@PREFIX@). A unit is enabled only with --enable.
#   6b. app updates, the units: the path unit that waits for a request, the service that installs it,
#      and the boot unit that finishes an update a reboot interrupted. Enabling the path and boot units
#      starts nothing by itself.
#   7. network folder (only with --share, or alone with --share-only): shares the Inbox of the
#      etude user's Songs folder on the home network as "etude" with Samba, for one user
#      (etude) and a password that is asked for, taken from ETUDE_SHARE_PASSWORD, or made and
#      printed once. No guest access; only the private-network interfaces (--share-interface IF
#      names them instead of finding them); the share is a file of its own, which smb.conf includes.
#      It is the "Network folder" tile of the app's Add songs page.
# It removes nothing but the safe-mode units and marker of an install from before the crash screen, never
# starts the service, and is safe to run again (step 7 starts and
# enables Samba and Avahi, which is its job).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIPT_NAME=setup.sh
DRY_RUN=0
# step, ok, change, warn, die, run, install_file and install_app_tree: shared with the release
# package script (scripts/release-pack.sh).
# shellcheck source=install-lib.sh
source "$SCRIPT_DIR/install-lib.sh"
# The versions/ layout: migrate_old_layout, point_current, project_version.
# shellcheck source=layout-lib.sh
source "$SCRIPT_DIR/layout-lib.sh"

APP_USER=etude
APP_GROUPS=(video render input audio)
PREFIX=/opt/etude
BUILD_DIR="$REPO/build/pi-release"
UNIT_DIR=/etc/systemd/system
PAM_FILE=/etc/pam.d/etude
DO_BUILD=1
DO_SHARE=0
SHARE_ONLY=0
SHARE_INTERFACES=()
SHARE_PATH=""
SHARE_SANDBOX="" # a test: files go under this folder and nothing on the machine is changed
RESET_SHARE_PASSWORD=0
ENABLE=""
JOBS="$(nproc 2>/dev/null || echo 2)"

# The CI package list (.github/workflows/ci.yml), plus cmake and a compiler.
BUILD_PACKAGES=(
    build-essential cmake ninja-build pkg-config ca-certificates
    libasound2-dev libudev-dev
    libdrm-dev libgbm-dev libegl-dev libgles-dev
    libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev
    libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols
    libdecor-0-dev libcurl4-openssl-dev
)
# The GPU drivers (Mesa) the KMSDRM and labwc paths draw with, and the tools the checklist uses.
RUNTIME_PACKAGES=(libgl1-mesa-dri libegl1 libgles2 libgbm1 evtest alsa-utils kbd)
# For the labwc kiosk path only. Skipped with a warning if the repository does not carry them.
LABWC_PACKAGES=(labwc wlr-randr)

usage() {
    sed -n '3,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while (($#)); do
    case "$1" in
    --dry-run) DRY_RUN=1 ;;
    --no-build) DO_BUILD=0 ;;
    --enable)
        shift
        [[ "${1:-}" == kmsdrm || "${1:-}" == labwc ]] || die "--enable needs kmsdrm or labwc"
        ENABLE="$1"
        ;;
    --prefix)
        shift
        [[ -n "${1:-}" ]] || die "--prefix needs a directory"
        PREFIX="$1"
        ;;
    --share) DO_SHARE=1 ;;
    --share-only)
        DO_SHARE=1
        SHARE_ONLY=1
        ;;
    --share-interface)
        shift
        [[ -n "${1:-}" ]] || die "--share-interface needs an interface name"
        SHARE_INTERFACES+=("$1")
        ;;
    --share-path)
        shift
        [[ -n "${1:-}" ]] || die "--share-path needs a folder"
        SHARE_PATH="$1"
        ;;
    --share-sandbox)
        shift
        [[ -n "${1:-}" ]] || die "--share-sandbox needs a folder"
        SHARE_SANDBOX="$1"
        DO_SHARE=1
        SHARE_ONLY=1
        ;;
    --reset-share-password) RESET_SHARE_PASSWORD=1 ;;
    -h | --help)
        usage
        exit 0
        ;;
    *) die "unknown option $1 (try --help)" ;;
    esac
    shift
done

# --- checks ------------------------------------------------------------------------------------

step "Checking the machine"
if [[ "$(uname -s)" != Linux ]]; then
    ((DRY_RUN)) || [[ -n "$SHARE_SANDBOX" ]] || die "this only runs on Linux (Pi OS); use --dry-run to read the plan"
    warn "not Linux: showing the plan only, package and user checks are skipped"
fi
if ((EUID != 0)) && ((!DRY_RUN)) && [[ -z "$SHARE_SANDBOX" ]]; then
    die "run it as root: sudo $0 $*"
fi
if [[ -r /proc/device-tree/model ]]; then
    ok "board: $(tr -d '\0' </proc/device-tree/model)"
else
    warn "no /proc/device-tree/model: not a Raspberry Pi? continuing"
fi
[[ -f "$REPO/CMakeLists.txt" ]] || die "no CMakeLists.txt in $REPO"
ok "repository: $REPO"
((DRY_RUN)) && ok "dry run: commands are printed with a leading +, not run"

# The login user who ran sudo; builds and group membership use it, not root.
LOGIN_USER=""
if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root ]]; then
    LOGIN_USER="$SUDO_USER"
fi

# --- 1. packages -------------------------------------------------------------------------------

is_installed() {
    command -v dpkg-query >/dev/null 2>&1 || return 1
    local status
    status="$(dpkg-query -W -f='${Status}' "$1" 2>/dev/null || true)"
    [[ "$status" == *"install ok installed"* ]]
}

APT_UPDATED=0
apt_update_once() {
    if ((!APT_UPDATED)); then
        run apt-get update
        APT_UPDATED=1
    fi
}

# install_packages REQUIRED|OPTIONAL pkg...
install_packages() {
    local kind="$1"
    shift
    local missing=() pkg
    for pkg in "$@"; do
        is_installed "$pkg" || missing+=("$pkg")
    done
    if ((${#missing[@]} == 0)); then
        ok "$kind packages installed ($#)"
        return 0
    fi
    if [[ "$kind" == OPTIONAL ]] && command -v apt-cache >/dev/null 2>&1; then
        local available=()
        for pkg in "${missing[@]}"; do
            if apt-cache show "$pkg" >/dev/null 2>&1; then
                available+=("$pkg")
            else
                warn "optional package $pkg is not in the apt sources; skipping it"
            fi
        done
        if ((${#available[@]} == 0)); then
            return 0
        fi
        missing=("${available[@]}")
    fi
    change "installing ${#missing[@]} $kind package(s): ${missing[*]}"
    apt_update_once
    run env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
}

# --- 7. network folder (Samba) -----------------------------------------------------------------
# Defined here, ahead of steps 1 to 6, so that --share-only can run it alone; step 7 of the full run
# calls the same function at the end.

SHARE_NAME=etude
SHARE_USER="$APP_USER"
SHARE_CHANGED=0
SHARE_WORD=""
SHARE_WORD_MADE=0
SHARE_WORD_SET=0

# A command that changes the machine: printed, not run, in a sandbox test.
sysrun() {
    if [[ -n "$SHARE_SANDBOX" ]]; then
        printf '  + (sandbox, not run)'
        printf ' %q' "$@"
        printf '\n'
        [[ -t 0 ]] || cat >/dev/null # what would have been read (a password) is taken, not left to a broken pipe
    else
        run "$@"
    fi
}

# share_put SRC DEST MODE: copies only when the content differs (SHARE_CHANGED says whether it did).
share_put() {
    SHARE_CHANGED=0
    if [[ -f "$2" ]] && cmp -s "$1" "$2"; then
        ok "$2 unchanged"
        return 0
    fi
    change "writing $2"
    SHARE_CHANGED=1
    run mkdir -p "$(dirname "$2")"
    run cp "$1" "$2"
    run chmod "$3" "$2"
}

# Interfaces that are not the home network whatever address they hold: VPNs and tunnels, and the
# virtual ones of containers and virtual machines.
is_not_home_interface() {
    case "$1" in
    tun* | tap* | wg* | tailscale* | utun* | ppp* | zt* | ipsec* | vpn* | docker* | br-* | veth* | virbr*)
        return 0
        ;;
    esac
    return 1
}

# The names of the interfaces that have a private-network IPv4 address (10/8, 172.16/12,
# 192.168/16) and are not a VPN or a virtual one: the share is bound to those and no others. The
# listing is `ip -o -4 addr show scope global` (a sandbox test gives it in ETUDE_SHARE_IP_LISTING).
private_interfaces() {
    local listing name addr second
    if [[ -n "$SHARE_SANDBOX" && -n "${ETUDE_SHARE_IP_LISTING:-}" ]]; then
        listing="$(cat "$ETUDE_SHARE_IP_LISTING")"
    else
        command -v ip >/dev/null 2>&1 || return 0
        listing="$(ip -o -4 addr show scope global 2>/dev/null || true)"
    fi
    printf '%s\n' "$listing" | awk '{split($4, a, "/"); sub(/@.*/, "", $2); print $2, a[1]}' |
        while read -r name addr; do
            is_not_home_interface "$name" && continue
            case "$addr" in
            10.* | 192.168.*) echo "$name" ;;
            172.*)
                second="${addr#172.}"
                second="${second%%.*}"
                ((second >= 16 && second <= 31)) && echo "$name"
                ;;
            esac
        done | sort -u
}

# The share's file, on standard output. $1 is the folder shared, the rest are the interfaces.
render_share_conf() {
    local path="$1"
    shift
    cat <<CONF
# Written by deploy/pi/setup.sh (the network folder). Change it there, not here.
[global]
   interfaces = lo $*
   bind interfaces only = yes
   hosts allow = 127.0.0.1 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16 169.254.0.0/16 fe80::/10
   hosts deny = ALL
   map to guest = Never
   restrict anonymous = 2
   server min protocol = SMB2_10
   disable netbios = yes
   smb ports = 445
   load printers = no

[$SHARE_NAME]
   comment = etude songs
   path = $path
   browseable = yes
   read only = no
   guest ok = no
   valid users = $SHARE_USER
   create mask = 0644
   directory mask = 0755
   hide dot files = yes
   follow symlinks = no
   wide links = no
   # Song names are kept as typed on the Pi's disk (? * " < > | : too). catia shows those to a
   # Windows computer as the private-use characters Windows and Mac both read; fruit does the same
   # for a Mac (its native encoding hands the mapping to catia).
   vfs objects = catia fruit streams_xattr
   fruit:encoding = native
   catia:mappings = 0x22:0xf020,0x2a:0xf021,0x3a:0xf022,0x3c:0xf023,0x3e:0xf024,0x3f:0xf025,0x7c:0xf027
CONF
}

# The password for the share's user, in SHARE_WORD: from ETUDE_SHARE_PASSWORD, asked for at a
# terminal, or made. SHARE_WORD_MADE is 1 only for a made one, the only kind that is ever printed.
share_password() {
    SHARE_WORD_MADE=0
    if [[ -n "${ETUDE_SHARE_PASSWORD:-}" ]]; then
        SHARE_WORD="$ETUDE_SHARE_PASSWORD"
    elif [[ -t 0 && -z "$SHARE_SANDBOX" ]] && ((!DRY_RUN)); then
        local first second
        read -r -s -p "  Password for the network folder (user $SHARE_USER): " first </dev/tty
        printf '\n' >&2
        read -r -s -p "  Again: " second </dev/tty
        printf '\n' >&2
        [[ -n "$first" && "$first" == "$second" ]] || die "the two passwords differ or are empty"
        SHARE_WORD="$first"
    else
        SHARE_WORD="$(LC_ALL=C tr -dc 'A-HJ-NP-Za-km-z2-9' </dev/urandom | head -c 12 || true)"
        SHARE_WORD_MADE=1
    fi
}

# Whether the user already has a network password (a sandbox test leaves a file to say so).
share_has_password() {
    if [[ -n "$SHARE_SANDBOX" ]]; then
        [[ -e "$SHARE_SANDBOX/etc/samba/.password-set" ]]
    else
        command -v pdbedit >/dev/null 2>&1 && pdbedit -L -u "$SHARE_USER" >/dev/null 2>&1
    fi
}

share_user() {
    if ! id "$SHARE_USER" >/dev/null 2>&1 && [[ -z "$SHARE_SANDBOX" ]] && ((!DRY_RUN)); then
        die "no user $SHARE_USER: run the full setup first (step 2 makes it)"
    fi
    if share_has_password && ((!RESET_SHARE_PASSWORD)); then
        ok "$SHARE_USER already has a network password (--reset-share-password sets a new one)"
        return 0
    fi
    if ((DRY_RUN)); then
        change "the network password of $SHARE_USER would be set here"
        return 0
    fi
    share_password
    change "setting the network password of $SHARE_USER"
    printf '%s\n%s\n' "$SHARE_WORD" "$SHARE_WORD" | sysrun smbpasswd -a -s "$SHARE_USER"
    [[ -z "$SHARE_SANDBOX" ]] || : >"$SHARE_SANDBOX/etc/samba/.password-set"
    SHARE_WORD_SET=1
}

share_services() { # share_services CONFIG_CHANGED
    local unit
    for unit in smbd avahi-daemon; do
        if [[ -n "$SHARE_SANDBOX" ]] || ! command -v systemctl >/dev/null 2>&1; then
            sysrun systemctl enable --now "$unit"
        elif systemctl is-active --quiet "$unit" 2>/dev/null; then
            if [[ "$unit" == smbd && "$1" == 1 ]]; then
                change "restarting $unit for the new share"
                run systemctl restart "$unit"
            else
                ok "$unit running"
            fi
        else
            change "enabling and starting $unit"
            run systemctl enable --now "$unit"
        fi
    done
}

# The folder that is shared, made (as the app's user, so the app owns it) when it is not there.
share_folder() {
    if [[ -d "$1" ]]; then
        ok "$1 exists"
    elif [[ -n "$SHARE_SANDBOX" ]] || ! id "$SHARE_USER" >/dev/null 2>&1; then
        change "making $1"
        run mkdir -p "$1"
    else
        change "making $1"
        run runuser -u "$SHARE_USER" -- mkdir -p "$1"
    fi
}

# The interfaces to bind to, one a line: the ones named, else the ones on a private network.
share_interfaces() {
    if ((${#SHARE_INTERFACES[@]} > 0)); then
        printf '%s\n' "${SHARE_INTERFACES[@]}"
    else
        private_interfaces
    fi
}

# Tries the share on a copy: testparm reads the Pi's smb.conf with this share's file included (and
# not the one already there), and nothing is written to /etc until it passes.
share_validate() { # share_validate SMB_CONF CANDIDATE INCLUDE_LINE
    command -v testparm >/dev/null 2>&1 || return 0
    ((DRY_RUN)) && return 0
    local dir
    dir="$(mktemp -d)"
    grep -vxF "$3" "$1" >"$dir/smb.conf" || true
    cp "$2" "$dir/share.conf"
    printf 'include = %s\n' "$dir/share.conf" >>"$dir/smb.conf"
    if ! testparm -s "$dir/smb.conf" >"$dir/out" 2>&1; then
        sed 's/^/  testparm: /' "$dir/out" >&2
        rm -rf "$dir"
        die "testparm refused the share: nothing was changed"
    fi
    rm -rf "$dir"
}

share_step() {
    local root="$SHARE_SANDBOX" home path
    local smb_dir="$root/etc/samba"
    local smb_conf="$smb_dir/smb.conf" share_conf="$smb_dir/etude-share.conf"
    if [[ -n "$SHARE_SANDBOX" ]]; then
        mkdir -p "$smb_dir"
        [[ -f "$smb_conf" ]] || printf '[global]\n   workgroup = WORKGROUP\n' >"$smb_conf"
    else
        install_packages REQUIRED samba samba-vfs-modules avahi-daemon
        [[ -f "$smb_conf" ]] || ((DRY_RUN)) || die "no $smb_conf: is samba installed?"
    fi

    # The folder shared: the Inbox, where the phone page's uploads go and the folder watch looks.
    path="$SHARE_PATH"
    if [[ -z "$path" ]]; then
        home="$(getent passwd "$SHARE_USER" 2>/dev/null | cut -d: -f6 || true)"
        path="$root${home:-/home/$SHARE_USER}/.local/share/io.github.techneissa.etude/Songs/Inbox"
    fi
    share_folder "$path"

    local interfaces=() line
    while IFS= read -r line; do
        [[ -z "$line" ]] || interfaces+=("$line")
    done < <(share_interfaces)
    if ((${#interfaces[@]} == 0)); then
        ((DRY_RUN)) || die "no network interface has a home-network address: connect the Pi and run this again (or name one with --share-interface)"
        warn "no network interface has a home-network address; a real run stops here"
        interfaces=(eth0)
    fi
    ok "shared on: ${interfaces[*]} (and this machine)"

    local tmp changed include="include = $share_conf"
    tmp="$(mktemp)"
    render_share_conf "$path" "${interfaces[@]}" >"$tmp"
    share_validate "$smb_conf" "$tmp" "$include"
    share_put "$tmp" "$share_conf" 0644
    changed="$SHARE_CHANGED"
    rm -f "$tmp"
    if grep -qxF "$include" "$smb_conf" 2>/dev/null; then
        ok "$smb_conf includes the share"
    else
        change "adding the share to $smb_conf (a copy is kept as smb.conf.before-etude)"
        changed=1
        if ((!DRY_RUN)); then
            [[ -e "$smb_conf.before-etude" ]] || cp -p "$smb_conf" "$smb_conf.before-etude"
            printf '\n# etude: the network folder\n%s\n' "$include" >>"$smb_conf"
        fi
    fi
    share_user
    share_services "$changed"
    if [[ "$(hostname 2>/dev/null || true)" != etude ]]; then
        warn "the address etude.local needs the Pi's name to be etude: sudo hostnamectl set-hostname etude"
    fi
    local SHARE_PASSWORD_TEXT="the password set before"
    if ((SHARE_WORD_SET && SHARE_WORD_MADE)); then
        SHARE_PASSWORD_TEXT="the password $SHARE_WORD (made for you, shown only now: write it down)"
    elif ((SHARE_WORD_SET)); then
        SHARE_PASSWORD_TEXT="the password you gave"
    fi
    cat <<DONE

  Network folder: $SHARE_NAME ($path)
    Windows:   \\\\etude.local\\$SHARE_NAME
    Mac/Linux: smb://etude.local/$SHARE_NAME
    Sign in as $SHARE_USER with ${SHARE_PASSWORD_TEXT}
DONE
}

if ((SHARE_ONLY)); then
    step "7. Network folder (Samba)"
    share_step
    exit 0
fi

step "1. Packages"
install_packages REQUIRED "${BUILD_PACKAGES[@]}" "${RUNTIME_PACKAGES[@]}"
install_packages OPTIONAL "${LABWC_PACKAGES[@]}"

# --- 2. user and groups ------------------------------------------------------------------------

in_group() { # in_group USER GROUP
    local members
    members="$(id -nG "$1" 2>/dev/null || true)"
    [[ " $members " == *" $2 "* ]]
}

ensure_groups() { # ensure_groups USER
    local user="$1" group
    for group in "${APP_GROUPS[@]}"; do
        if ! getent group "$group" >/dev/null 2>&1; then
            warn "group $group does not exist on this image; drop it from SupplementaryGroups in the units"
        elif in_group "$user" "$group"; then
            ok "$user is in $group"
        else
            change "adding $user to $group"
            run usermod -aG "$group" "$user"
        fi
    done
}

step "2. User and groups"
if id "$APP_USER" >/dev/null 2>&1; then
    ok "user $APP_USER exists"
else
    change "creating user $APP_USER (no login shell, no password)"
    run useradd --create-home --shell /usr/sbin/nologin --comment "etude kiosk" "$APP_USER"
fi
ensure_groups "$APP_USER"
if [[ -n "$LOGIN_USER" ]]; then
    ensure_groups "$LOGIN_USER"
    ok "log out and in again for $LOGIN_USER's new groups to apply"
fi

# --- 3. build ----------------------------------------------------------------------------------

as_builder() {
    if [[ -n "$LOGIN_USER" ]]; then
        run runuser -u "$LOGIN_USER" -- "$@"
    else
        run "$@"
    fi
}

step "3. Build (Release)"
if ((DO_BUILD)); then
    change "configuring and building in $BUILD_DIR (the first run downloads dependencies)"
    as_builder cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DETUDE_BUILD_TESTS=OFF
    as_builder cmake --build "$BUILD_DIR" --parallel "$JOBS" \
        --target etude display-spike midi-ports etude-update-helper
else
    ok "skipped (--no-build)"
fi

# --- 4. install --------------------------------------------------------------------------------

step "4. Install to $PREFIX/versions/ and point $PREFIX/current at it"
migrate_old_layout "$PREFIX" "$APP_USER"
VERSION="$(project_version "$REPO")"
[[ "$VERSION" =~ $VERSION_PATTERN ]] || die "no X.Y.Z project version in $REPO/CMakeLists.txt"
install_app_tree "$BUILD_DIR" "$PREFIX/versions/$VERSION"
point_current "$PREFIX" "$VERSION"

# --- 5. icon and desktop entry -----------------------------------------------------------------

step "5. Icon and desktop entry"
# Desktops only look in the XDG data dirs, so the icon and the entry go to /usr/local/share
# (not under the prefix). The entry's Exec points at this install's binary.
XDG_SHARE=/usr/local/share
ICON_SIZES=(16 24 32 48 64 128 256 512)
# The colours of the app icon, in the order of the titles (core/include/etude/icon_colour.hpp).
ICON_COLOURS=(wine teal petrol cobalt midnight aubergine graphite black)
ICONS_CHANGED=0
for size in "${ICON_SIZES[@]}"; do
    for colour in "${ICON_COLOURS[@]}"; do
        install_file "$REPO/app/assets/icon/$colour/etude-$size.png" \
            "$XDG_SHARE/icons/hicolor/${size}x${size}/apps/etude-$colour.png" 0644
        ((CHANGED)) && ICONS_CHANGED=1
    done
    # The entry's own icon name, the default colour's.
    install_file "$REPO/app/assets/icon/wine/etude-$size.png" \
        "$XDG_SHARE/icons/hicolor/${size}x${size}/apps/etude.png" 0644
    ((CHANGED)) && ICONS_CHANGED=1
done
DESKTOP_TMP="$(mktemp)"
sed "s|^Exec=.*|Exec=$PREFIX/current/bin/etude|" "$REPO/deploy/linux/etude.desktop" >"$DESKTOP_TMP"
install_file "$DESKTOP_TMP" "$XDG_SHARE/applications/etude.desktop" 0644
rm -f "$DESKTOP_TMP"
if ((ICONS_CHANGED)); then
    if command -v gtk-update-icon-cache >/dev/null 2>&1 || ((DRY_RUN)); then
        run gtk-update-icon-cache -f -t "$XDG_SHARE/icons/hicolor"
    else
        warn "gtk-update-icon-cache not found; the icon appears after the next icon cache refresh"
    fi
fi

# --- 6a. app updates: the root helper and its settings --------------------------------------------
# Before the units: the app units run the helper before every start (ExecStartPre), so it and its
# settings are in place first, and a failure here (die) never leaves an enabled unit pointing at no helper.

# A path as the helper's settings file takes it: absolute, plain characters, no `.` or `..` part.
plain_path() {
    [[ "$1" =~ ^/[A-Za-z0-9._+@:,/-]*$ && "$1" != *//* && "$1" != */ && "$1" != */./* && "$1" != */../* &&
        "$1" != */. && "$1" != */.. ]]
}

# install_root_file SRC DEST MODE: like install_file, root-owned whoever's file it was.
install_root_file() {
    local src="$1" dest="$2" mode="$3"
    CHANGED=0
    if [[ ! -e "$src" ]]; then
        if ((DRY_RUN)); then
            change "would install $dest (not built yet)"
            CHANGED=1
            return 0
        fi
        die "missing $src: build first, or drop --no-build"
    fi
    if [[ -f "$dest" && ! -L "$dest" ]] && cmp -s "$src" "$dest" && [[ "$(stat -c '%U:%a' "$dest" 2>/dev/null)" == "root:${mode#0}" ]]; then
        ok "$dest unchanged"
        return 0
    fi
    change "installing $dest (root, $mode)"
    CHANGED=1
    run install -D -o root -g root -m "$mode" "$src" "$dest"
}

# render_unit SRC DEST: the unit with @PREFIX@, @DATA_DIR@ and @APP_HOME@ filled in.
render_unit() {
    local tmp
    tmp="$(mktemp)"
    sed -e "s|@PREFIX@|$PREFIX|g" -e "s|@DATA_DIR@|$DATA_DIR|g" -e "s|@APP_HOME@|$APP_HOME|g" "$1" >"$tmp"
    install_file "$tmp" "$2" 0644
    rm -f "$tmp"
}

step "6a. App updates (the root helper and its settings)"
UPDATE_HELPER=/usr/local/lib/etude/etude-update-helper
LABWC_SESSION=/usr/local/lib/etude/etude-labwc-session
UPDATE_CONF=/etc/etude-update.conf
UPDATE_STATE=/var/lib/etude-update
plain_path "$PREFIX" || die "the prefix $PREFIX has a character the update helper does not take (letters, digits and . _ + @ : , / - only)"
# The app's data folder as SDL_GetPrefPath finds it: the backup and restore write there, as the app's
# user, in the app user's home, which is what the helper's sandbox may write beside its own folders.
APP_HOME="$(getent passwd "$APP_USER" 2>/dev/null | cut -d: -f6 || true)"
APP_HOME="${APP_HOME:-/home/$APP_USER}"
# The helper's sandbox may write the app user's home and not the data folder inside it: the app owns its
# data folder and can turn it into a link, and systemd follows a link when it sets up a writable path.
# The home cannot be swapped by the app only while it is the user's own, with every link resolved and
# every folder above it root's and closed to writes by others, and no system folder (check_app_home in
# install-lib.sh); the data folder is below it.
if ((DRY_RUN)) && [[ ! -e "$APP_HOME" ]]; then
    warn "the home folder $APP_HOME does not exist yet (a dry run): it is checked when it does"
elif ! home_checked="$(check_app_home "$APP_HOME" "$APP_USER")"; then
    die "the update helper's sandbox cannot be set up for $APP_USER's home (see above)"
else
    APP_HOME="$home_checked"
fi
DATA_DIR="$APP_HOME/.local/share/io.github.techneissa.etude"
plain_path "$APP_HOME" || die "the home folder $APP_HOME of $APP_USER has a character the update helper does not take"
plain_path "$DATA_DIR" || die "the data folder $DATA_DIR has a character the update helper does not take"
[[ "$DATA_DIR" == "$APP_HOME"/* ]] || die "the data folder $DATA_DIR is not under the home $APP_HOME of $APP_USER"
# The unit's ReadWritePaths names this folder, and systemd follows a link in it: a link the app's user
# made (under its own home, say) would point the sandbox's writable path somewhere else. Refuse a
# folder with a link in its path that root did not make; the helper checks the same before it backs
# anything up.
pinned_path() {
    local path="" part
    local -a parts
    IFS=/ read -ra parts <<<"${1#/}"
    for part in "${parts[@]}"; do
        path="$path/$part"
        if [[ -L "$path" && "$(stat -c '%u' "$path" 2>/dev/null || echo 1)" != 0 ]]; then
            echo "$path"
            return 1
        fi
    done
    return 0
}
if ! link_at="$(pinned_path "$DATA_DIR")"; then
    die "the data folder $DATA_DIR goes through the link $link_at, which $APP_USER (not root) made: remove it and run setup again"
fi
install_root_file "$BUILD_DIR/helper/etude-update-helper" "$UPDATE_HELPER" 0755
# The labwc units' ExecStart (read the comment there): root's, like the helper, since it runs as root's file
# under the app's user.
install_root_file "$SCRIPT_DIR/etude-labwc-session" "$LABWC_SESSION" 0755
if [[ -e /usr/local/lib/etude/update-helper ]]; then
    change "removing the old shell helper /usr/local/lib/etude/update-helper"
    run rm -f /usr/local/lib/etude/update-helper
fi
# The settings: data for the helper to parse, never shell, so a path is quoted for the parser's sake
# and the file is root's, 0644. The helper refuses it otherwise.
CONF_TMP="$(mktemp)"
printf 'PREFIX="%s"\nAPP_USER="%s"\n' "$PREFIX" "$APP_USER" >"$CONF_TMP"
install_root_file "$CONF_TMP" "$UPDATE_CONF" 0644
rm -f "$CONF_TMP"
# The sandbox of everything the helper does, one list (etude-update-sandbox.conf): a drop-in of the
# update service and of the boot one, and what the helper's `sandboxed` modes (the app units' count and
# idle) read from the update service's copy to run the same program as a transient service. Before any
# unit, as the helper is: the app units' first start already asks for it.
SANDBOX_CHANGED=0
for sandboxed_unit in etude-update.service etude-update-resume.service; do
    render_unit "$SCRIPT_DIR/etude-update-sandbox.conf" "$UNIT_DIR/$sandboxed_unit.d/sandbox.conf"
    ((CHANGED)) && SANDBOX_CHANGED=1
done
# The helper refuses a state folder that is not root's alone, and a refusal ends `count` and `idle` with
# 0 and no record at all: so an existing folder is set to root and 0700 too, not left as it was.
if [[ -d "$UPDATE_STATE" && "$(stat -c '%U:%a' "$UPDATE_STATE" 2>/dev/null)" == "root:700" ]]; then
    ok "$UPDATE_STATE exists (root, 0700)"
else
    change "making $UPDATE_STATE (root, 0700), or setting its owner and mode"
    run install -d -o root -g root -m 0700 "$UPDATE_STATE"
fi
# The folder the app drops its downloaded update and request in: the app's user owns it, root owns
# everything above it. The helper reads everything in it as hostile: it copies the files out by
# descriptor, verifies the signature itself and unpacks into versions/ on its own.
if [[ -d "$PREFIX/update" ]]; then
    ok "$PREFIX/update exists"
else
    change "making $PREFIX/update (owned by $APP_USER)"
    run install -d -o "$APP_USER" -g "$APP_USER" -m 0755 "$PREFIX/update"
fi
# --- 6. systemd --------------------------------------------------------------------------------

step "6. systemd units (the app's, rendered for $PREFIX)"
if ! command -v systemctl >/dev/null 2>&1; then
    warn "no systemctl here; skipping units"
else
    UNITS_CHANGED=0
    install_file "$SCRIPT_DIR/pam-etude" "$PAM_FILE" 0644
    render_unit "$SCRIPT_DIR/etude.service" "$UNIT_DIR/etude.service"
    ((CHANGED)) && UNITS_CHANGED=1
    render_unit "$SCRIPT_DIR/etude-labwc.service" "$UNIT_DIR/etude-labwc.service"
    ((CHANGED)) && UNITS_CHANGED=1
    # What OnFailure= of the two starts after a crash: the crash screen, where the player chooses Restart.
    # Never enabled.
    render_unit "$SCRIPT_DIR/etude-crash-screen.service" "$UNIT_DIR/etude-crash-screen.service"
    ((CHANGED)) && UNITS_CHANGED=1
    render_unit "$SCRIPT_DIR/etude-labwc-crash-screen.service" "$UNIT_DIR/etude-labwc-crash-screen.service"
    ((CHANGED)) && UNITS_CHANGED=1
    # An install from before the crash screen: its safe-mode units and marker are gone.
    remove_safe_mode_leftovers "$UNIT_DIR" "$DATA_DIR"
    ((REMOVED)) && UNITS_CHANGED=1
    if ((UNITS_CHANGED)); then
        run systemctl daemon-reload
    fi
    if ((!DRY_RUN)); then
        systemd-analyze verify "$UNIT_DIR/etude.service" "$UNIT_DIR/etude-labwc.service" \
            "$UNIT_DIR/etude-crash-screen.service" "$UNIT_DIR/etude-labwc-crash-screen.service" ||
            warn "systemd-analyze verify reported the issues above; read them before enabling"
    fi

    case "$ENABLE" in
    kmsdrm | labwc)
        [[ "$ENABLE" == kmsdrm ]] && chosen=etude.service other=etude-labwc.service
        [[ "$ENABLE" == labwc ]] && chosen=etude-labwc.service other=etude.service
        if systemctl is-enabled --quiet "$other" 2>/dev/null; then
            change "disabling $other"
            run systemctl disable "$other"
        fi
        if systemctl is-enabled --quiet "$chosen" 2>/dev/null; then
            ok "$chosen already enabled"
        else
            change "enabling $chosen (starts at the next boot)"
            run systemctl enable "$chosen"
        fi
        ;;
    *)
        ok "no unit enabled (pass --enable kmsdrm or --enable labwc once the checklist passes)"
        ;;
    esac
fi

# --- 6b. the update units -----------------------------------------------------------------------

step "6b. App updates (their units)"
if command -v systemctl >/dev/null 2>&1; then
    UPDATE_UNITS_CHANGED=0
    for unit in etude-update.service etude-update.path etude-update-resume.service; do
        render_unit "$SCRIPT_DIR/$unit" "$UNIT_DIR/$unit"
        ((CHANGED)) && UPDATE_UNITS_CHANGED=1
    done
    if ((UPDATE_UNITS_CHANGED || SANDBOX_CHANGED)); then
        run systemctl daemon-reload
    fi
    if ((!DRY_RUN)); then
        systemd-analyze verify "$UNIT_DIR/etude-update.service" "$UNIT_DIR/etude-update.path" \
            "$UNIT_DIR/etude-update-resume.service" ||
            warn "systemd-analyze verify reported the issues above; read them before relying on updates"
    fi
    if systemctl is-enabled --quiet etude-update.path 2>/dev/null; then
        ok "etude-update.path already enabled"
    else
        change "enabling etude-update.path (it only waits for a request from the app)"
        run systemctl enable --now etude-update.path
    fi
    if systemctl is-enabled --quiet etude-update-resume.service 2>/dev/null; then
        ok "etude-update-resume.service already enabled"
    else
        change "enabling etude-update-resume.service (at boot it finishes an update a reboot interrupted)"
        run systemctl enable etude-update-resume.service
    fi
else
    warn "no systemctl here; skipping the update units"
fi

if ((DO_SHARE)); then
    step "7. Network folder (Samba)"
    share_step
fi

step "Done"
cat <<EOF
Nothing was started. Next:
  - Log in on the Pi's own console (tty1, not over SSH), then run the checks in
    $SCRIPT_DIR/CHECKLIST.md, for example:
      $PREFIX/current/bin/display-spike --seconds 10
  - Test the service by hand with: sudo systemctl start etude.service
    (this takes over tty1; stop it from SSH with: sudo systemctl stop etude.service)
EOF
