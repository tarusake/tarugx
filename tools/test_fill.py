#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Exercise the TaruGX rectangle-fill engine through BAR0 and verify BAR2."""

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
FILL_COMMAND = 0x70
FILL_STATUS = 0x74
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
    parser.add_argument("--address", type=lambda value: int(value, 0), default=0x3FE000,
                        help="byte address in VRAM (default: safe off-screen tail)")
    parser.add_argument("--stride", type=lambda value: int(value, 0), default=128)
    parser.add_argument("--width", type=int, default=17)
    parser.add_argument("--height", type=int, default=5)
    parser.add_argument("--color", type=lambda value: int(value, 0), default=0x5AA55AA5)
    parser.add_argument("--timeout", type=float, default=1.0)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        parser.error("invalid PCI BDF")
    if args.address & 3 or args.width < 1 or args.width > 0xFFFF:
        parser.error("address must be pixel-aligned and width must be 1..65535")
    if (args.height < 1 or args.height > 0xFFFF or args.stride < args.width * 4
            or args.stride & 3):
        parser.error("height must be 1..65535 and aligned stride must cover one row")
    end = args.address + (args.height - 1) * args.stride + args.width * 4
    if end > VRAM_SIZE or args.color < 0 or args.color > 0xFFFFFFFF:
        parser.error("rectangle/color is outside the supported range")

    device = Path("/sys/bus/pci/devices") / args.bdf
    if ((device / "vendor").read_text().strip(), (device / "device").read_text().strip()) != (
            "0x22c2", "0x1100"):
        raise RuntimeError("not the expected TaruGX device")
    if resource_size(device, 0) != BAR0_SIZE or resource_size(device, 2) != VRAM_SIZE:
        raise RuntimeError("unexpected BAR size")

    def read_rows() -> list[bytes]:
        # Use a fresh process for each observation.  Keeping one sysfs BAR mmap
        # across an FPGA-side write can return its earlier read completion.
        result = subprocess.run(
            [sys.executable, "-c", BAR_READER, str(device / "resource2"),
             str(args.address), str(args.stride), str(args.height)],
            check=True, capture_output=True)
        if len(result.stdout) != args.stride * args.height:
            raise RuntimeError("short BAR2 read")
        return [result.stdout[row * args.stride:(row + 1) * args.stride]
                for row in range(args.height)]

    before = read_rows()
    with (device / "resource0").open("r+b", buffering=0) as regs_file, \
            mmap.mmap(regs_file.fileno(), BAR0_SIZE, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
        def read_reg(offset: int) -> int:
            return struct.unpack_from("<I", regs, offset)[0]

        def write_reg(offset: int, value: int) -> None:
            struct.pack_into("<I", regs, offset, value)
            read_reg(offset)  # Flush the posted PCIe write.

        if read_reg(0) != GPU_ID:
            raise RuntimeError("GPU_ID mismatch")
        write_reg(FILL_ADDR, args.address)
        write_reg(FILL_STRIDE, args.stride)
        write_reg(FILL_SIZE, (args.height << 16) | args.width)
        write_reg(FILL_COLOR, args.color)
        write_reg(FILL_COMMAND, 1)

        # A BAR0 read can observe the previous sticky completion before the
        # posted start write has crossed into the memory clock domain.
        time.sleep(0.01)
        deadline = time.monotonic() + args.timeout
        status = read_reg(FILL_STATUS)
        while not (status & 0x2) and time.monotonic() < deadline:
            status = read_reg(FILL_STATUS)
        if not (status & 0x2):
            raise RuntimeError(f"fill timed out, status={status:#x}")
        if status & 0x5:
            raise RuntimeError(f"fill completed with busy/error status={status:#x}")

    pixel = struct.pack("<I", args.color)
    expected_pixels = pixel * args.width
    after = read_rows()
    for row, actual in enumerate(after):
        if actual[:args.width * 4] != expected_pixels:
            got = struct.unpack_from("<I", actual)[0]
            raise RuntimeError(
                f"filled pixels differ on row {row}: expected {args.color:#010x}, got {got:#010x}")
        if actual[args.width * 4:] != before[row][args.width * 4:]:
            raise RuntimeError(f"fill changed bytes outside the rectangle on row {row}")

    print(f"Fill PASS: {args.width}x{args.height}, stride={args.stride}, "
          f"address={args.address:#x}, color={args.color:#010x}, status={status:#x}")


if __name__ == "__main__":
    main()
