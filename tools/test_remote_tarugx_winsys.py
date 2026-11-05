#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile and exercise the Mesa-facing TaruGX winsys on the hardware host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def encoded(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def main() -> None:
    files = {
        "tarugx_winsys.c": encoded(ROOT / "mesa" / "tarugx_winsys.c"),
        "tarugx_winsys.h": encoded(ROOT / "mesa" / "tarugx_winsys.h"),
        "tarugx_drm.h": encoded(ROOT / "drivers" / "tarugx" / "tarugx_drm.h"),
        "tarugx_winsys_test.c": encoded(
            ROOT / "tests" / "tarugx_winsys_test.c"
        ),
    }
    remote = f"""
import base64
from pathlib import Path
import subprocess
import tempfile

files = {files!r}
with tempfile.TemporaryDirectory(prefix='tarugx-winsys-test-') as directory:
    work = Path(directory)
    for name, contents in files.items():
        (work/name).write_bytes(base64.b64decode(contents))
    test = work/'tarugx-winsys-test'
    subprocess.run([
        'cc', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I'+str(work), '-I/usr/include/libdrm',
        str(work/'tarugx_winsys.c'), str(work/'tarugx_winsys_test.c'),
        '-ldrm', '-o', str(test),
    ], check=True, timeout=45)
    nodes = [node for node in Path('/dev/dri').glob('renderD*')
             if Path('/sys/class/drm', node.name, 'device/vendor').exists() and
             Path('/sys/class/drm', node.name,
                  'device/vendor').read_text().strip() == '0x22c2']
    assert len(nodes) == 1, f'expected one TaruGX render node, got {{nodes}}'
    result = subprocess.run([str(test), str(nodes[0])], check=True,
                            capture_output=True, text=True, timeout=30)
    print(result.stdout, end='')
"""
    print(Backend().remote(remote, "test TaruGX Mesa winsys", 90), end="")


if __name__ == "__main__":
    main()
