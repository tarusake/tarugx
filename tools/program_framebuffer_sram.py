#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Safely update the FPGA SRAM image and restore the host display."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from recover_pcie import Backend, ROOT

ENDPOINT = '0000:01:00.0'
ROOT_PORT = '0000:00:13.0'
IDENT = 0x110022c2
USER_CODE = '0x54474631'

PREPARE = r"""
import json
from pathlib import Path
import subprocess
endpoint = Path('/sys/bus/pci/devices')/ENDPOINT
assert endpoint.resolve(strict=True).name == ENDPOINT, 'Endpoint missing'
assert endpoint.joinpath('vendor').read_text().strip() == '0x22c2', 'Unexpected vendor'
assert endpoint.joinpath('device').read_text().strip() == '0x1100', 'Unexpected device'
subprocess.run(['systemctl', 'stop', 'tarugx-main-display'], check=True)
try:
    # The boot service starts collected transient units.  They can outlive a
    # failed/restarted oneshot service, so stop them explicitly before trying
    # to unload the DRM module.
    for unit in ('tarugx-terminal', 'tarugx-desktop', 'tarugx-xorg'):
        subprocess.run(['systemctl', 'stop', unit], check=False)
    subprocess.run(['modprobe', '-r', 'tarugx'], check=True)
    assert not endpoint.joinpath('driver').exists(), 'Driver remains bound'
    endpoint.joinpath('remove').write_text('1')
    assert not endpoint.exists(), 'Endpoint removal failed'
except Exception:
    subprocess.run(['modprobe', 'tarugx'], check=False)
    subprocess.run(['systemctl', 'start', 'tarugx-main-display'], check=False)
    raise
print(json.dumps({'removed': True}))
"""

RESTORE = r"""
import json
from pathlib import Path
import subprocess
import time
root = Path('/sys/bus/pci/devices')/ROOT_PORT
assert root.resolve(strict=True).name == ROOT_PORT, 'Root port missing'
root.joinpath('power/control').write_text('on')
root.joinpath('rescan').write_text('1')
endpoint = Path('/sys/bus/pci/devices')/ENDPOINT
deadline = time.monotonic()+10
while not endpoint.exists() and time.monotonic() < deadline:
    time.sleep(0.5)
assert endpoint.exists(), 'Endpoint did not enumerate'
with endpoint.joinpath('config').open('rb', buffering=0) as f:
    ident = int.from_bytes(f.read(4), 'little')
assert ident == IDENT, 'Unexpected endpoint ID'
subprocess.run(['modprobe', 'tarugx'], check=True)
assert endpoint.joinpath('driver').resolve(strict=True).name == 'tarugx', 'Driver bind failed'
subprocess.run(['systemctl', 'start', 'tarugx-main-display'], check=True)
service = subprocess.run(['systemctl', 'is-active', 'tarugx-main-display'],
                         check=True, text=True, capture_output=True).stdout.strip()
print(json.dumps({'id': ident, 'driver': 'tarugx', 'display': service}))
"""


def orchestrate(prepare, program, restore):
    """Restore the host after every attempted program once preparation succeeds."""
    prepare()
    program_error = None
    try:
        program()
    except Exception as error:  # restoration must still be attempted
        program_error = error
    try:
        state = restore()
    except Exception as restore_error:
        if program_error:
            raise RuntimeError(f'{program_error}; host restoration also failed') from restore_error
        raise
    if program_error:
        raise program_error
    return state


class SramBackend(Backend):
    opts = ['--device', 'GW5AST-138B', '--chain_index', '0',
            '--chain_size', '1', '--frequency', '2.5MHz']

    def _host_code(self, body):
        return (f'ENDPOINT = {ENDPOINT!r}\nROOT_PORT = {ROOT_PORT!r}\n'
                f'IDENT = {IDENT!r}\n' + body)

    def prepare(self):
        return json.loads(self.remote(self._host_code(PREPARE), 'prepare SRAM update', 30))

    def restore(self):
        return json.loads(self.remote(self._host_code(RESTORE), 'restore display', 120))

    @staticmethod
    def _run(args, timeout=120):
        try:
            result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                                    timeout=timeout)
        except subprocess.TimeoutExpired:
            raise RuntimeError('JTAG operation timed out') from None
        if result.returncode:
            raise RuntimeError('JTAG/USB operation failed')
        return result.stdout+result.stderr

    def program(self, bitstream):
        detached = False
        original_error = None
        try:
            self._run([sys.executable, 'tools/ftdi-driver.py', 'detach'], 30)
            detached = True
            try:
                self._run(['gowin-programmer', '--operation_index', '2'] + self.opts +
                          ['--fsFile', str(bitstream)])
                code = self._run(['gowin-programmer', '--operation_index', '73'] + self.opts, 30)
                if USER_CODE not in code:
                    raise RuntimeError('Unexpected FPGA UserCode after SRAM update')
            except Exception as error:
                original_error = error
                # Recover the last known-good Flash image before restoring PCIe.
                self._run(['gowin-programmer', '--operation_index', '1'] + self.opts, 30)
        finally:
            if detached:
                self._run([sys.executable, 'tools/ftdi-driver.py', 'attach'], 30)
        if original_error:
            raise original_error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bitstream', type=Path)
    args = parser.parse_args()
    bitstream = args.bitstream.resolve(strict=True)
    if not bitstream.is_file() or bitstream.stat().st_size == 0:
        raise RuntimeError('Bitstream is empty or not a regular file')
    backend = SramBackend()
    state = orchestrate(backend.prepare, lambda: backend.program(bitstream), backend.restore)
    print(json.dumps({'result': 'sram_updated', 'state': state}))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print('SRAM update stopped: '+str(error), file=sys.stderr)
        sys.exit(1)
