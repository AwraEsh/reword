"""A deterministic lock provider, only for a private test D-Bus session."""
from pathlib import Path
import sys

from gi.repository import Gio, GLib

active = False
delay_next_reply = False
info = Gio.DBusNodeInfo.new_for_xml('''
<node><interface name="org.gnome.ScreenSaver">
  <method name="GetActive"><arg type="b" direction="out"/></method>
  <method name="SetActive"><arg type="b" direction="in"/></method>
  <method name="DelayNextReply"/>
</interface></node>
''').interfaces[0]


def method(connection, sender, path, interface, name, parameters, invocation):
    global active, delay_next_reply
    if name == 'GetActive':
        def reply():
            invocation.return_value(GLib.Variant('(b)', (active,)))
            return GLib.SOURCE_REMOVE

        if delay_next_reply:
            delay_next_reply = False
            GLib.timeout_add(70, reply)
        else:
            reply()
    elif name == 'SetActive':
        active = parameters.unpack()[0]
        invocation.return_value(GLib.Variant('()', ()))
    elif name == 'DelayNextReply':
        delay_next_reply = True
        invocation.return_value(GLib.Variant('()', ()))


def connected(connection, name):
    connection.register_object('/org/gnome/ScreenSaver', info, method, None, None)


def acquired(connection, name):
    Path(sys.argv[1]).touch()


if __name__ == '__main__':
    Gio.bus_own_name(Gio.BusType.SESSION, 'org.gnome.ScreenSaver',
                     Gio.BusNameOwnerFlags.NONE, connected, acquired, None)
    GLib.MainLoop().run()
