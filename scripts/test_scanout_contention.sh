#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
for priority in 0 1; do
 verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module scanout_contention_tb \
  "-GPRIORITY=1'b$priority" --Mdir build/scanout_contention_$priority -f tarugx.f tests/scanout_contention_tb.sv
 build/scanout_contention_$priority/Vscanout_contention_tb
done
