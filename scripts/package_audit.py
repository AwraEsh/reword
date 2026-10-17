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


def debian_host_dependencies(binary):
    """Resolve host-addon packages using the build host's Debian symbols metadata."""
    from pathlib import Path
    import tempfile

    binary = Path(binary).resolve()
    with tempfile.TemporaryDirectory(prefix="reword-shlibs-") as directory:
        control = Path(directory) / "debian/control"
        control.parent.mkdir()
        control.write_text(
            "Source: reword\nSection: utils\nPriority: optional\n"
            "Maintainer: Reword contributors\n\nPackage: reword\nArchitecture: any\n"
            "Description: Reword host-addon dependency audit\n")
        result = subprocess.run(["dpkg-shlibdeps", "-O", "-e" + str(binary)],
                                cwd=directory, capture_output=True, text=True,
                                check=True, env=dict(os.environ, LC_ALL="C"))
    fields = [line.removeprefix("shlibs:Depends=").strip()
              for line in result.stdout.splitlines() if line.startswith("shlibs:Depends=")]
    if len(fields) != 1 or not fields[0]:
        raise RuntimeError("Missing Debian dependency metadata for the Fcitx addon.")
    dependencies = [item.strip() for item in fields[0].split(",")]
    if any(not item for item in dependencies):
        raise RuntimeError("Malformed Debian dependency metadata for the Fcitx addon.")
    return dependencies


def debian_dependencies(glibc, host_dependencies):
    """Keep the stronger ELF or Debian-symbols libc floor without duplicates."""
    other = []
    for dependency in host_dependencies:
        if dependency == "libc6":
            continue
        if dependency.startswith("libc6 "):
            match = re.fullmatch(r"libc6 \(>= ([^()\s]+)\)", dependency)
            if not match:
                raise RuntimeError("Unsupported libc constraint in Debian dependency metadata.")
            version = match.group(1)
            comparison = subprocess.run(["dpkg", "--compare-versions", version, "gt", glibc],
                                        env=dict(os.environ, LC_ALL="C"))
            if comparison.returncode not in (0, 1):
                raise RuntimeError("Cannot compare Debian libc dependency versions.")
            if comparison.returncode == 0:
                glibc = version
        else:
            other.append(dependency)
    return list(dict.fromkeys([f"libc6 (>= {glibc})", "libgl1", "libegl1"] + other))
