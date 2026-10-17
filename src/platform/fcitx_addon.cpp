#include "core/core.h"
#include "core/store.h"
#include "platform/adapter.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QLockFile>
#include <future>
#undef emit
#undef signals
#undef slots
#undef foreach
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/event.h>
#include <fcitx-utils/event.h>

namespace {
class RewordAddon final : public fcitx::AddonInstance {
    int argc_ = 1;
    char name_[7] = "reword";
    char *argv_[2] = {name_, nullptr};
    std::unique_ptr<QCoreApplication> qt_;
    std::unique_ptr<reword::Store> store_;
    std::unique_ptr<reword::Engine> engine_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> key_, focus_, surrounding_,
        capability_, reset_;
    std::unique_ptr<fcitx::EventSourceTime> timer_;
    reword::RetryCycle retry_;
    QString context_, prefix_, observedContext_;
    QByteArray observedHash_;
    unsigned observedCursor_ = 0, observedAnchor_ = 0;
    int retryStart_ = -1;
    bool deletionKey_ = false;
    uint64_t deletionDeadline_ = 0;
    fcitx::TrackableObjectReference<fcitx::InputContext> deletionContext_;
    qint64 heartbeat_ = 0;
    QString directory_;
    std::unique_ptr<QLockFile> operationLock_;
    std::future<reword::Change> future_;
    std::atomic_bool cancel_{false};
    struct Edit {
        fcitx::TrackableObjectReference<fcitx::InputContext> context;
        reword::RetryCycle retryAfter;
        QString app, before, after, original, corrected;
        unsigned cursor = 0, anchorBefore = 0, cursorBefore = 0;
        int start = 0;
        qint64 revision = 0, expires = 0;
        bool passage = false, valid = false, confirmed = false;
    } pending_, job_;
    void resetRetry() {
        retry_.reset();
        retryStart_ = -1;
        prefix_.clear();
    }
    void resetIfContinued(const QString &before) {
        static const QRegularExpression continued(R"([\s.,!?،؛؟]+[^\s.,!?،؛؟])");
        if (retryStart_ >= 0 &&
            (before.size() < retryStart_ || before.left(retryStart_) != prefix_ ||
             continued.match(before.mid(retryStart_)).hasMatch()))
            resetRetry();
    }
    void observeRetry(fcitx::InputContext *ic) {
        if (qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland")
            return;
        const auto app = ic ? QString::fromStdString(ic->program()) : QString{};
        if (app.isEmpty() || !permitted(ic, "fcitx:" + app) || !unlocked()) {
            resetRetry();
            observedContext_.clear();
            observedHash_.clear();
            pending_ = {};
            cancel_.store(true);
            return;
        }
        const auto &st = ic->surroundingText();
        if (!st.isValid() || st.text().size() > 400000) {
            resetRetry();
            observedContext_.clear();
            observedHash_.clear();
            pending_ = {};
            cancel_.store(true);
            return;
        }
        const auto text = QString::fromStdString(st.text());
        const auto points = text.toUcs4();
        if (st.cursor() > unsigned(points.size()) || st.anchor() > unsigned(points.size())) {
            resetRetry();
            observedContext_.clear();
            observedHash_.clear();
            pending_ = {};
            cancel_.store(true);
            return;
        }
        const auto hash = QCryptographicHash::hash(QByteArray::fromStdString(st.text()),
                                                   QCryptographicHash::Sha256);
        const auto current = QString::number(reinterpret_cast<quintptr>(ic), 16);
        if (hash != observedHash_ && deletionContext_.get() == ic)
            deletionKey_ = false;
        // Confirm our write before classifying subsequent client updates.
        observe(ic);
        if (current != observedContext_ ||
            (hash == observedHash_ &&
             (st.cursor() != observedCursor_ || st.anchor() != observedAnchor_) &&
             (!pending_.valid || pending_.confirmed)))
            resetRetry();
        if (retryStart_ >= 0)
            resetIfContinued(QString::fromUcs4(
                reinterpret_cast<const char32_t *>(points.constData()), st.cursor()));
        observedContext_ = current;
        observedHash_ = hash;
        observedCursor_ = st.cursor();
        observedAnchor_ = st.anchor();
    }
    bool unlocked() {
        for (auto spec : {QStringList{"org.gnome.ScreenSaver", "/org/gnome/ScreenSaver",
                                      "org.gnome.ScreenSaver"},
                          QStringList{"org.freedesktop.ScreenSaver", "/ScreenSaver",
                                      "org.freedesktop.ScreenSaver"}}) {
            QDBusInterface d(spec[0], spec[1], spec[2], QDBusConnection::sessionBus());
            d.setTimeout(80);
            QDBusReply<bool> r = d.call("GetActive");
            if (r.isValid())
                return !r.value();
        }
        return false;
    }
    bool permitted(fcitx::InputContext *ic, const QString &id) {
        if (!ic || !ic->hasFocus())
            return false;
        auto flags = ic->capabilityFlags();
        if (flags.test(fcitx::CapabilityFlag::Password) ||
            flags.test(fcitx::CapabilityFlag::Sensitive) ||
            !flags.test(fcitx::CapabilityFlag::SurroundingText))
            return false;
        return !store_->paused() && store_->allowed(id) &&
               (!reword::browserApp(id.mid(6)) || store_->options().browsers);
    }
    bool hotkey(const fcitx::Key &key, QString setting, QString fallback) {
        auto s = store_->value(setting, fallback).toString().replace("Ctrl+", "Control+");
        // Fcitx normalizes Control+letter events to uppercase key symbols.
        // Compare canonical keys on both sides, including user-defined shortcuts.
        return key.normalize().check(fcitx::Key(s.toStdString()).normalize());
    }
    void observe(fcitx::InputContext *ic) {
        if (!pending_.valid || pending_.context.get() != ic)
            return;
        const auto &st = ic->surroundingText();
        if (QDateTime::currentMSecsSinceEpoch() > pending_.expires) {
            pending_ = {};
            return;
        }
        if (st.isValid() && QString::fromStdString(st.text()) == pending_.after &&
            st.cursor() == pending_.cursor && st.anchor() == pending_.cursor) {
            if (!pending_.confirmed) {
                auto o = store_->options();
                if (pending_.passage)
                    store_->history(pending_.original, pending_.corrected, o);
                else {
                    retry_ = pending_.retryAfter;
                    store_->count(pending_.original, pending_.corrected, o);
                }
                pending_.confirmed = true;
                if (o.notifications && !pending_.passage) {
                    QDBusInterface n(
                        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                        "org.freedesktop.Notifications", QDBusConnection::sessionBus());
                    n.asyncCall("Notify", "Reword", uint(0), "", "Spelling corrected",
                                pending_.original + " → " + pending_.corrected, QStringList{},
                                QVariantMap{}, 3000);
                }
            }
        }
    }
    void completed() {
        if (!future_.valid() ||
            future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return;
        auto operationLock = std::move(operationLock_);
        auto change = future_.get();
        auto job = job_;
        job_ = {};
        auto ic = job.context.get();
        if (cancel_.load() || !change.changed || !change.error.isEmpty() ||
            !permitted(ic, job.app) || !unlocked() ||
            job.revision != store_->value("configRevision", 0).toLongLong())
            return;
        const auto &st = ic->surroundingText();
        if (!st.isValid() || QString::fromStdString(st.text()) != job.before ||
            QString::fromStdString(st.selectedText()) != job.original ||
            st.anchor() != job.anchorBefore || st.cursor() != job.cursorBefore)
            return;
        auto points = job.before.toUcs4();
        unsigned start = std::min(st.anchor(), st.cursor()),
                 end = std::max(st.anchor(), st.cursor());
        QString before =
            QString::fromUcs4(reinterpret_cast<const char32_t *>(points.constData()), start);
        QString after = QString::fromUcs4(
            reinterpret_cast<const char32_t *>(points.constData() + end), points.size() - end);
        job.after = before + change.corrected + after;
        job.corrected = change.corrected;
        job.cursor = start + change.corrected.toUcs4().size();
        job.start = before.size();
        job.valid = true;
        job.expires = QDateTime::currentMSecsSinceEpoch() + 30000;
        pending_ = job;
        ic->commitString(change.corrected.toStdString());
    }
    void handle(fcitx::Event &event) {
        // X11 is owned by the accessibility service. Never run two correction cycles.
        if (qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland")
            return;
        auto &e = static_cast<fcitx::KeyEvent &>(event);
        auto *ic = e.inputContext();
        if (e.isRelease())
            return;
        if (!ic || !ic->hasFocus())
            return;
        try {
            deletionKey_ = false;
            deletionContext_ = ic->watch();
            auto app = QString::fromStdString(ic->program());
            if (app.isEmpty())
                return;
            QString id = "fcitx:" + app;
            store_->discover(id, app, reword::restrictedApp(app));
            auto now = QDateTime::currentSecsSinceEpoch();
            if (now != heartbeat_) {
                store_->setValue("fcitxHeartbeat", now);
                heartbeat_ = now;
            }
            if (hotkey(e.key(), "pauseShortcut", "Ctrl+Alt+Shift+P")) {
                cancel_.store(true);
                store_->pause(store_->paused() ? 0 : -1);
                e.filterAndAccept();
                return;
            }
            if (!permitted(ic, id)) {
                resetRetry();
                pending_ = {};
                cancel_.store(true);
                return;
            }
            bool selection = hotkey(e.key(), "fixShortcut", "Ctrl+Alt+R"),
                 undo = hotkey(e.key(), "undoShortcut", "Ctrl+Alt+Z");
            auto code = fcitx::Key::keySymToUnicode(e.key().sym());
            const auto &st = ic->surroundingText();
            if (!st.isValid() || st.text().size() > 400000)
                return;
            auto text = QString::fromStdString(st.text());
            auto points = text.toUcs4();
            if (st.cursor() > unsigned(points.size()) || st.anchor() > unsigned(points.size()))
                return;
            const bool backspace = e.key().sym() == FcitxKey_BackSpace;
            const bool forwardDelete = e.key().sym() == FcitxKey_Delete;
            deletionKey_ = (backspace || forwardDelete) &&
                           (st.cursor() != st.anchor() || (backspace && st.cursor() > 0) ||
                            (forwardDelete && st.cursor() < unsigned(points.size())));
            deletionDeadline_ = fcitx::now(CLOCK_MONOTONIC) + 250000;
            observe(ic);
            // Some clients refresh surrounding text only before the next key.
            // Recognize continuation now so deleting it before a delimiter still resets.
            if (retryStart_ >= 0 && code >= 0x20 && code != 0x7f && !selection && !undo &&
                !e.key().states().test(fcitx::KeyState::Ctrl) &&
                !e.key().states().test(fcitx::KeyState::Alt) && unlocked()) {
                auto before = QString::fromUcs4(
                    reinterpret_cast<const char32_t *>(points.constData()), st.cursor());
                char32_t character = code;
                before += QString::fromUcs4(&character, 1);
                resetIfContinued(before);
            }
            if (undo) {
                if (!unlocked() || !pending_.valid || !pending_.confirmed ||
                    pending_.context.get() != ic || text != pending_.after ||
                    st.cursor() != pending_.cursor || st.anchor() != st.cursor())
                    return;
                auto original = pending_.original;
                int length = pending_.corrected.toUcs4().size();
                // Live edits include the delimiter, so restore the word and delimiter together.
                if (!pending_.passage) {
                    auto suffix = pending_.after.mid(pending_.start + pending_.corrected.size(), 1);
                    original += suffix;
                    ++length;
                }
                ic->deleteSurroundingText(-length, length);
                ic->commitString(original.toStdString());
                pending_ = {};
                e.filterAndAccept();
                return;
            }
            if (selection) {
                if (future_.valid() || st.cursor() == st.anchor() || !unlocked())
                    return;
                auto original = QString::fromStdString(st.selectedText());
                auto options = store_->options();
                if (original.size() > 2 * options.maxCharacters ||
                    original.toUcs4().size() > options.maxCharacters)
                    return;
                auto lock = std::make_unique<QLockFile>(directory_ + "/operation.lock");
                lock->setStaleLockTime(15000);
                if (!lock->tryLock(0))
                    return;
                operationLock_ = std::move(lock);
                cancel_.store(false);
                job_ = {};
                job_.context = ic->watch();
                job_.app = id;
                job_.before = text;
                job_.original = original;
                job_.anchorBefore = st.anchor();
                job_.cursorBefore = st.cursor();
                job_.passage = true;
                job_.revision = store_->value("configRevision", 0).toLongLong();
                auto words = store_->words();
                auto replacements = store_->replacements();
                future_ =
                    std::async(std::launch::async, [this, original, options, words, replacements] {
                        try {
                            QElapsedTimer elapsed;
                            elapsed.start();
                            reword::Engine engine;
                            engine.setPersonal(words, replacements);
                            return engine.text(original, options, &cancel_,
                                               std::max(qint64(0), 10000 - elapsed.elapsed()));
                        } catch (...) {
                            return reword::Change{original, original, false, "Correction failed."};
                        }
                    });
                e.filterAndAccept();
                return;
            }
            if (future_.valid() || (code != ' ' && code != '.' && code != ',' && code != '!' &&
                                    code != '?' && code != 0x060c && code != 0x061f))
                return;
            if (!unlocked() || st.cursor() != st.anchor() ||
                !ic->inputPanel().clientPreedit().empty())
                return;
            auto current = QString::number(reinterpret_cast<quintptr>(ic), 16);
            if (current != context_) {
                resetRetry();
                context_ = current;
                prefix_.clear();
            }
            auto before = QString::fromUcs4(reinterpret_cast<const char32_t *>(points.constData()),
                                            st.cursor());
            char32_t delimiter = code;
            auto mark = QString::fromUcs4(&delimiter, 1);
            reword::Field snapshot;
            snapshot.app = id;
            snapshot.id = current;
            snapshot.text = text;
            snapshot.caret = before.size();
            auto typed = snapshot;
            typed.text.insert(typed.caret, mark);
            typed.caret += mark.size();
            const auto words = reword::completedWords(snapshot, typed);
            if (words.isEmpty())
                return;
            const auto &word = words.back();
            auto token = word.token, prefix = before.left(word.start);
            if (prefix != prefix_) {
                resetRetry();
                prefix_ = prefix;
            }
            auto options = store_->options();
            options.abbreviations = false;
            engine_->setPersonal(store_->words(), store_->replacements());
            auto corrected = engine_->word(token, options, prefix.right(128));
            if (corrected == token)
                return;
            retryStart_ = word.start;
            auto tentativeRetry = retry_;
            if (!tentativeRetry.shouldCorrect(current, word.start, token,
                                              QDateTime::currentMSecsSinceEpoch())) {
                retry_ = tentativeRetry;
                return;
            }
            auto suffix = QString::fromUcs4(
                reinterpret_cast<const char32_t *>(points.constData() + st.cursor()),
                points.size() - st.cursor());
            pending_ = {};
            pending_.retryAfter = tentativeRetry;
            pending_.context = ic->watch();
            pending_.app = id;
            pending_.before = text;
            pending_.after = prefix + corrected + mark + suffix;
            pending_.original = token;
            pending_.corrected = corrected;
            pending_.start = prefix.size();
            pending_.cursor = prefix.toUcs4().size() + corrected.toUcs4().size() + 1;
            pending_.valid = true;
            pending_.expires = QDateTime::currentMSecsSinceEpoch() + 30000;
            ic->deleteSurroundingText(-int(token.toUcs4().size()), token.toUcs4().size());
            ic->commitString(corrected.toStdString());
        } catch (...) {
            resetRetry();
            pending_ = {};
            cancel_.store(true);
        }
    }

  public:
    explicit RewordAddon(fcitx::Instance *instance) {
        if (!QCoreApplication::instance())
            qt_ = std::make_unique<QCoreApplication>(argc_, argv_);
        auto dir = qEnvironmentVariable("REWORD_DATA_DIR");
        if (dir.isEmpty())
            dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reword";
        directory_ = dir;
        store_ = std::make_unique<reword::Store>(dir);
        engine_ = std::make_unique<reword::Engine>();
        key_ = instance->watchEvent(fcitx::EventType::InputContextKeyEvent,
                                    fcitx::EventWatcherPhase::PreInputMethod,
                                    [this](fcitx::Event &e) { handle(e); });
        auto trackSurrounding = [this](fcitx::Event &e) {
            try {
                observeRetry(static_cast<fcitx::InputContextEvent &>(e).inputContext());
            } catch (...) {
                resetRetry();
                observedContext_.clear();
                observedHash_.clear();
                pending_ = {};
                cancel_.store(true);
            }
        };
        surrounding_ = instance->watchEvent(fcitx::EventType::InputContextSurroundingTextUpdated,
                                            fcitx::EventWatcherPhase::Default, trackSurrounding);
        capability_ = instance->watchEvent(fcitx::EventType::InputContextCapabilityChanged,
                                           fcitx::EventWatcherPhase::Default, trackSurrounding);
        reset_ = instance->watchEvent(
            fcitx::EventType::InputContextReset, fcitx::EventWatcherPhase::Default,
            [this, trackSurrounding](fcitx::Event &e) {
                if (qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland")
                    return;
                auto *ic = static_cast<fcitx::InputContextEvent &>(e).inputContext();
                // GTK sends Reset for deletion and for cursor changes without updated text.
                // Async clients can send it after key release. Bound the exemption,
                // reject no-op deletions, and invalidate it on text updates.
                const bool deleting = deletionKey_ && deletionContext_.get() == ic &&
                                      fcitx::now(CLOCK_MONOTONIC) <= deletionDeadline_;
                deletionKey_ = false;
                trackSurrounding(e);
                if (!deleting) {
                    resetRetry();
                    // A late confirmation must not reinstall the pre-navigation cycle.
                    pending_.retryAfter.reset();
                }
            });
        focus_ = instance->watchEvent(fcitx::EventType::InputContextFocusOut,
                                      fcitx::EventWatcherPhase::Default, [this](fcitx::Event &) {
                                          deletionKey_ = false;
                                          deletionContext_ = {};
                                          resetRetry();
                                          observedContext_.clear();
                                          observedHash_.clear();
                                          context_.clear();
                                          prefix_.clear();
                                          pending_ = {};
                                          cancel_.store(true);
                                      });
        timer_ = instance->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 40000, 0,
            [this](fcitx::EventSourceTime *timer, uint64_t) {
                try {
                    completed();
                    if (pending_.valid) {
                        auto ic = pending_.context.get();
                        if (permitted(ic, pending_.app) && unlocked())
                            observe(ic);
                        else
                            pending_ = {};
                    }
                } catch (...) {
                    cancel_.store(true);
                    pending_ = {};
                }
                timer->setNextInterval(40000);
                timer->setEnabled(true);
                return true;
            });
    }
    ~RewordAddon() override {
        cancel_.store(true);
        if (future_.valid())
            future_.wait();
    }
};
class Factory : public fcitx::AddonFactory {
  public:
    fcitx::AddonInstance *create(fcitx::AddonManager *m) override {
        try {
            return new RewordAddon(m->instance());
        } catch (...) {
            return nullptr;
        }
    }
};
} // namespace
FCITX_ADDON_FACTORY(Factory)
