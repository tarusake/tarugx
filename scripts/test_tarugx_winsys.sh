#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
cc -O2 -Wall -Wextra -Werror \
    -Imesa -Idrivers/tarugx -I/usr/include/libdrm \
    mesa/tarugx_winsys.c tests/tarugx_winsys_test.c \
    -ldrm -o build/tarugx-winsys-test
