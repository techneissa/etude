#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tests the root side of app updates (helper/, deploy/pi/layout-lib.sh and the units of deploy/pi/) in
# a Debian 13 (trixie) container, as root, with a real etude-update-helper built there, real signed
# tarballs (minisign) and a stand-in for the app and for systemctl: a service manager that does what
# etude.service does (the helper's `count` before each start, the app as the etude user, no restart
# by itself: a failure leaves the unit failed and starts the crash screen, as OnFailure= does), and an
# app that behaves as asked and says ready through the app's own
# ReadyGate and file writer (scripts/update-ready-probe.cpp).
#
#   scripts/update-helper-test.sh             run it in the container (needs Docker)
#   scripts/update-helper-test.sh --inside    run it here (as root, on a throwaway Debian 13 machine)
#
# The helper is built with a public key made here, for this run only (-DETUDE_UPDATE_KEY_SOURCE):
# never the release key, and never a build that ships.
#
# It checks the install into versions/ and the move of an older install into it; updates (the data
# backed up, the new version confirmed, older versions removed); an update that never reaches its
# first frame, one that crashes at once and one whose history cannot migrate (all rolled back, data
# restored); a reboot or a power cut at any point of an update (the helper killed at set times, then
# the boot path); and the attacks: a link planted at each backup or restore path, a link where the
# status is written, a request or tarball that is a FIFO, a link for `staging`, a hard link in the
# tarball, a tampered tarball, a signature for another version, machine, key or an older release, a
# service that will not stop, a full disk, leftovers of a cut-short run, and settings that are not
# root's or not plain data. Under a real systemd it ends with the crash chain of the real app
# (update-helper-test-crash.sh: the build deploy/pi/setup.sh installs, run with SDL's offscreen video driver).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
# The image the release workflow builds in (the oldest system the releases run on).
IMAGE="debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5"

if [[ "${1:-}" != --inside ]]; then
    command -v docker >/dev/null || { echo "update-helper-test: Docker is not installed" >&2; exit 2; }
    docker info >/dev/null 2>&1 || { echo "update-helper-test: Docker is not running" >&2; exit 2; }
    # The whole suite runs in one container whose PID 1 is a real systemd (a privileged container
    # with the host's cgroup tree), so the last section can run an update and a rollback through the
    # real units. SYS_ADMIN for the full-disk case comes with --privileged. The Docker volume
    # etude-uh-build keeps the real app's build folder and download cache between runs (the crash
    # chain at the end builds the app with deploy/pi/setup.sh; only the first run builds it all). When such a container
    # cannot be started here, the suite runs in a plain container and says, loudly, that the real
    # systemd section did not run.
    NAME="etude-update-test-$$"
    trap 'docker rm -f "$NAME" >/dev/null 2>&1' EXIT
    if docker run -d --name "$NAME" --privileged --cgroupns=host -v /sys/fs/cgroup:/sys/fs/cgroup:rw \
        --tmpfs /run --tmpfs /run/lock -v "$REPO":/src:ro -v etude-uh-build:/cache "$IMAGE" bash -c \
        'apt-get update -qq >/dev/null 2>&1 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends systemd dbus >/dev/null 2>&1 || exit 1; exec /lib/systemd/systemd --system --unit=multi-user.target' >/dev/null 2>&1; then
        up=0
        for _ in $(seq 120); do
            state="$(docker exec "$NAME" systemctl is-system-running 2>/dev/null || true)"
            if [[ "$state" == running || "$state" == degraded ]]; then up=1; break; fi
            sleep 1
        done
        if ((up)); then
            docker exec "$NAME" bash /src/scripts/update-helper-test.sh --inside --systemd
            exit $?
        fi
        echo "update-helper-test: systemd did not come up in the container (it said '${state:-nothing}')" >&2
        docker logs "$NAME" 2>&1 | tail -5 >&2
        docker rm -f "$NAME" >/dev/null 2>&1
    else
        echo "update-helper-test: Docker would not start a privileged container with systemd as PID 1" >&2
    fi
    echo "update-helper-test: *** THE REAL-SYSTEMD SECTION WILL NOT RUN: running the stand-in suite in a plain container; test the units on the Pi before a release ***" >&2
    exec docker run --rm --cap-add SYS_ADMIN -v "$REPO":/src:ro "$IMAGE" bash /src/scripts/update-helper-test.sh --inside
fi

SYSTEMD_MODE=0
[[ "${2:-}" == --systemd ]] && SYSTEMD_MODE=1

[[ "$(id -u)" == 0 ]] || { echo "update-helper-test: --inside needs root" >&2; exit 2; }

SCRIPT_NAME=update-helper-test
DRY_RUN=0
PASSED=0
FAILED=0
pass() { PASSED=$((PASSED + 1)); printf '  ok    %s\n' "$*"; }
flunk() { FAILED=$((FAILED + 1)); printf '  FAIL  %s\n' "$*"; }
expect() { # expect DESCRIPTION COMMAND...
    local what="$1"
    shift
    if "$@" >/dev/null 2>&1; then pass "$what"; else flunk "$what"; fi
}
expect_equal() { # expect_equal DESCRIPTION WANT GOT
    if [[ "$2" == "$3" ]]; then pass "$1"; else flunk "$1: wanted '$2', got '$3'"; fi
}
expect_contains() { # expect_contains DESCRIPTION NEEDLE HAYSTACK
    if [[ "$3" == *"$2"* ]]; then pass "$1"; else flunk "$1: '$3' does not say '$2'"; fi
}

# --- the machine -------------------------------------------------------------------------------

echo "== setting up: packages, the helper built with a test key, the stand-ins"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build ca-certificates \
    minisign systemd tar gzip procps util-linux kbd >/dev/null 2>&1 || { echo "update-helper-test: apt-get failed" >&2; exit 2; }

# shellcheck source=../deploy/pi/install-lib.sh
source "$REPO/deploy/pi/install-lib.sh"
# shellcheck source=../deploy/pi/layout-lib.sh
source "$REPO/deploy/pi/layout-lib.sh"

id etude >/dev/null 2>&1 || useradd --create-home --shell /usr/sbin/nologin etude
WORK=/tmp/uh
rm -rf "$WORK"
mkdir -p "$WORK/keys" "$WORK/pack" "$WORK/svc" "$WORK/decoy" "$WORK/open"
chmod 0755 "$WORK" "$WORK/decoy"
chmod 1777 "$WORK/open" # where an app run as the etude user may leave a note
chown etude: "$WORK/decoy"
# Not under /tmp: the helper refuses a prefix with a world-writable folder above it.
PREFIX=/opt/uh-prefix
STATE=$WORK/state
CONF=$WORK/update.conf
DATA=/home/etude/.local/share/io.github.techneissa.etude
HELPER=/usr/local/lib/etude/etude-update-helper
SVC=$WORK/svc
ARCH="$(dpkg --print-architecture)"
READY=3

# Two key pairs: the build trusts the first; the second signs what must be refused.
(cd "$WORK/keys" && minisign -G -W -f -p good.pub -s good.sec >/dev/null 2>&1 && minisign -G -W -f -p other.pub -s other.sec >/dev/null 2>&1)
TEST_KEY="$(sed -n 2p "$WORK/keys/good.pub")"
sed "s|^constexpr std::string_view kUpdatePublicKey = .*|constexpr std::string_view kUpdatePublicKey = \"$TEST_KEY\";|" \
    "$REPO/core/src/update_key.cpp" >"$WORK/update_key_test.cpp"
grep -q "$TEST_KEY" "$WORK/update_key_test.cpp" || { echo "update-helper-test: the test key was not put in" >&2; exit 2; }

cmake -S "$REPO" -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DETUDE_CCACHE=OFF \
    -DETUDE_UPDATE_HELPER_ONLY=ON -DETUDE_BUILD_APP=OFF -DETUDE_BUILD_TOOLS=OFF -DETUDE_BUILD_TESTS=OFF \
    -DETUDE_UPDATE_KEY_SOURCE="$WORK/update_key_test.cpp" >"$WORK/cmake.log" 2>&1 &&
    cmake --build /build --target etude-update-helper >>"$WORK/cmake.log" 2>&1 ||
    { tail -30 "$WORK/cmake.log" >&2; echo "update-helper-test: the helper did not build" >&2; exit 2; }
install -D -o root -g root -m 0755 /build/helper/etude-update-helper "$HELPER"
g++ -std=c++20 -O1 -I"$REPO/core/include" -I"$REPO/app" "$REPO/scripts/update-ready-probe.cpp" \
    "$REPO/app/update_files.cpp" "$REPO/app/watchdog.cpp" "$REPO/core/src/update_handoff.cpp" "$REPO/core/src/semver.cpp" \
    -o "$WORK/ready-probe" || { echo "update-helper-test: the ready probe did not build" >&2; exit 2; }

# --- the stand-ins -----------------------------------------------------------------------------

write_conf() {
    printf 'PREFIX="%s"\nAPP_USER="etude"\nSTATE_DIR="%s"\nSYSTEMCTL="%s"\nREADY_TIMEOUT=%s\nMAX_STARTS=2\n' \
        "$PREFIX" "$STATE" "$WORK/systemctl" "$READY" >"$CONF"
    chown root:root "$CONF"
    chmod 0644 "$CONF"
}

# What systemd does for etude.service: the helper's count before every start (ExecStartPre=+), the app
# as etude, and no restart by itself (Restart=no): a failure (the app's, or a count that ends 75)
# leaves the unit failed, and OnFailure= starts the crash screen, unless the helper is acting on the app
# (its `idle` ExecCondition), after the helper has gone back from a version on trial that crashed (its
# `crashed` ExecStartPre). The crash screen here is a marker file: `crash-screen-up`.
cat >"$WORK/svc-run" <<RUN
#!/usr/bin/env bash
dir=$SVC
on_failure() {
    touch "\$dir/failed"
    "$HELPER" --config "$CONF" idle >>"\$dir/count.log" 2>&1 || return 0
    "$HELPER" --config "$CONF" crashed >>"\$dir/count.log" 2>&1 || true
    touch "\$dir/crash-screen-up"
}
"$HELPER" --config "$CONF" count >>"\$dir/count.log" 2>&1
rc=\$?
if [[ \$rc -eq 75 ]]; then
    on_failure
else
    setsid runuser -u etude -- "$PREFIX/current/bin/etude" >>"\$dir/app.log" 2>&1 </dev/null &
    echo \$! >"\$dir/app.pid"
    wait \$!
    rc=\$?
    if [[ ! -e "\$dir/stopping" && \$rc -ne 0 ]]; then
        on_failure
    fi
fi
rm -f "\$dir/app.pid" "\$dir/super.pid"
RUN
chmod 0755 "$WORK/svc-run"

cat >"$WORK/systemctl" <<SYSTEMCTL
#!/usr/bin/env bash
# The parts of systemctl the helper uses, for one unit: etude.service.
dir=$SVC
echo "\$*" >>"\$dir/calls"
alive() { [[ -f "\$dir/super.pid" ]] && kill -0 "\$(cat "\$dir/super.pid")" 2>/dev/null; }
stop_all() {
    touch "\$dir/stopping"
    [[ -f "\$dir/app.pid" ]] && kill -TERM -- "-\$(cat "\$dir/app.pid")" 2>/dev/null
    [[ -f "\$dir/super.pid" ]] && kill "\$(cat "\$dir/super.pid")" 2>/dev/null
    for _ in \$(seq 50); do alive || break; sleep 0.1; done
    rm -f "\$dir/stopping" "\$dir/super.pid" "\$dir/app.pid"
}
start_it() {
    rm -f "\$dir/stopping" "\$dir/failed" "\$dir/crash-screen-up"
    [[ -e "\$dir/stick-on-start" ]] && touch "\$dir/stuck" # a stop that does nothing from now on
    setsid bash "$WORK/svc-run" >>"\$dir/super.log" 2>&1 </dev/null &
    echo \$! >"\$dir/super.pid"
    sleep 0.3
}
# A power cut at the start: the caller (the helper) is killed before anything starts.
cut() { [[ -e "\$dir/cut-on-start" ]] && { rm -f "\$dir/cut-on-start"; kill -9 "\$PPID"; exit 1; }; }
# A signal to the caller (the helper) at a set call: a stop of the update service, as systemd does.
if [[ -e "\$dir/term-on-\$1" ]]; then rm -f "\$dir/term-on-\$1"; kill -TERM "\$PPID"; fi
case "\$1" in
is-active)
    shift
    # \$dir/answer: the word every unit is in, whatever it is really doing (activating, ...).
    if [[ -e "\$dir/answer" ]]; then cat "\$dir/answer"; exit 3; fi
    if [[ "\${1:-}" == etude-crash-screen.service ]]; then
        if [[ -e "\$dir/crash-screen-up" ]]; then echo active; else echo inactive; exit 3; fi
    elif [[ "\${1:-}" == etude.service ]] && alive; then echo active
    elif [[ "\${1:-}" == etude.service && -e "\$dir/failed" ]]; then echo failed; exit 3
    else echo inactive; exit 3; fi
    ;;
stop)
    [[ -e "\$dir/stuck" ]] || stop_all
    if [[ -e "\$dir/term-after-stop" ]]; then rm -f "\$dir/term-after-stop"; kill -TERM "\$PPID"; fi
    ;;
kill) [[ -e "\$dir/unkillable" ]] || stop_all ;;
start|restart)
    for last; do :; done
    # The crash screen, started by the helper after a trial that crashed: a marker, not the app.
    if [[ "\$last" == etude-crash-screen.service ]]; then touch "\$dir/crash-screen-up"; exit 0; fi
    cut
    [[ "\$1" == restart ]] && stop_all
    if [[ "\$1" == restart ]] || ! alive; then start_it; fi
    ;;
show) if [[ -e "\$dir/show-env" ]]; then cat "\$dir/show-env"; else echo "SDL_VIDEO_DRIVER=kmsdrm"; fi ;;
*) exit 0 ;;
esac
SYSTEMCTL
chmod 0755 "$WORK/systemctl"

# make_version DIR VERSION BEHAVIOUR: a folder fit to be a version whose app behaves as asked:
#   ok       migrates the database forward, then says ready (through the app's own ReadyGate)
#   hang     migrates it and never reaches a first frame
#   crash    exits at once, failing
#   dbfail   shows its first frame but its history cannot migrate: it never says ready
#   plant    as hang, and plants links at the restore paths of its own data (a hostile app)
#   deaf     as hang, and ignores SIGTERM (a stop of it hangs until it is killed)
#   segv     dies of a SEGV at once (a shell reports 139)
#   crash-once   exits at once, failing, the first time it is started (then it behaves as ok)
#   pings    as ok, and answers the unit's watchdog for ever (the app's own ping code, from its loop)
#   stalls   as ok, answers the watchdog for 3 s and then hangs
make_version() {
    local dir="$1" version="$2" behaviour="$3"
    mkdir -p "$dir/bin" "$dir/share/etude"
    : >"$dir/share/etude/etude-piano.sf2"
    printf '%s\n' "$behaviour" >"$dir/behaviour"
    cat >"$dir/bin/etude" <<APP
#!/usr/bin/env bash
V=$version
here="\$(cd "\$(dirname "\$0")/.." && pwd -P)"
if [[ "\${1:-}" == --version ]]; then echo "etude \$V"; exit 0; fi
behaviour="\$(cat "\$here/behaviour")"
if [[ "\$*" == *--crash-screen* ]]; then
    # The crash screen (a stand-in): it is up until "Restart" is pressed (the test makes the file), then it
    # exits 0 and systemd starts the app (OnSuccess=); or it crashes, when told to.
    echo "\$V" >>"$SVC/crash-screen-started"
    [[ -e "$SVC/crash-screen-crashes" ]] && exit 1
    trap 'exit 1' TERM # stopped from outside (not Restart): the real screen exits 1, so nothing starts the app
    while [[ ! -s "$SVC/press-restart" ]]; do sleep 0.2; done
    : >"$SVC/press-restart"
    exit 0
fi
echo "\$V" >>"$SVC/started"
date +%s.%N >>"$SVC/started-at"
if [[ "\$behaviour" == crash-once && ! -s "$SVC/crashed-once" ]]; then
    echo 1 >"$SVC/crashed-once"
    exit 1
fi
have="\$(sed -n 's/^schema=//p' "$DATA/history.db" 2>/dev/null | head -n 1)"
if [[ -n "\$have" && "\$(printf '%s\n%s\n' "\$have" "\$V" | sort -V | tail -n 1)" != "\$V" ]]; then
    echo "the database is from a newer version" >&2
    exit 3
fi
[[ "\$behaviour" == crash ]] && exit 1
[[ "\$behaviour" == segv ]] && kill -SEGV \$\$
echo "schema=\$V" >"$DATA/history.db"
echo "wal of \$V" >"$DATA/history.db-wal"
echo "shm of \$V" >"$DATA/history.db-shm"
case "\$behaviour" in
ok|crash-once) exec "$WORK/ready-probe" --update-dir "$PREFIX/update" --version "\$V" --history ok ;;
dbfail) exec "$WORK/ready-probe" --update-dir "$PREFIX/update" --version "\$V" --history failed ;;
pings) exec "$WORK/ready-probe" --update-dir "$PREFIX/update" --version "\$V" --history ok --pings-for -1 ;;
stalls) exec "$WORK/ready-probe" --update-dir "$PREFIX/update" --version "\$V" --history ok --pings-for 3 ;;
plant)
    rm -f "$DATA/history.db" "$DATA/.settings.conf.restore"
    ln -s "$WORK/decoy/planted-db" "$DATA/history.db"
    ln -s "$WORK/decoy/planted-temp" "$DATA/.settings.conf.restore"
    exec sleep 600
    ;;
deaf) trap '' TERM; while :; do sleep 1; done ;;
*) exec sleep 600 ;;
esac
APP
    chmod 0755 "$dir/bin/etude"
}

# make_release VERSION BEHAVIOUR OUT [COMMENT] [KEY]: a signed tarball of a version folder, as the
# release workflow makes one: a top folder etude-VERSION/, the signature with its trusted comment.
make_release() {
    local version="$1" behaviour="$2" out="$3" comment="${4:-etude $1 trixie $ARCH}" key="${5:-good}"
    rm -rf "$WORK/pack"
    mkdir -p "$WORK/pack"
    make_version "$WORK/pack/etude-$version" "$version" "$behaviour"
    : >"$WORK/pack/etude-$version/LICENSE"
    [[ -z "${PACK_HOOK:-}" ]] || "$PACK_HOOK" "$WORK/pack/etude-$version"
    tar -C "$WORK/pack" --owner=0 --group=0 --numeric-owner -cf - "etude-$version" | gzip -n >"$out"
    # ${SIGN_FLAGS:-}: `-l` makes the old kind of signature (Ed), which signs the bytes and not their hash.
    # shellcheck disable=SC2086
    minisign -S ${SIGN_FLAGS:-} -s "$WORK/keys/$key.sec" -x "$out.minisig" -t "$comment" -m "$out" >/dev/null 2>&1
}

# --- helpers of the tests ----------------------------------------------------------------------

status() { sed -n "s/^$1=//p" "$PREFIX/update/status" 2>/dev/null | head -n 1; }
current() { readlink "$PREFIX/current"; }
state_phase() { # none when there is no state file yet (a helper cut before it wrote any)
    local phase
    phase="$(sed -n 's/^phase=//p' "$STATE/state" 2>/dev/null | head -n 1)"
    echo "${phase:-none}"
}
versions() { (cd "$PREFIX/versions" && ls -A | tr '\n' ' ' | sed 's/ $//'); }
last_started() { tail -n 1 "$SVC/started" 2>/dev/null; }
ready_version() { sed -n 's/^version=//p' "$PREFIX/update/ready" 2>/dev/null | head -n 1; }
alive() { "$WORK/systemctl" is-active etude.service >/dev/null; }
wait_until() { # wait_until SECONDS COMMAND...
    local n="$1"
    shift
    for ((i = 0; i < n * 10; i++)); do
        "$@" >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    return 1
}
ready_is() { [[ "$(ready_version)" == "$1" ]]; }
phase_is() { [[ "$(state_phase)" == "$1" ]]; }
status_is() { [[ "$(status state)" == "$1" ]]; }

# as_helper MODE: runs the helper as the unit would, root with the settings file, bounded.
as_helper() {
    timeout "${HELPER_TIMEOUT:-300}" "$HELPER" --config "$CONF" "$1" >"$WORK/helper.log" 2>&1
    return $?
}

# baseline: 0.1.0 installed and running (ready), its data on disk, nothing pending.
baseline() {
    "$WORK/systemctl" stop etude.service
    umount "$DATA" 2>/dev/null
    rm -rf "$PREFIX" "$STATE" "$SVC"/* "$DATA" "$WORK/decoy"/* "$WORK/pack"
    : >"$SVC/started"
    chmod 0666 "$SVC/started"
    mkdir -p "$PREFIX/versions" "$PREFIX/update" "$DATA" "$WORK/pack"
    chmod 0755 "$PREFIX" "$PREFIX/versions"
    chown etude: "$PREFIX/update"
    chown -R etude: /home/etude
    make_version "$PREFIX/versions/0.1.0" 0.1.0 ok
    chown -R root:root "$PREFIX/versions"
    ln -s versions/0.1.0 "$PREFIX/current"
    echo "schema=0.1.0" >"$DATA/history.db"
    echo "pins" >"$DATA/settings.conf"
    chown -R etude: /home/etude
    write_conf
    "$WORK/systemctl" start etude.service
    wait_until 20 ready_is 0.1.0 || flunk "the baseline app (0.1.0) did not say ready"
    : >"$SVC/calls"
}

# leave VERSION BEHAVIOUR [COMMENT] [KEY]: what the app leaves: a signed tarball, its signature and the
# request, all the app's own files.
leave() {
    local version="$1" behaviour="$2"
    make_release "$version" "$behaviour" "$WORK/release.tar.gz" "${3:-etude $1 trixie $ARCH}" "${4:-good}"
    cp "$WORK/release.tar.gz" "$PREFIX/update/release.tar.gz"
    cp "$WORK/release.tar.gz.minisig" "$PREFIX/update/release.tar.gz.minisig"
    printf 'version=%s\n' "$version" >"$PREFIX/update/request"
    chown etude: "$PREFIX/update/release.tar.gz" "$PREFIX/update/release.tar.gz.minisig" "$PREFIX/update/request"
    rm -f "$PREFIX/update/status"
}

# refused DESCRIPTION: the run said failed, nothing was switched and the old app still runs.
refused() {
    expect_equal "$1: the status says failed" "failed" "$(status state)"
    expect_equal "$1: current did not move" "versions/0.1.0" "$(current)"
    expect "$1: the app still runs" alive
    expect_equal "$1: nothing is on trial" "" "$(state_phase | grep -v '^none$')"
    expect "$1: the request is gone from the app's folder" test ! -e "$PREFIX/update/request"
}

# --- 1. units and settings ---------------------------------------------------------------------

echo "== the units, the settings and what setup.sh writes"
UNITS=$WORK/units
mkdir -p "$UNITS"
for unit in etude-update.service etude-update.path etude-update-resume.service etude.service etude-labwc.service etude-crash-screen.service etude-labwc-crash-screen.service; do
    sed -e "s|@PREFIX@|/opt/etude|g" -e "s|@DATA_DIR@|$DATA|g" "$REPO/deploy/pi/$unit" >"$UNITS/$unit"
done
# The sandbox of the two update units is one drop-in each, rendered from the one list.
for unit in etude-update.service etude-update-resume.service; do
    mkdir -p "$UNITS/$unit.d"
    sed -e "s|@PREFIX@|/opt/etude|g" -e "s|@APP_HOME@|/home/etude|g" "$REPO/deploy/pi/etude-update-sandbox.conf" >"$UNITS/$unit.d/sandbox.conf"
done
mkdir -p /opt/etude
touch /etc/etude-update.conf
verify_out="$(cd "$UNITS" && systemd-analyze verify ./etude-update.service ./etude-update.path ./etude-update-resume.service ./etude.service ./etude-labwc.service ./etude-crash-screen.service ./etude-labwc-crash-screen.service 2>&1 || true)"
if printf '%s' "$verify_out" | grep -qiE "unknown (key|section|lvalue)|invalid|failed to parse|bad (unit|setting)|unknown assignment"; then
    flunk "systemd-analyze verify finds the units well formed: $verify_out"
else
    pass "systemd-analyze verify finds the units well formed"
fi
for must in "ProtectSystem=strict" "ProtectHome=read-only" "NoNewPrivileges=yes" "RestrictSUIDSGID=yes" "PrivateTmp=yes" "ProtectKernelTunables=yes" "ReadWritePaths=/opt/etude /var/lib/etude-update -/home/etude"; do
    expect "the sandbox of etude-update.service has $must" grep -qxF "$must" "$UNITS/etude-update.service.d/sandbox.conf"
done
expect "the update service is not sandboxed away from systemctl's socket (AF_UNIX stays)" grep -qxF "RestrictAddressFamilies=AF_UNIX" "$UNITS/etude-update.service.d/sandbox.conf"
expect "the boot unit has the very same sandbox" cmp "$UNITS/etude-update.service.d/sandbox.conf" "$UNITS/etude-update-resume.service.d/sandbox.conf"
expect "neither update unit carries a sandbox list of its own" test "$(grep -c '^ProtectSystem=' "$UNITS/etude-update.service" "$UNITS/etude-update-resume.service" | grep -vc ':0$')" = 0
expect "the update service's time limit covers the longest run (2401 s, 3201 at the settings' maxima)" grep -qxF "TimeoutStartSec=3600" "$UNITS/etude-update.service"
expect "and a stop of it covers the short going back (996 s)" grep -qxF "TimeoutStopSec=1200" "$UNITS/etude-update.service"
for unit in etude.service etude-labwc.service; do
    expect "$unit does not restart by itself: Restart=no, and none of the backoff or limit lines" test "$(grep -cxE 'Restart=no' "$UNITS/$unit")" = 1 -a "$(grep -cE '^(Restart=on|RestartSec|RestartSteps|RestartMaxDelaySec|StartLimitBurst)' "$UNITS/$unit")" = 0
    expect "$unit refuses no start of the player's: no start limit" grep -qxF "StartLimitIntervalSec=0" "$UNITS/$unit"
done
expect "etude.service starts the crash screen when it fails" grep -qxF "OnFailure=etude-crash-screen.service" "$UNITS/etude.service"
expect "etude-labwc.service starts its crash screen when it fails" grep -qxF "OnFailure=etude-labwc-crash-screen.service" "$UNITS/etude-labwc.service"
# No unit names the prefix by hand: setup.sh renders every one for the prefix it was given.
for unit in etude.service etude-labwc.service etude-crash-screen.service etude-labwc-crash-screen.service; do
    expect "$unit has no hard-coded /opt/etude (it says @PREFIX@)" test "$(grep -v '^#' "$REPO/deploy/pi/$unit" | grep -c '/opt/etude')" = 0
    expect "$unit renders for another prefix" test "$(sed "s|@PREFIX@|/srv/kiosk|g" "$REPO/deploy/pi/$unit" | grep -c '^ExecStart=.*/srv/kiosk/current/bin/etude')" = 1
done
for unit in etude-crash-screen.service etude-labwc-crash-screen.service; do
    expect "$unit runs the app with --crash-screen" grep -q "^ExecStart=.*etude --crash-screen" "$UNITS/$unit"
    expect "$unit stops when the screen does: no Restart, no OnFailure, no Install" test "$(grep -cE '^(Restart=on|OnFailure=|\[Install\])' "$UNITS/$unit")" = 0
    expect "$unit goes back from a version on trial that crashed before it shows (crashed, in the sandbox)" grep -qxF "ExecStartPre=+/usr/local/lib/etude/etude-update-helper sandboxed crashed" "$UNITS/$unit"
    expect "$unit does not start while the helper is acting on the app (idle, in the sandbox)" grep -qxF "ExecCondition=+/usr/local/lib/etude/etude-update-helper sandboxed idle" "$UNITS/$unit"
    expect "$unit starts the app when the screen exits 0 (Restart, Try again)" grep -qE '^OnSuccess=etude(-labwc)?\.service$' "$UNITS/$unit"
    expect "$unit has nothing of the old safe start left in it" test "$(grep -c 'safe' "$UNITS/$unit")" = 0
done
for unit in etude-labwc.service etude-labwc-crash-screen.service; do
    expect "$unit runs the app through the session script, which passes its exit status on (labwc ends 0 whatever it did)" grep -qE '^ExecStart=/usr/local/lib/etude/etude-labwc-session /opt/etude/current/bin/etude' "$UNITS/$unit"
    expect "$unit never starts labwc by itself: a crash would end the unit well" test "$(grep -c '^ExecStart=/usr/bin/labwc' "$UNITS/$unit")" = 0
    expect "$unit has a runtime folder of its own for the status the script writes" grep -qE '^RuntimeDirectory=etude-labwc' "$UNITS/$unit"
done
expect "the session script is executable" test -x "$REPO/deploy/pi/etude-labwc-session"
expect "setup.sh installs the session script where the labwc units look for it, as root's file" grep -qE '^install_root_file "\$SCRIPT_DIR/etude-labwc-session" "\$LABWC_SESSION" 0755' "$REPO/deploy/pi/setup.sh"
expect "  and the path it installs to is the one the units name" grep -qxF "LABWC_SESSION=/usr/local/lib/etude/etude-labwc-session" "$REPO/deploy/pi/setup.sh"
expect "the path unit watches the app's folder for the request" grep -qxF "PathExists=/opt/etude/update/request" "$UNITS/etude-update.path"
for unit in etude.service etude-labwc.service; do
    expect "$unit counts its starts through the helper in the sandbox (as root, and a failure stops the start)" grep -qxF "ExecStartPre=+/usr/local/lib/etude/etude-update-helper sandboxed count" "$UNITS/$unit"
    expect "$unit gives the count a time limit that covers a restore" grep -qxF "TimeoutStartSec=600" "$UNITS/$unit"
done
expect "the boot unit resumes an interrupted update" grep -qxF "ExecStart=/usr/local/lib/etude/etude-update-helper resume" "$UNITS/etude-update-resume.service"
expect "setup.sh writes the prefix quoted, as data" grep -q 'PREFIX="%s"' "$REPO/deploy/pi/setup.sh"
expect "setup.sh no longer sources a settings file" test "$(grep -c '^\. \|^source /etc/etude' "$REPO/deploy/pi/setup.sh")" = 0
rm -f /etc/etude-update.conf

echo "== the app user's home that the helper's sandbox may write (setup.sh refuses the others)"
refused_home() { # refused_home DESCRIPTION HOME USER WORDS: the check says no, and why
    local out rc
    out="$(check_app_home "$2" "$3" 2>&1)"
    rc=$?
    expect_equal "$1: refused" "1" "$rc"
    expect_contains "  and says why" "$4" "$out"
}
expect_equal "the user's own home passes, resolved" "/home/etude" "$(check_app_home /home/etude etude 2>&1)"
ln -sfn /home/etude /home/uh-fine-link
expect_equal "a link to a good home resolves to it" "/home/etude" "$(check_app_home /home/uh-fine-link etude 2>&1)"
mkdir -p /tmp/uh-linked/h
chown etude: /tmp/uh-linked/h
ln -sfn /tmp/uh-linked/h /home/uh-bad-link
refused_home "a home that is a link into a folder others can write" /home/uh-bad-link etude "above the home /tmp/uh-linked/h"
mkdir -p /home/uh-open/h
chmod 0777 /home/uh-open
chown etude: /home/uh-open/h
refused_home "a home below a folder the app could write" /home/uh-open/h etude "/home/uh-open above the home"
mkdir -p /home/uh-notmine
refused_home "a home that is root's, not the user's" /home/uh-notmine etude "belongs to root"
refused_home "a home that does not exist" /home/uh-nowhere etude "does not exist"
id uh-varuser >/dev/null 2>&1 || useradd --no-create-home -d /var -s /usr/sbin/nologin uh-varuser
refused_home "an existing app user whose home is /var" "$(getent passwd uh-varuser | cut -d: -f6)" uh-varuser "a system folder"
refused_home "a home that is /" / etude "a system folder"
refused_home "a home under /usr" /usr/lib etude "a system folder"
refused_home "root's own home" /root etude "a system folder"
rm -rf /home/uh-fine-link /home/uh-bad-link /tmp/uh-linked /home/uh-open /home/uh-notmine
userdel uh-varuser 2>/dev/null

echo "== the helper refuses settings that are not data, or not root's"
baseline
expect "the helper runs with good settings" "$HELPER" --config "$CONF" count
cp "$CONF" "$WORK/conf-etude"
chown etude: "$WORK/conf-etude"
"$HELPER" --config "$WORK/conf-etude" apply >"$WORK/out" 2>&1; rc=$?
expect_equal "settings owned by the app's user: apply refuses (exit 2)" "2" "$rc"
expect_contains "  and it says why" "root's" "$(cat "$WORK/out")"
cp "$CONF" "$WORK/conf-group"
chmod 0664 "$WORK/conf-group"
"$HELPER" --config "$WORK/conf-group" apply >"$WORK/out" 2>&1
expect_equal "settings a group can write: refused" "2" "$?"
cp "$CONF" "$WORK/conf-world"
chmod 0666 "$WORK/conf-world"
"$HELPER" --config "$WORK/conf-world" apply >"$WORK/out" 2>&1
expect_equal "settings anyone can write: refused" "2" "$?"
ln -s "$CONF" "$WORK/conf-link"
"$HELPER" --config "$WORK/conf-link" apply >"$WORK/out" 2>&1
expect_equal "settings that are a link: refused" "2" "$?"
printf 'PREFIX=$(touch %s/pwned)\nAPP_USER=etude\n' "$WORK" >"$WORK/conf-shell"
chmod 0644 "$WORK/conf-shell"
"$HELPER" --config "$WORK/conf-shell" apply >"$WORK/out" 2>&1
expect_equal "settings with shell in them: refused" "2" "$?"
expect "  and nothing was run" test ! -e "$WORK/pwned"
printf 'PREFIX="/opt/x"\nEVIL=1\n' >"$WORK/conf-key"
"$HELPER" --config "$WORK/conf-key" apply >"$WORK/out" 2>&1
expect_equal "a setting that is not one: refused" "2" "$?"
printf 'PREFIX="/opt/x"\nXDG_DATA_HOME="/srv/data"\n' >"$WORK/conf-xdg"
"$HELPER" --config "$WORK/conf-xdg" apply >"$WORK/out" 2>&1
expect_equal "XDG_DATA_HOME is not a setting any more (the service's environment is the one source): refused" "2" "$?"
printf 'PREFIX="/opt/x"\nAPP_USER="root"\n' >"$WORK/conf-root"
"$HELPER" --config "$WORK/conf-root" apply >"$WORK/out" 2>&1
expect_equal "the app's user may not be root: refused" "2" "$?"

# What gates a start of the app never keeps it from starting because of the helper's own trouble: `count`
# (and `crashed`, and `idle`) end 0 and say why in the journal; only `apply` ends 2.
for mode in count crashed idle; do
    "$HELPER" --config "$WORK/conf-world" "$mode" >"$WORK/out" 2>&1
    expect_equal "$mode with settings anyone can write: ends 0, the app is not kept from starting" "0" "$?"
    expect_contains "  and says it refused" "refusing to run" "$(cat "$WORK/out")"
done
printf 'PREFIX="/opt/x"\nAPP_USER="nobody-by-that-name"\n' >"$WORK/conf-nouser"
chmod 0644 "$WORK/conf-nouser"
"$HELPER" --config "$WORK/conf-nouser" count >"$WORK/out" 2>&1
expect_equal "count with an app user that does not exist: ends 0" "0" "$?"
"$HELPER" --config "$WORK/conf-nouser" apply >"$WORK/out" 2>&1
expect_equal "  while apply refuses (exit 2)" "2" "$?"
# idle ends 0 on the helper's own trouble: a state folder that is not root's alone must not keep the crash screen out.
mkdir -p "$WORK/idle-state"
chown etude: "$WORK/idle-state"
chmod 0755 "$WORK/idle-state"
printf 'PREFIX="/opt/x"\nAPP_USER="etude"\nSTATE_DIR="%s"\n' "$WORK/idle-state" >"$WORK/conf-idle"
chmod 0644 "$WORK/conf-idle"
"$HELPER" --config "$WORK/conf-idle" idle >"$WORK/out" 2>&1
expect_equal "idle with a state folder that is not root's: ends 0 (its own trouble)" "0" "$?"
expect_contains "  and says so" "idle:" "$(cat "$WORK/out")"
# setup.sh sets the owner and mode of an existing state folder instead of keeping them.
snippet="$(sed -n '/^# The helper refuses a state folder that is not root/,/^fi$/p' "$REPO/deploy/pi/setup.sh")"
expect "setup.sh has the state-folder step" test -n "$snippet"
UPDATE_STATE="$WORK/idle-state"
eval "$snippet"
expect_equal "  an existing state folder is made root's, mode 0700" "root:700" "$(stat -c '%U:%a' "$WORK/idle-state")"

# --- 2. install and migration ------------------------------------------------------------------

echo "== install into versions/, an older install moved into it"
BUILD=$WORK/build
mkdir -p "$BUILD/app/verovio-data" "$BUILD/tools"
make_version "$WORK/built" 0.1.0 ok
cp "$WORK/built/bin/etude" "$BUILD/app/etude"
cp "$WORK/built/bin/etude" "$BUILD/tools/display-spike"
cp "$WORK/built/bin/etude" "$BUILD/tools/midi-ports"
: >"$BUILD/app/etude-piano.sf2"
echo font >"$BUILD/app/verovio-data/font"
OLDP=$WORK/instprefix
mkdir -p "$OLDP"
install_app_tree "$BUILD" "$OLDP/versions/0.1.0" >/dev/null
point_current "$OLDP" 0.1.0 >/dev/null
expect_equal "current points at versions/0.1.0 (a relative link)" "versions/0.1.0" "$(readlink "$OLDP/current")"
expect "the app is in versions/0.1.0/bin" test -x "$OLDP/versions/0.1.0/bin/etude"
expect "its samples and fonts are under share/etude" test -f "$OLDP/versions/0.1.0/share/etude/verovio-data/font"
point_current "$OLDP" 0.1.0 | grep -q "already points" && pass "pointing again changes nothing" || flunk "pointing again changes nothing"
expect_equal "the project version is read from CMakeLists.txt" "$(sed -n 's/^project(etude VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")" "$(project_version "$REPO")"

OLD=$WORK/old
mkdir -p "$OLD/bin" "$OLD/share/etude" "$OLD/versions"
cat >"$OLD/bin/etude" <<'OLDAPP'
#!/usr/bin/env bash
id -u >/tmp/uh/open/old-binary-ran-as
echo "etude 0.0.9"
OLDAPP
chmod 0755 "$OLD/bin/etude"
cp "$OLD/bin/etude" "$OLD/bin/display-spike"
echo samples >"$OLD/share/etude/etude-piano.sf2"
chmod 0755 "$OLD" "$OLD/versions"
migrate_old_layout "$OLD" etude >/dev/null
expect_equal "current points at the version the old binary reported" "versions/0.0.9" "$(readlink "$OLD/current")"
expect "bin/ moved into the version" test -x "$OLD/versions/0.0.9/bin/display-spike"
expect "share/ moved into the version" test -f "$OLD/versions/0.0.9/share/etude/etude-piano.sf2"
expect "nothing is left at the old places" test ! -e "$OLD/bin" -a ! -e "$OLD/share"
expect "no temporary folder or version file is left" test ! -e "$OLD/versions/.migrate.new" -a ! -e "$OLD/versions/0.0.9/.version"
expect_equal "the old binary was asked as the app's user, never as root" "$(id -u etude)" "$(cat "$WORK/open/old-binary-ran-as" 2>/dev/null)"
migrate_old_layout "$OLD" etude | grep -q "nothing to move" && pass "a second run moves nothing" || flunk "a second run moves nothing"

# An install from before the crash screen: the safe-mode units (and a drop-in of one) and the marker in the
# data folder are removed; the units that stay, and what a link in the marker's place points at, are not.
OLDU=$WORK/oldunits
OLDD=$WORK/olddata
mkdir -p "$OLDU/etude-labwc-safe-mode.service.d" "$OLDD"
for unit in etude-safe-mode.service etude-labwc-safe-mode.service etude.service etude-crash-screen.service; do
    printf '[Unit]\nDescription=%s\n' "$unit" >"$OLDU/$unit"
done
: >"$OLDU/etude-labwc-safe-mode.service.d/x.conf"
: >"$OLDD/safe-mode"
: >"$OLDD/settings.conf"
remove_safe_mode_leftovers "$OLDU" "$OLDD" >/dev/null
expect "the old safe-mode unit is removed" test ! -e "$OLDU/etude-safe-mode.service"
expect "the old labwc safe-mode unit and its drop-in are removed" test ! -e "$OLDU/etude-labwc-safe-mode.service" -a ! -e "$OLDU/etude-labwc-safe-mode.service.d"
expect_equal "  and the caller is told to reload systemd" "1" "$REMOVED"
expect "the units that stay are kept" test -f "$OLDU/etude.service" -a -f "$OLDU/etude-crash-screen.service"
expect "the old safe-mode marker is deleted, the settings beside it are not" test ! -e "$OLDD/safe-mode" -a -f "$OLDD/settings.conf"
remove_safe_mode_leftovers "$OLDU" "$OLDD" >/dev/null
expect_equal "a second run removes nothing (nothing to reload)" "0" "$REMOVED"
echo keep >"$WORK/marker-target"
ln -s "$WORK/marker-target" "$OLDD/safe-mode"
remove_safe_mode_leftovers "$OLDU" "$OLDD" >/dev/null
expect "a link in the marker's place goes, and what it names is not touched" test ! -e "$OLDD/safe-mode" -a "$(cat "$WORK/marker-target")" = keep

# A run cut short after the version was noted and bin/ was moved: the next run finishes it.
CUT=$WORK/cut
mkdir -p "$CUT/versions/.migrate.new" "$CUT/share/etude"
mv "$OLD/versions/0.0.9/bin" "$CUT/versions/.migrate.new/bin"
echo "0.0.7" >"$CUT/versions/.migrate.new/.version"
echo samples >"$CUT/share/etude/x"
migrate_old_layout "$CUT" etude >/dev/null
expect_equal "an interrupted migration is finished with the version it had noted" "versions/0.0.7" "$(readlink "$CUT/current")"
expect "both parts are in the version" test -x "$CUT/versions/0.0.7/bin/etude" -a -f "$CUT/versions/0.0.7/share/etude/x"
OLD2=$WORK/old2
mkdir -p "$OLD2/bin" "$OLD2/versions"
printf '#!/bin/sh\necho what\n' >"$OLD2/bin/etude"
chmod 0755 "$OLD2/bin/etude" "$OLD2" "$OLD2/versions"
migrate_old_layout "$OLD2" etude >/dev/null
expect_equal "a binary that does not report X.Y.Z is filed under 0.0.0" "versions/0.0.0" "$(readlink "$OLD2/current")"

# --- 3. an update ------------------------------------------------------------------------------

echo "== an update"
baseline
cp "$PREFIX/update/ready" "$WORK/ready-0.1.0" 2>/dev/null
leave 0.2.0 ok
as_helper apply
expect_equal "the run succeeded" "0" "$?"
expect_equal "the status says ok" "ok" "$(status state)"
expect_equal "current points at the new version" "versions/0.2.0" "$(current)"
expect_equal "previous points at the old version" "versions/0.1.0" "$(readlink "$PREFIX/previous")"
expect "the new version says ready, through the app's own code" wait_until 10 ready_is 0.2.0
expect_equal "it was the new version that ran" "0.2.0" "$(last_started)"
expect "nothing is on trial any more" test "$(state_phase)" = none
expect "the request and the files the app left are gone" test ! -e "$PREFIX/update/request" -a ! -e "$PREFIX/update/release.tar.gz" -a ! -e "$PREFIX/update/release.tar.gz.minisig"
expect_equal "the new version folder is root's" "root" "$(stat -c %U "$PREFIX/versions/0.2.0/bin/etude")"
expect_equal "and not writable by others" "755" "$(stat -c %a "$PREFIX/versions/0.2.0")"
expect_equal "the old data is backed up" "schema=0.1.0" "$(cat "$DATA/update-backup/0.1.0/history.db")"
expect "the settings are backed up" test -f "$DATA/update-backup/0.1.0/settings.conf"
expect_equal "the backup is the app user's" "etude" "$(stat -c %U "$DATA/update-backup/0.1.0/history.db")"
expect_equal "the service was stopped before the new version started" "stop" "$(grep -m1 -oE '^stop|^start|^restart' "$SVC/calls")"
expect_equal "both versions are kept" "0.1.0 0.2.0" "$(versions)"
expect_equal "the helper's state folder is root's alone" "700" "$(stat -c %a "$STATE")"
leave 0.3.0 ok
as_helper apply
expect_equal "a second update works" "versions/0.3.0" "$(current)"
expect_equal "only two versions are kept" "0.2.0 0.3.0" "$(versions)"
expect_equal "the backup is of the version before" "schema=0.2.0" "$(cat "$DATA/update-backup/0.2.0/history.db")"
expect "the first backup was replaced" test ! -e "$DATA/update-backup/0.1.0"

# --- 4. an update that does not start ---------------------------------------------------------

echo "== an update that never reaches its first frame goes back after two starts"
baseline
leave 0.2.0 hang
as_helper apply
expect_equal "the status says rolled-back" "rolled-back" "$(status state)"
expect_equal "the status names the version that failed" "0.2.0" "$(status version)"
expect_equal "current is back at the old version" "versions/0.1.0" "$(current)"
expect "the old version runs again and says ready" wait_until 15 ready_is 0.1.0
expect "the failed version is removed" test ! -e "$PREFIX/versions/0.2.0"
expect_equal "the database is the old version's again, restored from the backup" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect_equal "and its WAL file" "wal of 0.1.0" "$(cat "$DATA/history.db-wal")"
expect "the new version's shared-memory file is not left beside it" test "$(cat "$DATA/history.db-shm" 2>/dev/null)" != "shm of 0.2.0"
expect_equal "the new version was started twice, no more" "2" "$(grep -c '^0.2.0$' "$SVC/started")"
expect_equal "nothing is on trial" "none" "$(state_phase)"
expect_contains "the reason says how many starts" "starts" "$(status reason)"

echo "== an update that crashes at once is rolled back at once, and the crash screen shows, not the app"
baseline
leave 0.2.0 crash
as_helper apply
expect_equal "a crashing version is rolled back too" "rolled-back" "$(status state)"
expect_equal "and the old version is current" "versions/0.1.0" "$(current)"
expect_equal "the data is the old version's" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect_equal "the crashing version was started once: nothing restarts it by itself" "1" "$(grep -c '^0.2.0$' "$SVC/started")"
expect "the crash screen is up" test -e "$SVC/crash-screen-up"
expect "the old version is not started on its own" test ! -e "$SVC/app.pid"
expect_equal "nothing is on trial" "none" "$(state_phase)"
expect "the status carries no data reason (the data is back)" test -z "$(status data)"
expect "Restart (the player's start of the app) runs the old version, which says ready" "$WORK/systemctl" start etude.service
expect "  and it says ready" wait_until 15 ready_is 0.1.0

echo "== a history that cannot migrate counts as a failed start"
baseline
leave 0.2.0 dbfail
as_helper apply
expect_equal "the first frame alone does not make a start good: rolled back" "rolled-back" "$(status state)"
expect "no ready file of the new version was written" test "$(ready_version)" != 0.2.0
expect_equal "the old version is current" "versions/0.1.0" "$(current)"
expect_equal "the data is restored" "schema=0.1.0" "$(cat "$DATA/history.db")"

echo "== a hostile new app plants links at the restore paths; the restore replaces them"
baseline
echo "root only" >/root/uh-secret
chmod 0600 /root/uh-secret
leave 0.2.0 plant
as_helper apply
expect_equal "it was rolled back" "rolled-back" "$(status state)"
expect_equal "the data is restored through no link" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "history.db is a file again, not a link" test ! -L "$DATA/history.db"
expect "the restore wrote nothing through the link at a temporary name" test ! -e "$WORK/decoy/planted-temp"
expect "the restore wrote the old data through no link (what is there is the hostile app's own write)" test "$(cat "$WORK/decoy/planted-db" 2>/dev/null)" != "schema=0.1.0"
expect "the secret is untouched" test "$(cat /root/uh-secret)" = "root only"

# --- 5. a reboot or a power cut ----------------------------------------------------------------

power_cut() { # kills the service and everything of the update, as a cut does; the disk stays as it is
    rm -f "$SVC/cut-on-start"
    pkill -9 -f "$WORK/svc-run" 2>/dev/null
    pkill -9 -f "etude-update-helper" 2>/dev/null
    pkill -9 -f "versions/.*/bin/etude" 2>/dev/null
    pkill -9 -f "ready-probe" 2>/dev/null
    pkill -9 -f "^sleep 600" 2>/dev/null
    sleep 0.3
    rm -f "$SVC/super.pid" "$SVC/app.pid" "$SVC/stopping"
}
boot() { # the boot path: the resume unit and the service, started at once as systemd would
    ( timeout 120 "$HELPER" --config "$CONF" resume >"$WORK/resume.log" 2>&1 & )
    "$WORK/systemctl" start etude.service
}
settled() { [[ "$(state_phase)" == none ]] && alive; }

echo "== a power cut right when the new version is to be started, then the boot path"
baseline
leave 0.2.0 ok
touch "$SVC/cut-on-start"
as_helper apply
expect "the helper was cut off (a signal, not a normal end)" test "$?" -ge 128
expect_equal "current was already pointed at the new version" "versions/0.2.0" "$(current)"
expect_equal "and the state says it is on trial" "pending" "$(state_phase)"
power_cut
boot
expect "the new version, started at boot, says ready and is confirmed" wait_until 40 settled
expect_equal "the new version is current" "versions/0.2.0" "$(current)"
expect "the status says ok" test "$(status state)" = ok

echo "== a power cut, then a boot of a version that never says ready: back after two starts, across the reboot"
baseline
leave 0.2.0 hang
touch "$SVC/cut-on-start"
as_helper apply
power_cut
boot
expect "the boot path settles it by going back" wait_until 60 settled
expect_equal "current is the old version" "versions/0.1.0" "$(current)"
expect_equal "the data is the old version's" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "the old version says ready" wait_until 20 ready_is 0.1.0

echo "== a power cut in the middle of going back is finished at the next start"
baseline
leave 0.2.0 hang
as_helper apply
# Rolled back by now. Make it look as if the cut came after the links went back but before the data did.
cat >"$STATE/state" <<STATE
phase=rolling-back
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=2
backup=yes
reason=0.2.0 did not start
STATE
"$WORK/systemctl" stop etude.service
echo "schema=0.2.0" >"$DATA/history.db"
echo "shm of 0.2.0" >"$DATA/history.db-shm"
echo "half" >"$DATA/.history.db.restore"
chown etude: "$DATA/history.db" "$DATA/history.db-shm" "$DATA/.history.db.restore"
mkdir -p "$PREFIX/versions/0.2.0"
boot
expect "the boot start finishes the going back" wait_until 30 settled
expect_equal "the data is the old version's again" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "the leftover temporary file of the cut restore is gone" test ! -e "$DATA/.history.db.restore"
expect "the new version's shared-memory file is not left beside the old database" test "$(cat "$DATA/history.db-shm" 2>/dev/null)" != "shm of 0.2.0"
expect "the half-made version folder is gone" test ! -e "$PREFIX/versions/0.2.0"
expect "the old version runs" wait_until 20 ready_is 0.1.0

echo "== the helper killed at set times during an update, then the boot path: every cut ends well"
for delay in 0.05 0.3 0.7 1.2 1.8 2.5 3.5 5; do
    baseline
    leave 0.2.0 ok
    "$HELPER" --config "$CONF" apply >"$WORK/helper.log" 2>&1 &
    helper_pid=$!
    sleep "$delay"
    kill -9 "$helper_pid" 2>/dev/null
    wait "$helper_pid" 2>/dev/null
    power_cut
    boot
    if wait_until 60 settled; then
        cur="$(readlink "$PREFIX/current")"
        v="${cur#versions/}"
        okc=1
        [[ -d "$PREFIX/versions/$v" ]] || okc=0
        [[ "$(cat "$DATA/history.db")" == "schema=$v" ]] || [[ "$v" == 0.1.0 && "$(cat "$DATA/history.db")" == "schema=0.1.0" ]] || okc=0
        wait_until 20 ready_is "$v" || okc=0
        ((okc)) && pass "cut after ${delay}s: settled on $v, its data, and it says ready" ||
            flunk "cut after ${delay}s: current=$cur data='$(cat "$DATA/history.db")' ready=$(ready_version)"
    else
        flunk "cut after ${delay}s: never settled (state $(state_phase), current $(readlink "$PREFIX/current"))"
    fi
done

# --- 6. attacks and failures -------------------------------------------------------------------

echo "== a link planted at each backup path: the backup fails, nothing is read through it"
echo "root only" >/root/uh-secret
chmod 0600 /root/uh-secret
for name in history.db history.db-wal history.db-shm settings.conf settings.conf.bak; do
    baseline
    rm -f "$DATA/$name"
    ln -s /root/uh-secret "$DATA/$name"
    chown -h etude: "$DATA/$name"
    leave 0.2.0 ok
    as_helper apply
    refused "a link at $name"
    expect_contains "a link at $name: the status says the data could not be backed up" "backed up" "$(status reason)"
    expect "a link at $name: the secret is in no backup" test -z "$(grep -rl 'root only' "$DATA" 2>/dev/null)"
    expect "a link at $name: the new version's folder was removed" test ! -e "$PREFIX/versions/0.2.0"
    expect "a link at $name: no half-made backup is left" test -z "$(find "$DATA/update-backup" -name '*.new' 2>/dev/null)"
done
baseline
mkdir -p "$DATA/update-backup"
chown etude: "$DATA/update-backup"
ln -s "$WORK/decoy" "$DATA/update-backup/0.1.0.new"
chown -h etude: "$DATA/update-backup/0.1.0.new"
leave 0.2.0 ok
as_helper apply
expect_equal "a link at the backup's temporary name is removed, not followed: the update goes through" "ok" "$(status state)"
expect "  and nothing was written where it pointed" test -z "$(ls -A "$WORK/decoy")"
baseline
rm -rf "$DATA/update-backup"
ln -s "$WORK/decoy" "$DATA/update-backup"
chown -h etude: "$DATA/update-backup"
leave 0.2.0 ok
as_helper apply
refused "a link as the backup folder"
expect "a link as the backup folder: nothing was written where it pointed" test -z "$(ls -A "$WORK/decoy")"

echo "== the status is written over a link, never through it"
baseline
cp "$CONF" "$WORK/conf-before"
rm -f "$PREFIX/update/status"
ln -s "$CONF" "$PREFIX/update/status"
chown -h etude: "$PREFIX/update/status"
printf 'version=0.1.0\n' >"$PREFIX/update/request"
chown etude: "$PREFIX/update/request"
as_helper apply
expect "the settings file was not written through the link" cmp -s "$CONF" "$WORK/conf-before"
expect_equal "the settings are still root's, 0644" "root:644" "$(stat -c %U:%a "$CONF")"
expect "the status is a file again, not a link" test ! -L "$PREFIX/update/status"
expect_equal "and it says failed (0.1.0 is installed)" "failed" "$(status state)"
rm -f "$PREFIX/update/status"
mkdir "$PREFIX/update/status"
chown etude: "$PREFIX/update/status"
printf 'version=0.1.0\n' >"$PREFIX/update/request"
chown etude: "$PREFIX/update/request"
as_helper apply
expect "a status that is a folder is left alone" test -d "$PREFIX/update/status" -a -z "$(ls -A "$PREFIX/update/status")"
expect_equal "and the run still refuses what it should" "versions/0.1.0" "$(current)"
rmdir "$PREFIX/update/status"

echo "== a link where staging used to be has no effect: nothing is taken from the app's folder but two files"
baseline
ln -s "$PREFIX/versions/0.1.0" "$PREFIX/update/staging"
chown -h etude: "$PREFIX/update/staging"
leave 0.2.0 ok
as_helper apply
expect_equal "the update goes through" "ok" "$(status state)"
expect "the running version's folder is intact" test -x "$PREFIX/versions/0.1.0/bin/etude"
expect_equal "and still root's" "root" "$(stat -c %U "$PREFIX/versions/0.1.0/bin/etude")"

echo "== a request or a tarball that is a pipe does not hold the helper"
baseline
mkfifo "$PREFIX/update/request"
chown etude: "$PREFIX/update/request"
HELPER_TIMEOUT=30 as_helper apply
expect_equal "a request that is a pipe: the helper returns (it does not wait for a writer)" "1" "$?"
expect_equal "a request that is a pipe: failed" "failed" "$(status state)"
expect "a request that is a pipe: removed" test ! -e "$PREFIX/update/request"
baseline
leave 0.2.0 ok
rm -f "$PREFIX/update/release.tar.gz"
mkfifo "$PREFIX/update/release.tar.gz"
chown etude: "$PREFIX/update/release.tar.gz"
HELPER_TIMEOUT=30 as_helper apply
expect_equal "a tarball that is a pipe: the helper returns" "1" "$?"
refused "a tarball that is a pipe"
rm -f "$PREFIX/update/release.tar.gz"
baseline
leave 0.2.0 ok
rm -f "$PREFIX/update/release.tar.gz"
ln -s /etc/passwd "$PREFIX/update/release.tar.gz"
chown -h etude: "$PREFIX/update/release.tar.gz"
as_helper apply
refused "a tarball that is a link"
expect_contains "a tarball that is a link: the status says it was refused" "refused" "$(status reason)"
rm -f "$PREFIX/update/release.tar.gz"
baseline
printf 'version=0.2.0\n' >"$PREFIX/update/request"
chown etude: "$PREFIX/update/request"
as_helper apply
refused "no tarball at all"
baseline
leave 0.2.0 ok
printf 'x' >>"$PREFIX/update/release.tar.gz"
chown root:root "$PREFIX/update/release.tar.gz"
as_helper apply
refused "a tarball that is not the app's own file (root's)"

echo "== a tarball with a hard link, a link out, a device or a second top folder"
hardlink_hook() { echo data >"$1/share/etude/a"; ln "$1/share/etude/a" "$1/share/etude/b"; }
PACK_HOOK=hardlink_hook
baseline
leave 0.2.0 ok
as_helper apply
refused "a hard link in the tarball"
expect_contains "a hard link in the tarball: the status says why" "not allowed" "$(status reason)"
unset PACK_HOOK
outlink_hook() { ln -s ../../../../.. "$1/share/etude/up"; }
PACK_HOOK=outlink_hook
baseline
leave 0.2.0 ok
as_helper apply
refused "a link that leaves the folder"
unset PACK_HOOK
chain_hook() { mkdir -p "$1/a"; ln -s .. "$1/a/up"; ln -s a/up/.. "$1/c"; }
PACK_HOOK=chain_hook
baseline
leave 0.2.0 ok
as_helper apply
refused "links that chain out through each other"
unset PACK_HOOK
device_hook() { mknod "$1/share/etude/null" c 1 3; }
PACK_HOOK=device_hook
baseline
leave 0.2.0 ok
as_helper apply
refused "a device file"
unset PACK_HOOK
suid_hook() { chmod 4755 "$1/bin/etude"; }
PACK_HOOK=suid_hook
baseline
leave 0.2.0 ok
as_helper apply
expect_equal "a set-uid bit does not come through (the update is fine without it)" "ok" "$(status state)"
expect "a set-uid bit does not come through: the installed file has none" test ! -u "$PREFIX/versions/0.2.0/bin/etude"
unset PACK_HOOK

echo "== a tampered tarball, and signatures for another version, machine, key or an older release"
baseline
leave 0.2.0 ok
printf '\x00' | dd of="$PREFIX/update/release.tar.gz" bs=1 seek=100 count=1 conv=notrunc 2>/dev/null
as_helper apply
refused "a tarball changed after it was signed"
expect_contains "  it fails the signature check" "signature" "$(status reason)"
baseline
leave 0.2.0 ok "etude 0.9.9 trixie $ARCH"
as_helper apply
refused "a genuine signature of another version"
expect_contains "  the status names both versions" "0.9.9" "$(status reason)"
baseline
leave 0.2.0 ok "etude 0.2.0 trixie armv0"
as_helper apply
refused "a signature for another machine"
expect_contains "  the status says so" "not for this machine" "$(status reason)"
baseline
leave 0.2.0 ok "etude 0.2.0 trixie $ARCH" other
as_helper apply
refused "a signature by another key"
expect_contains "  the status says so" "another key" "$(status reason)"
baseline
leave 0.2.0 ok "etude 0.2.0 trixie $ARCH x"
as_helper apply
refused "a trusted comment that is not etude VERSION OS ARCH"
baseline
leave 0.0.5 ok
as_helper apply
refused "an older release, genuinely signed"
expect_contains "  the status says it is not newer" "not newer" "$(status reason)"
baseline
leave 0.2.0 ok
printf 'version=0.3.0\n' >"$PREFIX/update/request"
as_helper apply
refused "a request for a version the signature is not for"
baseline
leave 0.2.0 ok
echo "garbage" >"$PREFIX/update/release.tar.gz.minisig"
as_helper apply
refused "a signature file that is garbage"

echo "== a download read in pieces: the kind of signature, a changed byte in the middle, a release far larger than the memory"
baseline
leave 0.2.0 ok
expect_equal "the release signature is the prehashed kind (minisign's default, ED)" "RU" "$(sed -n 2p "$WORK/release.tar.gz.minisig" | cut -c1-2)"
as_helper apply
expect_equal "  and the helper installs from it" "ok" "$(status state)"
baseline
SIGN_FLAGS=-l
leave 0.2.0 ok
unset SIGN_FLAGS
expect_equal "a release signed the old way (Ed) is the other kind" "RW" "$(sed -n 2p "$WORK/release.tar.gz.minisig" | cut -c1-2)"
as_helper apply
refused "a release signed the old way, which would need the whole file in memory"
expect_contains "  the status says why" "signed the old way" "$(status reason)"
baseline
leave 0.2.0 ok
size="$(stat -c %s "$PREFIX/update/release.tar.gz")"
mid=$((size / 2))
was="$(dd if="$PREFIX/update/release.tar.gz" bs=1 skip=$mid count=1 2>/dev/null | od -An -tu1 | tr -d ' ')"
printf "\\x$(printf %02x $(((was + 1) % 256)))" | dd of="$PREFIX/update/release.tar.gz" bs=1 seek=$mid count=1 conv=notrunc 2>/dev/null
as_helper apply
refused "a byte changed in the middle of the tarball"
expect_contains "  it fails the signature check" "signature" "$(status reason)"
big_hook() { dd if=/dev/zero of="$1/share/etude/big.bin" bs=1M count=200 status=none; }
PACK_HOOK=big_hook
baseline
leave 0.2.0 ok
unset PACK_HOOK
expect "the 200 MiB release packs to a small tarball (it is mostly zeros)" test "$(stat -c %s "$PREFIX/update/release.tar.gz")" -lt 2000000
# The helper may not grow its writable memory (heap and anonymous maps) by more than 64 MiB: whole in
# memory, the 200 MiB would not fit. The unpacked tar is a file mapped read-only, not memory it keeps.
timeout 600 prlimit --data=$((64 * 1024 * 1024)) -- "$HELPER" --config "$CONF" apply >"$WORK/helper.log" 2>&1
expect_equal "a release that unpacks to 200 MiB is installed by a helper held to 64 MiB of writable memory" "ok" "$(status state)"
expect_equal "  and all of it is there" "209715200" "$(stat -c %s "$PREFIX/versions/0.2.0/share/etude/big.bin" 2>/dev/null)"
expect "  and the helper's scratch files are gone" test -z "$(ls -A "$STATE/incoming" 2>/dev/null)"
# The scratch tar is read through a mapping, so a read error on the card ends the helper with SIGBUS in
# the middle of the unpack; a kill at that point leaves what any cut leaves (before the switch), and the
# next run cleans it up.
echo "== a helper ended in the middle of the unpack (a read error on the mapped scratch file does that)"
PACK_HOOK=big_hook
baseline
leave 0.2.0 ok
unset PACK_HOOK
"$HELPER" --config "$CONF" apply >"$WORK/helper.log" 2>&1 &
cut_pid=$!
for _ in $(seq 1500); do [[ -e "$PREFIX/versions/0.2.0.new/share/etude/big.bin" ]] && break; sleep 0.02; done
kill -9 "$cut_pid" 2>/dev/null
wait "$cut_pid" 2>/dev/null
expect_equal "the half-made version folder is what was left (else the helper was faster than the kill)" "yes" "$([[ -d "$PREFIX/versions/0.2.0.new" ]] && echo yes || echo no)"
expect_equal "  current is untouched" "versions/0.1.0" "$(current)"
expect "  the old app still runs" alive
expect_equal "  nothing is on trial" "" "$(state_phase | grep -v '^none$')"
PACK_HOOK=big_hook
leave 0.2.0 ok
unset PACK_HOOK
as_helper apply
expect_equal "the next run installs the update" "ok" "$(status state)"
expect "  and the half-made folder is gone" test ! -e "$PREFIX/versions/0.2.0.new"
expect "  and nothing is left in the helper's folder" test -z "$(ls -A "$STATE/incoming" 2>/dev/null)"

echo "== a service that will not stop: nothing is copied"
baseline
touch "$SVC/stuck"
leave 0.2.0 ok
as_helper apply
refused "a service that will not stop"
expect_contains "a service that will not stop: the status says so" "stop" "$(status reason)"
expect "a service that will not stop: no backup was made" test ! -e "$DATA/update-backup"
expect "a service that will not stop: the unpacked version is removed" test ! -e "$PREFIX/versions/0.2.0"
rm -f "$SVC/stuck"

echo "== a full disk on the backup: the update is dropped, the old app is started again, the old backup survives"
baseline
"$WORK/systemctl" stop etude.service
rm -rf "$DATA"
mkdir -p "$DATA"
if mount -t tmpfs -o size=400k tmpfs "$DATA" 2>/dev/null; then
    chown etude: "$DATA"
    echo "schema=0.1.0" >"$DATA/history.db"
    echo "pins" >"$DATA/settings.conf"
    head -c 100000 /dev/urandom >"$DATA/settings.conf.bak"   # what the backup must copy and cannot
    head -c 285000 /dev/zero >"$DATA/ballast"                 # the rest of the disk
    mkdir -p "$DATA/update-backup/0.0.9"
    echo "older" >"$DATA/update-backup/0.0.9/history.db"
    echo "version=0.0.9" >"$DATA/update-backup/0.0.9/.complete"
    chown -R etude: "$DATA"
    "$WORK/systemctl" start etude.service
    wait_until 10 alive
    leave 0.2.0 ok
    as_helper apply
    refused "a full disk"
    expect_contains "a full disk: the status says no space" "No space left" "$(status reason)"
    expect "a full disk: no half-made backup is left" test -z "$(find "$DATA/update-backup" -name '*.new' 2>/dev/null)"
    expect "a full disk: the older backup survives" test -f "$DATA/update-backup/0.0.9/.complete"
    expect "a full disk: the unpacked version is removed" test ! -e "$PREFIX/versions/0.2.0"
    expect "a full disk: the old version was started again (it said ready)" wait_until 20 ready_is 0.1.0
    umount "$DATA"
else
    flunk "a full disk: could not mount a small tmpfs (the container needs --cap-add SYS_ADMIN)"
fi
rm -rf "$DATA"
mkdir -p "$DATA"
chown etude: "$DATA"

echo "== what a cut-short run left is cleaned at the start of the next"
baseline
mkdir -p "$PREFIX/versions/0.2.0.new" "$PREFIX/versions/.incoming.x"
echo junk >"$PREFIX/versions/0.2.0.new/junk"
ln -s versions/9.9.9 "$PREFIX/.current.new"
mkdir -p "$STATE/incoming"
echo junk >"$STATE/incoming/junk"
leave 0.2.0 ok
as_helper apply
expect_equal "the update goes through" "ok" "$(status state)"
expect "no .new folder, incoming folder or link is left" test -z "$(find "$PREFIX" -maxdepth 2 \( -name '*.new' -o -name '.incoming*' \) 2>/dev/null)"
expect "the helper's copies of the download are gone" test ! -e "$STATE/incoming"
expect_equal "the new version is whole" "versions/0.2.0" "$(current)"

echo "== a second request while one is on trial, and a request for the version already running"
baseline
leave 0.1.0 ok
as_helper apply
refused "the running version again"
baseline
cat >"$STATE/state" <<STATE
phase=pending
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=1
backup=no
reason=
STATE
leave 0.3.0 ok
as_helper apply
expect_equal "an update while one is on trial is refused" "failed" "$(status state)"
expect_contains "  and says why" "still being settled" "$(status reason)"
rm -f "$STATE/state"

echo "== a prefix that others can write to is not worked in"
baseline
chmod 0777 "$PREFIX"
leave 0.2.0 ok
as_helper apply
expect_equal "the helper stops" "1" "$?"
expect_equal "current did not move" "versions/0.1.0" "$(current)"
expect "the request is consumed (the path unit must not loop on it)" test ! -e "$PREFIX/update/request"
expect "nothing is written under it: no status" test ! -e "$PREFIX/update/status"
expect "the tarball the app left is not touched" test -e "$PREFIX/update/release.tar.gz"
chmod 0755 "$PREFIX"
baseline
chmod 0777 "$PREFIX"
"$HELPER" --config "$CONF" count >"$WORK/out" 2>&1
expect_equal "count does not stop the app from starting under such a prefix (exit 0)" "0" "$?"
expect_contains "  and says why" "not root's alone" "$(cat "$WORK/out")"
chmod 0755 "$PREFIX"

# --- 7. what the second review asked for ------------------------------------------------------

echo "== a service state that is not exactly inactive or failed is running: nothing is installed under it"
for word in activating deactivating reloading; do
    baseline
    leave 0.2.0 ok
    echo "$word" >"$SVC/answer"
    as_helper apply
    rm -f "$SVC/answer"
    refused "the service is $word"
    expect_contains "the service is $word: the status names it" "$word" "$(status reason)"
    expect "the service is $word: no backup was made" test ! -e "$DATA/update-backup"
    expect "the service is $word: the old app is running (started again)" wait_until 10 alive
done

echo "== a signed comment for another Debian release than this system's"
baseline
leave 0.2.0 ok "etude 0.2.0 forky $ARCH"
as_helper apply
refused "an update signed for a newer Debian release (forky) than this system (trixie)"
expect_contains "  the status says which" "forky" "$(status reason)"
baseline
leave 0.2.0 ok "etude 0.2.0 bookworm $ARCH"
as_helper apply
expect_equal "an update built on an older release (bookworm) runs here: it is installed" "ok" "$(status state)"
baseline
leave 0.2.0 ok "etude 0.2.0 sid $ARCH"
as_helper apply
refused "a release name this system does not know"

echo "== the helper is told to stop (SIGTERM) at each stage: the same way out as any failure"
baseline
leave 0.2.0 ok
touch "$SVC/term-after-stop"
as_helper apply
rm -f "$SVC/term-after-stop"
refused "SIGTERM right after the service stopped"
expect_contains "  the status says it was told to stop" "interrupted" "$(status reason)"
expect "  the old app runs again" wait_until 15 ready_is 0.1.0
expect "  no backup was made" test ! -e "$DATA/update-backup"
baseline
leave 0.2.0 ok
touch "$SVC/term-on-show"
as_helper apply
rm -f "$SVC/term-on-show"
refused "SIGTERM while the data is backed up"
expect "  the old app runs again" wait_until 15 ready_is 0.1.0
expect "  the unpacked version is removed" test ! -e "$PREFIX/versions/0.2.0"
expect_equal "  the data is the old version's" "schema=0.1.0" "$(cat "$DATA/history.db")"
baseline
leave 0.2.0 hang
touch "$SVC/term-on-start"
as_helper apply
rm -f "$SVC/term-on-start"
expect_equal "SIGTERM as the new version is started: it goes back" "rolled-back" "$(status state)"
expect_contains "  the status says it was interrupted" "interrupted" "$(status reason)"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the old version runs and says ready" wait_until 15 ready_is 0.1.0
expect_equal "  the data is restored" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect_equal "  nothing is on trial" "none" "$(state_phase)"
baseline
leave 0.2.0 hang
touch "$SVC/term-on-restart"
as_helper apply
rm -f "$SVC/term-on-restart"
expect_equal "SIGTERM while a new version that never says ready is waited for: it goes back" "rolled-back" "$(status state)"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the old version runs and says ready" wait_until 15 ready_is 0.1.0
expect_equal "  the new version was started twice (the second call finished), then given up on" "2" "$(grep -c '^0.2.0$' "$SVC/started")"

echo "== an unreadable XDG_DATA_HOME in the service is not quietly replaced by the home folder"
baseline
leave 0.2.0 ok
printf 'SDL_VIDEO_DRIVER=kmsdrm "XDG_DATA_HOME=/srv/a b"\n' >"$SVC/show-env"
as_helper apply
refused "an environment that cannot be read"
expect_contains "  the status says the data folder was not found" "data folder" "$(status reason)"
expect "  no backup was made" test ! -e "$DATA/update-backup"

echo "== a state file the helper cannot trust is not taken for no update"
baseline
leave 0.2.0 hang
as_helper apply >/dev/null 2>&1 &
HELPER_BG=$!
wait_until 30 phase_is pending
# The new version is on trial and its record is damaged (owned by the app's user: not root's).
power_cut
chown etude: "$STATE/state"
as_helper count
expect_equal "count went on (exit 0)" "0" "$?"
expect_equal "the unproven version was gone back from" "versions/0.1.0" "$(current)"
expect_equal "the status says it was rolled back" "rolled-back" "$(status state)"
expect_contains "  and why" "could not be read" "$(status reason)"
expect "the damaged file was set aside" test -e "$STATE/state.unreadable"
expect_equal "the data is the old version's" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect "the failed version is removed" test ! -e "$PREFIX/versions/0.2.0"

echo "== a going back whose data cannot be put back keeps the app from starting (count ends with 75)"
baseline
cat >"$STATE/state" <<STATE
phase=rolling-back
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=2
backup=yes
reason=a test
STATE
mkdir -p "$PREFIX/versions/0.2.0/bin"
as_helper count
expect_equal "count says so (75)" "75" "$?"
expect "the state stays, so the next start tries again" test "$(state_phase)" = rolling-back
mkdir -p "$DATA/update-backup/0.1.0"
echo "schema=0.1.0" >"$DATA/update-backup/0.1.0/history.db"
echo "version=0.1.0" >"$DATA/update-backup/0.1.0/.complete"
chown -R etude: "$DATA"
as_helper count
expect_equal "with the backup there the restore finishes (0)" "0" "$?"
expect_equal "and nothing is pending" "none" "$(state_phase)"

echo "== an update that was taken in and cut short before the switch is reported at boot"
baseline
printf 'state=applying\nversion=0.2.0\nfrom=0.1.0\nreason=\ntime=1\n' >"$PREFIX/update/status"
chown root: "$PREFIX/update/status"
as_helper resume
expect_equal "the status no longer says applying: failed" "failed" "$(status state)"
expect_contains "  it says the run was cut short" "cut short" "$(status reason)"
expect_equal "  and the old version is still in use" "versions/0.1.0" "$(current)"

echo "== a folder above the prefix that the app can write to refuses the prefix"
rm -rf /opt/uh-anc
mkdir -p /opt/uh-anc/p/versions/0.1.0/bin /opt/uh-anc/p/update
chmod 0755 /opt/uh-anc /opt/uh-anc/p /opt/uh-anc/p/versions
ln -s versions/0.1.0 /opt/uh-anc/p/current
chown etude: /opt/uh-anc /opt/uh-anc/p/update # the app owns the folder the prefix is in
sed "s|^PREFIX=.*|PREFIX=\"/opt/uh-anc/p\"|" "$CONF" >"$WORK/conf-anc"
chmod 0644 "$WORK/conf-anc"
printf 'version=0.2.0\n' >/opt/uh-anc/p/update/request
chown etude: /opt/uh-anc/p/update/request
"$HELPER" --config "$WORK/conf-anc" apply >"$WORK/out" 2>&1
expect_equal "apply refuses (exit 1)" "1" "$?"
expect_contains "  and says a folder above the prefix is the app's" "above the prefix" "$(cat "$WORK/out")"
expect "  the request is consumed" test ! -e /opt/uh-anc/p/update/request
"$HELPER" --config "$WORK/conf-anc" count >"$WORK/out" 2>&1
expect_equal "count does not keep the app from starting (exit 0)" "0" "$?"
chown root: /opt/uh-anc
chmod 0775 /opt/uh-anc
"$HELPER" --config "$WORK/conf-anc" count >"$WORK/out" 2>&1
expect_contains "a folder above the prefix that a group can write: the same" "above the prefix" "$(cat "$WORK/out")"
rm -rf /opt/uh-anc

echo "== the data folder through a link the app made is refused"
baseline
leave 0.2.0 ok
rm -rf /home/etude/.local/share/etude
mkdir -p "$WORK/decoy/data" /home/etude/.local/share
ln -s "$WORK/decoy/data" /home/etude/.local/share/etude
chown -h etude: /home/etude/.local/share/etude
as_helper apply
refused "a data folder behind a link the app made"
expect_contains "  the status says the data folder was not found" "data folder" "$(status reason)"
expect "  nothing was written through the link" test -z "$(find "$WORK/decoy/data" -mindepth 1 2>/dev/null)"
rm -f /home/etude/.local/share/etude
mkdir -p "$DATA"
chown -R etude: /home/etude

# --- 7b. fix round 3 ----------------------------------------------------------------------------

echo "== a trial version whose record is damaged while it is watched is gone back from"
baseline
leave 0.2.0 hang
as_helper apply &
HELPER_BG=$!
wait_until 30 phase_is pending
printf 'junk' >"$STATE/state" # damaged in the middle of the watch (still root's file)
wait "$HELPER_BG"
expect_equal "the watch went back: rolled-back" "rolled-back" "$(status state)"
expect_contains "  and says the record could not be read" "could not be read" "$(status reason)"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the old version runs and says ready" wait_until 15 ready_is 0.1.0
expect_equal "  the data is restored" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect_equal "  nothing is on trial" "none" "$(state_phase)"

echo "== a request that finds a damaged record of an earlier update goes back from it and is refused"
baseline
leave 0.2.0 hang
as_helper apply >/dev/null 2>&1 &
wait_until 30 phase_is pending
power_cut
printf 'junk' >"$STATE/state"
leave 0.3.0 ok
as_helper apply
expect_equal "the new request is refused (failed)" "failed" "$(status state)"
expect_contains "  and says the earlier record was damaged" "damaged" "$(status reason)"
expect_equal "  the old version is current" "versions/0.1.0" "$(current)"
expect "  the old version runs again" wait_until 15 alive
expect "  the trial version is removed" test ! -e "$PREFIX/versions/0.2.0"
expect_equal "  nothing is on trial" "none" "$(state_phase)"

echo "== a trial app that will not stop is stopped again and killed, and the old version comes back"
baseline
touch "$SVC/stick-on-start" # from the new version's start on, `stop` does nothing
leave 0.2.0 hang
as_helper apply
rm -f "$SVC/stick-on-start" "$SVC/stuck"
expect_equal "it went back" "rolled-back" "$(status state)"
expect_contains "  the reason says the app had to be killed" "killed" "$(status reason)"
expect "  the kill was asked for" grep -q "^kill --kill-whom=main" "$SVC/calls"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect "  the old version runs and says ready" wait_until 15 ready_is 0.1.0
expect_equal "  the data is restored" "schema=0.1.0" "$(cat "$DATA/history.db")"

echo "== an app that cannot be stopped even killed: links back, the start queued, the data left to count"
baseline
touch "$SVC/stick-on-start"
leave 0.2.0 hang
touch "$SVC/unkillable"
as_helper apply
expect_equal "the run said the app could not be stopped (failed)" "failed" "$(status state)"
expect_equal "  current is the old version" "versions/0.1.0" "$(current)"
expect_equal "  the going back is recorded" "rolling-back" "$(state_phase)"
expect "  a start was queued, so the app is not left down" grep -q "^start --no-block etude.service" "$SVC/calls"
expect_equal "  the data is not touched under a running app (the new version's)" "schema=0.2.0" "$(cat "$DATA/history.db")"
rm -f "$SVC/stick-on-start" "$SVC/stuck" "$SVC/unkillable"
"$WORK/systemctl" stop etude.service
as_helper count
expect_equal "the next start's count finishes it (0)" "0" "$?"
expect_equal "  the data is restored" "schema=0.1.0" "$(cat "$DATA/history.db")"
expect_equal "  the status says rolled-back" "rolled-back" "$(status state)"
expect_equal "  nothing is on trial" "none" "$(state_phase)"

echo "== a going back that is recorded keeps the app from starting when the layout is refused"
baseline
cat >"$STATE/state" <<STATE
phase=rolling-back
from=0.1.0
to=0.2.0
previous=
unit=etude.service
starts=2
backup=yes
reason=a test
STATE
chmod 0777 "$PREFIX"
as_helper count
expect_equal "count ends 75 (the app must not start on the new data)" "75" "$?"
chmod 0755 "$PREFIX"
rm -f "$STATE/state"

# --- 8. real systemd: the real units, an update, a rollback, a stop ----------------------------

if ((SYSTEMD_MODE)); then
# shellcheck source=update-helper-test-systemd.sh
source "$SCRIPT_DIR/update-helper-test-systemd.sh"
# shellcheck source=update-helper-test-crash.sh
source "$SCRIPT_DIR/update-helper-test-crash.sh"
fi

echo
printf '%d passed, %d failed\n' "$PASSED" "$FAILED"
((FAILED == 0))
