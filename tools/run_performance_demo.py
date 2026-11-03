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
import mmap
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

source_b64 = {source_b64!r}
device = Path('/sys/bus/pci/devices/0000:01:00.0')
assert device.exists(), 'TaruGX endpoint is absent'
assert (device/'vendor').read_text().strip() == '0x22c2'
assert (device/'device').read_text().strip() == '0x1100'
assert (device/'driver').resolve().name == 'tarugx'
with (device/'resource0').open('r+b', buffering=0) as reg_file:
    regs = mmap.mmap(
        reg_file.fileno(), 64 * 1024, flags=mmap.MAP_SHARED,
        prot=mmap.PROT_READ | mmap.PROT_WRITE
    )
    gpu_id, gpu_version = struct.unpack_from('<II', regs, 0)
    dma_status = struct.unpack_from('<I', regs, 0xd4)[0]
    queue_status = struct.unpack_from('<I', regs, 0xa8)[0]
    regs.close()
assert gpu_id == 0x54475055, 'unexpected TaruGX register identity'
dma_ready = bool(dma_status & (1 << 4)) and not bool(
    dma_status & ((1 << 0) | (1 << 2) | (1 << 3))
)
queue_ready = bool(queue_status & (1 << 3))
if not dma_ready or not queue_ready:
    print(
        'Performance demo skipped before stopping the display: '
        f'FPGA image lacks required DMA/render support '
        f'(version={{gpu_version >> 16}}.{{gpu_version & 0xffff}}, '
        f'dma_status={{dma_status:#x}}, queue_status={{queue_status:#x}}).'
    )
    raise SystemExit(0)
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
        # A standalone KMS test disables its CRTC while exiting.  If Xorg
        # reconnects to the still-active DRM state without issuing a fresh
        # modeset, the FPGA display enable can otherwise remain cleared.
        with (device/'resource0').open('r+b', buffering=0) as reg_file:
            regs = mmap.mmap(
                reg_file.fileno(), 64 * 1024, flags=mmap.MAP_SHARED,
                prot=mmap.PROT_READ | mmap.PROT_WRITE
            )
            if struct.unpack_from('<I', regs, 0x10)[0] == 0:
                struct.pack_into('<I', regs, 0x10, 1)
                assert struct.unpack_from('<I', regs, 0x10)[0] == 1
                print('Restored FPGA display enable after the demo.')
            regs.close()
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
