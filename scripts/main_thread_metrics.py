"""Bounded main-thread CPU diagnostics for an already pinned owned process."""
import math
import os
from pathlib import Path

from process_metrics import identity


class MainThreadSeries:
    def __init__(self, ticks_per_second=None):
        self.ticks = os.sysconf('SC_CLK_TCK') if ticks_per_second is None else ticks_per_second
        if type(self.ticks) is not int or self.ticks <= 0:
            raise ValueError('A positive clock tick frequency is required')
        self.first = self.previous = None
        self.count = 0

    def sample(self, pid, expected_start, elapsed, proc_root=Path('/proc')):
        if (type(pid) is not int or pid <= 0 or type(expected_start) is not int or
                expected_start < 0 or not math.isfinite(elapsed) or elapsed < 0):
            raise ValueError('Invalid main-thread identity or elapsed time')
        start, cpu = identity(Path(proc_root) / str(pid) / 'task' / str(pid) / 'stat', pid)
        if start != expected_start:
            raise ValueError('Main-thread identity differs from the pinned process')
        if self.previous:
            previous_pid, previous_start, previous_cpu, previous_elapsed = self.previous
            if ((pid, start) != (previous_pid, previous_start) or
                    cpu < previous_cpu or elapsed < previous_elapsed):
                raise ValueError('Main-thread identity or counters changed unexpectedly')
        current = (pid, start, cpu, elapsed)
        if self.first is None:
            self.first = current
        self.previous = current
        self.count += 1
        duration = elapsed - self.first[3]
        seconds = (cpu - self.first[2]) / self.ticks
        return {'pid': pid, 'startTicks': start, 'cpuTicks': cpu,
                'samples': self.count, 'elapsedSeconds': duration,
                'cpuSeconds': seconds, 'cpuPercent': 100 * seconds / duration if duration else 0.0}
