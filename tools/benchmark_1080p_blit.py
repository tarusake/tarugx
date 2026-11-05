#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Time a full 1080p VRAM blit and flip to the copied page."""

import json

from recover_pcie import Backend


REMOTE = r'''
import json
import mmap
from pathlib import Path
import struct
import time

device = Path('/sys/bus/pci/devices/0000:01:00.0')
assert not (device/'driver').exists(), 'unbind the DRM driver before raw register access'
frame_bytes = 1920 * 1080 * 4
with (device/'resource0').open('r+b', buffering=0) as register_file, \
     mmap.mmap(register_file.fileno(), 0x10000, flags=mmap.MAP_SHARED,
               prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
    def read(offset):
        return struct.unpack_from('<I', regs, offset)[0]
    def write(offset, value):
        struct.pack_into('<I', regs, offset, value)
        read(offset)

    assert read(0) == 0x54475055
    assert read(4) >= 0x00010002
    source = read(0x34)
    assert source in (0, frame_bytes), hex(source)
    target = frame_bytes if source == 0 else 0
    write(0x80, source)
    write(0x84, target)
    write(0x88, 1920 * 4)
    write(0x8c, 1920 * 4)
    write(0x90, (1080 << 16) | 1920)
    started = time.monotonic_ns()
    write(0x94, 1)
    deadline = time.monotonic() + 2
    status = read(0x98)
    while not (status & 0x2) and time.monotonic() < deadline:
        status = read(0x98)
    elapsed_ns = time.monotonic_ns() - started
    assert status & 0x2 and not status & 0x5, hex(status)

    write(0x1c, target)
    write(0x20, 1920 * 4)
    write(0x24, 1)
    write(0x10, 1)
    deadline = time.monotonic() + 0.2
    while read(0x34) != target and time.monotonic() < deadline:
        pass
    assert read(0x34) == target
    print(json.dumps({'blit_ns': elapsed_ns, 'blit_ms': elapsed_ns / 1e6,
                      'source': source, 'target': target, 'status': status}))
'''


def main() -> None:
    output = Backend().remote(REMOTE, "benchmark 1080p VRAM blit", timeout=30)
    result = json.loads(output.strip().splitlines()[-1])
    if result["status"] != 2:
        raise RuntimeError(f"unexpected blit status: {result}")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
