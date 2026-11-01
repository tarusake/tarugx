#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_memory_integration_tb \
 --Mdir build/pcie_memory_integration_test -f tarugx.f tests/pcie_memory_integration_tb.sv
build/pcie_memory_integration_test/Vpcie_memory_integration_tb
