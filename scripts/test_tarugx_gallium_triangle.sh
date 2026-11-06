#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
driver_dir=${1:?usage: test_tarugx_gallium_triangle.sh DRIVER_DIR CARD_NODE}
card_node=${2:?usage: test_tarugx_gallium_triangle.sh DRIVER_DIR CARD_NODE}
shift 2
test_binary=$(mktemp "${TMPDIR:-/tmp}/tarugx-gallium-triangle.XXXXXX")
trap 'rm -f "$test_binary"' EXIT

cc -O2 -Wall -Wextra -Werror \
    "$root/tests/tarugx_gallium_triangle.c" -o "$test_binary" \
    $(pkg-config --cflags --libs egl glesv2 gbm libdrm) -lm

result=$(env LIBGL_DRIVERS_PATH="$driver_dir" \
    MESA_LOADER_DRIVER_OVERRIDE=tarugx \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp}" \
    "$test_binary" "$card_node" "$@")
printf '%s\n' "$result"
grep -qx 'renderer=TaruGX (FPGA 3D + softpipe fallback)' <<<"$result"
grep -qx 'corner=001f' <<<"$result"
if grep -qx 'path=fpga-batch' <<<"$result"; then
    grep -qx 'center=001f' <<<"$result"
    grep -qx 'top_left=ffe0' <<<"$result"
    grep -qx 'top_right=ffff' <<<"$result"
    grep -qx 'bottom_left=f800' <<<"$result"
    grep -qx 'bottom_right=07e0' <<<"$result"
elif ! grep -qx 'path=fpga-gradient' <<<"$result"; then
    if grep -qx 'path=fpga-depth' <<<"$result"; then
        grep -qx 'center=07e0' <<<"$result"
    elif grep -Eq '^path=fpga-cull-(front|back-cw)$' <<<"$result"; then
        grep -qx 'center=001f' <<<"$result"
    else
        grep -qx 'center=f800' <<<"$result"
    fi
fi
