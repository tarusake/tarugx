#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module ddr3_native_adapter_tb \
 --Mdir build/ddr3_native_test build/memory/ddr3_native_adapter.sv tests/ddr3_native_adapter_tb.sv
build/ddr3_native_test/Vddr3_native_adapter_tb
