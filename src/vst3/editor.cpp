#include "vst3/editor.hpp"
#include "vst3/keyboard.hpp"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "vst3/bounded.hpp"
#include <Windows.h>
#include <cmath>
#include <stdexcept>
#include <thread>
namespace nexus::vst3 {
using namespace Steinberg;
struct EditorWindow::Impl {
 struct Frame final:detail::Borrowed<IPlugFrame>{
  Impl* owner=nullptr;
  tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* size)override{
   if(!owner||std::this_thread::get_id()!=owner->thread||view!=owner->view.get()||!size)return kInvalidArgument;
   try{owner->resize(*size,false);return kResultOk;}catch(...){return kResultFalse;}
  }
 }frame;
 std::unique_ptr<EditorKeyboard> keyboard;
 IPtr<IPlugView> view;HWND window=nullptr;HINSTANCE instance=nullptr;std::wstring class_name;
 const std::thread::id thread=std::this_thread::get_id();
 bool attempted=false,sizing=false,closing=false;
 static bool valid(const ViewRect& r){
  const int64 width=int64(r.right)-r.left,height=int64(r.bottom)-r.top;
  return r.left==0&&r.top==0&&width>=32&&height>=32&&width<=8192&&height<=8192;
 }
 static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
  auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(self)try{
   if(message==WM_CLOSE){self->detach();DestroyWindow(hwnd);return 0;}
   if(message==WM_DESTROY){self->detach();return 0;}
   if(message==WM_NCDESTROY){self->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);}
   if(message==WM_SIZE&&self->attempted&&!self->sizing&&wparam!=SIZE_MINIMIZED){
    ViewRect r(0,0,LOWORD(lparam),HIWORD(lparam));self->resize(r,true);return 0;
   }
   if(message==WM_DPICHANGED&&self->view){
    const auto* suggested=reinterpret_cast<RECT*>(lparam);
    SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    self->scale(double(HIWORD(wparam))/96.);return 0;
   }
   if(message==WM_ACTIVATE&&self->keyboard)self->keyboard->focus(LOWORD(wparam)!=WA_INACTIVE);
   if(message==WM_SETFOCUS&&self->keyboard)self->keyboard->focus(true);
   if(message==WM_KILLFOCUS&&self->keyboard&&reinterpret_cast<HWND>(wparam)!=hwnd&&!IsChild(hwnd,reinterpret_cast<HWND>(wparam)))self->keyboard->focus(false);
   EditorCallbackGuard callback;
   if(message==WM_MOUSEWHEEL&&self->view&&self->view->onWheel(float(GET_WHEEL_DELTA_WPARAM(wparam))/WHEEL_DELTA)==kResultOk)return 0;
  }catch(...){if(message!=WM_CLOSE&&message!=WM_DESTROY)PostMessageW(hwnd,WM_CLOSE,0,0);}
  return DefWindowProcW(hwnd,message,wparam,lparam);
 }
 void scale(double factor){
  if(auto support=U::cast<IPlugViewContentScaleSupport>(view))support->setContentScaleFactor(float(factor));
 }
 void resize(ViewRect size,bool host){
  if(sizing||!view||!window)return;
  if(host){
   if(view->canResize()!=kResultOk){ViewRect fixed;if(view->getSize(&fixed)!=kResultOk)return;size=fixed;}
   else if(view->checkSizeConstraint(&size)!=kResultOk)throw std::runtime_error("VST3 rejected size constraint");
  }
  if(!valid(size))throw std::runtime_error("VST3 editor dimensions exceed safe bounds");
  EditorCallbackGuard callback;
  sizing=true;struct Reset{bool& value;~Reset(){value=false;}}reset{sizing};
  RECT outer{0,0,size.getWidth(),size.getHeight()};
  AdjustWindowRectExForDpi(&outer,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),false,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window));
  SetWindowPos(window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  if(view->onSize(&size)!=kResultOk)throw std::runtime_error("VST3 rejected editor resize");
 }
 void detach()noexcept{
  EditorCallbackGuard callback;
  if(closing)return;closing=true;
  keyboard.reset();
  if(view){
   if(attempted){attempted=false;try{view->removed();}catch(...){}}
   try{view->setFrame(nullptr);}catch(...){}
   view.reset();
  }frame.owner=nullptr;
 }
 ~Impl(){detach();if(window)DestroyWindow(window);if(!class_name.empty())UnregisterClassW(class_name.c_str(),instance);}
 Impl(Vst::IEditController* controller,const std::string& title,bool visible){
  EditorCallbackGuard callback;
  bool complete=false;
  struct Cleanup{Impl& self;bool& complete;~Cleanup(){if(!complete){self.detach();if(self.window)DestroyWindow(self.window);if(!self.class_name.empty())UnregisterClassW(self.class_name.c_str(),self.instance);}}}cleanup{*this,complete};
  frame.owner=this;
  if(!controller)throw std::runtime_error("VST3 has no edit controller");
  view=owned(controller->createView(Vst::ViewType::kEditor));
  if(!view)throw std::runtime_error("This VST3 does not provide an original editor");
  if(view->isPlatformTypeSupported(kPlatformTypeHWND)!=kResultOk)throw std::runtime_error("VST3 editor does not support Windows HWND");
  ViewRect rect;if(view->getSize(&rect)!=kResultOk||!valid(rect))throw std::runtime_error("Invalid VST3 editor dimensions");
  // Unique per session: no stale WNDPROC registered after unloading this DLL.
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),reinterpret_cast<HMODULE*>(&instance));
  class_name=L"NexusVst3Editor_"+std::to_wstring(reinterpret_cast<uintptr_t>(this));
  WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=class_name.c_str();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  if(!RegisterClassExW(&wc))throw std::runtime_error("Cannot register VST3 editor window");
  DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
  if(view->canResize()!=kResultOk)style&=~(WS_THICKFRAME|WS_MAXIMIZEBOX);
  const auto wide=Vst::StringConvert::convert("NEXUS | "+title);
  window=CreateWindowExW(0,class_name.c_str(),reinterpret_cast<const wchar_t*>(wide.c_str()),style,CW_USEDEFAULT,CW_USEDEFAULT,rect.getWidth(),rect.getHeight(),nullptr,nullptr,instance,this);
  if(!window)throw std::runtime_error("Cannot create VST3 editor window");
  if(view->setFrame(&frame)!=kResultOk)throw std::runtime_error("VST3 rejected editor frame");
  keyboard=std::make_unique<EditorKeyboard>(window,view.get());
  attempted=true;
  if(view->attached(window,kPlatformTypeHWND)!=kResultOk)throw std::runtime_error("Cannot attach VST3 original editor");
  scale(double(GetDpiForWindow(window))/96.);
  if(view->getSize(&rect)!=kResultOk)throw std::runtime_error("Cannot read attached VST3 editor size");
  resize(rect,false);
  if(visible)ShowWindow(window,SW_SHOW);
  complete=true;
 }
};
// Impl guards partial attachments before its constructor can fail.
EditorWindow::EditorWindow(Vst::IEditController* controller,const std::string& title,bool visible){
 impl_=std::make_unique<Impl>(controller,title,visible);
}
EditorWindow::~EditorWindow()=default;
void EditorWindow::show(){if(impl_->window){ShowWindow(impl_->window,SW_RESTORE);SetForegroundWindow(impl_->window);}}
void* EditorWindow::handle()const noexcept{return impl_->window;}
}
