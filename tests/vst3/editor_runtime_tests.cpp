#include "obs/vst3_runtime.hpp"
#include "vst3/core.hpp"
#include "vst3/keyboard.hpp"
#include <QApplication>
#include <Windows.h>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <iostream>
using namespace nexus;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){QApplication app(argc,argv);try{
 check(argc==2,"Fixture path");check(obs_bridge::initialize_vst3_runtime(),"Initialize control runtime");
 auto node=default_node(ModuleKind::vst3,1);node.vst3.path=argv[1];
 for(const auto& item:vst3::inspect(argv[1]))if(item.name=="NEXUS Gain16 fixture")node.vst3.class_id=item.id;
 check(!node.vst3.class_id.empty(),"Fixture class");
 auto closed=[&](HWND window){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(IsWindow(window)){check(std::chrono::steady_clock::now()<end,"Deferred destruction timeout");QApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 };
 auto create=[&]{auto rack=obs_bridge::make_vst3_rack({48000,2,1024},{node},false);rack->open_editor(1,false);return rack;};
 auto window=[]()->HWND{HWND result=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND value,LPARAM raw)->BOOL{
  wchar_t title[256]{};GetWindowTextW(value,title,256);if(std::wstring(title)==L"NEXUS | NEXUS Gain16 fixture"){*reinterpret_cast<HWND*>(raw)=value;return false;}return true;
 },reinterpret_cast<LPARAM>(&result));return result;};
 {
  auto rack=create();auto hwnd=window();check(hwnd,"Editor missing");
  {vst3::EditorCallbackGuard callback;rack.reset();QApplication::processEvents();check(IsWindow(hwnd),"Rack destroyed inside an active GUI callback");}
  closed(hwnd);check(vst3::editor_keyboard_routes()==0,"Deferred keyboard hook survived");
 }
 {
  auto rack=create();auto hwnd=window();rack.reset();check(!IsWindow(hwnd),"Ordinary UI deletion was not immediate");
 }
 {
  auto rack=create();auto hwnd=window();std::thread worker([owned=std::move(rack)]()mutable{owned.reset();});worker.join();
  check(IsWindow(hwnd),"Worker destroyed native editor directly");closed(hwnd);
 }
 check(!vst3::editor_callback_active()&&vst3::editor_keyboard_routes()==0,"GUI callback/router state leaked");obs_bridge::shutdown_vst3_runtime();
 std::cout<<"PASS: GUI callback retirement waits for return, ordinary UI deletion, worker retirement, native window and keyboard hook cleanup\n";return 0;
}catch(const std::exception& error){obs_bridge::shutdown_vst3_runtime();std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}}
