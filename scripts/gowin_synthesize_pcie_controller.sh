#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
# Requires a normally licensed Gowin EDA environment.

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
gowin_root=${GOWIN_ROOT:-$HOME/Gowin/V1.9.12.03}
data_dir="$gowin_root/IDE/ipcore/SERDES_IP/IPlib/PCIE/data"
output_dir="$project_root/build/gowin_pcie_x1"

mkdir -p "$output_dir"

export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="$gowin_root/IDE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="/lib/x86_64-linux-gnu/libstdc++.so.6:/lib/x86_64-linux-gnu/libfreetype.so.6${LD_PRELOAD:+:$LD_PRELOAD}"

exec "$gowin_root/IDE/bin/GowinSynthesis" \
    -i "$data_dir/pcie_controller_encrypt.v" "$data_dir/pcie_controller_top.v" \
    -inc "$project_root/vendor/gowin/pcie_x1" "$data_dir" \
    -o "$output_dir/pcie_controller.vg" \
    -ot "$output_dir/pcie_controller_tmp.v" \
    -top PCIE_Controller_Top \
    -pn GW5AST-LV138PG484AC2/I1 \
    -disable_insert_pad 1 \
    -enable_dsrm 0 \
    -ram_balance \
    -ram_rw_check 1 \
    -vcc 0.9 \
    -vccx 1.8 \
    -verilog_language sysv-2017
