#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Verify Weston, an EGL client, and TaruGX KMS on the hardware host."""

import argparse

from recover_pcie import Backend


REMOTE = r'''
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

host_dir = Path('/home/tarusake/tarugx-dev/host')  # public-check: allow
runtime = Path('/run/tarugx-weston-regression')
log_path = Path('/tmp/tarugx-weston-regression.log')
device = Path('/sys/bus/pci/devices/0000:01:00.0')
names = ('page_flips', 'flip_timeouts', 'vblank_timeouts', 'bytes_uploaded',
         'dma_uploads', 'dma_upload_failures', 'render_failures')
weston = None
client = None
stage = 'check dependencies'
use_pixman = __USE_PIXMAN__

def counters():
    return {name: int((device/name).read_text()) for name in names}

def ticks(pid):
    fields = (Path('/proc')/str(pid)/'stat').read_text().rsplit(')', 1)[1].split()
    return int(fields[11]) + int(fields[12])

def stop_desktop():
    subprocess.run(['systemctl', 'stop', 'tarugx-wayland-test.service'],
                   check=False, timeout=30)
    for unit in ('tarugx-terminal', 'tarugx-desktop', 'tarugx-xorg'):
        subprocess.run(['systemctl', 'stop', unit], check=False, timeout=45)

def restore_desktop():
    subprocess.run(['systemctl', 'stop', 'tarugx-wayland-test.service'],
                   check=False, timeout=30)
    subprocess.run(['python3', str(host_dir/'desktop.py'), 'start',
                    '--user', 'tarusake', '--terminal'], check=True,
                   capture_output=True, text=True, timeout=90)
    time.sleep(2)

def terminate(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)

try:
    assert shutil.which('weston'), 'weston is not installed'
    assert shutil.which('weston-simple-egl'), 'weston-simple-egl is not installed'
    stage = 'stop Xorg desktop'
    stop_desktop()
    runtime.mkdir(mode=0o700, exist_ok=True)
    runtime.chmod(0o700)
    log_path.unlink(missing_ok=True)
    environment = os.environ.copy()
    environment.update({'XDG_RUNTIME_DIR': str(runtime),
                        'WAYLAND_DISPLAY': 'wayland-tarugx',
                        'LIBGL_ALWAYS_SOFTWARE': '1'})
    if not use_pixman:
        assert any(Path(directory, 'tarugx_dri.so').exists()
                   for directory in ('/usr/lib/x86_64-linux-gnu/dri',
                                     '/usr/lib/dri')), \
            'install the TaruGX Mesa shim first'
    stage = 'start Weston DRM backend'
    renderer_arguments = ['--use-pixman'] if use_pixman else []
    weston = subprocess.Popen([
        'weston', '--backend=drm-backend.so', '--drm-device=card0',
        '--tty=2', *renderer_arguments, '--current-mode',
        '--continue-without-input', '--no-config',
        '--socket=wayland-tarugx', '--log='+str(log_path),
    ], env=environment)
    socket = runtime/'wayland-tarugx'
    for _ in range(100):
        if weston.poll() is not None:
            break
        if socket.exists():
            break
        time.sleep(0.1)
    assert weston.poll() is None, 'Weston exited during startup'
    assert socket.exists(), 'Wayland socket was not created'
    log = log_path.read_text(errors='replace')
    assert 'DRM: supports atomic modesetting' in log, log[-6000:]
    assert "Output 'HDMI-A-1' enabled" in log, log[-6000:]
    assert '1920x1080@60.0' in log, log[-6000:]
    if not use_pixman:
        assert 'GL renderer: llvmpipe' in log, log[-12000:]
    stage = 'identify Mesa renderer'
    egl = subprocess.run(['eglinfo', '-B'], env=environment,
                         capture_output=True, text=True, timeout=30)
    egl_report = egl.stdout + egl.stderr
    # eglinfo probes every platform and returns failure when the unrelated
    # X11/device probes fail; the active Wayland platform remains valid.
    assert 'EGL vendor string: Mesa Project' in egl_report, egl_report[-6000:]
    tarugx_dri_available = any(Path(directory, 'tarugx_dri.so').exists()
                               for directory in
                               ('/usr/lib/x86_64-linux-gnu/dri', '/usr/lib/dri'))
    stage = 'run EGL animation'
    before = counters()
    client = subprocess.Popen(['weston-simple-egl'], env=environment,
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
    time.sleep(1)
    assert client.poll() is None, 'EGL client exited during startup'
    cpu_before = ticks(client.pid)
    started = time.monotonic()
    time.sleep(5)
    elapsed = time.monotonic() - started
    cpu_seconds = (ticks(client.pid) - cpu_before) / os.sysconf('SC_CLK_TCK')
    after = counters()
    delta = {name: after[name] - before[name] for name in names}
    fps = delta['page_flips'] / (elapsed + 1.0)
    assert fps >= 20.0, (fps, delta)
    assert fps <= 65.0, (fps, delta)
    assert delta['dma_uploads'] > 0, delta
    assert delta['bytes_uploaded'] > 0, delta
    assert delta['flip_timeouts'] == 0, delta
    assert delta['vblank_timeouts'] == 0, delta
    assert delta['dma_upload_failures'] == 0, delta
    assert delta['render_failures'] == 0, delta
    result = {'result': 'wayland_egl_pass',
              'renderer': 'pixman' if use_pixman else 'mesa-llvmpipe',
              'fps': fps,
              'client_cpu_percent_one_core': cpu_seconds * 100.0 / elapsed,
              'tarugx_dri_available': tarugx_dri_available,
              'delta': delta, 'mesa_report': egl_report[-6000:],
              'weston_log': log[-4000:]}
except Exception as error:
    result = {'result': 'wayland_egl_failed', 'stage': stage,
              'error_type': type(error).__name__, 'error': str(error),
              'weston_log': (log_path.read_text(errors='replace')[-12000:]
                             if log_path.exists() else '')}
finally:
    terminate(client)
    terminate(weston)
    restored = False
    try:
        restore_desktop()
        restored = True
    except Exception as restore_error:
        result['restore_error'] = str(restore_error)
    result['xorg_restored'] = restored
    print(json.dumps(result))
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--pixman', action='store_true',
        help='use the Pixman compositor instead of the Mesa GL compositor',
    )
    args = parser.parse_args()
    remote = REMOTE.replace('__USE_PIXMAN__', repr(args.pixman))
    output = Backend().remote(remote, "test Weston EGL desktop", 180)
    print(output, end="")
    if '"result": "wayland_egl_pass"' not in output or \
            '"xorg_restored": true' not in output:
        raise RuntimeError("Wayland EGL hardware regression failed")


if __name__ == "__main__":
    main()
