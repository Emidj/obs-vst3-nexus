#pragma once
#include "dsp/rack.hpp"
#include "dsp/state_text.hpp"
#include "obs/eq_settings.hpp"
#include <obs.h>
#include <stdexcept>
namespace nexus::obs_bridge {
inline bool typed_user_value(obs_data_t* data,const char* name,obs_data_type type){
    if(!obs_data_has_user_value(data,name))return false;
    auto* item=obs_data_item_byname(data,name);const bool valid=item&&obs_data_item_gettype(item)==type;
    obs_data_item_release(&item);return valid;
}
inline Vst3Settings read_vst3(obs_data_t* data){
    for(const auto* key:{"vst3_path","vst3_class_id","vst3_component_hex","vst3_controller_hex"})
        if(!typed_user_value(data,key,OBS_DATA_STRING))throw std::invalid_argument("Missing VST3 text field");
    if(!typed_user_value(data,"vst3_state_present",OBS_DATA_BOOLEAN))
        throw std::invalid_argument("Missing VST3 state marker");
    Vst3Settings v;
    for(const auto* key:{"vst3_name","vst3_vendor"})if(obs_data_has_user_value(data,key)&&!typed_user_value(data,key,OBS_DATA_STRING))
        throw std::invalid_argument("Invalid VST3 metadata");
    v.name=obs_data_get_string(data,"vst3_name");v.vendor=obs_data_get_string(data,"vst3_vendor");
    v.path=obs_data_get_string(data,"vst3_path");v.class_id=obs_data_get_string(data,"vst3_class_id");
    v.has_state=obs_data_get_bool(data,"vst3_state_present");
    v.component_state=state_unhex(obs_data_get_string(data,"vst3_component_hex"));
    v.controller_state=state_unhex(obs_data_get_string(data,"vst3_controller_hex"));
    auto* parameters=obs_data_get_array(data,"vst3_parameters");
    if(!parameters)throw std::invalid_argument("Missing VST3 parameters");
    try{
        const auto count=obs_data_array_count(parameters);
        if(count>4096)throw std::invalid_argument("Too many VST3 parameters");
        for(size_t i=0;i<count;++i){
            auto* p=obs_data_array_item(parameters,i);
            try{
                if(!typed_user_value(p,"id",OBS_DATA_STRING)||!typed_user_value(p,"value",OBS_DATA_NUMBER))
                    throw std::invalid_argument("Invalid VST3 parameter fields");
                v.parameters.emplace_back(parameter_id(obs_data_get_string(p,"id")),obs_data_get_double(p,"value"));
                obs_data_release(p);
            }catch(...){obs_data_release(p);throw;}
        }
        obs_data_array_release(parameters);return v;
    }catch(...){obs_data_array_release(parameters);throw;}
}
inline void write_vst3(obs_data_t* data,const Vst3Settings& v){
    obs_data_set_string(data,"vst3_name",v.name.c_str());obs_data_set_string(data,"vst3_vendor",v.vendor.c_str());
    obs_data_set_string(data,"vst3_path",v.path.c_str());obs_data_set_string(data,"vst3_class_id",v.class_id.c_str());
    obs_data_set_bool(data,"vst3_state_present",v.has_state);
    obs_data_set_string(data,"vst3_component_hex",state_hex(v.component_state).c_str());
    obs_data_set_string(data,"vst3_controller_hex",state_hex(v.controller_state).c_str());
    auto* parameters=obs_data_array_create();
    for(const auto& p:v.parameters){
        auto* item=obs_data_create();obs_data_set_string(item,"id",std::to_string(p.first).c_str());
        obs_data_set_double(item,"value",p.second);obs_data_array_push_back(parameters,item);obs_data_release(item);
    }
    obs_data_set_array(data,"vst3_parameters",parameters);obs_data_array_release(parameters);
}
inline RackConfig read_nodes(obs_data_t* root,const char* key){
    auto* array=obs_data_get_array(root,key);
    if(!array)throw std::invalid_argument("Missing rack module list");
    const auto count=obs_data_array_count(array);
    if(count>16){obs_data_array_release(array);throw std::invalid_argument("Too many rack modules");}
    RackConfig result;size_t remaining_state=32*1024*1024;
    try{
        for(size_t i=0;i<count;++i){
            auto* data=obs_data_array_item(array,i);
            try{
                const std::string id=obs_data_get_string(data,"id");size_t used=0;
                const auto value=std::stoull(id,&used);
                if(id.empty()||id[0]=='-'||used!=id.size()||!value)throw std::invalid_argument("Invalid rack node ID");
                auto node=default_node(parse_module_id(obs_data_get_string(data,"type")),value);
                node.enabled=obs_data_get_bool(data,"enabled");
                const auto amount=obs_data_get_double(data,"value");
                if(!std::isfinite(amount)||amount< -60000||amount>60000)throw std::invalid_argument("Invalid rack value");
                node.value=float(amount);node.eq.enabled=obs_data_get_bool(data,"eq_enabled");
                for(size_t band=0;band<4;++band){
                    const double f=obs_data_get_double(data,nexus::obs_bridge::key(band,"frequency").c_str());
                    const double g=obs_data_get_double(data,nexus::obs_bridge::key(band,"gain").c_str());
                    const double q=obs_data_get_double(data,nexus::obs_bridge::key(band,"q").c_str());
                    if(!std::isfinite(f)||f<20||f>20000||!std::isfinite(g)||g<-18||g>18||!std::isfinite(q)||q<.2||q>10)
                        throw std::invalid_argument("Invalid rack EQ data");
                    node.eq.bands[band]={float(f),float(g),float(q),obs_data_get_bool(data,nexus::obs_bridge::key(band,"enabled").c_str())};
                }
                bool hasDynamics=is_dynamics(node.kind)||obs_data_has_user_value(data,"dyn_gate");
                for(const auto& field:dynamics_fields)hasDynamics=hasDynamics||obs_data_has_user_value(data,("dyn_"+std::string(field.key)).c_str());
                if(hasDynamics){
                    for(const auto& field:dynamics_fields){
                        const auto name="dyn_"+std::string(field.key);
                        if(!typed_user_value(data,name.c_str(),OBS_DATA_NUMBER))throw std::invalid_argument("Missing numeric dynamics setting");
                        const auto amount=obs_data_get_double(data,name.c_str());
                        if(!std::isfinite(amount)||amount<field.low||amount>field.high)throw std::invalid_argument("Dynamics setting out of range");
                        node.dynamics.*(field.member)=float(amount);
                    }
                    if(!typed_user_value(data,"dyn_gate",OBS_DATA_BOOLEAN))throw std::invalid_argument("Missing dynamics gate mode");
                    node.dynamics.gate=obs_data_get_bool(data,"dyn_gate");
                }
                if(node.kind==ModuleKind::vst3){
                    node.vst3=read_vst3(data);
                    const auto bytes=node.vst3.component_state.size()+node.vst3.controller_state.size();
                    if(bytes>remaining_state)throw std::invalid_argument("Rack VST3 state exceeds 32 MiB");
                    remaining_state-=bytes;
                }
                result.push_back(node);
                obs_data_release(data);
            }catch(...){obs_data_release(data);throw;}
        }
        validate_rack(result);obs_data_array_release(array);return result;
    }catch(...){obs_data_array_release(array);throw;}
}
inline RackDocument read_rack(obs_data_t* settings){
    if(!obs_data_has_user_value(settings,"rack_version")){
        auto doc=default_document();
        const double gain=obs_data_get_double(settings,"gain_db");
        doc.a[0].value=std::isfinite(gain)&&gain>=-60&&gain<=24?float(gain):0;
        doc.a[1].eq=read(settings);doc.b=doc.a;doc.bypass=obs_data_get_bool(settings,"bypass");return doc;
    }
    const auto version=obs_data_get_double(settings,"rack_version");
    if(version!=1&&version!=2)throw std::invalid_argument("Unsupported rack scene version");
    RackDocument doc{read_nodes(settings,"rack_a"),read_nodes(settings,"rack_b"),obs_data_get_bool(settings,"rack_use_b"),obs_data_get_bool(settings,"bypass")};
    if(version!=2&&contains_vst3(doc))throw std::invalid_argument("VST3 requires scene version 2");
    validate_document(doc);return doc;
}
inline void write_nodes(obs_data_t* settings,const char* key,const RackConfig& config){
    validate_rack(config);
    auto* array=obs_data_array_create();
    for(const auto& node:config){
        auto* data=obs_data_create();obs_data_set_string(data,"id",std::to_string(node.id).c_str());
        obs_data_set_string(data,"type",module_id(node.kind));obs_data_set_bool(data,"enabled",node.enabled);
        obs_data_set_double(data,"value",node.value);write(data,node.eq);
        for(const auto& field:dynamics_fields)obs_data_set_double(data,("dyn_"+std::string(field.key)).c_str(),node.dynamics.*(field.member));
        obs_data_set_bool(data,"dyn_gate",node.dynamics.gate);
        if(node.kind==ModuleKind::vst3)write_vst3(data,node.vst3);
        obs_data_array_push_back(array,data);obs_data_release(data);
    }
    obs_data_set_array(settings,key,array);obs_data_array_release(array);
}
inline void write_rack(obs_data_t* settings,const RackDocument& document){
    validate_document(document);
    write_nodes(settings,"rack_a",document.a);write_nodes(settings,"rack_b",document.b);
    obs_data_set_bool(settings,"bypass",document.bypass);obs_data_set_int(settings,"rack_version",contains_vst3(document)?2:1);obs_data_set_bool(settings,"rack_use_b",document.use_b);
}
}