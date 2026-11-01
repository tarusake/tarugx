#!/usr/bin/env python3
"""Decode complete FR03 through FR07 UART lines; ignore partial captures."""
import json
import re
import sys


def decode(line):
    match = re.fullmatch(r"D([01]{4})X([0-9a-fA-F]{72})", line.strip())
    if not match:
        return None
    payload = int(match[2], 16)
    version = payload >> 256
    if version not in (0x46523033, 0x46523034, 0x46523035, 0x46523036, 0x46523037):
        return None
    status = dict(zip(("ddr_pll", "calibrated", "link", "underflow"),
                      (int(v) for v in match[1])))
    fault = (payload >> 8) & ((1 << 64) - 1)
    status["aux"] = f"{payload & 255:02x}"
    if version in (0x46523034, 0x46523035, 0x46523036, 0x46523037):
        pci = (payload >> 72) & ((1 << 96) - 1)
        status["pcie"] = {
            "visited": f"{pci >> 64:08x}",
            "transitions": (pci >> 56) & 255,
            "perst_falls": (pci >> 48) & 255,
            "cmu_falls": (pci >> 40) & 255,
            "link_falls": (pci >> 32) & 255,
            "previous_ltssm": (pci >> 24) & 255,
            "ltssm": (pci >> 16) & 255,
            "seen_flags": f"{pci & 255:02x}",
        }
        for bit, name in enumerate(("link", "dr_refclk_det", "k_lock", "cmu0_ready",
                                    "cmu1_ready", "perst_n", "core_rst_n", "tlp_clock_alive")):
            status["pcie"][name] = (pci >> (8 + bit)) & 1
    if version in (0x46523035, 0x46523036, 0x46523037):
        recovery = (payload >> 168) & 255
        status["recovery"] = {"fired": recovery & 1,
                              "inhibited": (recovery >> 1) & 1,
                              "reset_active": (recovery >> 2) & 1}
    if version == 0x46523037:
        status["phy_recovery"] = True
    if version in (0x46523036, 0x46523037):
        trace = (payload >> 176) & ((1 << 56) - 1)
        status["reset_scope"] = dict(zip(
            ("flags", "before", "during_high", "during_low", "after_high", "after_low", "current"),
            ((trace >> shift) & 255 for shift in (48, 40, 32, 24, 16, 8, 0))))
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
