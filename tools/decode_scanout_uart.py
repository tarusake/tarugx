#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Decode complete FR03 through FR16 UART lines; ignore partial captures."""
import json
import re
import sys


def decode(line):
    match = re.fullmatch(r"D([01]{4})X([0-9a-fA-F]{72})", line.strip())
    if not match:
        return None
    payload = int(match[2], 16)
    version = payload >> 256
    if version in (0x46523038, 0x46523039, 0x46523130, 0x46523131):
        flags=(payload>>184)&255
        return {
            **dict(zip(("ddr_pll","calibrated","link","underflow"),(int(v) for v in match[1]))),
            "rom_probe": {
                "accepted_sops":(payload>>224)&0xffffffff,
                "unmatched_sops":(payload>>192)&0xffffffff,
                "captured":bool(flags&1), "sop":bool(flags&2), "eop":bool(flags&4),
                "unmatched":bool(flags&8), "bar":(payload>>176)&255,
                "valid":(payload>>168)&255, "error":(payload>>160)&255,
                "dwords":[f"{(payload>>shift)&0xffffffff:08x}" for shift in ((128,96,64,32) if version in (0x46523130, 0x46523131) else (128,96,64,32,0))],
                **({"raw_bar": f"{payload & 0xfffff:05x}"} if version in (0x46523130, 0x46523131) else {})
            }
        }
    performance = version in (0x46523133, 0x46523134)
    if performance:
        version = 0x46523033  # FR13 preserves the fault/aux portion only.
    automatic_rom = version in (0x46523132, 0x46523136)
    if automatic_rom:
        version = 0x46523037  # FR12 carries the FR07 fault/reset layout.
    if version not in (0x46523033, 0x46523034, 0x46523035, 0x46523036, 0x46523037):
        return None
    status = dict(zip(("ddr_pll", "calibrated", "link", "underflow"),
                      (int(v) for v in match[1])))
    fault = (payload >> 8) & ((1 << 64) - 1)
    if automatic_rom:
        status["automatic_rom"] = True
    if performance:
        perf = (payload >> 72) & ((1 << 184) - 1)
        status["automatic_rom"] = True
        status["performance"] = {
            "max_scan_wait_cycles": (perf >> 168) & 65535,
            "max_scan_response_cycles": (perf >> 152) & 65535,
            "max_memory_read_cycles": (perf >> 136) & 65535,
            "max_memory_write_cycles": (perf >> 120) & 65535,
            "scan_requests": (perf >> 88) & 0xffffffff,
            "memory_requests": (perf >> 56) & 0xffffffff,
            "scan_wait_cycles": (perf >> 24) & 0xffffffff,
            "frozen": bool((perf >> 23) & 1),
            "level_armed": bool((perf >> 22) & 1),
            "min_level_upper_estimate": (perf >> 10) & 1023,
            "max_level_upper_estimate": perf & 1023,
        }
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
