#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Measure the diagnostic PCIe DMA path with scanout enabled and disabled."""

import json
import re
import statistics

from recover_pcie import Backend


REMOTE = r"""
import json
import mmap
from pathlib import Path
import struct
import sys
import time

device = Path('/sys/bus/pci/devices/0000:01:00.0')
assert device.exists(), 'TaruGX endpoint is absent'
assert (device/'vendor').read_text().strip() == '0x22c2'
assert (device/'device').read_text().strip() == '0x1100'
assert (device/'driver').resolve().name == 'tarugx'
length = 640 * 480 * 4

with (device/'resource0').open('rb', buffering=0) as regs_file, \
     mmap.mmap(regs_file.fileno(), 65536, flags=mmap.MAP_SHARED,
               prot=mmap.PROT_READ) as regs:
    gpu_version = struct.unpack_from('<I', regs, 0x04)[0]
if gpu_version >= 0x00010002:
    print(json.dumps({
        'supported': False,
        'gpu_version': hex(gpu_version),
        'reason': 'the BAR2-readable diagnostic window overlaps 1080p scanout',
    }))
    sys.exit(0)

def samples(count):
    result = []
    for _ in range(count):
        started = time.monotonic_ns()
        (device/'dma_test').write_text(str(length))
        result.append({
            'elapsed_ms': (time.monotonic_ns() - started) / 1_000_000,
            'profile': (device/'dma_profile').read_text().strip(),
            'error_code': int((device/'dma_error_code').read_text().strip()),
        })
    return result

with (device/'resource0').open('r+b', buffering=0) as regs_file, \
     mmap.mmap(regs_file.fileno(), 65536, flags=mmap.MAP_SHARED,
               prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
    display_ctrl = struct.unpack_from('<I', regs, 0x10)[0]
    try:
        scanout_on = samples(5)
        struct.pack_into('<I', regs, 0x10, 0)
        time.sleep(0.1)
        scanout_off = samples(5)
    finally:
        struct.pack_into('<I', regs, 0x10, display_ctrl)

assert all(sample['error_code'] == 0 for sample in scanout_on + scanout_off)
print(json.dumps({
    'supported': True,
    'bytes': length,
    'display_ctrl': hex(display_ctrl),
    'scanout_on': scanout_on,
    'scanout_off': scanout_off,
}))
"""


def main() -> None:
    output = Backend().remote(REMOTE, "benchmark diagnostic DMA", timeout=60)
    result = json.loads(output)
    if not result["supported"]:
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return

    def summary(samples):
        busy = [int(re.search(r"busy_cycles=(\d+)", sample["profile"])[1])
                for sample in samples]
        median_cycles = statistics.median(busy)
        return {
            "fpga_busy_ms_median": median_cycles / 100_000,
            "fpga_throughput_mb_s": result["bytes"] * 100_000_000 /
            median_cycles / 1_000_000,
            "self_test_ms_median": statistics.median(
                sample["elapsed_ms"] for sample in samples),
            "note": "self_test time includes BAR2 readback verification",
        }

    result["summary"] = {
        "scanout_on": summary(result["scanout_on"]),
        "scanout_off": summary(result["scanout_off"]),
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
