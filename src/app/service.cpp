#include "service.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QElapsedTimer>
namespace reword {
void Service::start() {
    try {
        store_ = std::make_unique<Store>(directory_);
        engine_ = std::make_unique<Engine>();
        if (!adapter_)
            adapter_ = createAdapter();
        wayland_ = qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland";
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &Service::tick);
        timer_->start(80);
    } catch (...) {
        emit message("Native integration could not start. Use the writing workspace.");
    }
}
void Service::stop() {
    cancel();
    if (job_) {
        job_->disconnect(this);
        job_->waitForFinished();
        delete job_;
        job_ = nullptr;
    }
    jobLock_.reset();
    if (timer_)
        timer_->stop();
    previous_ = {};
    undoAfter_ = {};
    undoBefore_.clear();
    adapter_.reset();
    engine_.reset();
    store_.reset();
    shortcutSettings_.clear();
    workingShortcuts_.clear();
    shortcutsHealthy_ = false;
    shortcutRevision_ = -1;
}
Field Service::permitted() {
    auto f = adapter_->inspect(false);
    if (f.app.isEmpty())
        return {};
    store_->discover(f.app, QFileInfo(f.app).fileName(), restrictedApp(f.app));
    if (store_->shouldAsk(f.app) && (lastAsk_ != f.app || askDate_ != QDate::currentDate())) {
        lastAsk_ = f.app;
        askDate_ = QDate::currentDate();
        emit application(f.app, QFileInfo(f.app).fileName());
    }
    Access access{store_->allowed(f.app), browserApp(f.app), store_->paused()};
    if (!safe(f, access, store_->options()))
        return {};
    auto read = adapter_->inspect(true);
    access = {store_->allowed(f.app), browserApp(f.app), store_->paused()};
    if (read.id != f.id || read.app != f.app || !safe(read, access, store_->options()))
        return {};
    return read;
}
bool Service::policyCurrent(const Field &field, qint64 revision, quint64 cancellation) {
    const Access access{store_->allowed(field.app), browserApp(field.app), store_->paused()};
    return safe(field, access, store_->options()) &&
           revision == store_->value("configRevision", 0).toLongLong() &&
           cancellation == cancellationRevision_.load();
}
void Service::tick() {
    if (!adapter_ || !store_)
        return;
    try {
        auto revision = store_->value("configRevision", 0).toLongLong();
        auto settings = store_->shortcuts();
        if (settings != shortcutSettings_ ||
            (!wayland_ && !shortcutsHealthy_ && revision != shortcutRevision_)) {
            bool hadAttempt = !shortcutSettings_.isEmpty();
            // Exceptions or newer settings must leave this attempt eligible for retry.
            shortcutSettings_.clear();
            shortcutRevision_ = revision;
            shortcutsHealthy_ = false;
            bool ok = adapter_->shortcuts(settings[0], settings[1], settings[2]);
            shortcutsHealthy_ = ok;
            if (ok) {
                workingShortcuts_ = settings;
                shortcutSettings_ = settings;
            } else if (!workingShortcuts_.isEmpty()) {
                bool restored = adapter_->shortcuts(workingShortcuts_[0], workingShortcuts_[1],
                                                    workingShortcuts_[2]);
                shortcutsHealthy_ = restored;
                if (store_->restoreShortcuts(settings, revision, workingShortcuts_)) {
                    shortcutSettings_ = workingShortcuts_;
                    shortcutRevision_ = revision + 1;
                    cancel();
                    emit message(
                        restored ? "This shortcut set is unavailable or already in use. Previous "
                                   "shortcuts were restored."
                                 : "This shortcut set could not be registered. Previous settings "
                                   "were restored, but global shortcuts are unavailable.");
                }
                // A newer edit survives the compare-and-set and is checked next tick.
                ok = restored;
            } else {
                shortcutSettings_ = settings;
                if (hadAttempt && !wayland_)
                    emit message(
                        "Global shortcuts are unavailable in this environment or already in use.");
            }
            shortcutsHealthy_ = ok;
            lastDescription_ = adapter_->description();
            emit state(lastDescription_, ok);
        }
        auto now = QDateTime::currentMSecsSinceEpoch();
        if (now - platformPoll_ >= 1000) {
            platformPoll_ = now;
            auto description = adapter_->description();
            if (description != lastDescription_) {
                lastDescription_ = description;
                emit state(description, shortcutsHealthy_);
            }
        }
        auto a = adapter_->takeAction();
        if (!a.isEmpty()) {
            action(a);
            return;
        }
        if (!adapter_->changed())
            return;
        auto f = permitted();
        if (f.id.isEmpty()) {
            previous_ = {};
            undoAfter_ = {};
            undoBefore_.clear();
            retry_.reset();
            retryStart_ = -1;
            return;
        }
        if (f.id != previous_.id) {
            undoAfter_ = {};
            undoBefore_.clear();
            retry_.reset();
            retryStart_ = -1;
            previous_ = f;
            return;
        }
        if (f.text == previous_.text) {
            if (f.caret != previous_.caret)
                retry_.reset();
            previous_ = f;
            return;
        }
        auto old = previous_;
        previous_ = f;
        if (retryStart_ >= 0) {
            static const QRegularExpression continued(R"([\s.,!?،؛؟]+[^\s.,!?،؛؟])");
            if (f.caret < retryStart_ || f.text.left(retryStart_) != retryPrefix_ ||
                continued.match(f.text.mid(retryStart_, f.caret - retryStart_)).hasMatch()) {
                retry_.reset();
                retryStart_ = -1;
            }
        }
        if (jobRunning_)
            return;
        if (wayland_ || f.caret <= 0 || f.selectionStart >= 0)
            return;
        if (f.text.size() < old.text.size())
            return; // Deleting a correction keeps its retry session.
        if (f.caret > f.text.size())
            return;
        const auto completed = completedWords(old, f);
        if (completed.isEmpty())
            return;
        const auto cancellation = cancellationRevision_.load();
        const auto operationRevision = store_->value("configRevision", 0).toLongLong();
        auto o = store_->options();
        o.abbreviations = false;
        engine_->setPersonal(store_->words(), store_->replacements());
        int offset = 0;
        for (const auto &word : completed) {
            int start = word.start + offset;
            const auto &token = word.token;
            auto correct = engine_->word(token, o, f.text.left(start).right(128));
            if (correct == token) {
                if (retryStart_ == start && token != retryWord_) {
                    retry_.reset();
                    retryStart_ = -1;
                }
                continue;
            }
            auto savedRetry = retry_;
            retryStart_ = start;
            retryPrefix_ = f.text.left(start);
            retryWord_ = token;
            if (!retry_.shouldCorrect(f.id, start, token, QDateTime::currentMSecsSinceEpoch()))
                continue;
            QString updated = f.text;
            updated.replace(start, token.size(), correct);
            int caret = f.caret + correct.size() - token.size();
            if (!policyCurrent(f, operationRevision, cancellation) ||
                !adapter_->replace(f, updated, caret)) {
                retry_ = savedRetry;
                break;
            }
            undoPassage_ = false;
            undoBefore_ = f.text;
            undoCaret_ = f.caret;
            undoAfter_ = adapter_->inspect(true);
            previous_ = undoAfter_;
            store_->count(token, correct, o);
            emit corrected(token, correct);
            if (undoAfter_.id != f.id || undoAfter_.app != f.app || undoAfter_.text != updated ||
                undoAfter_.caret != caret)
                break;
            f = undoAfter_;
            offset += correct.size() - token.size();
        }

    } catch (...) {
        previous_ = {};
        retry_.reset();
        emit state(lastDescription_, shortcutsHealthy_);
        emit message("An integration operation failed safely. No further edit was attempted.");
    }
}
void Service::runText(QString input, bool native, Field field) {
    if (jobRunning_) {
        if (native)
            emit message("A correction is already running.");
        else
            emit result(input, input, "A correction is already running.", false);
        return;
    }
    if (!store_) {
        emit result(input, input, "Correction engine is unavailable.", native);
        return;
    }
    if (store_->paused()) {
        emit result(input, input, "Reword is paused.", native);
        return;
    }
    auto operationLock = std::make_unique<QLockFile>(directory_ + "/operation.lock");
    operationLock->setStaleLockTime(15000);
    if (!operationLock->tryLock(0)) {
        emit result(input, input, "Another correction is already running.", native);
        return;
    }
    jobLock_ = std::move(operationLock);
    jobRunning_ = true;
    const auto cancellation = cancellationRevision_.load();
    const auto revision = store_->value("configRevision", 0).toLongLong();
    auto options = store_->options();
    auto words = store_->words();
    auto replacements = store_->replacements();
    job_ = new QFutureWatcher<Change>(this);
    connect(
        job_, &QFutureWatcher<Change>::finished, this,
        [this, input, native, field, revision, cancellation] {
            auto operationLock = std::move(jobLock_);
            auto completed = job_;
            job_ = nullptr;
            jobRunning_ = false;
            completed->deleteLater();
            try {
                auto change = completed->result();
                if (cancel_.load() || cancellation != cancellationRevision_.load() ||
                    store_->paused() || revision != store_->value("configRevision", 0).toLongLong())
                    change.error = "Cancelled or settings changed. Nothing was replaced.";
                if (!change.error.isEmpty()) {
                    emit result(input, input, change.error, native);
                    return;
                }
                if (!native) {
                    emit result(input, change.corrected, {}, false);
                    return;
                }
                if (!change.changed) {
                    emit message("No confident corrections found.");
                    return;
                }
                try {
                    auto current = permitted();
                    if (!sameTarget(field, current)) {
                        emit message("The selection changed. Nothing was replaced.");
                        return;
                    }
                    QString updated = field.text;
                    updated.replace(field.selectionStart, field.selectionEnd - field.selectionStart,
                                    change.corrected);
                    if (!policyCurrent(current, revision, cancellation)) {
                        emit message("Cancelled or settings changed. Nothing was replaced.");
                        return;
                    }
                    if (!adapter_->replace(field, updated,
                                           field.selectionStart + change.corrected.size())) {
                        emit message(
                            "The application did not confirm the replacement. Check its text "
                            "before continuing.");
                        return;
                    }
                    undoPassage_ = true;
                    undoBefore_ = field.text;
                    undoCaret_ = field.caret;
                    undoAfter_ = adapter_->inspect(true);
                    previous_ = undoAfter_;
                    emit passageApplied(input, change.corrected);
                    store_->history(input, change.corrected, store_->options());
                    emit result(input, change.corrected, {}, true);
                } catch (...) {
                    emit message("The operation stopped safely.");
                }
            } catch (...) {
                emit result(input, input, "Local storage is unavailable. The operation stopped.",
                            native);
            }
        });
    job_->setFuture(QtConcurrent::run([this, input, options, words, replacements] {
        try {
            QElapsedTimer elapsed;
            elapsed.start();
            Engine engine;
            engine.setPersonal(words, replacements);
            return engine.text(input, options, &cancel_,
                               std::max(qint64(0), 10000 - elapsed.elapsed()));
        } catch (...) {
            return Change{input, input, false, "Correction failed. The original is unchanged."};
        }
    }));
}
void Service::processText(QString input) {
    try {
        runText(input, false);
    } catch (...) {
        jobRunning_ = false;
        jobLock_.reset();
        emit result(input, input, "Correction could not start. Check local storage access.", false);
    }
}
void Service::fixSelection() {
    if (jobRunning_) {
        emit message("A correction is already running.");
        return;
    }
    cancel_.store(false);
    auto field = permitted();
    if (field.id.isEmpty() || field.selectionStart < 0 ||
        field.selectionEnd <= field.selectionStart) {
        emit message("No supported, permitted text selection. Use the writing workspace instead.");
        return;
    }
    runText(field.text.mid(field.selectionStart, field.selectionEnd - field.selectionStart), true,
            field);
}
void Service::undo() {
    const auto cancellation = cancellationRevision_.load();
    const auto revision = store_->value("configRevision", 0).toLongLong();
    auto f = permitted();
    if (undoBefore_.isEmpty() || !sameTarget(undoAfter_, f)) {
        emit message(undoPassage_ ? "Undo is no longer safe. Copy the latest passage original from "
                                    "session recovery in Reword."
                                  : "Undo is no longer safe. Nothing was replaced.");
        return;
    }
    if (!policyCurrent(f, revision, cancellation)) {
        emit message("Cancelled or settings changed. Nothing was replaced.");
        return;
    }
    if (adapter_->replace(f, undoBefore_, undoCaret_)) {
        previous_ = adapter_->inspect(true);
        undoBefore_.clear();
        undoAfter_ = {};
        emit message("Correction restored.");
    }
}
void Service::action(QString name) {
    try {
        if (!adapter_ || !store_)
            return;
        if (name == "fix")
            fixSelection();
        else if (name == "undo")
            undo();
        else if (name == "pause") {
            store_->pause(store_->paused() ? 0 : -1);
            if (store_->paused())
                cancel();
            previous_ = {};
            retry_.reset();
            emit message(store_->paused() ? "Reword paused." : "Reword resumed.");
        }
    } catch (...) {
        if (!job_) {
            jobRunning_ = false;
            jobLock_.reset();
        }
        emit message("The operation stopped safely.");
    }
}
} // namespace reword
