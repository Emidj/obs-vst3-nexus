#pragma once
#include <string>
namespace nexus::vst3 {
struct ClassInfo {
    std::string path,id,name,vendor,version,category,subcategories;
    bool effect=false;
};
inline bool effect_candidate(const std::string& category,const std::string& subcategories){
    if(category!="Audio Module Class")return false;
    size_t start=0;
    do{
        const auto end=subcategories.find('|',start);const auto item=subcategories.substr(start,end-start);
        if(item=="Instrument"||item=="Synth")return false;
        if(end==std::string::npos)break;start=end+1;
    }while(start<=subcategories.size());
    return true;
}
}
