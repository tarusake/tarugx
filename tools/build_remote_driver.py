#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Compile the TaruGX module against the hardware host's running kernel."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers" / "tarugx"


def remote_program(files: dict[str, str]) -> str:
    return f"""
import base64
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

files = {files!r}
work = Path(tempfile.mkdtemp(prefix='tarugx-driver-build-'))
try:
    for name, contents in files.items():
        (work/name).write_bytes(base64.b64decode(contents))
    kernel = platform.release()
    build = Path('/lib/modules')/kernel/'build'
    assert build.exists(), f'kernel headers missing for {{kernel}}'
    run = subprocess.run(
        ['make', '-C', str(build), 'M='+str(work), 'W=1', 'modules'],
        text=True, capture_output=True, timeout=120
    )
    print(run.stdout, end='')
    print(run.stderr, end='')
    if run.returncode:
        print('TARUGX_DRIVER_BUILD_FAILED')
    else:
        assert (work/'tarugx.ko').exists(), 'module was not produced'
finally:
    shutil.rmtree(work, ignore_errors=True)
"""


def main() -> None:
    names = ("Makefile", "tarugx.c", "tarugx_drm.h")
    files = {
        name: base64.b64encode((DRIVER / name).read_bytes()).decode("ascii")
        for name in names
    }
    output = Backend().remote(
        remote_program(files), "build DRM driver", timeout=150
    )
    print(output, end="")
    if "TARUGX_DRIVER_BUILD_FAILED" in output:
        raise RuntimeError("driver build failed")


if __name__ == "__main__":
    main()
