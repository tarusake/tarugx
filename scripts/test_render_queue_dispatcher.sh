#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module render_queue_dispatcher_tb \
 --Mdir build/render_queue_dispatcher_test build/render/render_queue_dispatcher.sv tests/render_queue_dispatcher_tb.sv
build/render_queue_dispatcher_test/Vrender_queue_dispatcher_tb
