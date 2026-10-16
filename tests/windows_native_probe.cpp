#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/adapter.h"
#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);auto adapter=reword::createAdapter();QProcess fixture;
 fixture.start(QCoreApplication::applicationDirPath()+"/reword_win_fixture.exe",{});if(!fixture.waitForStarted())return 2;
 auto pump=[&](int milliseconds){QElapsedTimer time;time.start();while(time.elapsed()<milliseconds){app.processEvents();QThread::msleep(10);}};
 auto until=[&](auto predicate,int timeout=5000){QElapsedTimer time;time.start();do{pump(40);if(predicate())return true;}while(time.elapsed()<timeout);return false;};
 auto finish=[&](const char*message,int code){std::cout<<message<<'\n';fixture.write("quit\n");fixture.waitForBytesWritten();if(!fixture.waitForFinished(2000)){fixture.kill();fixture.waitForFinished();}return code;};
 reword::Field field;
 if(!until([&]{field=adapter->inspect(false);return field.app.endsWith("reword_win_fixture.exe",Qt::CaseInsensitive)&&field.focused;}))return finish("FAIL: native fixture did not receive focus on this desktop.",1);
 if(!field.text.isEmpty())return finish("FAIL: metadata inspection contained text.",1);
 field=adapter->inspect(true);if(!field.known||field.locked||!field.editable||field.text!="teh "){std::cerr<<"known="<<field.known<<" locked="<<field.locked<<" editable="<<field.editable<<" plain="<<field.plain<<" characters="<<field.text.size()<<" caret="<<field.caret<<"\n";return finish("FAIL: Win32 edit field was not read safely.",1);}
 auto stale=field;stale.text="stale";if(adapter->replace(stale,"WRONG",5))return finish("FAIL: stale replacement accepted.",1);
 if(!adapter->replace(field,"the ",4))return finish("FAIL: native replacement was not confirmed.",1);
 auto command=[&](const QString&text){fixture.write((text+'\n').toUtf8());fixture.waitForBytesWritten(5000);pump(200);};
 auto emoji=QString::fromUcs4(U"🙂").repeated(40000)+"end";command("text:"+emoji);
 if(!until([&]{return adapter->inspect(true).text==emoji;}))return finish("FAIL: supplementary-character text was truncated.",1);
 command("text:"+QString(150000,'a'));field=adapter->inspect(true);if(field.known||!field.text.isEmpty())return finish("FAIL: oversized document was accepted.",1);
 command("text:teh ");
 if(!adapter->shortcuts("Ctrl+Alt+Shift+J","Ctrl+Alt+Shift+K","Ctrl+Alt+Shift+L"))return finish("FAIL: native test shortcuts could not register.",1);
 INPUT inputs[8]{};WORD keys[]{VK_CONTROL,VK_MENU,VK_SHIFT,'J','J',VK_SHIFT,VK_MENU,VK_CONTROL};
 for(int i=0;i<8;++i){inputs[i].type=INPUT_KEYBOARD;inputs[i].ki.wVk=keys[i];if(i>=4)inputs[i].ki.dwFlags=KEYEVENTF_KEYUP;}
 if(SendInput(8,inputs,sizeof(INPUT))!=8)return finish("FAIL: synthetic test shortcut could not be delivered.",1);
 if(!until([&]{return adapter->takeAction()=="fix";}))return finish("FAIL: registered shortcut was lost in the event loop.",1);
 command("password");field=adapter->inspect(true);if(!field.password||!field.text.isEmpty())return finish("FAIL: password exclusion failed.",1);
 return finish("PASS: Win32 metadata, guarded replacement, large Unicode reads, size rejection, hotkey delivery and password exclusion.",0);
}
