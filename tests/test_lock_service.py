"""A deterministic lock provider, only for a private test D-Bus session."""
import gi
from gi.repository import Gio, GLib
state={'active':False}
xml='''<node><interface name="org.gnome.ScreenSaver"><method name="GetActive"><arg type="b" direction="out"/></method><method name="SetActive"><arg type="b" direction="in"/></method></interface></node>'''
info=Gio.DBusNodeInfo.new_for_xml(xml).interfaces[0]
def method(connection,sender,path,interface,name,parameters,invocation):
 if name=='GetActive':invocation.return_value(GLib.Variant('(b)',(state['active'],)))
 elif name=='SetActive':state['active']=parameters.unpack()[0];invocation.return_value(GLib.Variant('()',()))
 else:invocation.return_dbus_error('org.freedesktop.DBus.Error.UnknownMethod','Unknown test method')
def acquired(connection,name):connection.register_object('/org/gnome/ScreenSaver',info,method,None,None)
if __name__=='__main__':
 Gio.bus_own_name(Gio.BusType.SESSION,'org.gnome.ScreenSaver',Gio.BusNameOwnerFlags.NONE,acquired,None,None)
 GLib.MainLoop().run()
