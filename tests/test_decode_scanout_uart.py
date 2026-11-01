import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "decoder", Path(__file__).resolve().parents[1] / "tools/decode_scanout_uart.py")
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class DecodeTest(unittest.TestCase):
    def test_injected_fault(self):
        data = decoder.decode("D1111X46523033" + "0" * 46 + "28001006400640ACFD\r\n")
        self.assertEqual((data["x"], data["y"], data["expected"], data["actual"]),
                         (160, 1, 100, 100))
        self.assertTrue(data["fault_captured"])
        self.assertEqual(data["fifo_valid"], 0)

    def test_waiting_snapshot(self):
        data = decoder.decode("D1111X46523033" + "0" * 62 + "FD")
        self.assertFalse(data["fault_captured"])
        self.assertNotIn("x", data)

    def test_reject_partial_and_old_version(self):
        self.assertIsNone(decoder.decode("D1111X123"))
        self.assertIsNone(decoder.decode("D1111X46523032" + "0" * 64))


if __name__ == "__main__":
    unittest.main()
