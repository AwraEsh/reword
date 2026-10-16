#include "core/core.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);reword::Engine engine;reword::Options options;
 QStringList samples{"teh","recieve","hello","helllo","wrld","because","seperate","adress","langauge","qzxv",QString::fromUtf8("سلام"),QString::fromUtf8("سلاام"),QString::fromUtf8("برناهم"),QString::fromUtf8("کتاب")};
 QList<qint64> timings;QElapsedTimer timer;
 for(int round=0;round<100;++round)for(auto word:samples){timer.start();engine.word(word,options);timings.append(timer.nsecsElapsed());}
 std::sort(timings.begin(),timings.end());double p95=timings[(timings.size()-1)*95/100]/1000000.0;
 auto text=QString("teh ").repeated(12500);timer.start();auto result=engine.text(text,options);double passage=timer.elapsed();
 QJsonObject metrics{{"samples",timings.size()},{"wordP95Ms",p95},{"wordMaxMs",timings.last()/1000000.0},{"passageCharacters",text.size()},{"passageMs",passage},{"passageCompleted",result.error.isEmpty()}};
 std::cout<<QJsonDocument(metrics).toJson(QJsonDocument::Indented).constData();return p95<50 && result.error.isEmpty()?0:1;
}
