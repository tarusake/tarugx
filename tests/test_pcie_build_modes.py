"""Reject unsafe IP/reset combinations before invoking the FPGA tools."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildModes(unittest.TestCase):
    def run_mode(self, *, por=False, generated=True, **flags):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            for name in ("pcie_controller", "upar_arbiter", "src/gowin_pll"):
                (tmp / name).mkdir(parents=True)
            (tmp / "serdes.v").write_text("input por_n_i;\n" if por else "// no POR input\n")
            (tmp / "serdes.csr").write_text("upar_write_driver(0x1,0x0)\n")
            for name in ("pcie_controller/pcie_controller.v", "upar_arbiter/upar_arbiter.v",
                         "src/gowin_pll/gowin_pll.v"):
                (tmp / name).write_text("// mock IP\n")
            env = {k: v for k, v in os.environ.items() if not k.startswith("TARUGX_PCIE_")}
            env["SIPEED_PCIE_GEN2_DIR"] = str(tmp)
            if generated:
                env["TARUGX_PCIE_GENERATED_DIR"] = str(tmp)
            env.update({"TARUGX_PCIE_" + k: str(v) for k, v in flags.items()})
            # Abort at the first tool command: no project or bitstream is created.
            script = ('proc create_project {args} {error "VALIDATED_TOOL_BOUNDARY"}\n'
                      'if {[catch {source scripts/gowin_build_framebuffer.tcl} msg]} '
                      '{puts stderr $msg; exit 1}\n')
            result = subprocess.run(["tclsh"], input=script, text=True,
                                    capture_output=True, env=env, cwd=ROOT, timeout=10)
            self.assertEqual(result.returncode, 1)
            return result.stderr

    def test_accept_explicit_modes(self):
        for flags, por in (({}, False), ({"POR_RELEASE_TEST": 1}, True),
                           ({"PHY_RECOVERY_EXPERIMENT": 1}, True)):
            with self.subTest(flags=flags):
                self.assertIn("VALIDATED_TOOL_BOUNDARY", self.run_mode(por=por, **flags))

    def test_reject_unconnected_por(self):
        self.assertIn("refusing an unconnected port", self.run_mode(por=True))

    def test_require_por_port(self):
        for flag in ("POR_RELEASE_TEST", "PHY_RECOVERY_EXPERIMENT"):
            with self.subTest(flag=flag):
                self.assertIn("requires the generated por_n_i input", self.run_mode(**{flag: 1}))

    def test_require_generated_bundle(self):
        for flag in ("POR_RELEASE_TEST", "PHY_RECOVERY_EXPERIMENT"):
            with self.subTest(flag=flag):
                self.assertIn("requires a generated IP bundle",
                              self.run_mode(generated=False, **{flag: 1}))

    def test_reject_conflicting_resets(self):
        for flag in ("POR_RELEASE_TEST", "RECOVERY_EXPERIMENT"):
            with self.subTest(flag=flag):
                self.assertIn("Select only one recovery experiment",
                              self.run_mode(por=True, PHY_RECOVERY_EXPERIMENT=1, **{flag: 1}))
        self.assertIn("cannot be combined with core-only recovery",
                      self.run_mode(RECOVERY_EXPERIMENT=1))


if __name__ == "__main__":
    unittest.main()
