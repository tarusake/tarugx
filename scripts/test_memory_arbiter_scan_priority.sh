#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
  --top-module memory_arbiter_scan_priority_tb \
  --Mdir build/memory_arbiter_scan_priority_test -f tarugx.f \
  tests/memory_arbiter_scan_priority_tb.sv
build/memory_arbiter_scan_priority_test/Vmemory_arbiter_scan_priority_tb
