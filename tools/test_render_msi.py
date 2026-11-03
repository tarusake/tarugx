#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Verify a queued render completion raises MSI and restore the IRQ mask."""

import argparse
import mmap
from pathlib import Path
import re
import struct
import time


BAR0_SIZE = 0x10000
GPU_ID = 0x54475055
IRQ_ENABLE = 0x30
FILL_ADDR = 0x60
FILL_STRIDE = 0x64
FILL_SIZE = 0x68
FILL_COLOR = 0x6C
QUEUE_FENCE = 0xA0
QUEUE_SUBMIT = 0xA4
QUEUE_STATUS = 0xA8
QUEUE_COMPLETED = 0xAC
QUEUE_READY = 1 << 3
QUEUE_ERROR = 1 << 5


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bdf", default="0000:01:00.0")
    parser.add_argument("--timeout", type=float, default=1.0)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        parser.error("invalid PCI BDF")

    device = Path("/sys/bus/pci/devices") / args.bdf
    if ((device / "vendor").read_text().strip(), (device / "device").read_text().strip()) != (
            "0x22c2", "0x1100"):
        raise RuntimeError("not the expected TaruGX device")
    interrupt_counter = device / "msi_interrupts"
    if not interrupt_counter.is_file():
        raise RuntimeError("tarugx MSI counter is unavailable")

    with (device / "resource0").open("r+b", buffering=0) as regs_file, \
            mmap.mmap(regs_file.fileno(), BAR0_SIZE, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
        def read_reg(offset: int) -> int:
            return struct.unpack_from("<I", regs, offset)[0]

        def write_reg(offset: int, value: int) -> None:
            struct.pack_into("<I", regs, offset, value)
            read_reg(offset)

        if read_reg(0) != GPU_ID:
            raise RuntimeError("GPU_ID mismatch")
        status = read_reg(QUEUE_STATUS)
        if not (status & QUEUE_READY) or status & QUEUE_ERROR:
            raise RuntimeError(f"queue is not ready, status={status:#x}")

        original_irq_mask = read_reg(IRQ_ENABLE)
        before = 0
        fence = (time.monotonic_ns() >> 10) & 0xFFFFFFFF
        try:
            # Stop VBlank as an MSI source briefly so the observed increment
            # can only be caused by this render completion.
            write_reg(IRQ_ENABLE, 2)
            time.sleep(0.03)
            before = int(interrupt_counter.read_text())
            write_reg(FILL_ADDR, 0x3FFFFC)
            write_reg(FILL_STRIDE, 4)
            write_reg(FILL_SIZE, 0x00010001)
            write_reg(FILL_COLOR, 0x4D534921)
            write_reg(QUEUE_FENCE, fence)
            write_reg(QUEUE_SUBMIT, 1)

            deadline = time.monotonic() + args.timeout
            completed = read_reg(QUEUE_COMPLETED)
            while completed != fence and time.monotonic() < deadline:
                completed = read_reg(QUEUE_COMPLETED)
            if completed != fence:
                raise RuntimeError(f"render fence timed out: expected {fence:#x}, got {completed:#x}")
            after = int(interrupt_counter.read_text())
            while after == before and time.monotonic() < deadline:
                after = int(interrupt_counter.read_text())
            if after <= before:
                raise RuntimeError("render fence completed without a Linux MSI interrupt")
            status = read_reg(QUEUE_STATUS)
            if status & QUEUE_ERROR:
                raise RuntimeError(f"render completed with queue status={status:#x}")
        finally:
            write_reg(IRQ_ENABLE, original_irq_mask)

    print(f"Render MSI PASS: fence={fence:#010x}, interrupts={before}->{after}, "
          f"restored IRQ mask={original_irq_mask:#x}")


if __name__ == "__main__":
    main()
