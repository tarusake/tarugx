#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module pcie_memory_pipeline_tb \
 --Mdir build/pcie_memory_pipeline_test -f tarugx.f tests/pcie_memory_pipeline_tb.sv
build/pcie_memory_pipeline_test/Vpcie_memory_pipeline_tb
