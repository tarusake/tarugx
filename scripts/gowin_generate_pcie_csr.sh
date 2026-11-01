#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sample_dir=${SIPEED_PCIE_GEN2_DIR:?Set SIPEED_PCIE_GEN2_DIR to the official sample directory}
base_csr="$sample_dir/src/serdes/serdes.csr"
output_dir="$project_root/build/gowin_pcie_x1"
output_csr="$output_dir/serdes.csr"
expected_sha=a72403005712764121b066d18b169084803105260e8465d7139dc2fee3751817

actual_sha=$(sha256sum "$base_csr" | awk '{print $1}')
if [ "$actual_sha" != "$expected_sha" ]; then
    echo "Unsupported Sipeed standard-Dock Gen2 CSR: $actual_sha" >&2
    exit 1
fi

mkdir -p "$output_dir"

# Preserve both the complete board-qualified SerDes/CMU setup and the exact
# initialization order.  In particular, these configuration-space writes must
# remain before the reference design's final 0xb001d8 controller write.
# BAR0 = 64 KiB, 32-bit, non-prefetchable; BAR2 = 4 MiB, 32-bit, prefetchable.
sed \
    -e 's/0x05800000/0x03800000/' \
    -e 's/0xb00048,0xFFF00004/0xb00048,0xFFFF0000/' \
    -e 's/0xb00050,0xFFF00004/0xb00050,0xFFC00008/' \
    -e 's/0xb00058,0xFFF00004/0xb00058,0x00000000/' \
    "$base_csr" > "$output_csr"

echo "Generated $output_csr"
