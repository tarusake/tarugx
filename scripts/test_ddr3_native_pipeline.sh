#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module ddr3_native_pipeline_tb \
 --Mdir build/ddr3_native_pipeline_test build/memory/ddr3_native_pipeline.sv tests/ddr3_native_pipeline_tb.sv
build/ddr3_native_pipeline_test/Vddr3_native_pipeline_tb
