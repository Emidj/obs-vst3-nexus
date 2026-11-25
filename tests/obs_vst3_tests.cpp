#include "obs/rack_settings.hpp"
#include "vst3/core.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QTemporaryDir>
#include <util/platform.h>
#include <array>
#include <Windows.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>
#include <iostream>
using namespace nexus;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
const char* name(void*){return "VST3 bridge signal";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
struct Captured{size_t blocks=0;float peak=0;bool valid=true;uint64_t timestamp=0;};
void capture(void* p,obs_source_t*,const audio_data* d,bool){
 auto& c=*static_cast<Captured*>(p);++c.blocks;c.peak=0;c.timestamp=d->timestamp;
 for(size_t channel=0;channel<2;++channel){
  if(!d->data[channel]){c.valid=false;continue;}
  auto* values=reinterpret_cast<const float*>(d->data[channel]);
  for(size_t i=0;i<d->frames;++i){if(!std::isfinite(values[i]))c.valid=false;c.peak=(std::max)(c.peak,std::abs(values[i]));}
 }
}
HWND original_editor_window(){
 HWND result=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND value,LPARAM raw)->BOOL{
  wchar_t title[256]{};GetWindowTextW(value,title,256);if(std::wstring(title)==L"NEXUS | NEXUS Gain16 fixture"){*reinterpret_cast<HWND*>(raw)=value;return false;}return true;
 },reinterpret_cast<LPARAM>(&result));return result;
}
void events(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
QWidget* window_widget(const char* name){
 for(auto* w:QApplication::topLevelWidgets()){
  if(w->objectName()==name)return w;
  if(auto* child=w->findChild<QWidget*>(name))return child;
 }
 return nullptr;
}
template<class F>void wait(F done){
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(!done()){if(std::chrono::steady_clock::now()>=end){
    if(auto* picker=window_widget("nexus_vst3_picker"))
        if(auto* status=picker->findChild<QLabel*>("vst3_scan_status"))std::cerr<<"Picker status: "<<status->text().toStdString()<<"\n";
    throw std::runtime_error("Actual picker timeout");
  }events();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
std::string class_id(const char* path,const char* wanted){
 for(const auto& c:vst3::inspect(path))if(c.name==wanted)return c.id;
 throw std::runtime_error("Missing fixture class");
}
}
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=4)return 2;
 bool started=false;obs_source_t* filter=nullptr;obs_source_t* input=nullptr;
 std::thread sender;std::atomic<bool> stop{false};
 try{
  wchar_t loaded[MAX_PATH]{};
  check(GetModuleFileNameW(GetModuleHandleW(L"obs.dll"),loaded,MAX_PATH)>0,"Loaded libobs path");
  std::cout<<"Loaded libobs: "<<std::filesystem::path(loaded).u8string()<<"\n";
  std::cout<<"Runtime version: "<<obs_get_version_string()<<"\n";
  check(GetModuleFileNameW(GetModuleHandleW(L"Qt6Core.dll"),loaded,MAX_PATH)>0,"Loaded Qt path");
  std::cout<<"Loaded Qt: "<<std::filesystem::path(loaded).u8string()<<"; version "<<qVersion()<<"\n";
  QTemporaryDir temp;check(temp.isValid(),"Temporary configuration");
  auto node=default_node(ModuleKind::vst3,1);node.vst3.path=argv[2];
  node.vst3.class_id=class_id(argv[2],"ADelay");node.vst3.parameters={{100,.015625}};
  RackDocument doc{{node},{node},false,false};
  check(obs_startup("en-US",temp.path().toUtf8().constData(),nullptr),"OBS startup");started=true;
  obs_audio_info audio{};audio.samples_per_sec=48000;audio.speakers=SPEAKERS_STEREO;
  check(obs_reset_audio(&audio),"OBS audio setup");
  obs_module_t* module=nullptr;check(obs_open_module(&module,argv[1],".")==MODULE_SUCCESS&&obs_init_module(module),"VST3 OBS DLL load");
  auto* settings=obs_data_create();obs_bridge::write_rack(settings,doc);
  filter=obs_source_create_private("nexus_audio_rack","VST3 actual filter",settings);obs_data_release(settings);
  check(filter,"Filter create");
  calldata_t status;calldata_init(&status);
  check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status),"Actual filter context unavailable");
  calldata_free(&status);
  obs_source_info info{};info.id="nexus_vst3_signal";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_AUDIO;
  info.get_name=name;info.create=create;info.destroy=destroy;obs_register_source(&info);
  input=obs_source_create_private(info.id,"VST3 pipeline",nullptr);check(input,"Input");
  obs_source_filter_add(input,filter);Captured captured;
  obs_source_add_audio_capture_callback(input,capture,&captured);
  std::array<float,1024> left{},right{};size_t sequence=0;const auto origin=os_gettime_ns();
  auto send=[&]{
   left.fill(.2f);right.fill(.2f);
   obs_source_audio a{};a.frames=1024;a.samples_per_sec=48000;a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;
   a.timestamp=origin+sequence++*1024*1000000000ULL/48000;
   a.data[0]=reinterpret_cast<const uint8_t*>(left.data());a.data[1]=reinterpret_cast<const uint8_t*>(right.data());
   obs_source_output_audio(input,&a);
  };
  for(size_t i=0;i<8;++i)send();
  check(captured.valid&&captured.blocks==8&&captured.peak==.2f,"Actual VST3 audio");
  sender=std::thread([&]{while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
  obs_source_save(filter);
  stop.store(true);sender.join();
  settings=obs_source_get_settings(filter);
  check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Live save failed");
  auto saved=obs_bridge::read_rack(settings);
  check(saved.a[0].vst3.has_state&&!saved.a[0].vst3.component_state.empty()&&saved.a[0].vst3.parameters.empty(),"OBS save did not capture VST3 state");
  const std::string stored=obs_data_get_json(settings);obs_data_release(settings);
  // Resume after state capture before another operation.
  for(size_t i=0;i<8;++i)send();
  check(captured.valid&&captured.peak==.2f,"Audio failed after save");
  // Synchronous OBS serialization from a worker is marshalled to UI with a
  // bounded wait; UI never writes the caller settings after a timeout.
  {
      stop.store(false);std::atomic<bool> finished{false};
      sender=std::thread([&]{while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
      std::thread saver([&]{obs_source_save(filter);finished.store(true);});
      wait([&]{return finished.load();});saver.join();stop.store(true);sender.join();
      settings=obs_source_get_settings(filter);
      check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Worker live save failed");
      check(obs_bridge::read_rack(settings).a[0].vst3.has_state,"Worker state absent");obs_data_release(settings);
      for(size_t i=0;i<8;++i)send();
      std::thread stalled([&]{obs_source_save(filter);});stalled.join();
      settings=obs_source_get_settings(filter);
      const auto preserved=obs_bridge::read_rack(settings);
      check(!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Unpumped UI save did not time out");
      const std::string timeoutError=obs_data_get_string(settings,"rack_error");events();
      check(same_document(preserved,obs_bridge::read_rack(settings))&&timeoutError==obs_data_get_string(settings,"rack_error"),"Late worker capture modified caller settings");
      obs_data_release(settings);
  }
  // Non-UI producers return before UI work; only their latest request applies.
  auto counters=[&]{
      calldata_t args;calldata_init(&args);
      check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&args),"Control diagnostics");
      const auto values=std::make_pair(calldata_int(&args,"control_revision"),calldata_int(&args,"control_updates"));
      calldata_free(&args);return values;
  };
  auto snapshotBusy=[&]{
      calldata_t args;calldata_init(&args);
      check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&args),"Snapshot diagnostics");
      const bool busy=calldata_bool(&args,"snapshot_busy");calldata_free(&args);return busy;
  };
  auto beforeControl=counters();
  std::thread worker([&]{
      for(size_t i=0;i<100;++i){
          auto request=saved;request.bypass=i%2==1;
          auto* values=obs_data_create();obs_bridge::write_rack(values,request);
          obs_source_update(filter,values);obs_data_release(values);
      }
  });worker.join();
  settings=obs_source_get_settings(filter);
  check(obs_data_get_bool(settings,"rack_pending"),"Worker update was not queued");obs_data_release(settings);
  events();auto afterControl=counters();
  check(afterControl.first==beforeControl.first+100&&afterControl.second==beforeControl.second+1,"Worker requests did not coalesce");
  settings=obs_source_get_settings(filter);
  check(!obs_data_get_bool(settings,"rack_pending")&&obs_data_get_bool(settings,"bypass")&&std::string(obs_data_get_string(settings,"rack_error")).empty(),"Latest worker update did not apply");
  obs_data_release(settings);
  // A direct UI edit supersedes a queued older request.
  worker=std::thread([&]{auto request=saved;request.bypass=true;auto* values=obs_data_create();obs_bridge::write_rack(values,request);obs_source_update(filter,values);obs_data_release(values);});worker.join();
  settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);obs_data_release(settings);
  events();settings=obs_source_get_settings(filter);
  check(!obs_data_get_bool(settings,"bypass")&&!obs_data_get_bool(settings,"rack_pending"),"Stale queued update replaced UI edit");
  obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();
  // Exercise real VST3 graph replacements while audio and request production run.
  {
      std::atomic<bool> produced{false};
      const auto startRevision=counters().first;
      stop.store(false);
      sender=std::thread([&]{while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
      std::thread producer([&]{
          for(size_t i=0;i<300;++i){
              auto request=saved;
              request.a[0].vst3.parameters={{100,i%2?.015625:.03125}};
              auto* values=obs_data_create();obs_bridge::write_rack(values,request);
              obs_source_update(filter,values);obs_data_release(values);
              std::this_thread::sleep_for(std::chrono::microseconds(100));
          }
          produced.store(true);
      });
      struct Join{std::thread& t;~Join(){if(t.joinable())t.join();}} join{producer};
      wait([&]{return produced.load();});producer.join();
      wait([&]{return counters().first==startRevision+300;});
      stop.store(true);sender.join();
      settings=obs_source_get_settings(filter);
      check(!obs_data_get_bool(settings,"rack_pending")&&std::string(obs_data_get_string(settings,"rack_error")).empty(),"Concurrent dispatch did not finish");
      auto final=obs_bridge::read_rack(settings);
      check(final.a[0].vst3.parameters.size()==1&&final.a[0].vst3.parameters[0].second==.015625,"Concurrent dispatch lost latest VST3 value");
      obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);obs_data_release(settings);
      for(size_t i=0;i<8;++i)send();
      check(captured.valid&&captured.peak==.2f,"Concurrent dispatch damaged audio");
  }
  // A request arriving during capture must not be overwritten by the older snapshot.
  {
      stop.store(false);std::atomic<bool> posted{false};
      sender=std::thread([&]{
          while(!posted.load()&&!stop.load())std::this_thread::yield();
          while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
      });
      std::thread producer([&]{
          const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
          while(!snapshotBusy()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
          auto request=saved;request.a[0].vst3.parameters={{100,.03125}};
          auto* values=obs_data_create();obs_bridge::write_rack(values,request);obs_source_update(filter,values);obs_data_release(values);
          posted.store(true);
      });
      struct Join{std::thread& t;~Join(){if(t.joinable())t.join();}} join{producer};
      obs_source_save(filter);producer.join();
      settings=obs_source_get_settings(filter);
      check(std::string(obs_data_get_string(settings,"rack_error")).find("Rack changed")!=std::string::npos,"Concurrent capture conflict was not reported");
      check(obs_bridge::read_rack(settings).a[0].vst3.parameters[0].second==.03125,"Old snapshot overwrote newer request");
      obs_data_release(settings);
      const auto resumeDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
      while(snapshotBusy()){
          check(std::chrono::steady_clock::now()<resumeDeadline,"Snapshot did not resume");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      stop.store(true);sender.join();events();
      settings=obs_source_get_settings(filter);
      check(std::string(obs_data_get_string(settings,"rack_error")).empty()&&!obs_data_get_bool(settings,"rack_pending"),"New request did not apply after capture");
      check(obs_bridge::read_rack(settings).a[0].vst3.parameters[0].second==.03125,"New request was lost");
      obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);obs_data_release(settings);
      for(size_t i=0;i<8;++i)send();
  }
  // Positive declared latency is processed and subtracted from OBS timestamps.
  auto delayed=doc;delayed.a[0].vst3.path=argv[3];
  delayed.a[0].vst3.class_id=class_id(argv[3],"NEXUS Gain16 fixture");delayed.a[0].vst3.parameters={{7,.25}};
  settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,delayed);obs_source_update(filter,settings);
  check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Positive-latency effect rejected");obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();
  check(captured.valid&&captured.peak==.1f,"Positive-latency processing level");
  const auto expectedTimestamp=origin+(sequence-1)*1024*1000000000ULL/48000-16*1000000000ULL/48000;
  check(captured.timestamp==expectedTimestamp,"OBS timestamp was not compensated");
  calldata_init(&status);proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status);
  check(calldata_int(&status,"latency_frames")==16,"Declared latency diagnostics");calldata_free(&status);
  // Bypass remains aligned to the plugin delay rather than jumping in time.
  delayed.bypass=true;settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,delayed);obs_source_update(filter,settings);obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();check(captured.peak==.2f,"Latency-aligned bypass level");
  check(captured.timestamp==origin+(sequence-1)*1024*1000000000ULL/48000-16*1000000000ULL/48000,"Bypass changed latency compensation");
  settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();check(captured.peak==.2f,"Return to zero latency");
  // Float64-only effects are converted at the host boundary with no UI mode.
  delayed.bypass=false;delayed.a[0].vst3.class_id=class_id(argv[3],"Float64-only fixture");delayed.a[0].vst3.parameters={{7,.25}};
  settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,delayed);obs_source_update(filter,settings);obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();check(captured.valid&&captured.peak==.1f,"Float64-only effect failed in actual OBS bridge");
  delayed.a[0].vst3.class_id=class_id(argv[3],"NEXUS Gain16 fixture");
  // If a vendor getState outlives the worker deadline after dispatch started,
  // its eventual result must remain isolated from already serialized settings.
  {
      delayed.bypass=false;delayed.a[0].vst3.parameters={{7,.91}};
      settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,delayed);obs_source_update(filter,settings);obs_data_release(settings);
      for(size_t i=0;i<8;++i)send();
      settings=obs_source_get_settings(filter);const auto beforeSlow=obs_bridge::read_rack(settings);obs_data_release(settings);
      stop.store(false);std::atomic<bool> finished{false};
      sender=std::thread([&]{while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
      std::thread slowSave([&]{obs_source_save(filter);finished.store(true);});
      wait([&]{return finished.load();});slowSave.join();stop.store(true);sender.join();
      settings=obs_source_get_settings(filter);
      check(same_document(beforeSlow,obs_bridge::read_rack(settings)),"Late vendor state overwrote serialized settings");
      check(!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Slow vendor state timeout absent");obs_data_release(settings);
      for(size_t i=0;i<8;++i)send();
  }
  // Exercise periodic control service through the actual DLL, with latency,
  // I/O and full-component reload requests from processor/controller feedback.
  auto service=[&]{calldata_t args;calldata_init(&args);check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_service_control",&args),"Service callback");calldata_free(&args);};
  for(double trigger:{.54,.535,.536}){
      delayed.bypass=false;delayed.a[0].vst3.parameters={{7,trigger}};
      settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,delayed);obs_source_update(filter,settings);obs_data_release(settings);
      for(size_t i=0;i<30;++i){send();service();}
      settings=obs_source_get_settings(filter);check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Actual reconfiguration failed");obs_data_release(settings);
      calldata_init(&status);proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status);
      check(calldata_int(&status,"latency_frames")== (trigger==.54?32:16)&&!calldata_bool(&status,"snapshot_busy"),"Actual restart did not resume with new latency");calldata_free(&status);
  }
  // Repeated processing errors have a finite recovery budget, then park dry.
  delayed.a[0].vst3.parameters={{7,.96}};
  settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,delayed);obs_source_update(filter,settings);obs_data_release(settings);
  for(size_t i=0;i<40;++i){send();service();}
  settings=obs_source_get_settings(filter);check(!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Repeated plugin failure was not quarantined");
  check(captured.valid&&captured.peak==.2f,"Quarantined rack did not pass dry audio");
  obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();
  // An unsupported no-input effect must still be refused.
  auto invalid=doc;invalid.a[0].vst3.path=argv[3];
  invalid.a[0].vst3.class_id=class_id(argv[3],"Rejected no-input fixture");invalid.a[0].vst3.parameters={{7,.25}};
  settings=obs_source_get_settings(filter);obs_bridge::write_rack(settings,invalid);obs_source_update(filter,settings);
  check(!std::string(obs_data_get_string(settings,"rack_error")).empty(),"No-input effect accepted");
  for(size_t i=0;i<8;++i)send();
  check(captured.valid&&captured.peak==.2f,"Rejected effect replaced live chain");
  obs_data_release(settings);
  worker=std::thread([&]{auto* values=obs_data_create();obs_bridge::write_rack(values,invalid);obs_source_update(filter,values);obs_data_release(values);});worker.join();
  events();settings=obs_source_get_settings(filter);
  check(obs_bridge::read_rack(settings).active()[0].vst3.class_id==saved.active()[0].vst3.class_id,"Rejected worker request remained in scene");
  check(!obs_data_get_bool(settings,"rack_pending")&&!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Worker rejection status missing");
  obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();
  // A save without incoming audio retains the last snapshot and reports timeout.
  settings=obs_source_get_settings(filter);
  const auto previous=obs_bridge::read_rack(settings);obs_data_release(settings);
  obs_source_save(filter);
  settings=obs_source_get_settings(filter);
  check(!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Stopped-source timeout absent");
  check(same_document(previous,obs_bridge::read_rack(settings)),"Timeout changed saved state");
  obs_data_release(settings);
  for(size_t i=0;i<8;++i)send();
  obs_source_remove_audio_capture_callback(input,capture,&captured);
  obs_source_filter_remove(input,filter);obs_source_release(input);input=nullptr;
  obs_source_release(filter);filter=nullptr;obs_wait_for_destroy_queue();
  QCoreApplication::processEvents();
  // Reload the actual OBS filter from the captured settings.
  settings=obs_data_create_from_json(stored.c_str());obs_data_set_bool(settings,"rack_pending",true);
  filter=obs_source_create_private("nexus_audio_rack","restored VST3",settings);obs_data_release(settings);
  calldata_init(&status);
  check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status),"Restored filter unavailable");
  calldata_free(&status);
  settings=obs_source_get_settings(filter);
  check(!obs_data_get_bool(settings,"rack_pending"),"Reload retained obsolete pending status");obs_data_release(settings);
  // Exercise the actual DLL's Add Effect selector without scanning user folders.
  auto* properties=obs_source_properties(filter);
  auto* button=obs_properties_get(properties,"open_rack_editor");
  check(button,"Editor button");obs_property_button_clicked(button,filter);events();
  auto* editor=window_widget("nexus_rack_editor");check(editor,"Editor unavailable");
  auto select_effect=[&](const char* folder,const char* wanted){
      auto* action=editor->findChild<QAction*>("add_vst3");check(action,"VST3 Add Effect action");action->trigger();events();
      auto* picker=window_widget("nexus_vst3_picker");check(picker,"Actual selector unavailable");
      auto* list=picker->findChild<QListWidget*>("vst3_effects");check(list&&list->count()==0,"Selector scanned installed plugins implicitly");
      picker->findChild<QLineEdit*>("vst3_folder")->setText(QString::fromUtf8(folder));
      picker->findChild<QPushButton*>("vst3_scan_folder")->click();
      wait([&]{return list->count()>0;});
      int match=-1;for(int i=0;i<list->count();++i)if(list->item(i)->text().contains(wanted))match=i;
      check(match>=0,"Selected fixture missing");list->setCurrentRow(match);
      picker->findChild<QPushButton*>("vst3_add")->click();events();
      check(!window_widget("nexus_vst3_picker"),"Selector did not close");
  };
  select_effect(argv[2],"ADelay");
  settings=obs_source_get_settings(filter);
  auto visible=obs_bridge::read_rack(settings);
  check(visible.active().size()==2&&visible.active().back().vst3.name=="ADelay","Selector did not update actual OBS rack");
  obs_data_release(settings);
  check(editor->findChild<QPushButton*>("rack_remove_2"),"VST3 card missing");
  editor->findChild<QPushButton*>("rack_remove_2")->click();events();
  select_effect(argv[3],"Rejected no-input");
  settings=obs_source_get_settings(filter);visible=obs_bridge::read_rack(settings);
  check(visible.active().size()==1&&!editor->findChild<QPushButton*>("rack_remove_2"),"Rejected VST3 remained in visible or saved rack");
  check(!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Rejection explanation missing");
  obs_data_release(settings);
  editor->window()->close();events();obs_properties_destroy(properties);
  obs_source_release(filter);filter=nullptr;obs_wait_for_destroy_queue();
  QCoreApplication::processEvents();
  settings=obs_data_create();obs_bridge::write_rack(settings,saved);
  auto* fleeting=obs_source_create_private("nexus_audio_rack","queued then destroyed",settings);obs_data_release(settings);
  check(fleeting,"Short-lived filter");
  worker=std::thread([&]{auto* values=obs_data_create();obs_bridge::write_rack(values,saved);obs_source_update(fleeting,values);obs_data_release(values);});worker.join();
  obs_source_release(fleeting);obs_wait_for_destroy_queue();events();
  // Worker creation returns while the UI is deliberately not pumping events.
  worker=std::thread([&]{
      auto* values=obs_data_create();obs_bridge::write_rack(values,saved);
      filter=obs_source_create_private("nexus_audio_rack","worker-created VST3",values);obs_data_release(values);
  });worker.join();
  check(filter,"Worker creation returned no source");
  calldata_init(&status);
  check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status),"Worker-created filter has no context");
  check(calldata_bool(&status,"control_pending"),"Initial plugin load was not deferred");calldata_free(&status);
  settings=obs_source_get_settings(filter);check(obs_data_get_bool(settings,"rack_pending"),"Initial pending flag absent");obs_data_release(settings);
  input=obs_source_create_private(info.id,"bootstrap pipeline",nullptr);check(input,"Bootstrap input");
  obs_source_filter_add(input,filter);captured={};obs_source_add_audio_capture_callback(input,capture,&captured);
  for(size_t i=0;i<8;++i)send();
  check(captured.valid&&captured.peak==.2f,"Bootstrap did not pass dry audio");
  events();calldata_init(&status);
  check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status),"Bootstrap diagnostics");
  check(!calldata_bool(&status,"control_pending")&&calldata_int(&status,"control_updates")==1,"Initial VST3 did not prepare on UI");calldata_free(&status);
  for(size_t i=0;i<8;++i)send();check(captured.valid&&captured.peak==.2f,"Deferred plugin audio failed");
  settings=obs_source_get_settings(filter);check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Deferred creation error");obs_data_release(settings);
  obs_source_remove_audio_capture_callback(input,capture,&captured);obs_source_filter_remove(input,filter);
  obs_source_release(input);input=nullptr;obs_source_release(filter);filter=nullptr;obs_wait_for_destroy_queue();events();
  // A failed initial load preserves its serialized request, and a later valid
  // update can recover the same source without creating another context.
  worker=std::thread([&]{auto* values=obs_data_create();obs_bridge::write_rack(values,invalid);
      filter=obs_source_create_private("nexus_audio_rack","failed bootstrap",values);obs_data_release(values);
  });worker.join();check(filter,"Failed bootstrap context");events();
  settings=obs_source_get_settings(filter);
  check(same_document(invalid,obs_bridge::read_rack(settings)),"Initial failure erased serialized request");
  check(!obs_data_get_bool(settings,"rack_pending")&&!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Initial failure status missing");
  obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);
  check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Bootstrap recovery failed");obs_data_release(settings);
  obs_source_release(filter);filter=nullptr;obs_wait_for_destroy_queue();events();
  // UI creation also preserves a missing plugin and allows editing recovery.
  auto missing=saved;missing.a[0].vst3.path+=".missing.vst3";
  settings=obs_data_create();obs_bridge::write_rack(settings,missing);
  filter=obs_source_create_private("nexus_audio_rack","missing plugin editable",settings);obs_data_release(settings);
  check(filter,"Missing plugin source absent");calldata_init(&status);
  check(proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status),"Missing plugin context absent");calldata_free(&status);
  settings=obs_source_get_settings(filter);
  check(same_document(missing,obs_bridge::read_rack(settings))&&!std::string(obs_data_get_string(settings,"rack_error")).empty(),"Missing plugin state not preserved");
  obs_source_save(filter);check(same_document(missing,obs_bridge::read_rack(settings)),"UI save erased missing plugin state");
  std::atomic<bool> missingSaved{false};
  worker=std::thread([&]{obs_source_save(filter);missingSaved.store(true);});
  wait([&]{return missingSaved.load();});worker.join();
  check(same_document(missing,obs_bridge::read_rack(settings)),"Worker save erased missing plugin state");
  obs_bridge::write_rack(settings,saved);obs_source_update(filter,settings);
  check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Missing plugin replacement failed");obs_data_release(settings);
  obs_source_release(filter);filter=nullptr;obs_wait_for_destroy_queue();events();
  // Creation can also be abandoned before its queued plugin load executes.
  worker=std::thread([&]{auto* values=obs_data_create();obs_bridge::write_rack(values,saved);
      auto* abandoned=obs_source_create_private("nexus_audio_rack","abandoned bootstrap",values);obs_data_release(values);
      if(abandoned)obs_source_release(abandoned);
  });worker.join();obs_wait_for_destroy_queue();events();
  // The real Rack Editor button opens the view of the actual rack processor.
  auto editable=doc;editable.a[0].vst3.path=argv[3];editable.a[0].vst3.class_id=class_id(argv[3],"NEXUS Gain16 fixture");editable.a[0].vst3.parameters={{7,.5}};
  settings=obs_data_create();obs_bridge::write_rack(settings,editable);
  filter=obs_source_create_private("nexus_audio_rack","original editor integration",settings);obs_data_release(settings);check(filter,"Original editor filter");
  input=obs_source_create_private(info.id,"original editor signal",nullptr);obs_source_filter_add(input,filter);captured={};obs_source_add_audio_capture_callback(input,capture,&captured);
  for(size_t i=0;i<8;++i)send();
  properties=obs_source_properties(filter);button=obs_properties_get(properties,"open_rack_editor");obs_property_button_clicked(button,filter);events();
  editor=window_widget("nexus_rack_editor");check(editor,"Original editor rack window");
  if(!editor->findChild<QPushButton*>("vst3_open_1"))editor->findChild<QToolButton*>("rack_expand_1")->click();
  auto* openOriginal=editor->findChild<QPushButton*>("vst3_open_1");check(openOriginal&&openOriginal->isEnabled(),"Original editor card button");openOriginal->click();
  auto original=original_editor_window();check(original,"Original native window absent after button click");
  auto originalInput=GetDlgItem(GetDlgItem(original,1000),1003);check(originalInput,"Original native input");
  SetFocus(originalInput);SetWindowTextW(originalInput,L"0.375");PostMessageW(originalInput,WM_KEYDOWN,VK_RETURN,0);PostMessageW(originalInput,WM_KEYUP,VK_RETURN,0);events();
  for(size_t i=0;i<8;++i)send();check(std::abs(captured.peak-.15f)<.000001f,"Original editor keyboard did not edit actual OBS audio");
  SendMessageW(GetDlgItem(GetDlgItem(original,1000),1001),BM_CLICK,0,0);
  for(size_t i=0;i<8;++i)send();check(captured.peak==.1f,"Original editor did not edit actual OBS audio");
  // An unrelated native edit/structural rebuild closes the old original view.
  stop.store(false);sender=std::thread([&]{while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
  obs_source_save(filter);
  settings=obs_source_get_settings(filter);check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Pre-opaque snapshot failed");obs_data_release(settings);
  wait([&]{return !snapshotBusy();});
  calldata_init(&status);proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status);check(!calldata_bool(&status,"editor_dirty"),"Pre-opaque snapshot did not clear edits");calldata_free(&status);
  SendMessageW(GetDlgItem(GetDlgItem(original,1000),1004),BM_CLICK,0,0);
  SendMessageW(GetDlgItem(GetDlgItem(original,1000),1005),BM_CLICK,0,0);
  calldata_init(&status);proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status);check(calldata_bool(&status,"editor_dirty"),"Opaque-only editor change was not marked");calldata_free(&status);
  QAction* addGain=nullptr;for(auto* action:editor->findChildren<QAction*>())if(action->text()=="Gain / Trim")addGain=action;
  check(addGain,"Native Add Effect action");addGain->trigger();
  wait([&]{return !IsWindow(original);});stop.store(true);sender.join();
  for(size_t i=0;i<8;++i)send();
  check(captured.peak==.1f,"Rack UI structural edit discarded original-editor changes");
  settings=obs_source_get_settings(filter);const auto retained=obs_bridge::read_rack(settings);obs_data_release(settings);
  check(retained.active().size()==2&&retained.active()[0].vst3.has_state,"Rack edit did not retain captured original-editor state");
  check(retained.active()[0].vst3.controller_state==std::vector<uint8_t>({42,0,0,0}),"Rack structural edit lost opaque controller data");
  calldata_init(&status);calldata_set_string(&status,"id","1");calldata_set_bool(&status,"visible",false);proc_handler_call(obs_source_get_proc_handler(filter),"nexus_open_vst3_editor",&status);
  check(std::string(calldata_string(&status,"error")).empty(),"Restored opaque editor failed");calldata_free(&status);
  auto opaqueWindow=original_editor_window();check(opaqueWindow&&intptr_t(GetPropW(GetDlgItem(opaqueWindow,1000),L"NexusOpaque"))==42,"Restored actual editor lost opaque choice");
  editor->window()->close();events();obs_properties_destroy(properties);
  obs_source_remove_audio_capture_callback(input,capture,&captured);obs_source_filter_remove(input,filter);obs_source_release(input);input=nullptr;
  obs_source_release(filter);filter=nullptr;obs_wait_for_destroy_queue();events();
  obs_shutdown();started=false;
  std::cout<<"PASS: actual OBS VST3 DLL audio/save/restore, timeout, worker create/update/save, late-capture isolation, adaptive latency/float64/restarts, deferred destruction and actual selector/rollback\n";
  return 0;
 }catch(const std::exception& e){
  stop.store(true);if(sender.joinable())sender.join();
  if(input&&filter)obs_source_filter_remove(input,filter);
  if(input)obs_source_release(input);if(filter)obs_source_release(filter);
  if(started){obs_wait_for_destroy_queue();QCoreApplication::processEvents();obs_shutdown();}
  std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
 }
}

