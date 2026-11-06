#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module texture_sampler_tb \
 --Mdir build/texture_sampler_test build/render/texture_sampler.sv \
 tests/texture_sampler_tb.sv
build/texture_sampler_test/Vtexture_sampler_tb
