# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).parents[1] / "tools" / "test_camera_visual_regression.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("camera_visual_regression", SCRIPT)
CAMERA = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = CAMERA
SPEC.loader.exec_module(CAMERA)


class CameraVisualRegressionTest(unittest.TestCase):
    @staticmethod
    def frame(width, height, color):
        return bytes(color) * (width * height)

    def test_identical_frame_passes(self):
        image = self.frame(320, 180, (80, 100, 120))
        result = CAMERA.compare_pixels(320, 180, image, image)
        self.assertTrue(result.passed)
        self.assertEqual(result.changed_pixels, 0)

    def test_small_noise_passes(self):
        baseline = self.frame(320, 180, (80, 100, 120))
        observed = bytearray(baseline)
        for index in range(0, len(observed), 997):
            observed[index] = min(255, observed[index] + 20)
        result = CAMERA.compare_pixels(320, 180, baseline, bytes(observed))
        self.assertTrue(result.passed)

    def test_black_horizontal_strip_fails(self):
        width, height = 320, 180
        baseline = self.frame(width, height, (100, 120, 140))
        observed = bytearray(baseline)
        for y in range(80, 86):
            for x in range(20, 300):
                offset = (y * width + x) * 3
                observed[offset:offset + 3] = b"\0\0\0"
        result = CAMERA.compare_pixels(width, height, baseline, bytes(observed))
        self.assertFalse(result.passed)
        self.assertGreater(result.maximum_horizontal_dark_run, 128)

    def test_top_mask_ignores_clock_change(self):
        width, height = 320, 180
        baseline = self.frame(width, height, (100, 120, 140))
        observed = bytearray(baseline)
        observed[:width * 20 * 3] = b"\0" * (width * 20 * 3)
        result = CAMERA.compare_pixels(
            width, height, baseline, bytes(observed), ((0, 0, width, 20),)
        )
        self.assertTrue(result.passed)

    def test_mean_luma_distinguishes_black_frame(self):
        self.assertEqual(CAMERA.mean_luma(self.frame(4, 3, (0, 0, 0))), 0)
        self.assertGreater(CAMERA.mean_luma(self.frame(4, 3, (80, 100, 120))), 8)

    def test_ppm_round_trip(self):
        pixels = self.frame(4, 3, (1, 2, 3))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.ppm"
            path.write_bytes(b"P6\n# capture\n4 3\n255\n" + pixels)
            self.assertEqual(CAMERA.read_ppm(path), (4, 3, pixels))


if __name__ == "__main__":
    unittest.main()
