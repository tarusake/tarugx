#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module bar2_tlp_bridge_tb \
 --Mdir build/bar2_test build/pcie/bar2_tlp_bridge.sv build/pcie/tlp_request_decoder.sv \
 build/pcie/tlp_completion_builder.sv tests/bar2_tlp_bridge_tb.sv
build/bar2_test/Vbar2_tlp_bridge_tb
