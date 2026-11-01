#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module scanout_pipeline_tb \
 --Mdir build/scanout_pipeline_test -f tarugx.f tests/scanout_pipeline_tb.sv
build/scanout_pipeline_test/Vscanout_pipeline_tb
