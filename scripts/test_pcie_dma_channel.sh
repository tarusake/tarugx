#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_dma_channel_tb \
 --Mdir build/pcie_dma_channel_test build/dma/*.sv build/pcie/dma_read_request_tx.sv \
 build/pcie/dma_completion_rx.sv tests/pcie_dma_channel_tb.sv
build/pcie_dma_channel_test/Vpcie_dma_channel_tb
