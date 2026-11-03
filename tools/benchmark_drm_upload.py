#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Build the current KMS test on the host and compare BAR2 with DMA safely."""
import argparse
import base64
import json
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def remote_program(source_b64, modes):
    return f"""
import base64
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

source_b64 = {source_b64!r}
modes = {modes!r}
device = Path('/sys/bus/pci/devices/0000:01:00.0')
assert device.exists(), 'TaruGX endpoint is absent'
assert (device/'vendor').read_text().strip() == '0x22c2'
assert (device/'device').read_text().strip() == '0x1100'
assert (device/'driver').resolve().name == 'tarugx'
parameter = Path('/sys/module/tarugx/parameters/dma_upload')
original_parameter = parameter.read_text().strip()
services = ['tarugx-main-display', 'tarugx-xorg', 'tarugx-desktop']
was_active = {{name: subprocess.run(
    ['systemctl', 'is-active', name], capture_output=True, text=True
).stdout.strip() == 'active' for name in services}}
work = Path(tempfile.mkdtemp(prefix='tarugx-benchmark-'))
results = []
try:
    subprocess.run(['systemctl', 'stop', *services], check=True, timeout=45)
    source = work/'tarugx-kms-test.c'
    binary = work/'tarugx-kms-test'
    source.write_bytes(base64.b64decode(source_b64))
    subprocess.run([
        'cc', '-O2', '-Wall', '-Wextra', str(source), '-o', str(binary),
        '-ldrm', '-I/usr/include/libdrm'
    ], check=True, capture_output=True, text=True, timeout=45)
    cards = []
    for card in Path('/sys/class/drm').glob('card[0-9]*'):
        vendor = card/'device/vendor'
        if vendor.exists() and vendor.read_text().strip() == '0x22c2':
            cards.append('/dev/dri/' + card.name)
    assert len(cards) == 1, f'expected one TaruGX card, got {{cards}}'
    for mode in modes:
        parameter.write_text('1' if mode == 'dma' else '0')
        started = time.monotonic()
        run = subprocess.run(
            [str(binary), cards[0]], capture_output=True, text=True, timeout=180
        )
        results.append({{
            'mode': mode,
            'returncode': run.returncode,
            'elapsed_seconds': time.monotonic() - started,
            'stdout': run.stdout,
            'stderr': run.stderr,
        }})
        if run.returncode:
            raise RuntimeError(f'{{mode}} KMS benchmark failed')
finally:
    parameter.write_text(original_parameter)
    shutil.rmtree(work, ignore_errors=True)
    if was_active['tarugx-main-display']:
        subprocess.run(
            ['systemctl', 'start', 'tarugx-main-display'],
            check=True, timeout=120
        )
print(json.dumps({{'original_dma_upload': original_parameter, 'results': results}}))
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('both', 'bar', 'dma'), default='both')
    args = parser.parse_args()
    modes = ['bar', 'dma'] if args.mode == 'both' else [args.mode]
    source_b64 = base64.b64encode(
        (ROOT/'tools'/'tarugx-kms-test.c').read_bytes()
    ).decode('ascii')
    output = Backend().remote(
        remote_program(source_b64, modes), 'benchmark DRM uploads', 420
    )
    result = json.loads(output)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
