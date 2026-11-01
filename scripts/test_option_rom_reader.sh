#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
cd "$(dirname "$0")/.."
veryl build
python3 tools/make_rom_mem.py build/uefi/TaruGxGop.rom build/uefi/TaruGxGop.mem
verilator --binary --timing --assert -Wno-TIMESCALEMOD --top-module option_rom_reader_tb \
 --Mdir build/option_rom_reader_test build/pcie/option_rom_reader.sv tests/option_rom_reader_tb.sv
build/option_rom_reader_test/Voption_rom_reader_tb
