#!/bin/sh
set -eu
veryl build
rm -rf build/render_rgb565_test
verilator --binary --timing -Wno-fatal --top-module render_rgb565_tb \
  -Mdir build/render_rgb565_test tests/render_rgb565_tb.sv $(cat tarugx.f)
build/render_rgb565_test/Vrender_rgb565_tb
