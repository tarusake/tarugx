#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module memory_request_cdc_tb \
 --Mdir build/memory_cdc_test build/memory/memory_request_cdc.sv tests/memory_request_cdc_tb.sv
build/memory_cdc_test/Vmemory_request_cdc_tb
