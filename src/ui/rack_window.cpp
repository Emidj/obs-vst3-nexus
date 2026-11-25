#include "ui/rack_widget.hpp"
#include "obs/rack_settings.hpp"
#ifdef NEXUS_OBS_VST3
#include "ui/vst3_picker.hpp"
#include <obs-module.h>
#include <QFileInfo>
#include <QDir>
#endif
#include <QApplication>
#include <QDialog>
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
namespace nexus::ui {
namespace {
class RackWindow final : public QDialog {
public:
    RackWindow(obs_source_t* source,const QString& directory):weak_(obs_source_get_weak_source(source)){
        setAttribute(Qt::WA_DeleteOnClose);setWindowTitle("NEXUS Rack Editor");resize(780,820);
        obs_audio_info audio{};obs_get_audio_info(&audio);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        panel_=new RackWidget(directory,get_audio_channels(audio.speakers),this);layout->addWidget(panel_);
#ifdef NEXUS_OBS_VST3
        panel_->set_vst3_chooser([this,directory]{
            if(picker_){picker_->showNormal();picker_->raise();picker_->activateWindow();return;}
            const auto binary=QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
            const auto helper=QDir(QFileInfo(binary).absolutePath()).filePath("nexus_vst3_worker.exe");
            const auto cache=QDir::cleanPath(QDir(directory).absoluteFilePath("../vst3-cache"));
            auto* picker=new Vst3Picker(std::filesystem::u8path(helper.toStdString()),std::filesystem::u8path(cache.toStdString()),this);
            picker_=picker;
            picker->on_selected=[this](const vst3::ClassInfo& selected){
                Vst3Settings settings;settings.path=selected.path;settings.class_id=selected.id;
                settings.name=selected.name;settings.vendor=selected.vendor;panel_->add_vst3(settings);
            };
            picker->show();
        });
        panel_->on_open_vst3_editor=[this](uint64_t id){
            if(auto* source=obs_weak_source_get_source(weak_)){
                calldata_t args;calldata_init(&args);const auto text=std::to_string(id);
                calldata_set_string(&args,"id",text.c_str());calldata_set_bool(&args,"visible",true);
                const bool called=proc_handler_call(obs_source_get_proc_handler(source),"nexus_open_vst3_editor",&args);
                const auto error=QString::fromUtf8(calldata_string(&args,"error")?calldata_string(&args,"error"):"");
                editor_error_=called?error:"VST3 editor is unavailable.";
                panel_->set_status(editor_error_.isEmpty()?"Original plugin interface opened.":editor_error_);
                calldata_free(&args);obs_source_release(source);
            }else close();
        };
        panel_->before_edit=[this](bool capture_dirty){
            auto* source=obs_weak_source_get_source(weak_);if(!source)throw std::runtime_error("Source is no longer available");
            auto* settings=obs_source_get_settings(source);
            try{
                calldata_t status;calldata_init(&status);
                const bool known=proc_handler_call(obs_source_get_proc_handler(source),"nexus_rack_status",&status);
                const bool busy=known&&calldata_bool(&status,"control_busy");const bool dirty=known&&calldata_bool(&status,"editor_dirty");calldata_free(&status);
                if(busy)throw std::runtime_error("Wait for the plugin lifecycle operation before editing the rack");
                if(capture_dirty&&dirty){obs_source_save(source);const std::string error=obs_data_get_string(settings,"rack_error");if(!error.empty())throw std::runtime_error(error);}
                auto current=obs_bridge::read_rack(settings);obs_data_release(settings);obs_source_release(source);return current;
            }catch(...){obs_data_release(settings);obs_source_release(source);throw;}
        };
        panel_->before_snapshot=[this]{
            auto* source=obs_weak_source_get_source(weak_);
            if(!source)throw std::runtime_error("Source is no longer available");
            auto* settings=obs_source_get_settings(source);
            try{
                auto document=obs_bridge::read_rack(settings);
                if(contains_vst3(document)){
                    obs_source_save(source);
                    const std::string error=obs_data_get_string(settings,"rack_error");
                    if(!error.empty())throw std::runtime_error(error);
                    document=obs_bridge::read_rack(settings);
                }
                obs_data_release(settings);obs_source_release(source);return document;
            }catch(...){obs_data_release(settings);obs_source_release(source);throw;}
        };
#endif

        panel_->on_change=[this](const RackDocument& document){
            if(auto* source=obs_weak_source_get_source(weak_)){
                auto* settings=obs_source_get_settings(source);
                try{
                    const auto previous=obs_bridge::read_rack(settings);
                    auto applied=document;auto rollback=previous;
#ifdef NEXUS_OBS_VST3
                    const auto& old=previous.active();const auto& next=document.active();
                    bool rebuild=previous.use_b!=document.use_b||old.size()!=next.size();
                    if(!rebuild)for(size_t i=0;i<old.size();++i)
                        if(old[i].id!=next[i].id||old[i].kind!=next[i].kind||!same_vst3(old[i].vst3,next[i].vst3)){rebuild=true;break;}
                    const bool activeVst=std::any_of(old.begin(),old.end(),[](const auto& node){return node.kind==ModuleKind::vst3;});
                    calldata_t editorState;calldata_init(&editorState);
                    const bool statusAvailable=proc_handler_call(obs_source_get_proc_handler(source),"nexus_rack_status",&editorState);
                    const bool lifecycleBusy=statusAvailable&&calldata_bool(&editorState,"control_busy");
                    const bool editorChanged=statusAvailable&&calldata_bool(&editorState,"editor_dirty");calldata_free(&editorState);
                    if(lifecycleBusy)throw std::runtime_error("Wait for the plugin lifecycle operation before editing the rack");
                    if(rebuild&&activeVst&&editorChanged){
                        obs_source_save(source);
                        const std::string failure=obs_data_get_string(settings,"rack_error");
                        if(!failure.empty())throw std::runtime_error(failure);
                        const auto captured=obs_bridge::read_rack(settings);rollback=captured;
                        auto& destination=previous.use_b?applied.b:applied.a;
                        for(const auto& original:old)if(original.kind==ModuleKind::vst3){
                            const auto state=std::find_if(captured.active().begin(),captured.active().end(),[&](const auto& node){return node.id==original.id;});
                            for(auto& node:destination)if(node.id==original.id&&node.kind==ModuleKind::vst3&&same_vst3(node.vst3,original.vst3)&&state!=captured.active().end())node.vst3=state->vst3;
                        }
                    }
#endif
                    obs_bridge::write_rack(settings,applied);obs_source_update(source,settings);
                    const QString error=QString::fromUtf8(obs_data_get_string(settings,"rack_error"));
                    if(!error.isEmpty()){
                        obs_bridge::write_rack(settings,rollback);obs_source_update(source,settings);
                        obs_data_set_string(settings,"rack_error",error.toUtf8().constData());
                        panel_->set_document(rollback);
                    }
                    panel_->set_status(error.isEmpty()?"Close this window to keep processing audio.":error);
                }catch(const std::exception& e){panel_->set_status(QString::fromUtf8(e.what()));}
                obs_data_release(settings);obs_source_release(source);
            }else close();
        };
        auto* timer=new QTimer(this);QObject::connect(timer,&QTimer::timeout,this,[this]{refresh();});timer->start(250);refresh();
    }
    ~RackWindow()override{obs_weak_source_release(weak_);}
    bool matches(obs_source_t* source)const{return obs_weak_source_references_source(weak_,source);}
private:
    void refresh(){
        auto* source=obs_weak_source_get_source(weak_);if(!source){close();return;}
        auto* settings=obs_source_get_settings(source);
        try{
            if(!obs_data_get_bool(settings,"rack_pending"))panel_->set_document(obs_bridge::read_rack(settings));
            auto* parent=obs_filter_get_parent(source);
            panel_->set_source_name("Source: "+QString::fromUtf8(obs_source_get_name(parent?parent:source)));
            const auto error=QString::fromUtf8(obs_data_get_string(settings,"rack_error"));
            calldata_t status;calldata_init(&status);
            const bool available=proc_handler_call(obs_source_get_proc_handler(source),"nexus_rack_status",&status);
            const auto events=available?calldata_int(&status,"recovery_events"):0;
            nexus::LoudnessReading reading;
            bool meterAvailable=false;
            if(available&&proc_handler_call(obs_source_get_proc_handler(source),"nexus_loudness_status",&status)){
                meterAvailable=calldata_bool(&status,"available");
                reading={float(calldata_float(&status,"momentary")),float(calldata_float(&status,"short_term")),
                    float(calldata_float(&status,"integrated")),float(calldata_float(&status,"sample_peak")),
                    calldata_bool(&status,"momentary_ready"),calldata_bool(&status,"short_term_ready"),
                    calldata_bool(&status,"integrated_ready"),calldata_bool(&status,"active")};
            }
            panel_->set_loudness(reading,meterAvailable);
            if(available)for(const auto& node:panel_->document().active())if(is_dynamics(node.kind)){
                calldata_t reduction;calldata_init(&reduction);calldata_set_string(&reduction,"id",std::to_string(node.id).c_str());
                if(proc_handler_call(obs_source_get_proc_handler(source),"nexus_gain_reduction",&reduction))
                    panel_->set_gain_reduction(node.id,float(calldata_float(&reduction,"db")));
                calldata_free(&reduction);
            }
            calldata_free(&status);
            if(obs_data_get_bool(settings,"rack_pending"))panel_->set_status("Applying rack changes...");
            else if(!error.isEmpty())panel_->set_status(error);
            else if(!editor_error_.isEmpty())panel_->set_status(editor_error_);
            else if(events>0)panel_->set_status(QString("Audio recovery events: %1. Review the source and effect settings.").arg(events));
            else panel_->set_status("Close this window to keep processing audio.");
        }catch(const std::exception& e){panel_->set_status(QString::fromUtf8(e.what()));}
        obs_data_release(settings);obs_source_release(source);
        obs_audio_info info{};if(obs_get_audio_info(&info))panel_->set_sample_rate(info.samples_per_sec);
    }
    obs_weak_source_t* weak_;RackWidget* panel_;QString editor_error_;
#ifdef NEXUS_OBS_VST3
    QPointer<Vst3Picker> picker_;
#endif
};
std::vector<QPointer<RackWindow>> windows;
QPointer<QObject> dispatcher;
}
void open_rack_editor(obs_source_t* source,const char* directory){
    if(!source||!qApp||QThread::currentThread()!=qApp->thread())return;
    if(!dispatcher)dispatcher=new QObject(qApp);
    auto weak=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(source),obs_weak_source_release);
    const auto path=QString::fromUtf8(directory?directory:"");
    QMetaObject::invokeMethod(dispatcher,[weak,path]{
        auto* source=obs_weak_source_get_source(weak.get());if(!source)return;
        windows.erase(std::remove_if(windows.begin(),windows.end(),[](const auto& p){return p.isNull();}),windows.end());
        for(const auto& window:windows)if(window&&window->matches(source)){
            window->showNormal();window->raise();window->activateWindow();obs_source_release(source);return;
        }
        auto* window=new RackWindow(source,path);windows.push_back(window);window->show();obs_source_release(source);
    },Qt::QueuedConnection);
}
void close_rack_editors(){
    if(!qApp)return;
    auto close=[]{
        if(dispatcher)delete dispatcher.data();
        for(auto& window:windows)if(window)delete window.data();windows.clear();
#ifdef NEXUS_OBS_VST3
        Vst3Picker::shutdown_jobs();
#endif
    };
    if(QThread::currentThread()==qApp->thread())close();
    else QMetaObject::invokeMethod(qApp,close,Qt::BlockingQueuedConnection);
}
}