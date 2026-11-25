#include "vst3/keyboard.hpp"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/keycodes.h"
#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <stdexcept>
namespace nexus::vst3 {
using namespace Steinberg;
namespace {
thread_local unsigned callback_depth=0;
struct Key {bool down=false,handled=false;char16 character=0;int16 code=0,modifiers=0;};
struct Target {
 HWND window=nullptr;IPtr<IPlugView> view;std::array<Key,256> keys{};
};
thread_local HHOOK hook=nullptr;
thread_local std::vector<std::weak_ptr<Target>> targets;
int16 code_for(UINT vk,LPARAM details){
 if(vk>=VK_F1&&vk<=VK_F12)return int16(KEY_F1+vk-VK_F1);
 if(vk>=VK_F13&&vk<=VK_F24)return int16(KEY_F13+vk-VK_F13);
 if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return int16(KEY_NUMPAD0+vk-VK_NUMPAD0);
 if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))return CharToVirtualKeyCode(tchar(vk));
 switch(vk){
 case VK_BACK:return KEY_BACK;case VK_TAB:return KEY_TAB;case VK_CLEAR:return KEY_CLEAR;
 case VK_RETURN:return (details&(LPARAM(1)<<24))?KEY_ENTER:KEY_RETURN;
 case VK_PAUSE:return KEY_PAUSE;case VK_ESCAPE:return KEY_ESCAPE;case VK_SPACE:return KEY_SPACE;
 case VK_END:return KEY_END;case VK_HOME:return KEY_HOME;case VK_LEFT:return KEY_LEFT;case VK_UP:return KEY_UP;case VK_RIGHT:return KEY_RIGHT;case VK_DOWN:return KEY_DOWN;
 case VK_PRIOR:return KEY_PAGEUP;case VK_NEXT:return KEY_PAGEDOWN;case VK_SELECT:return KEY_SELECT;case VK_PRINT:return KEY_PRINT;case VK_SNAPSHOT:return KEY_SNAPSHOT;
 case VK_INSERT:return KEY_INSERT;case VK_DELETE:return KEY_DELETE;case VK_HELP:return KEY_HELP;
 case VK_MULTIPLY:return KEY_MULTIPLY;case VK_ADD:return KEY_ADD;case VK_SEPARATOR:return KEY_SEPARATOR;case VK_SUBTRACT:return KEY_SUBTRACT;case VK_DECIMAL:return KEY_DECIMAL;case VK_DIVIDE:return KEY_DIVIDE;
 case VK_NUMLOCK:return KEY_NUMLOCK;case VK_SCROLL:return KEY_SCROLL;
 case VK_SHIFT:case VK_LSHIFT:case VK_RSHIFT:return KEY_SHIFT;
 case VK_CONTROL:case VK_LCONTROL:case VK_RCONTROL:return KEY_CONTROL;
 case VK_MENU:case VK_LMENU:case VK_RMENU:return KEY_ALT;
 case VK_APPS:return KEY_CONTEXTMENU;case VK_LWIN:case VK_RWIN:return KEY_SUPER;
 default:return 0;
 }
}
int16 modifiers(){
 int16 result=0;if(GetKeyState(VK_SHIFT)&0x8000)result|=kShiftKey;
 if(GetKeyState(VK_MENU)&0x8000)result|=kAlternateKey;
 if(GetKeyState(VK_CONTROL)&0x8000)result|=kCommandKey;
 if((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)result|=kControlKey;
 return result;
}
char16 character_for(UINT vk,LPARAM details){
 BYTE state[256]{};if(!GetKeyboardState(state))return 0;
 wchar_t text[8]{};const auto layout=GetKeyboardLayout(0);
 UINT scan=UINT((details>>16)&0xff);if(!scan)scan=MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,layout);
 // Bit 2 requests a non-mutating translation (Windows 10 1607+). Native
 // TranslateMessage retains dead-key/IME handling for unconsumed keys.
 const int count=ToUnicodeEx(vk,scan,state,text,8,4,layout);
 return count==1?char16(text[0]):0;
}
bool route(const std::shared_ptr<Target>& target,MSG& message){
 if(!target->view||!target->window||GetFocus()!=message.hwnd||
    (message.hwnd!=target->window&&!IsChild(target->window,message.hwnd)))return false;
 const bool down=message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN;
 const bool up=message.message==WM_KEYUP||message.message==WM_SYSKEYUP;
 const UINT vk=UINT(message.wParam);if((!down&&!up)||vk>=256)return false;
 // Leave the system close gesture to Windows.
 if(vk==VK_F4&&(message.lParam&(LPARAM(1)<<29)))return false;
 auto view=target->view;EditorCallbackGuard active;
 auto& key=target->keys[vk];
 if(up){
  if(!key.down)return false;
  const auto saved=key;key={};
  return view->onKeyUp(saved.character,saved.code,modifiers())==kResultOk||saved.handled;
 }
 const Key current{true,key.handled,character_for(vk,message.lParam),code_for(vk,message.lParam),modifiers()};key=current;
 const bool handled=view->onKeyDown(current.character,current.code,current.modifiers)==kResultOk;
 key.handled=key.handled||handled;
 if(handled)return true;
 if(!target->window||!target->view)return false;
 // Native controls retain ordinary text/selection; dialog-style Tab is used
 // only when the focused control does not request Tab itself.
 if(vk==VK_TAB&&!(current.modifiers&(kAlternateKey|kCommandKey|kControlKey))){
  const auto wanted=SendMessageW(message.hwnd,WM_GETDLGCODE,VK_TAB,reinterpret_cast<LPARAM>(&message));
  if(!(wanted&(DLGC_WANTTAB|DLGC_WANTALLKEYS))){
   const auto next=GetNextDlgTabItem(target->window,message.hwnd,(current.modifiers&kShiftKey)!=0);
   if(next&&next!=message.hwnd){key.handled=true;SetFocus(next);return true;}
  }
 }
 return false;
}
LRESULT CALLBACK keyboard_hook(int code,WPARAM removal,LPARAM raw){
 if(code>=0&&removal==PM_REMOVE){
  auto* message=reinterpret_cast<MSG*>(raw);
  if(message&&(message->message==WM_KEYDOWN||message->message==WM_KEYUP||message->message==WM_SYSKEYDOWN||message->message==WM_SYSKEYUP)){
   std::shared_ptr<Target> selected;
   for(auto& weak:targets)if(auto target=weak.lock())if(target->window&&(message->hwnd==target->window||IsChild(target->window,message->hwnd))){selected=std::move(target);break;}
   try{if(selected&&route(selected,*message)){message->message=WM_NULL;message->wParam=0;message->lParam=0;}}catch(...){/* No exception crosses the Win32 hook. */}
  }
 }
 return CallNextHookEx(nullptr,code,removal,raw);
}
void remove(const std::shared_ptr<Target>& target){
 targets.erase(std::remove_if(targets.begin(),targets.end(),[&](const auto& weak){auto value=weak.lock();return !value||value==target;}),targets.end());
 if(targets.empty()&&hook){UnhookWindowsHookEx(hook);hook=nullptr;}
}
}
EditorCallbackGuard::EditorCallbackGuard()noexcept{++callback_depth;}
EditorCallbackGuard::~EditorCallbackGuard(){--callback_depth;}
bool editor_callback_active()noexcept{return callback_depth!=0;}
struct EditorKeyboard::Impl {std::shared_ptr<Target> target;};
EditorKeyboard::EditorKeyboard(void* window,IPlugView* view):impl_(std::make_unique<Impl>()){
 auto target=std::make_shared<Target>();target->window=static_cast<HWND>(window);target->view=view;
 if(!hook){hook=SetWindowsHookExW(WH_GETMESSAGE,keyboard_hook,nullptr,GetCurrentThreadId());if(!hook)throw std::runtime_error("Cannot install editor thread keyboard routing");}
 try{targets.push_back(target);}catch(...){if(targets.empty()){UnhookWindowsHookEx(hook);hook=nullptr;}throw;}
 impl_->target=std::move(target);
}
EditorKeyboard::~EditorKeyboard(){auto target=impl_->target;remove(target);target->window=nullptr;target->view.reset();}
void EditorKeyboard::focus(bool focused){
 auto target=impl_->target;auto view=target->view;if(!view)return;
 EditorCallbackGuard active;
 if(!focused)for(auto& key:target->keys)if(key.down){const auto saved=key;key={};view->onKeyUp(saved.character,saved.code,0);}
 view->onFocus(focused);
}
size_t editor_keyboard_routes()noexcept{return targets.size();}
}
