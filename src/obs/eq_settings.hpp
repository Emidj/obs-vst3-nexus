#pragma once
#include "dsp/equalizer.hpp"
#include <obs.h>
#include <string>
namespace nexus::obs_bridge {
inline std::string key(size_t i,const char* parameter){return "eq_band_"+std::to_string(i)+"_"+parameter;}
inline void defaults(obs_data_t* data){
    const EqSettings initial;obs_data_set_default_bool(data,"eq_enabled",true);
    for(size_t i=0;i<4;++i){
        obs_data_set_default_double(data,key(i,"frequency").c_str(),initial.bands[i].frequency);
        obs_data_set_default_double(data,key(i,"gain").c_str(),0);
        obs_data_set_default_double(data,key(i,"q").c_str(),1);
        obs_data_set_default_bool(data,key(i,"enabled").c_str(),true);
    }
}
inline EqSettings read(obs_data_t* data){
    defaults(data);EqSettings values;values.enabled=obs_data_get_bool(data,"eq_enabled");
    for(size_t i=0;i<4;++i){
        const double f=obs_data_get_double(data,key(i,"frequency").c_str());
        const double g=obs_data_get_double(data,key(i,"gain").c_str());
        const double q=obs_data_get_double(data,key(i,"q").c_str());
        // Check doubles before narrowing: malformed scene data cannot overflow to floats.
        if(std::isfinite(f)&&f>=20&&f<=20000&&std::isfinite(g)&&g>=-18&&g<=18&&std::isfinite(q)&&q>=.2&&q<=10)
            values.bands[i]={float(f),float(g),float(q),obs_data_get_bool(data,key(i,"enabled").c_str())};
    }
    return values;
}
inline void write(obs_data_t* data,const EqSettings& values){
    obs_data_set_bool(data,"eq_enabled",values.enabled);
    for(size_t i=0;i<4;++i){
        obs_data_set_double(data,key(i,"frequency").c_str(),values.bands[i].frequency);
        obs_data_set_double(data,key(i,"gain").c_str(),values.bands[i].gain_db);
        obs_data_set_double(data,key(i,"q").c_str(),values.bands[i].q);
        obs_data_set_bool(data,key(i,"enabled").c_str(),values.bands[i].enabled);
    }
}
}