# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "check_public_push.py"
SPEC = importlib.util.spec_from_file_location("check_public_push", SCRIPT)
CHECK = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(CHECK)


class PublicPushCheckTest(unittest.TestCase):
    def test_private_paths_are_rejected(self):
        self.assertIsNotNone(CHECK.forbidden_path(".local/gowin.env"))
        self.assertIsNotNone(CHECK.forbidden_path("docs/lab-notes.md"))
        self.assertIsNotNone(CHECK.forbidden_path("build/output.fs"))

    def test_public_paths_are_accepted(self):
        self.assertIsNone(CHECK.forbidden_path("rtl/tarugx_top.veryl"))
        self.assertIsNone(CHECK.forbidden_path("README.md"))

    def test_sensitive_patterns(self):
        samples = (
            "github_pat_abcdefghijklmnopqrstuvwxyz0123456789",  # public-check: allow
            "password=definitely-not-public",  # public-check: allow
            "ssh://name:secret-value@example.invalid/",  # public-check: allow
            "192.168.10.20",  # public-check: allow
            "/home/private-user/project/",  # public-check: allow
        )
        for sample in samples:
            with self.subTest(sample=sample):
                self.assertTrue(any(pattern.search(sample) for _, pattern in CHECK.PATTERNS))

    def test_allow_marker_is_explicit(self):
        self.assertEqual(CHECK.ALLOW_MARKER, "public-check: allow")


if __name__ == "__main__":
    unittest.main()
