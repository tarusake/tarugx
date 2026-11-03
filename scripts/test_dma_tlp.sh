#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module dma_tlp_tb \
 --Mdir build/dma_tlp_test build/pcie/dma_read_request_tx.sv build/pcie/dma_completion_rx.sv tests/dma_tlp_tb.sv
build/dma_tlp_test/Vdma_tlp_tb
