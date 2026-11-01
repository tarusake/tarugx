#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module memory_arbiter_tb \
 --Mdir build/memory_arbiter_test build/memory/memory_arbiter.sv tests/memory_arbiter_tb.sv
build/memory_arbiter_test/Vmemory_arbiter_tb
