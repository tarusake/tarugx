#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Safely reload the already-programmed FPGA Flash image."""
import json
import sys
import time

from program_framebuffer_sram import SramBackend, USER_CODE, orchestrate


class ReloadBackend(SramBackend):
    def reload(self):
        detached = False
        try:
            self._run([sys.executable, 'tools/ftdi-driver.py', 'detach'], 30)
            detached = True
            self._run(['gowin-programmer', '--operation_index', '1'] + self.opts, 30)
            time.sleep(2)
            code = self._run(['gowin-programmer', '--operation_index', '73'] + self.opts, 30)
            if USER_CODE not in code:
                raise RuntimeError('Unexpected FPGA UserCode after Flash reload')
        finally:
            if detached:
                self._run([sys.executable, 'tools/ftdi-driver.py', 'attach'], 30)


def main():
    backend = ReloadBackend()
    state = orchestrate(backend.prepare, backend.reload, backend.restore)
    print(json.dumps({'result': 'flash_reloaded', 'state': state}))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print('Flash reload stopped: ' + str(error), file=sys.stderr)
        sys.exit(1)
