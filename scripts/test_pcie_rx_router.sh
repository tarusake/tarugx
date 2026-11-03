#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
 --top-module pcie_rx_router_tb \
 --Mdir build/pcie_rx_router_test \
 build/pcie/pcie_rx_router.sv tests/pcie_rx_router_tb.sv
build/pcie_rx_router_test/Vpcie_rx_router_tb
