#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Build and run the visual BAR-versus-DMA demo on the TaruGX host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def remote_program(source_b64: str) -> str:
    return f"""
import base64
from pathlib import Path
import shutil
import subprocess
import tempfile

source_b64 = {source_b64!r}
device = Path('/sys/bus/pci/devices/0000:01:00.0')
assert device.exists(), 'TaruGX endpoint is absent'
assert (device/'vendor').read_text().strip() == '0x22c2'
assert (device/'device').read_text().strip() == '0x1100'
assert (device/'driver').resolve().name == 'tarugx'
services = ['tarugx-main-display', 'tarugx-xorg', 'tarugx-desktop']
was_active = {{name: subprocess.run(
    ['systemctl', 'is-active', name], capture_output=True, text=True
).stdout.strip() == 'active' for name in services}}
work = Path(tempfile.mkdtemp(prefix='tarugx-performance-demo-'))
try:
    subprocess.run(['systemctl', 'stop', *services], check=True, timeout=45)
    source = work/'tarugx-performance-demo.c'
    binary = work/'tarugx-performance-demo'
    source.write_bytes(base64.b64decode(source_b64))
    subprocess.run([
        'cc', '-O2', '-Wall', '-Wextra', str(source), '-o', str(binary),
        '-ldrm', '-I/usr/include/libdrm'
    ], check=True, timeout=45)
    cards = []
    for card in Path('/sys/class/drm').glob('card[0-9]*'):
        vendor = card/'device/vendor'
        if vendor.exists() and vendor.read_text().strip() == '0x22c2':
            cards.append('/dev/dri/' + card.name)
    assert len(cards) == 1, f'expected one TaruGX card, got {{cards}}'
    run = subprocess.run(
        [str(binary), cards[0]], text=True, capture_output=True, timeout=90
    )
    print(run.stdout, end='')
    if run.stderr:
        print(run.stderr, end='')
    if run.returncode:
        raise RuntimeError('performance demo failed')
finally:
    shutil.rmtree(work, ignore_errors=True)
    if was_active['tarugx-main-display']:
        subprocess.run(
            ['systemctl', 'start', 'tarugx-main-display'],
            check=True, timeout=120
        )
"""


def main() -> None:
    source_b64 = base64.b64encode(
        (ROOT / 'tools' / 'tarugx-performance-demo.c').read_bytes()
    ).decode('ascii')
    output = Backend().remote(
        remote_program(source_b64), 'run visual performance demo', 180
    )
    print(output, end='')


if __name__ == '__main__':
    main()
