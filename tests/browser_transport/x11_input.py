"""Send a fixture chord only to the foreground browser on the owned X server."""
import argparse
import json
from pathlib import Path
import re
import sqlite3
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from active_desktop import OwnedDesktop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("pid", type=int)
    parser.add_argument("action", choices=("foreground", "undo", "custom-undo", "pause", "paused", "resumed"))
    args = parser.parse_args()
    desktop = OwnedDesktop(args.root)
    try:
        window = desktop.normal_window(args.pid)
        active = subprocess.check_output(["xprop", "-root", "_NET_ACTIVE_WINDOW"],
                                         text=True, timeout=2)
        match = re.search(r"window id # (0x[0-9a-f]+)", active)
        if not window or not match or int(match[1], 16) != window:
            return 1
        if args.action in ("paused", "resumed"):
            database = (args.root / "reword/reword.sqlite").as_uri() + "?mode=ro"
            with sqlite3.connect(database, uri=True, timeout=1) as store:
                row = store.execute("SELECT value FROM settings WHERE key='pauseUntil'").fetchone()
            expected = -1 if args.action == "paused" else 0
            return 0 if row and json.loads(row[0]) == [expected] else 1
        if args.action == "custom-undo":
            desktop._keys(["Control_L", "Alt_L", "k"], chord=True)
        elif args.action != "foreground":
            desktop.shortcut(args.action)
        return 0
    finally:
        desktop.close()


if __name__ == "__main__":
    raise SystemExit(main())
