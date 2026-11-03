# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

TOOLS = Path(__file__).resolve().parents[1]/'tools'
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location(
    'program_flash', TOOLS/'program_framebuffer_flash.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class FlashProgrammingTest(unittest.TestCase):
    def backend(self, side_effect=None):
        backend = object.__new__(m.FlashBackend)
        backend._run = Mock(side_effect=side_effect)
        return backend

    @patch.object(m.time, 'sleep')
    def test_program_verify_reconfigure_and_user_code(self, sleep):
        outputs = ['', '', '', '', 'User Code is: ' + m.USER_CODE, '']
        backend = self.backend(outputs)
        backend.program(Path('/tmp/release.fs'))
        operations = [call.args[0] for call in backend._run.call_args_list]
        self.assertEqual([args[2] for args in operations[1:5]],
                         ['53', '66', '1', '73'])
        self.assertIn('--fsFile', operations[1])
        self.assertIn('--fsFile', operations[2])
        self.assertNotIn('--fsFile', operations[3])
        sleep.assert_called_once_with(2)
        self.assertEqual(operations[-1][-1], 'attach')

    def test_verify_failure_still_reattaches(self):
        backend = self.backend(['', '', RuntimeError('verify failed'), ''])
        with self.assertRaisesRegex(RuntimeError, 'verify failed'):
            backend.program(Path('/tmp/release.fs'))
        self.assertEqual(backend._run.call_args_list[-1].args[0][-1], 'attach')

    def test_bad_user_code_is_rejected_and_reattached(self):
        backend = self.backend(['', '', '', '', 'User Code is: 0', ''])
        with patch.object(m.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'Unexpected FPGA UserCode'):
                backend.program(Path('/tmp/release.fs'))
        self.assertEqual(backend._run.call_args_list[-1].args[0][-1], 'attach')


if __name__ == '__main__':
    unittest.main()
