#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Update and compile the TaruGX Gallium driver on the hardware host."""

import base64
from pathlib import Path

from recover_pcie import Backend


ROOT = Path(__file__).resolve().parents[1]


def remote_program(files: dict[str, str], patch: str) -> str:
    return f"""
import base64
import os
from pathlib import Path
import shutil
import subprocess

files = {files!r}
patch_data = {patch!r}
source = Path('/tmp/tarugx-mesa-23.2.1')
build = Path('/tmp/tarugx-mesa-build')
driver = source/'src/gallium/drivers/tarugx'
assert source.is_dir() and build.is_dir(), 'prepared Mesa build is missing'
assert (source/'VERSION').read_text().strip().startswith('23.2.1'), 'unexpected Mesa source version'
assert driver.is_dir(), 'TaruGX Gallium source directory is missing'
patch_file = source/'.tarugx-current.patch'
patch_file.write_bytes(base64.b64decode(patch_data))
paths_run = subprocess.run(
    ['git', '-C', str(source), 'apply', '--numstat', str(patch_file)],
    check=True, capture_output=True, text=True, timeout=30)
patch_paths = [line.split('\t', 2)[2] for line in paths_run.stdout.splitlines()]
assert patch_paths and all(path and not Path(path).is_absolute() and
                           '..' not in Path(path).parts
                           for path in patch_paths), patch_paths
# This is a generated Mesa tree pinned to the tag above.  Restore only files
# owned by our patch, then apply the current patch so edits outside the copied
# TaruGX driver cannot silently remain stale between incremental builds.
subprocess.run(
    ['git', '-C', str(source), 'restore', '--source=HEAD', '--worktree', '--',
     *patch_paths], check=True, timeout=30)
subprocess.run(
    ['patch', '--batch', '-d', str(source), '-p1', '-i', str(patch_file)],
    check=True, timeout=30)
for name, contents in files.items():
    target = driver/name
    target.write_bytes(base64.b64decode(contents))
(source/'src/tarugx/tarugx_drm.h').write_bytes(
    base64.b64decode(files['tarugx_drm.h']))
environment = os.environ.copy()
environment['MESA_GIT_SHA1_OVERRIDE'] = ''
environment['CCACHE_DISABLE'] = '1'
run = subprocess.run(
    ['ninja', '-C', str(build),
     'src/gallium/targets/dri/libgallium_dri.so'],
    text=True, capture_output=True, timeout=300, env=environment)
print(run.stdout, end='')
print(run.stderr, end='')
if run.returncode:
    print('TARUGX_MESA_BUILD_FAILED')
else:
    output = build/'src/gallium/targets/dri/libgallium_dri.so'
    assert output.exists(), 'Gallium DRI library was not produced'
    installed = output.parent/'tarugx_dri.so'
    temporary = output.parent/'.tarugx_dri.so.new'
    shutil.copy2(output, temporary)
    os.replace(temporary, installed)
    assert installed.read_bytes() == output.read_bytes(), \
        'TaruGX DRI artifact did not refresh'
    print('TARUGX_MESA_BUILD_OK')
"""


def main() -> None:
    sources = {
        "meson.build": ROOT / "mesa" / "gallium" / "meson.build",
        "tarugx_context.c": ROOT / "mesa" / "gallium" / "tarugx_context.c",
        "tarugx_context.h": ROOT / "mesa" / "gallium" / "tarugx_context.h",
        "tarugx_screen.c": ROOT / "mesa" / "gallium" / "tarugx_screen.c",
        "tarugx_screen.h": ROOT / "mesa" / "gallium" / "tarugx_screen.h",
        "tarugx_winsys.c": ROOT / "mesa" / "tarugx_winsys.c",
        "tarugx_winsys.h": ROOT / "mesa" / "tarugx_winsys.h",
        "tarugx_drm.h": ROOT / "drivers" / "tarugx" / "tarugx_drm.h",
    }
    files = {
        name: base64.b64encode(path.read_bytes()).decode("ascii")
        for name, path in sources.items()
    }
    output = Backend().remote(
        remote_program(
            files,
            base64.b64encode(
                (ROOT / "mesa" / "mesa-23.2.1-tarugx.patch").read_bytes()
            ).decode("ascii"),
        ),
        "build Mesa Gallium driver", timeout=330)
    print(output, end="")
    if "TARUGX_MESA_BUILD_FAILED" in output:
        raise RuntimeError("Mesa Gallium build failed")


if __name__ == "__main__":
    main()
