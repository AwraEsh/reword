#!/usr/bin/env python3
"""Run native GTK probes on a private X server and private accessibility bus.

The lock service is a controlled fixture, not proof of a physical lock screen.
No host application or display is read or modified.
"""
import argparse
import json
from contextlib import nullcontext
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def detach_test_mounts(directory):
    for relative in ("runtime/doc", "runtime/gvfs"):
        mount = Path(directory) / relative
        if os.path.ismount(mount):
            tool = shutil.which("fusermount3") or shutil.which("fusermount")
            if not tool:
                raise RuntimeError("Cannot detach the private test FUSE mount.")
            subprocess.run([tool, "-uz", str(mount)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--directory", help=argparse.SUPPRESS)
    parser.add_argument("--fixture-timing-only", action="store_true",
                        help="Check only the synthetic watchdog/timing protocol")
    parser.add_argument("--active-app", help="Exercise this packaged AppDir/AppRun in the owned desktop")
    parser.add_argument("--active-seconds", type=int, default=120)
    parser.add_argument("--active-output", help="New directory for content-free workload reports")
    args = parser.parse_args()
    if args.active_app:
        if args.fixture_timing_only or not args.active_output or not 5 <= args.active_seconds <= 14400:
            parser.error("Active workload needs a new report directory and 5–14400 seconds.")
        args.active_app = str(Path(args.active_app).resolve(strict=True))
        args.active_output = str(Path(args.active_output).resolve())
    if not args.inside:
        # Own the directory outside D-Bus, so activated services have stopped
        # before any private FUSE mount or temporary file is removed.
        with tempfile.TemporaryDirectory(prefix="reword-native-") as directory:
            try:
                command = [
                    "dbus-run-session", "--", sys.executable, str(Path(__file__).resolve()),
                    "--inside", "--directory", directory,
                ]
                if args.fixture_timing_only:
                    command.append("--fixture-timing-only")
                if args.active_app:
                    command.extend(["--active-app", args.active_app, "--active-seconds",
                                    str(args.active_seconds), "--active-output", args.active_output])
                result = subprocess.run(command)
            finally:
                detach_test_mounts(directory)
            return result.returncode
    if not args.directory:
        raise RuntimeError("The private session needs its caller-owned temporary directory.")
    server = shutil.which("Xvfb") or str(ROOT / ".deps/xvfb/usr/bin/Xvfb")
    if not Path(server).exists():
        raise RuntimeError("Install Xvfb to run the controlled native probes.")
    with nullcontext(args.directory) as directory:
        folder = Path(directory)
        (folder / "runtime").mkdir(mode=0o700)
        number = 4000 + secrets.randbelow(16000)
        while Path(f"/tmp/.X{number}-lock").exists():
            number += 1
        display = f":{number}"
        authority = folder / "Xauthority"
        subprocess.run(["xauth", "-f", str(authority), "add", display, ".", secrets.token_hex(16)],
                       check=True, stderr=subprocess.DEVNULL)
        env = dict(os.environ, DISPLAY=display, XAUTHORITY=str(authority),
                   XDG_RUNTIME_DIR=str(folder / "runtime"), XDG_SESSION_TYPE="x11",
                   XDG_CONFIG_HOME=str(folder / "config"), XDG_CACHE_HOME=str(folder / "cache"),
                   XDG_DATA_HOME=str(folder / "data"), GTK_IM_MODULE="gtk-im-context-simple",
                   GSETTINGS_BACKEND="memory", GIO_USE_VFS="local", GIO_USE_VOLUME_MONITOR="unix",
                   GVFS_DISABLE_FUSE="1", GDK_BACKEND="x11")
        env.pop("AT_SPI_BUS_ADDRESS", None)
        env.pop("WAYLAND_DISPLAY", None)
        from gi.repository import Gio, GLib
        activation_keys = (
            "DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_CACHE_HOME",
            "XDG_DATA_HOME", "XDG_SESSION_TYPE", "GDK_BACKEND", "GSETTINGS_BACKEND",
            "GIO_USE_VFS", "GIO_USE_VOLUME_MONITOR", "GVFS_DISABLE_FUSE",
        )
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "UpdateActivationEnvironment",
                      GLib.Variant("(a{ss})", ({key: env[key] for key in activation_keys},)),
                      None, Gio.DBusCallFlags.NONE, 2000, None)
        children = []
        logs = []

        def spawn(command, name):
            log = (folder / (name + ".log")).open("wb")
            logs.append(log)
            child = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            children.append(child)
            return child

        try:
            xserver = spawn([server, display, "-screen", "0", "1280x800x24", "-nolisten", "tcp",
                             "-auth", str(authority)], "xvfb")
            deadline = time.monotonic() + 5
            while not Path(f"/tmp/.X11-unix/X{number}").exists():
                if xserver.poll() is not None:
                    raise RuntimeError("Private X server failed to start.")
                if time.monotonic() > deadline:
                    raise RuntimeError("Private X server did not become ready.")
                time.sleep(.05)
            manager = shutil.which("openbox")
            if not manager:
                raise RuntimeError("Install Openbox to run the controlled desktop probes.")
            config = folder / "openbox.xml"
            config.write_text('<openbox_config xmlns="http://openbox.org/3.4/rc">'
                              '<focus><focusNew>yes</focusNew></focus><keyboard/><mouse/>'
                              '</openbox_config>')
            wm = spawn([manager, "--sm-disable", "--config-file", str(config)], "openbox")
            deadline = time.monotonic() + 5
            while True:
                if wm.poll() is not None:
                    raise RuntimeError("Private window manager exited during startup.")
                check = subprocess.run(["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
                if check.returncode == 0 and "window id #" in check.stdout:
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError("Private window manager did not become ready.")
                time.sleep(.05)
            bus.call_sync("org.a11y.Bus", "/org/a11y/bus", "org.freedesktop.DBus.Properties", "Set",
                          GLib.Variant("(ssv)", ("org.a11y.Status", "IsEnabled", GLib.Variant("b", True))),
                          None, Gio.DBusCallFlags.NONE, 3000, None)
            spawn([sys.executable, str(ROOT / "tests/test_lock_service.py")], "lock")
            for _ in range(40):
                ready = subprocess.run([
                    "gdbus", "call", "--session", "--dest", "org.gnome.ScreenSaver",
                    "--object-path", "/org/gnome/ScreenSaver",
                    "--method", "org.gnome.ScreenSaver.GetActive",
                ], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if ready.returncode == 0:
                    break
                time.sleep(.05)
            else:
                raise RuntimeError("Controlled lock service failed to start.")
            subprocess.run([sys.executable, str(ROOT / "tests/fixture_timing_probe.py"),
                            str(ROOT / "build/reword_fixture")], env=env, timeout=25, check=True)
            if args.fixture_timing_only:
                return 0
            if args.active_app:
                from process_metrics import sample_process
                context = {key: env[key] for key in
                           ("DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR")}
                context.update(xServerPid=xserver.pid,
                               xServerStartTicks=sample_process(xserver.pid)["startTicks"])
                context_file = folder / "active-context.json"
                with context_file.open("x", encoding="utf-8") as stream:
                    json.dump(context, stream)
                context_file.chmod(0o600)
                driver = spawn([sys.executable, str(ROOT / "scripts/active_stability.py"),
                                "--owned-directory", str(folder), "--app", args.active_app,
                                "--fixture", str(ROOT / "build/reword_fixture"),
                                "--seconds", str(args.active_seconds),
                                "--output", args.active_output], "active-workload")
                try:
                    result = driver.wait(timeout=args.active_seconds + 1860)
                except BaseException:
                    driver.terminate()
                    try:
                        driver.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        driver.kill()
                        driver.wait()
                    raise
                print("PASS: active packaged workload." if result == 0 else
                      "FAIL: active packaged workload; inspect the content-free report.", flush=True)
                return result
            repeat = int(os.environ.get("REWORD_NATIVE_REPEAT", "3"))
            if not 1 <= repeat <= 100:
                raise RuntimeError("Native probe repeat count must be between 1 and 100.")
            for index in range(repeat):
                for probe in ("reword_native_probe", "reword_native_service_probe"):
                    result = subprocess.run([str(ROOT / "build" / probe)], env=env, timeout=40)
                    if result.returncode:
                        raise RuntimeError(f"{probe} failed on run {index + 1}.")
            print(f"PASS: {repeat} controlled native probe cycles.", flush=True)
        finally:
            for child in reversed(children):
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            for log in logs:
                log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
