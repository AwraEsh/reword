"""Run desktop tests without sharing the user's accessibility sockets/settings."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='reword-session-') as directory:
    runtime = Path(directory) / 'runtime'
    runtime.mkdir(mode=0o700)
    environment = dict(os.environ, XDG_RUNTIME_DIR=str(runtime),
                       GSETTINGS_BACKEND='memory', DISPLAY='')
    environment.pop('AT_SPI_BUS_ADDRESS', None)
    result = subprocess.run(['dbus-run-session', '--', *sys.argv[1:]], env=environment)
    sys.exit(result.returncode)
