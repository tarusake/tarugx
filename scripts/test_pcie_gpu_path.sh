#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_gpu_path_tb \
 --Mdir build/pcie_gpu_path_test build/pcie/*.sv build/registers/gpu_registers.sv tests/pcie_gpu_path_tb.sv
build/pcie_gpu_path_test/Vpcie_gpu_path_tb
