#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sample=${SIPEED_HDMI_DIR:?Set SIPEED_HDMI_DIR to the official sample directory}
out="$repo/build/gowin_video"
mkdir -p "$out"
# Adapt the official Rev.B PLL generator settings. PFD must be >=19 MHz.
# The default remains the verified 640x480 mode. 1080p60 uses 148.125 MHz
# pixels and 740.625 MHz DDR, which produces 59.848485 Hz and stays within
# the GW5AST-138 PLL and 1.6 Gbps LVDS-TX limits.
mode=${TARUGX_VIDEO_MODE:-480p60}
case "$mode" in
    480p60)
        mdiv=50
        mfrac=3
        odiv0=50
        odiv1=10
        ;;
    1080p60)
        # 50 / 2 * 29.625 = 740.625 MHz VCO.  Divide by five for
        # 148.125 MHz pixels and expose the VCO directly as the x5 DDR
        # serializer clock.  Keeping the VCO below 1.3 GHz is required by
        # the GW5AST PLL timing model.
        mdiv=29
        mfrac=5
        odiv0=5
        odiv1=1
        ;;
    *)
        echo "Unsupported TARUGX_VIDEO_MODE: $mode" >&2
        exit 1
        ;;
esac
sed -e 's/-mod_name Gowin_PLL/-mod_name Tarugx_Video_PLL/' \
    -e 's/-file_name gowin_pll/-file_name tarugx_video_pll/' \
    -e "s|^-path .*|-path $out/|" \
    -e 's/^-idiv_sel 1$/-idiv_sel 2/' \
    -e "s/^-mdiv_sel 30$/-mdiv_sel $mdiv/" \
    -e "s/^-mdiv_frac_sel 0$/-mdiv_frac_sel $mfrac/" \
    -e "s/^-odiv0_sel 10$/-odiv0_sel $odiv0/" \
    -e "s/^-odiv1_sel 2$/-odiv1_sel $odiv1/" \
    "$sample/gowin_pll/gowin_pll.mod" > "$out/video.mod"
"${GOWIN_MODGEN:-$HOME/Gowin/V1.9.12.03/IDE/bin/GowinModGen}" -do "$out/video.mod"
