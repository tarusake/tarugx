#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD \
 --top-module framebuffer_request_arbiter_tb \
 --Mdir build/framebuffer_request_arbiter_test \
 build/memory/framebuffer_request_arbiter.sv tests/framebuffer_request_arbiter_tb.sv
build/framebuffer_request_arbiter_test/Vframebuffer_request_arbiter_tb
