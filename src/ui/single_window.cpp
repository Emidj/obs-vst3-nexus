#include "i18n/strings.hpp"
#include "ui/vst3_picker.hpp"
#include "obs/single_settings.hpp"
#include <obs-module.h>
#include <QApplication>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <QImage>
#include <QCryptographicHash>
#include <QDateTime>
#include <QUrl>
#include <Windows.h>
#include <shellapi.h>
namespace nexus::ui {
void refresh_single_properties(obs_properties_t*,obs_source_t*,int bypass_override=-1);
namespace {
using Weak=std::shared_ptr<obs_weak_source_t>;
struct PickerEntry {Weak source;QPointer<Vst3Picker> picker;};
std::vector<PickerEntry> pickers;
QString module_icon(const QString& path){
    auto binary=QFileInfo(path);
    if(binary.isDir()){
        QDir folder(QDir(path).filePath("Contents/x86_64-win"));
        const auto files=folder.entryInfoList({"*.vst3"},QDir::Files);
        if(files.isEmpty())return {};binary=files.front();
    }
    if(!binary.isFile())return {};
    HICON large=nullptr,small=nullptr;
    const auto count=ExtractIconExW(reinterpret_cast<LPCWSTR>(binary.absoluteFilePath().utf16()),0,&large,&small,1);
    if(!count||count==UINT_MAX){if(large)DestroyIcon(large);if(small)DestroyIcon(small);return {};}
    auto image=QImage::fromHICON(large?large:small);if(large)DestroyIcon(large);if(small)DestroyIcon(small);
    if(image.isNull())return {};
    auto* raw=obs_module_config_path("vst3-icons");const auto directory=QString::fromUtf8(raw?raw:"");bfree(raw);
    if(directory.isEmpty()||!QDir().mkpath(directory))return {};
    const auto key=(binary.absoluteFilePath()+QString::number(binary.size())+QString::number(binary.lastModified().toMSecsSinceEpoch())).toUtf8();
    const auto filename=QDir(directory).filePath(QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex())+".png");
    if(!QFileInfo::exists(filename)&&!image.save(filename,"PNG"))return {};
    return filename;
}
void replace(obs_source_t* source,const vst3::ClassInfo* selected){
    auto* settings=obs_source_get_settings(source);
    try{
        calldata_t args;calldata_init(&args);proc_handler_call(obs_source_get_proc_handler(source),"nexus_rack_status",&args);
        bool busy=calldata_bool(&args,"control_busy")||calldata_bool(&args,"snapshot_busy");calldata_free(&args);
        if(busy)throw std::runtime_error(nexus::i18n::text("Wait for the current plugin operation to finish."));
        RackDocument doc;doc.bypass=obs_data_get_bool(settings,"bypass");
        if(selected){auto node=default_node(ModuleKind::vst3,1);node.vst3.path=selected->path;node.vst3.class_id=selected->id;node.vst3.name=selected->name;node.vst3.vendor=selected->vendor;doc.a.push_back(node);}
        obs_bridge::write_single(settings,doc);obs_data_set_string(settings,"vst3_ui_error","");obs_source_update(source,settings);
    }catch(...){obs_data_release(settings);throw;}
    obs_data_release(settings);
}
void error(obs_source_t* source,const char* message){auto* settings=obs_source_get_settings(source);obs_data_set_string(settings,"vst3_ui_error",message);obs_data_release(settings);}
bool choose(obs_properties_t*,obs_property_t*,void* raw){
    auto* source=static_cast<obs_source_t*>(raw);if(!source||!qApp||QThread::currentThread()!=qApp->thread())return false;
    pickers.erase(std::remove_if(pickers.begin(),pickers.end(),[](const auto& e){return e.picker.isNull();}),pickers.end());
    for(auto& e:pickers){auto* current=obs_weak_source_get_source(e.source.get());bool same=current==source;if(current)obs_source_release(current);if(same){e.picker->showNormal();e.picker->raise();e.picker->activateWindow();return false;}}
    const auto binary=QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));auto* config=obs_module_config_path("vst3-cache");
    auto* picker=new Vst3Picker(std::filesystem::u8path(QDir(QFileInfo(binary).absolutePath()).filePath("nexus_vst3_worker.exe").toStdString()),std::filesystem::u8path(config));bfree(config);
    Weak weak(obs_source_get_weak_source(source),obs_weak_source_release);pickers.push_back({weak,picker});picker->setWindowTitle(nexus::i18n::text("Choose VST3 effect"));
    picker->on_selected=[weak](const vst3::ClassInfo& selected){
        auto* source=obs_weak_source_get_source(weak.get());if(!source)throw std::runtime_error(nexus::i18n::text("Source is no longer available."));
        try{replace(source,&selected);obs_source_update_properties(source);obs_source_release(source);}catch(...){obs_source_release(source);throw;}
    };
    auto* timer=new QTimer(picker);QObject::connect(timer,&QTimer::timeout,picker,[weak,picker]{auto* source=obs_weak_source_get_source(weak.get());if(source)obs_source_release(source);else picker->close();});timer->start(200);picker->show();return false;
}
bool open(obs_properties_t* props,obs_property_t*,void* raw){
    auto* source=static_cast<obs_source_t*>(raw);if(!source)return false;
    calldata_t args;calldata_init(&args);calldata_set_string(&args,"id","1");calldata_set_bool(&args,"visible",true);
    bool ok=proc_handler_call(obs_source_get_proc_handler(source),"nexus_open_vst3_editor",&args);
    error(source,ok?(calldata_string(&args,"error")?calldata_string(&args,"error"):""):nexus::i18n::text("Plugin interface is unavailable."));calldata_free(&args);refresh_single_properties(props,source);return true;
}
bool remove(obs_properties_t* props,obs_property_t*,void* raw){auto* source=static_cast<obs_source_t*>(raw);if(!source)return false;try{replace(source,nullptr);}catch(const std::exception& e){error(source,e.what());}refresh_single_properties(props,source);return true;}
}
void refresh_single_properties(obs_properties_t* props,obs_source_t* source,int bypass_override){
    bool present=false,pending=false,bypass=false;QString icon;QString label=nexus::i18n::text("No effect selected. Audio passes through."),status=nexus::i18n::text("One effect per filter. Add another OBS filter for another effect.");
    if(source){auto* settings=obs_source_get_settings(source);bypass=obs_data_get_bool(settings,"bypass");try{auto doc=obs_bridge::read_single(settings);present=!doc.a.empty();if(present){label=QString::fromStdString(doc.a[0].vst3.name);icon=module_icon(QString::fromStdString(doc.a[0].vst3.path));}}
        catch(const std::exception& e){label=nexus::i18n::text("Effect unavailable");status=QString::fromUtf8(e.what());present=obs_data_has_user_value(settings,"effect");}
        pending=obs_data_get_bool(settings,"rack_pending");const auto* engine=obs_data_get_string(settings,"rack_error");const auto* ui=obs_data_get_string(settings,"vst3_ui_error");
        if(*ui)status=QString::fromUtf8(ui);else if(*engine)status=QString::fromUtf8(nexus::i18n::text("Plugin error. Technical details:"))+"\n"+QString::fromUtf8(engine);else if(pending)status=nexus::i18n::text("Loading effect...");obs_data_release(settings);
    }
    if(bypass_override>=0)bypass=bypass_override!=0;
    const auto image=icon.isEmpty()?QString():QString("<img src=\"%1\" width=\"32\" height=\"32\">&nbsp; ").arg(QUrl::fromLocalFile(icon).toString().toHtmlEscaped());
    const auto heading=present?image+QString("<span style=\"font-size:18pt; font-weight:600;%1\">%2</span>").arg(bypass?"color:#ff6666;":"",label.toHtmlEscaped()):label.toHtmlEscaped();
    obs_property_set_description(obs_properties_get(props,"selected_vst3"),heading.toUtf8().constData());
    obs_property_set_enabled(obs_properties_get(props,"open_vst3"),present&&!pending);
    obs_property_set_enabled(obs_properties_get(props,"remove_vst3"),present);
    obs_property_set_description(obs_properties_get(props,"effect_status"),status.toHtmlEscaped().toUtf8().constData());
}
bool bypass_changed(void* raw,obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    refresh_single_properties(props,static_cast<obs_source_t*>(raw),obs_data_get_bool(settings,"bypass")?1:0);return true;
}
void add_single_properties(obs_properties_t* props,obs_source_t* source){
    obs_property_set_modified_callback2(obs_properties_get(props,"bypass"),bypass_changed,source);
    obs_properties_add_text(props,"selected_vst3","",OBS_TEXT_INFO);
    obs_properties_add_button2(props,"choose_vst3",nexus::i18n::text("Choose VST3 effect..."),choose,source);
    obs_properties_add_button2(props,"open_vst3",nexus::i18n::text("Open plugin interface"),open,source);
    obs_properties_add_button2(props,"remove_vst3",nexus::i18n::text("Remove effect"),remove,source);
    obs_properties_add_text(props,"effect_status","",OBS_TEXT_INFO);
    obs_properties_add_text(props,"credit","a plugin from <a href=\"https://discord.gg/dhcqZPDYh\">DjEmiMas</a>",OBS_TEXT_INFO);
    refresh_single_properties(props,source);
}
void close_rack_editors(){
    if(!qApp)return;auto close=[]{for(auto& e:pickers)if(e.picker)delete e.picker.data();pickers.clear();Vst3Picker::shutdown_jobs();};
    if(QThread::currentThread()==qApp->thread())close();else QMetaObject::invokeMethod(qApp,close,Qt::BlockingQueuedConnection);
}
}
