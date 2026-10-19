"""Synthetic protocol and acceptance checks for the owned GUI workload."""
import base64
from contextlib import closing
import json
import os
from pathlib import Path
import sys
import signal
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from active_stability import Evidence, FixtureProtocol, WorkloadFailure, write_report


class ActiveStabilityTest(unittest.TestCase):
    def test_history_read_closes_connection_on_success_and_policy_failure(self):
        import sqlite3
        from active_history import HistoryCheck
        real_connect = sqlite3.connect
        opened = []
        def connect(*args, **kwargs):
            connection = real_connect(*args, **kwargs)
            opened.append(connection)
            return connection
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / 'synthetic.sqlite'
            with closing(real_connect(database)) as connection, connection:
                connection.execute('CREATE TABLE history(id INTEGER, original TEXT, corrected TEXT, bytes INTEGER)')
                connection.execute('CREATE TABLE statistics(id INTEGER)')
            check = HistoryCheck(database, Evidence())
            with patch('active_history.sqlite3.connect', side_effect=connect):
                self.assertEqual(check.count(), 0)
            with self.assertRaises(sqlite3.ProgrammingError):
                opened[-1].execute('SELECT 1')
            with closing(real_connect(database)) as connection, connection:
                connection.execute('INSERT INTO statistics VALUES(1)')
            with patch('active_history.sqlite3.connect', side_effect=connect):
                with self.assertRaises(WorkloadFailure):
                    check.count()
            with self.assertRaises(sqlite3.ProgrammingError):
                opened[-1].execute('SELECT 1')

    def test_private_database_allows_fixture_and_denies_workload_gui(self):
        import sqlite3
        from active_stability import seed_database
        with tempfile.TemporaryDirectory() as directory:
            database = seed_database(Path(directory) / 'data', Path('/synthetic/fixture'),
                                     Path('/synthetic/reword'))
            with closing(sqlite3.connect(database)) as connection, connection:
                rows = dict(connection.execute('SELECT id,decision FROM applications'))
            self.assertEqual(rows, {'/synthetic/fixture': 'allow', '/synthetic/reword': 'deny'})

    def test_fragmented_protocol_preserves_exact_snapshot_and_event_order(self):
        protocol = FixtureProtocol()
        protocol.feed(b'CHANGE:1:100\nCHA')
        protocol.feed(b'NGE:2:110\nDATA:' + base64.b64encode('متن test'.encode()) + b'\n')
        self.assertEqual(protocol.changes, [(1, 100), (2, 110)])
        self.assertEqual(protocol.take_snapshot(), 'متن test')
        self.assertIsNone(protocol.take_snapshot())

    def test_bad_sequence_time_payload_and_oversized_frame_are_refused(self):
        for payload in [b'CHANGE:0:5\n', b'CHANGE:1:-3\n', b'OTHER:1:2\n',
                        b'DATA:%%%%\n', b'DATA:/w==\n', b'x' * (262144 + 1),
                        b'CHANGE:1:10\nCHANGE:2:9\n']:
            with self.subTest(payload_length=len(payload)):
                with self.assertRaises(WorkloadFailure):
                    FixtureProtocol().feed(payload)

    def test_no_snapshot_queue_or_event_buffer_can_grow_without_bound(self):
        protocol = FixtureProtocol()
        for index in range(1, 2001):
            protocol.feed(f'CHANGE:{index}:{index}\n'.encode())
        self.assertLessEqual(len(protocol.changes), 512)
        self.assertEqual(protocol.changes[-1], (2000, 2000))
        protocol.feed(b'DATA:YQ==\n')
        with self.assertRaises(WorkloadFailure):
            protocol.feed(b'DATA:Yg==\n')

    def populate_checks(self, evidence, excluded=None):
        for name in ['live-first', 'live-second', 'live-third', 'live-undo',
                     'selected-fix', 'selected-undo', 'pause', 'protected',
                     'oversized-selection', 'history-pruned', 'app-sampled',
                     'helper-sampled', 'duration-complete', 'selection-limit-control']:
            if name != excluded:
                evidence.counter(name)

    def test_mismatch_and_excess_history_fail_without_copying_content_into_reports(self):
        evidence = Evidence()
        with self.assertRaises(WorkloadFailure):
            evidence.expect_text('expected synthetic input', 'wrong synthetic input')
        self.assertEqual(evidence.report()['failure'], 'native-text-mismatch')
        self.assertNotIn('synthetic input', json.dumps(evidence.report()))
        for count, byte_count in [(101, 10), (1, 20 * 1024 * 1024 + 1)]:
            with self.assertRaises(WorkloadFailure):
                Evidence().history(count, byte_count)

    def test_missing_protected_check_and_over_budget_latency_cannot_pass(self):
        evidence = Evidence()
        self.populate_checks(evidence, excluded='protected')
        evidence.latency(1000)
        with self.assertRaises(WorkloadFailure):
            evidence.finish()
        evidence = Evidence()
        self.populate_checks(evidence)
        evidence.latency(50000)
        evidence.latency(51000)
        with self.assertRaises(WorkloadFailure):
            evidence.finish()
        self.assertEqual(evidence.report()['failure'], 'latency-budget')

    def test_complete_evidence_reports_actual_percentiles_without_text(self):
        evidence = Evidence()
        for micros in [1000, 2000, 3000, 4000, 45000]:
            evidence.latency(micros)
        self.populate_checks(evidence)
        evidence.counter('live-first')
        evidence.finish()
        report = evidence.report()
        self.assertEqual(report['state'], 'passed')
        self.assertEqual(report['latency']['p50Ms'], 3)
        self.assertEqual(report['latency']['p95Ms'], 45)
        self.assertEqual(report['counters']['live-first'], 2)
        self.assertNotIn('original', json.dumps(report))

    def test_missing_latency_and_invalid_numeric_samples_cannot_pass(self):
        with self.assertRaises(WorkloadFailure):
            Evidence().finish()
        for value in [-1, float('nan'), float('inf')]:
            with self.subTest(value=value):
                with self.assertRaises(WorkloadFailure):
                    Evidence().latency(value)

    @unittest.skipUnless(sys.platform.startswith('linux') and hasattr(os, 'pidfd_open') and
                         hasattr(signal, 'pidfd_send_signal'), 'Linux Python with pidfd support required')
    def test_owned_process_exit_is_detected_without_sampling_replacement_pid(self):
        import subprocess
        from active_stability import ProcessWatch
        process = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(5)'])
        try:
            with ProcessWatch(process.pid, 'app') as watch:
                first = watch.sample(0)
                self.assertGreater(first['rssKiB'], 0)
                watch.terminate()
                process.wait(timeout=2)
                with self.assertRaises(WorkloadFailure) as failure:
                    watch.sample(1)
                self.assertEqual(failure.exception.reason, 'app-process-exit')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    def test_wait_timeout_and_cancellation_do_not_return_success(self):
        import threading
        from active_stability import wait_until
        with self.assertRaises(WorkloadFailure) as failure:
            wait_until(lambda: False, timeout=.02)
        self.assertEqual(failure.exception.reason, 'operation-timeout')
        cancelled = threading.Event()
        timer = threading.Timer(.01, cancelled.set)
        timer.start()
        try:
            with self.assertRaises(WorkloadFailure) as failure:
                wait_until(lambda: False, timeout=1, cancelled=cancelled.is_set)
            self.assertEqual(failure.exception.reason, 'cancelled')
        finally:
            timer.cancel()
            timer.join()

    def test_wait_propagates_monitor_failure_before_accepting_stale_result(self):
        from active_stability import wait_until
        def rejected_metrics():
            raise WorkloadFailure('metrics-unavailable')
        with self.assertRaises(WorkloadFailure) as failure:
            wait_until(lambda: True, timeout=1, tick=rejected_metrics)
        self.assertEqual(failure.exception.reason, 'metrics-unavailable')

    def test_desktop_input_is_refused_without_the_owned_context(self):
        from active_desktop import OwnedDesktop
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(WorkloadFailure) as failure:
                OwnedDesktop(Path(directory))
            self.assertEqual(failure.exception.reason, 'test-infrastructure')

    def test_fixture_client_reads_owned_snapshot_and_rejects_process_loss(self):
        import subprocess
        from active_fixture import FixtureClient
        program = "import sys\nfor line in sys.stdin:\n if line.strip() == 'snapshot': print('DATA:c3ludGhldGlj', flush=True)\n"
        process = subprocess.Popen([sys.executable, '-u', '-c', program],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            client = FixtureClient(process)
            self.assertEqual(client.snapshot(), 'synthetic')
            self.assertEqual(client.last_snapshot_characters, 9)
            process.terminate()
            process.wait(timeout=2)
            with self.assertRaises(WorkloadFailure) as failure:
                client.snapshot()
            self.assertEqual(failure.exception.reason, 'fixture-process-exit')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdin.close()
            process.stdout.close()

    def test_report_replaces_only_complete_validated_json(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'report.json'
            destination.write_text('{"state":"previous"}')
            with self.assertRaises(ValueError):
                write_report(destination, {'invalid': float('nan')})
            self.assertEqual(json.loads(destination.read_text()), {'state': 'previous'})
            write_report(destination, {'state': 'running', 'samples': 2})
            self.assertEqual(json.loads(destination.read_text()), {'state': 'running', 'samples': 2})
            self.assertFalse(destination.with_suffix('.tmp').exists())

    @unittest.skipUnless(sys.platform.startswith('linux') and hasattr(os, 'pidfd_open') and
                         hasattr(signal, 'pidfd_send_signal'), 'Linux Python with pidfd support required')
    def test_cleanup_reaps_stubborn_fixture_and_continues_after_desktop_error(self):
        import subprocess
        from active_cleanup import cleanup_owned
        from active_stability import ProcessWatch
        program = ('import signal,time\n'
                   'signal.signal(signal.SIGTERM, signal.SIG_IGN)\n'
                   'print("ready", flush=True)\n'
                   'time.sleep(30)\n')
        processes = [subprocess.Popen([sys.executable, '-u', '-c', program],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                     for _ in range(3)]
        class BrokenDesktop:
            closed = False
            def close_app(self, pid):
                raise OSError('synthetic close failure')
            def close(self):
                self.closed = True
        desktop = BrokenDesktop()
        watches = {}
        try:
            for process in processes:
                self.assertEqual(process.stdout.readline(), b'ready\n')
            watches['helper'] = ProcessWatch(processes[1].pid, 'helper')
            result = cleanup_owned(processes[0], processes[2], watches, desktop, grace=.05)
            self.assertTrue(result['forced'])
            self.assertEqual(result['forcedProcesses'], ['app', 'fixture', 'helper'])
            self.assertFalse(result['failed'])
            self.assertTrue(desktop.closed)
            self.assertIsNotNone(processes[0].returncode)
            self.assertEqual(processes[2].returncode, -9)
            self.assertEqual(processes[1].wait(timeout=1), -9)
            self.assertIsNone(watches['helper'].fd)
            self.assertTrue(processes[0].stdin.closed)
            self.assertTrue(processes[0].stdout.closed)
        finally:
            for watch in watches.values():
                watch.close()
            for process in processes:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=2)
                process.stdin.close()
                process.stdout.close()

    def test_cleanup_failure_does_not_skip_other_resources(self):
        from active_cleanup import cleanup_owned
        class BrokenFixture:
            stdin = stdout = None
            def poll(self):
                raise OSError('synthetic fixture failure')
        class Desktop:
            closed = False
            def close(self):
                self.closed = True
        desktop = Desktop()
        result = cleanup_owned(BrokenFixture(), None, {}, desktop, grace=.05)
        self.assertTrue(result['failed'])
        self.assertTrue(desktop.closed)

    def test_history_requires_new_id_and_exact_pair_even_at_capacity(self):
        import sqlite3
        from active_history import HistoryCheck
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / 'history.sqlite'
            with closing(sqlite3.connect(database)) as connection, connection:
                connection.execute('CREATE TABLE history(id INTEGER PRIMARY KEY, original TEXT, corrected TEXT, bytes INTEGER)')
                connection.execute('CREATE TABLE statistics(id INTEGER)')
                connection.executemany('INSERT INTO history VALUES(?,?,?,?)',
                                       [(i, 'before', 'after', 11) for i in range(1, 101)])
            check = HistoryCheck(database, Evidence())
            self.assertEqual(check.latest_id(), 100)
            self.assertFalse(check.inserted_after(100, 'before', 'after'))
            with closing(sqlite3.connect(database)) as connection, connection:
                connection.execute('DELETE FROM history WHERE id=1')
                connection.execute('INSERT INTO history VALUES(101,?,?,11)', ('before', 'wrong'))
            with self.assertRaises(WorkloadFailure):
                check.inserted_after(100, 'before', 'after')
            with closing(sqlite3.connect(database)) as connection, connection:
                connection.execute('UPDATE history SET corrected=? WHERE id=101', ('after',))
            self.assertTrue(HistoryCheck(database, Evidence()).inserted_after(100, 'before', 'after'))
            self.assertEqual(check.count(), 100)


if __name__ == '__main__':
    unittest.main()
