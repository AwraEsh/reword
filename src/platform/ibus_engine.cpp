#include "platform/adapter.h"
#include "core/store.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QKeySequence>
#include <QScopeGuard>
#include <QStandardPaths>
#include <chrono>
#include <condition_variable>
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
    NoWord, NoCorrection, RetrySkipped, TimedOut, Write, Reset, NoFocus, NoPreparedEdit,
    MissingWitness, WitnessBeforeFocus, ExpiredWitness, ForegroundChanged,
    UndoRejected, UndoApplied, UndoNoRecord, UndoNoObservation,
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
    bool operator==(const Observation &) const = default;
};

constexpr char16_t wordBoundaries[] = u" .,!؟?،";

struct SuggestionRequest {
    quint64 generation = 0;
    qint64 revision = 0;
    Observation observed;
    bool operator==(const SuggestionRequest &) const = default;
};

struct Suggestion {
    reword::CompletedWord word;
    QString corrected, boundaries;
};

class SuggestionWorker {
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::optional<SuggestionRequest> request_;
    std::optional<Suggestion> result_;
    quint64 serial_ = 0;
    std::jthread thread_;

    void run(std::stop_token stop, const QString &directory) {
        reword::Engine dictionary;
        std::unique_ptr<reword::Store> store;
        qint64 dictionaryRevision = -1;
        quint64 processed = 0;
        while (!stop.stop_requested()) {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [&] { return request_ && processed != serial_; }))
                return;
            const auto request = *request_;
            processed = serial_;
            lock.unlock();

            std::optional<Suggestion> suggestion;
            try {
                if (!store)
                    store = std::make_unique<reword::Store>(directory, 0);
                if (store->value("configRevision", 0).toLongLong() == request.revision) {
                    auto options = store->options();
                    options.abbreviations = false;
                    if (dictionaryRevision != request.revision) {
                        dictionary.setPersonal(store->words(), store->replacements());
                        dictionaryRevision = request.revision;
                    }
                    for (const auto mark : wordBoundaries) {
                        if (!mark || stop.stop_requested())
                            break;
                        const auto word = reword::wordBeforeBoundary(
                            request.observed.text, request.observed.caret, QChar{mark});
                        if (!word)
                            continue;
                        if (!suggestion) {
                            auto corrected = dictionary.word(word->token, options,
                                request.observed.text.left(word->start).right(128));
                            suggestion = Suggestion{*word, std::move(corrected), {}};
                        }
                        if (suggestion->word.start == word->start &&
                            suggestion->word.token == word->token)
                            suggestion->boundaries += QChar{mark};
                    }
                    if (store->value("configRevision", 0).toLongLong() != request.revision)
                        suggestion.reset();
                }
            } catch (...) {
                suggestion.reset();
            }
            lock.lock();
            if (processed == serial_)
                result_ = std::move(suggestion);
        }
    }

  public:
    explicit SuggestionWorker(const QString &directory)
        : thread_([this, directory](std::stop_token stop) {
              try {
                  run(stop, directory);
              } catch (...) {
                  // An unavailable dictionary must leave native typing usable.
              }
          }) {}

    void offer(SuggestionRequest request) {
        std::lock_guard lock(mutex_);
        if (request_ == request)
            return;
        request_ = std::move(request);
        result_.reset();
        ++serial_;
        changed_.notify_one();
    }
    void cancel(quint64 generation) {
        std::lock_guard lock(mutex_);
        if (request_ && request_->generation == generation) {
            request_.reset();
            result_.reset();
            ++serial_;
        }
    }
    std::optional<Suggestion> ready(const SuggestionRequest &request, QChar boundary) {
        std::lock_guard lock(mutex_);
        if (request_ != request || !result_ || !result_->boundaries.contains(boundary))
            return std::nullopt;
        return result_;
    }
};

struct UndoEdit {
    Observation expected;
    QString original;
    guint replacementLength = 0;
    qint64 revision = 0;
};

struct PendingEdit {
    UndoEdit undo;
    reword::RetryCycle retry;
    Clock::time_point expires;
};

struct Context {
    Clock::time_point focused, deletionUntil;
    quint64 generation = 0;
    guint capabilities = 0;
    reword::InputMethodTarget target;
    std::optional<Observation> surrounding;
    std::optional<PendingEdit> pending;
    std::optional<UndoEdit> undo;
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
        undo.reset();
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
    SuggestionWorker suggestions;
    quint64 nextGeneration = 0;
    IBusEngine *focusedEngine = nullptr;

    explicit Runtime(const QString &directory) : store(directory, 0), suggestions(directory) {}

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
        if (focusedEngine != engine || context.focused == Clock::time_point::max()) {
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
        if (!witness.target.window) {
            trace(Trace::MissingWitness);
            return {};
        }
        if (witness.observed < context.focused) {
            trace(Trace::WitnessBeforeFocus);
            return {};
        }
        if (Clock::now() - witness.observed > witnessLifetime) {
            trace(Trace::ExpiredWitness);
            return {};
        }
        if (activeWindow() != witness.target.window) {
            trace(Trace::ForegroundChanged);
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

void clear(Context &context) {
    runtime->suggestions.cancel(context.generation);
    context.clear();
    context.generation = ++runtime->nextGeneration;
}

void focusIn(IBusEngine *engine) {
    trace(Trace::Focused);
    if (runtime->focusedEngine && runtime->focusedEngine != engine) {
        auto &previous = state(runtime->focusedEngine);
        clear(previous);
        previous.focused = Clock::time_point::max();
    }
    runtime->focusedEngine = engine;
    auto &context = state(engine);
    clear(context);
    context.focused = Clock::now();
}
void focusOut(IBusEngine *engine) {
    trace(Trace::Blurred);
    if (runtime->focusedEngine == engine)
        runtime->focusedEngine = nullptr;
    auto &context = state(engine);
    clear(context);
    context.focused = Clock::time_point::max();
}

gboolean dropUnavailableObservation(gpointer data) {
    auto &app = *static_cast<Runtime *>(data);
    if (auto *engine = app.focusedEngine) {
        auto &context = state(engine);
        try {
            if (context.target.window &&
                (!app.allowed(context.target) || app.activeWindow() != context.target.window))
                clear(context);
        } catch (...) {
            clear(context);
        }
    }
    return G_SOURCE_CONTINUE;
}

void reset(IBusEngine *engine) {
    trace(Trace::Reset);
    auto &context = state(engine);
    context.surrounding.reset();
    context.undo.reset();
    runtime->suggestions.cancel(context.generation);
    if (Clock::now() > context.deletionUntil)
        context.resetRetry();
    context.deletionUntil = {};
}

void capabilities(IBusEngine *engine, guint caps) {
    auto &context = state(engine);
    context.capabilities = caps;
    if (!(caps & IBUS_CAP_SURROUNDING_TEXT))
        clear(context);
}

void contentType(IBusEngine *engine, guint purpose, guint hints) {
    if (purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN ||
        (hints & IBUS_INPUT_HINT_PRIVATE))
        clear(state(engine));
}

void observe(IBusEngine *engine, IBusText *text, guint cursor, guint anchor) {
    auto &context = state(engine);
    try {
        const auto target = runtime->targetFor(engine, context);
        if (!runtime->allowed(target) || cursor != anchor) {
            clear(context);
            return;
        }
        if (!sameField(context.target, target)) {
            clear(context);
            context.target = target;
        }
        const char *utf8 = ibus_text_get_text(text);
        const auto bytes = utf8 ? strnlen(utf8, surroundingByteLimit + 1) : 0;
        if (bytes > surroundingByteLimit) {
            clear(context);
            return;
        }
        const auto value = QString::fromUtf8(utf8, bytes);
        const auto points = value.toUcs4();
        if (cursor > unsigned(points.size()) || points.size() > reword::MaxPassageCharacters) {
            clear(context);
            return;
        }
        const auto caret = QString::fromUcs4(
            reinterpret_cast<const char32_t *>(points.constData()), cursor).size();
        Observation observed{value, int(caret)};
        if (context.undo && observed != context.undo->expected)
            context.undo.reset();
        if (context.pending) {
            if (Clock::now() <= context.pending->expires &&
                observed == context.pending->undo.expected) {
                context.retry = context.pending->retry;
                context.undo = std::move(context.pending->undo);
                context.pending.reset();
            } else {
                context.resetRetry();
            }
        }
        if (context.retryStart >= 0 &&
            (caret < context.retryStart || value.left(context.retryStart) != context.prefix))
            context.resetRetry();
        context.surrounding = std::move(observed);
        runtime->suggestions.offer({context.generation,
            runtime->store.value("configRevision", 0).toLongLong(), *context.surrounding});
        trace(Trace::Snapshot);
    } catch (...) {
        clear(context);
    }
}

bool undoShortcut(guint key, guint modifiers) {
    if (!(modifiers & (IBUS_SHIFT_MASK | IBUS_CONTROL_MASK | IBUS_MOD1_MASK |
                       IBUS_MOD4_MASK | IBUS_SUPER_MASK | IBUS_META_MASK)) ||
        modifiers & (IBUS_MOD5_MASK | IBUS_HYPER_MASK))
        return false;
    const auto letter = QChar::toUpper(ibus_keyval_to_unicode(key));
    if (letter < 'A' || letter > 'Z')
        return false;
    const auto shortcut = QKeySequence::fromString(
        runtime->store.value("undoShortcut", "Ctrl+Alt+Z").toString(),
        QKeySequence::PortableText);
    if (shortcut.count() != 1 || shortcut[0].key() != int(letter))
        return false;
    Qt::KeyboardModifiers pressed;
    if (modifiers & IBUS_SHIFT_MASK)
        pressed |= Qt::ShiftModifier;
    if (modifiers & IBUS_CONTROL_MASK)
        pressed |= Qt::ControlModifier;
    if (modifiers & IBUS_MOD1_MASK)
        pressed |= Qt::AltModifier;
    if (modifiers & (IBUS_MOD4_MASK | IBUS_SUPER_MASK | IBUS_META_MASK))
        pressed |= Qt::MetaModifier;
    return shortcut[0].keyboardModifiers() == pressed;
}

bool applyUndo(IBusEngine *engine, Context &context, const reword::InputMethodTarget &target,
               const std::optional<Observation> &observed, Clock::time_point started) {
    auto undo = std::exchange(context.undo, std::nullopt);
    if (!undo || !observed) {
        trace(!undo ? Trace::UndoNoRecord : Trace::UndoNoObservation);
        return false;
    }
    if (*observed != undo->expected ||
        !sameField(runtime->targetFor(engine, context), target) || !runtime->allowed(target) ||
        runtime->store.value("configRevision", 0).toLongLong() != undo->revision ||
        Clock::now() - started > keyBudget) {
        trace(Trace::UndoRejected);
        return false;
    }
    auto *restored = ibus_text_new_from_string(undo->original.toUtf8().constData());
    if (!restored)
        return false;
    context.resetRetry();
    runtime->suggestions.cancel(context.generation);
    ibus_engine_delete_surrounding_text(engine, -int(undo->replacementLength),
                                       undo->replacementLength);
    ibus_engine_commit_text(engine, restored);
    trace(Trace::UndoApplied);
    return true;
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
            clear(context);
            return FALSE;
        }
        if (!sameField(context.target, target)) {
            clear(context);
            context.target = target;
        }
        // Only a verified, permitted field can request surrounding text.
        // The toolkit sends snapshots in later IBus callbacks.
        ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);
        // Pressing a physical modifier does not edit the surrounding text.
        // Keep the observation available for the following shortcut key.
        if (key == IBUS_KEY_Control_L || key == IBUS_KEY_Control_R ||
            key == IBUS_KEY_Alt_L || key == IBUS_KEY_Alt_R ||
            key == IBUS_KEY_Shift_L || key == IBUS_KEY_Shift_R ||
            key == IBUS_KEY_Meta_L || key == IBUS_KEY_Meta_R ||
            key == IBUS_KEY_Super_L || key == IBUS_KEY_Super_R)
            return FALSE;
        auto observed = std::exchange(context.surrounding, std::nullopt);
        if (undoShortcut(key, modifiers))
            return applyUndo(engine, context, target, observed, started);
        const auto code = ibus_keyval_to_unicode(key);
        const bool modified = modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK);
        context.deletionUntil = key == IBUS_KEY_BackSpace || key == IBUS_KEY_Delete
                                    ? started + 250ms : Clock::time_point{};
        if (key == IBUS_KEY_Left || key == IBUS_KEY_Right || key == IBUS_KEY_Up ||
            key == IBUS_KEY_Down || key == IBUS_KEY_Home || key == IBUS_KEY_End ||
            key == IBUS_KEY_Page_Up || key == IBUS_KEY_Page_Down) {
            context.resetRetry();
            context.undo.reset();
            return FALSE;
        }
        const QChar mark{char16_t(code)};
        const bool boundary = code <= 0xffff && QStringView(wordBoundaries).contains(mark);
        if (!observed || modified) {
            if (boundary && !modified)
                trace(Trace::NoSnapshot);
            return FALSE;
        }
        const auto revision = runtime->store.value("configRevision", 0).toLongLong();
        if (!boundary) {
            if (code >= 0x20 && context.retryStart >= 0 &&
                observed->caret > context.retryStart + context.lastToken.size())
                context.resetRetry();
            // Prepare the word while the next key is still in the future.
            // The prediction is usable only if the toolkit confirms it.
            if (QChar::isLetter(code) || QChar::isMark(code) || code == 0x200c) {
                const char32_t typed = code;
                const auto character = QString::fromUcs4(&typed, 1);
                observed->text.insert(observed->caret, character);
                observed->caret += character.size();
                runtime->suggestions.offer({context.generation, revision, std::move(*observed)});
            }
            return FALSE;
        }
        const auto suggestion = runtime->suggestions.ready(
            {context.generation, revision, *observed}, mark);
        if (!suggestion) {
            trace(Trace::NoPreparedEdit);
            return FALSE;
        }
        const auto &word = suggestion->word;
        const auto &corrected = suggestion->corrected;
        const auto prefix = observed->text.left(word.start);
        if (context.prefix != prefix || context.lastToken != word.token ||
            context.retryStart != word.start)
            context.resetRetry();
        if (corrected == word.token) {
            trace(Trace::NoCorrection);
            return FALSE;
        }

        auto retry = context.retry;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (!retry.shouldCorrect(target.field, word.start, word.token, now)) {
            context.retry = std::move(retry);
            trace(Trace::RetrySkipped);
            return FALSE;
        }
        if (!sameField(runtime->targetFor(engine, context), target) || !runtime->allowed(target) ||
            runtime->store.value("configRevision", 0).toLongLong() != revision ||
            Clock::now() - started > keyBudget) {
            trace(Trace::TimedOut);
            clear(context);
            return FALSE;
        }
        const auto replacement = corrected + mark;
        const auto expected = prefix + replacement + observed->text.mid(observed->caret);
        context.prefix = prefix;
        context.lastToken = word.token;
        context.retryStart = word.start;
        context.undo.reset();
        context.pending = PendingEdit{
            {{expected, int(prefix.size() + replacement.size())}, word.token + mark,
             guint(replacement.toUcs4().size()), revision},
            std::move(retry), Clock::now() + 300ms};
        const int length = word.token.toUcs4().size();
        const auto encoded = replacement.toUtf8();
        ibus_engine_delete_surrounding_text(engine, -length, length);
        ibus_engine_commit_text(engine, ibus_text_new_from_string(encoded.constData()));
        trace(Trace::Write);
        return TRUE;
    } catch (...) {
        clear(context);
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
        auto *context = reinterpret_cast<RewordIBusEngine *>(object)->context;
        if (runtime) {
            if (runtime->focusedEngine == IBUS_ENGINE(object))
                runtime->focusedEngine = nullptr;
            runtime->suggestions.cancel(context->generation);
        }
        delete context;
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
        const auto resetRuntime = qScopeGuard([] { runtime = nullptr; });
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
        const auto sweep = g_timeout_add(125, dropUnavailableObservation, &state);
        g_main_loop_run(loop.get());
        g_source_remove(sweep);
        g_signal_handlers_disconnect_by_data(bus.get(), loop.get());
        return 0;
    } catch (...) {
        qWarning("Reword IBus could not initialize local correction.");
        return 1;
    }
}
