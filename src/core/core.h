#pragma once
#include <QString>
#include <QDateTime>
#include <QSet>
#include <QHash>
#include <memory>
#include <atomic>
#include <functional>

namespace reword {
struct Options {
    bool english = true, persian = true, spelling = true, abbreviations = true;
    bool capitalization = false, normalizePersian = false, halfSpace = false;
    bool periods = false, commas = false, spacing = false;
    bool paragraphPeriods = false, introductoryCommas = false, questionMarks = false,
         exclamationMarks = false, semicolons = false, colons = false;
    bool history = true, statistics = false, notifications = false, browsers = true;
    int sensitivity = 0, historyCount = 100, historyMiB = 20, historyDays = 30,
        maxCharacters = 50000;
};
struct Field {
    QString app, id, text;
    int caret = -1, selectionStart = -1, selectionEnd = -1;
    bool known = false, password = false, editable = false, focused = false, locked = true,
         plain = false;
    quint64 revision = 0;
};
struct CompletedWord {
    QString token;
    int start = 0;
};
QVector<CompletedWord> completedWords(const Field &before, const Field &after);
struct Access {
    bool allowed = false;
    bool browser = false;
    bool paused = false;
};
bool safe(const Field &, const Access &, const Options &);
bool sameTarget(const Field &, const Field &);
struct Change {
    QString original, corrected;
    bool changed = false;
    QString error;
};
class RetryCycle {
    QString field_, word_;
    int location_ = -1, attempts_ = 0;
    qint64 time_ = 0;

  public:
    bool shouldCorrect(const QString &field, int location, const QString &word, qint64 now);
    void reset();
};
class Engine {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Engine(const QString &englishDir = {}, const QString &persianDir = {});
    ~Engine();
    QString word(const QString &, const Options &, const QString &context = {});
    Change text(const QString &, const Options &, const std::atomic_bool *cancel = nullptr,
                qint64 timeoutMs = 10000);
    void setPersonal(const QSet<QString> &words, const QHash<QString, QString> &replacements);
    bool englishAvailable() const;
    bool persianAvailable() const;
};
} // namespace reword
