#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_rx_probe_tb \
 --Mdir build/pcie_rx_probe_test build/debug/pcie_rx_probe.sv tests/pcie_rx_probe_tb.sv
build/pcie_rx_probe_test/Vpcie_rx_probe_tb
python3 -m unittest discover -s tests -p test_decode_scanout_uart.py
