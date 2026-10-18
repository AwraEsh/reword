#!/usr/bin/env python3
"""Exercise real Fcitx input in a private headless GNOME Wayland session.

Requires GNOME Shell, Fcitx5, Python GI, and the matching GTK/Qt Fcitx5 modules.
Only the compositor launched here receives injected keys. No host display,
clipboard, application text, or desktop settings are used.
"""
import argparse
import base64
import hashlib
import json
import socket
import struct
from contextlib import nullcontext
import os
from pathlib import Path
import selectors
import shutil
import sqlite3
import subprocess
import sys
import sysconfig
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SERVICE = "org.gnome.Mutter.RemoteDesktop"
MANAGER = "/org/gnome/Mutter/RemoteDesktop"
SESSION = SERVICE + ".Session"


def wait_for(predicate, description, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.05)
    raise RuntimeError(description)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy-races", action="store_true")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--fixture", choices=("qt", "gtk"), default="qt")
    parser.add_argument("--directory", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        with tempfile.TemporaryDirectory(prefix="reword-wayland-") as temporary:
            result = subprocess.run([
                "dbus-run-session", "--", sys.executable, str(Path(__file__).resolve()),
                "--inside", "--fixture", args.fixture, "--directory", temporary,
                *(["--policy-races"] if args.policy_races else []),
            ])
            # Portals may leave private FUSE mounts after their D-Bus session ends.
            # Detach only mountpoints inside the directory created by this caller.
            for relative in ("runtime/doc", "runtime/gvfs"):
                mount = Path(temporary) / relative
                if os.path.ismount(mount):
                    tool = shutil.which("fusermount3") or shutil.which("fusermount")
                    if not tool:
                        raise RuntimeError("Cannot detach the private test FUSE mount.")
                    subprocess.run([tool, "-uz", str(mount)], check=True,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return result.returncode
    if not args.directory:
        raise RuntimeError("The private session needs its caller-owned temporary directory.")

    from gi.repository import Gio, GLib
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def call(destination, path, interface, method, signature=None, values=(), timeout=3000):
        parameters = GLib.Variant(signature, values) if signature else None
        result = bus.call_sync(destination, path, interface, method, parameters, None,
                               Gio.DBusCallFlags.NONE, timeout, None)
        return result.unpack() if result else ()

    def owned():
        return call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                    "org.freedesktop.DBus", "NameHasOwner", "(s)", (SERVICE,))[0]

    with nullcontext(args.directory) as temporary:
        folder = Path(temporary)
        for name in ("runtime", "config", "cache", "data"):
            (folder / name).mkdir(mode=0o700)
        env = dict(os.environ)
        env.update({
            "XDG_RUNTIME_DIR": str(folder / "runtime"),
            "XDG_CONFIG_HOME": str(folder / "config"),
            "XDG_CACHE_HOME": str(folder / "cache"),
            "XDG_DATA_HOME": str(folder / "data"),
            "XDG_SESSION_TYPE": "wayland", "DISPLAY": "",
            "WAYLAND_DISPLAY": "reword-wayland", "GDK_BACKEND": "wayland",
            "GSETTINGS_BACKEND": "memory", "LIBGL_ALWAYS_SOFTWARE": "1",
            "GIO_USE_VFS": "local", "GIO_USE_VOLUME_MONITOR": "unix",
            "GVFS_DISABLE_FUSE": "1",
            "QT_QPA_PLATFORM": "wayland", "QT_QUICK_BACKEND": "software",
            "QT_QPA_PLATFORMTHEME": "", "QT_NO_XDG_DESKTOP_PORTAL": "1",
            "GTK_USE_PORTAL": "0", "GTK_IM_MODULE": "fcitx5", "QT_IM_MODULE": "fcitx",
            "REWORD_DATA_DIR": str(folder / "reword"),
            "REWORD_DICTIONARY_DIR": str(ROOT / "assets/dictionaries"),
            "REWORD_FIXTURE_MAXIMIZE": "1",
            **({"REWORD_FIXTURE_STATE": str(folder / "fixture-state")} if args.policy_races else {}),
        })
        env.pop("AT_SPI_BUS_ADDRESS", None)
        activation = {key: env[key] for key in (
            "DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME",
            "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_SESSION_TYPE", "GDK_BACKEND",
            "GSETTINGS_BACKEND", "GIO_USE_VFS", "GIO_USE_VOLUME_MONITOR", "GVFS_DISABLE_FUSE",
        )}
        call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
             "UpdateActivationEnvironment", "(a{ss})", (activation,))
        children, logs = [], []

        def spawn(command, name, stdin=None, stdout=None):
            log = (folder / (name + ".log")).open("wb")
            logs.append(log)
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdin=stdin,
                                       stdout=stdout if stdout is not None else log, stderr=log)
            children.append(process)
            return process

        try:
            if args.policy_races:
                spawn([sys.executable, str(ROOT / "tests/screensaver_fixture.py"), str(folder)],
                      "screensaver")
                wait_for(lambda: (folder / "screensaver-ready").exists(),
                         "Private screensaver fixture did not start.")
            shell = spawn([
                "gnome-shell", "--headless", "--wayland", "--no-x11",
                "--virtual-monitor", "1280x800", "--wayland-display", "reword-wayland",
                "--sm-disable",
            ], "gnome")
            wait_for(lambda: (folder / "runtime/reword-wayland").exists() and owned(),
                     "Private Wayland compositor did not become ready.", 20)
            owner_pid = call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                             "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                             "(s)", (SERVICE,))[0]
            if owner_pid != shell.pid:
                raise RuntimeError("Refusing input: remote-desktop owner is not our compositor.")
            # The compositor advertises its D-Bus name before shell startup is complete.
            # Keep normal calls short; allow this one-time startup request longer.
            session = call(SERVICE, MANAGER, SERVICE, "CreateSession", timeout=15000)[0]
            smoke = subprocess.run([str(ROOT / "scripts/env.sh"), str(ROOT / "build/reword"),
                                    "--smoke-test"], cwd=ROOT, env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
            if smoke.returncode:
                raise RuntimeError("Qt application failed to launch on private Wayland.")
            print("PASS: Qt application launch on GNOME Wayland.", flush=True)

            config = folder / "config/fcitx5"
            config.mkdir(parents=True)
            (config / "profile").write_text(
                "[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=keyboard-us\n\n"
                "[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n\n[GroupOrder]\n0=Default\n")
            addon = folder / "data/fcitx5/addon"
            addon.mkdir(parents=True)
            shutil.copyfile(ROOT / "packaging/reword-fcitx.conf", addon / "reword.conf")
            libdir = Path("/usr/lib") / sysconfig.get_config_var("MULTIARCH")
            env["FCITX_ADDON_DIRS"] = str(ROOT / "build") + ":" + str(libdir / "fcitx5")
            fixture_name = "reword_qt_fixture" if args.fixture == "qt" else "reword_fixture"
            database = sqlite3.connect(folder / "reword/reword.sqlite")
            with database:
                database.execute(
                    "INSERT OR REPLACE INTO applications(id,name,decision,remind) "
                    "VALUES(?,?,?,NULL)", ("fcitx:" + fixture_name, "Synthetic fixture", "allow"))
            spawn([str(ROOT / "scripts/env.sh"), "fcitx5", "--disable=all",
                   "--enable=keyboard,dbus,dbusfrontend,reword,wayland"], "fcitx")
            wait_for(lambda: call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                  "org.freedesktop.DBus", "NameHasOwner", "(s)",
                                  ("org.fcitx.Fcitx5",))[0], "Private Fcitx did not start.")
            def recovery_generation():
                identity = hashlib.sha256(str(folder / "reword").encode()).hexdigest()[:24]
                endpoint = Path(os.environ.get("TMPDIR", "/tmp")) / ("reword-recovery-" + identity)
                try:
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as channel:
                        channel.settimeout(1)
                        channel.connect(str(endpoint))
                        def read_exact(size):
                            result = b""
                            while len(result) < size:
                                part = channel.recv(size - len(result))
                                if not part:
                                    raise OSError("Private recovery connection closed.")
                                result += part
                            return result
                        channel.sendall(b"RWH1")
                        if read_exact(4) != b"RWR1":
                            raise OSError("Private recovery handshake was invalid.")
                        channel.sendall(b"RWS1")
                        status = read_exact(12)
                        if status[:4] != b"STAT":
                            raise OSError("Private recovery status was invalid.")
                        return struct.unpack("<Q", status[4:])[0]
                except OSError:
                    return None

            application = spawn([str(ROOT / "scripts/env.sh"), str(ROOT / "build/reword")], "application")
            time.sleep(.5)
            if application.poll() is not None:
                raise RuntimeError("The private Reword application exited during startup.")
            wait_for(lambda: recovery_generation() is not None, "The private recovery channel did not start.")
            plugins = str(ROOT / ".deps/usr/lib/x86_64-linux-gnu/qt6/plugins") + ":" + str(libdir / "qt6/plugins")
            fixture = spawn([str(ROOT / "scripts/env.sh"), "env", "QT_PLUGIN_PATH=" + plugins,
                             str(ROOT / "build" / fixture_name)], "fixture",
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            time.sleep(2)
            call(SERVICE, session, SESSION, "Start")

            def key_state(symbol, pressed):
                call(SERVICE, session, SESSION, "NotifyKeyboardKeysym", "(ub)", (symbol, pressed))

            def key(symbol):
                key_state(symbol, True)
                key_state(symbol, False)
                time.sleep(.08)

            # Close this compositor's startup overview and focus our maximized fixture.
            key(0xff1b)
            time.sleep(.7)
            for dx, dy in ((-10000.0, -10000.0), (600.0, 400.0)):
                call(SERVICE, session, SESSION, "NotifyPointerMotionRelative", "(dd)", (dx, dy))
            for pressed in (True, False):
                call(SERVICE, session, SESSION, "NotifyPointerButton", "(ib)", (272, pressed))
            time.sleep(1)
            selector = selectors.DefaultSelector()
            selector.register(fixture.stdout, selectors.EVENT_READ)

            def command(value):
                fixture.stdin.write(value.encode() + b"\n")
                fixture.stdin.flush()
                time.sleep(.2)

            def snapshot():
                fixture.stdin.write(b"snapshot\n")
                fixture.stdin.flush()
                if not selector.select(3):
                    raise RuntimeError("No synthetic fixture snapshot received.")
                line = fixture.stdout.readline()
                if not line.startswith(b"DATA:"):
                    raise RuntimeError("Invalid synthetic fixture response.")
                return base64.b64decode(line[5:].strip(), validate=True).decode()

            def expect(value):
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    actual = snapshot()
                    if actual == value:
                        return
                    time.sleep(.05)
                raise RuntimeError(f"Synthetic fixture mismatch: {actual[:128]!r} != {value[:128]!r}")

            def type_word():
                command("text:")
                for char in "teh ":
                    key(ord(char))
                time.sleep(.3)

            def chord(letter, shift=False, before_key=None):
                modifiers = (0xffe3, 0xffe9, 0xffe1) if shift else (0xffe3, 0xffe9)
                for symbol in modifiers:
                    key_state(symbol, True)
                if before_key:
                    time.sleep(.15)
                    before_key()
                key(ord(letter.upper() if shift else letter))
                for symbol in reversed(modifiers):
                    key_state(symbol, False)

            if args.policy_races:
                if args.fixture != "qt":
                    raise RuntimeError("Policy races require the Qt selection fixture.")
                for operation in ("selection", "undo"):
                    for change in ("pause", "deny"):
                        command("password"); command("plain")
                        with database:
                            database.execute("DELETE FROM settings WHERE key='pauseUntil'")
                            database.execute("UPDATE applications SET decision='allow' WHERE id=?",
                                             ("fcitx:" + fixture_name,))
                        command("text:teh"); command("select-all")
                        if operation == "undo":
                            chord("r"); expect("the")
                        time.sleep(.15)
                        fired = folder / "policy-fired"
                        fired.unlink(missing_ok=True)
                        def arm():
                            (folder / "policy-arm.json").write_text(json.dumps({
                                "remaining": 1 if operation == "selection" else 2,
                                "operationOnly": operation == "selection",
                                "change": change, "app": "fcitx:" + fixture_name,
                            }))
                        chord("r" if operation == "selection" else "z", before_key=arm)
                        wait_for(fired.exists, "Policy injection did not reach its lock query.")
                        time.sleep(.25)
                        expected = "teh" if operation == "selection" else "the"
                        if fired.read_text() != expected:
                            raise RuntimeError("Policy was injected after the target edit.")
                        expect(expected)
                        print(f"PASS: {operation} stopped after {change} during final lock query.", flush=True)
                return 0

            if args.fixture == "qt":
                command("reject-native"); type_word(); expect("teh ")
                fixture.stdin.write(b"rejected-events\n"); fixture.stdin.flush()
                if not selector.select(3):
                    raise RuntimeError("No rejected-event confirmation.")
                rejected = fixture.stdout.readline().decode().strip()
                if not rejected.startswith("REJECTED:") or int(rejected[9:]) < 1:
                    raise RuntimeError("Fixture did not reject an actual IME write.")
                # Keep an unconfirmed correction alive, then change only field capability.
                with database:
                    database.execute("INSERT OR REPLACE INTO settings VALUES('statistics','[true]')")
                before = database.execute("SELECT coalesce(sum(count),0) FROM statistics").fetchone()[0]
                command("password"); command("plain")
                # A late client snapshot matching the abandoned result must not confirm it.
                command("text:the ")
                time.sleep(.2)
                if database.execute("SELECT coalesce(sum(count),0) FROM statistics").fetchone()[0] != before:
                    raise RuntimeError("A correction invalidated by a password transition was counted.")
                if database.execute("SELECT count(*) FROM history").fetchone()[0]:
                    raise RuntimeError("An unconfirmed protected transition entered history.")
                with database:
                    database.execute("DELETE FROM settings WHERE key='statistics'")
                command("accept-native")
                print("PASS: password capability transition discards an unconfirmed correction.", flush=True)
            type_word(); expect("the ")
            if args.fixture == "qt":
                print("PASS: rejected native write does not consume the correction attempt.", flush=True)
            type_word(); expect("teh ")
            type_word(); expect("the ")
            print("PASS: first/second/third correction.", flush=True)
            for expected in ("teh ", "the "):
                for _ in range(4):
                    key(0xff08)  # Actual Backspace, preserving the same word location.
                expect("")
                for char in "teh ":
                    key(ord(char))
                expect(expected)
            print("PASS: Backspace rewriting preserves alternate attempts.", flush=True)
            chord("z"); expect("teh ")
            print("PASS: live Undo.", flush=True)
            key(0xff51)  # Left, then return to the same location.
            key(0xff53)
            type_word(); expect("the ")
            print("PASS: cursor movement resets the retry cycle.", flush=True)
            key(ord("x")); expect("the x")
            type_word(); expect("the ")
            print("PASS: continuing text resets the retry cycle.", flush=True)
            key(0xffff)  # No-op Delete at end must not exempt a later cursor reset.
            expect("the ")
            command("cursor:0"); command("cursor:4")
            type_word(); expect("the ")
            print("PASS: client cursor updates reset the retry cycle without key navigation.", flush=True)
            command("text:`foo teh")
            key(ord(" ")); expect("`foo teh ")
            print("PASS: code-span protection.", flush=True)
            chord("p", shift=True)
            wait_for(lambda: database.execute("SELECT value FROM settings WHERE key='pauseUntil'").fetchone() == ('[-1]',),
                     "Pause shortcut did not persist the paused state.")
            type_word(); expect("teh "); chord("p", shift=True)
            print("PASS: pause and resume.", flush=True)
            if recovery_generation() != 0:
                raise RuntimeError("Everyday word corrections entered passage recovery.")
            original = "im here with teh book"
            command("text:" + original); command("select-all"); chord("r")
            if args.fixture == "qt":
                expect("I am here with the book")
                wait_for(lambda: database.execute("SELECT count(*) FROM history").fetchone()[0] == 1,
                         "Confirmed passage history was not saved.")
                chord("z"); expect(original)
                print("PASS: selected correction, confirmed history and selected Undo.", flush=True)
                wait_for(lambda: (recovery_generation() or 0) > 0, "Confirmed passage did not reach Reword.")
                generation = recovery_generation()
                with database:
                    database.execute("INSERT OR REPLACE INTO settings VALUES('history','[false]')")
                second = "im here with teh book again"
                command("text:" + second); command("select-all"); chord("r")
                expect("I am here with the book again")
                wait_for(lambda: (recovery_generation() or 0) > generation,
                         "History-off passage did not reach the running application.")
                if database.execute("SELECT count(*) FROM history").fetchone()[0] != 1:
                    raise RuntimeError("History-off passage was persisted.")
                chord("z"); expect(second)
                with database:
                    database.execute("DELETE FROM settings WHERE key='history'")
                print("PASS: confirmed History-off recovery delivery to the actual app and Undo.", flush=True)
            else:
                expect(original)
                if database.execute("SELECT count(*) FROM history").fetchone()[0]:
                    raise RuntimeError("Unsupported GTK selection was recorded.")
                print("LIMIT: GTK3 does not expose the selection anchor to this input method.", flush=True)
            history_count = database.execute("SELECT count(*) FROM history").fetchone()[0]
            command("password"); type_word(); expect("teh ")
            chord("r")
            if database.execute("SELECT count(*) FROM history").fetchone()[0] != history_count:
                raise RuntimeError("Protected operation was recorded.")
            print("PASS: password exclusion.", flush=True)
            command("plain"); type_word(); expect("the ")
            time.sleep(31)
            type_word(); expect("the ")
            print("PASS: retry cycle expires after 30 seconds.", flush=True)
            chord("p", shift=True)
            wait_for(lambda: database.execute("SELECT value FROM settings WHERE key='pauseUntil'").fetchone() == ('[-1]',),
                     "Pause shortcut failed after the idle interval.")
            type_word(); expect("teh "); chord("p", shift=True)
            print("PASS: pause remains usable after the idle interval.", flush=True)
            # Only our two private windows exist. Alt+Tab returns to Reword; no tray
            # is provided by this compositor, so closing it invokes Controller::quit.
            key_state(0xffe9, True); key(0xff09); key_state(0xffe9, False)
            time.sleep(.5)
            key_state(0xffe9, True); key(0xffc1); key_state(0xffe9, False)  # Alt+F4
            wait_for(lambda: application.poll() is not None, "Reword did not quit from its private window.")
            if application.returncode:
                raise RuntimeError("The private Reword application did not exit cleanly.")
            time.sleep(.2)
            type_word(); expect("teh ")
            print("PASS: no addon correction after application Quit.", flush=True)
            application = spawn([str(ROOT / "scripts/env.sh"), str(ROOT / "build/reword")], "application-restart")
            wait_for(lambda: recovery_generation() is not None, "Reword recovery did not restart.")
            time.sleep(.5)
            key_state(0xffe9, True); key(0xff09); key_state(0xffe9, False)
            time.sleep(.3)
            type_word(); expect("the ")
            print("PASS: addon reconnects after Reword restarts.", flush=True)
            application.terminate()
            application.wait(timeout=5)
            time.sleep(.2)
            type_word(); expect("teh ")
            print("PASS: no addon correction after abrupt application exit.", flush=True)
            application = spawn([str(ROOT / "scripts/env.sh"), str(ROOT / "build/reword")], "application-after-crash")
            wait_for(lambda: recovery_generation() == 0, "Reword could not reclaim its channel after a crash.")
            print("PASS: restart after crash reclaims the private channel with empty session recovery.", flush=True)
            database.close()
            selector.close()
        finally:
            for process in reversed(children):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            for log in logs:
                log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
