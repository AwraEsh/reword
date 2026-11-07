#!/usr/bin/python3
"""A fixed-response native messaging host used only by the browser fixture."""
import json
from pathlib import Path
import struct
import sys
import time


def main():
    directory = Path(__file__).resolve().parent
    origin = (directory / "expected-origin").read_text(encoding="ascii")
    if sys.argv[1:] != [origin]:
        return 2
    header = sys.stdin.buffer.read(4)
    if len(header) != 4:
        return 2
    size, = struct.unpack("=I", header)
    if not 0 < size <= 128:
        return 2
    body = sys.stdin.buffer.read(size)
    if len(body) != size:
        return 2
    request = json.loads(body)
    case = request.get("test") if isinstance(request, dict) else None
    if case not in ("plain", "multiline", "stale", "blur"):
        return 2
    with (directory / "host-cases").open("a", encoding="ascii") as log:
        log.write(case + "\n")
    if case in ("stale", "blur"):
        time.sleep(0.65)
    reply = b'{"corrected":"the"}'
    sys.stdout.buffer.write(struct.pack("=I", len(reply)) + reply)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
