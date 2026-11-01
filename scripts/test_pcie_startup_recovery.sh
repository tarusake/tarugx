#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_startup_recovery_tb \
 --Mdir build/pcie_startup_recovery_test build/pcie/pcie_startup_recovery.sv tests/pcie_startup_recovery_tb.sv
build/pcie_startup_recovery_test/Vpcie_startup_recovery_tb
python3 -m unittest discover -s tests -p test_decode_scanout_uart.py
