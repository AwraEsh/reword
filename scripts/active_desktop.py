"""X11 input restricted to the private desktop owned by the workload runner."""
import ctypes as C
import json
import os
from pathlib import Path
import re
import stat
import sys

from process_metrics import sample_process
from stability_evidence import WorkloadFailure


class _ClientData(C.Union):
    _fields_ = [('b', C.c_char * 20), ('s', C.c_short * 10), ('l', C.c_long * 5)]


class _ClientMessage(C.Structure):
    _fields_ = [('type', C.c_int), ('serial', C.c_ulong), ('send_event', C.c_int),
                ('display', C.c_void_p), ('window', C.c_ulong), ('message_type', C.c_ulong),
                ('format', C.c_int), ('data', _ClientData)]


class _Event(C.Union):
    _fields_ = [('client', _ClientMessage), ('padding', C.c_long * 24)]


class OwnedDesktop:
    def __init__(self, directory):
        self.display = None
        self.directory = Path(directory)
        try:
            if sys.platform != 'linux' or self.directory.is_symlink():
                raise ValueError()
            info = self.directory.stat()
            context = self.directory / 'active-context.json'
            if (info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077 or
                    context.is_symlink() or context.stat().st_uid != os.getuid() or
                    stat.S_IMODE(context.stat().st_mode) & 0o077):
                raise ValueError()
            with context.open('rb') as stream:
                data = stream.read(16385)
            if len(data) > 16384:
                raise ValueError()
            self.context = json.loads(data)
            for key in ['DISPLAY', 'XAUTHORITY', 'DBUS_SESSION_BUS_ADDRESS', 'XDG_RUNTIME_DIR']:
                if not self.context[key] or self.context[key] != os.environ.get(key):
                    raise ValueError()
            if (not re.fullmatch(r':[0-9]{4,5}', self.context['DISPLAY']) or
                    Path(self.context['XDG_RUNTIME_DIR']) != self.directory / 'runtime' or
                    Path(self.context['XAUTHORITY']) != self.directory / 'Xauthority'):
                raise ValueError()
            authority = Path(self.context['XAUTHORITY'])
            if authority.is_symlink() or authority.stat().st_uid != os.getuid():
                raise ValueError()
            self.check_alive()
        except (OSError, ValueError, KeyError, TypeError):
            raise WorkloadFailure('test-infrastructure') from None
        self.x = C.CDLL('libX11.so.6')
        self.xtest = C.CDLL('libXtst.so.6')
        self._api('XOpenDisplay', C.c_void_p, [C.c_char_p])
        self._api('XCloseDisplay', C.c_int, [C.c_void_p])
        self._api('XDefaultRootWindow', C.c_ulong, [C.c_void_p])
        self._api('XStringToKeysym', C.c_ulong, [C.c_char_p])
        self._api('XKeysymToKeycode', C.c_ubyte, [C.c_void_p, C.c_ulong])
        self._api('XSync', C.c_int, [C.c_void_p, C.c_int])
        self._api('XInternAtom', C.c_ulong, [C.c_void_p, C.c_char_p, C.c_int])
        self._api('XQueryTree', C.c_int, [C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong),
                                        C.POINTER(C.c_ulong), C.POINTER(C.POINTER(C.c_ulong)),
                                        C.POINTER(C.c_uint)])
        self._api('XGetWindowProperty', C.c_int, [C.c_void_p, C.c_ulong, C.c_ulong,
                 C.c_long, C.c_long, C.c_int, C.c_ulong, C.POINTER(C.c_ulong),
                 C.POINTER(C.c_int), C.POINTER(C.c_ulong), C.POINTER(C.c_ulong),
                 C.POINTER(C.POINTER(C.c_ubyte))])
        self._api('XSendEvent', C.c_int, [C.c_void_p, C.c_ulong, C.c_int, C.c_long,
                                        C.POINTER(_Event)])
        self._api('XFree', C.c_int, [C.c_void_p])
        self.xtest.XTestFakeKeyEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
        self.xtest.XTestFakeKeyEvent.restype = C.c_int
        self.x.XSetErrorHandler.argtypes = [C.c_void_p]
        self.x.XSetErrorHandler.restype = C.c_void_p
        callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
        self._error = False
        def on_error(*_):
            self._error = True
            return 0
        self._handler = callback_type(on_error)
        self._previous_handler = self.x.XSetErrorHandler(C.cast(self._handler, C.c_void_p))
        self.display = self.x.XOpenDisplay(self.context['DISPLAY'].encode('ascii'))
        if not self.display:
            self.x.XSetErrorHandler(self._previous_handler)
            raise WorkloadFailure('test-infrastructure')

    def _api(self, name, result, arguments):
        function = getattr(self.x, name)
        function.restype, function.argtypes = result, arguments

    def check_alive(self):
        if sample_process(self.context['xServerPid'])['startTicks'] != self.context['xServerStartTicks']:
            raise WorkloadFailure('test-infrastructure')

    def _keys(self, names, chord=False):
        self.check_alive()
        pressed = []
        try:
            for name in names:
                code = self.x.XKeysymToKeycode(self.display, self.x.XStringToKeysym(name.encode('ascii')))
                if not code or not self.xtest.XTestFakeKeyEvent(self.display, code, True, 0):
                    raise WorkloadFailure('test-infrastructure')
                pressed.append(code)
                if not chord:
                    if not self.xtest.XTestFakeKeyEvent(self.display, code, False, 0):
                        raise WorkloadFailure('test-infrastructure')
                    pressed.pop()
        finally:
            for code in reversed(pressed):
                self.xtest.XTestFakeKeyEvent(self.display, code, False, 0)
            self.x.XSync(self.display, False)
        if self._error:
            raise WorkloadFailure('test-infrastructure')

    def type_mistake(self):
        self._keys(['t', 'e', 'h', 'space'])

    def shortcut(self, action):
        choices = {'fix': ['Control_L', 'Alt_L', 'r'],
                   'undo': ['Control_L', 'Alt_L', 'z'],
                   'pause': ['Control_L', 'Alt_L', 'Shift_L', 'p']}
        if action not in choices:
            raise WorkloadFailure('test-infrastructure')
        self._keys(choices[action], chord=True)

    def normal_window(self, pid):
        self.check_alive()
        pending = [self.x.XDefaultRootWindow(self.display)]
        atom = self.x.XInternAtom(self.display, b'_NET_WM_PID', False)
        for _ in range(4096):
            if not pending:
                return False
            window = pending.pop()
            kind, form, count, rest = C.c_ulong(), C.c_int(), C.c_ulong(), C.c_ulong()
            data = C.POINTER(C.c_ubyte)()
            result = self.x.XGetWindowProperty(self.display, window, atom, 0, 1, False, 6,
                                              C.byref(kind), C.byref(form), C.byref(count),
                                              C.byref(rest), C.byref(data))
            try:
                matches = (result == 0 and kind.value == 6 and form.value == 32 and count.value == 1 and data and
                           C.cast(data, C.POINTER(C.c_ulong))[0] == pid)
            finally:
                if data:
                    self.x.XFree(data)
            if matches:
                # Ignore Qt's hidden menu/tooltip windows.
                type_atom = self.x.XInternAtom(self.display, b'_NET_WM_WINDOW_TYPE', False)
                normal = self.x.XInternAtom(self.display, b'_NET_WM_WINDOW_TYPE_NORMAL', False)
                data = C.POINTER(C.c_ubyte)()
                result = self.x.XGetWindowProperty(self.display, window, type_atom, 0, 16, False, 4,
                                                  C.byref(kind), C.byref(form), C.byref(count),
                                                  C.byref(rest), C.byref(data))
                try:
                    matches = (result == 0 and kind.value == 4 and form.value == 32 and
                               0 < count.value <= 16 and data and
                               normal in [C.cast(data, C.POINTER(C.c_ulong))[i]
                                          for i in range(count.value)])
                finally:
                    if data:
                        self.x.XFree(data)
            if matches:
                state_atom = self.x.XInternAtom(self.display, b'WM_STATE', False)
                data = C.POINTER(C.c_ubyte)()
                result = self.x.XGetWindowProperty(self.display, window, state_atom, 0, 2, False, state_atom,
                                                  C.byref(kind), C.byref(form), C.byref(count),
                                                  C.byref(rest), C.byref(data))
                try:
                    matches = (result == 0 and form.value == 32 and count.value >= 1 and data and
                               C.cast(data, C.POINTER(C.c_ulong))[0] == 1)
                finally:
                    if data:
                        self.x.XFree(data)
            if matches:
                return window
            root, parent, children, length = C.c_ulong(), C.c_ulong(), C.POINTER(C.c_ulong)(), C.c_uint()
            if self.x.XQueryTree(self.display, window, C.byref(root), C.byref(parent),
                                 C.byref(children), C.byref(length)):
                try:
                    if len(pending) + length.value > 4096:
                        raise WorkloadFailure('test-infrastructure')
                    pending.extend(children[index] for index in range(length.value))
                finally:
                    if children:
                        self.x.XFree(children)
        raise WorkloadFailure('test-infrastructure')

    def close_app(self, pid):
        window = self.normal_window(pid)
        if window:
            event = _Event()
            event.client.type, event.client.display = 33, self.display
            event.client.window = window
            event.client.message_type = self.x.XInternAtom(self.display, b'WM_PROTOCOLS', False)
            event.client.format = 32
            event.client.data.l[0] = self.x.XInternAtom(self.display, b'WM_DELETE_WINDOW', False)
            sent = self.x.XSendEvent(self.display, window, False, 0, C.byref(event))
            self.x.XSync(self.display, False)
            return bool(sent)
        return False

    def close(self):
        if self.display:
            self.x.XCloseDisplay(self.display)
            self.x.XSetErrorHandler(self._previous_handler)
            self.display = None
