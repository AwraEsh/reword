#!/usr/bin/env python3
"""Exercise the two IBus editing mechanisms with isolated real X11 clients."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import time

from active_desktop import OwnedDesktop
from active_fixture import FixtureClient
from stability_evidence import WorkloadFailure

ROOT = Path(__file__).resolve().parents[1]


def wait_for(check, seconds):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if check():
            return True
        time.sleep(.08)
    return False


def xprop(*args):
    return subprocess.run(["xprop", *args], capture_output=True, text=True,
                          timeout=2, check=True).stdout


def active_window():
    found = re.search(r"0x[0-9a-f]+", xprop("-root", "_NET_ACTIVE_WINDOW"))
    return int(found[0], 16) if found else 0


def active_process(desktop, process):
    try:
        return active_window() == desktop.normal_window(process.pid)
    except WorkloadFailure:
        return False


def click_editor(desktop, window):
    info = subprocess.run(["xwininfo", "-id", str(window)], capture_output=True, text=True,
                          timeout=8, check=True).stdout
    x, y = (int(re.search(rf"Absolute upper-left {axis}: +(-?[0-9]+)", info)[1])
            for axis in ("X", "Y"))
    desktop.check_alive()
    desktop.xtest.XTestFakeMotionEvent.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_ulong]
    desktop.xtest.XTestFakeButtonEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
    desktop.xtest.XTestFakeMotionEvent(desktop.display, -1, x + 145, y + 110, 0)
    desktop.xtest.XTestFakeButtonEvent(desktop.display, 1, True, 0)
    desktop.xtest.XTestFakeButtonEvent(desktop.display, 1, False, 0)
    desktop.x.XSync(desktop.display, False)


def type_typo(desktop):
    for key in ("t", "e", "h", "space"):
        desktop._keys([key])
        time.sleep(.13)


def browser_state():
    window = active_window()
    if not window:
        return ""
    try:
        title = xprop("-id", str(window), "_NET_WM_NAME")
    except subprocess.CalledProcessError:
        return ""  # The window may have closed while focus was changing.
    found = re.search(r"\brw-(plain|password)-(empty|original|corrected)\b", title)
    return found[0] if found else ""

def saved_correction(document):
    try:
        text = document.read_text(encoding="utf-8")
    except FileNotFoundError:
        return False
    # The editor may treat the final newline as part of the file format and
    # place the cursor before it. Removing the inserted word must recover
    # the exact original document.
    return (text.count("the ") == 1 and text.replace("the ", "", 1) == "anchor!\n"
            or text in ("anchor!\nthe\n", "anchor!\nthe \n"))

def input_method_target(helper):
    helper.stdin.write(b'{"operation":"input-method-target"}\n')
    helper.stdin.flush()
    if not select.select([helper.stdout], [], [], 3)[0]:
        raise RuntimeError("Native helper did not answer the metadata request")
    return json.loads(helper.stdout.readline(8192))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--owned-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    root = args.owned_directory
    desktop = OwnedDesktop(root)
    try:
        return run_probe(desktop, root, args.output)
    finally:
        desktop.close()


def run_probe(desktop, root, output):
    output.mkdir(mode=0o700)
    address = f"unix:path={root}/runtime/reword-ibus"
    env = dict(os.environ, IBUS_ADDRESS=address, GTK_IM_MODULE="ibus", QT_IM_MODULE="ibus",
               IBUS_ENABLE_SYNC_MODE="1", GSK_RENDERER="cairo")
    children = []
    result = {}

    def spawn(argv, tag, **kwargs):
        with (output / (tag + ".log")).open("wb") as log:
            process = subprocess.Popen(argv, env=env, stderr=log, **kwargs)
        children.append(process)
        return process

    def stop(child):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=4)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=4)

    try:
        helper = spawn([str(ROOT / "build/reword-native-helper")], "native-helper",
                       stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        spawn(["ibus-daemon", "--single", "--panel=disable", "--emoji-extension=disable",
               "--config=disable", "--address=" + address], "ibus-daemon")
        if not wait_for(lambda: (root / "runtime/reword-ibus").is_socket(), 5):
            raise RuntimeError("Private IBus socket did not appear")
        spawn([sys.executable, str(ROOT / "tests/ibus_probe_engine.py"), str(root)], "engine")
        if not wait_for(lambda: (root / "ibus-engine-ready").is_file(), 5):
            raise RuntimeError("Private test engine did not register")
        subprocess.run(["ibus", "engine", "reword-owned-probe"], env=env, timeout=5, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        result["stage"] = "gtk-control"
        control = spawn([str(ROOT / "build/reword_fixture")], "gtk-control",
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        if not wait_for(lambda: active_process(desktop, control), 8):
            raise RuntimeError("GTK control did not receive focus")
        client = FixtureClient(control)
        client.command("text:")
        client.wait_text("", 3)
        time.sleep(.2)
        result["single_line_witness_excluded"] = not input_method_target(helper).get("window")
        type_typo(desktop)
        result["gtk_control"] = client.snapshot() == "the "
        client.command("password")
        client.command("text:")
        client.wait_text("", 3)
        result["password_witness_excluded"] = not input_method_target(helper).get("window")
        client.command("quit")
        control.wait(timeout=3)

        result["stage"] = "gnome-editor"
        document = root / "owned-editor.txt"
        document.write_text("anchor!\n", encoding="utf-8")
        editor = spawn(["gnome-text-editor", "--standalone", str(document)], "editor")
        result["editor_active"] = wait_for(lambda: active_process(desktop, editor), 15)
        if result["editor_active"]:
            time.sleep(1.2)
            result["stage"] = "gnome-editor-click"
            window = desktop.normal_window(editor.pid)
            click_editor(desktop, window)
            time.sleep(.25)
            def matches_editor():
                target = input_method_target(helper)
                return (Path(target.get("app", "")).name == "gnome-text-editor"
                        and bool(target.get("field")) and target.get("window") == window)
            result["editor_witness"] = wait_for(matches_editor, 4)
            desktop._keys(["Control_L", "End"], chord=True)
            time.sleep(.2)
            type_typo(desktop)
            desktop._keys(["Control_L", "s"], chord=True)
            result["editor_range_edit"] = wait_for(lambda: saved_correction(document), 5)
            value = document.read_text(encoding="utf-8")
            result["editor_saved"] = {
                "characters": len(value), "reference_preserved": saved_correction(document),
                "corrected_word": "the" in value, "original_word": "teh" in value,
            }
        stop(editor)

        result["stage"] = "helium"
        page = root / "owned-browser.html"
        page.write_text('''<!doctype html><meta charset="utf-8"><title>Owned test</title>
<label>Plain <input id="plain" autofocus spellcheck="false" autocomplete="off"></label>
<label>Password <input id="password" type="password" autocomplete="off"></label>
<script>
setInterval(() => {
  const field = document.activeElement;
  const state = {"":"empty", "teh ":"original", "the ":"corrected"}[field.value] || "other";
  document.title = "rw-" + field.id + "-" + state;
}, 40);
</script>''', encoding="utf-8")
        spawn(["helium", "--user-data-dir=" + str(root / "helium-profile"), "--no-first-run",
               "--no-default-browser-check", "--disable-background-networking", "--disable-sync",
               "--no-proxy-server", "--password-store=basic", "--ozone-platform=x11",
               "--host-resolver-rules=MAP * ~NOTFOUND", page.as_uri()], "helium")
        result["browser_focused"] = wait_for(lambda: browser_state() == "rw-plain-empty", 16)
        if result["browser_focused"]:
            result["old_editor_witness_excluded"] = not input_method_target(helper).get("window")
            type_typo(desktop)
            result["browser_preedit"] = wait_for(lambda: browser_state() == "rw-plain-corrected", 3)
            desktop._keys(["Tab"])
            if wait_for(lambda: browser_state() == "rw-password-empty", 3):
                type_typo(desktop)
                result["browser_password_unchanged"] = wait_for(
                    lambda: browser_state() == "rw-password-original", 3)
    except (OSError, RuntimeError, subprocess.SubprocessError, TypeError, ValueError) as error:
        result["failure"] = type(error).__name__
    finally:
        metrics = root / "ibus-stats"
        if metrics.is_file():
            result["engine"] = json.loads(metrics.read_text(encoding="utf-8"))
        for child in reversed(children):
            stop(child)
        (output / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    passed = all(result.get(key) for key in
                 ("gtk_control", "editor_range_edit", "browser_preedit", "browser_password_unchanged",
                  "single_line_witness_excluded", "password_witness_excluded", "editor_witness",
                  "old_editor_witness_excluded"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
