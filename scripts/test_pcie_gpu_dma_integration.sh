#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
 --top-module pcie_gpu_dma_integration_tb \
 --Mdir build/pcie_gpu_dma_integration_test \
 build/pcie/*.sv build/dma/*.sv build/memory/framebuffer_request_arbiter.sv \
 build/registers/gpu_registers.sv tests/pcie_gpu_dma_integration_tb.sv
build/pcie_gpu_dma_integration_test/Vpcie_gpu_dma_integration_tb
