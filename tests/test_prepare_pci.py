# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""No hardware accesses: validate PCI window encoding and write width."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "prepare_pci", Path(__file__).resolve().parents[1] / "tools/prepare_pci.py")
pci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pci)


class PreparePciTest(unittest.TestCase):
    def test_window(self):
        self.assertEqual(pci.window_value(0x80200000, 0x817fffff), 0x81708020)

    def test_reject_invalid_windows(self):
        for pair in [(0, 0xfffff), (0x80200001, 0x817fffff),
                     (0x80200000, 0x81800000), (0x100000000, 0x100ffffff),
                     (0x80300000, 0x802fffff)]:
            with self.subTest(pair=pair), self.assertRaises(ValueError):
                pci.window_value(*pair)

    def test_command_write_does_not_touch_status(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            original = bytes(range(64))
            (path / "config").write_bytes(original)
            pci.write_config(path, 4, 2, 0x0002)
            data = (path / "config").read_bytes()
            self.assertEqual(data[:4], original[:4])
            self.assertEqual(data[4:6], b"\x02\x00")
            self.assertEqual(data[6:], original[6:])


if __name__ == "__main__":
    unittest.main()
