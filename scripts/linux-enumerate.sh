#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
set -euo pipefail

vendor_id=${TARUGX_VENDOR_ID:-22c2}
device_id=${TARUGX_DEVICE_ID:-1100}

if ! command -v lspci >/dev/null; then
    echo "error: lspci is required (install pciutils)" >&2
    exit 1
fi

matches=$(lspci -Dnnd "${vendor_id}:${device_id}" || true)
if [[ -z ${matches} ]]; then
    echo "TaruGX candidate ${vendor_id}:${device_id} was not enumerated" >&2
    exit 2
fi

printf '%s\n' "${matches}"
while read -r bdf _; do
    echo
    lspci -s "${bdf}" -nnvv
    if [[ -x ./build/tools/tarugx-mmio ]]; then
        ./build/tools/tarugx-mmio "${bdf}" identify
    fi
done <<<"${matches}"
