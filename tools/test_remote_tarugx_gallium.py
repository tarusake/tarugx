#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the TaruGX Gallium driver and FPGA depth path on the host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    source = base64.b64encode(
        (ROOT / "tests" / "tarugx_gallium_triangle.c").read_bytes()
    ).decode("ascii")
    remote = f"""
import atexit
import base64
import os
from pathlib import Path
import subprocess
import tempfile

driver = Path('/tmp/tarugx-mesa-build/src/gallium/targets/dri')
assert (driver/'tarugx_dri.so').exists(), 'TaruGX Mesa build is missing'
cards = [node for node in Path('/dev/dri').glob('card*')
         if Path('/sys/class/drm', node.name, 'device/vendor').exists() and
         Path('/sys/class/drm', node.name,
              'device/vendor').read_text().strip() == '0x22c2']
assert len(cards) == 1, f'expected one TaruGX card, got {{cards}}'

def restart_display():
    subprocess.run(['systemctl', 'start', 'tarugx-main-display'],
                   check=False, timeout=30)

atexit.register(restart_display)
subprocess.run(['systemctl', 'stop', 'tarugx-main-display'],
               check=True, timeout=30)
with tempfile.TemporaryDirectory(prefix='tarugx-gallium-test-') as directory:
    work = Path(directory)
    source = work/'tarugx_gallium_triangle.c'
    binary = work/'tarugx-gallium-triangle'
    source.write_bytes(base64.b64decode({source!r}))
    flags = subprocess.run(
        ['pkg-config', '--cflags', '--libs', 'egl', 'glesv2', 'gbm', 'libdrm'],
        check=True, capture_output=True, text=True, timeout=15).stdout.split()
    compile_result = subprocess.run(
        ['cc', '-O2', '-Wall', '-Wextra', '-Werror',
         str(source), '-o', str(binary), *flags],
        capture_output=True, text=True, timeout=45)
    if compile_result.returncode:
        print(compile_result.stdout, end='')
        print(compile_result.stderr, end='')
        print('TARUGX_GALLIUM_COMPILE_FAILED')
        raise SystemExit(0)
    environment = os.environ.copy()
    environment.update({{
        'LIBGL_DRIVERS_PATH': str(driver),
        'MESA_LOADER_DRIVER_OVERRIDE': 'tarugx',
        'XDG_RUNTIME_DIR': '/tmp',
    }})
    counters = Path('/sys/bus/pci/devices/0000:01:00.0/render_commands')
    for name, arguments, expected, expected_commands in (
            ('gradient', ['1', '--gradient'],
             ('path=fpga-gradient', 'corner=001f'), 2),
            ('depth-one', ['1', '--depth-one'],
             ('path=fpga-depth-one', 'center=07e0'), 3),
            ('depth', ['1', '--depth'],
             ('path=fpga-depth', 'center=07e0'), 4)):
        commands_before = int(counters.read_text())
        result = subprocess.run([str(binary), str(cards[0]), *arguments],
                                capture_output=True, text=True,
                                timeout=30, env=environment)
        commands_after = int(counters.read_text())
        command_delta = commands_after - commands_before
        if result.returncode:
            print(result.stdout, end='')
            print(result.stderr, end='')
            print(f'hardware_commands={{command_delta}}')
            print(f'TARUGX_GALLIUM_{{name.upper()}}_FAILED')
            raise SystemExit(0)
        output = result.stdout.strip().splitlines()
        if ('renderer=TaruGX (FPGA 3D + softpipe fallback)' not in output or
                not all(line in output for line in expected)):
            print(result.stdout, end='')
            print(f'TARUGX_GALLIUM_{{name.upper()}}_OUTPUT_FAILED')
            raise SystemExit(0)
        if command_delta != expected_commands:
            print(result.stdout, end='')
            print(f'hardware_commands={{command_delta}} '
                  f'expected={{expected_commands}}')
            print(f'TARUGX_GALLIUM_{{name.upper()}}_COMMANDS_FAILED')
            raise SystemExit(0)
        print(f'Gallium {{name}} PASS')
        print(f'hardware_commands={{command_delta}}')
        print(result.stdout, end='')
"""
    output = Backend().remote(remote, "test TaruGX Gallium hardware paths", 120)
    print(output, end="")
    if 'TARUGX_GALLIUM_' in output and '_FAILED' in output:
        raise RuntimeError("Gallium regression test failed")


if __name__ == "__main__":
    main()
