from pathlib import Path
import json
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from retry_diagnostics import RetryDiagnostics


class RetryDiagnosticsTest(unittest.TestCase):
    def test_only_fixed_numeric_events_survive(self):
        diagnostic = RetryDiagnostics()
        diagnostic.feed(b'private passage\nRTRY 12 1 0\nRTRY 13 99 0\nRTRY 14 22 -1\n')
        self.assertEqual(list(diagnostic.events), [{'atMs': 12, 'reason': 1, 'start': 0},
                                                  {'atMs': 14, 'reason': 22, 'start': -1}])
        self.assertNotIn('private', json.dumps(list(diagnostic.events)))

    def test_fragmentation_and_event_memory_are_bounded(self):
        diagnostic = RetryDiagnostics()
        diagnostic.feed(b'RTRY 1 ')
        diagnostic.feed(b'2 0\n')
        for number in range(100):
            diagnostic.feed(f'RTRY {number} 3 -1\n'.encode())
        self.assertEqual(len(diagnostic.events), 64)
        diagnostic.feed(b'x' * 20000)
        self.assertEqual(diagnostic.pending, b'')

    def test_native_write_and_confirmation_have_a_bounded_numeric_range(self):
        diagnostic = RetryDiagnostics()
        diagnostic.feed(b'RTRY 12 23 -1\nRTRY 13 33 -1\nRTRY 14 53 0\n'
                        b'RTRY 15 54 -1\nRTRY 16 46 private\n')
        self.assertEqual([event['reason'] for event in diagnostic.events], [23, 33, 53])


if __name__ == '__main__':
    unittest.main()
