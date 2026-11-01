#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_debug_monitor_tb \
 --Mdir build/pcie_debug_monitor_test build/display/display_config_cdc.sv \
 build/debug/pcie_debug_monitor.sv tests/pcie_debug_monitor_tb.sv
build/pcie_debug_monitor_test/Vpcie_debug_monitor_tb
python3 -m unittest discover -s tests -p test_decode_scanout_uart.py
