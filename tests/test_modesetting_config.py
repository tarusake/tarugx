# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
from pathlib import Path
import unittest


ROOT = Path(__file__).parents[1]


class ModesettingConfigTest(unittest.TestCase):
    def test_standard_driver_configuration(self):
        config = (ROOT / "host" / "xorg-modesetting.conf").read_text()
        for required in (
            'Driver "modesetting"',
            'Option "kmsdev" "/dev/dri/by-path/pci-0000:01:00.0-card"',
            'Option "AccelMethod" "none"',
            'Option "ShadowFB" "true"',
            'Option "PageFlip" "false"',
            'DefaultDepth 16',
            'Modes "1920x1080"',
        ):
            with self.subTest(required=required):
                self.assertIn(required, config)
        self.assertNotIn('Driver "tarugx"', config)


if __name__ == "__main__":
    unittest.main()
