"""Bounded synthetic fixture pipe I/O; no clipboard or desktop access."""
import os
import select
import time

from stability_evidence import FixtureProtocol, WorkloadFailure, wait_until


class FixtureClient:
    def __init__(self, process, tick=None, cancelled=None):
        self.process = process
        self.protocol = FixtureProtocol()
        self.last_snapshot_characters = None
        self.tick = tick or self.pump
        self.cancelled = cancelled
        os.set_blocking(process.stdin.fileno(), False)
        os.set_blocking(process.stdout.fileno(), False)

    def pump(self):
        if self.process.poll() is not None:
            raise WorkloadFailure('fixture-process-exit')
        for _ in range(64):
            try:
                block = os.read(self.process.stdout.fileno(), 4096)
            except BlockingIOError:
                return
            if not block:
                raise WorkloadFailure('fixture-process-exit')
            self.protocol.feed(block)

    def command(self, command):
        if not isinstance(command, str) or '\n' in command or '\r' in command or '\0' in command:
            raise WorkloadFailure('fixture-protocol')
        data = (command + '\n').encode('utf-8')
        if len(data) > 200000:
            raise WorkloadFailure('fixture-protocol')
        position, deadline = 0, time.monotonic() + 2
        while position < len(data):
            if self.cancelled and self.cancelled():
                raise WorkloadFailure('cancelled')
            self.tick()
            if time.monotonic() > deadline:
                raise WorkloadFailure('operation-timeout')
            if select.select([], [self.process.stdin], [], .02)[1]:
                try:
                    position += os.write(self.process.stdin.fileno(), data[position:])
                except BlockingIOError:
                    continue
                except BrokenPipeError:
                    raise WorkloadFailure('fixture-process-exit') from None

    def snapshot(self, timeout=1):
        self.command('snapshot')
        def ready():
            result = self.protocol.take_snapshot()
            return (result,) if result is not None else None
        result = wait_until(ready, timeout=timeout, tick=self.tick, cancelled=self.cancelled)[0]
        self.last_snapshot_characters = len(result)
        return result

    def wait_text(self, expected, timeout=10):
        wait_until(lambda: self.snapshot() == expected, timeout=timeout,
                   tick=self.tick, cancelled=self.cancelled)
