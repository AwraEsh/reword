from pathlib import Path
import json
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from retry_diagnostics import RetryDiagnostics


class RetryDiagnosticsTest(unittest.TestCase):
    def test_only_fixed_numeric_events_survive(self):
        diagnostic = RetryDiagnostics()
        diagnostic.feed(b'private passage\nRTRY 12 1 0\nRTRY 13 99 0\n')
        self.assertEqual(list(diagnostic.events), [{'atMs': 12, 'reason': 1, 'start': 0}])
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


if __name__ == '__main__':
    unittest.main()
