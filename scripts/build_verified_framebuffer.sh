#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
# Build the complete configuration used by the SRAM hardware regression.
set -eu
cd "$(dirname "$0")/.."

if [ -f .local/gowin.env ]; then
    set -a
    # shellcheck disable=SC1091
    . .local/gowin.env
    set +a
fi

examples=$PWD/build/deps/TangMega-138K-example
generated=$PWD/build/tarugx-pcie-rom-probe
for required in \
    "$examples/pcie_dma_demo/pcie_gen2(5G)" \
    "$examples/hdmi_colorbar/eda_proj/src" \
    "$examples/cam_dvi/cam2dvi/src/ddr3_memory_interface" \
    "$generated/serdes.v" \
    "$generated/serdes.csr"; do
    test -e "$required" || { echo "Missing verified build input: $required" >&2; exit 1; }
done

export SIPEED_PCIE_GEN2_DIR="$examples/pcie_dma_demo/pcie_gen2(5G)"
export SIPEED_HDMI_DIR="$examples/hdmi_colorbar/eda_proj/src"
export SIPEED_CAM_DIR="$examples/cam_dvi/cam2dvi/src"
export SIPEED_DDR3_B_DIR="$examples/cam_dvi/cam2dvi/src/ddr3_memory_interface"
export TARUGX_PCIE_GENERATED_DIR="$generated"
export TARUGX_PCIE_PHY_RECOVERY_EXPERIMENT=1
export TARUGX_PCIE_ROM_PROBE=1
export TARUGX_PCIE_ROM_DIAGNOSTIC=1
export TARUGX_PCIE_RAW_BAR_PROBE=1
export TARUGX_PCIE_AUTO_ROM=1
export TARUGX_PCIE_PIPELINED_MEMORY=1

gowin_sh=${GOWIN_SH:-}
if [ -z "$gowin_sh" ]; then
    gowin_sh=$(command -v gw_sh || true)
fi
test -n "$gowin_sh" || { echo "Set GOWIN_SH to the gw_sh executable" >&2; exit 1; }
exec "$gowin_sh" scripts/gowin_build_framebuffer.tcl
