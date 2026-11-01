#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_reset_scope_tb \
 --Mdir build/pcie_reset_scope_test build/debug/pcie_reset_scope.sv tests/pcie_reset_scope_tb.sv
build/pcie_reset_scope_test/Vpcie_reset_scope_tb
python3 -m unittest discover -s tests -p test_decode_scanout_uart.py
