#include "core.h"
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <hunspell/hunspell.hxx>
#include <algorithm>

namespace reword {
bool safe(const Field &f,const Access &a,const Options &o) {
 return a.allowed && !a.paused && (!a.browser || o.browsers) && f.known && !f.password
  && f.editable && f.focused && !f.locked && f.plain && !f.app.isEmpty() && !f.id.isEmpty();
}
bool sameTarget(const Field &a,const Field &b) {
 return a.app==b.app && a.id==b.id && a.text==b.text && a.caret==b.caret
  && a.selectionStart==b.selectionStart && a.selectionEnd==b.selectionEnd
  && a.revision==b.revision && b.known && !b.password && b.editable && b.focused && !b.locked && b.plain;
}
bool RetryCycle::shouldCorrect(const QString &field,int location,const QString &word,qint64 now) {
 if(field_!=field || location_!=location || word_!=word || now-time_>30000 || now<time_) {
  field_=field; location_=location; word_=word; attempts_=0;
 }
 time_=now; return (++attempts_%2)==1;
}
void RetryCycle::reset(){field_.clear();word_.clear();location_=-1;attempts_=0;time_=0;}

static QString dictionary(const QString &name,const QString &dir) {
 QStringList locations;
 if(!dir.isEmpty())locations<<dir;
 if(QCoreApplication::instance())locations<<QCoreApplication::applicationDirPath()+"/dictionaries";
 if(QCoreApplication::instance())locations<<QCoreApplication::applicationDirPath()+"/../share/reword/dictionaries";
 locations<<"/usr/share/hunspell"<<"/usr/share/myspell/dicts";
 for(const auto &p:locations) if(QFileInfo::exists(p+"/"+name+".aff") && QFileInfo::exists(p+"/"+name+".dic"))return p+"/"+name;
 return {};
}
static bool persian(const QString &w) {
 for(auto c:w)if(c.unicode()>=0x600 && c.unicode()<=0x6ff)return true;
 return false;
}
static bool protectedToken(const QString &w) {
 static const QRegularExpression risky(R"([@/\\_:=<>`#\d(){}\[\]\"]|https?\.|www\.|\w\.\w)");
 return risky.match(w).hasMatch() || w.size()>64;
}
using Spans=QVector<QPair<qsizetype,qsizetype>>;
static Spans protectedSpans(const QString &text){
 Spans ranges;
 const QRegularExpression code(R"(```[\s\S]*?(?:```|$)|`[^`]*(?:`|$))");
 auto c=code.globalMatch(text);while(c.hasNext()){auto m=c.next();ranges.push_back({m.capturedStart(),m.capturedEnd()});}
 const QRegularExpression chunks(R"(\S+)");auto t=chunks.globalMatch(text);
 while(t.hasNext()){auto m=t.next();if(protectedToken(m.captured()))ranges.push_back({m.capturedStart(),m.capturedEnd()});}
 std::sort(ranges.begin(),ranges.end());Spans merged;
 for(auto r:ranges){if(!merged.isEmpty()&&r.first<=merged.back().second)merged.back().second=std::max(merged.back().second,r.second);else merged.push_back(r);}return merged;
}
static bool overlaps(const Spans &spans,qsizetype start,qsizetype end){
 auto it=std::lower_bound(spans.begin(),spans.end(),start,[](auto r,qsizetype n){return r.second<=n;});
 return it!=spans.end()&&it->first<end;
}
static int distance(const QString &a,const QString &b) {
 QVector<int> prev(b.size()+1), curr(b.size()+1), before;
 for(int j=0;j<=b.size();++j)prev[j]=j;
 for(int i=1;i<=a.size();++i){
  curr[0]=i;
  for(int j=1;j<=b.size();++j){
   curr[j]=std::min({prev[j]+1,curr[j-1]+1,prev[j-1]+int(a[i-1]!=b[j-1])});
   if(i>1 && j>1 && a[i-1]==b[j-2] && a[i-2]==b[j-1])curr[j]=std::min(curr[j],before[j-2]+1);
  }
  before=prev;prev=curr;
 }
 return prev[b.size()];
}
static QString preserveCase(const QString &from,QString to) {
 if(from==from.toUpper())return to.toUpper();
 if(!from.isEmpty() && from[0].isUpper())to[0]=to[0].toUpper();
 return to;
}
struct Engine::Impl {
 std::unique_ptr<Hunspell> en,fa;
 QSet<QString> personal;
 QHash<QString,QString> replacements;
 // Auditable common-error exceptions. Each candidate is still dictionary-validated.
 QHash<QString,QString> common{{"teh","the"},{"recieve","receive"},{"recieved","received"},
  {"seperate","separate"},{"definately","definitely"},{"occured","occurred"},
  {"adress","address"},{"becuase","because"},{"wierd","weird"},{"thier","their"},
  {"langauge","language"},{"tommorow","tomorrow"},{"suport","support"},{QString::fromUtf8("سلاام"),QString::fromUtf8("سلام")},{QString::fromUtf8("برناهم"),QString::fromUtf8("برنامه")}};
 QSet<QString> frequent=QSet<QString>{"the","and","that","with","have","this","from","they","would","there","their","what","about","which","when","make","like","time","just","know","people","into","year","your","good","some","could","them","other","than","then","only","come","over","think","also","back","after","work","first","well","even","want","because","these","give","most","hello","world","please","thank","thanks","receive","separate","definitely","address","language","support","tomorrow","occurred","weird"};
};
Engine::Engine(const QString &enDir,const QString &faDir):impl_(std::make_unique<Impl>()) {
 auto load=[](QString path)->std::unique_ptr<Hunspell>{
  if(path.isEmpty())return {};
  return std::make_unique<Hunspell>((path+".aff").toUtf8().constData(),(path+".dic").toUtf8().constData());
 };
 for(const auto &w:QString::fromUtf8("سلام خوب خانه کتاب برنامه برای این است هست شما من ما آنها امروز فردا ممنون زبان فارسی درست اشتباه نوشتن متن کلمه کار زمان مردم ایران زندگی دوست خیلی بیشتر کمتر همیشه هرگز شاید چیزی باید شود بود شده دارد داشته باشد اگر ولی دیگر وقتی بدون همراه قبل بعد جدید مهم بهتر" ).split(' '))impl_->frequent.insert(w);
 impl_->en=load(dictionary("en_US",enDir));impl_->fa=load(dictionary("fa_IR",faDir));
 if(!impl_->fa)impl_->fa=load(dictionary("fa",faDir));
}
Engine::~Engine()=default;
bool Engine::englishAvailable()const{return bool(impl_->en);}
bool Engine::persianAvailable()const{return bool(impl_->fa);}
void Engine::setPersonal(const QSet<QString>&words,const QHash<QString,QString>&replacements){impl_->personal=words;impl_->replacements=replacements;}
QString Engine::word(const QString &w,const Options &o,const QString &context) {
 bool fa=persian(w); if(w.isEmpty() || (fa?!o.persian:!o.english) || protectedToken(w))return w;
 const auto lower=w.toLower();
 if(impl_->personal.contains(lower))return w;
 if(impl_->replacements.contains(lower))return preserveCase(w,impl_->replacements.value(lower));
 if(!o.spelling)return w;
 auto *hun=fa?impl_->fa.get():impl_->en.get(); if(!hun)return w;
 if(hun->spell(w.toUtf8().toStdString()) || hun->spell(lower.toUtf8().toStdString()))return w;
 // Interior capitals are often proper nouns or product identifiers.
 for(int i=1;i<w.size();++i)if(w[i].isUpper() && w!=w.toUpper())return w;
 if(impl_->common.contains(lower)){
  auto candidate=impl_->common.value(lower);
  if(!candidate.contains(' ') && hun->spell(candidate.toUtf8().toStdString()))return preserveCase(w,candidate);
 }
 auto suggestions=hun->suggest(lower.toUtf8().toStdString());
 struct Candidate{QString value;int score;}; QVector<Candidate> candidates;
 for(size_t i=0;i<std::min(size_t(16),suggestions.size());++i){
  auto s=QString::fromUtf8(suggestions[i]);
  if(s.contains(' ') || s.contains('-') || s.size()>64)continue;
  int d=distance(lower,s.toLower()); if(d>1)continue;
  int score=100-d*30+(impl_->frequent.contains(s.toLower())?20:0);
  const QHash<QString,QString> phrases{{"hello","world"},{"thank","you"},{"good","morning"},{"best","regards"},{QString::fromUtf8("سلام"),QString::fromUtf8("علیکم")}};
  auto previous=context.trimmed().section(' ',-1).toLower();if(phrases.value(previous)==s.toLower())score+=10;
  // Small preference for a neighboring QWERTY key; never sufficient alone.
  const QString keyboard="qwertyuiop asdfghjkl zxcvbnm";
  if(s.size()==lower.size())for(int j=0;j<s.size();++j)
   if(s[j]!=lower[j] && keyboard.contains(s[j]) && std::abs(keyboard.indexOf(s[j])-keyboard.indexOf(lower[j]))==1)score+=3;
  if(std::none_of(candidates.begin(),candidates.end(),[&](const Candidate&c){return c.value==s;}))candidates.push_back({s,score});
 }
 std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.score>b.score;});
 int margin=o.sensitivity==0?18:10;
 if(!candidates.isEmpty() && candidates[0].score>=90 && (candidates.size()==1 || candidates[0].score-candidates[1].score>=margin))
  return preserveCase(w,candidates[0].value);
 return w;
}
Change Engine::text(const QString &input,const Options&o,const std::atomic_bool*cancel,qint64 timeoutMs) {
 Change out{input,input,false,{}};
 if(input.size()>2*std::clamp(o.maxCharacters,1,100000) || input.toUcs4().size()>std::clamp(o.maxCharacters,1,100000)){out.error="Text exceeds the operation limit. Nothing was changed.";return out;}
 QElapsedTimer timer;timer.start();
 auto expired=[&]{return (cancel && cancel->load()) || timer.elapsed()>=timeoutMs;};
 if(expired()){out.error="Operation cancelled or timed out.";return out;}
 static const QRegularExpression chunks(R"(\S+)");
 static const QRegularExpression words(R"([\p{L}\x{200C}]+(?:['’][\p{L}]+)*)");
 QString result;qsizetype last=0;const auto inputProtected=protectedSpans(input);
 auto iterator=chunks.globalMatch(input);
 while(iterator.hasNext()){
  if(expired()){out.error="Operation cancelled or timed out.";return out;}
  auto m=iterator.next();result+=input.mid(last,m.capturedStart()-last);
  auto chunk=m.captured();
  if(!overlaps(inputProtected,m.capturedStart(),m.capturedEnd())){
   QString replaced;qsizetype p=0;auto wi=words.globalMatch(chunk);
   while(wi.hasNext()){
    if(expired()){out.error="Operation cancelled or timed out.";return out;}
    auto wm=wi.next();replaced+=chunk.mid(p,wm.capturedStart()-p);auto token=wm.captured();
    if(o.persian && persian(token)){
     if(o.normalizePersian)token.replace(QChar(0x064a),QChar(0x06cc)).replace(QChar(0x0643),QChar(0x06a9));
    }
    if(o.english && o.abbreviations && token.compare("im",Qt::CaseInsensitive)==0)token="I am";
    else token=word(token,o,(result.right(128)+replaced.right(128)).right(128));
    replaced+=token;p=wm.capturedEnd();
   }
   chunk=replaced+chunk.mid(p);
  }
  result+=chunk;last=m.capturedEnd();
 }
 result+=input.mid(last);
 auto languageEnabled=[&](qsizetype position){
  for(qsizetype i=position-1;i>=0;--i)if(result[i].isLetter())return persian(QString(result[i]))?o.persian:o.english;
  return false;
 };
 auto controlled=[&](const QRegularExpression &re,auto transform,bool checkLanguage=true){
  QString next;qsizetype pos=0;const auto spans=protectedSpans(result);auto it=re.globalMatch(result);
  while(it.hasNext()){auto m=it.next();next+=result.mid(pos,m.capturedStart()-pos);next+=(!checkLanguage||languageEnabled(m.capturedStart()))&&!overlaps(spans,m.capturedStart(),m.capturedEnd())?transform(m):m.captured();pos=m.capturedEnd();}
  result=next+result.mid(pos);
 };
 if(o.persian && o.halfSpace)controlled(QRegularExpression(QString::fromUtf8(R"((?<![\p{L}\x{200C}])(ن?می) +([\x{600}-\x{6ff}]+))")),[&](const auto&m){
  auto joined=m.captured(1)+QChar(0x200c)+m.captured(2);
  return impl_->fa&&impl_->fa->spell(joined.toUtf8().toStdString())?joined:m.captured();
 },false);
 if(o.english && o.capitalization){
  const auto spans=protectedSpans(result);bool beginning=true;
  for(qsizetype i=0;i<result.size();++i){
   auto c=result[i];if(overlaps(spans,i,i+1)){if(c.isLetter())beginning=false;continue;}
   if(beginning&&c.isLetter()){if(c.unicode()<128)result[i]=c.toUpper();beginning=false;}
   if(c=='.'||c=='!'||c=='?'||c=='\n')beginning=true;
  }
 }
 if(o.spacing){
  if(o.periods)controlled(QRegularExpression(R"( +([.!?]))"),[](const auto&m){return m.captured(1);});
  if(o.commas)controlled(QRegularExpression(QString::fromUtf8(" +([,،])")),[](const auto&m){return m.captured(1);});
 }
 if(o.periods)controlled(QRegularExpression(R"((?<!\.)\.{2}(?!\.))"),[](const auto&){return QString(".");});
 if(o.commas)controlled(QRegularExpression(QString::fromUtf8("([,،])\\1+")),[](const auto&m){return m.captured(1);});
 if(expired()){out.error="Operation cancelled or timed out.";return out;}
 if(result.toUcs4().size()>std::clamp(o.maxCharacters,1,100000)){out.error="Corrected text exceeds the operation limit.";return out;}
 out.corrected=result;out.changed=result!=input;return out;
}
}
