#!/bin/sh
set -eu
# Requires a normally licensed Gowin EDA environment.
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
gowin_root=${GOWIN_ROOT:-$HOME/Gowin/V1.9.12.03}
sample=${SIPEED_PCIE_GEN2_DIR:?Set SIPEED_PCIE_GEN2_DIR to the official sample directory}
data_dir="$gowin_root/IDE/ipcore/SERDES_IP/data/Upar_Arbiter"
params="$sample/src/serdes/upar_arbiter/temp/Upar_Arbiter"
out="$repo/build/gowin_upar_b"
mkdir -p "$out"
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="$gowin_root/IDE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="/lib/x86_64-linux-gnu/libstdc++.so.6:/lib/x86_64-linux-gnu/libfreetype.so.6${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$gowin_root/IDE/bin/GowinSynthesis" \
    -i "$data_dir/upar_arbiter.v" "$data_dir/upar_arbiter_wrap.v" \
    -inc "$params" "$data_dir" \
    -o "$out/upar_arbiter.vg" -ot "$out/upar_arbiter_tmp.v" \
    -top '~upar_arbiter_wrap.SerDes_Top' -pn GW5AST-LV138PG484AC2/I1 \
    -disable_insert_pad 1 -enable_dsrm 0 -ram_balance -ram_rw_check 1 \
    -vcc 0.9 -vccx 1.8 -verilog_language sysv-2017
