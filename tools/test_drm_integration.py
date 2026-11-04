#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Run the complete TaruGX DRM hardware regression in one command."""

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run(label: str, *arguments: str) -> None:
    print(f"\n=== {label} ===", flush=True)
    subprocess.run(
        [sys.executable, *arguments], cwd=ROOT, check=True
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hold", type=int, default=1)
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument(
        "--keep-driver", action="store_true",
        help="leave the worktree module loaded after the complete regression",
    )
    args = parser.parse_args()
    if args.hold < 0:
        parser.error("--hold must not be negative")
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")

    mode_test = str(ROOT / "tools" / "test_drm_mode.py")
    run("kernel module build", str(ROOT / "tools" / "build_remote_driver.py"))
    for bpp, name in ((32, "XRGB8888"), (16, "RGB565")):
        run(
            f"{name} KMS, PRIME, fence, and page-flip regression",
            mode_test,
            "--keep-driver",
            "--hold", str(args.hold),
            "--iterations", str(args.iterations),
            "--bpp", str(bpp),
        )
    render_arguments = [str(ROOT / "tools" / "test_drm_render_ioctl.py")]
    if args.keep_driver:
        render_arguments.append("--keep-driver")
    run("2D render, scheduler, fence, and direct-scanout regression",
        *render_arguments)
    print("\nTARUGX DRM INTEGRATION PASS", flush=True)


if __name__ == "__main__":
    main()
