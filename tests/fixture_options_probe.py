#!/usr/bin/env python3
"""Reject invalid synthetic GTK fixture options before touching a desktop."""
import os
from pathlib import Path
import subprocess
import sys

fixture = Path(sys.argv[1]).resolve(strict=True)
base = dict(os.environ, DISPLAY='', WAYLAND_DISPLAY='', GDK_BACKEND='x11',
            DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent/reword-fixture-options')
for key, values, message in [
    ('REWORD_FIXTURE_LIFETIME_SECONDS', ['', '0', '-1', '18001', '1.5', '5x', ' 3', '999999999999'],
     b'Invalid synthetic fixture lifetime'),
    ('REWORD_FIXTURE_CHANGE_TIMING', ['', 'true', '2', '-1'],
     b'Invalid synthetic fixture timing option'),
]:
    for value in values:
        environment = dict(base)
        environment.pop('REWORD_FIXTURE_LIFETIME_SECONDS', None)
        environment.pop('REWORD_FIXTURE_CHANGE_TIMING', None)
        environment[key] = value
        result = subprocess.run([str(fixture)], env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=3)
        if result.returncode != 2 or message not in result.stderr:
            raise RuntimeError('Invalid fixture option did not fail before desktop initialization')
print('PASS: invalid fixture options are rejected before desktop initialization.')
