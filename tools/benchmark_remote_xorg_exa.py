#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Compare modesetting and TaruGX EXA on the same Xorg workload."""

import base64
import json
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]
BASELINE = b'''Section "ServerFlags"
    Option "AutoAddGPU" "false"
    Option "BlankTime" "0"
EndSection
Section "Device"
    Identifier "TaruGX"
    Driver "modesetting"
    BusID "PCI:1:0:0"
    Option "kmsdev" "/dev/dri/by-path/pci-0000:01:00.0-card"
    Option "AccelMethod" "none"
    Option "ShadowFB" "true"
    Option "DoubleShadow" "true"
    Option "SWcursor" "true"
    Option "PageFlip" "false"
EndSection
Section "Monitor"
    Identifier "TaruGX-HDMI"
EndSection
Section "Screen"
    Identifier "TaruGX-Screen"
    Device "TaruGX"
    Monitor "TaruGX-HDMI"
    DefaultDepth 24
EndSection
Section "ServerLayout"
    Identifier "TaruGX-Desktop"
    Screen 0 "TaruGX-Screen"
EndSection
'''


def remote_program(accelerated: str, baseline: str) -> str:
    return f"""
import base64
import json
import os
from pathlib import Path
import subprocess
import time

host_dir = Path('/home/tarusake/tarugx-dev/host')
config = host_dir/'xorg-tarugx.conf'
accelerated = base64.b64decode({accelerated!r})
baseline = base64.b64decode({baseline!r})
device = Path('/sys/bus/pci/devices/0000:01:00.0')
names = ('render_commands', 'render_failures', 'bytes_uploaded', 'dma_uploads')

def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def stop():
    for unit in ('tarugx-terminal', 'tarugx-desktop', 'tarugx-xorg'):
        subprocess.run(['systemctl', 'stop', unit], check=False, timeout=45)

def start():
    run(['python3', str(host_dir/'desktop.py'), 'start', '--user', 'tarusake',
         '--terminal'], timeout=90, capture_output=True)
    time.sleep(2)

def ticks(pid):
    fields = Path('/proc')/str(pid)/'stat'
    values = fields.read_text().rsplit(')', 1)[1].split()
    return int(values[11]) + int(values[12])

def sample(label, contents):
    stop()
    config.write_bytes(contents)
    start()
    pid = int(subprocess.check_output(
        ['systemctl', 'show', '-p', 'MainPID', '--value', 'tarugx-xorg'],
        text=True).strip())
    before = {{name: int((device/name).read_text()) for name in names}}
    cpu0 = ticks(pid)
    started = time.monotonic()
    env = ['runuser', '-u', 'tarusake', '--', 'env', 'DISPLAY=:1',
           'XAUTHORITY='+str(host_dir/'Xauthority')]
    solid = run([*env, 'x11perf', '-sync', '-time', '2', '-repeat', '1',
                 '-rect500'], timeout=30, capture_output=True).stdout
    copy = run([*env, 'x11perf', '-sync', '-time', '2', '-repeat', '1',
                '-copywinwin500'], timeout=30, capture_output=True).stdout
    elapsed = time.monotonic() - started
    cpu_seconds = (ticks(pid) - cpu0) / os.sysconf('SC_CLK_TCK')
    after = {{name: int((device/name).read_text()) for name in names}}
    return {{'label': label, 'elapsed_seconds': elapsed,
             'xorg_cpu_seconds': cpu_seconds,
             'xorg_cpu_percent_one_core': 100 * cpu_seconds / elapsed,
             'delta': {{name: after[name] - before[name] for name in names}},
             'solid_result': solid.strip().splitlines()[-1],
             'copy_result': copy.strip().splitlines()[-1]}}

try:
    old = sample('modesetting-shadow', baseline)
    exa = sample('tarugx-exa', accelerated)
    print(json.dumps({{'result': 'xorg_exa_comparison', 'baseline': old,
                      'accelerated': exa}}))
except Exception as error:
    stop()
    config.write_bytes(accelerated)
    restored = False
    try:
        start()
        restored = True
    except Exception:
        pass
    print(json.dumps({{'result': 'xorg_exa_comparison_failed',
                      'error': str(error), 'accelerated_restored': restored}}))
"""


def main() -> None:
    accelerated = base64.b64encode(
        (ROOT / "host" / "xorg-tarugx.conf").read_bytes()).decode("ascii")
    baseline = base64.b64encode(BASELINE).decode("ascii")
    print(Backend().remote(remote_program(accelerated, baseline),
                           "benchmark Xorg EXA", 240), end="")


if __name__ == "__main__":
    main()
