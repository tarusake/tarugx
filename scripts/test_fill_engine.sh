#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module fill_engine_tb \
 --Mdir build/fill_engine_test build/render/fill_engine.sv tests/fill_engine_tb.sv
build/fill_engine_test/Vfill_engine_tb
