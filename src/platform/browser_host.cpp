#include "core/store.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

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
    static int remaining = qEnvironmentVariableIsSet("REWORD_BROWSER_HOST_DIAGNOSTICS") ? 128 : 0;
    if (remaining > 0) {
        --remaining;
        std::fprintf(stderr, "REWORD_BROWSER_HOST %d\n", int(stage));
    }
}

QString browserApplication() {
    const auto path = QFileInfo(QString("/proc/%1/exe").arg(getppid())).symLinkTarget();
    const auto name = QFileInfo(path).fileName();
    if (name == "helium" || name == "chrome" || name == "chromium")
        return path;
    return {};
}

bool allowedOrigin(const QString &origin) {
    static const QRegularExpression extensionOrigin(R"(\Achrome-extension://[a-p]{32}/\z)");
    return extensionOrigin.match(origin).hasMatch() &&
           origin == qEnvironmentVariable("REWORD_BROWSER_ORIGIN");
}

bool writeResponse(const QJsonObject &response) {
    const auto payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const auto size = quint32(payload.size());
    return std::fwrite(&size, sizeof size, 1, stdout) == 1 &&
           std::fwrite(payload.constData(), 1, size, stdout) == size && std::fflush(stdout) == 0;
}

class BrowserCorrection {
    const QString application_;
    reword::Store store_;
    reword::Engine engine_;
    reword::Options options_;
    qint64 settingsRevision_ = -1;

    bool permitted() const {
        return !store_.paused() && store_.allowed(application_) &&
               store_.value("browsers", true).toBool();
    }

  public:
    BrowserCorrection(const QString &application, const QString &directory)
        : application_(application), store_(directory, 0) {}

    QJsonObject suggest(const QByteArray &payload) {
        const QDeadlineTimer deadline(MaximumSuggestionAgeMs, Qt::PreciseTimer);
        const auto revision = store_.value("configRevision", 0).toLongLong();
        if (!permitted()) {
            trace(Stage::Blocked);
            return {};
        }
        const auto request = QJsonDocument::fromJson(payload).object();
        const auto expectedRevision = QString::number(revision);
        if (request.size() == 1 && request.value("policy").toBool())
            return {{"revision", expectedRevision}};
        const auto text = request.value("text").toString();
        const auto caret = request.value("caret").toInt(-1);
        const auto boundary = request.value("boundary").toString();
        if (request.size() != 4 || request.value("revision").toString() != expectedRevision ||
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
        return {{"start", word->start}, {"length", word->token.size()}, {"corrected", corrected}};
    }
};
}

int main(int argc, char **argv) {
    umask(0077);
    trace(Stage::Started);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const auto application = browserApplication();
    if (application.isEmpty() || args.size() != 2 || !allowedOrigin(args[1]))
        return 2;
    trace(Stage::Verified);
    auto directory = qEnvironmentVariable("REWORD_DATA_DIR");
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reword";
    try {
        BrowserCorrection correction(application, directory);
        trace(Stage::Ready);
        for (;;) {
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
