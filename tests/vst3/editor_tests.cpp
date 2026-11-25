#include "vst3/core.hpp"
#include "vst3/rack_factory.hpp"
#include <Windows.h>
#include <array>
#include <thread>
#include <stdexcept>
#include <iostream>
using namespace nexus;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string id(const char* path,const char* name){for(const auto& info:vst3::inspect(path))if(info.name==name)return info.id;throw std::runtime_error("Fixture id");}
int main(int argc,char** argv){try{
 check(argc==3,"Fixtures required");const auto cid=id(argv[1],"NEXUS Gain16 fixture");
 // Warm up native control focus/input infrastructure before measuring cycles.
 const auto cold=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
 {vst3::Effect warm(argv[1],cid);warm.prepare({48000,2,1024});warm.open_editor(false);SendMessageW(GetDlgItem(GetDlgItem(static_cast<HWND>(warm.editor_window()),1000),1001),BM_CLICK,0,0);warm.close_editor();}
 {
  vst3::Effect preset(argv[1],cid);preset.prepare({48000,2,1024});preset.open_editor(false);
  auto hwnd=static_cast<HWND>(preset.editor_window());auto child=GetDlgItem(hwnd,1000);
  for(int cycle=0;cycle<8;++cycle){
   SendMessageW(GetDlgItem(child,1009),BM_CLICK,0,0);
   check(!preset.needs_reprepare(),"Parameter-only preset stopped audio");preset.service_control();
   std::array<float,1024> l{},r{};l.fill(.2f);r.fill(.2f);float* p[]{l.data(),r.data()};preset.process({p,2,1024});
   check(std::abs(l.back()-.14f)<1e-6f&&IsWindow(hwnd),"Parameter-only preset failed audio/editor continuity");
  }
 }
 {
  auto n=default_node(ModuleKind::vst3,99);n.vst3.path=argv[1];n.vst3.class_id=cid;n.vst3.parameters={{7,.54}};
  LiveRack live({48000,2,1024},{n},false,0,vst3::rack_factory(),true);live.open_editor(99,false);
  HWND hwnd=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM raw)->BOOL{wchar_t name[256]{};GetWindowTextW(w,name,256);if(std::wstring(name)==L"NEXUS | NEXUS Gain16 fixture"){*reinterpret_cast<HWND*>(raw)=w;return false;}return true;},reinterpret_cast<LPARAM>(&hwnd));
  check(IsWindow(hwnd),"Reconfiguration test editor missing");
  std::array<float,1024> l{},r{};float* p[]{l.data(),r.data()};l.fill(.2f);r.fill(.2f);live.process({p,2,1024});
  check(live.service_control(),"Latency restart missing");
  for(int waiting=0;waiting<3;++waiting){
   l.fill(.2f);r.fill(.2f);live.process({p,2,1024});
   for(auto value:l)check(value>.05f,"Restart wait inserted an audible silence block");
  }
  live.request_snapshot();
  for(int i=0;i<100&&!live.snapshot_ready();++i){l.fill(.2f);r.fill(.2f);live.process({p,2,1024});}
  check(live.snapshot_ready()&&live.reconfigure_snapshot(),"In-place reconfiguration failed");
  for(int i=0;i<20;++i){l.fill(.2f);r.fill(.2f);live.process({p,2,1024});}
  check(IsWindow(hwnd)&&live.latency_frames()==32&&!live.snapshot_busy(),"Reconfiguration closed editor or failed resume");
  check(std::abs(l.back()-.22f)<1e-6f&&!live.service_control(),"Reconfiguration reverted preset or restarted endlessly");
 }
 const auto userBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
 std::cout<<"USER initialization cold="<<cold<<" warmed="<<userBefore<<"\n";
 HWND destroyed=nullptr;
 {
  vst3::Effect effect(argv[1],cid),peer(argv[1],cid);effect.prepare({48000,2,1024});peer.prepare({48000,2,1024});
  effect.open_editor(false);auto hwnd=static_cast<HWND>(effect.editor_window());check(IsWindow(hwnd),"Native parent missing");
  effect.open_editor(false);check(effect.editor_window()==hwnd,"Duplicate editor window");
  peer.open_editor(false);check(peer.editor_window()!=hwnd,"Instances share a window");
  auto child=GetDlgItem(hwnd,1000);check(child&&GetDlgItem(child,1001),"Original plugin child absent");
  SendMessageW(GetDlgItem(child,1001),BM_CLICK,0,0);
  std::array<float,1024> left{},right{};float* planes[]{left.data(),right.data()};left.fill(.2f);right.fill(.2f);
  effect.process({planes,2,1024});check(left[16]==.1f,"Editor did not change the actual processor");
  left.fill(.2f);right.fill(.2f);peer.process({planes,2,1024});check(left[16]==.2f,"Editor affected another instance");
  SendMessageW(GetDlgItem(child,1002),BM_CLICK,0,0);RECT size{};GetClientRect(hwnd,&size);check(size.right==480&&size.bottom==260,"Plugin-requested resize");
  SendMessageW(hwnd,WM_CLOSE,0,0);check(!IsWindow(hwnd)&&!effect.editor_window(),"Close left a native view");
  effect.open_editor(false);left.fill(.2f);right.fill(.2f);effect.process({planes,2,1024});check(left[16]==.1f,"Reopen reset audio state");
  bool rejected=false;std::thread wrong([&]{try{effect.open_editor(false);}catch(const std::exception&){rejected=true;}});wrong.join();check(rejected,"Wrong-thread editor accepted");
  effect.close_editor();
  for(size_t i=0;i<20;++i){effect.open_editor(false);auto window=static_cast<HWND>(effect.editor_window());effect.close_editor();check(!IsWindow(window),"Repeated close leaked native window");}
  effect.open_editor(false);destroyed=static_cast<HWND>(effect.editor_window());
 }
 check(!IsWindow(destroyed),"Effect destruction retained editor");
 {
  vst3::Effect noEditor(argv[2],id(argv[2],"ADelay"));bool rejected=false;try{noEditor.open_editor(false);}catch(const std::exception&){rejected=true;}check(rejected&&!noEditor.editor_window(),"Missing editor not reported");
 }
 {
  vst3::Effect failed(argv[1],cid);vst3::State state{cid,std::vector<uint8_t>(8),{}};
  const double value=.73;memcpy(state.component.data(),&value,8);failed.restore_state(state);
  bool rejected=false;try{failed.open_editor(false);}catch(const std::exception&){rejected=true;}
  check(rejected&&!failed.editor_window(),"Failed attachment not cleaned up");
 }
 {
  vst3::Effect opaque(argv[1],cid);opaque.prepare({48000,2,1024});opaque.open_editor(false);
  auto child=GetDlgItem(static_cast<HWND>(opaque.editor_window()),1000);
  auto click=[&](int id){SendMessageW(GetDlgItem(child,id),BM_CLICK,0,0);check(GetPropW(child,L"NexusOpaqueRequestOk")!=nullptr,"Handler2 request was not accepted");};
  check(!opaque.editor_dirty(),"Fresh editor unexpectedly dirty");click(1004);
  check(opaque.editor_dirty()&&opaque.read_parameter(7)==.5,"Opaque-only change not detected independently");click(1005);check(opaque.editor_dirty(),"Clean hint discarded uncaptured state");
  opaque.suspend();const auto saved=opaque.capture_state();
  check(saved.controller==std::vector<uint8_t>({42,0,0,0})&&!opaque.editor_dirty(),"Opaque controller state was reset during save");
  {vst3::Effect restored(argv[1],cid);restored.restore_state(saved);restored.prepare({48000,2,1024});restored.open_editor(false);
   check(intptr_t(GetPropW(GetDlgItem(static_cast<HWND>(restored.editor_window()),1000),L"NexusOpaque"))==42,"Opaque editor setting not restored");}
  click(1006);opaque.suspend();bool failed=false;try{opaque.capture_state();}catch(const std::exception&){failed=true;}
  check(failed&&opaque.editor_dirty(),"Failed controller capture acknowledged dirty state");click(1004);opaque.capture_state();check(!opaque.editor_dirty(),"Opaque capture recovery");
  click(1007);const auto first=opaque.capture_state();
  check(first.controller==std::vector<uint8_t>({42,0,0,0})&&opaque.editor_dirty(),"Dirty notification during capture was lost");
  const auto second=opaque.capture_state();check(second.controller==std::vector<uint8_t>({43,0,0,0})&&!opaque.editor_dirty(),"Follow-up opaque capture");
  const auto rejected=opaque.rejected_host_requests();click(1008);check(!opaque.editor_dirty()&&opaque.rejected_host_requests()==rejected+1,"Wrong-thread dirty notification accepted");
  opaque.close_editor();
 }
 const auto userAfter=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
 std::cout<<"USER handles before="<<userBefore<<" after="<<userAfter<<"\n";
 check(userAfter<=userBefore+2,"Native user handle growth");
 std::cout<<"PASS: original HWND view, actual parameter editing, independent instances, resize, close/reopen, failed attach, destruction and opaque-state notifications/capture/recovery; USER handles "<<userBefore<<" -> "<<userAfter<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}}
