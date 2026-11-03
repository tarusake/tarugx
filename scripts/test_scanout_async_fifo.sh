#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
rm -rf build/scanout_async_fifo_test
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
  --top-module scanout_async_fifo_tb \
  --Mdir build/scanout_async_fifo_test \
  build/display/scanout_async_fifo.sv build/display/scanout_fifo.sv \
  tests/scanout_async_fifo_tb.sv
build/scanout_async_fifo_test/Vscanout_async_fifo_tb
