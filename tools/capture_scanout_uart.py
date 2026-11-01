#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Timestamp FR03/FR04 UART observations; print only changed decoded states."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import serial
from decode_scanout_uart import decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="JSONL file (prefer ignored build/evidence)")
    parser.add_argument("--port", default="/dev/ttyUSB1")
    parser.add_argument("--seconds", type=float, default=120)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    last = None
    with args.output.open("x") as out, serial.Serial(args.port, 115200, timeout=1) as uart:
        uart.reset_input_buffer()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            line = uart.readline().decode("ascii", errors="replace").strip()
            if not line:
                continue
            state = decode(line)
            record = {"utc": datetime.now(timezone.utc).isoformat(), "raw": line, "state": state}
            out.write(json.dumps(record) + "\n")
            out.flush()
            if state is not None and state != last:
                print(json.dumps({"utc": record["utc"], "state": state}), flush=True)
                last = state


if __name__ == "__main__":
    main()
