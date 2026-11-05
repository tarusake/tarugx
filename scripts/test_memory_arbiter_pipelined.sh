#!/bin/sh
set -eu
veryl build
rm -rf build/memory_arbiter_pipelined_test
verilator --binary --timing -Wno-fatal --top-module memory_arbiter_pipelined_tb \
  -Mdir build/memory_arbiter_pipelined_test tests/memory_arbiter_pipelined_tb.sv $(cat tarugx.f)
build/memory_arbiter_pipelined_test/Vmemory_arbiter_pipelined_tb
rm -rf build/memory_arbiter_pipeline_rate_test
verilator --binary --timing --assert -Wno-fatal --top-module memory_arbiter_pipeline_rate_tb \
  -Mdir build/memory_arbiter_pipeline_rate_test tests/memory_arbiter_pipeline_rate_tb.sv $(cat tarugx.f)
build/memory_arbiter_pipeline_rate_test/Vmemory_arbiter_pipeline_rate_tb
