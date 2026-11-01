#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
veryl build
python3 tools/make_rom_mem.py build/uefi/TaruGxGop.rom build/uefi/TaruGxGop.mem
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module option_rom_diagnostic_tb \
 --Mdir build/option_rom_diagnostic_test build/pcie/*.sv build/registers/gpu_registers.sv fpga/tang_mega_138k/tarugx_option_rom_storage.sv tests/option_rom_diagnostic_tb.sv
build/option_rom_diagnostic_test/Voption_rom_diagnostic_tb
