#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
# Authorized destructive operation: replace external Flash with this GPU image.
set -eu
cd "$(dirname "$0")/.."
out=build/gowin_framebuffer/tarugx/impl/pnr
python3 scripts/check_gowin_timing.py "$out/tarugx_tr_content.html"
test -s "$out/tarugx.fs"
snapshot=$(mktemp -d "$(pwd)/build/framebuffer-flash-XXXXXX")
cp "$out/tarugx.fs" "$snapshot/framebuffer.fs"
cp "$out/tarugx_tr_content.html" "$snapshot/timing.html"
sha256sum "$snapshot/framebuffer.fs"
echo "Programming log: $snapshot/program.log"
# Keep an immutable input while Programmer is running.  The Python wrapper
# stops and restores the host, verifies Flash contents, reloads from Flash and
# checks the FPGA UserCode before reporting success.
python3 tools/program_framebuffer_flash.py "$snapshot/framebuffer.fs" \
  > "$snapshot/program.log"
cat "$snapshot/program.log"
