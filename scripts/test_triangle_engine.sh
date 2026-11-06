#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module triangle_engine_tb \
 --Mdir build/triangle_engine_test build/render/triangle_rasterizer.sv \
 build/render/texture_sampler.sv build/render/texture_fragment_stage.sv \
 build/render/fragment_writer.sv build/render/triangle_engine.sv tests/triangle_engine_tb.sv
build/triangle_engine_test/Vtriangle_engine_tb
