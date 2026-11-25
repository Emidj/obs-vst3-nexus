// Optional internal evaluator for explicitly selected installed effects.
// Never installed, never changes OBS configuration or writes vendor presets.
#include "vst3/core.hpp"
#include <QApplication>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <Windows.h>
#include <objbase.h>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace nexus;
int main(int argc,char** argv){
 QApplication app(argc,argv);
 if(argc!=4&&argc!=5)return 2;
 const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 QJsonObject report{{"module",QString::fromUtf8(argv[1])},{"class_id",QString::fromUtf8(argv[2])}};
 for(const auto& info:vst3::inspect(argv[1]))if(info.id==argv[2]){
  report.insert("name",QString::fromStdString(info.name));report.insert("vendor",QString::fromStdString(info.vendor));report.insert("version",QString::fromStdString(info.version));
 }
 if(argc==5){
  bool passed=false;
  try{
   vst3::Effect effect(argv[1],argv[2]),peer(argv[1],argv[2]);effect.prepare({48000,2,1024});peer.prepare({48000,2,1024});
   std::array<float,1024> left{},right{};float* planes[]{left.data(),right.data()};
   for(int iteration=0;iteration<3;++iteration){
    effect.open_editor(false);peer.open_editor(false);
    auto window=static_cast<HWND>(effect.editor_window());auto other=static_cast<HWND>(peer.editor_window());
    if(!IsWindow(window)||!IsWindow(other)||window==other)throw std::runtime_error("Original editor HWND missing/shared");
    int children=0;EnumChildWindows(window,[](HWND,LPARAM value)->BOOL{++*reinterpret_cast<int*>(value);return true;},reinterpret_cast<LPARAM>(&children));
    if(!children)throw std::runtime_error("Original editor has no native child window");
    for(int n=0;n<25;++n){
     QApplication::processEvents();left.fill(.01f);right.fill(.02f);effect.process({planes,2,1024});effect.service_control();
     if(effect.needs_reprepare())throw std::runtime_error("Editor initialization requested reconfiguration");
     std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    effect.close_editor();peer.close_editor();if(IsWindow(window)||IsWindow(other))throw std::runtime_error("Editor native window survived close");
   }
   effect.suspend();effect.capture_state();peer.suspend();peer.capture_state();passed=true;
  }catch(const std::exception& error){report.insert("detail",QString::fromUtf8(error.what()));}
  report.insert("status",passed?"pass":"failed");report.insert("scope","hidden native attachment; two instances; three close/reopen cycles; event pump, audio and state; no visual, keyboard, DPI or OBS-frontend qualification");
  QFile output(QString::fromUtf8(argv[3]));if(!output.open(QIODevice::WriteOnly))return 2;output.write(QJsonDocument(report).toJson());output.close();
  if(SUCCEEDED(com))CoUninitialize();std::cout<<(passed?"PASS":"FAIL")<<": external original editor attachment, see JSON\n";return passed?0:1;
 }
 QJsonArray cases;int stereo=0;
 for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)}){
  QJsonObject item{{"rate",rate},{"channels",int(channels)}};
  try{
   vst3::Effect first(argv[1],argv[2]),second(argv[1],argv[2]);
   first.prepare({rate,channels,1024});second.prepare({rate,channels,1024});
   const auto latency=first.latency_frames();
   if(!first.parameters().empty())for(const auto& parameter:first.parameters())if(!parameter.read_only){
    if(!first.set_parameter(parameter.id,first.read_parameter(parameter.id)))throw std::runtime_error("Writable parameter rejected");break;
   }
   std::atomic<bool> done{false},failed{false},cancel{false};std::atomic<size_t> blocks{0};
   std::thread audio([&]{
    std::array<std::array<float,1024>,2> a{},b{};float* pa[]{a[0].data(),a[1].data()},*pb[]{b[0].data(),b[1].data()};
    size_t position=0;
    for(size_t n=0;position<size_t(rate*2)&&!cancel.load();++n){
     const size_t frames=(std::min)(size_t(rate*2)-position,n%3==0?size_t(17):(n%3==1?size_t(257):size_t(1024)));
     for(size_t c=0;c<channels;++c)for(size_t i=0;i<frames;++i){a[c][i]=float(.1*std::sin(double(position+i)*.031));b[c][i]=float(.05*std::sin(double(position+i)*.073));}
     first.process({pa,channels,frames});second.process({pb,channels,frames});
     for(size_t c=0;c<channels;++c)for(size_t i=0;i<frames;++i)if(!std::isfinite(a[c][i])||!std::isfinite(b[c][i]))failed.store(true);
     position+=frames;++blocks;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }done.store(true);
   });
   struct Join{std::thread& thread;std::atomic<bool>& cancel;~Join(){cancel.store(true);if(thread.joinable())thread.join();}} join{audio,cancel};
   while(!done.load()){
    first.service_control();second.service_control();QApplication::processEvents();
    if(first.needs_reprepare()||second.needs_reprepare()){cancel.store(true);failed.store(true);}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }audio.join();
   if(first.needs_reprepare()||second.needs_reprepare()||first.faults()||second.faults())failed.store(true);
   if(failed.load())throw std::runtime_error("Audio failed or requested unsupported qualification-time restart");
   first.suspend();second.suspend();const auto state=first.capture_state();const auto independent=second.capture_state();
   vst3::Effect restored(argv[1],argv[2]);restored.restore_state(state);restored.prepare({rate,channels,1024});
   std::array<float,1024> left{},right{};float* planes[]{left.data(),right.data()};
   for(size_t i=0;i<8;++i){left.fill(.01f);right.fill(.02f);restored.process({planes,channels,1024});}
   if(restored.needs_reprepare())throw std::runtime_error("Restored instance failed");
   for(float value:left)if(!std::isfinite(value))throw std::runtime_error("Restored output nonfinite");
   restored.suspend();restored.capture_state();
   item.insert("status","pass");item.insert("blocks_per_instance",int(blocks.load()));item.insert("declared_latency_frames",int(latency));
   item.insert("component_state_bytes",int(state.component.size()));item.insert("second_state_bytes",int(independent.component.size()));
   item.insert("parameters",int(first.parameters().size()));if(channels==2)++stereo;
  }catch(const std::exception& error){item.insert("status","unsupported_or_failed");item.insert("detail",QString::fromUtf8(error.what()));}
  cases.append(item);
 }
 report.insert("cases",cases);report.insert("stereo_all_rates_pass",stereo==3);
 report.insert("scope","two instances; variable blocks; two seconds simulated per case; state capture/restore; no original UI, OBS frontend, broadcast or prolonged session claim");
 QFile output(QString::fromUtf8(argv[3]));if(!output.open(QIODevice::WriteOnly))return 2;output.write(QJsonDocument(report).toJson());output.close();
 if(SUCCEEDED(com))CoUninitialize();
 std::cout<<(stereo==3?"PASS":"LIMITED")<<": external effect core qualification, see JSON\n";return stereo==3?0:1;
}
