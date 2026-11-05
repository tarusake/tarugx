#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mesa_source=${1:?usage: build_mesa_tarugx.sh MESA_SOURCE [BUILD_DIR]}
build_dir=${2:-"$root/build/mesa-tarugx"}
python_modules=${TARUGX_MESA_PYTHONPATH:-"$root/build/mesa-python"}
ninja=${TARUGX_NINJA:-"$python_modules/bin/ninja"}

"$root/scripts/prepare_mesa_tarugx.sh" "$mesa_source"

export PYTHONPATH="$python_modules${PYTHONPATH:+:$PYTHONPATH}"
export NINJA="$ninja"
python3 -m mesonbuild.mesonmain setup --reconfigure "$build_dir" "$mesa_source" \
    -Dgallium-drivers=tarugx -Dvulkan-drivers=[] -Dplatforms=[] \
    -Degl=enabled -Dglx=disabled -Dgbm=enabled \
    -Dgles1=disabled -Dgles2=enabled -Dllvm=disabled \
    -Dvideo-codecs=[] -Dbuild-tests=false
CCACHE_DISABLE=1 "$ninja" -C "$build_dir" \
    src/gallium/targets/dri/libgallium_dri.so

echo "Built $build_dir/src/gallium/targets/dri/libgallium_dri.so"
