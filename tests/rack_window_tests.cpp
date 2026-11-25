#include "ui/rack_widget.hpp"
#include "obs/rack_settings.hpp"
#include <obs.h>
#include <QApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
namespace nexus::ui{void open_rack_editor(obs_source_t*,const char*);void close_rack_editors();}
namespace {
const char* name(void*){return "Rack UI test";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
nexus::ui::RackWidget* panel(){
    for(auto* window:QApplication::topLevelWidgets())for(auto* child:window->findChildren<QWidget*>())
        if(auto* panel=dynamic_cast<nexus::ui::RackWidget*>(child))return panel;
    return nullptr;
}
void events(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=2)return 2;
    bool started=false;obs_source_t* source=nullptr;
    QTemporaryDir temporary(QString::fromUtf8(argv[1])+"/rack-window-XXXXXX");
    try{
        check(temporary.isValid(),"Preset test root");check(obs_startup("en-US",nullptr,nullptr),"OBS startup");started=true;
        obs_audio_info audio{};audio.samples_per_sec=48000;audio.speakers=SPEAKERS_STEREO;check(obs_reset_audio(&audio),"Audio setup");
        obs_source_info info{};info.id="nexus_rack_ui_test";info.type=OBS_SOURCE_TYPE_FILTER;info.output_flags=OBS_SOURCE_AUDIO;
        info.get_name=name;info.create=create;info.destroy=destroy;obs_register_source(&info);
        source=obs_source_create_private(info.id,"rack editor lifecycle",nullptr);check(source,"Source");
        const auto directory=temporary.path().toUtf8();
        nexus::ui::open_rack_editor(source,directory.constData());events();auto* first=panel();check(first,"Editor did not open");
        nexus::ui::open_rack_editor(source,directory.constData());events();check(panel()==first,"Duplicate editor");
        first->add_module(nexus::ModuleKind::high_pass);first->select_side(true);first->add_module(nexus::ModuleKind::mono);
        auto* settings=obs_source_get_settings(source);const auto saved=nexus::obs_bridge::read_rack(settings);obs_data_release(settings);
        check(saved.a.size()==3&&saved.b.size()==3&&saved.use_b,"Rack edits did not reach scene state");
        QPointer<QWidget> window=first->window();window->close();events();check(window.isNull(),"Editor did not close");
        nexus::ui::open_rack_editor(source,directory.constData());events();check(panel(),"Editor reopen");
        check(nexus::same_document(saved,panel()->document()),"A/B state lost on close/reopen");
        obs_source_release(source);source=nullptr;obs_wait_for_destroy_queue();
        QEventLoop loop;QTimer::singleShot(350,&loop,&QEventLoop::quit);loop.exec();events();check(panel()==nullptr,"Editor outlived source");
        source=obs_source_create_private(info.id,"queued editor",nullptr);
        nexus::ui::open_rack_editor(source,directory.constData());nexus::ui::close_rack_editors();events();check(!panel(),"Pending editor survived teardown");
        obs_source_release(source);source=nullptr;obs_wait_for_destroy_queue();obs_shutdown();started=false;
        std::cout<<"PASS: Rack Editor singleton, A/B persistence, close/reopen, source removal and queued-open teardown\n";return 0;
    }catch(const std::exception& e){
        nexus::ui::close_rack_editors();if(source)obs_source_release(source);if(started)obs_shutdown();
        std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
    }
}