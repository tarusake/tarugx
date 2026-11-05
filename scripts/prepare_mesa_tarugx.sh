#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mesa_source=${1:?usage: prepare_mesa_tarugx.sh MESA_SOURCE}
expected_tag=mesa-23.2.1
actual_tag=$(git -C "$mesa_source" describe --tags --exact-match HEAD 2>/dev/null || true)

if [[ "$actual_tag" != "$expected_tag" ]]; then
    echo "error: expected $expected_tag, got ${actual_tag:-untagged source}" >&2
    exit 1
fi

patch_file="$root/mesa/mesa-23.2.1-tarugx.patch"
if patch --dry-run -d "$mesa_source" -p1 < "$patch_file" >/dev/null 2>&1; then
    patch -d "$mesa_source" -p1 < "$patch_file"
elif ! patch --dry-run -R -d "$mesa_source" -p1 < "$patch_file" >/dev/null 2>&1; then
    echo "error: Mesa tree is neither clean nor already patched for TaruGX" >&2
    exit 1
fi

driver_dir="$mesa_source/src/gallium/drivers/tarugx"
install -d "$driver_dir"
install -m 0644 "$root/mesa/gallium/meson.build" "$driver_dir/meson.build"
install -m 0644 "$root/mesa/gallium/tarugx_screen.c" "$driver_dir/tarugx_screen.c"
install -m 0644 "$root/mesa/gallium/tarugx_screen.h" "$driver_dir/tarugx_screen.h"
install -m 0644 "$root/mesa/tarugx_winsys.c" "$driver_dir/tarugx_winsys.c"
install -m 0644 "$root/mesa/tarugx_winsys.h" "$driver_dir/tarugx_winsys.h"
install -m 0644 "$root/drivers/tarugx/tarugx_drm.h" "$driver_dir/tarugx_drm.h"

echo "Prepared TaruGX Gallium sources in $mesa_source"
