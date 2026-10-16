"""Inspect local build artifacts before packaging. No runtime user data is read."""
import os
import re
import subprocess


def resolved_libraries(output):
    missing = re.findall(r"^\s*(\S+)\s+=>\s+not found\s*$", output, re.MULTILINE)
    if missing:
        raise RuntimeError("Unresolved runtime libraries: " + ", ".join(sorted(set(missing))))
    return re.findall(r"=> (/\S+)", output)


def dynamic_libraries(binary):
    output = subprocess.run(["ldd", str(binary)], capture_output=True, text=True,
                            check=True, env=dict(os.environ, LC_ALL="C")).stdout
    return resolved_libraries(output)


def is_elf(path):
    if not path.is_file():
        return False
    with path.open("rb") as source:
        return source.read(4) == b"\x7fELF"


def minimum_glibc(objects):
    versions = set()
    for binary in objects:
        output = subprocess.run(["readelf", "--version-info", str(binary)],
                                capture_output=True, text=True, check=True,
                                env=dict(os.environ, LC_ALL="C")).stdout
        needs = False
        for line in output.splitlines():
            if line.startswith("Version "):
                needs = line.startswith("Version needs section")
            if needs:
                for match in re.findall(r"Name: GLIBC_(\d+(?:\.\d+)+)\b", line):
                    versions.add(tuple(map(int, match.split("."))))
    if not versions:
        raise RuntimeError("No glibc version requirements found in staged ELF objects.")
    return ".".join(map(str, max(versions)))
