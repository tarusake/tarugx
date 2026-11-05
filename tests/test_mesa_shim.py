# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MesaShimTest(unittest.TestCase):
    def test_exports_tarugx_entry_point_and_forwards_to_kms_swrast(self):
        source = (ROOT / "mesa" / "tarugx_dri_shim.c").read_text()
        self.assertIn("__driDriverGetExtensions_tarugx", source)
        self.assertIn("__driDriverGetExtensions_kms_swrast", source)

    def test_installer_verifies_exported_symbol(self):
        source = (ROOT / "tools" / "install_remote_mesa_shim.py").read_text()
        self.assertIn("nm', '-D'", source)
        self.assertIn("libgallium_dri.so", source)
        self.assertIn("mesa_shim_installed", source)


if __name__ == "__main__":
    unittest.main()
