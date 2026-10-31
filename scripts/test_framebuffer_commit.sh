#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module framebuffer_commit_tb \
 --Mdir build/framebuffer_commit_test -f tarugx.f tests/framebuffer_commit_tb.sv
build/framebuffer_commit_test/Vframebuffer_commit_tb
