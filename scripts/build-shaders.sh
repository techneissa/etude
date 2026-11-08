#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the checked-in shader headers from the GLSL sources in app/shaders/. An ordinary build
# needs none of these tools: the headers are in the tree, and this script only runs when a shader
# source changes (then commit the headers it writes).
#
#   scripts/build-shaders.sh [--check]
#     --check  build into a temporary folder and fail when a checked-in header differs
#
# Each source is Vulkan-flavoured GLSL 450 written once. glslang compiles it to SPIR-V; SPIRV-Cross
# turns the SPIR-V into Metal source (the GPU renderer on a Mac), GLSL 1.20 (the OpenGL renderer on a
# Mac) and GLSL ES 1.00 (the OpenGL ES 2 renderer on Linux). The SPIR-V itself is the GPU renderer's
# shader on Linux (Vulkan). Tools, pinned (Homebrew: `brew install glslang spirv-cross`):
#   glslang      16.6.0                  BSD-3-Clause (glslangValidator)
#   SPIRV-Cross  Vulkan SDK 1.4.363.0    Apache-2.0   (spirv-cross, built 2026-09-16T11:27:19)
# Both licences are GPL-3.0-compatible. A different version writes different bytes, so the script
# stops unless ETUDE_SHADER_TOOLS_ANY=1 is set.
set -euo pipefail
cd "$(dirname "$0")/.."

GLSLANG_VERSION=16.6.0
SPIRV_CROSS_STAMP=2026-09-16T11:27:19
SRC=app/shaders
CHECK=0
[ "${1:-}" != --check ] || CHECK=1

for tool in glslangValidator spirv-cross xxd; do
    command -v "$tool" > /dev/null || { echo "build-shaders: $tool not found (brew install glslang spirv-cross; xxd comes with vim)" >&2; exit 1; }
done
if [ "${ETUDE_SHADER_TOOLS_ANY:-0}" != 1 ]; then
    glslang_version=$(glslangValidator --version 2>&1 | head -1)
    spirv_cross_revision=$(spirv-cross --revision 2>&1 | tr '\n' ' ')
    grep -q "Glslang Version: .*:$GLSLANG_VERSION" <<< "$glslang_version" ||
        { echo "build-shaders: glslang $GLSLANG_VERSION wanted, have: $glslang_version" >&2; exit 1; }
    grep -q "Timestamp: $SPIRV_CROSS_STAMP" <<< "$spirv_cross_revision" ||
        { echo "build-shaders: SPIRV-Cross built $SPIRV_CROSS_STAMP wanted, have: $spirv_cross_revision" >&2; exit 1; }
fi

OUT=$SRC
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
[ "$CHECK" = 0 ] || OUT=$WORK/out
mkdir -p "$OUT"

# header NAME KIND FILE: FILE as a C++ header. KIND bytes: the bytes of a binary; text: a raw string.
header() {
    local name=$1 kind=$2 file=$3 out=$4
    {
        echo "// SPDX-License-Identifier: GPL-3.0-or-later"
        echo "// Written by scripts/build-shaders.sh from $5. Do not edit."
        echo "#pragma once"
        echo
        echo "#include <cstddef>"
        echo "#include <cstdint>"
        echo
        if [ "$kind" = bytes ]; then
            echo "inline constexpr std::uint8_t $name[] = {"
            xxd -i < "$file" | sed -e 's/^ /   /'
            echo "};"
        else
            echo "inline constexpr char $name[] = R\"shader($(cat "$file")"
            echo ")shader\";"
        fi
    } > "$out"
}

# build SOURCE STAGE NAMEBASE FORMATS...: formats among spv msl gl120 es100
build() {
    local source=$1 stage=$2 base=$3
    shift 3
    local spv=$WORK/$base.$stage.spv
    local log
    log=$(glslangValidator -V -S "$stage" "$SRC/$source" -o "$spv" 2>&1) || { echo "$log" >&2; exit 1; }
    for format in "$@"; do
        local file=$WORK/$base.$stage.$format
        case $format in
        spv) ;;
        msl) spirv-cross "$spv" --msl --output "$file" ;;
        gl120) spirv-cross "$spv" --version 120 --no-es --remove-unused-variables --output "$file" ;;
        es100) spirv-cross "$spv" --version 100 --es --remove-unused-variables --output "$file" ;;
        esac
        local kind=text
        [ "$format" != spv ] || kind=bytes
        local ident
        ident=$(echo "k_${base}_${stage}_${format}" | awk -F_ '{ out = ""; for (i = 2; i <= NF; i++) out = out toupper(substr($i, 1, 1)) substr($i, 2); print "k" out }')
        header "$ident" "$kind" "$file" "$OUT/$base.$stage.$format.h" "$SRC/$source"
    done
}

# neon_glow: the fragment stage for all three paths, the vertex stage only for OpenGL.
build neon_glow.frag frag neon_glow spv msl gl120 es100
build neon_glow.vert vert neon_glow gl120 es100

if [ "$CHECK" = 1 ]; then
    for f in "$OUT"/*.h; do
        cmp -s "$f" "$SRC/$(basename "$f")" || { echo "build-shaders: $(basename "$f") differs from the source" >&2; exit 1; }
    done
    echo "build-shaders: headers match their sources"
fi
