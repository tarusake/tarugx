#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
 --top-module pcie_dma_subsystem_tb \
 --Mdir build/pcie_dma_subsystem_test \
 build/pcie/pcie_rx_router.sv build/pcie/pcie_tx_arbiter.sv \
 build/pcie/dma_read_request_tx.sv build/pcie/dma_completion_rx.sv \
 build/dma/pcie_dma_read_engine.sv build/dma/pcie_dma_channel.sv \
 build/memory/framebuffer_request_arbiter.sv \
 build/dma/pcie_dma_subsystem.sv tests/pcie_dma_subsystem_tb.sv
build/pcie_dma_subsystem_test/Vpcie_dma_subsystem_tb
