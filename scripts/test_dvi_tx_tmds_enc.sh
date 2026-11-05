#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")/.."
verilator --binary --timing --assert -Wno-fatal -Wno-TIMESCALEMOD \
    --top-module dvi_tx_tmds_enc_tb --Mdir build/dvi_tx_tmds_enc_test \
    fpga/tang_mega_138k/dvi_tx_tmds_enc.sv tests/dvi_tx_tmds_enc_tb.sv
build/dvi_tx_tmds_enc_test/Vdvi_tx_tmds_enc_tb
