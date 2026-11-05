#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
driver_dir=${1:?usage: test_tarugx_gallium.sh DRIVER_DIR RENDER_NODE}
render_node=${2:?usage: test_tarugx_gallium.sh DRIVER_DIR RENDER_NODE}
probe=$(mktemp "${TMPDIR:-/tmp}/tarugx-gallium-probe.XXXXXX")
trap 'rm -f "$probe"' EXIT

cc -O2 -Wall -Wextra -Werror "$root/tests/tarugx_gallium_probe.c" \
    -o "$probe" $(pkg-config --cflags --libs egl glesv2 gbm)

result=$(env LIBGL_DRIVERS_PATH="$driver_dir" \
    MESA_LOADER_DRIVER_OVERRIDE=tarugx \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp}" \
    "$probe" "$render_node")
printf '%s\n' "$result"
grep -qx 'renderer=TaruGX (softpipe fallback)' <<<"$result"
grep -qx 'vendor=TaruGX' <<<"$result"
