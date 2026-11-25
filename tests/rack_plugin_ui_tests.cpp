#include "obs/rack_settings.hpp"
#include <QApplication>
#include <QAction>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <chrono>
#include <limits>
namespace {
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
QWidget* panel(){
    for(auto* w:QApplication::topLevelWidgets())if(auto* p=w->findChild<QWidget*>("nexus_rack_editor"))return p;
    return nullptr;
}
void events(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
const char* name(void*){return "Rack UI audio signal";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
struct Capture{size_t blocks=0;bool finite=true;float last_peak=0;};
void capture(void* p,obs_source_t*,const audio_data* data,bool){
    auto& state=*static_cast<Capture*>(p);++state.blocks;state.last_peak=0;
    if(data->frames!=1024){state.finite=false;return;}
    for(size_t c=0;c<2;++c){
        if(!data->data[c]){state.finite=false;continue;}
        const auto* values=reinterpret_cast<const float*>(data->data[c]);
        for(size_t i=0;i<1024;++i){if(!std::isfinite(values[i]))state.finite=false;else state.last_peak=(std::max)(state.last_peak,std::abs(values[i]));}
    }
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=3)return 2;
    QTemporaryDir temp(QString::fromUtf8(argv[2])+"/plugin-ui-XXXXXX");
    obs_source_t* filter=nullptr;obs_source_t* input=nullptr;obs_properties_t* props=nullptr;bool started=false;std::thread sender;
    try{
        check(temp.isValid(),"Test config directory");
        const auto directory=temp.path().toUtf8();check(obs_startup("en-US",directory.constData(),nullptr),"OBS startup");started=true;
        obs_audio_info format{};format.samples_per_sec=48000;format.speakers=SPEAKERS_STEREO;check(obs_reset_audio(&format),"Audio setup");
        obs_module_t* module=nullptr;check(obs_open_module(&module,argv[1],".")==MODULE_SUCCESS&&obs_init_module(module),"DLL module load");
        filter=obs_source_create_private("nexus_audio_rack","actual plugin Rack Editor",nullptr);check(filter,"Actual filter create");
        props=obs_source_properties(filter);auto* button=obs_properties_get(props,"open_rack_editor");check(button,"Rack Editor button missing");
        obs_property_button_clicked(button,filter);events();auto* editor=panel();check(editor,"Button did not open Rack Editor");
        obs_property_button_clicked(button,filter);events();check(panel()==editor,"Button opened duplicate window");
        auto* add=editor->findChild<QAction*>("add_highpass");check(add,"Add menu");add->trigger();events();
        editor=panel();editor->findChild<QPushButton*>("rack_b")->click();events();
        editor=panel();editor->findChild<QAction*>("add_mono")->trigger();events();
        editor=panel();auto* expand=editor->findChild<QToolButton*>("rack_expand_2");check(expand,"EQ expansion");expand->click();events();
        editor=panel();auto* gain=editor->findChild<QDoubleSpinBox*>("eq_band_0_gain");check(gain,"Embedded EQ");gain->setValue(3);
        editor=panel();editor->findChild<QAction*>("add_compressor")->trigger();events();
        editor=panel();auto* threshold=editor->findChild<QDoubleSpinBox*>("rack_dynamics_4_threshold_db");
        check(threshold,"Actual compressor controls");threshold->setValue(-30);events();
        for(const auto action:{"add_expander","add_stereo_width","add_limiter"}){
            editor=panel();auto* entry=editor->findChild<QAction*>(action);check(entry,"Actual dynamics menu");entry->trigger();events();
        }
        editor=panel();auto* ceiling=editor->findChild<QDoubleSpinBox*>("rack_dynamics_7_threshold_db");
        check(ceiling,"Actual limiter control");ceiling->setValue(-36);events();
        auto* settings=obs_source_get_settings(filter);auto document=nexus::obs_bridge::read_rack(settings);obs_data_release(settings);
        check(document.use_b&&document.a.size()==3&&document.b.size()==7&&document.b[3].dynamics.threshold_db==-30&&document.b[1].eq.bands[0].gain_db==3,"Live UI did not reach actual plugin state");
        obs_source_info info{};info.id="nexus_actual_ui_signal";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_AUDIO;
        info.get_name=name;info.create=create;info.destroy=destroy;obs_register_source(&info);
        input=obs_source_create_private(info.id,"real UI pipeline",nullptr);check(input,"Input source");obs_source_filter_add(input,filter);
        Capture state;obs_source_add_audio_capture_callback(input,capture,&state);
        std::array<float,1024> left{},right{};const auto origin=os_gettime_ns();size_t sequence=0;
        auto send=[&](bool invalid=false){
            for(size_t i=0;i<1024;++i){left[i]=float(.1*std::sin(double(sequence*1024+i)*.13));right[i]=left[i];}
            if(invalid)left[0]=std::numeric_limits<float>::quiet_NaN();
            obs_source_audio audio{};audio.frames=1024;audio.samples_per_sec=48000;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;
            audio.timestamp=origin+sequence++*1024*1000000000ULL/48000;audio.data[0]=reinterpret_cast<const uint8_t*>(left.data());audio.data[1]=reinterpret_cast<const uint8_t*>(right.data());
            obs_source_output_audio(input,&audio);
        };
        for(int i=0;i<10;++i)send();
        std::atomic<bool> audioStarted{false},editsDone{false};std::atomic<size_t> overlap{0};
        sender=std::thread([&]{
            for(int i=0;i<200;++i){
                send();if(!editsDone.load(std::memory_order_relaxed))++overlap;
                audioStarted.store(true,std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        while(!audioStarted.load(std::memory_order_acquire))std::this_thread::yield();
        for(int i=0;i<12;++i){
            editor=panel();editor->findChild<QPushButton*>(i%2?"rack_b":"rack_a")->click();events();
        }
        editsDone.store(true,std::memory_order_relaxed);sender.join();check(overlap>0,"No audio overlapped live UI edits");
        auto* edited=obs_source_get_settings(filter);document=nexus::obs_bridge::read_rack(edited);obs_data_release(edited);
        editor=panel();
        QPointer<QWidget> window=editor->window();window->close();events();check(window.isNull()&&!panel(),"Rack Editor did not close");
        for(int i=0;i<10;++i)send();
        send(true);calldata_t status;calldata_init(&status);
        const bool diagnostic=proc_handler_call(obs_source_get_proc_handler(filter),"nexus_rack_status",&status);
        const auto recoveries=calldata_int(&status,"recovery_events");calldata_free(&status);
        check(diagnostic&&recoveries>0,"Non-finite recovery diagnostics");
        calldata_init(&status);
        const bool metering=proc_handler_call(obs_source_get_proc_handler(filter),"nexus_loudness_status",&status);
        check(metering&&calldata_bool(&status,"available")&&calldata_bool(&status,"short_term_ready")&&
            std::isfinite(calldata_float(&status,"integrated")),"Actual DLL output metering");
        calldata_free(&status);
        calldata_init(&status);calldata_set_string(&status,"id","4");
        const bool dynamics=proc_handler_call(obs_source_get_proc_handler(filter),"nexus_gain_reduction",&status);
        check(dynamics&&calldata_float(&status,"db")>.1&&std::isfinite(calldata_float(&status,"db")),"Actual gain reduction telemetry");calldata_free(&status);
        check(state.blocks==221&&state.finite,"Audio stopped or became invalid after editor close");
        check(state.last_peak<=std::pow(10.,-36./20)+1e-7,"Actual DLL output limiter ceiling");
        obs_property_button_clicked(button,filter);events();check(panel(),"Actual editor reopen");
        settings=obs_source_get_settings(filter);check(nexus::same_document(document,nexus::obs_bridge::read_rack(settings)),"Actual state lost on reopen");obs_data_release(settings);
        obs_source_remove_audio_capture_callback(input,capture,&state);obs_source_filter_remove(input,filter);
        obs_source_release(input);input=nullptr;obs_source_release(filter);filter=nullptr;obs_properties_destroy(props);props=nullptr;obs_wait_for_destroy_queue();
        QEventLoop loop;QTimer::singleShot(350,&loop,&QEventLoop::quit);loop.exec();events();check(!panel(),"Actual editor outlived filter");
        obs_shutdown();started=false;
        std::cout<<"PASS: actual DLL button, live editor mutations, A/B, embedded EQ, close/reopen and uninterrupted captured blocks\n";return 0;
    }catch(const std::exception& e){
        if(sender.joinable())sender.join();
        if(input&&filter)obs_source_filter_remove(input,filter);
        if(input)obs_source_release(input);if(filter)obs_source_release(filter);if(props)obs_properties_destroy(props);
        if(started)obs_shutdown();std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
    }
}