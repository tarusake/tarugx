#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module option_rom_completion_plan_tb \
 --Mdir build/option_rom_completion_plan_test build/pcie/option_rom_completion_plan.sv tests/option_rom_completion_plan_tb.sv
build/option_rom_completion_plan_test/Voption_rom_completion_plan_tb
