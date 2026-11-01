#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module scanout_fifo_tb \
 --Mdir build/scanout_fifo_test -f tarugx.f tests/scanout_fifo_tb.sv
build/scanout_fifo_test/Vscanout_fifo_tb
