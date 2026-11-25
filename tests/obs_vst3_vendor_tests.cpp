#include "obs/rack_settings.hpp"
#ifdef NEXUS_SINGLE_VST3
#include "obs/single_settings.hpp"
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QTextDocument>
#include <QTextBlock>
#include <QPainter>
#endif
#include "vst3/core.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <util/platform.h>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <iostream>
#include <Windows.h>
#include <filesystem>
using namespace nexus;
namespace {
#ifdef NEXUS_SINGLE_VST3
constexpr auto filter_id="nexus_vst3";
void write_state(obs_data_t* s,const RackDocument& d){obs_bridge::write_single(s,d);}
RackDocument read_state(obs_data_t* s){return obs_bridge::read_single(s);}
#else
constexpr auto filter_id="nexus_audio_rack";
void write_state(obs_data_t* s,const RackDocument& d){obs_bridge::write_rack(s,d);}
RackDocument read_state(obs_data_t* s){return obs_bridge::read_rack(s);}
#endif
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const char* name(void*){return "External VST3 qualification signal";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
struct Captured{bool compare=false;std::array<float,1024> expected[2]{};std::atomic<bool> changed{false};std::atomic<float> peak{0};std::atomic<size_t> blocks{0};std::atomic<bool> invalid{false},nonzero{false};};
void capture(void* raw,obs_source_t*,const audio_data* data,bool){
 auto& captured=*static_cast<Captured*>(raw);captured.blocks.fetch_add(1);float peak=0;
 for(size_t c=0;c<2;++c){if(!data->data[c]){captured.invalid.store(true);continue;}
  const auto* samples=reinterpret_cast<const float*>(data->data[c]);
  for(size_t i=0;i<data->frames;++i){if(!std::isfinite(samples[i]))captured.invalid.store(true);if(captured.compare&&(i>=1024||std::abs(samples[i]-captured.expected[c][i])>1e-6f))captured.changed.store(true);peak=(std::max)(peak,std::abs(samples[i]));if(std::abs(samples[i])>.000001f)captured.nonzero.store(true);}
 }
 captured.peak.store(peak);
}
template<class F>void wait(F predicate){
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(!predicate()){check(std::chrono::steady_clock::now()<end,"Vendor OBS operation timeout");QApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
}
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=5&&argc!=6)return 2;
 bool started=false;std::array<obs_source_t*,2> filters{},inputs{};
 std::array<Captured,2> captured;std::thread sender,saver;std::atomic<bool> stop{false};
 QJsonObject report{{"module",QString::fromUtf8(argv[2])},{"class_id",QString::fromUtf8(argv[3])}};
 auto cleanup=[&]{
  stop.store(true);if(sender.joinable())sender.join();if(saver.joinable())saver.join();
  for(size_t i=0;i<2;++i){
   if(inputs[i]){obs_source_remove_audio_capture_callback(inputs[i],capture,&captured[i]);if(filters[i])obs_source_filter_remove(inputs[i],filters[i]);obs_source_release(inputs[i]);inputs[i]=nullptr;}
   if(filters[i]){obs_source_release(filters[i]);filters[i]=nullptr;}
  }
  if(started){obs_wait_for_destroy_queue();QApplication::processEvents();obs_shutdown();started=false;}
 };
 try{
#ifdef NEXUS_SINGLE_VST3
  {
   auto* s=obs_data_create();check(obs_bridge::read_single(s).a.empty(),"New filter is not empty");
   auto node=default_node(ModuleKind::vst3,1);node.vst3.path=argv[2];node.vst3.class_id=argv[3];
   for(auto bad:std::vector<RackDocument>{{{node,node},{},false,false},{{default_node(ModuleKind::gain,1)},{},false,false},{{node},{node},false,false},{{node},{},true,false}}){
    bool rejected=false;try{obs_bridge::write_single(s,bad);}catch(const std::exception&){rejected=true;}check(rejected,"Single-effect schema accepted rack/native data");
   }
   obs_data_set_int(s,"rack_version",2);bool rejected=false;try{obs_bridge::read_single(s);}catch(const std::exception&){rejected=true;}check(rejected,"Legacy rack silently converted");obs_data_release(s);
  }
#endif
  QTemporaryDir temp;check(temp.isValid(),"Temporary OBS configuration");
  check(obs_startup("en-US",temp.path().toUtf8().constData(),nullptr),"OBS startup");started=true;
  wchar_t loaded[32768]{};
  GetModuleFileNameW(GetModuleHandleW(L"obs.dll"),loaded,32768);
  std::cout<<"Loaded libobs: "<<std::filesystem::path(loaded).u8string()<<"\nRuntime version: "<<obs_get_version_string()<<"\n";
  GetModuleFileNameW(GetModuleHandleW(L"Qt6Core.dll"),loaded,32768);
  std::cout<<"Loaded Qt: "<<std::filesystem::path(loaded).u8string()<<"; version "<<qVersion()<<"\n";
  report.insert("obs_version",QString::fromUtf8(obs_get_version_string()));report.insert("qt_version",qVersion());
  obs_audio_info format{};format.samples_per_sec=48000;format.speakers=SPEAKERS_STEREO;check(obs_reset_audio(&format),"Audio format");
  obs_module_t* module=nullptr;check(obs_open_module(&module,argv[1],".")==MODULE_SUCCESS&&obs_init_module(module),"Actual NEXUS module");
#ifdef NEXUS_SINGLE_VST3
  check(std::string(obs_source_get_display_name(filter_id))=="VST 3 NEXUS","Filter display name mismatch");
  auto* legacyName=obs_source_create_private(filter_id,"VST3 NEXUS (development)",nullptr);check(legacyName,"Name migration fixture");
  wait([&]{return std::string(obs_source_get_name(legacyName))=="VST 3 NEXUS";});
  const char* locales[]{"en-US","es-ES","it-IT","fr-FR","de-DE"};
  const char* choices[]{u8"Choose VST3 effect...",u8"Elegir efecto VST3...",u8"Scegli effetto VST3...",u8"Choisir un effet VST3...",u8"VST3-Effekt ausw\u00e4hlen..."};
  const char* searches[]{u8"Search effects",u8"Buscar efectos",u8"Cerca effetti",u8"Rechercher des effets",u8"Effekte suchen"};
  for(int language=0;language<5;++language){
   obs_set_locale(locales[language]);auto* translated=obs_source_properties(legacyName);
   check(std::string(obs_property_description(obs_properties_get(translated,"choose_vst3")))==choices[language],"OBS language did not translate filter controls");
   obs_property_button_clicked(obs_properties_get(translated,"choose_vst3"),legacyName);QApplication::processEvents();
   QWidget* chooser=nullptr;for(auto* w:QApplication::allWidgets())if(w->objectName()=="nexus_vst3_picker")chooser=w;
   check(chooser&&chooser->findChild<QLineEdit*>("vst3_search")->placeholderText()==QString::fromUtf8(searches[language]),"Picker language mismatch");
   check(chooser->grab().save(QString::fromUtf8(argv[4])+"."+locales[language]+".picker.png"),"Localized picker preview failed");
   chooser->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);obs_properties_destroy(translated);
  }
  obs_set_locale("en-US");obs_source_release(legacyName);report.insert("languages_checked",5);
#endif
  auto node=default_node(ModuleKind::vst3,1);node.vst3.path=argv[2];node.vst3.class_id=argv[3];
  for(const auto& info:vst3::inspect(argv[2]))if(info.id==argv[3]){node.vst3.name=info.name;node.vst3.vendor=info.vendor;report.insert("vendor",QString::fromStdString(info.vendor));report.insert("version",QString::fromStdString(info.version));}
  #ifdef NEXUS_SINGLE_VST3
  RackDocument document{{node},{},false,false};
#else
  RackDocument document{{node},{node},false,false};
#endif
  obs_source_info info{};info.id="nexus_external_signal";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_AUDIO;
  info.get_name=name;info.create=create;info.destroy=destroy;obs_register_source(&info);
  for(size_t i=0;i<2;++i){
   auto* settings=obs_data_create();write_state(settings,document);
   filters[i]=obs_source_create_private(filter_id,i?"vendor B":"vendor A",settings);obs_data_release(settings);check(filters[i],"Filter source");
   inputs[i]=obs_source_create_private(info.id,i?"signal B":"signal A",nullptr);check(inputs[i],"Signal source");
   obs_source_filter_add(inputs[i],filters[i]);obs_source_add_audio_capture_callback(inputs[i],capture,&captured[i]);
  }
  const auto origin=os_gettime_ns();size_t sequence=0;bool jitterClock=false;
  auto send=[&]{
   std::array<float,1024> left{},right{};
   for(size_t source=0;source<2;++source){
    for(size_t i=0;i<1024;++i){left[i]=float((source+1)*.05*std::sin(double(sequence*1024+i)*.03));right[i]=-left[i];}
    obs_source_audio block{};block.frames=1024;block.samples_per_sec=48000;block.speakers=SPEAKERS_STEREO;block.format=AUDIO_FORMAT_FLOAT_PLANAR;
    block.timestamp=origin+sequence*1024*1000000000ULL/48000;block.data[0]=reinterpret_cast<const uint8_t*>(left.data());block.data[1]=reinterpret_cast<const uint8_t*>(right.data());
    if(jitterClock&&sequence%2)block.timestamp-=200000; // 0.2 ms device clock variation
    if(captured[source].compare){captured[source].expected[0]=left;captured[source].expected[1]=right;}
    obs_source_output_audio(inputs[source],&block);
   }++sequence;
  };
  sender=std::thread([&]{while(!stop.load()){send();std::this_thread::sleep_for(std::chrono::milliseconds(2));}});
  wait([&]{return captured[0].blocks.load()>30&&captured[1].blocks.load()>30;});
  if(argc==6){
   for(int cycle=0;cycle<3;++cycle){
    for(auto* filter:filters){
     calldata_t args;calldata_init(&args);calldata_set_string(&args,"id","1");calldata_set_bool(&args,"visible",false);
     const bool called=proc_handler_call(obs_source_get_proc_handler(filter),"nexus_open_vst3_editor",&args);
     const std::string error=calldata_string(&args,"error")?calldata_string(&args,"error"):"";calldata_free(&args);
     check(called,"Original editor callback missing");if(!error.empty())throw std::runtime_error(error);
    }
    std::vector<HWND> windows;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM raw)->BOOL{
     wchar_t title[256]{};GetWindowTextW(window,title,256);
     if(std::wstring(title).find(L"NEXUS | ")==0)reinterpret_cast<std::vector<HWND>*>(raw)->push_back(window);return true;
    },reinterpret_cast<LPARAM>(&windows));
    check(windows.size()==2,"Original editor windows not independent");
#ifdef NEXUS_SINGLE_VST3
    if(cycle==0&&node.vst3.vendor=="NEXUS internal QA"){
     auto window=windows[0];auto button=GetDlgItem(GetDlgItem(window,1000),1010);check(button,"Preset restart fixture missing");
     for(int change=0;change<8;++change){
      SendMessageW(button,BM_CLICK,0,0);
      const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(160);
      while(std::chrono::steady_clock::now()<until){QApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
      check(IsWindow(window),"Preset I/O reconfiguration closed original editor");
      for(auto* filter:filters){auto* state=obs_source_get_settings(filter);const std::string error=obs_data_get_string(state,"rack_error");obs_data_release(state);check(error.empty(),"Legitimate presets exhausted recovery budget");}
     }
     report.insert("preset_reconfiguration_cycles",8);
    }
#endif
    for(int n=0;n<10;++n){QApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    for(auto window:windows){SendMessageW(window,WM_CLOSE,0,0);check(!IsWindow(window),"Original editor window did not close");}
   }
   const size_t beforeA=captured[0].blocks.load(),beforeB=captured[1].blocks.load();
   captured[0].nonzero.store(false);captured[1].nonzero.store(false);
   wait([&]{return captured[0].blocks.load()>beforeA+8&&captured[1].blocks.load()>beforeB+8;});
   check(captured[0].nonzero.load()&&captured[1].nonzero.load()&&!captured[0].invalid.load()&&!captured[1].invalid.load(),"Audio did not remain finite/nonzero after editor cycles");
   report.insert("audio_after_editor_nonzero",true);
   report.insert("original_editor_cycles",3);
  }
  std::atomic<bool> saved{false};saver=std::thread([&]{obs_source_save(filters[0]);obs_source_save(filters[1]);saved.store(true);});
  wait([&]{return saved.load();});saver.join();stop.store(true);sender.join();
  RackDocument restored;
  for(size_t i=0;i<2;++i){
   check(!captured[i].invalid.load()&&captured[i].nonzero.load(),"External audio silent/nonfinite");
   auto* settings=obs_source_get_settings(filters[i]);const std::string error=obs_data_get_string(settings,"rack_error");
   const auto state=read_state(settings);obs_data_release(settings);
   if(!error.empty())throw std::runtime_error(error);
   check(state.active()[0].vst3.has_state&&!state.active()[0].vst3.component_state.empty(),"External state missing");
   if(i==0)restored=state;
   report.insert(i?"instance_b_blocks":"instance_a_blocks",int(captured[i].blocks.load()));
  }
  obs_source_filter_remove(inputs[0],filters[0]);obs_source_release(filters[0]);filters[0]=nullptr;
  obs_wait_for_destroy_queue();QApplication::processEvents();
  auto* settings=obs_data_create();write_state(settings,restored);
  filters[0]=obs_source_create_private(filter_id,"restored external effect",settings);obs_data_release(settings);check(filters[0],"Restored source");obs_source_filter_add(inputs[0],filters[0]);
  for(size_t i=0;i<30;++i){send();QApplication::processEvents();}
  settings=obs_source_get_settings(filters[0]);check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"External restored load");obs_data_release(settings);
  check(!captured[0].invalid.load(),"Restored external audio");
#ifdef NEXUS_SINGLE_VST3
  // Verify the new product UI and removal using the actual loaded module.
  auto* props=obs_source_properties(filters[0]);
  check(!obs_properties_get(props,"open_rack_editor"),"Legacy rack UI exposed");
  {
   const auto heading=QString::fromUtf8(obs_property_description(obs_properties_get(props,"selected_vst3")));
   QTextDocument doc;doc.setDefaultStyleSheet("body { color: white; }");doc.setHtml("<body>"+heading+"</body>");doc.setTextWidth(560);
   QImage preview(600,100,QImage::Format_ARGB32_Premultiplied);preview.fill(QColor("#202127"));QPainter paint(&preview);paint.translate(16,16);doc.drawContents(&paint);paint.end();
   check(preview.save(QString::fromUtf8(argv[4])+".heading.png"),"Heading preview failed");
  }
  check(!obs_properties_get(props,"open_vst3_settings"),"Intermediate settings button still exposed");
  {
   auto* preview=obs_source_get_settings(filters[0]);
   auto isRed=[&]{QTextDocument document;document.setHtml(QString::fromUtf8(obs_property_description(obs_properties_get(props,"selected_vst3"))));
    for(auto block=document.begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){auto fragment=it.fragment();if(fragment.text().contains(QString::fromStdString(node.vst3.name)))return fragment.charFormat().foreground().color()==QColor("#ff6666");}return false;};
   obs_data_set_bool(preview,"bypass",true);obs_properties_apply_settings(props,preview);check(isRed(),"Bypass did not render the VST name red");
   obs_data_set_bool(preview,"bypass",false);obs_properties_apply_settings(props,preview);check(!isRed(),"Disabling bypass left the VST name red");obs_data_release(preview);
  }
  check(obs_properties_get(props,"choose_vst3")&&obs_properties_get(props,"open_vst3")&&obs_properties_get(props,"remove_vst3"),"Inline controls missing");
  for(auto* w:QApplication::topLevelWidgets())check(w->objectName()!="nexus_vst3_settings","Intermediate window created");
  obs_property_button_clicked(obs_properties_get(props,"remove_vst3"),filters[0]);
  check(!obs_property_enabled(obs_properties_get(props,"open_vst3"))&&!obs_property_enabled(obs_properties_get(props,"remove_vst3")),"Removal left stale enabled filter buttons");
  check(QString::fromUtf8(obs_property_description(obs_properties_get(props,"selected_vst3"))).contains("No effect selected"),"Removal left stale effect name in existing properties");
  for(int i=0;i<30;++i){send();QApplication::processEvents();}
  settings=obs_source_get_settings(filters[0]);check(read_state(settings).a.empty(),"Effect removal failed");
  check(captured[0].peak.load()>.049f&&captured[0].peak.load()<.051f,"Empty filter did not pass dry audio");
  check(!obs_data_has_user_value(settings,"rack_a")&&!obs_data_has_user_value(settings,"rack_b"),"Legacy A/B state written");obs_data_release(settings);
  const auto beforeEmpty=captured[0].blocks.load();captured[0].compare=true;jitterClock=true;
  for(int i=0;i<1000;++i){send();QApplication::processEvents();}
  captured[0].compare=false;jitterClock=false;
  check(captured[0].blocks.load()==beforeEmpty+1000,"Empty filter dropped input packets with clock jitter");
  check(!captured[0].changed.load(),"Empty filter altered input samples");
  obs_property_button_clicked(obs_properties_get(props,"choose_vst3"),filters[0]);QApplication::processEvents();
  QWidget* picker=nullptr;for(auto* w:QApplication::allWidgets())if(w->objectName()=="nexus_vst3_picker")picker=w;
  check(picker,"Effect picker missing");
  picker->findChild<QLineEdit*>("vst3_folder")->setText(QString::fromUtf8(argv[2]));
  picker->findChild<QPushButton*>("vst3_scan_folder")->click();
  auto* list=picker->findChild<QListWidget*>("vst3_effects");auto* add=picker->findChild<QPushButton*>("vst3_add");
  wait([&]{if(list->count())list->setCurrentRow(0);return add->isEnabled();});add->click();
  for(int i=0;i<30;++i){send();QApplication::processEvents();}
  settings=obs_source_get_settings(filters[0]);check(read_state(settings).a.size()==1,"Picker did not select exactly one effect");
  check(std::string(obs_data_get_string(settings,"rack_error")).empty(),"Picker load failed");obs_data_release(settings);
  obs_properties_destroy(props);props=obs_source_properties(filters[0]);
  check(obs_property_enabled(obs_properties_get(props,"open_vst3")),"Inline open button did not refresh");
  obs_property_button_clicked(obs_properties_get(props,"open_vst3"),filters[0]);
  settings=obs_source_get_settings(filters[0]);check(std::string(obs_data_get_string(settings,"vst3_ui_error")).empty(),"Inline original editor failed");obs_data_release(settings);
  obs_properties_destroy(props);
  report.insert("single_effect_ui",true);
#endif
  report.insert("state_restored",true);report.insert("status","pass");cleanup();
 }catch(const std::exception& error){report.insert("status","failed");report.insert("detail",QString::fromUtf8(error.what()));cleanup();}
 report.insert("scope",argc==6?"actual NEXUS DLL/libobs; two sources, hidden original editors, three reopen cycles, audio/worker save/reload; no visual, keyboard, DPI, frontend or broadcast qualification":"actual NEXUS DLL/libobs; two per-source instances, concurrent audio/worker save, state reload; no original editor/frontend/devices/broadcast/prolonged-session claim");
 QFile output(QString::fromUtf8(argv[4]));if(!output.open(QIODevice::WriteOnly))return 2;output.write(QJsonDocument(report).toJson());output.close();
 const bool passed=report.value("status").toString()=="pass";
 std::cout<<(passed?"PASS":"FAIL")<<": actual OBS external effect, see JSON\n";return passed?0:1;
}
