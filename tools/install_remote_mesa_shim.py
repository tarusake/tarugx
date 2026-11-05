#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Build and install the TaruGX kms_swrast Mesa loader shim."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    source = base64.b64encode(
        (ROOT / "mesa" / "tarugx_dri_shim.c").read_bytes()
    ).decode("ascii")
    remote = f'''
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

dri = Path('/usr/lib/x86_64-linux-gnu/dri')
kms = dri/'kms_swrast_dri.so'
gallium = dri/'libgallium_dri.so'
destination = dri/'tarugx_dri.so'
if not gallium.exists():
    gallium.symlink_to(kms.name)
with tempfile.TemporaryDirectory(prefix='tarugx-mesa-') as directory:
    work = Path(directory)
    source = work/'tarugx_dri_shim.c'
    module = work/'tarugx_dri.so'
    source.write_bytes(base64.b64decode({source!r}))
    build = subprocess.run([
        'cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
        str(source), '-Wl,-rpath,'+str(dri), '-Wl,--no-as-needed', str(kms),
        '-o', str(module),
    ], capture_output=True, text=True, timeout=60)
    if build.returncode:
        print(json.dumps({{'result': 'mesa_shim_build_failed',
                          'diagnostic': build.stderr[-6000:]}}))
    else:
        temporary = destination.with_suffix('.so.new')
        temporary.unlink(missing_ok=True)
        temporary.write_bytes(module.read_bytes())
        temporary.chmod(0o644)
        temporary.replace(destination)
        symbols = subprocess.run(['nm', '-D', str(destination)],
                                 capture_output=True, text=True,
                                 timeout=10).stdout
        expected = '__driDriverGetExtensions_tarugx'
        assert expected in symbols, symbols
        print(json.dumps({{'result': 'mesa_shim_installed',
                          'sha256': hashlib.sha256(
                              destination.read_bytes()).hexdigest(),
                          'destination': str(destination)}}))
'''
    output = Backend().remote(remote, "install Mesa DRI shim", 90)
    print(output, end="")
    if '"result": "mesa_shim_installed"' not in output:
        raise RuntimeError("Mesa DRI shim installation failed")


if __name__ == "__main__":
    main()
