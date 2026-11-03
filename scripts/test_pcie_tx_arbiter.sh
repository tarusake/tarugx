#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
 --top-module pcie_tx_arbiter_tb \
 --Mdir build/pcie_tx_arbiter_test \
 build/pcie/pcie_tx_arbiter.sv tests/pcie_tx_arbiter_tb.sv
build/pcie_tx_arbiter_test/Vpcie_tx_arbiter_tb
