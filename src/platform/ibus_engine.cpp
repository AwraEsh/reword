#include "platform/adapter.h"
#include "core/store.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <sys/stat.h>

#undef signals
#include <ibus.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto witnessLifetime = 250ms;
constexpr auto keyBudget = 40ms;
constexpr int surroundingByteLimit = 200000;
constexpr char engineName[] = "reword-x11";

enum class Trace {
    Focused = 1, Blurred, Key, NoSurrounding, Unverified, Policy, Snapshot, NoSnapshot,
    NoWord, NoCorrection, RetrySkipped, TimedOut, Write, Reset, NoFocus,
};
void trace(Trace reason) {
    static const bool enabled = qEnvironmentVariableIsSet("REWORD_IBUS_DIAGNOSTICS");
    static int remaining = 512;
    if (enabled && remaining > 0) {
        --remaining;
        std::fprintf(stderr, "IBUS %d\n", int(reason));
    }
}

struct Witness {
    reword::InputMethodTarget target;
    Clock::time_point observed;
};

class TargetObserver {
    std::mutex mutex_;
    Witness latest_;
    std::jthread thread_;

  public:
    TargetObserver() : thread_([this](std::stop_token stop) {
        auto adapter = reword::createAdapter();
        while (!stop.stop_requested()) {
            // Record when the observation began. A slow provider cannot
            // turn an old focus snapshot into a fresh one.
            const auto observed = Clock::now();
            auto target = adapter->inputMethodTarget();
            {
                std::lock_guard lock(mutex_);
                latest_ = {std::move(target), observed};
            }
            std::this_thread::sleep_for(55ms);
        }
    }) {}

    Witness latest() {
        std::lock_guard lock(mutex_);
        return latest_;
    }
};

struct Observation {
    QString text;
    int caret = 0;
};

struct PendingEdit {
    Observation expected;
    reword::RetryCycle retry;
    Clock::time_point expires;
};

struct Context {
    Clock::time_point focused, deletionUntil;
    guint capabilities = 0;
    reword::InputMethodTarget target;
    std::optional<Observation> surrounding;
    std::optional<PendingEdit> pending;
    reword::RetryCycle retry;
    QString prefix, lastToken;
    int retryStart = -1;

    void resetRetry() {
        pending.reset();
        retry.reset();
        prefix.clear();
        lastToken.clear();
        retryStart = -1;
    }
    void clear() {
        resetRetry();
        surrounding.reset();
        target = {};
        deletionUntil = {};
    }
};

bool sameField(const reword::InputMethodTarget &a, const reword::InputMethodTarget &b) {
    return a.window == b.window && a.app == b.app && a.field == b.field;
}

class Runtime {
    std::unique_ptr<Display, decltype(&XCloseDisplay)> display_{XOpenDisplay(nullptr), XCloseDisplay};
    Atom activeWindow_ = display_ ? XInternAtom(display_.get(), "_NET_ACTIVE_WINDOW", True) : None;
    TargetObserver observer_;

  public:
    reword::Store store;
    reword::Engine dictionary;
    qint64 dictionaryRevision = -1;

    explicit Runtime(const QString &directory) : store(directory, 0) {}

    quint32 activeWindow() const {
        if (activeWindow_ == None)
            return 0;
        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char *value = nullptr;
        const auto status = XGetWindowProperty(display_.get(), DefaultRootWindow(display_.get()),
            activeWindow_, 0, 1, False, XA_WINDOW, &type, &format, &count, &remaining, &value);
        quint32 window = 0;
        if (status == Success && type == XA_WINDOW && format == 32 && count == 1 && value)
            window = quint32(*reinterpret_cast<unsigned long *>(value));
        if (value)
            XFree(value);
        return window;
    }

    reword::InputMethodTarget targetFor(IBusEngine *engine, const Context &context) {
        guint purpose = 0, hints = 0;
        ibus_engine_get_content_type(engine, &purpose, &hints);
        if (context.focused == Clock::time_point::max()) {
            trace(Trace::NoFocus);
            return {};
        }
        if (!(context.capabilities & IBUS_CAP_SURROUNDING_TEXT) ||
            purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN ||
            (hints & IBUS_INPUT_HINT_PRIVATE)) {
            trace(Trace::NoSurrounding);
            return {};
        }
        const auto witness = observer_.latest();
        if (!witness.target.window || witness.observed < context.focused ||
            Clock::now() - witness.observed > witnessLifetime ||
            activeWindow() != witness.target.window) {
            trace(Trace::Unverified);
            return {};
        }
        return witness.target;
    }

    bool allowed(const reword::InputMethodTarget &target) {
        // This client has been tested to apply both parts of the IBus edit.
        // Expand support only after validating a client's range behavior.
        return target.window && QFileInfo(target.app).fileName() == "gnome-text-editor" &&
               !store.paused() && store.allowed(target.app);
    }
};

Runtime *runtime = nullptr;

struct RewordIBusEngine {
    IBusEngine parent;
    Context *context;
};
struct RewordIBusEngineClass {
    IBusEngineClass parent;
};
G_DEFINE_TYPE(RewordIBusEngine, reword_ibus_engine, IBUS_TYPE_ENGINE)

Context &state(IBusEngine *engine) {
    return *reinterpret_cast<RewordIBusEngine *>(engine)->context;
}

void focusIn(IBusEngine *engine) {
    trace(Trace::Focused);
    auto &context = state(engine);
    context.clear();
    context.focused = Clock::now();
}
void focusOut(IBusEngine *engine) {
    trace(Trace::Blurred);
    auto &context = state(engine);
    context.clear();
    context.focused = Clock::time_point::max();
}

void reset(IBusEngine *engine) {
    trace(Trace::Reset);
    auto &context = state(engine);
    context.surrounding.reset();
    if (Clock::now() > context.deletionUntil)
        context.resetRetry();
    context.deletionUntil = {};
}

void capabilities(IBusEngine *engine, guint caps) {
    auto &context = state(engine);
    context.capabilities = caps;
    if (!(caps & IBUS_CAP_SURROUNDING_TEXT))
        context.clear();
}

void contentType(IBusEngine *engine, guint purpose, guint hints) {
    if (purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN ||
        (hints & IBUS_INPUT_HINT_PRIVATE))
        state(engine).clear();
}

void observe(IBusEngine *engine, IBusText *text, guint cursor, guint anchor) {
    auto &context = state(engine);
    try {
        const auto target = runtime->targetFor(engine, context);
        if (!runtime->allowed(target) || cursor != anchor) {
            context.clear();
            return;
        }
        if (!sameField(context.target, target)) {
            context.clear();
            context.target = target;
        }
        const char *utf8 = ibus_text_get_text(text);
        const auto bytes = utf8 ? strnlen(utf8, surroundingByteLimit + 1) : 0;
        if (bytes > surroundingByteLimit) {
            context.clear();
            return;
        }
        const auto value = QString::fromUtf8(utf8, bytes);
        const auto points = value.toUcs4();
        if (cursor > unsigned(points.size()) || points.size() > reword::MaxPassageCharacters) {
            context.clear();
            return;
        }
        const auto caret = QString::fromUcs4(
            reinterpret_cast<const char32_t *>(points.constData()), cursor).size();
        if (context.pending) {
            const auto &expected = context.pending->expected;
            if (Clock::now() <= context.pending->expires && value == expected.text &&
                caret == expected.caret) {
                context.retry = context.pending->retry;
                context.pending.reset();
            } else {
                context.resetRetry();
            }
        }
        if (context.retryStart >= 0 &&
            (caret < context.retryStart || value.left(context.retryStart) != context.prefix))
            context.resetRetry();
        context.surrounding = Observation{value, int(caret)};
        trace(Trace::Snapshot);
    } catch (...) {
        context.clear();
    }
}

gboolean process(IBusEngine *engine, guint key, guint, guint modifiers) {
    if (modifiers & IBUS_RELEASE_MASK)
        return FALSE;
    auto &context = state(engine);
    trace(Trace::Key);
    const auto started = Clock::now();
    try {
        const auto target = runtime->targetFor(engine, context);
        if (!runtime->allowed(target)) {
            trace(Trace::Policy);
            context.clear();
            return FALSE;
        }
        if (!sameField(context.target, target)) {
            context.clear();
            context.target = target;
        }
        // Only a verified, permitted field can request surrounding text.
        // The toolkit sends snapshots in later IBus callbacks.
        ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);
        auto observed = std::exchange(context.surrounding, std::nullopt);
        const auto code = ibus_keyval_to_unicode(key);
        const bool modified = modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK);
        context.deletionUntil = key == IBUS_KEY_BackSpace || key == IBUS_KEY_Delete
                                    ? started + 250ms : Clock::time_point{};
        if (key == IBUS_KEY_Left || key == IBUS_KEY_Right || key == IBUS_KEY_Up ||
            key == IBUS_KEY_Down || key == IBUS_KEY_Home || key == IBUS_KEY_End ||
            key == IBUS_KEY_Page_Up || key == IBUS_KEY_Page_Down) {
            context.resetRetry();
            return FALSE;
        }
        const bool boundary = code == ' ' || code == '.' || code == ',' || code == '!' ||
                              code == '?' || code == 0x060c || code == 0x061f;
        if (!observed || modified) {
            if (boundary && !modified)
                trace(Trace::NoSnapshot);
            return FALSE;
        }
        if (!boundary) {
            if (code >= 0x20 && context.retryStart >= 0 &&
                observed->caret > context.retryStart + context.lastToken.size())
                context.resetRetry();
            return FALSE;
        }
        const QChar mark{char16_t(code)};
        const auto word = reword::wordBeforeBoundary(observed->text, observed->caret, mark);
        if (!word) {
            trace(Trace::NoWord);
            return FALSE;
        }
        const auto prefix = observed->text.left(word->start);
        if (context.prefix != prefix || context.lastToken != word->token ||
            context.retryStart != word->start)
            context.resetRetry();

        const auto revision = runtime->store.value("configRevision", 0).toLongLong();
        auto options = runtime->store.options();
        options.abbreviations = false;
        if (revision != runtime->dictionaryRevision) {
            runtime->dictionary.setPersonal(runtime->store.words(), runtime->store.replacements());
            runtime->dictionaryRevision = revision;
        }
        const auto corrected = runtime->dictionary.word(word->token, options, prefix.right(128));
        if (corrected == word->token) {
            trace(Trace::NoCorrection);
            return FALSE;
        }

        auto retry = context.retry;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (!retry.shouldCorrect(target.field, word->start, word->token, now)) {
            context.retry = std::move(retry);
            trace(Trace::RetrySkipped);
            return FALSE;
        }
        if (!sameField(runtime->targetFor(engine, context), target) || !runtime->allowed(target) ||
            runtime->store.value("configRevision", 0).toLongLong() != revision ||
            Clock::now() - started > keyBudget) {
            trace(Trace::TimedOut);
            context.clear();
            return FALSE;
        }
        const auto replacement = corrected + mark;
        const auto expected = prefix + replacement + observed->text.mid(observed->caret);
        context.prefix = prefix;
        context.lastToken = word->token;
        context.retryStart = word->start;
        context.pending = PendingEdit{{expected, int(prefix.size() + replacement.size())},
                                      std::move(retry), Clock::now() + 300ms};
        const int length = word->token.toUcs4().size();
        const auto encoded = replacement.toUtf8();
        ibus_engine_delete_surrounding_text(engine, -length, length);
        ibus_engine_commit_text(engine, ibus_text_new_from_string(encoded.constData()));
        trace(Trace::Write);
        return TRUE;
    } catch (...) {
        context.clear();
        return FALSE;
    }
}

void reword_ibus_engine_init(RewordIBusEngine *engine) {
    engine->context = new Context;
    engine->context->focused = Clock::time_point::max();
}
void reword_ibus_engine_class_init(RewordIBusEngineClass *klass) {
    auto *engine = IBUS_ENGINE_CLASS(klass);
    engine->focus_in = focusIn;
    engine->focus_out = focusOut;
    engine->disable = focusOut;
    engine->reset = reset;
    engine->set_capabilities = capabilities;
    engine->set_content_type = contentType;
    engine->set_surrounding_text = observe;
    engine->process_key_event = process;
    G_OBJECT_CLASS(klass)->finalize = [](GObject *object) {
        delete reinterpret_cast<RewordIBusEngine *>(object)->context;
        G_OBJECT_CLASS(reword_ibus_engine_parent_class)->finalize(object);
    };
}

} // namespace

int main(int argc, char **argv) {
    umask(0077);
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariable("XDG_SESSION_TYPE") != "x11" ||
        !app.arguments().contains("--register"))
        return 2;
    try {
        auto directory = qEnvironmentVariable("REWORD_DATA_DIR");
        if (directory.isEmpty())
            directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                        "/reword";
        Runtime state(directory);
        runtime = &state;
        ibus_init();
        std::unique_ptr<IBusBus, decltype(&g_object_unref)> bus(ibus_bus_new(), g_object_unref);
        if (!ibus_bus_is_connected(bus.get())) {
            qWarning("Reword IBus could not connect to the input method bus.");
            return 1;
        }
        std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop(
            g_main_loop_new(nullptr, FALSE), g_main_loop_unref);
        g_signal_connect(bus.get(), "disconnected", G_CALLBACK(+[](IBusBus *, gpointer loop) {
            g_main_loop_quit(static_cast<GMainLoop *>(loop));
        }), loop.get());
        std::unique_ptr<IBusFactory, decltype(&g_object_unref)> factory(
            ibus_factory_new(ibus_bus_get_connection(bus.get())), g_object_unref);
        ibus_factory_add_engine(factory.get(), engineName, reword_ibus_engine_get_type());
        std::unique_ptr<IBusComponent, decltype(&g_object_unref)> component(
            ibus_component_new("org.freedesktop.IBus.Reword", "Reword X11 input method",
                               "0.1.0", "", "Reword", "", "", ""), g_object_unref);
        ibus_component_add_engine(component.get(), ibus_engine_desc_new(
            engineName, "Reword (experimental)", "Offline typing correction", "", "",
            "Reword", "", "default"));
        if (!ibus_bus_register_component(bus.get(), component.get())) {
            qWarning("Reword IBus could not register the engine.");
            return 1;
        }
        qInfo("Reword IBus engine registered.");
        g_main_loop_run(loop.get());
        g_signal_handlers_disconnect_by_data(bus.get(), loop.get());
        return 0;
    } catch (...) {
        qWarning("Reword IBus could not initialize local correction.");
        return 1;
    }
}
