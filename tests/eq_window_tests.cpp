#include "ui/eq_widget.hpp"
#include <obs.h>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <iostream>
#include <stdexcept>
namespace nexus::ui {void open_equalizer(obs_source_t*);void close_equalizers();}
namespace {
const char* name(void*){return "UI test filter";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
nexus::ui::EqWidget* panel(){
    for(auto* w:QApplication::topLevelWidgets())for(auto* child:w->findChildren<QWidget*>())if(auto* p=dynamic_cast<nexus::ui::EqWidget*>(child))return p;
    return nullptr;
}
void events(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    bool started=false;obs_source_t* source=nullptr;
    try{
        require(obs_startup("en-US",nullptr,nullptr),"OBS startup");started=true;
        obs_audio_info config{};config.samples_per_sec=48000;config.speakers=SPEAKERS_STEREO;
        require(obs_reset_audio(&config),"Audio setup");
        obs_source_info info{};info.id="nexus_eq_window_test";info.type=OBS_SOURCE_TYPE_FILTER;
        info.output_flags=OBS_SOURCE_AUDIO;info.get_name=name;info.create=create;info.destroy=destroy;obs_register_source(&info);
        source=obs_source_create_private(info.id,"window lifecycle test",nullptr);require(source,"Source creation");
        nexus::ui::open_equalizer(source);events();auto* first=panel();require(first,"EQ window not opened");
        nexus::ui::open_equalizer(source);events();require(panel()==first,"Duplicate EQ window");
        auto* gain=first->findChild<QDoubleSpinBox*>("eq_band_0_gain");gain->setValue(9);
        auto* settings=obs_source_get_settings(source);const double saved=obs_data_get_double(settings,"eq_band_0_gain");
        obs_data_release(settings);require(saved==9,"UI change did not reach OBS settings");
        QPointer<QWidget> window=first->window();window->close();events();require(window.isNull(),"Window did not delete on close");
        require(obs_source_get_ref(source)!=nullptr,"Closing window destroyed source");
        // Balance the temporary reference acquired above.
        obs_source_release(source);
        nexus::ui::open_equalizer(source);events();require(panel(),"EQ failed to reopen");
        require(panel()->settings().bands[0].gain_db==9,"EQ state lost on reopen");
        obs_source_release(source);source=nullptr;obs_wait_for_destroy_queue();
        QEventLoop loop;QTimer::singleShot(350,&loop,&QEventLoop::quit);loop.exec();events();
        require(panel()==nullptr,"Window outlived its removed source");
        source=obs_source_create_private(info.id,"pending open cancellation",nullptr);
        nexus::ui::open_equalizer(source);nexus::ui::close_equalizers();events();
        require(panel()==nullptr,"Queued open survived module teardown");
        obs_source_release(source);source=nullptr;obs_wait_for_destroy_queue();
        nexus::ui::close_equalizers();obs_shutdown();started=false;
        std::cout<<"PASS: singleton window, settings sync, close/reopen, removed source and pending-open cancellation\n";return 0;
    }catch(const std::exception& e){
        nexus::ui::close_equalizers();if(source)obs_source_release(source);
        if(started)obs_shutdown();std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
    }
}