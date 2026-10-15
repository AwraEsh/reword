#pragma once
#include "platform/adapter.h"
#include "core/store.h"
#include <QObject>
#include <QTimer>
namespace reword {
class Service:public QObject {
 Q_OBJECT
 QString directory_;std::unique_ptr<Store> store_;std::unique_ptr<Engine> engine_;std::unique_ptr<Adapter> adapter_;
 QTimer *timer_=nullptr;Field previous_,undoAfter_;QString undoBefore_;int undoCaret_=0;
 RetryCycle retry_;QString lastAsk_;QDate askDate_;QString shortcutSignature_;
 std::atomic_bool cancel_{false};bool wayland_=false;
 void tick();
 Field permitted();
 void fixSelection();
 void undo();
public:
 explicit Service(QString directory):directory_(std::move(directory)){}
 void cancel(){cancel_.store(true);}
public slots:
 void start();
 void stop();
 void processText(QString input);
 void action(QString name);
signals:
 void application(QString id,QString name);
 void result(QString original,QString corrected,QString error,bool native);
 void message(QString message);
 void state(QString description,bool shortcutsOK);
 void corrected(QString original,QString corrected);
};
}
