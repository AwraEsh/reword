#include "service.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QDeadlineTimer>
#include <QScopeGuard>
namespace reword {
void Service::clearJobTarget() {
    nativeJob_ = false;
    nativeJobApp_.clear();
    nativeJobField_.clear();
}
void Service::cancelNativeJob() {
    if (jobRunning_ && nativeJob_ && !cancel_.load())
        cancel();
}
void Service::resetRetry() {
    retry_.reset();
    retryStart_ = -1;
    retryPrefix_.clear();
    retryWord_.clear();
}
void Service::resetObservation() {
    previous_ = {};
    undoAfter_ = {};
    undoBefore_.clear();
    undoCaret_ = 0;
    undoPassage_ = false;
    resetRetry();
}
void Service::start() {
    try {
        store_ = std::make_unique<Store>(directory_);
        engine_ = std::make_unique<Engine>();
        if (!adapter_)
            adapter_ = createAdapter();
        wayland_ = qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland";
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &Service::tick);
        timer_->start(20);
    } catch (...) {
        emit message("Native integration could not start. Use the writing workspace.");
    }
}
void Service::stop() {
    const bool wasRunning = jobRunning_;
    cancel();
    if (job_) {
        job_->disconnect(this);
        job_->waitForFinished();
        delete job_;
        job_ = nullptr;
    }
    jobLock_.reset();
    jobRunning_ = false;
    clearJobTarget();
    if (timer_)
        timer_->stop();
    resetObservation();
    adapter_.reset();
    passageEngine_.reset();
    engine_.reset();
    store_.reset();
    shortcutSettings_.clear();
    workingShortcuts_.clear();
    shortcutsHealthy_ = false;
    shortcutRevision_ = -1;
    if (wasRunning)
        emit operationActive(false);
}
Field Service::permitted(const Field *expected) {
    auto f = adapter_->inspect(false);
    if (f.app.isEmpty())
        return {};
    if (expected && (f.id != expected->id || f.app != expected->app))
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
    auto read = adapter_->readExpected(f);
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
        if (a == "pending") {
            auto field = adapter_->inspect(false);
            // A passive shortcut grab can temporarily unset only the focus
            // state. Preserve Undo across that interval, but never read text.
            // All other policy/identity boundaries still erase observation.
            field.focused = true;
            const Access access{store_->allowed(field.app), browserApp(field.app), store_->paused()};
            if (jobRunning_ && nativeJob_ &&
                (!safe(field, access, store_->options()) || field.app != nativeJobApp_ ||
                 field.id != nativeJobField_))
                cancelNativeJob();
            if (!safe(field, access, store_->options()) || field.id != previous_.id ||
                field.app != previous_.app) {
                resetObservation();
            }
            return;
        }
        if (!a.isEmpty()) {
            action(a);
            return;
        }
        if (!adapter_->changed())
            return;
        Field jobTarget;
        jobTarget.app = nativeJobApp_;
        jobTarget.id = nativeJobField_;
        auto f = permitted(jobRunning_ && nativeJob_ ? &jobTarget : nullptr);
        if (f.id.isEmpty()) {
            cancelNativeJob();
            resetObservation();
            return;
        }
        if (f.id != previous_.id) {
            resetObservation();
            previous_ = f;
            return;
        }
        if (f.text == previous_.text) {
            if (f.caret != previous_.caret)
                resetRetry();
            previous_ = f;
            return;
        }
        auto old = previous_;
        previous_ = f;
        if (retryStart_ >= 0) {
            static const QRegularExpression continued(R"([\s.,!?،؛؟]+[^\s.,!?،؛؟])");
            if (f.caret < retryStart_ || f.text.left(retryStart_) != retryPrefix_ ||
                continued.match(f.text.mid(retryStart_, f.caret - retryStart_)).hasMatch()) {
                resetRetry();
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
                    resetRetry();
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
            auto after = permitted(&f);
            if (after.id != f.id || after.app != f.app || after.text != updated ||
                after.caret != caret) {
                resetObservation();
                break;
            }
            undoPassage_ = false;
            undoBefore_ = f.text;
            undoCaret_ = f.caret;
            undoAfter_ = after;
            previous_ = undoAfter_;
            store_->count(token, correct, o);
            emit corrected(token, correct);
            f = undoAfter_;
            offset += correct.size() - token.size();
        }

    } catch (...) {
        cancelNativeJob();
        resetObservation();
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
    const QDeadlineTimer deadline(10000);
    auto operationLock = std::make_unique<QLockFile>(directory_ + "/operation.lock");
    operationLock->setStaleLockTime(15000);
    if (!operationLock->tryLock(0)) {
        emit result(input, input, "Another correction is already running.", native);
        return;
    }
    jobLock_ = std::move(operationLock);
    jobRunning_ = true;
    nativeJob_ = native;
    nativeJobApp_ = native ? field.app : QString{};
    nativeJobField_ = native ? field.id : QString{};
    const auto cancellation = cancellationRevision_.load();
    const auto revision = store_->value("configRevision", 0).toLongLong();
    auto options = store_->options();
    auto words = store_->words();
    auto replacements = store_->replacements();
    job_ = new QFutureWatcher<Change>(this);
    connect(
        job_, &QFutureWatcher<Change>::finished, this,
        [this, input, native, field, revision, cancellation, deadline] {
            // All result/message/exception exits publish completion, after the
            // operation lock is released. A rejected competing request is not
            // a completion of the accepted job.
            auto activity = qScopeGuard([this] { emit operationActive(false); });
            auto operationLock = std::move(jobLock_);
            auto completed = job_;
            job_ = nullptr;
            jobRunning_ = false;
            clearJobTarget();
            completed->deleteLater();
            try {
                auto change = completed->result();
                if (cancel_.load() || cancellation != cancellationRevision_.load() ||
                    store_->paused() || revision != store_->value("configRevision", 0).toLongLong())
                    change.error = "Cancelled or settings changed. Nothing was replaced.";
                if (deadline.hasExpired())
                    change.error = "Operation timed out. Nothing was replaced.";
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
                    auto current = permitted(&field);
                    if (!sameTarget(field, current)) {
                        emit message("The selection changed. Nothing was replaced.");
                        return;
                    }
                    QString updated = field.text;
                    updated.replace(field.selectionStart, field.selectionEnd - field.selectionStart,
                                    change.corrected);
                    if (deadline.hasExpired() || !policyCurrent(current, revision, cancellation)) {
                        emit message(deadline.hasExpired()
                                         ? "Operation timed out. Nothing was replaced."
                                         : "Cancelled or settings changed. Nothing was replaced.");
                        return;
                    }
                    // Retain the requested original before attempting a write:
                    // a provider can reject confirmation after changing text.
                    emit passageRecoveryAvailable(input, change.corrected);
                    // Signal delivery may synchronously revoke this operation.
                    if (deadline.hasExpired() || !policyCurrent(current, revision, cancellation)) {
                        resetObservation();
                        emit message(deadline.hasExpired()
                                         ? "Operation timed out. Nothing was replaced."
                                         : "Cancelled or settings changed. Nothing was replaced.");
                        return;
                    }
                    if (!adapter_->replace(field, updated,
                                           field.selectionStart + change.corrected.size())) {
                        resetObservation();
                        emit message(
                            "Replacement was not confirmed. Check the text; its original is "
                            "available in session recovery.");
                        return;
                    }
                    auto after = permitted(&field);
                    if (after.id != field.id || after.app != field.app || after.text != updated ||
                        after.caret != field.selectionStart + change.corrected.size() ||
                        !policyCurrent(after, revision, cancellation)) {
                        resetObservation();
                        emit message("The field changed after correction. Copy the original from "
                                     "session recovery; Undo is unavailable.");
                        return;
                    }
                    undoPassage_ = true;
                    undoBefore_ = field.text;
                    undoCaret_ = field.caret;
                    undoAfter_ = after;
                    previous_ = undoAfter_;
                    store_->history(input, change.corrected, store_->options());
                    emit result(input, change.corrected, {}, true);
                } catch (...) {
                    resetObservation();
                    emit message("The operation stopped. Check the application's text before continuing.");
                }
            } catch (...) {
                emit result(input, input, "Local storage is unavailable. The operation stopped.",
                            native);
            }
        });
    job_->setFuture(QtConcurrent::run([this, input, options, words, replacements, cancellation,
                                      deadline] {
        try {
            if (cancel_.load() || cancellation != cancellationRevision_.load())
                return Change{input, input, false, "Operation cancelled. Nothing was replaced."};
            if (deadline.hasExpired())
                return Change{input, input, false, "Operation timed out. Nothing was replaced."};
            if (!passageEngine_)
                passageEngine_ = std::make_unique<Engine>();
            passageEngine_->setPersonal(words, replacements);
            return passageEngine_->text(input, options, &cancel_, deadline.remainingTime());
        } catch (...) {
            passageEngine_.reset();
            return Change{input, input, false, "Correction failed. The original is unchanged."};
        }
    }));
    emit operationActive(true);
}
void Service::processText(QString input) {
    try {
        runText(input, false);
    } catch (...) {
        jobRunning_ = false;
        clearJobTarget();
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
    auto f = permitted(&undoAfter_);
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
        auto after = permitted(&f);
        if (after.id != f.id || after.app != f.app || after.text != undoBefore_ ||
            after.caret != undoCaret_) {
            resetObservation();
            emit message("The field changed after Undo. Check the application's text.");
            return;
        }
        previous_ = after;
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
            resetObservation();
            emit message(store_->paused() ? "Reword paused." : "Reword resumed.");
        }
    } catch (...) {
        cancelNativeJob();
        resetObservation();
        if (!job_) {
            jobRunning_ = false;
            clearJobTarget();
            jobLock_.reset();
        }
        emit message("The operation stopped safely.");
    }
}
} // namespace reword
