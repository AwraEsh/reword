"""Change policy in the compiled host's owned fixture database."""
import json
from pathlib import Path
import sqlite3
import sys

directory, app, *change = sys.argv[1:]
directory = Path(directory)
directory.mkdir(mode=0o700, exist_ok=True)
with sqlite3.connect(directory / "reword.sqlite") as database:
    database.execute("CREATE TABLE IF NOT EXISTS applications (id TEXT PRIMARY KEY, "
                     "name TEXT NOT NULL, decision TEXT NOT NULL, remind TEXT)")
    database.execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)")

    def set_value(key, value):
        database.execute("INSERT OR REPLACE INTO settings VALUES (?, ?)",
                         (key, json.dumps([value])))

    key, value = change if change else ("rule", "allow")
    if key == "rule" and value in ("allow", "shortcut", "deny"):
        database.execute("INSERT OR REPLACE INTO applications VALUES (?, ?, ?, NULL)",
                         (app, Path(app).name, value))
    elif key == "rule" and value == "remove":
        database.execute("DELETE FROM applications WHERE id=?", (app,))
    elif key in ("english", "persian", "browsers") and value in ("true", "false"):
        set_value(key, value == "true")
    elif key == "pauseUntil" and value in ("-1", "0"):
        set_value(key, int(value))
    else:
        raise SystemExit("Unknown browser fixture policy change")
    if key != "pauseUntil":
        row = database.execute("SELECT value FROM settings WHERE key='configRevision'").fetchone()
        set_value("configRevision", (json.loads(row[0])[0] if row else 0) + 1)
