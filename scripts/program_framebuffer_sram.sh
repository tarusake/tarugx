#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
# Volatile update: program FPGA SRAM, leaving external Flash unchanged.
set -eu
cd "$(dirname "$0")/.."
out=${TARUGX_SRAM_OUT:-build/gowin_framebuffer_pipeline/tarugx/impl/pnr}
python3 scripts/check_gowin_timing.py "$out/tarugx_tr_content.html"
test -s "$out/tarugx.fs"
python3 scripts/bitstream_manifest.py verify "$out"
exec python3 tools/program_framebuffer_sram.py "$out/tarugx.fs"
