"""Numeric CPU diagnostics use only synthetic or caller-owned process counters."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from main_thread_metrics import MainThreadSeries


class MainThreadMetricsTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.thread = self.root / '73/task/73/stat'
        self.thread.parent.mkdir(parents=True)
        self.write_stat()

    def write_stat(self, cpu=20, start=800, pid=73):
        fields = ['S'] + ['0'] * 49
        fields[11], fields[12], fields[19] = str(cpu), '0', str(start)
        self.thread.write_text(f'{pid} (synthetic main) ' + ' '.join(fields))

    def test_main_thread_cpu_is_not_the_whole_process_counter(self):
        (self.root / '73/stat').write_text('irrelevant aggregate counter')
        series = MainThreadSeries(ticks_per_second=100)
        first = series.sample(73, 800, 1, self.root)
        self.assertEqual(first['cpuSeconds'], 0)
        self.write_stat(cpu=30)
        result = series.sample(73, 800, 2, self.root)
        self.assertEqual(result['cpuTicks'], 30)
        self.assertAlmostEqual(result['cpuSeconds'], .1)
        self.assertAlmostEqual(result['cpuPercent'], 10)
        self.assertEqual(result['samples'], 2)
        self.assertTrue(all(isinstance(value, (int, float)) for value in result.values()))

    def test_missing_or_reused_main_thread_is_an_error(self):
        series = MainThreadSeries(100)
        self.write_stat(start=801)
        with self.assertRaises(ValueError):
            series.sample(73, 800, 1, self.root)
        self.thread.unlink()
        with self.assertRaises(OSError):
            series.sample(73, 800, 1, self.root)

    def test_counter_rollback_and_backwards_time_are_errors(self):
        for cpu, elapsed in [(19, 2), (21, .5)]:
            with self.subTest(cpu=cpu, elapsed=elapsed):
                self.write_stat()
                series = MainThreadSeries(100)
                series.sample(73, 800, 1, self.root)
                self.write_stat(cpu=cpu)
                with self.assertRaises(ValueError):
                    series.sample(73, 800, elapsed, self.root)

    def test_identity_and_elapsed_arguments_are_validated(self):
        for pid, start, elapsed in [(0, 800, 1), (True, 800, 1),
                                    (73, -1, 1), (73, 800, float('nan')),
                                    (73, 800, -1)]:
            with self.subTest(pid=pid, elapsed=elapsed):
                with self.assertRaises(ValueError):
                    MainThreadSeries(100).sample(pid, start, elapsed, self.root)
        for ticks in [0, -1, True]:
            with self.assertRaises(ValueError):
                MainThreadSeries(ticks)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux procfs required')
    def test_actual_callers_main_thread_can_be_sampled(self):
        from process_metrics import identity
        pid = os.getpid()
        start, _ = identity(Path('/proc') / str(pid) / 'stat', pid)
        result = MainThreadSeries().sample(pid, start, 0)
        self.assertEqual(result['pid'], pid)
        self.assertEqual(result['startTicks'], start)


if __name__ == '__main__':
    unittest.main()
