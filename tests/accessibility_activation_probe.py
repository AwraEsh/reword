"""Private-session regression: starting the helper activates desktop discovery."""
import os
from pathlib import Path
import select
import subprocess
import sys


def status(method, *arguments):
    return subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.a11y.Bus',
                           '--object-path', '/org/a11y/bus', '--method',
                           'org.freedesktop.DBus.Properties.' + method,
                           'org.a11y.Status', *arguments], check=True,
                          capture_output=True, text=True, timeout=5).stdout.strip()


def main():
    status('Set', 'IsEnabled', '<false>')
    reader_before = status('Get', 'ScreenReaderEnabled')
    assert status('Get', 'IsEnabled') == '(<false>,)'
    helper = subprocess.Popen([str(Path(sys.argv[1]).resolve())],
                              env=dict(os.environ, DISPLAY='', XDG_SESSION_TYPE='x11'),
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL)
    try:
        helper.stdin.write(b'{"operation":"description"}\n')
        helper.stdin.flush()
        assert select.select([helper.stdout], [], [], 5)[0], 'Helper did not become ready'
        assert helper.stdout.readline(1024), 'Helper exited without a response'
        assert status('Get', 'IsEnabled') == '(<true>,)', 'Desktop discovery remains disabled'
        assert status('Get', 'ScreenReaderEnabled') == reader_before, 'Screen reader changed'
        print('PASS: desktop discovery activated; screen-reader setting preserved')
    finally:
        helper.stdin.close()
        try:
            helper.wait(timeout=3)
        except subprocess.TimeoutExpired:
            helper.kill()
            helper.wait(timeout=3)
        helper.stdout.close()


if __name__ == '__main__':
    main()
