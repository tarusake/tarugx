#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Build and install the current TaruGX driver on the hardware host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers" / "tarugx"


def encoded(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def remote_program(files: dict[str, str]) -> str:
    return f"""
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

files = {files!r}
kernel = platform.release()
work = Path(tempfile.mkdtemp(prefix='tarugx-driver-install-'))
try:
    for name, contents in files.items():
        (work/name).write_bytes(base64.b64decode(contents))
    build = Path('/lib/modules')/kernel/'build'
    subprocess.run(['make', '-C', str(build), 'M='+str(work), 'W=1',
                    'modules'], check=True, timeout=120,
                   stdout=subprocess.DEVNULL)
    module = work/'tarugx.ko'
    destination = Path('/lib/modules')/kernel/'updates'/'tarugx.ko'
    backup = destination.with_suffix('.ko.previous')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and not backup.exists():
        shutil.copy2(destination, backup)
    temporary = destination.with_suffix('.ko.new')
    shutil.copy2(module, temporary)
    temporary.chmod(0o644)
    os.replace(temporary, destination)
    subprocess.run(['depmod', '-a', kernel], check=True, timeout=60)
    initramfs_modules = Path('/etc/initramfs-tools/modules')
    module_lines = initramfs_modules.read_text().splitlines()
    if not any(line.split('#', 1)[0].strip().split()[:1] == ['tarugx']
               for line in module_lines):
        with initramfs_modules.open('a') as stream:
            if module_lines and module_lines[-1]:
                stream.write('\\n')
            stream.write('tarugx\\n')
    plymouth_hook = Path('/usr/share/initramfs-tools/hooks/plymouth')
    plymouth_config = Path('/etc/initramfs-tools/conf.d/tarugx-plymouth')
    if plymouth_hook.exists():
        plymouth_config.parent.mkdir(parents=True, exist_ok=True)
        plymouth_config.write_text('FRAMEBUFFER=y\\n')
    subprocess.run(['update-initramfs', '-u', '-k', kernel], check=True,
                   timeout=120, stdout=subprocess.DEVNULL)
    source_hash = hashlib.sha256(module.read_bytes()).hexdigest()
    installed_hash = hashlib.sha256(destination.read_bytes()).hexdigest()
    if source_hash != installed_hash:
        raise RuntimeError('installed module hash mismatch')
    print(json.dumps({{'result': 'driver_installed', 'kernel': kernel,
                      'sha256': installed_hash,
                      'destination': str(destination),
                      'initramfs': '/boot/initrd.img-'+kernel,
                      'plymouth': plymouth_hook.exists(),
                      'backup': str(backup) if backup.exists() else None}}))
finally:
    shutil.rmtree(work, ignore_errors=True)
"""


def main() -> None:
    names = ("Makefile", "tarugx.c", "tarugx_drm.h")
    files = {name: encoded(DRIVER / name) for name in names}
    output = Backend().remote(
        remote_program(files), "install DRM driver", timeout=210
    )
    print(output, end="")


if __name__ == "__main__":
    main()
