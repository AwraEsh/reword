"""Content-free observations of the controlled fixture's live retry attempts."""
from pathlib import Path
import json
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from active_fixture import live_observation


class LiveDiagnosticTest(unittest.TestCase):
    def test_classifies_fixed_fixture_literals_and_counts_only_current_changes(self):
        self.assertEqual(live_observation('teh ', [(9, 100), (10, 110), (11, 120)], 9),
                         {'characters': 4, 'fixtureState': 'original', 'changeCount': 2,
                          'recentChanges': [[10, 110], [11, 120]]})
        self.assertEqual(live_observation('the ', [], 0)['fixtureState'], 'corrected')
        self.assertEqual(live_observation('', [], 0)['fixtureState'], 'empty')
        self.assertEqual(live_observation('reset ', [], 0)['fixtureState'], 'reset')

    def test_unknown_content_is_never_returned_and_numeric_event_tail_is_bounded(self):
        result = live_observation('do not retain this content',
                                  [(n, n * 10) for n in range(1, 101)], 0)
        self.assertEqual(result['characters'], 26)
        self.assertEqual(result['fixtureState'], 'other')
        self.assertEqual(result['changeCount'], 100)
        self.assertEqual(len(result['recentChanges']), 12)
        self.assertEqual(result['recentChanges'][0], [89, 890])
        self.assertNotIn('do not retain', json.dumps(result))


if __name__ == '__main__':
    unittest.main()
