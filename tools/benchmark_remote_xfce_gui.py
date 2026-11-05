#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Compare real Xfce window operations with shadow and TaruGX EXA Xorg."""

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

host_dir = Path('/home/tarusake/tarugx-dev/host')  # public-check: allow
config = host_dir/'xorg-tarugx.conf'
accelerated = base64.b64decode({accelerated!r})
baseline = base64.b64decode({baseline!r})
device = Path('/sys/bus/pci/devices/0000:01:00.0')
names = ('render_commands', 'render_failures', 'bytes_uploaded', 'dma_uploads')
env = ['runuser', '-u', 'tarusake', '--', 'env', 'DISPLAY=:1',
       'XAUTHORITY='+str(host_dir/'Xauthority')]

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
    values = (Path('/proc')/str(pid)/'stat').read_text().rsplit(')', 1)[1].split()
    return int(values[11]) + int(values[12])

def terminal_window():
    for _ in range(30):
        result = subprocess.run(
            [*env, 'xdotool', 'search', '--onlyvisible',
             '--class', 'xfce4-terminal'], timeout=5,
            capture_output=True, text=True).stdout.split()
        if result:
            return result[-1]
        time.sleep(0.5)
    raise RuntimeError('Xfce terminal window was not found after 15 seconds')

def window_workload(window):
    commands = ['xdotool', 'windowactivate', '--sync', window,
                'windowsize', '--sync', window, '720', '480']
    for index in range(180):
        x = 40 + (index * 37) % 1080
        y = 60 + (index * 23) % 480
        commands += ['windowmove', '--sync', window, str(x), str(y)]
    run([*env, *commands], timeout=90, capture_output=True)
    run([*env, 'xdotool', 'windowmove', '--sync', window, '80', '100',
         'windowsize', '--sync', window, '900', '600'], timeout=15,
        capture_output=True)

def sample(label, contents):
    stop()
    config.write_bytes(contents)
    start()
    window = terminal_window()
    pid = int(subprocess.check_output(
        ['systemctl', 'show', '-p', 'MainPID', '--value', 'tarugx-xorg'],
        text=True).strip())
    before = {{name: int((device/name).read_text()) for name in names}}
    cpu0 = ticks(pid)
    started = time.monotonic()
    window_workload(window)
    elapsed = time.monotonic() - started
    cpu_seconds = (ticks(pid) - cpu0) / os.sysconf('SC_CLK_TCK')
    after = {{name: int((device/name).read_text()) for name in names}}
    stop()
    log = (host_dir/'Xorg.1.log').read_text(errors='replace')
    totals = [line for line in log.splitlines() if 'EXA totals:' in line]
    acceleration = totals[-1] if totals else None
    acceleration_fields = {{}}
    if acceleration:
        acceleration_fields = dict(
            field.split('=', 1)
            for field in acceleration.split('EXA totals:', 1)[1].split()
            if '=' in field
        )
    bar_bytes = int(acceleration_fields.get('bar_bytes', 0))
    return {{'label': label, 'operations': 182,
             'elapsed_seconds': elapsed, 'xorg_cpu_seconds': cpu_seconds,
             'xorg_cpu_percent_one_core': 100 * cpu_seconds / elapsed,
             'delta': {{name: after[name] - before[name] for name in names}},
             'pcie_bytes_observed': after['bytes_uploaded'] -
                                    before['bytes_uploaded'] + bar_bytes,
             'xorg_acceleration_totals': acceleration,
             'xorg_acceleration_fields': acceleration_fields}}

try:
    shadow = sample('modesetting-shadow', baseline)
    exa = sample('tarugx-exa', accelerated)
    assert shadow['delta']['bytes_uploaded'] > 0, shadow
    assert exa['delta']['render_commands'] > 0, exa
    assert exa['delta']['render_failures'] == 0, exa
    assert exa['delta']['bytes_uploaded'] == 0, exa
    assert int(exa['xorg_acceleration_fields']['async_submits']) > 0, exa
    assert int(exa['xorg_acceleration_fields']['async_wait_failures']) == 0, exa
    assert int(exa['xorg_acceleration_fields']['submit_failures']) == 0, exa
    assert exa['xorg_cpu_seconds'] < shadow['xorg_cpu_seconds'], (shadow, exa)
    assert exa['pcie_bytes_observed'] < shadow['pcie_bytes_observed'], \
        (shadow, exa)
    config.write_bytes(accelerated)
    start()
    print(json.dumps({{'result': 'xfce_gui_acceleration_pass',
                      'baseline': shadow, 'accelerated': exa}}))
except Exception as error:
    stop()
    config.write_bytes(accelerated)
    restored = False
    try:
        start()
        restored = True
    except Exception:
        pass
    print(json.dumps({{'result': 'xfce_gui_acceleration_failed',
                      'error': str(error), 'accelerated_restored': restored}}))
"""


def main() -> None:
    accelerated = base64.b64encode(
        (ROOT / "host" / "xorg-tarugx.conf").read_bytes()).decode("ascii")
    baseline = base64.b64encode(BASELINE).decode("ascii")
    output = Backend().remote(
        remote_program(accelerated, baseline), "benchmark Xfce GUI", 300
    )
    print(output, end="")
    if '"result": "xfce_gui_acceleration_pass"' not in output:
        raise RuntimeError("Xfce GUI acceleration benchmark failed")


if __name__ == "__main__":
    main()
