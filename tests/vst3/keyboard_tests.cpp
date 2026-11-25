#include "vst3/core.hpp"
#include "vst3/keyboard.hpp"
#include "pluginterfaces/base/keycodes.h"
#include <Windows.h>
#include <array>
#include <string>
#include <stdexcept>
#include <iostream>
#include <cmath>
using namespace nexus;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::string id(const char* path){for(const auto& item:vst3::inspect(path))if(item.name=="NEXUS Gain16 fixture")return item.id;throw std::runtime_error("Fixture identity");}
struct KeyboardState{BYTE original[256]{};KeyboardState(){GetKeyboardState(original);BYTE empty[256]{};SetKeyboardState(empty);}~KeyboardState(){SetKeyboardState(original);}};
int main(int argc,char** argv){try{
 check(argc==2,"Fixture required");KeyboardState restore;
 auto cid=id(argv[1]);vst3::Effect effect(argv[1],cid),peer(argv[1],cid);effect.prepare({48000,2,1024});peer.prepare({48000,2,1024});
 effect.open_editor(false);peer.open_editor(false);check(vst3::editor_keyboard_routes()==2,"Editor routes not registered");
 auto window=static_cast<HWND>(effect.editor_window()),other=static_cast<HWND>(peer.editor_window());
 auto child=GetDlgItem(window,1000),edit=GetDlgItem(child,1003);check(edit,"Native input field");
 auto host=CreateWindowExW(0,L"STATIC",L"Local accelerator sentinel",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 check(host,"Host sentinel");ACCEL accelerator{FVIRTKEY,VK_F2,4001};auto table=CreateAcceleratorTableW(&accelerator,1);check(table,"Accelerator sentinel");
 size_t accelerated=0;
 auto pump=[&]{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
  if(TranslateAcceleratorW(host,table,&message)){++accelerated;continue;}TranslateMessage(&message);DispatchMessageW(&message);
 }};
 auto key=[&](HWND target,UINT vk,LPARAM extra=0){const LPARAM down=1|(LPARAM(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC))<<16)|extra;PostMessageW(target,WM_KEYDOWN,vk,down);pump();auto released=GetFocus();PostMessageW(released?released:target,WM_KEYUP,vk,down|(LPARAM(1)<<30)|(LPARAM(1)<<31));pump();};
 SetFocus(edit);check(GetFocus()==edit,"Native editor focus");key(edit,VK_F2);
 check(accelerated==0,"Handled plugin key reached local host accelerator");
 check(intptr_t(GetPropW(child,L"NexusKeyCode"))==Steinberg::KEY_F2&&intptr_t(GetPropW(child,L"NexusKeyUp"))==Steinberg::KEY_F2,"Special key mapping/up");
 std::array<float,1024> left{},right{};float* planes[]{left.data(),right.data()};left.fill(.2f);right.fill(.2f);effect.process({planes,2,1024});check(effect.read_parameter(7)==.4&&std::abs(left[16]-.16f)<.000001f,"Keyboard did not edit actual processor");
 SetWindowTextW(edit,L"");key(edit,'A');wchar_t ordinary[16]{};GetWindowTextW(edit,ordinary,16);
 std::cout<<"ordinary units="<<int(ordinary[0])<<","<<int(ordinary[1])<<" viewchar="<<intptr_t(GetPropW(child,L"NexusCharacter"))<<" focus="<<(GetFocus()==edit)<<"\n";
 check(std::wstring(ordinary)==L"a"&&intptr_t(GetPropW(child,L"NexusCharacter"))=='a',"Key translation duplicated/lost native character");
 // WM_CHAR fallback is handled by the real native EDIT, without duplicating it.
 SetWindowTextW(edit,L"");for(wchar_t value:std::wstring(L"NEXUS \u00f1\u03a9"))PostMessageW(edit,WM_CHAR,value,0);pump();
 wchar_t text[128]{};GetWindowTextW(edit,text,128);check(std::wstring(text)==L"NEXUS \u00f1\u03a9","Native Unicode text fallback");
 SetWindowTextW(edit,L"");for(wchar_t value:std::wstring(L"0.375"))PostMessageW(edit,WM_CHAR,value,0);pump();key(edit,VK_RETURN);
 left.fill(.2f);right.fill(.2f);effect.process({planes,2,1024});check(std::abs(left[16]-.15f)<.000001f,"Numeric text/Enter did not commit");
 key(edit,VK_RETURN,LPARAM(1)<<24);check(intptr_t(GetPropW(child,L"NexusKeyCode"))==Steinberg::KEY_ENTER,"Numpad Enter mapping");
 BYTE state[256]{};state[VK_CONTROL]=0x80;state[VK_SHIFT]=0x80;SetKeyboardState(state);key(edit,VK_F2);
 check(intptr_t(GetPropW(child,L"NexusModifiers"))==(Steinberg::kCommandKey|Steinberg::kShiftKey),"Control/Shift mapping");
 ZeroMemory(state,sizeof(state));SetKeyboardState(state);SetFocus(edit);key(edit,VK_TAB);check(GetFocus()!=edit&&IsChild(window,GetFocus()),"Tab native focus navigation");
 state[VK_SHIFT]=0x80;SetKeyboardState(state);key(GetFocus(),VK_TAB);check(GetFocus()==edit,"Shift Tab focus navigation");ZeroMemory(state,sizeof(state));SetKeyboardState(state);
 SetFocus(other);key(other,VK_F2);left.fill(.2f);right.fill(.2f);peer.process({planes,2,1024});check(std::abs(left[16]-.16f)<.000001f,"Second editor keyboard route");
 SetFocus(host);key(host,VK_F2);check(accelerated==1,"Other window keyboard was intercepted");
 DestroyAcceleratorTable(table);DestroyWindow(host);
 effect.close_editor();peer.close_editor();check(vst3::editor_keyboard_routes()==0&&!vst3::editor_callback_active(),"Keyboard routes/callback guards leaked");
 std::cout<<"PASS: focused editor routing, handled-key accelerator exclusion, native Unicode/numeric text, Enter/numpad Enter, modifiers, Tab/Shift-Tab, two instances and hook cleanup; synthetic thread-local messages, no global OBS hotkey claim\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}}
