#include "adapter.h"
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QKeySequence>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QQueue>
#undef signals
#include <atspi/atspi.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XRes.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <unistd.h>
#include "x11_ibus_undo_route.h"
namespace reword {
template <class T> using GRef = std::unique_ptr<T, decltype(&g_object_unref)>;
class LinuxAdapter final : public Adapter {
    static constexpr int LockQueryTimeoutMs = 100;
    // AT-SPI and D-Bus are separate providers. Give their verification a
    // bounded window of its own without accepting a late policy decision.
    static constexpr int WriteRecheckBudgetMs = 250;
    bool available_ = false;
    QString unavailableReason_ = "Accessibility bus unavailable · writing workspace only";
    AtspiAccessible *focus_ = nullptr;
    GRef<AtspiAccessible> validated_{nullptr, g_object_unref};
    AtspiEventListener *listener_ = nullptr;
    static constexpr const char *fieldEventTypes_[] = {
        "object:state-changed:focused", "object:text-changed", "object:text-caret-moved",
        "object:text-selection-changed", "object:property-change:accessible-role",
        "object:state-changed:editable", "object:state-changed:sensitive",
        "object:state-changed:single-line"};
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
    quint64 fieldEvents_ = 0;
    QElapsedTimer lastPoll_, lastScan_;
    int scanApplication_ = 0;
    Display *display_ = nullptr;
    QHash<int, QString> keys_;
    Atom ibusUndoRoute_ = None, browserUndoRoute_ = None, activeWindowAtom_ = None;
    int undoShortcut_ = 0;
    bool undoGrabbed_ = false;
    QElapsedTimer lastUndoGrabFailure_;
    struct Shortcut {
        int key;
        QString action;
        bool released = false;
        QElapsedTimer releasedAt;
    };
    QQueue<Shortcut> pendingShortcuts_;
    bool wayland_ = false;
    QString foregroundApplication(Window *activeWindow = nullptr, unsigned long *process = nullptr) {
        if (!display_)
            return {};
        XSync(display_, False);
        trapped_ = display_;
        trapError_ = 0;
        previousHandler_ = XSetErrorHandler(errorHandler);
        auto property = [&](Window window, const char *name, Atom expected) -> unsigned long {
            Atom type = None;
            int format = 0;
            unsigned long count = 0, remaining = 0;
            unsigned char *data = nullptr;
            const auto atom = XInternAtom(display_, name, True);
            if (atom == None)
                return 0;
            const int result = XGetWindowProperty(display_, window, atom, 0, 1, False,
                                                  expected, &type, &format, &count, &remaining, &data);
            unsigned long value = 0;
            if (result == Success && type == expected && format == 32 && count == 1 && data)
                value = *reinterpret_cast<unsigned long *>(data);
            if (data)
                XFree(data);
            return value;
        };
        const auto window = property(DefaultRootWindow(display_), "_NET_ACTIVE_WINDOW", XA_WINDOW);
        unsigned long pid = 0;
        if (window) {
            XResClientIdSpec specification{window, XRES_CLIENT_ID_PID_MASK};
            long count = 0;
            XResClientIdValue *identities = nullptr;
            XResQueryClientIds(display_, 1, &specification, &count, &identities);
            if (identities) {
                for (long i = 0; i < count && i < 8; ++i) {
                    const auto candidate = XResGetClientPid(&identities[i]);
                    if (candidate > 0)
                        pid = static_cast<unsigned long>(candidate);
                }
                XResClientIdsDestroy(count, identities);
            }
            if (!pid)
                pid = property(window, "_NET_WM_PID", XA_CARDINAL);
        }
        XSync(display_, False);
        const bool valid = trapError_ == 0;
        XSetErrorHandler(previousHandler_);
        trapped_ = nullptr;
        if (!valid || !pid || pid == unsigned(getpid()) || pid == unsigned(getppid()))
            return {};
        if (activeWindow)
            *activeWindow = window;
        if (process)
            *process = pid;
        return QFileInfo(QString("/proc/%1/exe").arg(pid)).symLinkTarget();
    }
    static void event(AtspiEvent *e, void *data) {
        auto self = static_cast<LinuxAdapter *>(data);
        self->dirty_ = true;
        const bool focusChanged = QString::fromUtf8(e->type).startsWith("object:state-changed:focused");
        if (focusChanged || e->source == self->validated_.get())
            ++self->fieldEvents_;
        if (focusChanged && e->detail1) {
            if (self->focus_)
                g_object_unref(self->focus_);
            self->focus_ = ATSPI_ACCESSIBLE(g_object_ref(e->source));
        }
        g_boxed_free(ATSPI_TYPE_EVENT, e);
    }
    static void dispatchPendingEvents() {
        for (int i = 0; i < 32 && g_main_context_pending(nullptr); ++i)
            g_main_context_iteration(nullptr, false);
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
        const auto bus = QDBusConnection::sessionBus();
        const auto query = [&bus](const QString &service, const QString &path) {
            auto request = QDBusMessage::createMethodCall(service, path, service, "GetActive");
            return QDBusReply<bool>(bus.call(request, QDBus::Block, LockQueryTimeoutMs));
        };
        const auto r = query("org.gnome.ScreenSaver", "/org/gnome/ScreenSaver");
        if (r.isValid())
            return !r.value();
        const auto s = query("org.freedesktop.ScreenSaver", "/ScreenSaver");
        return s.isValid() && !s.value();
    }

  public:
    LinuxAdapter() {
        wayland_ = qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland";
        if (!wayland_) {
            QDBusInterface status("org.a11y.Bus", "/org/a11y/bus",
                                  "org.freedesktop.DBus.Properties",
                                  QDBusConnection::sessionBus());
            status.setTimeout(300);
            QDBusReply<QDBusVariant> enabled =
                status.call("Get", "org.a11y.Status", "IsEnabled");
            if (!enabled.isValid()) {
                unavailableReason_ = "Desktop accessibility status unavailable · writing workspace only";
                return;
            }
            if (!enabled.value().variant().toBool()) {
                status.call("Set", "org.a11y.Status", "IsEnabled",
                            QVariant::fromValue(QDBusVariant(true)));
                enabled = status.call("Get", "org.a11y.Status", "IsEnabled");
                if (!enabled.isValid() || !enabled.value().variant().toBool()) {
                    unavailableReason_ = "Desktop accessibility is disabled · enable it to detect applications";
                    return;
                }
            }
        }
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
        for (auto event : fieldEventTypes_)
            atspi_event_listener_register(listener_, event, nullptr);
        lastPoll_.start();
        if (!wayland_)
            display_ = XOpenDisplay(nullptr);
    }
    ~LinuxAdapter() override {
        if (listener_) {
            for (auto event : fieldEventTypes_)
                atspi_event_listener_deregister(listener_, event, nullptr);
            g_object_unref(listener_);
        }
        if (focus_)
            g_object_unref(focus_);
        if (display_)
            XCloseDisplay(display_);
        validated_.reset();
        if (available_)
            atspi_exit();
    }
    QString description() const override {
        if (!available_)
            return unavailableReason_;
        return wayland_ ? "Wayland · Fcitx5 integration required for live correction"
                        : "X11 · accessible plain text fields";
    }
    bool changed() override {
        if (!available_)
            return false;
        dispatchPendingEvents();
        if (lastPoll_.elapsed() > 1000) {
            dirty_ = true;
            lastPoll_.restart();
        }
        bool changed = dirty_;
        dirty_ = false;
        return changed;
    }
    Field inspect(bool includeText) override {
        auto field = snapshot(includeText, nullptr);
        if (!includeText && !wayland_) {
            const auto app = foregroundApplication();
            if (!app.isEmpty() && field.app != app) {
                // Application consent needs only executable identity. A window
                // without a validated text provider never becomes editable.
                field = {};
                field.app = app;
            }
        }
        return field;
    }
    Field readExpected(const Field &expected) override {
        if (expected.app.isEmpty() || expected.id.isEmpty()) {
            validated_.reset();
            return {};
        }
        return snapshot(true, &expected);
    }
    InputMethodTarget inputMethodTarget() override {
        Window before = 0, after = 0;
        unsigned long foregroundPid = 0, finalPid = 0;
        unsigned providerPid = 0;
        bool multiline = false;
        const auto app = foregroundApplication(&before, &foregroundPid);
        if (app.isEmpty() || !before)
            return {};
        const auto field = snapshot(false, nullptr, &multiline, &providerPid);
        if (!multiline || field.app != app || field.id.isEmpty() ||
            providerPid != foregroundPid || !field.editable || !field.focused ||
            field.password || field.locked)
            return {};
        if (foregroundApplication(&after, &finalPid) != app || before != after ||
            foregroundPid != finalPid)
            return {};
        return {app, field.id, quint32(after)};
    }
  private:
    static bool readEligibility(AtspiAccessible *target, Field &field, bool *multiline = nullptr) {
        GError *error = nullptr;
        const auto role = atspi_accessible_get_role(target, &error);
        if (error) {
            g_error_free(error);
            return false;
        }
        field.password = role == ATSPI_ROLE_PASSWORD_TEXT;
        GRef<AtspiStateSet> states(atspi_accessible_get_state_set(target), g_object_unref);
        if (!states)
            return false;
        field.focused = atspi_state_set_contains(states.get(), ATSPI_STATE_FOCUSED);
        field.editable = atspi_state_set_contains(states.get(), ATSPI_STATE_EDITABLE) &&
                         atspi_state_set_contains(states.get(), ATSPI_STATE_SENSITIVE);
        const bool singleLine = atspi_state_set_contains(states.get(), ATSPI_STATE_SINGLE_LINE);
        if (multiline)
            *multiline = role == ATSPI_ROLE_TEXT && !singleLine &&
                         atspi_state_set_contains(states.get(), ATSPI_STATE_MULTI_LINE);
        field.plain = role == ATSPI_ROLE_ENTRY || (role == ATSPI_ROLE_TEXT && singleLine);
        field.known = field.plain || field.password;
        return true;
    }
    Field snapshot(bool includeText, const Field *expected, bool *multiline = nullptr,
                   unsigned *providerPid = nullptr) {
        validated_.reset();
        if (!available_)
            return {};
        dispatchPendingEvents();
        discoverInitialFocus();
        const auto fieldEvents = fieldEvents_;
        Field f;
        if (!focus_)
            return f;
        // AT-SPI calls can dispatch focus events. Pin this object for the whole
        // snapshot so metadata and text cannot follow different focus_ values.
        GRef<AtspiAccessible> target(ATSPI_ACCESSIBLE(g_object_ref(focus_)), g_object_unref);
        GError *error = nullptr;
        auto pid = atspi_accessible_get_process_id(target.get(), &error);
        if (error) {
            g_error_free(error);
            return f;
        }
        if (providerPid)
            *providerPid = pid;
        f.app = QFileInfo(QString("/proc/%1/exe").arg(pid)).symLinkTarget();
        if (f.app.isEmpty() || (pid == getpid() || pid == getppid()))
            return Field{};
        auto obj = ATSPI_OBJECT(target.get());
        f.id = QString::fromUtf8(obj->app->bus_name) + QString::fromUtf8(obj->path);
        if (expected && (f.id != expected->id || f.app != expected->app))
            return {};
        if (multiline)
            atspi_accessible_clear_cache_single(target.get());
        if (!readEligibility(target.get(), f, multiline))
            return f;
        // The Fcitx addon owns Wayland live edits; never double-correct via AT-SPI.
        f.locked = !unlocked();
        if (multiline) {
            dispatchPendingEvents();
            if (focus_ != target.get() || fieldEvents_ != fieldEvents)
                return {};
        }
        if (!includeText || f.password || !f.known || !f.editable || !f.focused || f.locked)
            return f;
        GRef<AtspiText> text(atspi_accessible_get_text_iface(target.get()), g_object_unref);
        GRef<AtspiEditableText> editable(atspi_accessible_get_editable_text_iface(target.get()),
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
        if (f.known)
            validated_.reset(ATSPI_ACCESSIBLE(g_object_ref(target.get())));
        return f;
    }
  public:
    WriteResult replace(const Field &expected, const QString &newText, int caret,
                 const WriteAuthorization &authorize) override {
        auto current = readExpected(expected);
        if (!sameTarget(expected, current) || !validated_)
            return {false, {}, WriteRejection::TargetChanged};
        GRef<AtspiAccessible> target(ATSPI_ACCESSIBLE(g_object_ref(validated_.get())),
                                     g_object_unref);
        GRef<AtspiEditableText> editable(atspi_accessible_get_editable_text_iface(target.get()),
                                         g_object_unref);
        GRef<AtspiText> text(atspi_accessible_get_text_iface(target.get()), g_object_unref);
        if (!editable || !text)
            return {false, {}, WriteRejection::InterfacesUnavailable};
        const auto replacement = newText.toUtf8();
        const auto fieldEvents = fieldEvents_;
        const QDeadlineTimer authorizationWindow(WriteAuthorizationBudgetMs, Qt::PreciseTimer);
        if (!authorize || !authorize())
            return {false, {}, WriteRejection::AuthorizationDenied};
        if (authorizationWindow.hasExpired())
            return {false, {}, WriteRejection::AuthorizationExpired};
        const QDeadlineTimer recheckWindow(WriteRecheckBudgetMs, Qt::PreciseTimer);
        if (!unlocked())
            return {false, {}, WriteRejection::LockUnverified};
        // GTK can change the role of an existing entry without a text or
        // focus event. Refresh the metadata without requesting field text.
        atspi_accessible_clear_cache_single(target.get());
        Field eligibility;
        if (!readEligibility(target.get(), eligibility) || !eligibility.plain ||
            eligibility.password || !eligibility.editable || !eligibility.focused)
            return {false, {}, WriteRejection::EligibilityChanged};
        dispatchPendingEvents();
        if (recheckWindow.hasExpired())
            return {false, {}, WriteRejection::RecheckExpired};
        if (fieldEvents_ != fieldEvents)
            return {false, {}, WriteRejection::RecheckFieldEvent};
        if (g_main_context_pending(nullptr))
            return {false, {}, WriteRejection::RecheckBacklog};
        // The provider recheck may take longer than a policy reply. Require
        // permission again after it, then check events before writing.
        const QDeadlineTimer finalWindow(WriteAuthorizationBudgetMs, Qt::PreciseTimer);
        if (!authorize())
            return {false, {}, WriteRejection::AuthorizationDenied};
        if (finalWindow.hasExpired())
            return {false, {}, WriteRejection::FinalAuthorizationExpired};
        dispatchPendingEvents();
        if (finalWindow.hasExpired())
            return {false, {}, WriteRejection::FinalCheckExpired};
        if (fieldEvents_ != fieldEvents)
            return {false, {}, WriteRejection::FinalFieldEvent};
        if (g_main_context_pending(nullptr))
            return {false, {}, WriteRejection::FinalBacklog};
        GError *error = nullptr;
        bool ok = atspi_editable_text_set_text_contents(editable.get(),
                                                        replacement.constData(), &error);
        if (error) {
            g_error_free(error);
            return {false, {}, WriteRejection::ProviderError};
        }
        if (!ok)
            return {false, {}, WriteRejection::ProviderRejected};
        atspi_text_set_caret_offset(text.get(), newText.left(caret).toUcs4().size(), nullptr);
        // A single set-contents call avoids delete/insert windows in supported plain fields.
        const auto foreground = foregroundApplication();
        if (!foreground.isEmpty() && foreground != expected.app)
            return {true, {}};
        return {true, readExpected(expected)};
    }
    void setShortcutGrab(int id, bool grab) {
        const auto root = DefaultRootWindow(display_);
        for (unsigned extra :
             {0u, unsigned(LockMask), unsigned(Mod2Mask), unsigned(LockMask | Mod2Mask)}) {
            const auto modifiers = unsigned(id & 0xffff) | extra;
            if (grab)
                XGrabKey(display_, id >> 16, modifiers, root, False, GrabModeAsync, GrabModeAsync);
            else
                XUngrabKey(display_, id >> 16, modifiers, root);
        }
    }
    void updateUndoGrab() {
        if (!undoShortcut_ || (ibusUndoRoute_ == None && browserUndoRoute_ == None))
            return;
        const bool delegated =
            (ibusUndoRoute_ != None && x11IBusOwnsUndo(display_, ibusUndoRoute_, activeWindowAtom_)) ||
            (browserUndoRoute_ != None && x11BrowserOwnsUndo(display_, browserUndoRoute_, activeWindowAtom_));
        const bool native = !delegated;
        if (native == undoGrabbed_)
            return;
        if (!native) {
            setShortcutGrab(undoShortcut_, false);
            undoGrabbed_ = false;
            pendingShortcuts_.removeIf([](const Shortcut &shortcut) {
                return shortcut.action == "undo";
            });
            return;
        }
        if (lastUndoGrabFailure_.isValid() && lastUndoGrabFailure_.elapsed() < 1000)
            return;
        trapped_ = display_;
        trapError_ = 0;
        previousHandler_ = XSetErrorHandler(errorHandler);
        setShortcutGrab(undoShortcut_, true);
        XSync(display_, False);
        undoGrabbed_ = trapError_ == 0;
        if (!undoGrabbed_) {
            // A different client may have grabbed one of the lock variants.
            setShortcutGrab(undoShortcut_, false);
            lastUndoGrabFailure_.start();
        }
        XSetErrorHandler(previousHandler_);
        trapped_ = nullptr;
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
        undoShortcut_ = 0;
        undoGrabbed_ = false;
        lastUndoGrabFailure_.invalidate();
#ifdef REWORD_IBUS_UNDO_ROUTING
        ibusUndoRoute_ = XInternAtom(display_, IBusUndoRouteProperty, False);
#endif
#ifdef REWORD_BROWSER_UNDO_ROUTING
        browserUndoRoute_ = XInternAtom(display_, BrowserUndoRouteAtom, False);
#endif
#if defined(REWORD_IBUS_UNDO_ROUTING) || defined(REWORD_BROWSER_UNDO_ROUTING)
        activeWindowAtom_ = XInternAtom(display_, "_NET_ACTIVE_WINDOW", False);
#endif
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
            setShortcutGrab(id, true);
            if (action == "undo")
                undoShortcut_ = id;
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
            undoShortcut_ = 0;
            XSync(display_, False);
        }
        XSetErrorHandler(previousHandler_);
        trapped_ = nullptr;
        undoGrabbed_ = ok;
        updateUndoGrab();
        return ok;
    }
    QString takeAction() override {
        if (!display_)
            return {};
        updateUndoGrab();
        for (int count = 0; count < 64 && XPending(display_); ++count) {
            XEvent e;
            XNextEvent(display_, &e);
            if (e.type == KeyPress) {
                auto mods = e.xkey.state & ~(LockMask | Mod2Mask);
                const auto action = keys_.value((e.xkey.keycode << 16) | mods);
                if (action.isEmpty() || (action == "undo" && !undoGrabbed_))
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
