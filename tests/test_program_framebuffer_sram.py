# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location(
    'program_sram', Path(__file__).resolve().parents[1]/'tools/program_framebuffer_sram.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class OrchestrationTest(unittest.TestCase):
    def test_success(self):
        calls = []
        result = m.orchestrate(lambda: calls.append('prepare'),
                               lambda: calls.append('program'),
                               lambda: calls.append('restore') or {'display': 'active'})
        self.assertEqual(calls, ['prepare', 'program', 'restore'])
        self.assertEqual(result, {'display': 'active'})

    def test_program_failure_still_restores(self):
        restore = Mock(return_value={})
        with self.assertRaisesRegex(RuntimeError, 'program failed'):
            m.orchestrate(Mock(), Mock(side_effect=RuntimeError('program failed')), restore)
        restore.assert_called_once()

    def test_prepare_failure_stops(self):
        program, restore = Mock(), Mock()
        with self.assertRaisesRegex(RuntimeError, 'prepare failed'):
            m.orchestrate(Mock(side_effect=RuntimeError('prepare failed')), program, restore)
        program.assert_not_called()
        restore.assert_not_called()

    def test_restore_failure_is_reported(self):
        with self.assertRaisesRegex(RuntimeError, 'restore failed'):
            m.orchestrate(Mock(), Mock(), Mock(side_effect=RuntimeError('restore failed')))

    def test_dual_failure_reports_both(self):
        with self.assertRaisesRegex(RuntimeError, 'host restoration also failed'):
            m.orchestrate(Mock(), Mock(side_effect=RuntimeError('program failed')),
                          Mock(side_effect=RuntimeError('restore failed')))


if __name__ == '__main__':
    unittest.main()
