# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "bitstream_manifest",
    Path(__file__).resolve().parents[1] / "scripts" / "bitstream_manifest.py",
)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class BitstreamManifestTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.repo = Path(self.temporary.name)
        (self.repo / "rtl").mkdir()
        (self.repo / "fpga").mkdir()
        (self.repo / "rtl" / "core.veryl").write_text("module core {}\n")
        (self.repo / "Veryl.toml").write_text("[project]\n")
        generated = self.repo / "build" / "core.sv"
        generated.parent.mkdir()
        generated.write_text("module core; endmodule\n")
        (self.repo / "tarugx.f").write_text(str(generated) + "\n")
        self.output = (
            self.repo / "build" / "gowin_framebuffer" /
            "tarugx" / "impl" / "pnr"
        )
        self.output.mkdir(parents=True)
        (self.output / "tarugx.fs").write_bytes(b"bitstream")
        (self.output / "tarugx_tr_content.html").write_text("timing")
        self.extra = self.repo / "vendor.v"
        self.extra.write_text("module vendor; endmodule\n")
        m.create(self.output, self.repo, "1080p60_scanout", [self.extra])

    def tearDown(self):
        self.temporary.cleanup()

    def test_unchanged_build_verifies(self):
        m.verify(self.output, self.repo)

    def test_changed_source_is_rejected(self):
        (self.repo / "rtl" / "core.veryl").write_text("module changed {}\n")
        with self.assertRaisesRegex(ValueError, "build input changed"):
            m.verify(self.output, self.repo)

    def test_new_source_is_rejected(self):
        (self.repo / "rtl" / "new.veryl").write_text("module new {}\n")
        with self.assertRaisesRegex(ValueError, "build input set changed"):
            m.verify(self.output, self.repo)

    def test_changed_bitstream_is_rejected(self):
        (self.output / "tarugx.fs").write_bytes(b"different")
        with self.assertRaisesRegex(ValueError, "build artifact changed"):
            m.verify(self.output, self.repo)

    def test_changed_external_input_is_rejected(self):
        self.extra.write_text("module changed; endmodule\n")
        with self.assertRaisesRegex(ValueError, "build input changed"):
            m.verify(self.output, self.repo)


if __name__ == "__main__":
    unittest.main()
