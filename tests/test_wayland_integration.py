# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RemoteWaylandTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / "tools" / "test_remote_wayland.py"
        cls.program = path.read_text()

    def test_checks_atomic_1080p_egl_and_error_counters(self):
        program = self.program
        self.assertIn("DRM: supports atomic modesetting", program)
        self.assertIn("1920x1080@60.0", program)
        self.assertIn("weston-simple-egl", program)
        self.assertIn("GL renderer: llvmpipe", program)
        self.assertIn("--pixman", program)
        self.assertIn("EGL vendor string: Mesa Project", program)
        self.assertIn("tarugx_dri_available", program)
        self.assertIn("delta['flip_timeouts'] == 0", program)
        self.assertIn("delta['dma_upload_failures'] == 0", program)

    def test_always_restores_xorg(self):
        program = self.program
        self.assertIn("finally:", program)
        self.assertIn("restore_desktop()", program)
        self.assertIn("result['xorg_restored'] = restored", program)


if __name__ == "__main__":
    unittest.main()
