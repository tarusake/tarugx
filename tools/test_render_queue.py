#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Submit queued TaruGX fills, wait on a fence, and verify off-screen VRAM."""

import argparse
import mmap
from pathlib import Path
import re
import struct
import subprocess
import sys
import time


BAR0_SIZE = 0x10000
VRAM_SIZE = 0x400000
GPU_ID = 0x54475055
FILL_ADDR = 0x60
FILL_STRIDE = 0x64
FILL_SIZE = 0x68
FILL_COLOR = 0x6C
QUEUE_FENCE = 0xA0
QUEUE_SUBMIT = 0xA4
QUEUE_STATUS = 0xA8
QUEUE_COMPLETED = 0xAC
QUEUE_ERROR_FENCE = 0xB0
QUEUE_READY = 1 << 3
QUEUE_ACTIVE = 1 << 4
QUEUE_ERROR = 1 << 5
BAR_READER = r"""
import mmap, struct, sys
path, address, stride, height = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
with open(path, "rb", buffering=0) as source:
    with mmap.mmap(source.fileno(), 0x400000, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ) as bar:
        sys.stdout.buffer.write(b"".join(
            struct.pack("<I", struct.unpack_from("<I", bar, address + row * stride + offset)[0])
            for row in range(height) for offset in range(0, stride, 4)))
"""


def resource_size(device: Path, index: int) -> int:
    fields = (device / "resource").read_text().splitlines()[index].split()
    start, end = (int(value, 16) for value in fields[:2])
    return end - start + 1 if start else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bdf", default="0000:01:00.0")
    parser.add_argument("--address", type=lambda value: int(value, 0), default=0x3E0000)
    parser.add_argument("--stride", type=lambda value: int(value, 0), default=512)
    parser.add_argument("--width", type=int, default=128)
    parser.add_argument("--height", type=int, default=32)
    parser.add_argument("--commands", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        parser.error("invalid PCI BDF")
    if (args.address & 3 or args.stride & 3 or args.stride < args.width * 4
            or args.width < 1 or args.width > 0xFFFF
            or args.height < 1 or args.height > 0xFFFF
            or args.commands < 2 or args.commands > 5):
        parser.error("invalid aligned rectangle or command count (2..5)")
    region_size = args.height * args.stride
    end = args.address + (args.commands - 1) * region_size + (
        args.height - 1) * args.stride + args.width * 4
    if end > VRAM_SIZE:
        parser.error("queued rectangles exceed VRAM")

    device = Path("/sys/bus/pci/devices") / args.bdf
    if ((device / "vendor").read_text().strip(), (device / "device").read_text().strip()) != (
            "0x22c2", "0x1100"):
        raise RuntimeError("not the expected TaruGX device")
    if resource_size(device, 0) != BAR0_SIZE or resource_size(device, 2) != VRAM_SIZE:
        raise RuntimeError("unexpected BAR size")

    def read_rows(address: int) -> list[bytes]:
        result = subprocess.run(
            [sys.executable, "-c", BAR_READER, str(device / "resource2"),
             str(address), str(args.stride), str(args.height)],
            check=True, capture_output=True)
        if len(result.stdout) != args.stride * args.height:
            raise RuntimeError("short BAR2 read")
        return [result.stdout[row * args.stride:(row + 1) * args.stride]
                for row in range(args.height)]

    before = [read_rows(args.address + index * region_size)
              for index in range(args.commands)]
    colors = [0x51000000 | index * 0x00110101 for index in range(args.commands)]
    first_fence = (time.monotonic_ns() >> 10) & 0xFFFFFFFF
    fences = [(first_fence + index) & 0xFFFFFFFF for index in range(args.commands)]

    with (device / "resource0").open("r+b", buffering=0) as regs_file, \
            mmap.mmap(regs_file.fileno(), BAR0_SIZE, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
        def read_reg(offset: int) -> int:
            return struct.unpack_from("<I", regs, offset)[0]

        def write_reg(offset: int, value: int) -> None:
            struct.pack_into("<I", regs, offset, value)
            read_reg(offset)

        def wait_ready(deadline: float) -> int:
            status = read_reg(QUEUE_STATUS)
            while not (status & QUEUE_READY) and time.monotonic() < deadline:
                status = read_reg(QUEUE_STATUS)
            if not (status & QUEUE_READY):
                raise RuntimeError(f"queue submission timed out, status={status:#x}")
            return status

        if read_reg(0) != GPU_ID:
            raise RuntimeError("GPU_ID mismatch")
        write_reg(QUEUE_STATUS, QUEUE_ERROR)
        deadline = time.monotonic() + args.timeout
        max_level = 0
        for index, (fence, color) in enumerate(zip(fences, colors, strict=True)):
            status = wait_ready(deadline)
            if status & QUEUE_ERROR:
                raise RuntimeError(f"queue error before submit, status={status:#x}")
            address = args.address + index * region_size
            write_reg(FILL_ADDR, address)
            write_reg(FILL_STRIDE, args.stride)
            write_reg(FILL_SIZE, (args.height << 16) | args.width)
            write_reg(FILL_COLOR, color)
            write_reg(QUEUE_FENCE, fence)
            write_reg(QUEUE_SUBMIT, 1)
            status = read_reg(QUEUE_STATUS)
            max_level = max(max_level, status & 0x7)

        final_fence = fences[-1]
        completed = read_reg(QUEUE_COMPLETED)
        while completed != final_fence and time.monotonic() < deadline:
            completed = read_reg(QUEUE_COMPLETED)
        status = read_reg(QUEUE_STATUS)
        if completed != final_fence:
            raise RuntimeError(
                f"fence timed out: expected {final_fence:#x}, got {completed:#x}, status={status:#x}")
        if status & (QUEUE_ACTIVE | QUEUE_ERROR) or status & 0x7:
            error_fence = read_reg(QUEUE_ERROR_FENCE)
            raise RuntimeError(f"queue retired with status={status:#x}, error_fence={error_fence:#x}")

    for index, color in enumerate(colors):
        after = read_rows(args.address + index * region_size)
        expected = struct.pack("<I", color) * args.width
        for row, actual in enumerate(after):
            if actual[:len(expected)] != expected:
                got = struct.unpack_from("<I", actual)[0]
                raise RuntimeError(
                    f"command {index} row {row} differs: expected {color:#010x}, got {got:#010x}")
            if actual[len(expected):] != before[index][row][len(expected):]:
                raise RuntimeError(f"command {index} changed row {row} outside its rectangle")

    print(f"Render queue PASS: {args.commands} fills, final fence={fences[-1]:#010x}, "
          f"max observed queued level={max_level}, status={status:#x}")


if __name__ == "__main__":
    main()
