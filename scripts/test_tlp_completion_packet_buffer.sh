#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module tlp_completion_packet_buffer_tb \
 --Mdir build/tlp_completion_packet_buffer_test build/pcie/tlp_completion_packet_buffer.sv tests/tlp_completion_packet_buffer_tb.sv
build/tlp_completion_packet_buffer_test/Vtlp_completion_packet_buffer_tb
