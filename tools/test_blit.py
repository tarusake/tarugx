#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Exercise the TaruGX VRAM blit engine through BAR0 and verify BAR2."""

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
BLIT_SRC_ADDR = 0x80
BLIT_DST_ADDR = 0x84
BLIT_SRC_STRIDE = 0x88
BLIT_DST_STRIDE = 0x8C
BLIT_SIZE = 0x90
BLIT_COMMAND = 0x94
BLIT_STATUS = 0x98
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
    parser.add_argument("--source", type=lambda value: int(value, 0), default=0x3FC000)
    parser.add_argument("--destination", type=lambda value: int(value, 0), default=0x3FE000)
    parser.add_argument("--source-stride", type=lambda value: int(value, 0), default=128)
    parser.add_argument("--destination-stride", type=lambda value: int(value, 0), default=160)
    parser.add_argument("--width", type=int, default=17)
    parser.add_argument("--height", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=1.0)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        parser.error("invalid PCI BDF")
    if args.source & 3 or args.destination & 3 or args.width < 1 or args.width > 0xFFFF:
        parser.error("addresses must be pixel-aligned and width must be 1..65535")
    if args.height < 1 or args.height > 0xFFFF:
        parser.error("height must be 1..65535")
    for name, stride in (("source", args.source_stride),
                         ("destination", args.destination_stride)):
        if stride < args.width * 4 or stride & 3:
            parser.error(f"{name} stride must be aligned and cover one row")
    source_end = args.source + (args.height - 1) * args.source_stride + args.width * 4
    destination_end = (args.destination + (args.height - 1) * args.destination_stride
                       + args.width * 4)
    if source_end > VRAM_SIZE or destination_end > VRAM_SIZE:
        parser.error("source or destination rectangle is outside VRAM")

    device = Path("/sys/bus/pci/devices") / args.bdf
    if ((device / "vendor").read_text().strip(), (device / "device").read_text().strip()) != (
            "0x22c2", "0x1100"):
        raise RuntimeError("not the expected TaruGX device")
    if resource_size(device, 0) != BAR0_SIZE or resource_size(device, 2) != VRAM_SIZE:
        raise RuntimeError("unexpected BAR size")

    def read_rows(address: int, stride: int) -> list[bytes]:
        result = subprocess.run(
            [sys.executable, "-c", BAR_READER, str(device / "resource2"),
             str(address), str(stride), str(args.height)],
            check=True, capture_output=True)
        if len(result.stdout) != stride * args.height:
            raise RuntimeError("short BAR2 read")
        return [result.stdout[row * stride:(row + 1) * stride]
                for row in range(args.height)]

    destination_before = read_rows(args.destination, args.destination_stride)
    expected_rows = []
    with (device / "resource2").open("r+b", buffering=0) as vram_file, \
            mmap.mmap(vram_file.fileno(), VRAM_SIZE, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE) as vram:
        for row in range(args.height):
            pixels = b"".join(struct.pack("<I", 0xA5000000 | row << 16 | column)
                              for column in range(args.width))
            expected_rows.append(pixels)
            for offset in range(0, len(pixels), 4):
                struct.pack_into("<I", vram, args.source + row * args.source_stride + offset,
                                 struct.unpack_from("<I", pixels, offset)[0])
        struct.unpack_from("<I", vram, args.source)

    source_after = read_rows(args.source, args.source_stride)
    for row, expected in enumerate(expected_rows):
        if source_after[row][:len(expected)] != expected:
            raise RuntimeError(f"source BAR2 write verification failed on row {row}")

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
        write_reg(BLIT_SRC_ADDR, args.source)
        write_reg(BLIT_DST_ADDR, args.destination)
        write_reg(BLIT_SRC_STRIDE, args.source_stride)
        write_reg(BLIT_DST_STRIDE, args.destination_stride)
        write_reg(BLIT_SIZE, (args.height << 16) | args.width)
        write_reg(BLIT_COMMAND, 1)

        time.sleep(0.01)
        deadline = time.monotonic() + args.timeout
        status = read_reg(BLIT_STATUS)
        while not (status & 0x2) and time.monotonic() < deadline:
            status = read_reg(BLIT_STATUS)
        if not (status & 0x2):
            raise RuntimeError(f"blit timed out, status={status:#x}")
        if status & 0x5:
            raise RuntimeError(f"blit completed with busy/error status={status:#x}")

    destination_after = read_rows(args.destination, args.destination_stride)
    for row, expected in enumerate(expected_rows):
        if destination_after[row][:len(expected)] != expected:
            got = struct.unpack_from("<I", destination_after[row])[0]
            want = struct.unpack_from("<I", expected)[0]
            raise RuntimeError(
                f"blit pixels differ on row {row}: expected {want:#010x}, got {got:#010x}")
        if destination_after[row][len(expected):] != destination_before[row][len(expected):]:
            raise RuntimeError(f"blit changed bytes outside the rectangle on row {row}")

    print(f"Blit PASS: {args.width}x{args.height}, source={args.source:#x}, "
          f"destination={args.destination:#x}, status={status:#x}")


if __name__ == "__main__":
    main()
