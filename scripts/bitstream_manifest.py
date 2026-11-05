#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Record and verify the exact source inputs used for a Gowin bitstream."""

import argparse
import hashlib
import json
from pathlib import Path
import sys


REPO = Path(__file__).resolve().parents[1]
MANIFEST = "tarugx_build_manifest.json"
PROJECT_SUFFIXES = {".cst", ".mem", ".sdc", ".sv", ".v", ".vg"}


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def key_for(path: Path, repo: Path) -> str:
    path = path.resolve(strict=True)
    try:
        return path.relative_to(repo).as_posix()
    except ValueError:
        return str(path)


def path_for(key: str, repo: Path) -> Path:
    path = Path(key)
    return path if path.is_absolute() else repo / path


def auto_inputs(repo: Path, output: Path) -> set[Path]:
    inputs = {
        repo / "Veryl.toml",
        repo / "Veryl.lock",
        repo / "tarugx.f",
        repo / "scripts" / "build_verified_framebuffer.sh",
        repo / "scripts" / "gowin_build_framebuffer.tcl",
        repo / "scripts" / "gowin_generate_video.sh",
        repo / "scripts" / "check_gowin_csr.py",
        repo / "fpga" / "tang_mega_138k" / "framebuffer.sdc",
        repo / "build" / "gowin_video" / "tarugx_video_pll.v",
        repo / "build" / "gowin_ddr3_b" / "DDR3MI.vg",
    }
    inputs.update((repo / "rtl").rglob("*.veryl"))
    inputs.update(
        path for path in (repo / "fpga").rglob("*")
        if path.is_file() and path.suffix in PROJECT_SUFFIXES
    )
    file_list = repo / "tarugx.f"
    if file_list.is_file():
        inputs.update(
            Path(line.strip()) for line in file_list.read_text().splitlines()
            if line.strip()
        )
    # The build Tcl creates mode-specific wrappers and constraints beside the
    # Gowin project.  They must match the programmed pnr output as well.
    project_root = output.parents[2]
    inputs.update(
        path for path in project_root.iterdir()
        if path.is_file() and path.suffix in PROJECT_SUFFIXES
    )
    return {path.resolve() for path in inputs if path.is_file()}


def create(output: Path, repo: Path, video_mode: str,
           extra_inputs: list[Path]) -> None:
    output = output.resolve(strict=True)
    bitstream = output / "tarugx.fs"
    timing = output / "tarugx_tr_content.html"
    for required in (bitstream, timing):
        if not required.is_file() or required.stat().st_size == 0:
            raise ValueError(f"missing build output: {required}")
    automatic = auto_inputs(repo, output)
    extras = {path.resolve(strict=True) for path in extra_inputs}
    inputs = automatic | extras
    data = {
        "version": 1,
        "video_mode": video_mode,
        "bitstream_sha256": digest(bitstream),
        "timing_sha256": digest(timing),
        "auto_inputs": sorted(key_for(path, repo) for path in automatic),
        "extra_inputs": sorted(key_for(path, repo) for path in extras),
        "input_sha256": {
            key_for(path, repo): digest(path)
            for path in sorted(inputs, key=lambda item: str(item))
        },
    }
    destination = output / MANIFEST
    destination.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    print(f"Build manifest: {destination}")


def verify(output: Path, repo: Path, expected_video_mode: str | None = None) -> None:
    output = output.resolve(strict=True)
    manifest = output / MANIFEST
    if not manifest.is_file():
        raise ValueError(f"missing build manifest: {manifest}; rebuild before programming")
    data = json.loads(manifest.read_text())
    if data.get("version") != 1:
        raise ValueError("unsupported build manifest version")
    video_mode = data.get("video_mode")
    if expected_video_mode is not None and video_mode != expected_video_mode:
        raise ValueError(
            f"video mode mismatch: expected {expected_video_mode}, got {video_mode}"
        )
    expected_auto = set(data.get("auto_inputs", []))
    current_auto = {key_for(path, repo) for path in auto_inputs(repo, output)}
    if current_auto != expected_auto:
        added = sorted(current_auto - expected_auto)
        removed = sorted(expected_auto - current_auto)
        raise ValueError(f"build input set changed: added={added}, removed={removed}")
    for key, expected in data.get("input_sha256", {}).items():
        path = path_for(key, repo)
        if not path.is_file():
            raise ValueError(f"build input missing: {key}")
        actual = digest(path)
        if actual != expected:
            raise ValueError(f"build input changed: {key}")
    artifacts = {
        "tarugx.fs": data.get("bitstream_sha256"),
        "tarugx_tr_content.html": data.get("timing_sha256"),
    }
    for name, expected in artifacts.items():
        path = output / name
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f"build artifact changed: {name}")
    print(f"Build manifest verified: mode={video_mode} inputs={len(data['input_sha256'])}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("create", "verify"))
    parser.add_argument("output", type=Path)
    parser.add_argument("--video-mode", default="480p60")
    parser.add_argument("--expected-video-mode",
                        help="reject a verified build for a different video mode")
    parser.add_argument("--input", action="append", type=Path, default=[])
    parser.add_argument("--repo-root", type=Path, default=REPO,
                        help=argparse.SUPPRESS)
    args = parser.parse_args()
    repo = args.repo_root.resolve(strict=True)
    try:
        if args.action == "create":
            create(args.output, repo, args.video_mode, args.input)
        else:
            verify(args.output, repo, args.expected_video_mode)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"Build manifest check failed: {error}", file=sys.stderr)
        raise SystemExit(1) from None


if __name__ == "__main__":
    main()
