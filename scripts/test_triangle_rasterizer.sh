#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module triangle_rasterizer_tb \
 --Mdir build/triangle_rasterizer_test build/render/triangle_rasterizer.sv \
 tests/triangle_rasterizer_tb.sv
build/triangle_rasterizer_test/Vtriangle_rasterizer_tb
