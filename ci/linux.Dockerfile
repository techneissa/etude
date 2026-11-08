# The Linux CI job's machine as a local container: Ubuntu 24.04 with GCC 13, arm64 like the
# Raspberry Pi 5 (on an Apple Silicon Mac Docker runs it natively). Keep the package list the
# same as .github/workflows/ci.yml.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates python3 \
        libasound2-dev libudev-dev \
        libdrm-dev libgbm-dev libegl-dev libgles-dev \
        libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev \
        libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols \
        libdecor-0-dev libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
