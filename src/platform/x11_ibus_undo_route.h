#pragma once

#include <QElapsedTimer>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace reword {
inline constexpr char IBusUndoRouteProperty[] = "_REWORD_IBUS_UNDO_ROUTE_V1";
inline constexpr quint32 IBusUndoRouteLifetimeMs = 600;

inline quint32 x11RouteClock() {
    QElapsedTimer clock;
    clock.start();
    return quint32(clock.msecsSinceReference());
}

inline unsigned long x11RootValue(Display *display, Atom property, Atom expected) {
    if (property == None)
        return 0;
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display, DefaultRootWindow(display), property, 0, 1,
                                          False, expected, &type, &format, &count, &remaining, &data);
    const auto result = status == Success && type == expected && format == 32 && count == 1 && data
                            ? *reinterpret_cast<unsigned long *>(data) : 0;
    if (data)
        XFree(data);
    return result;
}

inline bool x11IBusOwnsUndo(Display *display, Atom route, Atom activeWindow) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display, DefaultRootWindow(display), route, 0, 2,
                                          False, XA_CARDINAL, &type, &format, &count, &remaining, &data);
    bool current = false;
    if (status == Success && type == XA_CARDINAL && format == 32 && count == 2 && !remaining && data) {
        const auto *values = reinterpret_cast<const unsigned long *>(data);
        current = values[0] && x11RouteClock() - quint32(values[1]) < IBusUndoRouteLifetimeMs &&
                  values[0] == x11RootValue(display, activeWindow, XA_WINDOW);
    }
    if (data)
        XFree(data);
    return current;
}
} // namespace reword
