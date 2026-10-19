"""Linux process sampling fixtures contain synthetic counters, never user text."""
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from process_metrics import ProcessSeries, read_child_pids, sample_process


class ProcessMetricsTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.process = self.root / '73'
        self.process.mkdir()
        (self.process / 'fd').mkdir()
        for name in ['0', '1', '2']:
            (self.process / 'fd' / name).touch()
        self.stat()
        self.status()
        (self.process / 'smaps_rollup').write_text(
            '1000-9000 ---p 00000000 00:00 0 [rollup]\n'
            'Rss: 4096 kB\nPss: 2100 kB\nPrivate_Clean: 100 kB\n'
            'Private_Dirty: 200 kB\nPrivate_Hugetlb: 12 kB\nSwap: 0 kB\n')

    def stat(self, start=800, user=250, system=50):
        # First entry is field 3; fields14/15 and22 are CPU ticks and start time.
        fields = ['S'] + ['0'] * 49
        fields[11], fields[12], fields[19] = str(user), str(system), str(start)
        (self.process / 'stat').write_text('73 (worker ) with spaces) ' + ' '.join(fields))

    def status(self, rss=4096):
        (self.process / 'status').write_text(
            f'Name:\tsynthetic\nVmRSS:\t{rss} kB\nVmData:\t900 kB\n'
            'VmSwap:\t0 kB\nThreads:\t4\n')

    def sample(self):
        return sample_process(73, self.root)

    def test_collects_private_swap_cpu_and_identity_without_process_text(self):
        result = self.sample()
        self.assertEqual(result, dict(pid=73, startTicks=800, cpuTicks=300,
                                     rssKiB=4096, privateKiB=312, pssKiB=2100,
                                     dataKiB=900, swapKiB=0, threads=4, descriptors=3))
        self.assertTrue(all(isinstance(value, int) for value in result.values()))

    def test_missing_memory_counter_is_an_error_instead_of_zero(self):
        (self.process / 'smaps_rollup').write_text('Rss: 1 kB\n')
        with self.assertRaises(ValueError):
            self.sample()

    def test_missing_process_or_unavailable_memory_does_not_pass_collection(self):
        (self.process / 'smaps_rollup').unlink()
        with self.assertRaises(OSError):
            self.sample()

    def test_permission_failure_is_not_converted_to_empty_metrics(self):
        from unittest.mock import patch
        from process_metrics import read_bounded
        def protected_read(path):
            if path.name == 'smaps_rollup':
                raise PermissionError('Synthetic inaccessible memory counters')
            return read_bounded(path)
        with patch('process_metrics.read_bounded', side_effect=protected_read):
            with self.assertRaises(PermissionError):
                self.sample()

    def test_truncated_stat_and_duplicate_status_counters_are_refused(self):
        (self.process / 'stat').write_text('73 (fixture) S 0')
        with self.assertRaises(ValueError):
            self.sample()
        self.stat()
        with (self.process / 'status').open('a') as stream:
            stream.write('VmRSS: 200 kB\n')
        with self.assertRaises(ValueError):
            self.sample()

    def test_negative_or_wrong_unit_counter_is_refused(self):
        for value in ['-1 kB', '15 MB', 'nan kB']:
            with self.subTest(value=value):
                self.status()
                path = self.process / 'status'
                path.write_text(path.read_text().replace('4096 kB', value))
                with self.assertRaises(ValueError):
                    self.sample()

    def test_process_identity_is_rechecked_after_memory_collection(self):
        from unittest.mock import patch
        from process_metrics import read_bounded
        count = 0
        def changing_read(path):
            nonlocal count
            if path.name == 'stat':
                count += 1
                if count == 2:
                    self.stat(start=801)
            return read_bounded(path)
        with patch('process_metrics.read_bounded', side_effect=changing_read):
            with self.assertRaises(ValueError):
                self.sample()

    def test_cpu_rollback_during_sample_is_refused(self):
        from unittest.mock import patch
        with patch('process_metrics.identity', side_effect=[(800, 500), (800, 300)]):
            with self.assertRaises(ValueError):
                self.sample()

    def test_oversized_stat_is_refused(self):
        (self.process / 'stat').write_text('x' * (1024 * 1024 + 1))
        with self.assertRaises(ValueError):
            self.sample()

    def test_children_from_every_thread_are_deduplicated(self):
        for thread, children in [('73', '74 75'), ('80', '75 81')]:
            path = self.process / 'task' / thread
            path.mkdir(parents=True)
            (path / 'children').write_text(children)
        self.assertEqual(read_child_pids(73, self.root), {74, 75, 81})

    def test_series_computes_elapsed_cpu_and_memory_growth(self):
        series = ProcessSeries(ticks_per_second=100)
        series.observe(self.sample(), 10)
        self.stat(user=300)
        self.status(rss=4196)
        report = series.observe(self.sample(), 12)
        self.assertEqual(report['cpuSeconds'], .5)
        self.assertEqual(report['cpuPercent'], 25)
        self.assertEqual(report['rssGrowthKiB'], 100)
        self.assertEqual(report['privateGrowthKiB'], 0)

    def test_series_rejects_pid_reuse_and_reversed_cpu_or_time(self):
        for field, value, elapsed in [('startTicks', 801, 12), ('pid', 74, 12),
                                      ('cpuTicks', 299, 12), ('cpuTicks', 300, 10),
                                      ('cpuTicks', 300, math.nan)]:
            with self.subTest(field=field, value=value, elapsed=elapsed):
                series = ProcessSeries(ticks_per_second=100)
                first = self.sample()
                series.observe(first, 10)
                with self.assertRaises(ValueError):
                    series.observe({**first, field: value}, elapsed)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux /proc integration')
    def test_owned_current_process_has_real_memory_and_identity(self):
        first = sample_process(os.getpid())
        second = sample_process(os.getpid())
        self.assertEqual(first['startTicks'], second['startTicks'])
        self.assertGreater(first['rssKiB'], 0)
        self.assertGreater(first['privateKiB'], 0)
        self.assertGreaterEqual(second['cpuTicks'], first['cpuTicks'])


if __name__ == '__main__':
    unittest.main()
