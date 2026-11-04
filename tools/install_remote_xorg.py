#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Build and install the TaruGX Xorg EXA module on the hardware host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def encoded(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def remote_program(files: dict[str, str]) -> str:
    return f"""
import base64
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

files = {files!r}
work = Path(tempfile.mkdtemp(prefix='tarugx-xorg-install-'))
try:
    for name, contents in files.items():
        (work/name).write_bytes(base64.b64decode(contents))
    cflags = shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', 'xorg-server', 'libdrm'], text=True))
    libs = shlex.split(subprocess.check_output(
        ['pkg-config', '--libs', 'libdrm'], text=True))
    module = work/'tarugx_drv.so'
    build = subprocess.run(
        ['cc', '-O2', '-fPIC', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter',
         '-shared', '-I'+str(work),
         str(work/'tarugx_driver.c'), *cflags, *libs, '-o', str(module)],
        text=True, capture_output=True, timeout=60)
    if build.returncode:
        print(json.dumps({{'result': 'xorg_driver_build_failed',
                          'diagnostic': build.stderr[-6000:]}}))
        raise SystemExit(0)
    destination = Path('/usr/lib/xorg/modules/drivers/tarugx_drv.so')
    backup = destination.with_suffix('.so.previous')
    if destination.exists() and not backup.exists():
        shutil.copy2(destination, backup)
    temporary = destination.with_suffix('.so.new')
    shutil.copy2(module, temporary)
    temporary.chmod(0o644)
    os.replace(temporary, destination)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    print(json.dumps({{'result': 'xorg_driver_installed',
                      'sha256': digest,
                      'destination': str(destination),
                      'backup': str(backup) if backup.exists() else None}}))
finally:
    shutil.rmtree(work, ignore_errors=True)
"""


def main() -> None:
    files = {
        "tarugx_driver.c": encoded(ROOT / "xorg" / "tarugx_driver.c"),
        "tarugx_drm.h": encoded(ROOT / "drivers" / "tarugx" / "tarugx_drm.h"),
    }
    output = Backend().remote(remote_program(files), "install Xorg driver", 90)
    print(output, end="")
    if 'xorg_driver_build_failed' in output:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
