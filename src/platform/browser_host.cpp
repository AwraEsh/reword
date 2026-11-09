#include "core/store.h"
#include "browser_extension_origin.h"
#include "browser_registration.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <memory>
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include "x11_ibus_undo_route.h"
#include <X11/extensions/XRes.h>
#endif

namespace {
constexpr quint32 MaxFrameBytes = 4096;
constexpr int MaxFieldUnits = 384;
constexpr int MaxCorrectionUnits = 128;
constexpr qint64 MaximumSuggestionAgeMs = 150;

enum class Stage {
    Started = 1, Verified, Ready, Received, Replied, Blocked, InvalidRequest, NoWord,
    NoCorrection, PolicyChanged, Deadline,
};

void trace(Stage stage) {
    static int remaining = qEnvironmentVariableIsSet("REWORD_BROWSER_HOST_DIAGNOSTICS") ? 256 : 0;
    if (remaining > 0) {
        --remaining;
        std::fprintf(stderr, "REWORD_BROWSER_HOST %d\n", int(stage));
    }
}

QString browserApplication() {
#ifdef _WIN32
    // Chrome owns the two pipe servers even when Windows starts the host
    // through cmd.exe. Both channels must identify the same live browser.
    ULONG inputServer = 0, outputServer = 0;
    if (!GetNamedPipeServerProcessId(GetStdHandle(STD_INPUT_HANDLE), &inputServer) ||
        !GetNamedPipeServerProcessId(GetStdHandle(STD_OUTPUT_HANDLE), &outputServer) ||
        !inputServer || inputServer != outputServer || inputServer == GetCurrentProcessId())
        return {};
    std::unique_ptr<void, decltype(&CloseHandle)> process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, inputServer),
        CloseHandle);
    if (!process || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT)
        return {};
    FILETIME browserCreated{}, hostCreated{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.get(), &browserCreated, &exited, &kernel, &user) ||
        !GetProcessTimes(GetCurrentProcess(), &hostCreated, &exited, &kernel, &user) ||
        CompareFileTime(&browserCreated, &hostCreated) > 0)
        return {};
    wchar_t image[32768];
    DWORD length = 32768;
    if (!QueryFullProcessImageNameW(process.get(), 0, image, &length))
        return {};
    const auto path = QString::fromWCharArray(image, length);
    const auto name = QFileInfo(path).fileName().toLower();
    if (name == "chrome.exe" || name == "chromium.exe")
        return path;
#else
    const auto path = QFileInfo(QString("/proc/%1/exe").arg(getppid())).symLinkTarget();
    const auto name = QFileInfo(path).fileName();
    if (name == "helium" || name == "chrome" || name == "chromium")
        return path;
#endif
    return {};
}

bool allowedInvocation(const QStringList &arguments) {
#ifdef _WIN32
    constexpr auto windowOption = u"--parent-window=";
    if (arguments.size() != 3 || !arguments[2].startsWith(windowOption))
        return false;
    bool valid = false;
    QStringView(arguments[2]).sliced(QStringView(windowOption).size()).toLongLong(&valid);
    if (!valid)
        return false;
#else
    if (arguments.size() != 2)
        return false;
#endif
    return arguments[1] == QLatin1StringView(reword::BrowserExtensionOrigin);
}

QString dataDirectory() {
    const auto directory = qEnvironmentVariable("REWORD_DATA_DIR");
    return directory.isEmpty() ?
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reword" : directory;
}

bool writeResponse(const QJsonObject &response) {
    const auto payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const auto size = quint32(payload.size());
    return std::fwrite(&size, sizeof size, 1, stdout) == 1 &&
           std::fwrite(payload.constData(), 1, size, stdout) == size && std::fflush(stdout) == 0;
}

#ifndef _WIN32
class BrowserUndoRoute {
    Display *display_ = nullptr;
    Window owner_ = None, browserWindow_ = None;
    Atom route_ = None, activeWindow_ = None;
    quint32 issued_ = 0;

  public:
    QString attempt;
    qint64 revision = -1;

    ~BrowserUndoRoute() {
        if (display_)
            XCloseDisplay(display_);
    }

    void clear() {
        if (!owner_)
            return;
        // Destroying our own window releases selection ownership without
        // disturbing another browser host that might now own the selection.
        XDestroyWindow(display_, owner_);
        XFlush(display_);
        owner_ = None;
        attempt.clear();
    }

    bool current() const {
        return owner_ && reword::x11RouteClock() - issued_ < reword::BrowserUndoRouteLifetimeMs &&
               reword::x11RootValue(display_, activeWindow_, XA_WINDOW) == browserWindow_ &&
               XGetSelectionOwner(display_, route_) == owner_;
    }

    bool claim(pid_t browserPid, const QString &offeredAttempt, qint64 offeredRevision) {
        clear();
        if (!display_) {
            if (qEnvironmentVariable("XDG_SESSION_TYPE") != "x11")
                return false;
            display_ = XOpenDisplay(nullptr);
            if (!display_)
                return false;
            // The foreground window can disappear while we query its client ID.
            XSetErrorHandler(+[](Display *, XErrorEvent *) { return 0; });
            activeWindow_ = XInternAtom(display_, "_NET_ACTIVE_WINDOW", False);
            route_ = XInternAtom(display_, reword::BrowserUndoRouteAtom, False);
        }
        browserWindow_ = reword::x11RootValue(display_, activeWindow_, XA_WINDOW);
        if (!browserWindow_ || browserPid <= 1)
            return false;
        XResClientIdSpec request{browserWindow_, XRES_CLIENT_ID_PID_MASK};
        long count = 0;
        XResClientIdValue *identities = nullptr;
        XResQueryClientIds(display_, 1, &request, &count, &identities);
        bool owned = false;
        if (identities) {
            for (long i = 0; i < count && i < 8; ++i)
                owned |= XResGetClientPid(&identities[i]) == browserPid;
            XResClientIdsDestroy(count, identities);
        }
        if (!owned || reword::x11RootValue(display_, activeWindow_, XA_WINDOW) != browserWindow_)
            return false;

        const auto root = DefaultRootWindow(display_);
        owner_ = XCreateSimpleWindow(display_, root, 0, 0, 1, 1, 0, 0, 0);
        issued_ = reword::x11RouteClock();
        const unsigned long values[] = {owner_, browserWindow_, issued_};
        XChangeProperty(display_, root, route_, XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(values), 3);
        XSetSelectionOwner(display_, route_, owner_, CurrentTime);
        if (!current()) {
            clear();
            return false;
        }
        attempt = offeredAttempt;
        revision = offeredRevision;
        return true;
    }
};
#endif

class BrowserCorrection {
    const QString application_;
    reword::Store store_;
    reword::Engine engine_;
    reword::Options options_;
    qint64 settingsRevision_ = -1;
    reword::RetryCycle retry_;
    QElapsedTimer clock_;
    quint64 lastOffer_ = 0;
    qint64 lastOfferRevision_ = -1;
#ifndef _WIN32
    bool offerCanClaimUndo_ = false;
    BrowserUndoRoute route_;
#endif

    bool permitted() const {
        return !store_.paused() && store_.allowed(application_) &&
               store_.value("browsers", true).toBool();
    }

  public:
    BrowserCorrection(const QString &application, const QString &directory)
        : application_(application), store_(directory, 0) {
        clock_.start();
    }

#ifndef _WIN32
    bool maintainRoute() {
        if (route_.attempt.isEmpty())
            return false;
        if (route_.current() && permitted() &&
            route_.revision == store_.value("configRevision", 0).toLongLong())
            return true;
        route_.clear();
        return false;
    }
#endif

    QJsonObject suggest(const QByteArray &payload) {
        const QDeadlineTimer deadline(MaximumSuggestionAgeMs, Qt::PreciseTimer);
        const auto revision = store_.value("configRevision", 0).toLongLong();
        if (!permitted()) {
#ifndef _WIN32
            route_.clear();
#endif
            trace(Stage::Blocked);
            return {};
        }
        const auto request = QJsonDocument::fromJson(payload).object();
        if (request.size() == 1 && request.value("releaseUndo").isString()) {
#ifndef _WIN32
            if (route_.attempt == request.value("releaseUndo").toString())
                route_.clear();
#endif
            return {};
        }
        const auto expectedRevision = QString::number(revision);
        if (request.size() == 2 && request.value("claimUndo").isString()) {
#ifdef _WIN32
            return {{"claimed", false}};
#else
            const auto attempt = request.value("claimUndo").toString();
            const bool claimed = offerCanClaimUndo_ && lastOfferRevision_ == revision &&
                request.value("revision").toString() == expectedRevision &&
                attempt == QString::number(lastOffer_) && route_.claim(getppid(), attempt, revision);
            if (claimed)
                offerCanClaimUndo_ = false;
            return {{"claimed", claimed}};
#endif
        }
        if (request.size() == 1 && request.value("policy").toBool()) {
            const auto undo = store_.value("undoShortcut", "Ctrl+Alt+Z").toString();
            if (store_.value("configRevision", 0).toLongLong() != revision)
                return {};
            return {{"revision", expectedRevision}, {"undoShortcut", undo}};
        }
        const auto text = request.value("text").toString();
        const auto caret = request.value("caret").toInt(-1);
        const auto boundary = request.value("boundary").toString();
        const auto continuation = request.value("continuation");
        if (request.size() != (continuation.isString() ? 5 : 4) ||
            request.value("revision").toString() != expectedRevision ||
            text.isEmpty() || text.size() >= MaxFieldUnits ||
            boundary.size() != 1 || !QStringView(u" .,!؟?،").contains(boundary.front())) {
            trace(Stage::InvalidRequest);
            return {};
        }
        const auto word = reword::wordBeforeBoundary(text, caret, boundary.front());
        if (!word) {
            trace(Stage::NoWord);
            return {};
        }
        if (settingsRevision_ != revision) {
            options_ = store_.options();
            engine_.setPersonal(store_.words(), store_.replacements());
            settingsRevision_ = revision;
        }
        const auto corrected = engine_.word(word->token, options_,
                                            text.left(word->start).right(128));
        if (corrected == word->token || corrected.isEmpty() || corrected.size() > MaxCorrectionUnits) {
            trace(Stage::NoCorrection);
            return {};
        }
        if (!permitted() || store_.value("configRevision", 0).toLongLong() != revision) {
            trace(Stage::PolicyChanged);
            return {};
        }
        if (deadline.hasExpired()) {
            trace(Stage::Deadline);
            return {};
        }
        if (!lastOffer_ || lastOfferRevision_ != revision ||
            continuation.toString() != QString::number(lastOffer_))
            retry_.reset();
        // The host keeps the retry decision without keeping the original word.
        const auto wordKey = QString::fromLatin1(
            QCryptographicHash::hash(word->token.toUtf8(), QCryptographicHash::Sha256).toHex());
        const bool apply = retry_.shouldCorrect(application_, word->start, wordKey, clock_.elapsed());
#ifndef _WIN32
        offerCanClaimUndo_ = apply;
#endif
        lastOfferRevision_ = revision;
        QJsonObject response{{"start", word->start}, {"length", word->token.size()},
                             {"attempt", QString::number(++lastOffer_)}};
        if (apply)
            response.insert("corrected", corrected);
        else
            response.insert("skipped", true);
        return response;
    }
};
}

int main(int argc, char **argv) {
#ifndef _WIN32
    umask(0077);
#endif
    trace(Stage::Started);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
#ifdef _WIN32
    if (args.size() == 3 && (args[1] == "--register-browser" || args[1] == "--unregister-browser")) {
        const bool remove = args[1] == "--unregister-browser";
        try {
            reword::registerWindowsBrowserHost(args[2], dataDirectory(), app.applicationFilePath(), remove);
            std::puts(remove ? "Reword browser registration removed." : "Reword browser host registered.");
            return 0;
        } catch (const std::exception &error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
#else
    const bool packaged = args.size() == 5 && args[1] == "--register-browser" &&
                          args[3] == "--launcher";
    if (packaged || (args.size() == 3 &&
                     (args[1] == "--register-browser" || args[1] == "--unregister-browser"))) {
        const bool remove = args[1] == "--unregister-browser";
        const QFileInfo executable(app.applicationFilePath());
        auto launchPath = executable.absoluteFilePath();
        if (packaged) {
            const QFileInfo launcher(args[4]);
            launchPath = launcher.canonicalFilePath();
            if (launcher.fileName() != "reword-browser-launcher" ||
                launcher.isSymLink() || launcher.canonicalPath() != executable.canonicalPath() ||
                !QFileInfo(launchPath).isExecutable()) {
                std::fputs("Use the browser launcher from the same Reword installation.\n", stderr);
                return 2;
            }
        }
        try {
            reword::registerBrowserHost(args[2], launchPath, remove);
            std::puts(remove ? "Reword browser registration removed." : "Reword browser host registered.");
            return 0;
        } catch (const std::exception &error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
#endif
    if (!allowedInvocation(args))
        return 2;
    const auto application = browserApplication();
    if (application.isEmpty())
        return 2;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1)
        return 2;
#endif
    trace(Stage::Verified);
    try {
        BrowserCorrection correction(application, dataDirectory());
        std::setvbuf(stdin, nullptr, _IONBF, 0);
        trace(Stage::Ready);
        for (;;) {
#ifndef _WIN32
            if (correction.maintainRoute()) {
                pollfd input{STDIN_FILENO, POLLIN, 0};
                const auto ready = poll(&input, 1, 150);
                if (!ready || (ready < 0 && errno == EINTR))
                    continue;
                if (ready < 0)
                    return 2;
            }
#endif
            quint32 size = 0;
            const auto header = std::fread(&size, 1, sizeof size, stdin);
            if (!header && std::feof(stdin))
                return 0;
            if (header != sizeof size || !size || size > MaxFrameBytes)
                return 2;
            QByteArray payload(size, Qt::Uninitialized);
            if (std::fread(payload.data(), 1, size, stdin) != size)
                return 2;
            trace(Stage::Received);
            if (!writeResponse(correction.suggest(payload)))
                return 1;
            trace(Stage::Replied);
        }
    } catch (...) {
        return 1;
    }
}
