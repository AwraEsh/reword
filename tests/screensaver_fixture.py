#!/usr/bin/env python3
"""Private-D-Bus screensaver fixture for native policy revocation tests."""
import json
from pathlib import Path
import sqlite3
import sys
from gi.repository import Gio, GLib

folder = Path(sys.argv[1])
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
reply = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
                      'org.freedesktop.DBus', 'RequestName',
                      GLib.Variant('(su)', ('org.gnome.ScreenSaver', 4)),
                      None, Gio.DBusCallFlags.NONE, 1000, None)
if reply.unpack()[0] != 1:
    raise RuntimeError('Private screensaver name is already owned')
xml = '''<node><interface name="org.gnome.ScreenSaver">
<method name="GetActive"><arg type="b" direction="out"/></method>
</interface></node>'''
info = Gio.DBusNodeInfo.new_for_xml(xml)


def active(connection, sender, path, interface, method, parameters, invocation):
    arm = folder / 'policy-arm.json'
    if arm.exists():
        policy = json.loads(arm.read_text())
        if policy.get('operationOnly') and not (folder / 'reword/operation.lock').exists():
            invocation.return_value(GLib.Variant('(b)', (False,)))
            return
        policy['remaining'] -= 1
        if policy['remaining']:
            arm.write_text(json.dumps(policy))
        else:
            arm.unlink()
            at_injection = (folder / "fixture-state").read_text()
            database = sqlite3.connect(folder / 'reword/reword.sqlite')
            with database:
                if policy['change'] == 'deny':
                    database.execute("UPDATE applications SET decision='deny' WHERE id=?", (policy['app'],))
                else:
                    database.execute("INSERT OR REPLACE INTO settings(key,value) VALUES('pauseUntil','[-1]')")
            database.close()
            (folder / 'policy-fired').write_text(at_injection)
    invocation.return_value(GLib.Variant('(b)', (False,)))


bus.register_object('/org/gnome/ScreenSaver', info.interfaces[0], active, None, None)
(folder / 'screensaver-ready').touch()
GLib.MainLoop().run()
