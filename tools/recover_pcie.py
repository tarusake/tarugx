#!/usr/bin/env python3
"""One-shot WSL recovery using existing Flash; no erase/program or background loop."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

# Read PCIe Link Status from the capability list, without accessing BARs.
REMOTE = r"""
import json
from pathlib import Path
root = Path('/sys/bus/pci/devices/0000:00:13.0').resolve(strict=True)
with (root/'config').open('rb', buffering=0) as f:
    config = f.read(256)
assert int.from_bytes(config[10:12], 'little') == 0x0604, 'Unexpected root port'
cap = config[0x34] & 0xfc
seen = set()
while cap and cap not in seen:
    seen.add(cap)
    assert 0x40 <= cap <= 0xec, 'Bad capability pointer'
    if config[cap] == 0x10:
        break
    cap = config[cap+1] & 0xfc
else:
    raise RuntimeError('PCIe capability not found')
link = int.from_bytes(config[cap+18:cap+20], 'little')
children = []
for p in root.rglob('config'):
    if p.parent == root:
        continue
    d = p.parent
    children.append({'bdf': d.name, 'bound': (d/'driver').exists(),
                     'vendor': (d/'vendor').read_text().strip(),
                     'device': (d/'device').read_text().strip()})
assert all(d['vendor']=='0x22c2' and d['device']=='0x1100' for d in children), 'Unexpected downstream device'
if ACTION in ('remove', 'rescan'):
    assert not any(d['bound'] for d in children), 'Downstream driver is bound'
if ACTION == 'remove':
    assert not (link & 0x2000), 'Link recovered already; refusing reconfiguration'
    for d in children:
        (root/d['bdf']/'remove').write_text('1')
if ACTION == 'rescan':
    (root/'power/control').write_text('on')
    (root/'rescan').write_text('1')
endpoint = root/'0000:01:00.0'
ident = None
if endpoint.exists() and (link & 0x2000):
    with (endpoint/'config').open('rb', buffering=0) as f:
        ident = int.from_bytes(f.read(4), 'little')
print(json.dumps({'linked': bool(link & 0x2000), 'training': bool(link & 0x800),
                  'speed_code': link & 15, 'width': (link >> 4) & 63,
                  'bound': any(d['bound'] for d in children), 'id': ident}))
"""


def recover(host, reconfigure, sleep=time.sleep):
    state = host('inspect')
    if state['linked'] and state['id'] == 0x110022c2:
        return 'already_linked', state
    if state['bound']:
        raise RuntimeError('Driver is bound; stop the display/driver before recovery')
    if not state['linked']:
        # Host-side guard is repeated immediately before removing the endpoint.
        host('remove')
        reconfigure()
        sleep(2)
    host('rescan')
    for _ in range(10):
        state = host('inspect')
        if state['linked'] and state['id'] == 0x110022c2:
            return 'recovered', state
        sleep(1)
    raise RuntimeError('Link/Config ID did not recover; no further reset attempted')


class Backend:
    def __init__(self):
        self.config = {}
        for name in ('host.env', 'credentials.env'):
            p = ROOT/'.local'/name
            if p.stat().st_mode & 0o077:
                raise RuntimeError('Private config must have mode 0600')
            self.config.update(dict(line.split('=', 1) for line in p.read_text().splitlines()
                                    if line and not line.startswith('#')))

    def host(self, action):
        c = self.config
        code = 'ACTION = ' + repr(action) + '\n' + REMOTE
        try:
            result = subprocess.run(
                ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                 c['TARUGX_SSH_USER']+'@'+c['TARUGX_HOST_IP'],
                 "sudo -S -p '' python3 -c " + shlex.quote(code)],
                input=c['TARUGX_SUDO_PASSWORD']+'\n', text=True,
                capture_output=True, timeout=15)
        except subprocess.TimeoutExpired:
            raise RuntimeError('Host SSH timed out; no further action') from None
        if result.returncode:
            # Do not echo commands, addresses, credentials or remote tracebacks.
            raise RuntimeError('Host check/action failed ('+action+'); no further action')
        return json.loads(result.stdout)

    def reconfigure(self):
        import serial
        from decode_scanout_uart import decode
        with serial.Serial('/dev/ttyUSB1', 115200, timeout=1) as uart:
            uart.reset_input_buffer()
            deadline = time.monotonic()+5
            state = None
            while time.monotonic() < deadline:
                candidate = decode(uart.readline().decode('ascii', errors='replace'))
                if candidate and 'pcie' in candidate and 'recovery' not in candidate:
                    state = candidate
                    break
        if state is None or not state['pcie']['cmu1_ready'] or not state['pcie']['tlp_clock_alive']:
            raise RuntimeError('FR04 UART / CMU1 / TLP clock not ready; no reconfiguration')
        if state['pcie']['link']:
            raise RuntimeError('UART reports link up; conflicting observations, no reconfiguration')
        def run(args):
            try:
                p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=30)
            except subprocess.TimeoutExpired:
                raise RuntimeError('JTAG operation timed out; no retry') from None
            if p.returncode:
                raise RuntimeError('JTAG/USB operation failed; no retry')
            return p.stdout+p.stderr
        opts = ['--device', 'GW5AST-138B', '--chain_index', '0', '--chain_size', '1', '--frequency', '2.5MHz']
        try:
            run([sys.executable, 'tools/ftdi-driver.py', 'detach'])
            code = run(['gowin-programmer', '--operation_index', '73']+opts)
            if '0x54474631' not in code:
                raise RuntimeError('Unexpected FPGA UserCode; no reconfiguration')
            run(['gowin-programmer', '--operation_index', '1']+opts)
        finally:
            run([sys.executable, 'tools/ftdi-driver.py', 'attach'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='allow one reconfiguration/rescan if needed')
    args = parser.parse_args()
    backend = Backend()
    if args.apply:
        result, state = recover(backend.host, backend.reconfigure)
        print(json.dumps({'result': result, 'state': state}))
    else:
        print(json.dumps(backend.host('inspect')))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        # OS error filenames can contain local paths; secrets are never included.
        print('Recovery stopped: '+str(error), file=sys.stderr)
        sys.exit(1)
