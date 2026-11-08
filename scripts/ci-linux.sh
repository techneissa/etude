#!/usr/bin/env bash
# Runs the Linux CI job on this machine: a Release build with warnings as errors and the full
# test suite, inside the Ubuntu 24.04 / GCC 13 container from ci/linux.Dockerfile. It needs only
# Docker. Downloaded dependencies and each checkout's build directory live in Docker volumes, so
# later runs are incremental. The run takes the heavy-check lock (scripts/heavy-lock.sh), the one that
# icon-audit and bench share: one heavy check at a time on this machine, a second waits.
set -euo pipefail

root=$(git rev-parse --show-toplevel)
name=$(basename "$root" | tr -c 'A-Za-z0-9_.-' '-')
image=etude-ci-linux:ubuntu24.04
cpus=${ETUDE_CI_CPUS:-8}

command -v docker >/dev/null || { echo "ci-linux: Docker is not installed" >&2; exit 2; }
docker info >/dev/null 2>&1 || { echo "ci-linux: Docker is not running" >&2; exit 2; }

docker build -q -t "$image" -f "$root/ci/linux.Dockerfile" "$root/ci" >/dev/null

echo "ci-linux: $root on Ubuntu 24.04 arm64, GCC 13 ($cpus CPUs)"
exec "$root/scripts/heavy-lock.sh" ci-linux \
    docker run --rm --cpus="$cpus" \
        -v "$root":/src:ro \
        -v etude-ci-deps:/deps \
        -v "etude-ci-build-$name":/build \
        "$image" bash -euo pipefail -c '
            cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DETUDE_PIANO_FROM_SOURCE=OFF \
                -DFETCHCONTENT_BASE_DIR=/deps/fetchcontent
            cmake --build /build -- -k 0
            ctest --test-dir /build --output-on-failure
        '
