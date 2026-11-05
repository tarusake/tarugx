#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Install and verify the standard Xorg modesetting path on the TaruGX host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def remote_program(config: str) -> str:
    return f"""
import base64
import json
from pathlib import Path
import subprocess
import time

user = 'tarusake'
host_dir = Path('/home/tarusake/tarugx-dev/host')  # public-check: allow
config_path = host_dir/'xorg-tarugx.conf'
original = config_path.read_bytes()
replacement = base64.b64decode({config!r})
device = Path('/sys/bus/pci/devices/0000:01:00.0')
names = ('page_flips', 'flip_timeouts', 'vblank_timeouts', 'bytes_uploaded',
         'dma_uploads', 'dma_upload_failures', 'render_failures')
env = ['runuser', '-u', user, '--', 'env', 'DISPLAY=:1',
       'XAUTHORITY='+str(host_dir/'Xauthority')]

def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def stop():
    for unit in ('tarugx-terminal', 'tarugx-desktop', 'tarugx-xorg'):
        subprocess.run(['systemctl', 'stop', unit], check=False, timeout=45)

def start():
    run(['python3', str(host_dir/'desktop.py'), 'start', '--user', user,
         '--terminal'], timeout=90, capture_output=True)
    time.sleep(2)

stage = 'stop previous desktop'
try:
    stop()
    stage = 'install modesetting config'
    config_path.write_bytes(replacement)
    stage = 'start modesetting desktop'
    start()
    stage = 'verify display geometry'
    display = run([*env, 'xdpyinfo'], timeout=15, capture_output=True).stdout
    assert 'dimensions:    1920x1080 pixels' in display, display[-2000:]
    assert 'depth of root window:    16 planes' in display, display[-2000:]
    modes = run([*env, 'xrandr', '--query'], timeout=15,
                capture_output=True).stdout
    assert ' connected ' in modes and '1920x1080' in modes, modes
    windows = run([*env, 'xdotool', 'search', '--sync', '--onlyvisible',
                   '--class', 'xfce4-terminal'], timeout=15,
                  capture_output=True).stdout.split()
    assert windows, 'Xfce terminal window was not found'
    window = windows[-1]
    run([*env, 'xdotool', 'windowactivate', '--sync', window,
         'windowmove', '--sync', window, '80', '100',
         'windowsize', '--sync', window, '900', '600'], timeout=15,
        capture_output=True)
    before = {{name: int((device/name).read_text()) for name in names}}
    stage = 'exercise standard rendering'
    solid = run([*env, 'x11perf', '-sync', '-time', '1', '-repeat', '1',
                 '-rect500'], timeout=20, capture_output=True).stdout[-800:]
    copy = run([*env, 'x11perf', '-sync', '-time', '1', '-repeat', '1',
                '-copywinwin500'], timeout=20, capture_output=True).stdout[-800:]
    commands = ['xdotool']
    for index in range(180):
        x = 40 + (index * 37) % 1080
        y = 60 + (index * 23) % 360
        commands += ['windowmove', '--sync', window, str(x), str(y)]
    run([*env, *commands], timeout=120, capture_output=True)
    run([*env, 'xdotool', 'windowmove', '--sync', window, '80', '100',
         'windowsize', '--sync', window, '900', '600'], timeout=15,
        capture_output=True)
    time.sleep(2)
    after = {{name: int((device/name).read_text()) for name in names}}
    delta = {{name: after[name] - before[name] for name in names}}
    log = (host_dir/'Xorg.1.log').read_text(errors='replace')
    assert 'modesetting' in log.lower(), log[-5000:]
    assert 'TaruGX EXA front buffer active' not in log, log[-5000:]
    assert delta['bytes_uploaded'] > 0, delta
    assert delta['dma_uploads'] > 0, delta
    assert delta['dma_upload_failures'] == 0, delta
    assert delta['render_failures'] == 0, delta
    assert delta['flip_timeouts'] == 0, delta
    assert delta['vblank_timeouts'] == 0, delta
    print(json.dumps({{'result': 'standard_modesetting_pass',
                      'config_installed': True, 'delta': delta,
                      'xrandr': modes, 'solid': solid, 'copy': copy}}))
except Exception as error:
    stop()
    failed_log_path = host_dir/'Xorg.1.log'
    failed_log = (failed_log_path.read_text(errors='replace')[-12000:]
                  if failed_log_path.exists() else '')
    config_path.write_bytes(original)
    restored = False
    try:
        start()
        restored = True
    except Exception:
        pass
    print(json.dumps({{'result': 'standard_modesetting_failed',
                      'stage': stage, 'error_type': type(error).__name__,
                      'error': str(error), 'old_desktop_restored': restored,
                      'xorg_log': failed_log}}))
"""


def main() -> None:
    config = base64.b64encode(
        (ROOT / "host" / "xorg-modesetting.conf").read_bytes()
    ).decode("ascii")
    output = Backend().remote(
        remote_program(config), "test standard Xorg modesetting", 300
    )
    print(output, end="")
    if '"result": "standard_modesetting_pass"' not in output:
        raise RuntimeError("standard Xorg modesetting test failed")


if __name__ == "__main__":
    main()
