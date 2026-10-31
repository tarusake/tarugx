#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module framebuffer_bridge_tb \
  --Mdir build/framebuffer_test build/memory/framebuffer_bridge.sv tests/framebuffer_bridge_tb.sv
build/framebuffer_test/Vframebuffer_bridge_tb
