#include "obs/rack_settings.hpp"
#include "ui/presets.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <functional>
#include <limits>
using namespace nexus;
namespace {
size_t checks=0;
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Malformed persistence accepted");}
struct DataDelete{void operator()(obs_data_t* p)const{obs_data_release(p);}};
using Data=std::unique_ptr<obs_data_t,DataDelete>;
RackDocument fixture(){
    auto n=default_node(ModuleKind::vst3,UINT64_MAX);
    n.vst3.path=u8"C:/Missing/\u00f1 \u97f3.vst3";n.vst3.class_id=std::string(32,'A');
    n.vst3.name=u8"Effect \u00f1";n.vst3.vendor="Fixture vendor";
    n.vst3.has_state=true;
    for(size_t i=0;i<256;++i)n.vst3.component_state.push_back(uint8_t(i));
    n.vst3.controller_state={0,255,1,128,0};
    n.vst3.parameters={{0,0},{UINT32_MAX,1},{7,.25}};
    RackDocument doc{{n},{n},true,true};
    doc.b[0].enabled=false;doc.b[0].vst3.parameters.back().second=.5;
    doc.b[0].vst3.controller_state={42,0,255};return doc;
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
    QTemporaryDir directory;check(directory.isValid(),"Test directory");
    const auto doc=fixture();
    const auto json=ui::rack_json(doc);
    check(json["schema_version"].toInt()==2,"VST3 preset schema");
    check(same_document(doc,ui::parse_rack_json(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object())),"Preset JSON byte/Unicode/A-B roundtrip");
    auto native=default_document();
    check(ui::rack_json(native)["schema_version"].toInt()==1,"Native schema retained");
    check(same_document(native,ui::parse_rack_json(ui::rack_json(native))),"Native roundtrip");
    auto corrupt=[&](std::function<void(QJsonObject&)> edit){
        auto bad=json;auto a=bad["a"].toArray();auto node=a[0].toObject();
        auto v=node["vst3"].toObject();edit(v);node["vst3"]=v;a[0]=node;bad["a"]=a;
        rejects([&]{ui::parse_rack_json(bad);});
    };
    corrupt([](auto& v){v.remove("state_present");});
    corrupt([](auto& v){v["state_present"]="true";});
    corrupt([](auto& v){v["component_hex"]="0";});
    corrupt([](auto& v){v["component_hex"]="gg";});
    corrupt([](auto& v){v["state_present"]=false;});
    corrupt([](auto& v){v["class_id"]="invalid";});
    corrupt([](auto& v){v["path"]="relative.vst3";});
    corrupt([](auto& v){v.remove("parameters");});
    corrupt([](auto& v){auto p=v["parameters"].toArray();p.append(p[0]);v["parameters"]=p;});
    corrupt([](auto& v){v["parameters"]=QJsonArray{QJsonObject{{"id","4294967296"},{"value",.5}}};});
    corrupt([](auto& v){v["parameters"]=QJsonArray{QJsonObject{{"id","-1"},{"value",.5}}};});
    corrupt([](auto& v){v["parameters"]=QJsonArray{QJsonObject{{"id","01"},{"value",.5}}};});
    corrupt([](auto& v){v["parameters"]=QJsonArray{QJsonObject{{"id",7},{"value",.5}}};});
    corrupt([](auto& v){v["parameters"]=QJsonArray{QJsonObject{{"id","7"},{"value",1.01}}};});
    {auto bad=json;bad["schema_version"]=1;rejects([&]{ui::parse_rack_json(bad);});}
    Data settings(obs_data_create());obs_bridge::write_rack(settings.get(),doc);
    check(obs_data_get_int(settings.get(),"rack_version")==2,"VST3 scene schema");
    Data restored(obs_data_create_from_json(obs_data_get_json(settings.get())));
    check(same_document(doc,obs_bridge::read_rack(restored.get())),"Scene JSON byte/Unicode/A-B roundtrip");
    auto scene_corrupt=[&](std::function<void(obs_data_t*)> edit){
        Data bad(obs_data_create_from_json(obs_data_get_json(settings.get())));
        auto* nodes=obs_data_get_array(bad.get(),"rack_a");auto* node=obs_data_array_item(nodes,0);
        edit(node);obs_data_release(node);obs_data_array_release(nodes);
        rejects([&]{obs_bridge::read_rack(bad.get());});
    };
    scene_corrupt([](auto* n){obs_data_unset_user_value(n,"vst3_state_present");});
    scene_corrupt([](auto* n){obs_data_set_string(n,"vst3_state_present","true");});
    scene_corrupt([](auto* n){obs_data_set_string(n,"vst3_component_hex","0");});
    scene_corrupt([](auto* n){obs_data_set_string(n,"vst3_controller_hex","xx");});
    scene_corrupt([](auto* n){obs_data_set_bool(n,"vst3_state_present",false);});
    scene_corrupt([](auto* n){obs_data_unset_user_value(n,"vst3_parameters");});
    scene_corrupt([](auto* n){
        auto* p=obs_data_get_array(n,"vst3_parameters");auto* first=obs_data_array_item(p,0);
        obs_data_array_push_back(p,first);obs_data_release(first);obs_data_array_release(p);
    });
    scene_corrupt([](auto* n){
        auto* p=obs_data_get_array(n,"vst3_parameters");auto* first=obs_data_array_item(p,0);
        obs_data_set_string(first,"id","4294967296");obs_data_release(first);obs_data_array_release(p);
    });
    scene_corrupt([](auto* n){
        auto* p=obs_data_get_array(n,"vst3_parameters");auto* first=obs_data_array_item(p,0);
        obs_data_set_double(first,"value",std::numeric_limits<double>::quiet_NaN());obs_data_release(first);obs_data_array_release(p);
    });
    {Data bad(obs_data_create_from_json(obs_data_get_json(settings.get())));
        obs_data_set_int(bad.get(),"rack_version",1);rejects([&]{obs_bridge::read_rack(bad.get());});}
    // Invalid writes validate both sides before changing the destination.
    auto invalid=doc;invalid.b[0].vst3.parameters.push_back({7,.75});
    const std::string before=obs_data_get_json(settings.get());
    rejects([&]{obs_bridge::write_rack(settings.get(),invalid);});
    check(before==obs_data_get_json(settings.get()),"Rejected scene write changed destination");
    obs_bridge::write_rack(settings.get(),native);
    check(obs_data_get_int(settings.get(),"rack_version")==1&&same_document(native,obs_bridge::read_rack(settings.get())),"Native scene compatibility");
    ui::PresetStore store(directory.path());
    const auto saved=store.save_as("VST3 missing plugin",doc);
    check(same_rack(saved.document.active(),doc.active()),"Save active snapshot");
    const auto loaded=store.inspect_file(saved.path);
    check(same_document(loaded.document,saved.document),"Save/inspect opaque state");
    check(loaded.document.a[0].vst3.path==doc.b[0].vst3.path,"Missing plugin path preserved without loading");
    const auto imported=store.import_file(saved.path);
    check(imported.category=="Imported"&&same_document(imported.document,saved.document),"VST3 import");
    QFile existing(saved.path);check(existing.open(QIODevice::ReadOnly),"Read preset");
    const auto original=existing.readAll();existing.close();
    auto large=doc;large.a[0].vst3.component_state.assign(400000,255);large.b=large.a;large.use_b=false;
    const auto largeFile=directory.path()+"/large.json";
    store.export_file(largeFile,"Larger than old limit",large);
    check(QFile(largeFile).size()>1024*1024,"Large preset fixture size");
    check(same_rack(store.inspect_file(largeFile).document.active(),large.active()),"Preset over 1 MiB roundtrip");
    auto huge=doc;huge.a[0].vst3.component_state.assign(16*1024*1024,255);
    huge.a[0].vst3.controller_state={1};huge.b=huge.a;huge.use_b=false;
    rejects([&]{store.export_file(saved.path,"Oversize",huge);});
    check(existing.open(QIODevice::ReadOnly)&&existing.readAll()==original,"Oversize export changed existing file");
    rejects([]{state_unhex(std::string(32*1024*1024+2,'0'));});
    std::cout<<"PASS: "<<checks<<" persistence checks; no VST3 factory or DLL loading\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
