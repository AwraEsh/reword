#include "core.h"
namespace reword {
bool safe(const Field &,const Access &,const Options &){return true;}
bool sameTarget(const Field &,const Field &){return true;}
bool RetryCycle::shouldCorrect(const QString &,int,const QString &,qint64){return true;}
void RetryCycle::reset(){}
}
