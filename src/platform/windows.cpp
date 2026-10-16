#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include "adapter.h"
#include <QKeySequence>
#include <QFileInfo>
using Microsoft::WRL::ComPtr;
namespace reword {
class WindowsAdapter final:public Adapter {
 ComPtr<IUIAutomation> automation_;
 ComPtr<IUIAutomationElement> validated_;
 bool com_=false;HWND window_=nullptr;QString pending_;
 static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
  if(message==WM_NCCREATE){auto create=reinterpret_cast<CREATESTRUCTW*>(l);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));}
  auto self=reinterpret_cast<WindowsAdapter*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_HOTKEY&&self){self->pending_=w==1?"fix":w==2?"undo":"pause";return 0;}
  return DefWindowProcW(window,message,w,l);
 }
 bool unlocked(){
  HDESK d=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);if(!d)return false;
  wchar_t name[256]{};DWORD length=0;bool ok=GetUserObjectInformationW(d,UOI_NAME,name,sizeof(name),&length);CloseDesktop(d);
  return ok && QString::fromWCharArray(name)=="Default";
 }
public:
 WindowsAdapter(){com_=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));if(com_)CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation_));
  ComPtr<IUIAutomation2> timed;if(automation_&&SUCCEEDED(automation_.As(&timed))){timed->put_ConnectionTimeout(500);timed->put_TransactionTimeout(500);}
  WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RewordHotkeyWindow";RegisterClassW(&cls);
  window_=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,this);
 }
 ~WindowsAdapter()override{for(int i=1;i<=3;++i)UnregisterHotKey(window_,i);if(window_)DestroyWindow(window_);validated_.Reset();automation_.Reset();if(com_)CoUninitialize();}
 QString description()const override{return "Windows · UI Automation plain edit fields";}
 Field inspect(bool read)override{
  validated_.Reset();Field f;if(!automation_)return f;ComPtr<IUIAutomationElement> e;
  if(FAILED(automation_->GetFocusedElement(&e))||!e)return f;
  int pid=0;if(FAILED(e->get_CurrentProcessId(&pid))||pid==int(GetCurrentProcessId()))return f;
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  if(!process)return f;wchar_t path[32768]{};DWORD length=32768;
  bool pathOK=QueryFullProcessImageNameW(process,0,path,&length);CloseHandle(process);if(!pathOK)return f;
  f.app=QString::fromWCharArray(path,length);
  SAFEARRAY *runtime=nullptr;if(SUCCEEDED(e->GetRuntimeId(&runtime)) && runtime){
   LONG lo=0,hi=0;SafeArrayGetLBound(runtime,1,&lo);SafeArrayGetUBound(runtime,1,&hi);
   f.id=QString::number(pid);for(LONG i=lo;i<=hi;++i){int n=0;SafeArrayGetElement(runtime,&i,&n);f.id+=':'+QString::number(n);}SafeArrayDestroy(runtime);
  }
  BOOL password=TRUE,enabled=FALSE,focused=FALSE;CONTROLTYPEID type=0;
  if(FAILED(e->get_CurrentIsPassword(&password))||FAILED(e->get_CurrentIsEnabled(&enabled))||FAILED(e->get_CurrentHasKeyboardFocus(&focused))||FAILED(e->get_CurrentControlType(&type)))return f;
  f.password=password;f.focused=focused;f.locked=!unlocked();f.known=type==UIA_EditControlTypeId;
  ComPtr<IUIAutomationValuePattern> value;BOOL readonly=TRUE;
  if(SUCCEEDED(e->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)))&&value&&SUCCEEDED(value->get_CurrentIsReadOnly(&readonly)))f.editable=enabled&&!readonly;
  f.plain=f.known&&bool(value);
  if(!read||f.password||f.locked||!f.known||!f.focused||!f.editable||!f.plain)return f;
  ComPtr<IUIAutomationTextPattern> pattern;ComPtr<IUIAutomationTextRange> doc;
  if(FAILED(e->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern)))||!pattern||FAILED(pattern->get_DocumentRange(&doc))){f.known=false;return f;}
  BSTR text=nullptr;if(FAILED(doc->GetText(100001,&text))){f.known=false;return f;}
  f.text=QString::fromWCharArray(text,SysStringLen(text));SysFreeString(text);
  if(f.text.size()>=100001 || f.text.toUcs4().size()>50000){f.text.clear();f.known=false;return f;}
  ComPtr<IUIAutomationTextRangeArray> selected;int n=0;
  if(FAILED(pattern->GetSelection(&selected))||!selected||FAILED(selected->get_Length(&n))||n!=1){f.known=false;return f;}
  ComPtr<IUIAutomationTextRange> range,prefix;selected->GetElement(0,&range);doc->Clone(&prefix);
  if(!range||!prefix||FAILED(prefix->MoveEndpointByRange(TextPatternRangeEndpoint_End,range.Get(),TextPatternRangeEndpoint_Start))){f.known=false;return f;}
  BSTR before=nullptr,part=nullptr;
  if(FAILED(prefix->GetText(100001,&before))||FAILED(range->GetText(100001,&part))){SysFreeString(before);SysFreeString(part);f.known=false;return f;}
  f.caret=int(SysStringLen(before)+SysStringLen(part));
  if(SysStringLen(part)>0){f.selectionStart=int(SysStringLen(before));f.selectionEnd=f.caret;}
  SysFreeString(before);SysFreeString(part);validated_=e;return f;
 }
 bool replace(const Field &expected,const QString &text,int caret)override{
  if(!sameTarget(expected,inspect(true)))return false;
  auto e=validated_;ComPtr<IUIAutomationValuePattern> v;BOOL password=TRUE,focused=FALSE;
  if(!e||FAILED(e->get_CurrentIsPassword(&password))||password||FAILED(e->get_CurrentHasKeyboardFocus(&focused))||!focused||!unlocked()||FAILED(e->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&v))))return false;
  BSTR replacement=SysAllocStringLen(reinterpret_cast<const OLECHAR*>(text.utf16()),UINT(text.size()));
  if(!replacement)return false;HRESULT written=v->SetValue(replacement);SysFreeString(replacement);
  if(FAILED(written))return false;
  ComPtr<IUIAutomationTextPattern> p;ComPtr<IUIAutomationTextRange> r;
  if(SUCCEEDED(e->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&p)))&&SUCCEEDED(p->get_DocumentRange(&r))){
   r->MoveEndpointByRange(TextPatternRangeEndpoint_End,r.Get(),TextPatternRangeEndpoint_Start);
   int moved=0;r->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,TextUnit_Character,text.left(caret).toUcs4().size(),&moved);r->Select();
  }
  auto after=inspect(true);return after.id==expected.id&&after.text==text;
 }
 bool shortcuts(const QString&fix,const QString&undo,const QString&pause)override{
  if(!window_)return false;for(int i=1;i<=3;++i)UnregisterHotKey(window_,i);
  int id=0;for(auto s:{fix,undo,pause}){
   ++id;QKeySequence seq(s);if(seq.count()!=1)return false;auto k=seq[0];UINT mods=MOD_NOREPEAT;auto m=k.keyboardModifiers();
   if(m&Qt::ControlModifier)mods|=MOD_CONTROL;if(m&Qt::AltModifier)mods|=MOD_ALT;if(m&Qt::ShiftModifier)mods|=MOD_SHIFT;if(m&Qt::MetaModifier)mods|=MOD_WIN;
   if(k.key()<Qt::Key_A||k.key()>Qt::Key_Z||!RegisterHotKey(window_,id,mods,UINT(k.key()))){for(int i=1;i<=3;++i)UnregisterHotKey(window_,i);return false;}
  }return true;
 }
 QString takeAction()override{auto result=pending_;pending_.clear();return result;}
};
std::unique_ptr<Adapter> createAdapter(){return std::make_unique<WindowsAdapter>();}
}
