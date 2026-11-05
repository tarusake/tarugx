#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Compare terminal scrolling CPU and PCIe traffic with shadow and TaruGX EXA."""

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
import pwd
import subprocess
import time

host_dir = Path('/home/tarusake/tarugx-dev/host')
config = host_dir/'xorg-tarugx.conf'
accelerated = base64.b64decode({accelerated!r})
baseline = base64.b64decode({baseline!r})
device = Path('/sys/bus/pci/devices/0000:01:00.0')
env = ['runuser', '-u', 'tarusake', '--', 'env', 'DISPLAY=:1',
       'XAUTHORITY='+str(host_dir/'Xauthority'),
       'XDG_RUNTIME_DIR=/run/user/'+str(pwd.getpwnam('tarusake').pw_uid)]

def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def stop():
    for unit in ('tarugx-terminal', 'tarugx-desktop', 'tarugx-xorg'):
        subprocess.run(['systemctl', 'stop', unit], check=False, timeout=45)

def start():
    run(['python3', str(host_dir/'desktop.py'), 'start', '--user', 'tarusake',
         '--terminal'], timeout=90, capture_output=True)
    time.sleep(2)

def process_ticks(pid):
    values = (Path('/proc')/str(pid)/'stat').read_text().rsplit(')', 1)[1].split()
    return int(values[11]) + int(values[12])

def host_ticks():
    fields = Path('/proc/stat').read_text().splitlines()[0].split()[1:]
    values = [int(value) for value in fields]
    idle = values[3] + values[4]
    return sum(values), idle

def partial_bytes():
    fields = dict(item.split('=', 1) for item in
                  (device/'partial_dma_profile').read_text().split())
    return int(fields['bytes'])

def counters():
    return {{
        'render_commands': int((device/'render_commands').read_text()),
        'render_failures': int((device/'render_failures').read_text()),
        'dma_uploads': int((device/'dma_uploads').read_text()),
        'frame_dma_bytes': int((device/'bytes_uploaded').read_text()),
        'partial_dma_bytes': partial_bytes(),
    }}

def scroll():
    marker = Path('/tmp/tarugx-terminal-scroll-done')
    script = Path('/tmp/tarugx-terminal-scroll-workload.sh')
    marker.unlink(missing_ok=True)
    script.write_text('#!/bin/sh\\nseq 1 4000\\ntouch '+str(marker)+
                      '\\nsleep 2\\n')
    script.chmod(0o755)
    terminal = subprocess.Popen([
        *env, 'env', 'XDG_CONFIG_HOME='+str(host_dir/'desktop-config'),
        '/usr/bin/xfce4-terminal', '--disable-server', '--geometry=125x38',
        '--command='+str(script),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if marker.exists():
            run([*env, 'xset', 'q'], timeout=5, capture_output=True)
            marker.unlink(missing_ok=True)
            terminal.wait(timeout=10)
            return
        if terminal.poll() is not None:
            raise RuntimeError('scroll terminal exited before completion')
        time.sleep(0.02)
    terminal.terminate()
    raise RuntimeError('terminal scroll workload timed out')

def sample(label, contents):
    stop()
    config.write_bytes(contents)
    start()
    subprocess.run(['systemctl', 'stop', 'tarugx-terminal'], check=False,
                   timeout=45)
    pid = int(subprocess.check_output(
        ['systemctl', 'show', '-p', 'MainPID', '--value', 'tarugx-xorg'],
        text=True).strip())
    before = counters()
    cpu0 = process_ticks(pid)
    total0, idle0 = host_ticks()
    started = time.monotonic()
    scroll()
    elapsed = time.monotonic() - started
    total1, idle1 = host_ticks()
    cpu_seconds = (process_ticks(pid) - cpu0) / os.sysconf('SC_CLK_TCK')
    after = counters()
    stop()
    log = (host_dir/'Xorg.1.log').read_text(errors='replace')
    totals = [line for line in log.splitlines() if 'EXA totals:' in line]
    delta = {{name: after[name] - before[name] for name in before}}
    active_ticks = (total1 - total0) - (idle1 - idle0)
    return {{
        'label': label,
        'lines': 4000,
        'elapsed_seconds': elapsed,
        'xorg_cpu_seconds': cpu_seconds,
        'xorg_cpu_percent_one_core': 100 * cpu_seconds / elapsed,
        'host_cpu_percent': 100 * active_ticks / (total1 - total0),
        'delta': delta,
        'pcie_bytes': delta['frame_dma_bytes'] + delta['partial_dma_bytes'],
        'xorg_acceleration_totals': totals[-1] if totals else None,
    }}

try:
    shadow = sample('modesetting-shadow', baseline)
    exa = sample('tarugx-exa', accelerated)
    assert shadow['delta']['frame_dma_bytes'] > 0, shadow
    assert exa['delta']['render_commands'] > 0, exa
    assert exa['delta']['render_failures'] == 0, exa
    config.write_bytes(accelerated)
    start()
    print(json.dumps({{'result': 'terminal_scroll_comparison',
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
    print(json.dumps({{'result': 'terminal_scroll_failed',
                      'error': str(error), 'accelerated_restored': restored}}))
"""


def main() -> None:
    accelerated = base64.b64encode(
        (ROOT / "host" / "xorg-tarugx.conf").read_bytes()).decode("ascii")
    baseline = base64.b64encode(BASELINE).decode("ascii")
    output = Backend().remote(
        remote_program(accelerated, baseline), "benchmark terminal scroll", 360
    )
    print(output, end="")
    if '"result": "terminal_scroll_comparison"' not in output:
        raise RuntimeError("terminal scroll benchmark failed")


if __name__ == "__main__":
    main()
