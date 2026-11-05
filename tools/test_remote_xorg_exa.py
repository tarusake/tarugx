#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Install the host launch files and verify the TaruGX EXA DDX with Xfce."""

import base64
import json
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def encoded(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def remote_program(files: dict[str, str]) -> str:
    return f"""
import base64
import json
from pathlib import Path
import pwd
import subprocess
import time

files = {files!r}
user = 'tarusake'
host_dir = Path('/home/tarusake/tarugx-dev/host')
targets = {{name: host_dir/name for name in files}}
original = {{name: path.read_bytes() for name, path in targets.items()}}
device = Path('/sys/bus/pci/devices/0000:01:00.0')
before = {{name: int((device/name).read_text()) for name in
          ('render_commands', 'render_failures', 'bytes_uploaded', 'dma_uploads')}}

def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def stop():
    for unit in ('tarugx-terminal', 'tarugx-desktop', 'tarugx-xorg'):
        subprocess.run(['systemctl', 'stop', unit], check=False, timeout=45)

def start():
    run(['python3', str(host_dir/'desktop.py'), 'start', '--user', user,
         '--terminal'], timeout=90, capture_output=True)

stage = 'stop old desktop'
try:
    stop()
    stage = 'install launch files'
    for name, contents in files.items():
        targets[name].write_bytes(base64.b64decode(contents))
    stage = 'start EXA desktop'
    start()
    account = pwd.getpwnam(user)
    env = ['runuser', '-u', user, '--', 'env', 'DISPLAY=:1',
           'XAUTHORITY='+str(host_dir/'Xauthority'),
           'XDG_RUNTIME_DIR=/run/user/'+str(account.pw_uid)]
    stage = 'query X server'
    display = run([*env, 'xdpyinfo'], timeout=15, capture_output=True).stdout
    assert 'dimensions:    1920x1080 pixels' in display, display[-2000:]
    assert 'depth of root window:    16 planes' in display, display[-2000:]
    stage = 'exercise solid rectangles'
    solid = run([*env, 'x11perf', '-sync', '-time', '2', '-repeat', '1',
                 '-rect500'], timeout=20, capture_output=True)
    stage = 'exercise screen copies'
    copy = run([*env, 'x11perf', '-sync', '-time', '2', '-repeat', '1',
                '-copywinwin500'], timeout=20, capture_output=True)
    time.sleep(1)
    after = {{name: int((device/name).read_text()) for name in before}}
    delta = {{name: after[name] - before[name] for name in before}}
    log = (host_dir/'Xorg.1.log').read_text(errors='replace')
    assert 'TaruGX EXA front buffer active' in log, log[-4000:]
    assert delta['render_commands'] > 0, delta
    assert delta['render_failures'] == 0, delta
    print(json.dumps({{'result': 'xfce_exa_active', 'delta': delta,
                      'solid': solid.stdout[-800:],
                      'copy': copy.stdout[-800:]}}))
except Exception as error:
    stop()
    failed_log_path = host_dir/'Xorg.1.log'
    failed_log = (failed_log_path.read_text(errors='replace')[-12000:]
                  if failed_log_path.exists() else '')
    command_stdout = getattr(error, 'stdout', '') or ''
    command_stderr = getattr(error, 'stderr', '') or ''
    for name, contents in original.items():
        targets[name].write_bytes(contents)
    try:
        start()
        restored = True
    except Exception:
        restored = False
    print(json.dumps({{'result': 'xfce_exa_failed', 'stage': stage,
                      'error_type': type(error).__name__, 'error': str(error),
                      'command_stdout': command_stdout[-4000:],
                      'command_stderr': command_stderr[-4000:],
                      'old_desktop_restored': restored,
                      'xorg_log': failed_log}}))
"""


def main() -> None:
    files = {
        "desktop.py": encoded(ROOT / "host" / "desktop.py"),
        "xorg-tarugx.conf": encoded(ROOT / "host" / "xorg-tarugx.conf"),
    }
    print(Backend().remote(remote_program(files), "test Xorg EXA", 180), end="")


if __name__ == "__main__":
    main()
