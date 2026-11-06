#include "controller.h"
#include <QStandardPaths>
#ifndef Q_OS_WIN
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>
#endif

namespace reword {
#ifndef Q_OS_WIN
static QDBusMessage notificationCall(const QString &method) {
    return QDBusMessage::createMethodCall("org.freedesktop.Notifications",
        "/org/freedesktop/Notifications", "org.freedesktop.Notifications", method);
}
static void closeNotification(uint id) {
    if (!id)
        return;
    auto message = notificationCall("CloseNotification");
    message << id;
    QDBusConnection::sessionBus().asyncCall(message);
}
#endif

void Controller::initializeNotifications() {
#ifndef Q_OS_WIN
    QDBusConnection::sessionBus().connect("org.freedesktop.Notifications",
        "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "ActionInvoked",
        this, SLOT(notificationAction(uint,QString)));
#endif
}

void Controller::closeConsentNotification() {
    ++notificationGeneration_;
#ifndef Q_OS_WIN
    closeNotification(notificationId_);
#endif
    notificationId_ = 0;
    notificationApp_.clear();
}

void Controller::notifyConsent() {
    if (pendingId_.isEmpty())
        return;
    reminderAt_ = QDateTime::currentMSecsSinceEpoch() + 15 * 60 * 1000;
    const QString title = "Allow Reword in " + pendingName_ + "?";
    const QString body = "Choose Yes, Not now or No. You can also review this request in Reword.";
    const auto generation = ++notificationGeneration_;
    notificationApp_ = pendingId_;
#ifndef Q_OS_WIN
    const auto token = QString::number(generation);
    const QString desktopEntry =
        QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "reword.desktop").isEmpty() &&
        !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "reword-preview.desktop").isEmpty()
            ? "reword-preview" : "reword";
    const QStringList actions{"default", "Open Reword", "allow:" + token, "Yes",
                              "later:" + token, "Not now", "deny:" + token, "No"};
    auto message = notificationCall("Notify");
    message << QString("Reword") << notificationId_ << desktopEntry << title
            << body << actions << QVariantMap{{"desktop-entry", desktopEntry}} << 7000;
    const auto application = pendingId_;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, application, generation, title, body](QDBusPendingCallWatcher *finished) {
            QDBusPendingReply<uint> reply = *finished;
            finished->deleteLater();
            if (generation != notificationGeneration_ || application != pendingId_) {
                if (!reply.isError() && reply.value() != notificationId_)
                    closeNotification(reply.value());
                return;
            }
            if (!reply.isError())
                notificationId_ = reply.value();
            else if (tray_)
                tray_->showMessage(title, body, QSystemTrayIcon::Information, 7000);
        });
#else
    Q_UNUSED(generation);
    if (tray_)
        tray_->showMessage(title, body, QSystemTrayIcon::Information, 7000);
#endif
}

void Controller::notificationAction(uint id, const QString &action) {
    if (!id || id != notificationId_ || notificationApp_ != pendingId_ || pendingId_.isEmpty())
        return;
    if (action == "default") {
        emit showWindow();
        return;
    }
    for (const auto &decision : {QString("allow"), QString("later"), QString("deny")})
        if (action == decision + ':' + QString::number(notificationGeneration_)) {
            consent(decision);
            return;
        }
}
} // namespace reword
