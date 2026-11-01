import importlib.util
import itertools
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location("desktop",Path(__file__).parents[1]/"host/desktop.py")
desktop=importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)

class DesktopStartup(unittest.TestCase):
    def test_cold_probe_timeout_is_retried(self):
        clock=itertools.count(0,0.1)
        results=[subprocess.TimeoutExpired("xrandr",5),subprocess.CompletedProcess([],1),subprocess.CompletedProcess([],0)]
        with patch.object(desktop.subprocess,"run",side_effect=results) as run, patch.object(desktop.time,"monotonic",side_effect=lambda:next(clock)), patch.object(desktop.time,"sleep"):
            desktop.wait_for_xorg("test",["DISPLAY=:1"])
        self.assertEqual(run.call_count,3)

    def test_deadline_is_bounded(self):
        clock=itertools.count()
        with patch.object(desktop.subprocess,"run",return_value=subprocess.CompletedProcess([],1)), patch.object(desktop.time,"monotonic",side_effect=lambda:next(clock)), patch.object(desktop.time,"sleep"):
            with self.assertRaisesRegex(RuntimeError,"deadline"):
                desktop.wait_for_xorg("test",[],timeout=4)

    def test_missing_program_is_not_suppressed(self):
        with patch.object(desktop.subprocess,"run",side_effect=FileNotFoundError):
            with self.assertRaises(FileNotFoundError):desktop.wait_for_xorg("test",[])
