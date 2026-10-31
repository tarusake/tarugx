#!/bin/sh
# Re-synthesize the official Rev.B camera example configuration for our part.
# This does not validate the physical memory wiring or timing on the board.
set -eu
# Requires a normally licensed Gowin EDA environment.
repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
gowin_root=${GOWIN_ROOT:-$HOME/Gowin/V1.9.12.03}
sample=${SIPEED_DDR3_B_DIR:?Set SIPEED_DDR3_B_DIR to the official sample directory}
data="$gowin_root/IDE/ipcore/DDR3/data/ddr3_1_4code_hs"
out="$repo/build/gowin_ddr3_b"
grep -qx 'target_device=gw5ast138b-011' "$sample/DDR3MI_400M.ipc"
mkdir -p "$out"
for file in gwmc_param.v DDR3_define.v ddr3_name.v; do
    cp "$sample/temp/DDR3/$file" "$out/$file"
done
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="$gowin_root/IDE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="/lib/x86_64-linux-gnu/libstdc++.so.6:/lib/x86_64-linux-gnu/libfreetype.so.6${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$gowin_root/IDE/bin/GowinSynthesis" \
    -i "$data/ddr3_1_4code_hs.v" "$data/DDR3_TOP.v" \
    -inc "$out" "$data" -o "$out/DDR3MI.vg" -ot "$out/DDR3MI_tmp.v" \
    -top DDR3MI -pn GW5AST-LV138PG484AC2/I1 \
    -disable_insert_pad 1 -enable_dsrm 0 -ram_balance -ram_rw_check 1 \
    -vcc 0.9 -vccx 1.8 -verilog_language sysv-2017
