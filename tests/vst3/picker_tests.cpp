#include "ui/vst3_picker.hpp"
#include "ui/rack_widget.hpp"
#include <QApplication>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <Windows.h>
#include <chrono>
#include <thread>
#include <iostream>
using namespace nexus;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void events(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
template<class F>void wait(F done){
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(!done()){check(std::chrono::steady_clock::now()<end,"Picker timeout");events();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
}
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=5)return 2;
 try{
  QTemporaryDir temp;check(temp.isValid(),"Cache directory");app.setStyle("Fusion");
  auto* picker=new ui::Vst3Picker(std::filesystem::u8path(argv[1]),std::filesystem::u8path(temp.path().toStdString()));
  QPointer<ui::Vst3Picker> guard(picker);picker->show();events();
  auto* list=picker->findChild<QListWidget*>("vst3_effects");check(list&&list->count()==0,"Picker scanned without request");
  picker->start_scan({std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3])});
  wait([&]{return !picker->scanning();});events(); // Final poll consumes results.
  wait([&]{return list->count()>0;});
  check(!GetModuleHandleW(L"NexusFixture.vst3")&&!GetModuleHandleW(L"ADelaySDKFixture.vst3"),"Picker loaded effect DLL in parent");
  int adelay=-1;
  for(int i=0;i<list->count();++i){
   check(!list->item(i)->text().contains("instrument",Qt::CaseInsensitive),"Instrument listed");
   if(list->item(i)->text().contains("ADelay"))adelay=i;
  }
  check(adelay>=0,"ADelay missing");
  auto* search=picker->findChild<QLineEdit*>("vst3_search");search->setText("ADelay");
  list->setCurrentRow(adelay);events();
  auto* add=picker->findChild<QPushButton*>("vst3_add");check(add->isEnabled(),"Visible effect cannot be selected");
  search->setText("not-a-match");events();check(!add->isEnabled(),"Hidden effect can be added");
  search->setText("ADelay");events();
  QImage preview(picker->size(),QImage::Format_ARGB32_Premultiplied);preview.fill(Qt::transparent);picker->render(&preview);
  check(preview.save(QString::fromUtf8(argv[4])),"Picker preview");
  vst3::ClassInfo selected;size_t chosen=0;
  picker->on_selected=[&](const auto& info){selected=info;++chosen;};
  add->click();events();check(!guard&&chosen==1&&selected.effect&&selected.name=="ADelay","Picker selection");
  ui::RackWidget rack(temp.path(),2);rack.set_vst3_chooser([]{});
  Vst3Settings settings;settings.path=selected.path;settings.class_id=selected.id;settings.name=selected.name;settings.vendor=selected.vendor;
  ui::RackWidget multichannel(temp.path(),8);multichannel.set_vst3_chooser([]{});
  bool unsupported=false;try{multichannel.add_vst3(settings);}catch(const std::exception&){unsupported=true;}
  check(unsupported,"Multichannel VST3 was offered");
  rack.add_vst3(settings);check(rack.document().active().back().vst3.name=="ADelay","VST3 card metadata");
  check(!rack.findChild<QDoubleSpinBox*>("rack_value_3"),"VST3 exposed fake native control");
  size_t snapshots=0;rack.before_snapshot=[&]{++snapshots;return rack.document();};
  rack.save_as("VST3 candidate");rack.export_preset(temp.path()+"/export.json");
  check(snapshots==2,"Save/export skipped host snapshot callback");
  const auto saved=temp.path()+"/export.json";QFile existing(saved);check(existing.open(QIODevice::ReadOnly),"Saved file");const auto bytes=existing.readAll();existing.close();
  rack.before_snapshot=[]{throw std::runtime_error("Snapshot unavailable");return RackDocument{};};
  bool rejected=false;try{rack.export_preset(saved);}catch(const std::exception&){rejected=true;}
  check(rejected&&existing.open(QIODevice::ReadOnly)&&existing.readAll()==bytes,"Failed snapshot overwrote preset");
  auto* closing=new ui::Vst3Picker(std::filesystem::u8path(argv[1]),std::filesystem::u8path(temp.path().toStdString()),nullptr);
  closing->start_scan({std::filesystem::u8path(argv[2])});QPointer<ui::Vst3Picker> closingGuard(closing);
  closing->close();events();check(!closingGuard,"Closing picker did not delete window");
  ui::Vst3Picker::shutdown_jobs(); // Also joins any job whose dialog was already closed.
  std::cout<<"PASS: asynchronous picker, candidate filter/search/selection, no parent DLL load, snapshot hooks and close/unload\n";return 0;
 }catch(const std::exception& e){ui::Vst3Picker::shutdown_jobs();std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
