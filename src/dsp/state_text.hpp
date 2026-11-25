#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace nexus {
inline std::string state_hex(const std::vector<uint8_t>& bytes){
    if(bytes.size()>16*1024*1024)throw std::invalid_argument("VST3 state exceeds 16 MiB");
    static constexpr char digits[]="0123456789abcdef";
    std::string result(bytes.size()*2,'0');
    for(size_t i=0;i<bytes.size();++i){result[2*i]=digits[bytes[i]>>4];result[2*i+1]=digits[bytes[i]&15];}
    return result;
}
inline std::vector<uint8_t> state_unhex(std::string_view text){
    if(text.size()%2||text.size()>32*1024*1024)throw std::invalid_argument("Invalid VST3 state encoding size");
    auto digit=[](char c)->uint8_t{
        if(c>='0'&&c<='9')return uint8_t(c-'0');
        if(c>='a'&&c<='f')return uint8_t(c-'a'+10);
        throw std::invalid_argument("Invalid VST3 state hex");
    };
    std::vector<uint8_t> result(text.size()/2);
    for(size_t i=0;i<result.size();++i)result[i]=uint8_t((digit(text[2*i])<<4)|digit(text[2*i+1]));
    return result;
}
inline uint32_t parameter_id(const std::string& text){
    if(text.empty()||text.size()>10||(text.size()>1&&text[0]=='0'))
        throw std::invalid_argument("Invalid VST3 parameter ID");
    uint64_t result=0;
    for(char c:text){
        if(c<'0'||c>'9')throw std::invalid_argument("Invalid VST3 parameter ID");
        result=result*10+uint64_t(c-'0');
        if(result>UINT32_MAX)throw std::invalid_argument("VST3 parameter ID overflow");
    }
    return uint32_t(result);
}
}
