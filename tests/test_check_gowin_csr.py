import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "csr", Path(__file__).resolve().parents[1] / "scripts/check_gowin_csr.py")
csr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(csr)


class CsrTests(unittest.TestCase):
    def test_reference_spelling_and_comments(self):
        self.assertEqual(csr.validate(
            "// reference\nupar_write_driver(0xb00000,0x00FFAA55) // comment\n"
            "upar_wirte_driver(0xb001d8,0x00000005)\n"), 2)

    def test_empty_generated_rom_value(self):
        with self.assertRaisesRegex(ValueError, "行2"):
            csr.validate("// legacy IPC\nupar_write_driver(0xb00060,0x)")

    def test_reject_unrecognized_or_overflowing_commands(self):
        for statement in ("upar_write_driver(0x1,0x100000000)",
                          "upar_write_driver(0x100000000,0x1)",
                          "upar_write_driver(0x1,0x2) extra",
                          "unknown(0x1,0x2)"):
            with self.subTest(statement=statement), self.assertRaises(ValueError):
                csr.validate(statement)

    def test_empty_file(self):
        with self.assertRaises(ValueError):
            csr.validate("// no writes\n")


if __name__ == "__main__":
    unittest.main()
