#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module framebuffer_display_tb \
 --Mdir build/framebuffer_display_test build/display/video_timing.sv \
 build/display/framebuffer_display.sv tests/framebuffer_display_tb.sv
build/framebuffer_display_test/Vframebuffer_display_tb
