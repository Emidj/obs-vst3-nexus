#include "vst3/scan_wire.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <stdexcept>
namespace nexus::vst3::wire {
namespace {
void need(bool v,const char* text){if(!v)throw std::runtime_error(text);}
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<uint8_t> object;
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    void init(){
        need(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider failed");
        DWORD length=0,done=0;
        need(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&done,0)>=0,"SHA256 object failed");
        object.resize(length);
        need(BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)>=0,"SHA256 initialization failed");
    }
    void add(const uint8_t* bytes,size_t size){
        need(size<=std::numeric_limits<ULONG>::max(),"SHA256 input too large");
        if(size)need(BCryptHashData(hash,const_cast<PUCHAR>(bytes),ULONG(size),0)>=0,"SHA256 update failed");
    }
    std::array<uint8_t,32> finish(){
        std::array<uint8_t,32> bytes{};
        need(BCryptFinishHash(hash,bytes.data(),ULONG(bytes.size()),0)>=0,"SHA256 finalization failed");return bytes;
    }
};
std::string hex(const uint8_t* data,size_t count){
    constexpr char digits[]="0123456789abcdef";std::string text; text.reserve(count*2);
    for(size_t i=0;i<count;++i){text+=digits[data[i]>>4];text+=digits[data[i]&15];}return text;
}
void validate_class(ClassInfo& c){
    need(c.id.size()==32,"Invalid class ID");
    for(auto& ch:c.id){
        if(ch>='a'&&ch<='f')ch=char(ch-'a'+'A');
        need((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F'),"Invalid class ID");
    }
    c.effect=effect_candidate(c.category,c.subcategories);
}
}
std::wstring native_path(const std::filesystem::path& path){
    auto absolute=std::filesystem::absolute(path).lexically_normal();
    absolute.make_preferred();auto text=absolute.native();
    const std::wstring slash(1,wchar_t(92));
    const auto prefix=slash+slash+L"?"+slash;
    if(text.compare(0,prefix.size(),prefix)==0)return text;
    if(text.compare(0,2,slash+slash)==0)return prefix+L"UNC"+slash+text.substr(2);
    return prefix+text;
}
std::string random_token(){
    std::array<uint8_t,16> bytes{};need(BCryptGenRandom(nullptr,bytes.data(),ULONG(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Random token failed");
    return hex(bytes.data(),bytes.size());
}
bool valid_text(const std::string& text){
    if(text.size()>32768||text.find(char(0))!=std::string::npos)return false;
    for(unsigned char c:text)if(c<32||c==127)return false;
    return text.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0)>0;
}
std::string hash_bytes(const std::vector<uint8_t>& data){Hash h;h.init();h.add(data.data(),data.size());const auto result=h.finish();return hex(result.data(),result.size());}
std::string hash_file(const std::filesystem::path& path,uint64_t maximum){
    File f;f.value=CreateFileW(native_path(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    need(f.value!=INVALID_HANDLE_VALUE,"Cannot open file for fingerprint");
    LARGE_INTEGER size{};need(GetFileSizeEx(f.value,&size)&&size.QuadPart>=0&&uint64_t(size.QuadPart)<=maximum,"Fingerprint file exceeds limit");
    Hash hash;hash.init();std::array<uint8_t,65536> bytes{};uint64_t remaining=uint64_t(size.QuadPart);
    while(remaining){
        DWORD read=0;const auto count=DWORD((std::min)(remaining,uint64_t(bytes.size())));
        need(ReadFile(f.value,bytes.data(),count,&read,nullptr)&&read==count,"File changed while fingerprinting");
        hash.add(bytes.data(),read);remaining-=read;
    }
    DWORD extra=0;need(ReadFile(f.value,bytes.data(),1,&extra,nullptr)&&extra==0,"File grew while fingerprinting");
    const auto result=hash.finish();return hex(result.data(),result.size());
}
std::vector<uint8_t> read_file(const std::filesystem::path& path,size_t maximum){
    File f;f.value=CreateFileW(native_path(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    need(f.value!=INVALID_HANDLE_VALUE,"Cannot read packet");
    LARGE_INTEGER size{};need(GetFileSizeEx(f.value,&size)&&size.QuadPart>=0&&uint64_t(size.QuadPart)<=maximum,"Packet exceeds limit");
    std::vector<uint8_t> data(size_t(size.QuadPart));DWORD count=0;
    if(!data.empty())need(ReadFile(f.value,data.data(),DWORD(data.size()),&count,nullptr)&&count==data.size(),"Incomplete packet");
    uint8_t extra=0;need(ReadFile(f.value,&extra,1,&count,nullptr)&&count==0,"Packet changed while reading");return data;
}
void atomic_write(const std::filesystem::path& path,const std::vector<uint8_t>& data){
    need(data.size()<=max_packet,"Packet exceeds limit");
    auto temp=path;temp+=std::filesystem::path("."+random_token()+".tmp");
    File f;f.value=CreateFileW(native_path(temp).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create packet (Windows "+std::to_string(GetLastError())+", path chars "+std::to_string(temp.native().size())+")");
    DWORD count=0;
    const bool written=(data.empty()||(WriteFile(f.value,data.data(),DWORD(data.size()),&count,nullptr)&&count==data.size()))&&FlushFileBuffers(f.value);
    CloseHandle(f.value);f.value=INVALID_HANDLE_VALUE;
    if(!written){DeleteFileW(native_path(temp).c_str());throw std::runtime_error("Cannot write packet");}
    if(!MoveFileExW(native_path(temp).c_str(),native_path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(native_path(temp).c_str());throw std::runtime_error("Cannot commit packet");
    }
}
void Writer::number(uint64_t value){
    need(data.size()+8<=max_packet-32,"Packet too large");
    for(unsigned i=0;i<8;++i)data.push_back(uint8_t(value>>(i*8)));
}
void Writer::text(const std::string& value,size_t maximum){
    need(value.size()<=maximum&&valid_text(value),"Invalid UTF-8 packet text");
    number(value.size());need(data.size()+value.size()<=max_packet-32,"Packet too large");
    data.insert(data.end(),value.begin(),value.end());
}
void Writer::classes(const std::vector<ClassInfo>& classes){
    need(classes.size()<=4096,"Too many classes");number(classes.size());std::set<std::string> ids;
    for(auto c:classes){
        validate_class(c);need(ids.insert(c.id).second,"Duplicate class ID");
        text(c.id,32);text(c.name);text(c.vendor);text(c.version);text(c.category);text(c.subcategories);
    }
}
std::vector<uint8_t> Writer::finish(){
    Hash h;h.init();h.add(data.data(),data.size());const auto sum=h.finish();data.insert(data.end(),sum.begin(),sum.end());return std::move(data);
}
Reader::Reader(const std::vector<uint8_t>& data):data_(data){
    need(data.size()>=32&&data.size()<=max_packet,"Invalid packet length");limit_=data.size()-32;
    Hash h;h.init();h.add(data.data(),limit_);const auto digest=h.finish();
    need(std::equal(digest.begin(),digest.end(),data.begin()+limit_),"Packet checksum mismatch");
}
uint64_t Reader::number(){
    need(position_<=limit_&&limit_-position_>=8,"Truncated packet");uint64_t value=0;
    for(unsigned i=0;i<8;++i)value|=uint64_t(data_[position_++])<<(i*8);return value;
}
std::string Reader::text(size_t maximum){
    const auto length=number();need(length<=maximum&&length<=limit_-position_,"Invalid packet string length");
    std::string result(reinterpret_cast<const char*>(data_.data()+position_),size_t(length));position_+=size_t(length);
    need(valid_text(result),"Invalid UTF-8 packet text");return result;
}
std::vector<ClassInfo> Reader::classes(){
    const auto count=number();need(count<=4096,"Too many packet classes");std::vector<ClassInfo> result;result.reserve(size_t(count));
    std::set<std::string> ids;
    for(uint64_t i=0;i<count;++i){
        ClassInfo c;c.id=text(32);c.name=text();c.vendor=text();c.version=text();c.category=text();c.subcategories=text();
        validate_class(c);need(ids.insert(c.id).second,"Duplicate packet class");
        result.push_back(std::move(c));
    }return result;
}
void Reader::end()const{need(position_==limit_,"Unexpected packet data");}
std::vector<uint8_t> encode_report(const Report& report){
    Writer writer;writer.text("NEXUS-VST3-REPLY");writer.number(1);writer.text(report.nonce,32);
    writer.text(report.error,16384);writer.classes(report.classes);return writer.finish();
}
Report decode_report(const std::vector<uint8_t>& data){
    Reader reader(data);need(reader.text()=="NEXUS-VST3-REPLY"&&reader.number()==1,"Unsupported worker protocol");
    Report report;report.nonce=reader.text(32);need(report.nonce.size()==32,"Invalid reply nonce");
    report.error=reader.text(16384);report.classes=reader.classes();reader.end();
    need(report.error.empty()||report.classes.empty(),"Failure response contains classes");return report;
}
}
