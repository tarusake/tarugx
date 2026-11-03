#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Temporarily load the worktree driver and show its preferred DRM mode."""

import argparse
import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def encoded(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def remote_program(files: dict[str, str], expected: str, hold: int,
                   keep_driver: bool, bpp: int | None,
                   iterations: int) -> str:
    return f"""
import base64
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import time

files = {files!r}
expected = {expected!r}
hold = {hold!r}
keep_driver = {keep_driver!r}
bpp = {bpp!r}
iterations = {iterations!r}
services = ['tarugx-main-display', 'tarugx-xorg', 'tarugx-desktop']
was_active = {{name: subprocess.run(
    ['systemctl', 'is-active', name], capture_output=True, text=True
).stdout.strip() == 'active' for name in services}}
work = Path(tempfile.mkdtemp(prefix='tarugx-drm-mode-test-'))
old_module = Path(subprocess.run(
    ['modinfo', '-n', 'tarugx'], check=True, capture_output=True, text=True
).stdout.strip())
old_copy = work/'tarugx-old.ko'
shutil.copy2(old_module, old_copy)
loaded_test_module = False
stage = 'prepare'
try:
    for name, contents in files.items():
        (work/name).write_bytes(base64.b64decode(contents))
    build = Path('/lib/modules')/platform.release()/'build'
    subprocess.run(['make', '-C', str(build), 'M='+str(work), 'W=1',
                    'modules'], check=True, timeout=120)
    test = work/'tarugx-drm-mode-test'
    subprocess.run([
        'cc', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I/usr/include/libdrm', str(work/'tarugx-drm-mode-test.c'),
        '-ldrm', '-o', str(test)
    ], check=True, timeout=45)
    # Transient desktop/Xorg units may already have been garbage-collected
    # after a failed prior run. Stop each producer idempotently.
    for name in services:
        subprocess.run(['systemctl', 'stop', name], check=False, timeout=45)
    subprocess.run(['modprobe', '-r', 'tarugx'], check=True, timeout=30)
    module = work/'tarugx.ko'
    if keep_driver:
        persistent = Path('/run/tarugx-worktree')
        persistent.mkdir(mode=0o755, exist_ok=True)
        module = persistent/'tarugx.ko'
        shutil.copy2(work/'tarugx.ko', module)
    subprocess.run(['insmod', str(module)], check=True, timeout=30)
    loaded_test_module = True
    stage = 'locate DRM device'
    device = Path('/sys/bus/pci/devices/0000:01:00.0')
    assert (device/'driver').resolve().name == 'tarugx'
    cards = [card for card in Path('/sys/class/drm').glob('card[0-9]*')
             if (card/'device/vendor').exists() and
             (card/'device/vendor').read_text().strip() == '0x22c2']
    assert len(cards) == 1, f'expected one TaruGX card, got {{cards}}'
    stage = 'run modeset test'
    test_args = [str(test), '/dev/dri/'+cards[0].name, str(hold)]
    if bpp is not None:
        test_args.append(str(bpp))
    runs = []
    for iteration in range(iterations):
        run = subprocess.run(test_args,
                             check=True, text=True, capture_output=True,
                             timeout=hold + 30)
        assert expected in run.stdout, run.stdout
        runs.append(run.stdout)
    counters = {{name: int((device/name).read_text()) for name in (
        'dma_uploads', 'dma_upload_failures', 'flip_timeouts',
        'vblank_timeouts', 'page_flips')}}
    dma_profile = (device/'dma_profile').read_text().strip()
    update_profile = (device/'update_profile').read_text().strip()
    update_timings = dict(
        field.split('=', 1) for field in update_profile.split()
    )
    pages_synced = int((device/'physical_pages_synced').read_text())
    assert counters['dma_uploads'] >= 6 * iterations, counters
    assert counters['dma_upload_failures'] == 0, counters
    assert counters['flip_timeouts'] == 0, counters
    assert counters['vblank_timeouts'] == 0, counters
    assert counters['page_flips'] >= 4 * iterations, counters
    assert int(update_timings['update_ns']) > 0, update_timings
    assert int(update_timings['dma_ns']) > 0, update_timings
    if 'pitch=7680' in run.stdout:
        assert int(update_timings['conversion_ns']) > 0, update_timings
    # The final active full-frame upload deliberately leaves the old physical
    # page stale, so the next partial update must reconstruct it.
    assert pages_synced == 0, pages_synced
    for iteration, stdout in enumerate(runs, 1):
        print(f'iteration={{iteration}}/{{iterations}}')
        print(stdout, end='')
    print(json.dumps({{'expected': expected, 'iterations': iterations,
                      'counters': counters,
                      'dma_profile': dma_profile,
                      'update_profile': update_profile,
                      'physical_pages_synced': pages_synced}}))
except Exception as error:
    device = Path('/sys/bus/pci/devices/0000:01:00.0')
    diagnostics = {{'stage': stage, 'error_type': type(error).__name__,
                   'error': str(error)}}
    if 'run' in locals():
        diagnostics['test_returncode'] = run.returncode
        diagnostics['test_stdout'] = run.stdout[-2000:]
        diagnostics['test_stderr'] = run.stderr[-2000:]
    elif isinstance(error, subprocess.CalledProcessError):
        diagnostics['test_returncode'] = error.returncode
        diagnostics['test_stdout'] = (error.stdout or '')[-2000:]
        diagnostics['test_stderr'] = (error.stderr or '')[-2000:]
    for name in ('dma_profile', 'update_profile', 'dma_error_code',
                 'dma_upload_failures'):
        if (device/name).exists():
            diagnostics[name] = (device/name).read_text().strip()
    diagnostics['dmesg'] = subprocess.run(
        ['dmesg'], capture_output=True, text=True
    ).stdout[-3000:]
    print('TARUGX_DRM_MODE_FAILED '+json.dumps(diagnostics))
finally:
    if loaded_test_module and not keep_driver:
        subprocess.run(['modprobe', '-r', 'tarugx'], check=False, timeout=30)
    if not keep_driver and not Path('/sys/module/tarugx').exists():
        subprocess.run(['insmod', str(old_copy)], check=True, timeout=30)
    for name in services:
        if was_active[name]:
            subprocess.run(['systemctl', 'start', name], check=True, timeout=120)
    shutil.rmtree(work, ignore_errors=True)
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected", default="1920x1080")
    parser.add_argument("--hold", type=int, default=10)
    parser.add_argument(
        "--keep-driver", action="store_true",
        help="leave the worktree module loaded after the test",
    )
    parser.add_argument("--bpp", type=int, choices=(16, 32))
    parser.add_argument(
        "--iterations", type=int, default=1,
        help="run the modeset/update sequence repeatedly without reloading the driver",
    )
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")
    driver = ROOT / "drivers" / "tarugx"
    files = {
        "Makefile": encoded(driver / "Makefile"),
        "tarugx.c": encoded(driver / "tarugx.c"),
        "tarugx_drm.h": encoded(driver / "tarugx_drm.h"),
        "tarugx-drm-mode-test.c": encoded(
            ROOT / "tools" / "tarugx-drm-mode-test.c"
        ),
    }
    output = Backend().remote(
        remote_program(files, args.expected, args.hold, args.keep_driver,
                       args.bpp, args.iterations),
        "test DRM preferred mode",
        timeout=args.iterations * (args.hold + 30) + 210
    )
    print(output, end="")
    if "TARUGX_DRM_MODE_FAILED" in output:
        raise RuntimeError("DRM preferred mode test failed")


if __name__ == "__main__":
    main()
