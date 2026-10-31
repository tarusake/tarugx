#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module memory_self_test_tb \
  --Mdir build/memory_self_test build/memory/memory_self_test.sv tests/memory_self_test_tb.sv
build/memory_self_test/Vmemory_self_test_tb
