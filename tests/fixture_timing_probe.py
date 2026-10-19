#!/usr/bin/env python3
"""Check the GTK fixture watchdog and content-free monotonic change records.

Run only from the owned desktop in scripts/test-native-linux.py.
"""
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time


def main():
    fixture = Path(sys.argv[1]).resolve(strict=True)
    environment = dict(os.environ, REWORD_FIXTURE_LIFETIME_SECONDS='3',
                       REWORD_FIXTURE_CHANGE_TIMING='1')
    started = time.monotonic_ns() // 1000
    process = subprocess.Popen([str(fixture)], env=environment, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        process.stdin.write(b'text:first synthetic value\ntext:second synthetic value\n')
        process.stdin.flush()
        records = []
        pending = b''
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + 10
            while process.poll() is None or selector.get_map():
                if time.monotonic() > deadline:
                    raise RuntimeError(f'Synthetic watchdog timeout (exit={process.poll()}, streams={len(selector.get_map())})')
                for key, _ in selector.select(.1):
                    block = os.read(key.fd, 4096)
                    if not block:
                        selector.unregister(key.fileobj)
                        continue
                    pending += block
                    if len(pending) > 65536:
                        raise RuntimeError('Fixture timing protocol exceeded its bound')
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        parts = line.split(b':')
                        if (len(parts) != 3 or parts[0] != b'CHANGE' or
                                not all(value.isdigit() for value in parts[1:])):
                            raise RuntimeError('Malformed content-free timing record')
                        records.append(tuple(map(int, parts[1:])))
                        if len(records) > 8:
                            raise RuntimeError('Unexpected number of synthetic changes')
        if pending:
            raise RuntimeError('Incomplete fixture timing record at process exit')
        ended = time.monotonic_ns() // 1000
        if process.wait(timeout=1) != 0 or not 2 <= (ended - started) / 1e6 <= 10:
            raise RuntimeError('Synthetic fixture lifetime was not bounded as requested')
        if len(records) != 2 or [value[0] for value in records] != [1, 2]:
            raise RuntimeError('Text changes did not produce sequential timing records')
        if not started <= records[0][1] <= records[1][1] <= ended:
            raise RuntimeError('Fixture timing does not share the monotonic test clock')
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        process.stdin.close()
        process.stdout.close()
    quiet = subprocess.run([str(fixture)], input=b'text:synthetic quiet value\n',
                           env=dict(environment, REWORD_FIXTURE_LIFETIME_SECONDS='1',
                                    REWORD_FIXTURE_CHANGE_TIMING='0'),
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=8)
    if quiet.returncode or quiet.stdout:
        raise RuntimeError('Disabled timing must not emit change records')
    print('PASS: fixture watchdog, two monotonic change records and quiet mode.')


if __name__ == '__main__':
    main()
