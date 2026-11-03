#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Temporarily load and exercise the DRM fill/blit ioctl driver on hardware."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def encoded(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def remote_program(files: dict[str, str]) -> str:
    return f"""
import base64
import json
import mmap
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tempfile
import time

files = {files!r}
services = ['tarugx-main-display', 'tarugx-xorg', 'tarugx-desktop']
was_active = {{name: subprocess.run(
    ['systemctl', 'is-active', name], capture_output=True, text=True
).stdout.strip() == 'active' for name in services}}
work = Path(tempfile.mkdtemp(prefix='tarugx-drm-render-test-'))
old_module = Path(subprocess.run(
    ['modinfo', '-n', 'tarugx'], check=True, capture_output=True, text=True
).stdout.strip())
old_copy = work/'tarugx-old.ko'
shutil.copy2(old_module, old_copy)
loaded_test_module = False
try:
    for name, contents in files.items():
        (work/name).write_bytes(base64.b64decode(contents))
    build = Path('/lib/modules')/platform.release()/'build'
    subprocess.run(
        ['make', '-C', str(build), 'M='+str(work), 'W=1', 'modules'],
        check=True, timeout=120
    )
    test = work/'tarugx-drm-render-test'
    subprocess.run([
        'cc', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I'+str(work), '-I/usr/include/libdrm',
        str(work/'tarugx-drm-render-test.c'), '-ldrm', '-o', str(test)
    ], check=True, timeout=45)
    scanout_test = work/'tarugx-drm-vram-scanout-test'
    subprocess.run([
        'cc', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I'+str(work), '-I/usr/include/libdrm',
        str(work/'tarugx-drm-vram-scanout-test.c'),
        '-ldrm', '-o', str(scanout_test)
    ], check=True, timeout=45)

    subprocess.run(['systemctl', 'stop', *services], check=True, timeout=45)
    subprocess.run(['modprobe', '-r', 'tarugx'], check=True, timeout=30)
    subprocess.run(['insmod', str(work/'tarugx.ko')], check=True, timeout=30)
    loaded_test_module = True

    device = Path('/sys/bus/pci/devices/0000:01:00.0')
    assert (device/'driver').resolve().name == 'tarugx'
    cards = [card for card in Path('/sys/class/drm').glob('card[0-9]*')
             if (card/'device/vendor').exists() and
             (card/'device/vendor').read_text().strip() == '0x22c2']
    assert len(cards) == 1, f'expected one TaruGX card, got {{cards}}'
    run = subprocess.run(
        [str(test), '/dev/dri/'+cards[0].name],
        check=True, text=True, capture_output=True, timeout=30
    )

    with (device/'resource0').open('r+b', buffering=0) as regs_file, \
         mmap.mmap(regs_file.fileno(), 65536, flags=mmap.MAP_SHARED,
                   prot=mmap.PROT_READ | mmap.PROT_WRITE) as regs, \
         (device/'resource2').open('r+b', buffering=0) as vram_file, \
         mmap.mmap(vram_file.fileno(), 4194304, flags=mmap.MAP_SHARED,
                   prot=mmap.PROT_READ | mmap.PROT_WRITE) as vram:
        gpu_version = struct.unpack_from('<I', regs, 0x04)[0]
        active = struct.unpack_from('<I', regs, 0x28)[0]
        assert active in (0, 1), f'invalid active buffer {{active}}'
        base = active * 640 * 480 * 4
        source = struct.unpack_from('<I', vram, base + (16*640+16)*4)[0]
        copied = struct.unpack_from('<I', vram, base + (16*640+64)*4)[0]
        batch_a = struct.unpack_from('<I', vram, base + (100*640+320)*4)[0]
        batch_b = struct.unpack_from('<I', vram, base + (300*640+320)*4)[0]
        gem_base = 2 * 640 * 480 * 4
        gem_first = struct.unpack_from('<I', vram, gem_base)[0]
        gem_last = struct.unpack_from('<I', vram,
                                      gem_base + 640*480*4 - 4)[0]
        gem_fill = struct.unpack_from('<I', vram,
                                      gem_base + (10*640+10)*4)[0]
        gem_blit = struct.unpack_from('<I', vram,
                                      gem_base + (10*640+100)*4)[0]
    assert source == 0xff2050d0 and copied == source, \
        f'readback mismatch source={{source:#x}} copied={{copied:#x}}'
    assert batch_a == 0xff40c060 and batch_b == 0xffd08020, \
        f'batch readback mismatch a={{batch_a:#x}} b={{batch_b:#x}}'
    assert gem_first == 0xfeed1234 and gem_last == 0xcafe5678, \
        f'VRAM GEM raw readback mismatch first={{gem_first:#x}} last={{gem_last:#x}}'
    assert gem_fill == 0xff30b070 and gem_blit == gem_fill, \
        f'VRAM GEM 2D mismatch fill={{gem_fill:#x}} blit={{gem_blit:#x}}'

    direct_output = ''
    if gpu_version >= 0x00010001:
        direct = subprocess.run(
            [str(scanout_test), '/dev/dri/'+cards[0].name],
            check=True, text=True, capture_output=True, timeout=30
        )
        direct_output = direct.stdout

    attrs = device
    dma_test_started = time.monotonic_ns()
    (attrs/'dma_test').write_text(str(640 * 480 * 4))
    dma_test_elapsed_ms = (time.monotonic_ns() - dma_test_started) / 1_000_000
    counters = {{name: (attrs/name).read_text().strip()
                for name in ('render_commands', 'render_failures',
                             'render_queue_peak',
                             'flip_timeouts', 'dma_upload_failures',
                             'direct_scanouts', 'dma_profile')}}
    assert int(counters['render_commands']) == 492, counters
    assert int(counters['render_failures']) == 0, counters
    assert int(counters['render_queue_peak']) >= 2, counters
    assert int(counters['direct_scanouts']) == \
        (1 if gpu_version >= 0x00010001 else 0), counters
    print(run.stdout, end='')
    print(direct_output, end='')
    print(json.dumps({{'readback': hex(source),
                      'dma_test_elapsed_ms': dma_test_elapsed_ms,
                      'counters': counters}}))
finally:
    if loaded_test_module:
        subprocess.run(['modprobe', '-r', 'tarugx'], check=False, timeout=30)
    if not Path('/sys/module/tarugx').exists():
        subprocess.run(['insmod', str(old_copy)], check=True, timeout=30)
    for name in services:
        if was_active[name]:
            subprocess.run(['systemctl', 'start', name], check=True, timeout=120)
    shutil.rmtree(work, ignore_errors=True)
"""


def main() -> None:
    driver = ROOT / "drivers" / "tarugx"
    files = {
        "Makefile": encoded(driver / "Makefile"),
        "tarugx.c": encoded(driver / "tarugx.c"),
        "tarugx_drm.h": encoded(driver / "tarugx_drm.h"),
        "tarugx-drm-render-test.c": encoded(
            ROOT / "tools" / "tarugx-drm-render-test.c"
        ),
        "tarugx-drm-vram-scanout-test.c": encoded(
            ROOT / "tools" / "tarugx-drm-vram-scanout-test.c"
        ),
    }
    print(Backend().remote(
        remote_program(files), "test DRM render ioctls", timeout=300
    ), end="")


if __name__ == "__main__":
    main()
