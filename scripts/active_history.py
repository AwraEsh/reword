"""Read only the owned synthetic database; never put its text into reports."""
import sqlite3
from contextlib import closing

from stability_evidence import WorkloadFailure


class HistoryCheck:
    def __init__(self, database, evidence):
        self.database, self.evidence = database, evidence

    def _read(self):
        with closing(sqlite3.connect(f'file:{self.database}?mode=ro', uri=True, timeout=.5)) as connection:
            count, size = connection.execute('SELECT COUNT(*),COALESCE(SUM(bytes),0) FROM history').fetchone()
            self.evidence.history(count, size)
            if connection.execute('SELECT COUNT(*) FROM statistics').fetchone()[0]:
                raise WorkloadFailure('history-bound')
            row = connection.execute('SELECT id,original,corrected FROM history ORDER BY id DESC LIMIT 1').fetchone()
            return count, row

    def count(self):
        return self._read()[0]

    def latest_id(self):
        row = self._read()[1]
        return row[0] if row else 0

    def inserted_after(self, identifier, original, corrected):
        row = self._read()[1]
        if row is None or row[0] <= identifier:
            return False
        self.evidence.expect_text(original, row[1])
        self.evidence.expect_text(corrected, row[2])
        return True
