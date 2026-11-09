"""Exercise the native-host installer without using the user's browser data."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

host = Path(sys.argv[1]).resolve(strict=True)
name = "org.reword.browser"
origin = "chrome-extension://ehbepfpelbccljnbicecfceoaaepnamb/"


def run(binary, option, profile, success=True):
    result = subprocess.run([str(binary), option, str(profile)], capture_output=True, timeout=5)
    assert (result.returncode == 0) == success, (option, result.returncode, result.stderr)


with tempfile.TemporaryDirectory(prefix="reword-registration-") as temp:
    root = Path(temp)
    profile = root / "Browser data"
    profile.mkdir(mode=0o700)
    directory = profile / "NativeMessagingHosts"
    manifest = directory / (name + ".json")
    run(host, "--unregister-browser", profile)
    assert not directory.exists()
    run(host, "--register-browser", profile)
    installed = json.loads(manifest.read_bytes())
    assert installed["path"] == str(host)
    assert installed["name"] == name
    assert installed["type"] == "stdio"
    assert installed["allowed_origins"] == [origin]
    assert manifest.stat().st_mode & 0o777 == 0o600
    before = manifest.stat().st_ino
    run(host, "--register-browser", profile)
    assert manifest.stat().st_ino == before

    upgraded = root / "updated host"
    shutil.copy2(host, upgraded)
    run(upgraded, "--register-browser", profile)
    assert json.loads(manifest.read_bytes())["path"] == str(upgraded)

    # A different extension's manifest must survive both operations.
    foreign = dict(installed, allowed_origins=["chrome-extension://" + "a" * 32 + "/"])
    manifest.write_text(json.dumps(foreign), encoding="utf8")
    original = manifest.read_bytes()
    run(host, "--register-browser", profile, False)
    run(host, "--unregister-browser", profile, False)
    assert manifest.read_bytes() == original

    manifest.unlink()
    target = root / "unrelated"
    target.write_bytes(b"preserve")
    manifest.symlink_to(target)
    run(host, "--register-browser", profile, False)
    run(host, "--unregister-browser", profile, False)
    assert target.read_bytes() == b"preserve" and manifest.is_symlink()
    manifest.unlink()

    run(host, "--register-browser", profile)
    run(host, "--unregister-browser", profile)
    run(host, "--unregister-browser", profile)
    assert not manifest.exists() and target.read_bytes() == b"preserve"

    directory.rmdir()
    directory.symlink_to(root, target_is_directory=True)
    run(host, "--register-browser", profile, False)
    assert not (root / manifest.name).exists()
    shared = root / "Group writable"
    shared.mkdir(mode=0o700)
    shared.chmod(0o770)
    run(host, "--register-browser", shared, False)
    assert not (shared / "NativeMessagingHosts").exists()
    run(host, "--register-browser", root / "missing", False)
    assert not (root / "missing").exists()
