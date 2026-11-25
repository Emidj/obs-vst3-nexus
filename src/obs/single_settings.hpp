#pragma once
#include "obs/rack_settings.hpp"
namespace nexus::obs_bridge {
inline RackDocument read_single(obs_data_t* settings){
    if(obs_data_has_user_value(settings,"vst3_nexus_version")&&obs_data_get_int(settings,"vst3_nexus_version")!=1)throw std::invalid_argument("Unsupported VST3 NEXUS state version");
    RackDocument doc;doc.bypass=obs_data_get_bool(settings,"bypass");
    if(obs_data_has_user_value(settings,"rack_version"))throw std::invalid_argument("Legacy rack data cannot be loaded as a single VST3 effect");
    if(obs_data_has_user_value(settings,"effect")){
        if(!typed_user_value(settings,"effect",OBS_DATA_OBJECT))throw std::invalid_argument("Invalid effect state");
        auto* effect=obs_data_get_obj(settings,"effect");
        try{auto node=default_node(ModuleKind::vst3,1);node.vst3=read_vst3(effect);doc.a.push_back(node);validate_document(doc);obs_data_release(effect);}
        catch(...){obs_data_release(effect);throw;}
    }
    return doc;
}
inline void write_single(obs_data_t* settings,const RackDocument& doc){
    if(doc.use_b||!doc.b.empty()||doc.a.size()>1||(!doc.a.empty()&&(doc.a[0].kind!=ModuleKind::vst3||doc.a[0].id!=1||!doc.a[0].enabled)))
        throw std::invalid_argument("Only one VST3 effect per filter is supported");
    validate_document(doc);obs_data_set_bool(settings,"bypass",doc.bypass);
    obs_data_set_int(settings,"vst3_nexus_version",1);
    if(doc.a.empty())obs_data_unset_user_value(settings,"effect");
    else{auto* effect=obs_data_create();write_vst3(effect,doc.a[0].vst3);obs_data_set_obj(settings,"effect",effect);obs_data_release(effect);}
}
}
