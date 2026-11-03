#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module fill_command_cdc_tb \
 --Mdir build/fill_command_cdc_test build/render/render_command_cdc.sv \
 build/render/fill_command_cdc.sv tests/fill_command_cdc_tb.sv
build/fill_command_cdc_test/Vfill_command_cdc_tb
