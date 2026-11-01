#!/usr/bin/env python3
"""Load the installed DRM module and start TaruGX as the dedicated main desktop."""
import argparse
import os
from pathlib import Path
import subprocess
import time
import desktop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--user", required=True)
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("run through the system service as root")
    device = Path("/sys/bus/pci/devices/0000:01:00.0")
    for _ in range(30):
        if device.exists():
            break
        time.sleep(1)
    else:
        raise RuntimeError("TaruGX was not enumerated; check FPGA power and PCIe link")
    if ((device / "vendor").read_text().strip(), (device / "device").read_text().strip()) != ("0x22c2", "0x1100"):
        raise RuntimeError("PCI address is not the expected TaruGX device")
    driver = device / "driver"
    if not driver.exists():
        # The module is installed for this kernel; never build code during boot.
        subprocess.run(["modprobe", "tarugx"], check=True)
    if not driver.exists() or driver.resolve().name != "tarugx":
        raise RuntimeError("TaruGX DRM driver did not bind")
    host_dir = Path(__file__).resolve().parent
    if desktop.active("tarugx-xorg") and desktop.active("tarugx-desktop"):
        desktop.make_primary(args.user, host_dir)
    else:
        desktop.start(args.user, False)
    subprocess.run(["chvt", "8"], check=True)


if __name__ == "__main__":
    main()
