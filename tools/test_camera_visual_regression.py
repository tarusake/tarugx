#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Detect display corruption after repeated Xfce window moves via HDMI capture."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import subprocess
import sys
import time

from recover_pcie import Backend


@dataclass(frozen=True)
class Metrics:
    width: int
    height: int
    changed_pixels: int
    changed_fraction: float
    new_dark_pixels: int
    new_dark_fraction: float
    maximum_horizontal_dark_run: int
    maximum_vertical_dark_run: int
    passed: bool


def ppm_tokens(contents: bytes):
    index = 0
    length = len(contents)
    while index < length:
        while index < length and chr(contents[index]).isspace():
            index += 1
        if index < length and contents[index] == ord("#"):
            while index < length and contents[index] not in (10, 13):
                index += 1
            continue
        if index >= length:
            return
        start = index
        while index < length and not chr(contents[index]).isspace():
            index += 1
        yield contents[start:index], index


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    contents = path.read_bytes()
    tokens = ppm_tokens(contents)
    try:
        magic, _ = next(tokens)
        width_token, _ = next(tokens)
        height_token, _ = next(tokens)
        maximum_token, end = next(tokens)
    except StopIteration as error:
        raise ValueError(f"invalid PPM header: {path}") from error
    if magic != b"P6" or maximum_token != b"255":
        raise ValueError(f"unsupported PPM format: {path}")
    width, height = int(width_token), int(height_token)
    while end < len(contents) and chr(contents[end]).isspace():
        end += 1
    pixels = contents[end:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"PPM payload is {len(pixels)} bytes, expected {expected}")
    return width, height, pixels


def in_masks(x: int, y: int, masks: tuple[tuple[int, int, int, int], ...]) -> bool:
    return any(left <= x < left + width and top <= y < top + height
               for left, top, width, height in masks)


def longest_runs(values: list[bool], width: int, height: int) -> tuple[int, int]:
    horizontal = 0
    for y in range(height):
        run = 0
        for x in range(width):
            run = run + 1 if values[y * width + x] else 0
            horizontal = max(horizontal, run)
    vertical = 0
    for x in range(width):
        run = 0
        for y in range(height):
            run = run + 1 if values[y * width + x] else 0
            vertical = max(vertical, run)
    return horizontal, vertical


def compare_pixels(
    width: int,
    height: int,
    baseline: bytes,
    observed: bytes,
    masks: tuple[tuple[int, int, int, int], ...] = (),
    difference_threshold: int = 28,
    dark_threshold: int = 18,
    baseline_brightness: int = 55,
    maximum_changed_fraction: float = 0.025,
    maximum_new_dark_fraction: float = 0.005,
    maximum_dark_run: int = 128,
) -> Metrics:
    if len(baseline) != width * height * 3 or len(observed) != len(baseline):
        raise ValueError("RGB payload size does not match the dimensions")
    changed = 0
    new_dark = 0
    compared = 0
    dark_map = [False] * (width * height)
    for pixel in range(width * height):
        y, x = divmod(pixel, width)
        if in_masks(x, y, masks):
            continue
        offset = pixel * 3
        old = baseline[offset:offset + 3]
        new = observed[offset:offset + 3]
        compared += 1
        if max(abs(old[channel] - new[channel]) for channel in range(3)) > difference_threshold:
            changed += 1
        old_luma = (54 * old[0] + 183 * old[1] + 19 * old[2]) >> 8
        new_luma = (54 * new[0] + 183 * new[1] + 19 * new[2]) >> 8
        if old_luma >= baseline_brightness and new_luma <= dark_threshold:
            new_dark += 1
            dark_map[pixel] = True
    horizontal, vertical = longest_runs(dark_map, width, height)
    changed_fraction = changed / compared if compared else 0.0
    dark_fraction = new_dark / compared if compared else 0.0
    passed = (
        changed_fraction <= maximum_changed_fraction
        and dark_fraction <= maximum_new_dark_fraction
        and horizontal <= maximum_dark_run
        and vertical <= maximum_dark_run
    )
    return Metrics(
        width, height, changed, changed_fraction, new_dark, dark_fraction,
        horizontal, vertical, passed,
    )


def compare_files(baseline: Path, observed: Path, top_mask: int) -> Metrics:
    width, height, first = read_ppm(baseline)
    other_width, other_height, second = read_ppm(observed)
    if (width, height) != (other_width, other_height):
        raise ValueError("capture dimensions changed between frames")
    masks = ((0, 0, width, top_mask),) if top_mask else ()
    return compare_pixels(width, height, first, second, masks)


def mean_luma(pixels: bytes) -> float:
    if not pixels:
        return 0.0
    total = 0
    for offset in range(0, len(pixels), 3):
        total += (54 * pixels[offset] + 183 * pixels[offset + 1]
                  + 19 * pixels[offset + 2]) >> 8
    return total / (len(pixels) // 3)


def capture(device: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-f", "v4l2", "-i", str(device),
             "-vf", "select=gte(n\\,30)", "-frames:v", "1",
             "-pix_fmt", "rgb24", str(destination)],
            check=True,
            timeout=30,
        )
        _, _, pixels = read_ppm(destination)
        if mean_luma(pixels) >= 8.0:
            return
        time.sleep(1)
    raise RuntimeError(f"capture stayed black after 3 attempts: {device}")


def remote_workload(moves: int) -> dict[str, object]:
    program = f"""
import json
from pathlib import Path
import subprocess
import time

host_dir = Path('/home/tarusake/tarugx-dev/host')  # public-check: allow
device = Path('/sys/bus/pci/devices/0000:01:00.0')
names = ('render_commands', 'render_failures', 'bytes_uploaded',
         'dma_uploads', 'dma_upload_failures', 'flip_timeouts')
env = ['runuser', '-u', 'tarusake', '--', 'env', 'DISPLAY=:1',
       'XAUTHORITY='+str(host_dir/'Xauthority')]

def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

windows = run([*env, 'xdotool', 'search', '--sync', '--onlyvisible',
               '--class', 'xfce4-terminal'], timeout=15,
              capture_output=True).stdout.split()
if not windows:
    raise RuntimeError('Xfce terminal window was not found')
window = windows[-1]
run([*env, 'xdotool', 'windowactivate', '--sync', window,
     'windowmove', '--sync', window, '80', '100',
     'windowsize', '--sync', window, '900', '600'], timeout=15,
    capture_output=True)
time.sleep(1)
before = {{name: int((device/name).read_text()) for name in names}}
commands = ['xdotool']
for index in range({moves}):
    x = 40 + (index * 37) % 1080
    y = 60 + (index * 23) % 360
    commands += ['windowmove', '--sync', window, str(x), str(y)]
run([*env, *commands], timeout=120, capture_output=True)
run([*env, 'xdotool', 'windowmove', '--sync', window, '80', '100',
     'windowsize', '--sync', window, '900', '600'], timeout=15,
    capture_output=True)
time.sleep(2)
after = {{name: int((device/name).read_text()) for name in names}}
print(json.dumps({{'moves': {moves},
                  'delta': {{name: after[name] - before[name]
                            for name in names}}}}))
"""
    output = Backend().remote(program, "visual regression window workload", 180)
    return json.loads(output.strip().splitlines()[-1])


def workload_passed(workload: dict[str, object] | None) -> bool:
    if workload is None:
        return True
    delta = workload.get("delta")
    if not isinstance(delta, dict):
        return False
    return all(int(delta.get(name, -1)) == 0 for name in
               ("render_failures", "dma_upload_failures", "flip_timeouts"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path, default=Path("/dev/video0"))
    parser.add_argument("--output", type=Path,
                        default=Path("/tmp/tarugx-visual-regression"))
    parser.add_argument("--moves", type=int, default=180)
    parser.add_argument("--top-mask", type=int, default=48,
                        help="ignore clock/panel rows at the top of the capture")
    parser.add_argument("--compare", nargs=2, type=Path,
                        metavar=("BASELINE", "OBSERVED"),
                        help="compare existing PPM files without hardware")
    args = parser.parse_args()

    workload: dict[str, object] | None = None
    if args.compare:
        baseline, observed = args.compare
    else:
        if not args.device.exists():
            parser.error(f"capture device is unavailable: {args.device}")
        baseline = args.output / "baseline.ppm"
        observed = args.output / "after-window-moves.ppm"
        # Normalize the terminal geometry before taking the reference frame.
        remote_workload(0)
        capture(args.device, baseline)
        time.sleep(1)
        workload = remote_workload(args.moves)
        capture(args.device, observed)

    metrics = compare_files(baseline, observed, args.top_mask)
    passed = metrics.passed and workload_passed(workload)
    result = {
        "result": "visual_regression_pass" if passed
                  else "visual_regression_failed",
        "baseline": str(baseline),
        "observed": str(observed),
        "metrics": asdict(metrics),
        "workload": workload,
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
