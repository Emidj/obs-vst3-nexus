#include "ui/presets.hpp"
#include "dsp/state_text.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace nexus::ui {
namespace {
void demand(bool value,const char* text){if(!value)throw std::invalid_argument(text);}
bool boolean(const QJsonObject& object,const char* key){
    demand(object.value(key).isBool(),"Missing boolean preset field");return object.value(key).toBool();
}
double number(const QJsonObject& object,const char* key,double low,double high){
    const auto v=object.value(key);demand(v.isDouble(),"Missing numeric preset field");
    const auto x=v.toDouble();demand(std::isfinite(x)&&x>=low&&x<=high,"Preset parameter out of range");return x;
}
QJsonObject vst3_json(const Vst3Settings& v){
    QJsonArray parameters;
    for(const auto& p:v.parameters)parameters.append(QJsonObject{{"id",QString::number(p.first)},{"value",p.second}});
    return {{"name",QString::fromStdString(v.name)},{"vendor",QString::fromStdString(v.vendor)},{"path",QString::fromStdString(v.path)},{"class_id",QString::fromStdString(v.class_id)},
        {"state_present",v.has_state},{"component_hex",QString::fromStdString(state_hex(v.component_state))},
        {"controller_hex",QString::fromStdString(state_hex(v.controller_state))},{"parameters",parameters}};
}
Vst3Settings vst3_read(const QJsonValue& value){
    demand(value.isObject(),"Missing VST3 configuration");const auto object=value.toObject();
    for(const auto* key:{"path","class_id","component_hex","controller_hex"})
        demand(object.value(key).isString(),"Missing VST3 text field");
    Vst3Settings v;
    for(const auto* key:{"name","vendor"})if(object.contains(key))demand(object.value(key).isString(),"Invalid VST3 metadata");
    v.name=object.value("name").toString().toStdString();v.vendor=object.value("vendor").toString().toStdString();
    v.path=object.value("path").toString().toStdString();
    v.class_id=object.value("class_id").toString().toStdString();v.has_state=boolean(object,"state_present");
    v.component_state=state_unhex(object.value("component_hex").toString().toStdString());
    v.controller_state=state_unhex(object.value("controller_hex").toString().toStdString());
    demand(object.value("parameters").isArray(),"Missing VST3 parameters");
    const auto parameters=object.value("parameters").toArray();
    demand(parameters.size()<=4096,"Too many VST3 parameters");
    for(const auto& item:parameters){
        demand(item.isObject(),"Invalid VST3 parameter");const auto p=item.toObject();
        demand(p.value("id").isString(),"Missing VST3 parameter ID");
        v.parameters.emplace_back(parameter_id(p.value("id").toString().toStdString()),number(p,"value",0,1));
    }
    return v;
}
QJsonArray nodes_json(const RackConfig& config){
    QJsonArray nodes;
    for(const auto& n:config){
        QJsonArray bands;for(const auto& band:n.eq.bands)bands.append(QJsonObject{{"frequency",band.frequency},{"gain_db",band.gain_db},{"q",band.q},{"enabled",band.enabled}});
        QJsonObject dynamics;for(const auto& field:dynamics_fields)dynamics[field.key]=n.dynamics.*(field.member);
        dynamics["gate"]=n.dynamics.gate;
        QJsonObject encoded{{"dynamics",dynamics},{"id",QString::number(n.id)},{"type",module_id(n.kind)},{"enabled",n.enabled},{"value",n.value},
            {"eq",QJsonObject{{"enabled",n.eq.enabled},{"bands",bands}}}};
        if(n.kind==ModuleKind::vst3)encoded["vst3"]=vst3_json(n.vst3);
        nodes.append(encoded);
    }return nodes;
}
RackConfig nodes_read(const QJsonValue& value){
    demand(value.isArray(),"Missing module list");const auto array=value.toArray();demand(array.size()<=16,"Too many modules");
    RackConfig result;size_t remaining_state=32*1024*1024;
    for(const auto& item:array){
        demand(item.isObject(),"Invalid module");const auto object=item.toObject();
        demand(object.value("id").isString()&&object.value("type").isString(),"Missing module identity");
        bool ok=false;const auto id=object.value("id").toString().toULongLong(&ok);demand(ok&&id,"Invalid module ID");
        auto node=default_node(parse_module_id(object.value("type").toString().toStdString()),id);
        node.enabled=boolean(object,"enabled");node.value=float(number(object,"value",-60000,60000));
        demand(object.value("eq").isObject(),"Missing EQ state");const auto eq=object.value("eq").toObject();
        node.eq.enabled=boolean(eq,"enabled");demand(eq.value("bands").isArray(),"Missing EQ bands");
        const auto bands=eq.value("bands").toArray();demand(bands.size()==4,"Expected four EQ bands");
        for(int i=0;i<4;++i){
            demand(bands[i].isObject(),"Invalid EQ band");const auto band=bands[i].toObject();
            node.eq.bands[size_t(i)]={float(number(band,"frequency",20,20000)),float(number(band,"gain_db",-18,18)),
                float(number(band,"q",.2,10)),boolean(band,"enabled")};
        }
        if(object.contains("dynamics")||is_dynamics(node.kind)){
            demand(object.value("dynamics").isObject(),"Missing dynamics state");
            const auto dynamics=object.value("dynamics").toObject();
            for(const auto& field:dynamics_fields)node.dynamics.*(field.member)=float(number(dynamics,field.key,field.low,field.high));
            node.dynamics.gate=boolean(dynamics,"gate");
        }
        if(node.kind==ModuleKind::vst3){
            node.vst3=vst3_read(object.value("vst3"));
            const auto bytes=node.vst3.component_state.size()+node.vst3.controller_state.size();
            if(bytes>remaining_state)throw std::invalid_argument("Rack VST3 state exceeds 32 MiB");
            remaining_state-=bytes;
        }
        result.push_back(node);
    }validate_rack(result);return result;
}
QJsonObject read_file(const QString& path){
    QFile file(path);demand(file.open(QIODevice::ReadOnly),"Cannot open preset");
    demand(file.size()>0&&file.size()<=80*1024*1024,"Invalid preset file size");
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    demand(error.error==QJsonParseError::NoError&&document.isObject(),"Invalid preset JSON");
    return document.object();
}
RackDocument single_snapshot(const RackDocument& input){return {input.active(),input.active(),false,input.bypass};}
void name_valid(const QString& name){
    demand(!name.trimmed().isEmpty()&&name.size()<=80,"Preset name must have 1-80 characters");
    for(const auto c:name)demand(!c.isNull()&&c.unicode()>=32,"Invalid preset name");
}
}
QJsonObject rack_json(const RackDocument& document){
    validate_document(document);
    return {{"format","nexus.audio-rack"},{"schema_version",contains_vst3(document)?2:1},{"a",nodes_json(document.a)},{"b",nodes_json(document.b)},{"use_b",document.use_b},{"bypass",document.bypass}};
}
RackDocument parse_rack_json(const QJsonObject& object){
    demand(object.value("format").toString()=="nexus.audio-rack","Unknown preset format");
    demand(object.value("schema_version").isDouble()&&(object.value("schema_version").toDouble()==1||object.value("schema_version").toDouble()==2),"Unsupported preset version");
    RackDocument result;result.a=nodes_read(object.value("a"));result.b=nodes_read(object.value("b"));result.use_b=boolean(object,"use_b");result.bypass=boolean(object,"bypass");
    demand(object.value("schema_version").toDouble()==2||!contains_vst3(result),"VST3 requires preset schema 2");
    validate_document(result);return result;
}
std::vector<Preset> PresetStore::list()const{
    std::vector<Preset> result;
    auto add=[&](const char* name,RackConfig nodes){RackDocument doc{nodes,nodes,false};result.push_back({name,"Factory",{},doc});};
    add("Flat",{});
    auto music=default_node(ModuleKind::equalizer,1);music.eq.bands[0].gain_db=2;music.eq.bands[1].gain_db=-2;
    add("Music - gentle shape",{music});
    auto hp=default_node(ModuleKind::high_pass,1);auto voice=default_node(ModuleKind::equalizer,2);
    voice.eq.bands[1].frequency=250;voice.eq.bands[1].gain_db=-2;voice.eq.bands[2].gain_db=2;
    auto comp=default_node(ModuleKind::compressor,3);comp.dynamics.ratio=3;
    add("Voice - starting point",{hp,voice,comp});
    auto trim=default_node(ModuleKind::gain,1);trim.value=-3;auto low=default_node(ModuleKind::low_pass,2);low.value=16000;
    auto limiter=default_node(ModuleKind::limiter,3);
    add("Streaming - headroom",{trim,low,limiter});
    if(directory_.trimmed().isEmpty())return result;
    for(const QString category:{"My Presets","Imported"}){
        QDir dir(QDir(directory_).filePath(category));
        for(const auto& name:dir.entryList({"*.json"},QDir::Files,QDir::Name)){
            const auto path=dir.filePath(name);
            try{const auto json=read_file(path);const auto label=json.value("name").toString();name_valid(label);
                result.push_back({label,category,path,parse_rack_json(json)});
            }catch(const std::exception&){/* Preserve invalid files without loading them. */}
        }
    }return result;
}
void PresetStore::export_file(const QString& path,const QString& name,const RackDocument& input)const{
    name_valid(name);auto json=rack_json(single_snapshot(input));json["name"]=name.trimmed();
    const auto bytes=QJsonDocument(json).toJson(QJsonDocument::Indented);
    demand(bytes.size()<=80*1024*1024,"Preset exceeds 80 MiB file limit");
    QSaveFile file(path);demand(file.open(QIODevice::WriteOnly),"Cannot create preset file");
    demand(file.write(bytes)==bytes.size()&&file.commit(),"Cannot commit preset file");
}
Preset PresetStore::persist(const QString& category,const QString& name,const RackDocument& document){
    demand(category=="My Presets"||category=="Imported","Factory presets are read-only");
    demand(!directory_.trimmed().isEmpty(),"Preset storage directory is unavailable");
    validate_document(document);
    name_valid(name);QDir dir(QDir(directory_).filePath(category));demand(dir.mkpath("."),"Cannot create preset directory");
    const auto path=dir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    export_file(path,name,document);return {name.trimmed(),category,path,single_snapshot(document)};
}
Preset PresetStore::save_as(const QString& name,const RackDocument& document){return persist("My Presets",name,document);}
Preset PresetStore::inspect_file(const QString& path)const{
    const auto json=read_file(path);const auto document=parse_rack_json(json);
    const auto name=json.value("name").toString("Imported preset");name_valid(name);
    return {name,"Imported",path,document};
}
Preset PresetStore::import_value(const Preset& preset){return persist("Imported",preset.name,preset.document);}
Preset PresetStore::import_file(const QString& path){
    return import_value(inspect_file(path));
}
}