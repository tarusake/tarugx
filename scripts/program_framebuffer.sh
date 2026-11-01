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
python3 tools/ftdi-driver.py detach
trap 'python3 tools/ftdi-driver.py attach || true' EXIT
# Keep an immutable input while Programmer is running.
gowin-programmer --device GW5AST-138B --operation_index 52 \
  --chain_index 0 --chain_size 1 --frequency 2.5MHz > "$snapshot/erase.log" 2>&1
gowin-programmer --device GW5AST-138B --operation_index 53 \
  --chain_index 0 --chain_size 1 --frequency 2.5MHz \
  --fsFile "$snapshot/framebuffer.fs" > "$snapshot/program.log" 2>&1
gowin-programmer --device GW5AST-138B --operation_index 1 \
  --chain_index 0 --chain_size 1 --frequency 2.5MHz
# Reprogram returns before Flash configuration finishes. An immediate code
# read can return zero even though programming succeeded.
sleep 2
gowin-programmer --device GW5AST-138B --operation_index 73 \
  --chain_index 0 --chain_size 1 --frequency 2.5MHz
echo "Programmer completed; Flash readback verification was not performed."
