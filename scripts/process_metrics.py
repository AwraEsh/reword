"""Content-free Linux process counters for controlled stability runs.

Only sample processes owned by the caller's test run. This module does not attach
to input, inspect command lines, or decide whether a product release is accepted.
CPU percent uses one logical CPU as 100 percent; child CPU is sampled separately.
"""
import math
import os
from pathlib import Path

MAX_READ_BYTES = 1024 * 1024


def read_bounded(path):
    with Path(path).open('rb') as stream:
        data = stream.read(MAX_READ_BYTES + 1)
    if len(data) > MAX_READ_BYTES:
        raise ValueError('Process metric exceeds the read limit')
    return data.decode('utf-8', errors='strict')


def unsigned(value):
    if not value.isascii() or not value.isdecimal():
        raise ValueError('Process metric is not an unsigned integer')
    return int(value)


def identity(path, pid):
    raw = read_bounded(path)
    begin, end = raw.find('('), raw.rfind(')')
    if begin < 0 or end <= begin or unsigned(raw[:begin].strip()) != pid:
        raise ValueError('Malformed process identity')
    fields = raw[end + 1:].split()
    if len(fields) < 20:
        raise ValueError('Process stat is incomplete')
    return unsigned(fields[19]), unsigned(fields[11]) + unsigned(fields[12])


def counters(path, required):
    found = {}
    for line in read_bounded(path).splitlines():
        key, separator, value = line.partition(':')
        if not separator or key not in required:
            continue
        parts = value.split()
        unit = required[key]
        if key in found or len(parts) != (2 if unit else 1) or (unit and parts[1] != unit):
            raise ValueError('Malformed process counter or unit')
        found[key] = unsigned(parts[0])
    if found.keys() != required.keys():
        raise ValueError('Required process counter is missing')
    return found


def sample_process(pid, proc_root=Path('/proc')):
    if type(pid) is not int or pid <= 0:
        raise ValueError('A positive process ID is required')
    process = Path(proc_root) / str(pid)
    start, first_cpu = identity(process / 'stat', pid)
    status = counters(process / 'status', {'VmRSS': 'kB', 'VmData': 'kB',
                                           'VmSwap': 'kB', 'Threads': ''})
    memory = counters(process / 'smaps_rollup', {'Pss': 'kB', 'Private_Clean': 'kB',
                                                'Private_Dirty': 'kB', 'Private_Hugetlb': 'kB'})
    descriptors = sum(1 for _ in (process / 'fd').iterdir())
    final_start, cpu = identity(process / 'stat', pid)
    if start != final_start:
        raise ValueError('Process identity changed during sampling')
    if cpu < first_cpu:
        raise ValueError('CPU counters ran backwards during sampling')
    if status['Threads'] == 0:
        raise ValueError('Sampled process has no live threads')
    return dict(pid=pid, startTicks=start, cpuTicks=cpu, rssKiB=status['VmRSS'],
                privateKiB=sum(memory[key] for key in ('Private_Clean', 'Private_Dirty', 'Private_Hugetlb')),
                pssKiB=memory['Pss'], dataKiB=status['VmData'], swapKiB=status['VmSwap'],
                threads=status['Threads'], descriptors=descriptors)


def read_child_pids(pid, proc_root=Path('/proc')):
    if type(pid) is not int or pid <= 0:
        raise ValueError('A positive process ID is required')
    process = Path(proc_root) / str(pid)
    start, _ = identity(process / 'stat', pid)
    children = set()
    for task in (process / 'task').iterdir():
        try:
            values = read_bounded(task / 'children').split()
        except FileNotFoundError:
            # Threads can exit between directory enumeration and the read.
            continue
        for value in values:
            child = unsigned(value)
            if child <= 0:
                raise ValueError('Child process ID must be positive')
            children.add(child)
    if identity(process / 'stat', pid)[0] != start:
        raise ValueError('Parent identity changed during child enumeration')
    return children


class ProcessSeries:
    def __init__(self, ticks_per_second=None):
        self.ticks = ticks_per_second if ticks_per_second is not None else os.sysconf('SC_CLK_TCK')
        if type(self.ticks) is not int or self.ticks <= 0:
            raise ValueError('A positive clock tick frequency is required')
        self.first = self.previous = None
        self.count = 0
        self.max_rss = self.max_private = 0

    def observe(self, sample, elapsed_seconds):
        if not math.isfinite(elapsed_seconds) or elapsed_seconds < 0:
            raise ValueError('Elapsed time must be finite and nonnegative')
        keys = {'pid', 'startTicks', 'cpuTicks', 'rssKiB', 'privateKiB', 'pssKiB',
                'dataKiB', 'swapKiB', 'threads', 'descriptors'}
        if sample.keys() != keys or any(type(v) is not int or v < 0 for v in sample.values()):
            raise ValueError('Process sample must contain complete unsigned counters')
        if sample['pid'] == 0 or sample['threads'] == 0:
            raise ValueError('A live process sample is required')
        if self.previous is not None:
            if any(sample[key] != self.previous[key] for key in ('pid', 'startTicks')):
                raise ValueError('Process identity changed in a continuous run')
            if elapsed_seconds <= self.previous_time or sample['cpuTicks'] < self.previous['cpuTicks']:
                raise ValueError('Time and CPU counters must not run backwards')
        if self.first is None:
            self.first, self.first_time = dict(sample), elapsed_seconds
        self.previous, self.previous_time = dict(sample), elapsed_seconds
        self.count += 1
        self.max_rss = max(self.max_rss, sample['rssKiB'])
        self.max_private = max(self.max_private, sample['privateKiB'])
        elapsed = elapsed_seconds - self.first_time
        cpu = (sample['cpuTicks'] - self.first['cpuTicks']) / self.ticks
        return {**sample, 'samples': self.count, 'elapsedSeconds': elapsed,
                'cpuSeconds': cpu, 'cpuPercent': 100 * cpu / elapsed if elapsed else 0,
                'rssGrowthKiB': sample['rssKiB'] - self.first['rssKiB'],
                'privateGrowthKiB': sample['privateKiB'] - self.first['privateKiB'],
                'maxRssKiB': self.max_rss, 'maxPrivateKiB': self.max_private}
