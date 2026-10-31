#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module scanout_reader_tb \
 --Mdir build/scanout_reader_test build/display/scanout_reader.sv tests/scanout_reader_tb.sv
build/scanout_reader_test/Vscanout_reader_tb
