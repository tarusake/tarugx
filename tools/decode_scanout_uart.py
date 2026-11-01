#!/usr/bin/env python3
"""Decode complete FR03 UART lines from stdin; ignore partial captures."""
import json
import re
import sys


def decode(line):
    match = re.fullmatch(r"D([01]{4})X([0-9a-fA-F]{72})", line.strip())
    if not match:
        return None
    payload = int(match[2], 16)
    if payload >> 256 != 0x46523033:
        return None
    status = dict(zip(("ddr_pll", "calibrated", "link", "underflow"),
                      (int(v) for v in match[1])))
    fault = (payload >> 8) & ((1 << 64) - 1)
    status["aux"] = f"{payload & 255:02x}"
    # A status flag can precede the bundled snapshot by one diagnostic cycle.
    status["fault_captured"] = bool(fault & 8)
    if status["fault_captured"]:
        status.update(x=fault >> 54, y=(fault >> 44) & 1023,
                      expected=(fault >> 28) & 65535,
                      actual=(fault >> 12) & 65535)
        for bit, name in enumerate(("fifo_error", "fifo_valid", "consuming", "armed",
                                    "active_solid", "active_enable", "requested_solid",
                                    "requested_enable")):
            status[name] = (fault >> bit) & 1
        # When fifo_valid=0 the actual index/error bits are not valid data.
    return status


if __name__ == "__main__":
    for line in sys.stdin:
        result = decode(line)
        if result is not None:
            print(json.dumps(result), flush=True)
