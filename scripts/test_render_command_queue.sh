#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module render_command_queue_tb \
 --Mdir build/render_command_queue_test build/render/render_command_queue.sv tests/render_command_queue_tb.sv
build/render_command_queue_test/Vrender_command_queue_tb
