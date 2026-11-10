from pathlib import Path
import os
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from active_stability import Evidence, WorkloadFailure


class ActiveBudgetTest(unittest.TestCase):
    def test_diagnostic_duration_is_rejected_before_resource_setup(self):
        from active_stability import run_owned
        with patch.dict(os.environ, {'REWORD_ACTIVE_DIAGNOSTIC_CONTINUE_CPU': '1'}):
            with self.assertRaises(WorkloadFailure):
                run_owned(SimpleNamespace(seconds=301))

    def sample(self):
        return dict(rssKiB=100000, swapKiB=0, privateGrowthKiB=0,
                    elapsedSeconds=31, cpuPercent=11)

    def test_normal_cpu_overrun_stops(self):
        from active_budget import enforce_budget
        with self.assertRaises(WorkloadFailure):
            enforce_budget(self.sample(), 'app', True, False, Evidence(), {})

    def test_diagnostic_cpu_overrun_can_never_pass(self):
        from active_budget import enforce_budget
        evidence, metadata = Evidence(), {}
        enforce_budget(self.sample(), 'app', True, True, evidence, metadata)
        self.assertTrue(metadata['cpuBudgetExceeded'])
        self.assertEqual(evidence.state, 'failed')
        self.assertEqual(evidence.failure, 'resource-budget')
        with self.assertRaises(WorkloadFailure):
            evidence.finish()

    def test_diagnostic_never_continues_memory_or_swap_overrun(self):
        from active_budget import enforce_budget
        for change in ({'rssKiB': 600000}, {'swapKiB': 1}, {'privateGrowthKiB': 70000}):
            with self.subTest(change=change), self.assertRaises(WorkloadFailure):
                sample = self.sample()
                sample.update(change)
                enforce_budget(sample, 'app', True, True, Evidence(), {})


if __name__ == '__main__':
    unittest.main()
