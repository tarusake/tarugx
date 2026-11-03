#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module memory_request_cdc_tb \
 --Mdir build/memory_cdc_test \
 dependencies/std/async_fifo/async_fifo_pkg.sv dependencies/std/gray/gray_decoder.sv \
 dependencies/std/gray/gray_encoder.sv dependencies/std/synchronizer/synchronizer_basic.sv \
 dependencies/std/async_fifo/async_fifo_reset_sync.sv dependencies/std/async_fifo/async_fifo.sv \
 build/memory/memory_request_cdc.sv tests/memory_request_cdc_tb.sv
build/memory_cdc_test/Vmemory_request_cdc_tb
