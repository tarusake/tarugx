#!/usr/bin/env python3
"""Read FR09 ROM via Linux sysfs after configuring its diagnostic BAR0 registers.

Run on the unbound Linux host as root. Memory Space must already be enabled.
This does not implement automatic UEFI discovery or change PCI allocations.
"""
import argparse
import hashlib
import mmap
from pathlib import Path
import re
import struct


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bdf", required=True)
    ap.add_argument("--expected-sha256", required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--auto", action="store_true", help="require FR11 hardware ROM decode; do not write diagnostic registers")
    args = ap.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        ap.error("invalid PCI BDF")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.expected_sha256):
        ap.error("invalid SHA256")
    p = Path("/sys/bus/pci/devices") / args.bdf
    if (p / "vendor").read_text().strip() != "0x22c2" or (p / "device").read_text().strip() != "0x1100":
        raise RuntimeError("not the expected TaruGX device")
    if (p / "driver").exists():
        raise RuntimeError("unbind the driver before ROM diagnostics")
    cfg = (p / "config").read_bytes()
    if not (int.from_bytes(cfg[4:6], "little") & 2):
        raise RuntimeError("PCI Memory Space is disabled")
    resources = [tuple(int(x, 16) for x in l.split()) for l in (p / "resource").read_text().splitlines()]
    start, end, _ = resources[6]
    if not start or start & 65535 or end - start + 1 != 65536 or end > 0xffffffff:
        raise RuntimeError("expected assigned 64 KiB, 32-bit ROM resource")
    if resources[0][1] - resources[0][0] + 1 != 65536:
        raise RuntimeError("unexpected BAR0 size")
    with (p / "resource0").open("r+b", buffering=0) as f:
        with mmap.mmap(f.fileno(), 65536, flags=mmap.MAP_SHARED,
                       prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs:
            def rd(offset):
                return struct.unpack_from("<I", regs, offset)[0]
            def wr(offset, value):
                struct.pack_into("<I", regs, offset, value)
                # Read flushes the preceding posted MMIO write.
                if rd(offset) != value:
                    raise RuntimeError(f"diagnostic register readback failed at {offset:#x}")
            if rd(0) != 0x54475055:
                raise RuntimeError("GPU_ID mismatch")
            old_enable, old_base = rd(0x40), rd(0x44)
            if old_enable not in (0, 1) or old_base & 65535:
                raise RuntimeError("FR09 diagnostic registers not present")
            if args.auto and (rd(0x48) != 1 or old_enable != 0 or old_base != 0):
                raise RuntimeError("expected automatic ROM mode with untouched diagnostic registers")
            try:
                if not args.auto:
                    wr(0x40, 0)
                    wr(0x44, start)
                    wr(0x40, 1)
                (p / "rom").write_text("1")
                data = (p / "rom").read_bytes()
            finally:
                try:
                    (p / "rom").write_text("0")
                finally:
                    if not args.auto:
                        wr(0x40, 0)
                        wr(0x44, old_base)
                        wr(0x40, old_enable)
                    elif rd(0x40) != old_enable or rd(0x44) != old_base:
                        raise RuntimeError("diagnostic registers changed unexpectedly")
    digest = hashlib.sha256(data).hexdigest()
    args.output.write_bytes(data)
    print(f"ROM resource: {start:#x}..{end:#x}")
    print(f"ROM bytes: {len(data)}, SHA256: {digest}")
    if len(data) != 65536 or digest.lower() != args.expected_sha256.lower():
        raise RuntimeError("ROM contents differ from the FPGA build image")
    print("ROM full-image match PASS; " + ("automatic mode, diagnostic registers untouched" if args.auto else "diagnostic registers restored"))


if __name__ == "__main__":
    main()
