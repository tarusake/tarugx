#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Draw one TaruGX hardware triangle and verify every RGB565 target pixel."""

import argparse
import mmap
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

from triangle_reference import Vertex, rasterize


BAR0_SIZE = 0x10000
VRAM_SIZE = 0x400000
GPU_ID = 0x54475055
TRI_BASE = 0x100
TRI_STRIDE = 0x104
TRI_SIZE = 0x108
TRI_X0 = 0x10C
TRI_Y0 = 0x110
TRI_X1 = 0x114
TRI_Y1 = 0x118
TRI_X2 = 0x11C
TRI_Y2 = 0x120
TRI_COLOR = 0x124
TRI_COMMAND = 0x128
TRI_STATUS = 0x12C
TRI_WRITES = 0x130
BAR_READER = r"""
import mmap, sys
path, address, length = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "rb", buffering=0) as source:
    with mmap.mmap(source.fileno(), 0x400000, flags=mmap.MAP_SHARED,
                   prot=mmap.PROT_READ) as bar:
        sys.stdout.buffer.write(bar[address:address + length])
"""


def resource_size(device: Path, index: int) -> int:
    fields = (device / "resource").read_text().splitlines()[index].split()
    start, end = (int(value, 16) for value in fields[:2])
    return end - start + 1 if start else 0


def rgb565(color: int) -> int:
    return ((color >> 8) & 0xF800) | ((color >> 5) & 0x07E0) | ((color >> 3) & 0x001F)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bdf", default="0000:01:00.0")
    parser.add_argument("--address", type=lambda value: int(value, 0), default=0x3F0000)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--stride", type=lambda value: int(value, 0), default=128)
    parser.add_argument("--color", type=lambda value: int(value, 0), default=0x00FF8000)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        parser.error("invalid PCI BDF")
    if not (4 <= args.width <= 0xFFFF and 4 <= args.height <= 0xFFFF):
        parser.error("width and height must be 4..65535")
    if args.address & 1 or args.stride & 1 or args.stride < args.width * 2:
        parser.error("address/stride must be RGB565-aligned and stride must cover one row")
    length = (args.height - 1) * args.stride + args.width * 2
    if args.address + length > VRAM_SIZE or not (0 <= args.color <= 0xFFFFFFFF):
        parser.error("target or color is outside the supported range")

    vertices = [(1, 1), (args.width - 2, 2), (2, args.height - 2)]
    device = Path("/sys/bus/pci/devices") / args.bdf
    if ((device / "vendor").read_text().strip(), (device / "device").read_text().strip()) != (
            "0x22c2", "0x1100"):
        raise RuntimeError("not the expected TaruGX device")
    if resource_size(device, 0) != BAR0_SIZE or resource_size(device, 2) != VRAM_SIZE:
        raise RuntimeError("unexpected BAR size")

    def read_target() -> bytes:
        result = subprocess.run(
            [sys.executable, "-c", BAR_READER, str(device / "resource2"),
             str(args.address), str(length)], check=True, capture_output=True)
        if len(result.stdout) != length:
            raise RuntimeError("short BAR2 read")
        return result.stdout

    before = read_target()
    with (device / "resource0").open("r+b", buffering=0) as register_file, \
            mmap.mmap(register_file.fileno(), BAR0_SIZE, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE) as registers:
        def read_reg(offset: int) -> int:
            return struct.unpack_from("<I", registers, offset)[0]

        def write_reg(offset: int, value: int) -> None:
            struct.pack_into("<I", registers, offset, value & 0xFFFFFFFF)
            read_reg(offset)

        if read_reg(0) != GPU_ID:
            raise RuntimeError("GPU_ID mismatch")
        write_reg(TRI_BASE, args.address)
        write_reg(TRI_STRIDE, args.stride)
        write_reg(TRI_SIZE, (args.height << 16) | args.width)
        coordinate_registers = (TRI_X0, TRI_Y0, TRI_X1, TRI_Y1, TRI_X2, TRI_Y2)
        for register, coordinate in zip(coordinate_registers, sum(vertices, ())):
            write_reg(register, coordinate << 8)
        write_reg(TRI_COLOR, args.color)
        write_reg(TRI_COMMAND, 1)
        time.sleep(0.01)
        deadline = time.monotonic() + args.timeout
        status = read_reg(TRI_STATUS)
        while not (status & 2) and time.monotonic() < deadline:
            status = read_reg(TRI_STATUS)
        writes = read_reg(TRI_WRITES)
        if not (status & 2):
            raise RuntimeError(f"triangle timed out, status={status:#x}, writes={writes}")
        if status & 5:
            raise RuntimeError(f"triangle completed with busy/error status={status:#x}")

    after = read_target()
    expected_color = struct.pack("<H", rgb565(args.color))
    coverage = rasterize(tuple(Vertex.pixel(*vertex) for vertex in vertices),
                         args.width, args.height)
    expected_writes = 0
    for y in range(args.height):
        for x in range(args.width):
            offset = y * args.stride + x * 2
            expected = expected_color if (x, y) in coverage else before[offset:offset + 2]
            if after[offset:offset + 2] != expected:
                got = struct.unpack_from("<H", after, offset)[0]
                raise RuntimeError(f"pixel mismatch at ({x},{y}): got {got:#06x}")
            expected_writes += (x, y) in coverage
        row_end = y * args.stride + args.width * 2
        next_row = min((y + 1) * args.stride, length)
        if after[row_end:next_row] != before[row_end:next_row]:
            raise RuntimeError(f"triangle changed row padding at y={y}")
    if writes != expected_writes:
        raise RuntimeError(f"write count mismatch: hardware={writes}, expected={expected_writes}")
    print(f"Triangle PASS: {args.width}x{args.height}, pixels={writes}, "
          f"address={args.address:#x}, RGB565={rgb565(args.color):#06x}")


if __name__ == "__main__":
    main()
