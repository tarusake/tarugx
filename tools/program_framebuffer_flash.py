#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Program and verify an authorized TaruGX external-Flash image safely."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from program_framebuffer_sram import (  # noqa: E402
    SramBackend, USER_CODE, orchestrate,
)


class FlashBackend(SramBackend):
    def program(self, bitstream):
        detached = False
        try:
            self._run([sys.executable, 'tools/ftdi-driver.py', 'detach'], 30)
            detached = True
            # Arora-V external Flash programming and verification take several
            # minutes at the conservative 2.5 MHz JTAG rate.  Keep them as
            # separate operations so neither a timeout nor a verify failure can
            # be mistaken for a successful update.
            self._run(['gowin-programmer', '--operation_index', '53'] +
                      self.opts + ['--fsFile', str(bitstream)], 900)
            self._run(['gowin-programmer', '--operation_index', '66'] +
                      self.opts + ['--fsFile', str(bitstream)], 900)
            self._run(['gowin-programmer', '--operation_index', '1'] +
                      self.opts, 30)
            time.sleep(2)
            code = self._run(['gowin-programmer', '--operation_index', '73'] +
                             self.opts, 30)
            if USER_CODE not in code:
                raise RuntimeError('Unexpected FPGA UserCode after Flash update')
        finally:
            if detached:
                self._run([sys.executable, 'tools/ftdi-driver.py', 'attach'], 30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bitstream', type=Path)
    args = parser.parse_args()
    bitstream = args.bitstream.resolve(strict=True)
    if not bitstream.is_file() or bitstream.stat().st_size == 0:
        raise RuntimeError('Bitstream is empty or not a regular file')
    digest = hashlib.sha256(bitstream.read_bytes()).hexdigest()
    backend = FlashBackend()
    state = orchestrate(backend.prepare,
                        lambda: backend.program(bitstream), backend.restore)
    print(json.dumps({'result': 'flash_programmed_and_verified',
                      'sha256': digest, 'state': state}))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print('Flash update stopped: ' + str(error), file=sys.stderr)
        sys.exit(1)
