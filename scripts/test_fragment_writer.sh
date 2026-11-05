#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module fragment_writer_tb \
 --Mdir build/fragment_writer_test build/render/fragment_writer.sv \
 tests/fragment_writer_tb.sv
build/fragment_writer_test/Vfragment_writer_tb
