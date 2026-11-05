#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
# Volatile update: program FPGA SRAM, leaving external Flash unchanged.
set -eu
cd "$(dirname "$0")/.."
out=${TARUGX_SRAM_OUT:-build/gowin_framebuffer_pipeline/tarugx/impl/pnr}
expected_video_mode=${TARUGX_EXPECT_VIDEO_MODE:-}
python3 scripts/check_gowin_timing.py "$out/tarugx_tr_content.html"
test -s "$out/tarugx.fs"
test -n "$expected_video_mode" || {
    echo "TARUGX_EXPECT_VIDEO_MODE must name the intended video mode" >&2
    exit 1
}
python3 scripts/bitstream_manifest.py verify "$out" \
    --expected-video-mode "$expected_video_mode"
exec python3 tools/program_framebuffer_sram.py "$out/tarugx.fs"
