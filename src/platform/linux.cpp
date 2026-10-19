#include "adapter.h"
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QKeySequence>
#include <QDBusInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QQueue>
#undef signals
#include <atspi/atspi.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <unistd.h>
namespace reword {
template <class T> using GRef = std::unique_ptr<T, decltype(&g_object_unref)>;
class LinuxAdapter final : public Adapter {
    bool available_ = false;
    AtspiAccessible *focus_ = nullptr;
    AtspiEventListener *listener_ = nullptr;
    static inline Display *trapped_ = nullptr;
    static inline int trapError_ = 0;
    static inline XErrorHandler previousHandler_ = nullptr;
    static int errorHandler(Display *d, XErrorEvent *e) {
        if (d == trapped_) {
            trapError_ = e->error_code;
            return 0;
        }
        return previousHandler_ ? previousHandler_(d, e) : 0;
    }
    bool dirty_ = true;
    QElapsedTimer lastPoll_, lastScan_;
    int scanApplication_ = 0;
    Display *display_ = nullptr;
    QHash<int, QString> keys_;
    struct Shortcut {
        int key;
        QString action;
        bool released = false;
        QElapsedTimer releasedAt;
    };
    QQueue<Shortcut> pendingShortcuts_;
    bool wayland_ = false;
    static void event(AtspiEvent *e, void *data) {
        auto self = static_cast<LinuxAdapter *>(data);
        self->dirty_ = true;
        if (QString::fromUtf8(e->type).startsWith("object:state-changed:focused") && e->detail1) {
            if (self->focus_)
                g_object_unref(self->focus_);
            self->focus_ = ATSPI_ACCESSIBLE(g_object_ref(e->source));
        }
        g_boxed_free(ATSPI_TYPE_EVENT, e);
    }
    void discoverInitialFocus() {
        if (focus_ || (lastScan_.isValid() && lastScan_.elapsed() < 1000))
            return;
        lastScan_.start();
        QElapsedTimer budget;
        budget.start();
        int nodes = 0;
        std::function<AtspiAccessible *(AtspiAccessible *, int)> visit =
            [&](AtspiAccessible *object, int depth) -> AtspiAccessible * {
            if (!object || depth > 10 || ++nodes > 400 || budget.elapsed() > 300)
                return nullptr;
            auto role = atspi_accessible_get_role(object, nullptr);
            GRef<AtspiStateSet> states(atspi_accessible_get_state_set(object), g_object_unref);
            if (!states)
                return nullptr;
            if ((role == ATSPI_ROLE_FRAME || role == ATSPI_ROLE_WINDOW) &&
                !atspi_state_set_contains(states.get(), ATSPI_STATE_ACTIVE))
                return nullptr;
            if (atspi_state_set_contains(states.get(), ATSPI_STATE_FOCUSED) &&
                (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT ||
                 role == ATSPI_ROLE_PASSWORD_TEXT))
                return ATSPI_ACCESSIBLE(g_object_ref(object));
            int count = atspi_accessible_get_child_count(object, nullptr);
            for (int i = 0; i < count && nodes < 400 && budget.elapsed() < 300; ++i) {
                GRef<AtspiAccessible> child(atspi_accessible_get_child_at_index(object, i, nullptr),
                                            g_object_unref);
                if (auto result = visit(child.get(), depth + 1))
                    return result;
            }
            return nullptr;
        };
        GRef<AtspiAccessible> desktop(atspi_get_desktop(0), g_object_unref);
        if (!desktop)
            return;
        int count = atspi_accessible_get_child_count(desktop.get(), nullptr);
        for (int i = 0; i < count && budget.elapsed() < 300; ++i) {
            int index = scanApplication_++ % count;
            GRef<AtspiAccessible> application(
                atspi_accessible_get_child_at_index(desktop.get(), index, nullptr), g_object_unref);
            auto found = visit(application.get(), 0);
            if (found) {
                if (!focus_)
                    focus_ = found;
                else
                    g_object_unref(found);
                return;
            }
        }
    }
    bool unlocked() {
        // A missing lock service is uncertainty, not permission to read text.
        QDBusInterface lock("org.gnome.ScreenSaver", "/org/gnome/ScreenSaver",
                            "org.gnome.ScreenSaver", QDBusConnection::sessionBus());
        lock.setTimeout(100);
        QDBusReply<bool> r = lock.call("GetActive");
        if (r.isValid())
            return !r.value();
        QDBusInterface other("org.freedesktop.ScreenSaver", "/ScreenSaver",
                             "org.freedesktop.ScreenSaver", QDBusConnection::sessionBus());
        other.setTimeout(100);
        QDBusReply<bool> s = other.call("GetActive");
        return s.isValid() && !s.value();
    }

  public:
    LinuxAdapter() {
        wayland_ = qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland";
        QDBusInterface a11y("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus",
                            QDBusConnection::sessionBus());
        a11y.setTimeout(100);
        QDBusReply<QString> address = a11y.call("GetAddress");
        if (!address.isValid() || address.value().isEmpty())
            return;
        auto probe = QDBusConnection::connectToBus(address.value(), "reword-a11y-probe");
        bool connected = probe.isConnected();
        QDBusConnection::disconnectFromBus("reword-a11y-probe");
        if (!connected)
            return;
        qputenv("AT_SPI_BUS_ADDRESS", address.value().toUtf8());
        atspi_init();
        atspi_set_timeout(80, 100);
        available_ = true;
        listener_ = atspi_event_listener_new(event, this, nullptr);
        for (auto event :
             {"object:state-changed:focused", "object:text-changed", "object:text-caret-moved"})
            atspi_event_listener_register(listener_, event, nullptr);
        lastPoll_.start();
        if (!wayland_)
            display_ = XOpenDisplay(nullptr);
    }
    ~LinuxAdapter() override {
        if (listener_) {
            for (auto event :
                 {"object:state-changed:focused", "object:text-changed", "object:text-caret-moved"})
                atspi_event_listener_deregister(listener_, event, nullptr);
            g_object_unref(listener_);
        }
        if (focus_)
            g_object_unref(focus_);
        if (display_)
            XCloseDisplay(display_);
        if (available_)
            atspi_exit();
    }
    QString description() const override {
        if (!available_)
            return "Accessibility bus unavailable · writing workspace only";
        return wayland_ ? "Wayland · Fcitx5 integration required for live correction"
                        : "X11 · accessible plain text fields";
    }
    bool changed() override {
        if (!available_)
            return false;
        for (int i = 0; i < 32 && g_main_context_pending(nullptr); ++i)
            g_main_context_iteration(nullptr, false);
        if (lastPoll_.elapsed() > 1000) {
            dirty_ = true;
            lastPoll_.restart();
        }
        bool changed = dirty_;
        dirty_ = false;
        return changed;
    }
    Field inspect(bool includeText) override {
        if (!available_)
            return {};
        for (int i = 0; i < 32 && g_main_context_pending(nullptr); ++i)
            g_main_context_iteration(nullptr, false);
        discoverInitialFocus();
        Field f;
        if (!focus_)
            return f;
        GError *error = nullptr;
        auto pid = atspi_accessible_get_process_id(focus_, &error);
        if (error) {
            g_error_free(error);
            return f;
        }
        f.app = QFileInfo(QString("/proc/%1/exe").arg(pid)).symLinkTarget();
        if (f.app.isEmpty() || (pid == getpid() || pid == getppid()))
            return Field{};
        auto obj = ATSPI_OBJECT(focus_);
        f.id = QString::fromUtf8(obj->app->bus_name) + QString::fromUtf8(obj->path);
        auto role = atspi_accessible_get_role(focus_, &error);
        if (error) {
            g_error_free(error);
            return f;
        }
        f.password = role == ATSPI_ROLE_PASSWORD_TEXT;
        auto states = atspi_accessible_get_state_set(focus_);
        if (!states)
            return f;
        f.focused = atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
        f.editable = atspi_state_set_contains(states, ATSPI_STATE_EDITABLE);
        bool sensitive = atspi_state_set_contains(states, ATSPI_STATE_SENSITIVE);
        bool singleLine = atspi_state_set_contains(states, ATSPI_STATE_SINGLE_LINE);
        g_object_unref(states);
        f.plain = role == ATSPI_ROLE_ENTRY || (role == ATSPI_ROLE_TEXT && singleLine);
        f.known = f.plain || f.password;
        // The Fcitx addon owns Wayland live edits; never double-correct via AT-SPI.
        f.locked = !unlocked();
        if (!includeText || f.password || !f.known || !f.editable || !f.focused || f.locked ||
            !sensitive)
            return f;
        GRef<AtspiText> text(atspi_accessible_get_text_iface(focus_), g_object_unref);
        GRef<AtspiEditableText> editable(atspi_accessible_get_editable_text_iface(focus_),
                                         g_object_unref);
        if (!text || !editable) {
            f.known = false;
            return f;
        }
        int count = atspi_text_get_character_count(text.get(), &error);
        if (error || count < 0 || count > 50000) {
            if (error)
                g_error_free(error);
            f.known = false;
            return f;
        }
        gchar *content = atspi_text_get_text(text.get(), 0, count, &error);
        if (error || !content) {
            if (error)
                g_error_free(error);
            g_free(content);
            f.known = false;
            return f;
        }
        f.text = QString::fromUtf8(content);
        g_free(content);
        auto chars = f.text.toUcs4();
        auto utf16offset = [&](int n) {
            return QString::fromUcs4(reinterpret_cast<const char32_t *>(chars.constData()),
                                     std::clamp(n, 0, int(chars.size())))
                .size();
        };
        f.caret = utf16offset(atspi_text_get_caret_offset(text.get(), &error));
        if (!error && atspi_text_get_n_selections(text.get(), &error) > 0) {
            AtspiRange *r = atspi_text_get_selection(text.get(), 0, &error);
            if (r) {
                f.selectionStart = utf16offset(r->start_offset);
                f.selectionEnd = utf16offset(r->end_offset);
                g_free(r);
            }
        }
        if (error) {
            g_error_free(error);
            f.known = false;
            f.text.clear();
        }
        return f;
    }
    bool replace(const Field &expected, const QString &newText, int caret) override {
        auto current = inspect(true);
        if (!sameTarget(expected, current))
            return false;
        GRef<AtspiEditableText> editable(atspi_accessible_get_editable_text_iface(focus_),
                                         g_object_unref);
        GRef<AtspiText> text(atspi_accessible_get_text_iface(focus_), g_object_unref);
        if (!editable || !text)
            return false;
        GError *error = nullptr;
        bool ok = atspi_editable_text_set_text_contents(editable.get(),
                                                        newText.toUtf8().constData(), &error);
        if (error) {
            g_error_free(error);
            return false;
        }
        if (!ok)
            return false;
        atspi_text_set_caret_offset(text.get(), newText.left(caret).toUcs4().size(), nullptr);
        // A single set-contents call avoids delete/insert windows in supported plain fields.
        auto after = inspect(true);
        return after.id == expected.id && after.text == newText;
    }
    bool shortcuts(const QString &fix, const QString &undo, const QString &pause) override {
        if (!display_)
            return false;
        XSync(display_, False);
        trapped_ = display_;
        trapError_ = 0;
        previousHandler_ = XSetErrorHandler(errorHandler);
        XUngrabKey(display_, AnyKey, AnyModifier, DefaultRootWindow(display_));
        keys_.clear();
        pendingShortcuts_.clear();
        auto grab = [&](QString sequence, QString action) {
            QKeySequence seq(sequence);
            if (seq.count() != 1)
                return false;
            auto key = seq[0];
            int qt = key.key();
            if (qt < Qt::Key_A || qt > Qt::Key_Z)
                return false;
            unsigned mods = 0;
            auto m = key.keyboardModifiers();
            if (m & Qt::ControlModifier)
                mods |= ControlMask;
            if (m & Qt::AltModifier)
                mods |= Mod1Mask;
            if (m & Qt::ShiftModifier)
                mods |= ShiftMask;
            if (m & Qt::MetaModifier)
                mods |= Mod4Mask;
            int code = XKeysymToKeycode(display_, XK_a + (qt - Qt::Key_A));
            int id = (code << 16) | int(mods);
            if (!code || keys_.contains(id))
                return false;
            for (unsigned extra :
                 {0u, unsigned(LockMask), unsigned(Mod2Mask), unsigned(LockMask | Mod2Mask)})
                XGrabKey(display_, code, mods | extra, DefaultRootWindow(display_), False,
                         GrabModeAsync, GrabModeAsync);
            keys_[id] = action;
            return true;
        };
        Bool detectable = False;
        const bool repeatSafe = XkbSetDetectableAutoRepeat(display_, True, &detectable) && detectable;
        bool ok = repeatSafe && grab(fix, "fix") && grab(undo, "undo") && grab(pause, "pause");
        XSync(display_, False);
        ok = ok && trapError_ == 0;
        if (!ok) {
            XUngrabKey(display_, AnyKey, AnyModifier, DefaultRootWindow(display_));
            keys_.clear();
            XSync(display_, False);
        }
        XSetErrorHandler(previousHandler_);
        trapped_ = nullptr;
        return ok;
    }
    QString takeAction() override {
        if (!display_)
            return {};
        for (int count = 0; count < 64 && XPending(display_); ++count) {
            XEvent e;
            XNextEvent(display_, &e);
            if (e.type == KeyPress) {
                auto mods = e.xkey.state & ~(LockMask | Mod2Mask);
                const auto action = keys_.value((e.xkey.keycode << 16) | mods);
                if (action.isEmpty())
                    continue;
                bool held = false;
                for (const auto &pending : pendingShortcuts_)
                    held |= pending.key == int(e.xkey.keycode) && !pending.released;
                if (held)
                    continue; // Detectable auto-repeat sends presses without releases.
                if (pendingShortcuts_.size() >= 8) {
                    pendingShortcuts_.clear();
                    return {};
                }
                pendingShortcuts_.enqueue({int(e.xkey.keycode), action, false, {}});
            } else if (e.type == KeyRelease) {
                for (auto &pending : pendingShortcuts_)
                    if (pending.key == int(e.xkey.keycode) && !pending.released) {
                        pending.released = true;
                        pending.releasedAt.start();
                        break;
                    }
            }
        }
        // Passive grabs transiently remove GTK focus. Dispatch after release
        // and one event-loop interval so accessibility can observe its return.
        if (XPending(display_))
            return "pending";
        if (pendingShortcuts_.isEmpty())
            return {};
        for (const auto &pending : pendingShortcuts_)
            if (!pending.released || pending.releasedAt.elapsed() < 20)
                return "pending";
        return pendingShortcuts_.dequeue().action;
    }
};
std::unique_ptr<Adapter> createNativeAdapter() {
    return std::make_unique<LinuxAdapter>();
}
} // namespace reword
