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
    $(pkg-config --cflags --libs egl glesv2 gbm libdrm)

result=$(env LIBGL_DRIVERS_PATH="$driver_dir" \
    MESA_LOADER_DRIVER_OVERRIDE=tarugx \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp}" \
    "$test_binary" "$card_node" "$@")
printf '%s\n' "$result"
grep -qx 'renderer=TaruGX (FPGA 3D + softpipe fallback)' <<<"$result"
grep -qx 'center=f800' <<<"$result"
grep -qx 'corner=001f' <<<"$result"
