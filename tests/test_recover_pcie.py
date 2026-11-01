import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location('recover', Path(__file__).resolve().parents[1]/'tools/recover_pcie.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
UP = {'linked': True, 'bound': False, 'id': 0x110022c2}
DOWN = {'linked': False, 'bound': False, 'id': None}

class RecoveryTest(unittest.TestCase):
    def test_healthy_never_resets(self):
        host, reset = Mock(return_value=UP), Mock()
        self.assertEqual(m.recover(host, reset)[0], 'already_linked')
        self.assertEqual(host.call_count, 1)
        reset.assert_not_called()

    def test_bound_refused(self):
        host, reset = Mock(return_value={**DOWN, 'bound': True}), Mock()
        with self.assertRaises(RuntimeError): m.recover(host, reset)
        reset.assert_not_called()
        self.assertEqual(host.call_count, 1)

    def test_failed_ssh_never_resets(self):
        reset = Mock()
        with self.assertRaises(RuntimeError):
            m.recover(Mock(side_effect=RuntimeError('SSH')), reset)
        reset.assert_not_called()

    def test_one_reset_then_rescan(self):
        actions = []
        def host(action):
            actions.append(action)
            return UP if len(actions) >= 4 else DOWN
        reset = Mock()
        self.assertEqual(m.recover(host, reset, lambda _: None)[0], 'recovered')
        self.assertEqual(actions, ['inspect', 'remove', 'rescan', 'inspect'])
        reset.assert_called_once()

    def test_unenumerated_link_only_rescans(self):
        host = Mock(side_effect=[{**UP, 'id': None}, UP, UP])
        reset = Mock()
        self.assertEqual(m.recover(host, reset, lambda _: None)[0], 'recovered')
        reset.assert_not_called()
        self.assertEqual([c.args[0] for c in host.call_args_list], ['inspect', 'rescan', 'inspect'])

    def test_no_endless_reset_loop(self):
        reset = Mock()
        with self.assertRaises(RuntimeError):
            m.recover(Mock(return_value=DOWN), reset, lambda _: None)
        reset.assert_called_once()

    def test_guard_failure_stops_before_jtag(self):
        reset = Mock()
        with self.assertRaises(RuntimeError):
            m.recover(Mock(side_effect=[DOWN, RuntimeError('race/driver')]), reset)
        reset.assert_not_called()

if __name__ == '__main__': unittest.main()
