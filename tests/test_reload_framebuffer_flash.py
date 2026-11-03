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
    'reload_flash', TOOLS/'reload_framebuffer_flash.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class FlashReloadTest(unittest.TestCase):
    def backend(self, side_effect=None):
        backend = object.__new__(m.ReloadBackend)
        backend._run = Mock(side_effect=side_effect)
        return backend

    @patch.object(m.time, 'sleep')
    def test_reconfigure_and_user_code(self, sleep):
        backend = self.backend(['', '', 'User Code is: ' + m.USER_CODE, ''])
        backend.reload()
        operations = [call.args[0] for call in backend._run.call_args_list]
        self.assertEqual([args[2] for args in operations[1:3]], ['1', '73'])
        self.assertNotIn('--fsFile', operations[1])
        sleep.assert_called_once_with(2)
        self.assertEqual(operations[-1][-1], 'attach')

    def test_bad_user_code_is_rejected_and_reattached(self):
        backend = self.backend(['', '', 'User Code is: 0', ''])
        with patch.object(m.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'Unexpected FPGA UserCode'):
                backend.reload()
        self.assertEqual(backend._run.call_args_list[-1].args[0][-1], 'attach')


if __name__ == '__main__':
    unittest.main()
