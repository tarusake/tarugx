#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Fill DDR with color bars and enable the experimental 1080p scanout.

Run this only after programming the 1080p60_scanout SRAM image.  The command
leaves the DRM driver and desktop stopped so they cannot replace the 7680-byte
stride with the release driver's fixed 640-pixel mode.  Reload the known-good
Flash image with tools/reload_framebuffer_flash.py when the observation ends.
"""

import json
from pathlib import Path
import sys

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from recover_pcie import Backend


REMOTE = r"""
import json
import mmap
from pathlib import Path
import struct
import subprocess
import time

ENDPOINT = '0000:01:00.0'
BAR0_SIZE = 0x10000
GPU_ID = 0x54475055
WIDTH = 1920
HEIGHT = 1080
STRIDE = WIDTH * 4
FB_BASE = 0

DISPLAY_CTRL = 0x10
FB_BASE_REG = 0x1c
FB_STRIDE = 0x20
FB_COMMIT = 0x24
FILL_ADDR = 0x60
FILL_STRIDE = 0x64
FILL_SIZE = 0x68
FILL_COLOR = 0x6c
FILL_COMMAND = 0x70
FILL_STATUS = 0x74

# XRGB8888 values. Each rectangle is 180 rows and the full frame occupies
# 8,294,400 bytes, below the scanout engine's 16 MiB DDR address limit.
COLORS = (0x00ff0000, 0x0000ff00, 0x000000ff,
          0x0000ffff, 0x00ff00ff, 0x00ffff00)

endpoint = Path('/sys/bus/pci/devices') / ENDPOINT
assert endpoint.resolve(strict=True).name == ENDPOINT, 'Endpoint missing'
assert endpoint.joinpath('vendor').read_text().strip() == '0x22c2', 'Unexpected vendor'
assert endpoint.joinpath('device').read_text().strip() == '0x1100', 'Unexpected device'

subprocess.run(['systemctl', 'stop', 'tarugx-main-display'], check=True)
try:
    for unit in ('tarugx-xorg', 'tarugx-desktop', 'tarugx-terminal'):
        subprocess.run(['systemctl', 'stop', unit], check=False, timeout=10)
    subprocess.run(['modprobe', '-r', 'tarugx'], check=True)
    assert not endpoint.joinpath('driver').exists(), 'Driver remains bound'

    fields = endpoint.joinpath('resource').read_text().splitlines()[0].split()
    start, end = (int(value, 16) for value in fields[:2])
    assert start and end - start + 1 == BAR0_SIZE, 'Unexpected BAR0 size'

    with endpoint.joinpath('resource0').open('r+b', buffering=0) as source, \
            mmap.mmap(source.fileno(), BAR0_SIZE, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
        def read_reg(offset):
            return struct.unpack_from('<I', regs, offset)[0]

        def write_reg(offset, value):
            struct.pack_into('<I', regs, offset, value)
            read_reg(offset)  # Flush the posted PCIe write.

        assert read_reg(0) == GPU_ID, 'GPU_ID mismatch'
        write_reg(DISPLAY_CTRL, 0)

        band_height = HEIGHT // len(COLORS)
        statuses = []
        for band, color in enumerate(COLORS):
            write_reg(FILL_ADDR, FB_BASE + band * band_height * STRIDE)
            write_reg(FILL_STRIDE, STRIDE)
            write_reg(FILL_SIZE, (band_height << 16) | WIDTH)
            write_reg(FILL_COLOR, color)
            write_reg(FILL_COMMAND, 1)

            # The sticky done flag is cleared asynchronously by the start.
            time.sleep(0.01)
            deadline = time.monotonic() + 5
            status = read_reg(FILL_STATUS)
            while not (status & 0x2) and time.monotonic() < deadline:
                status = read_reg(FILL_STATUS)
            assert status & 0x2, 'Fill timed out'
            assert not (status & 0x5), 'Fill busy/error after completion'
            statuses.append(status)

        write_reg(FB_BASE_REG, FB_BASE)
        write_reg(FB_STRIDE, STRIDE)
        write_reg(FB_COMMIT, 1)
        write_reg(DISPLAY_CTRL, 1)

        assert read_reg(FB_BASE_REG) == FB_BASE, 'FB base readback mismatch'
        assert read_reg(FB_STRIDE) == STRIDE, 'FB stride readback mismatch'
        assert read_reg(DISPLAY_CTRL) == 1, 'Display enable readback mismatch'

    print(json.dumps({'configured': True, 'width': WIDTH, 'height': HEIGHT,
                      'stride': STRIDE, 'fill_status': statuses}))
except Exception:
    # If configuration did not complete, return the host to its managed state.
    subprocess.run(['modprobe', 'tarugx'], check=False)
    subprocess.run(['systemctl', 'start', 'tarugx-main-display'], check=False)
    raise
"""


def main() -> None:
    state = json.loads(Backend().remote(REMOTE, 'configure 1080p scanout', 120))
    if not state.get('configured') or state.get('stride') != 7680:
        raise RuntimeError('1080p scanout configuration was not confirmed')
    print(json.dumps(state))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        raise SystemExit('1080p scanout configuration stopped: ' + str(error))
