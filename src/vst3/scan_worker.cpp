#include "vst3/core.hpp"
#include "vst3/scan_wire.hpp"
#include <windows.h>
#include <filesystem>
#include <stdexcept>
namespace {
std::string safe_error(std::string text){
    for(auto& c:text)if(static_cast<unsigned char>(c)<32||c==127)c=' ';
    if(!nexus::vst3::wire::valid_text(text))for(auto& c:text)if(static_cast<unsigned char>(c)>=128)c='?';
    if(text.size()>16000)text.resize(16000);
    while(!nexus::vst3::wire::valid_text(text)&&!text.empty())text.pop_back();
    return text.empty()?"VST3 inspection failed":text;
}
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc!=5||std::wstring(argv[1])!=L"inspect-worker")return 3;
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    nexus::vst3::wire::Report report;report.nonce=std::filesystem::path(argv[4]).u8string();
    try{
        report.classes=nexus::vst3::inspect(std::filesystem::path(argv[2]).u8string());
    }catch(const std::exception& e){report.error=safe_error(e.what());}
    try{nexus::vst3::wire::atomic_write(std::filesystem::path(argv[3]),nexus::vst3::wire::encode_report(report));}
    catch(...){if(SUCCEEDED(com))CoUninitialize();return 3;}
    if(SUCCEEDED(com))CoUninitialize();
    return report.error.empty()?0:2;
}
