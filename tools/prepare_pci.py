#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Restore a kernel-allocated PCI bridge window; never allocate addresses.

Default: inspect only. --apply requires the endpoint to be unbound.
This does not confirm DDR calibration or make MMIO accesses.
"""
import argparse
import os
from pathlib import Path
import re


def window_value(start, end):
    if not (0 < start <= end < (1 << 32)):
        raise ValueError("bridge window must be nonzero and below 4 GiB")
    if start & 0xfffff or (end & 0xfffff) != 0xfffff:
        raise ValueError("bridge reservation must have 1 MiB granularity")
    return (end & 0xfff00000) | ((start >> 16) & 0xfff0)


def resources(path):
    return [tuple(int(v, 16) for v in line.split())
            for line in (path / "resource").read_text().splitlines()]


def read_config(path, offset, size):
    with (path / "config").open("rb", buffering=0) as f:
        data = os.pread(f.fileno(), size, offset)
    if len(data) != size:
        raise ValueError("short PCI configuration read")
    return int.from_bytes(data, "little")


def write_config(path, offset, size, value):
    with (path / "config").open("r+b", buffering=0) as f:
        if os.pwrite(f.fileno(), value.to_bytes(size, "little"), offset) != size:
            raise OSError("short PCI configuration write")
    if read_config(path, offset, size) != value:
        raise OSError("PCI configuration readback mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bdf")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        raise ValueError("expected domain:bus:device.function")
    dev = (Path("/sys/bus/pci/devices") / args.bdf).resolve(strict=True)
    bridge = dev.parent
    if read_config(dev, 0, 4) != 0x110022c2:
        raise ValueError("endpoint is not 22c2:1100")
    if read_config(bridge, 0x0a, 2) != 0x0604:
        raise ValueError("immediate parent is not a PCI bridge")
    bars = resources(dev)
    for index, size in ((0, 65536), (2, 4194304)):
        lo, hi, flags = bars[index]
        if not lo or hi - lo + 1 != size or not flags & 0x200:
            raise ValueError(f"BAR{index} is unassigned or has unexpected size/type")
    windows = [r for r in resources(bridge)[7:]
               if r[2] & 0x200 and not r[2] & 0x2000
               and all(r[0] <= bars[i][0] <= bars[i][1] <= r[1] for i in (0, 2))]
    if len(windows) != 1:
        raise ValueError("no unique non-prefetchable kernel reservation covers both BARs")
    lo, hi, _ = windows[0]
    encoded = window_value(lo, hi)
    current = read_config(bridge, 0x20, 4)
    print(f"{bridge.name}: reserved {lo:#x}..{hi:#x}, window {current:08x} -> {encoded:08x}")
    print(f"{args.bdf}: BAR0={bars[0][0]:#x}, BAR2={bars[2][0]:#x}; enable Memory Space only")
    if not args.apply:
        print("Inspection only; use --apply before loading tarugx")
        return
    if (dev / "driver").exists():
        raise ValueError("endpoint has a bound driver; stop clients and unbind first")
    changes = [(bridge, 0x20, 4, encoded),
               (bridge, 4, 2, read_config(bridge, 4, 2) | 2),
               (dev, 4, 2, read_config(dev, 4, 2) | 2)]
    undo = []
    try:
        for path, offset, size, value in changes:
            undo.append((path, offset, size, read_config(path, offset, size)))
            write_config(path, offset, size, value)
    except Exception:
        for entry in reversed(undo):
            write_config(*entry)
        raise
    print("Applied and read back; no DMA, reset, rescan or MMIO performed")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
