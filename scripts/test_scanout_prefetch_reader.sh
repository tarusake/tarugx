#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module scanout_prefetch_reader_tb \
 --Mdir build/scanout_prefetch_reader_test build/display/scanout_prefetch_reader.sv tests/scanout_prefetch_reader_tb.sv
build/scanout_prefetch_reader_test/Vscanout_prefetch_reader_tb
