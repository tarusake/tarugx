#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sample=${SIPEED_HDMI_DIR:?Set SIPEED_HDMI_DIR to the official sample directory}
out="$repo/build/gowin_video"
mkdir -p "$out"
# Adapt the official Rev.B PLL generator settings. PFD must be >=19 MHz.
# 50 MHz / 2 * (50 + 3/8) = 1259.375 MHz VCO; outputs are
# 25.1875 MHz pixels and 125.9375 MHz DDR (59.970238 Hz frame rate).
sed -e 's/-mod_name Gowin_PLL/-mod_name Tarugx_Video_PLL/' \
    -e 's/-file_name gowin_pll/-file_name tarugx_video_pll/' \
    -e "s|^-path .*|-path $out/|" \
    -e 's/^-idiv_sel 1$/-idiv_sel 2/' \
    -e 's/^-mdiv_sel 30$/-mdiv_sel 50/' \
    -e 's/^-mdiv_frac_sel 0$/-mdiv_frac_sel 3/' \
    -e 's/^-odiv0_sel 10$/-odiv0_sel 50/' \
    -e 's/^-odiv1_sel 2$/-odiv1_sel 10/' \
    "$sample/gowin_pll/gowin_pll.mod" > "$out/video.mod"
"${GOWIN_MODGEN:-$HOME/Gowin/V1.9.12.03/IDE/bin/GowinModGen}" -do "$out/video.mod"
