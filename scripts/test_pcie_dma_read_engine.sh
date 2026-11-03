#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_dma_read_engine_tb \
 --Mdir build/pcie_dma_read_engine_test build/dma/pcie_dma_read_engine.sv tests/pcie_dma_read_engine_tb.sv
build/pcie_dma_read_engine_test/Vpcie_dma_read_engine_tb
