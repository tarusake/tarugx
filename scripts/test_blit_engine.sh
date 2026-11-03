#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module blit_engine_tb \
 --Mdir build/blit_engine_test build/render/blit_engine.sv tests/blit_engine_tb.sv
build/blit_engine_test/Vblit_engine_tb
