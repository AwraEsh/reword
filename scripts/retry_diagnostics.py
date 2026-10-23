"""Whitelist numeric retry events; discard every other byte from test stderr."""
from collections import deque
import re


class RetryDiagnostics:
    def __init__(self):
        self.pending = b''
        self.events = deque(maxlen=64)

    def feed(self, block):
        self.pending += block
        while b'\n' in self.pending:
            line, self.pending = self.pending.split(b'\n', 1)
            match = re.fullmatch(rb'RTRY ([0-9]{1,20}) ([0-9]{1,2}) (-?[0-9]{1,10})', line)
            if match:
                at, reason, start = map(int, match.groups())
                if at < 2**63 and 1 <= reason <= 15 and -1 <= start <= 50000:
                    self.events.append({'atMs': at, 'reason': reason, 'start': start})
        if len(self.pending) > 16384:
            self.pending = b''
