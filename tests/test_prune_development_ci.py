#!/usr/bin/env python3
"""Offline tests for bounded development CI run retention."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "pruner", Path(__file__).resolve().parents[1] / "tools" / "prune_development_ci.py"
)
pruner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pruner)


class RunPrunerTests(unittest.TestCase):
    @staticmethod
    def runs():
        # Newest first; oldest have smaller IDs. CI histories often contain
        # hundreds of failed intermediate builds.
        result = [
            {"id": i, "status": "completed", "conclusion": "success"}
            for i in range(120, 0, -1)
        ]
        for id_ in (113, 76, 50, 36, 22, 15, 7, 3):
            result[120-id_]["conclusion"] = "failure"
        result[120-2]["status"] = "in_progress"
        result[120-2]["conclusion"] = None
        return result

    def test_keeps_latest_and_recent_failures_and_active(self):
        doomed = pruner.selection(self.runs(), 40, 5)
        self.assertNotIn(113, doomed)  # latest 40 includes 113
        for recent_failed in (76, 50, 36, 22):
            self.assertNotIn(recent_failed, doomed)
        self.assertNotIn(2, doomed)  # still running
        self.assertIn(15, doomed)  # older failure is obsolete
        self.assertIn(1, doomed)
        self.assertEqual(doomed, sorted(doomed))  # oldest first

    def test_preserves_all_small_histories(self):
        self.assertEqual(pruner.selection(self.runs()[:35], 40, 5), [])

    def test_no_completed_after_most_recent_40(self):
        runs = self.runs()
        doomed = pruner.selection(runs, 40, 0)
        self.assertEqual(doomed[-1], 80)
        self.assertNotIn(81, doomed)


if __name__ == "__main__":
    unittest.main()
