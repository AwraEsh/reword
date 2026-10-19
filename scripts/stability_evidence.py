#!/usr/bin/env python3
"""Bounded evidence utilities for the owned packaged-GUI stability workload.

This module alone does not run or establish product stability acceptance.
Reports contain fixed scenario IDs and numeric summaries, never text snapshots.
"""
import base64
import binascii
from collections import deque
import json
import math
import os
from pathlib import Path
import tempfile
import select
import signal
import time

from process_metrics import ProcessSeries, sample_process

REQUIRED_CHECKS = frozenset({
    'live-first', 'live-second', 'live-third', 'live-undo', 'selected-fix',
    'selected-undo', 'pause', 'protected', 'oversized-selection', 'history-pruned',
    'app-sampled', 'helper-sampled', 'duration-complete', 'selection-limit-control',
})
COUNTERS = REQUIRED_CHECKS | {'history-insert', 'cycles'}
FAILURES = frozenset({
    'native-text-mismatch', 'app-process-exit', 'helper-process-exit',
    'history-bound', 'operation-timeout', 'cancelled', 'metrics-unavailable',
    'latency-budget', 'missing-evidence', 'fixture-protocol', 'fixture-process-exit',
    'resource-budget', 'shutdown-forced', 'test-infrastructure',
})


class WorkloadFailure(RuntimeError):
    def __init__(self, reason):
        self.reason = reason if reason in FAILURES else 'test-infrastructure'
        super().__init__(self.reason)


class FixtureProtocol:
    MAX_FRAME = 262144

    def __init__(self):
        self._pending = b''
        self._changes = deque(maxlen=512)
        self._sequence = self._timestamp = 0
        self._snapshot = None

    @property
    def changes(self):
        return list(self._changes)

    def feed(self, data):
        if not isinstance(data, bytes):
            raise WorkloadFailure('fixture-protocol')
        self._pending += data
        if len(self._pending) > self.MAX_FRAME:
            raise WorkloadFailure('fixture-protocol')
        while b'\n' in self._pending:
            line, self._pending = self._pending.split(b'\n', 1)
            if line.startswith(b'CHANGE:'):
                parts = line.split(b':')
                if (len(parts) != 3 or
                        any(not value.isdigit() or len(value) > 20 for value in parts[1:])):
                    raise WorkloadFailure('fixture-protocol')
                sequence, timestamp = map(int, parts[1:])
                if (sequence != self._sequence + 1 or timestamp <= 0 or
                        timestamp < self._timestamp or timestamp >= 2**63):
                    raise WorkloadFailure('fixture-protocol')
                self._sequence, self._timestamp = sequence, timestamp
                self._changes.append((sequence, timestamp))
            elif line.startswith(b'DATA:'):
                if self._snapshot is not None:
                    raise WorkloadFailure('fixture-protocol')
                try:
                    self._snapshot = base64.b64decode(line[5:], validate=True).decode('utf-8')
                except (ValueError, UnicodeError, binascii.Error):
                    raise WorkloadFailure('fixture-protocol') from None
            else:
                raise WorkloadFailure('fixture-protocol')

    def take_snapshot(self):
        snapshot, self._snapshot = self._snapshot, None
        return snapshot

    def finish(self):
        if self._pending:
            raise WorkloadFailure('fixture-protocol')


class Evidence:
    """Exact counts with conservative 0.1ms percentile bounds in constant memory."""
    def __init__(self):
        self.state = 'running'
        self.failure = None
        self.counts = {}
        self._bins = [0] * 100001
        self._samples = self._max = 0

    def fail(self, reason):
        failure = WorkloadFailure(reason)
        self.state = 'cancelled' if failure.reason == 'cancelled' else 'failed'
        self.failure = failure.reason

    def reject(self, reason):
        self.fail(reason)
        raise WorkloadFailure(reason)

    def counter(self, name, amount=1):
        if name not in COUNTERS or type(amount) is not int or amount <= 0:
            self.reject('test-infrastructure')
        self.counts[name] = self.counts.get(name, 0) + amount

    def expect_text(self, expected, actual):
        if expected != actual:
            self.reject('native-text-mismatch')

    def history(self, count, byte_count):
        if (type(count) is not int or type(byte_count) is not int or count < 0 or
                byte_count < 0 or count > 100 or byte_count > 20 * 1024 * 1024):
            self.reject('history-bound')

    def latency(self, micros):
        if type(micros) is not int or not 0 <= micros <= 10000000:
            self.reject('test-infrastructure')
        self._bins[(micros + 99) // 100] += 1
        self._samples += 1
        self._max = max(self._max, micros)

    def _percentile(self, fraction):
        target = max(1, math.ceil(self._samples * fraction))
        cumulative = 0
        for index, count in enumerate(self._bins):
            cumulative += count
            if cumulative >= target:
                return index / 10
        return None

    def finish(self):
        if self.failure:
            raise WorkloadFailure(self.failure)
        if not self._samples or not REQUIRED_CHECKS.issubset(self.counts):
            self.reject('missing-evidence')
        if self._percentile(.95) >= 50:
            self.reject('latency-budget')
        self.state = 'passed'

    def report(self):
        return {'state': self.state, 'failure': self.failure, 'counters': dict(self.counts),
                'latency': {'samples': self._samples, 'p50Ms': self._percentile(.5),
                            'p95Ms': self._percentile(.95), 'maxMs': self._max / 1000,
                            'precisionMs': .1, 'conservativeUpperBounds': True}}


def write_report(destination, report):
    destination = Path(destination)
    data = (json.dumps(report, allow_nan=False, indent=2) + '\n').encode('utf-8')
    fd, temporary = tempfile.mkstemp(prefix=destination.name + '.', suffix='.tmp',
                                     dir=destination.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)


def wait_until(predicate, timeout=10, cancelled=None, tick=None):
    if not math.isfinite(timeout) or not 0 < timeout <= 30:
        raise WorkloadFailure('test-infrastructure')
    deadline = time.monotonic() + timeout
    while True:
        if cancelled and cancelled():
            raise WorkloadFailure('cancelled')
        if tick:
            tick()
        result = predicate()
        if result:
            return result
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise WorkloadFailure('operation-timeout')
        time.sleep(min(.01, remaining))


class ProcessWatch:
    """Pin one owned process with a Linux pidfd; never signal a reused PID."""
    def __init__(self, pid, role):
        self.fd = None
        if role not in {'app', 'helper'} or type(pid) is not int or pid <= 0:
            raise WorkloadFailure('test-infrastructure')
        self.pid, self.role = pid, role
        self.series = ProcessSeries()
        try:
            self.fd = os.pidfd_open(pid)
            self.identity = sample_process(pid)['startTicks']
        except ProcessLookupError:
            self.close()
            raise WorkloadFailure(role + '-process-exit') from None
        except (OSError, ValueError, AttributeError):
            self.close()
            raise WorkloadFailure('metrics-unavailable') from None

    def _exited(self):
        if self.fd is None:
            raise WorkloadFailure('metrics-unavailable')
        return bool(select.select([self.fd], [], [], 0)[0])

    def sample(self, elapsed):
        if self._exited():
            raise WorkloadFailure(self.role + '-process-exit')
        try:
            counters = sample_process(self.pid)
        except FileNotFoundError:
            raise WorkloadFailure(self.role + '-process-exit') from None
        except (OSError, ValueError):
            raise WorkloadFailure('metrics-unavailable') from None
        if self._exited() or counters['startTicks'] != self.identity:
            raise WorkloadFailure(self.role + '-process-exit')
        try:
            return self.series.observe(counters, elapsed)
        except ValueError:
            raise WorkloadFailure('metrics-unavailable') from None

    def terminate(self):
        if not self._exited():
            try:
                signal.pidfd_send_signal(self.fd, signal.SIGTERM)
            except ProcessLookupError:
                pass

    def stop(self, grace=3):
        if self._exited():
            return False
        self.terminate()
        if not select.select([self.fd], [], [], grace)[0]:
            try:
                signal.pidfd_send_signal(self.fd, signal.SIGKILL)
            except ProcessLookupError:
                pass
            if not select.select([self.fd], [], [], grace)[0]:
                raise WorkloadFailure('shutdown-forced')
        return True

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
