#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module ddr_debug_uart_tb \
 --Mdir build/ddr_debug_uart_test build/debug/uart_tx.sv build/debug/ddr_debug_uart.sv \
 tests/ddr_debug_uart_tb.sv
build/ddr_debug_uart_test/Vddr_debug_uart_tb
