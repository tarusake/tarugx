#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module scanout_debug_monitor_tb \
 --Mdir build/scanout_debug_monitor_test build/debug/scanout_debug_monitor.sv tests/scanout_debug_monitor_tb.sv
build/scanout_debug_monitor_test/Vscanout_debug_monitor_tb
