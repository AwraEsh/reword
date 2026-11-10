"""Fixed-word experiment for the private X11 probe. Never install this engine."""
import json
import os
from pathlib import Path
import sys

import gi
gi.require_version("IBus", "1.0")
from gi.repository import GLib, IBus


root = Path(sys.argv[1]).resolve(strict=True)
runtime = root / "runtime"
if (not root.name.startswith("reword-native-")
        or not (root / "active-context.json").is_file()
        or os.environ.get("XDG_RUNTIME_DIR") != str(runtime)
        or os.environ.get("IBUS_ADDRESS") != f"unix:path={runtime}/reword-ibus"):
    raise SystemExit("Private IBus test context is required")

IBus.init()
stats = dict(keys=0, surrounding=0, range_edits=0, preedit_edits=0, purpose_events=0)


class Probe(IBus.Engine):
    __gtype_name__ = "RewordOwnedIBusProbe"

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.capabilities = 0
        self.clear()

    def clear(self):
        self.preceding_typo = False
        self.composition = ""

    def sensitive(self):
        purpose, hints = self.get_content_type()
        return (purpose in (IBus.InputPurpose.PASSWORD, IBus.InputPurpose.PIN)
                or hints & int(IBus.InputHints.PRIVATE))

    def do_focus_in(self):
        self.clear()
        if not self.sensitive():
            self.get_surrounding_text()

    def do_enable(self):
        if not self.sensitive():
            self.get_surrounding_text()

    def do_focus_out(self):
        self.clear()

    def do_reset(self):
        self.clear()

    def do_set_capabilities(self, capabilities):
        self.capabilities = int(capabilities)

    def do_set_content_type(self, purpose, hints):
        stats["purpose_events"] += 1
        if (purpose in (IBus.InputPurpose.PASSWORD, IBus.InputPurpose.PIN)
                or hints & int(IBus.InputHints.PRIVATE)):
            self.clear()

    def do_set_surrounding_text(self, text, cursor, anchor):
        stats["surrounding"] += 1
        self.preceding_typo = False
        if not self.sensitive() and cursor == anchor and cursor >= 3:
            value = text.get_text()
            self.preceding_typo = (value[cursor - 3:cursor] == "teh"
                                   and (cursor == 3 or not value[cursor - 4].isalpha()))

    def do_process_key_event(self, keyval, keycode, state):
        if state & int(IBus.ModifierType.RELEASE_MASK):
            return False
        stats["keys"] += 1
        if self.sensitive():
            return False
        modified = state & int(IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.MOD1_MASK)
        if self.capabilities & int(IBus.Capabilite.SURROUNDING_TEXT):
            if not modified and keyval == IBus.KEY_space and self.preceding_typo:
                self.delete_surrounding_text(-3, 3)
                self.commit_text(IBus.Text.new_from_string("the "))
                self.clear()
                stats["range_edits"] += 1
                return True
            return False
        if not self.capabilities & int(IBus.Capabilite.PREEDIT_TEXT):
            return False
        # The isolated Helium page has only two fixed synthetic fields. This
        # experiment can measure composition even when no purpose is reported.
        if not modified and keyval in (IBus.KEY_t, IBus.KEY_e, IBus.KEY_h):
            if len(self.composition) < 8:
                self.composition += IBus.keyval_to_unicode(keyval)
                self.update_preedit_text_with_mode(
                    IBus.Text.new_from_string(self.composition), len(self.composition),
                    True, IBus.PreeditFocusMode.COMMIT)
                return True
        if self.composition:
            original = self.composition
            self.clear()
            self.update_preedit_text_with_mode(
                IBus.Text.new_from_string(""), 0, False, IBus.PreeditFocusMode.COMMIT)
            if not modified and keyval == IBus.KEY_space and original == "teh":
                self.commit_text(IBus.Text.new_from_string("the "))
                stats["preedit_edits"] += 1
                return True
            self.commit_text(IBus.Text.new_from_string(original))
        return False


bus = IBus.Bus.new()
if not bus.is_connected():
    raise SystemExit("Private IBus bus is unavailable")
factory = IBus.Factory.new(bus.get_connection())
factory.add_engine("reword-owned-probe", Probe)
component = IBus.Component.new("org.freedesktop.IBus.RewordOwnedProbe",
                               "Owned diagnostic", "1", "", "Reword", "", "", "")
component.add_engine(IBus.EngineDesc.new("reword-owned-probe", "Owned test", "",
                                         "en", "", "Reword", "", "us"))
if not bus.register_component(component):
    raise SystemExit("Could not register the test engine")
(root / "ibus-engine-ready").touch()


def publish():
    partial = root / "ibus-stats.tmp"
    partial.write_text(json.dumps(stats), encoding="utf-8")
    partial.replace(root / "ibus-stats")
    return GLib.SOURCE_CONTINUE


GLib.timeout_add(80, publish)
GLib.MainLoop().run()
