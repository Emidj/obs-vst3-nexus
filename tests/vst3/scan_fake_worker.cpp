// Test-only worker. No diagnostic modes are compiled into the production worker.
#include "vst3/scan_wire.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
std::wstring quote_argument(const std::wstring& s){
    std::wstring out(1,wchar_t(34));size_t n=0;
    for(auto c:s){if(c==wchar_t(92)){++n;continue;}out.append(c==wchar_t(34)?n*2+1:n,wchar_t(92));n=0;out+=c;}
    out.append(n*2,wchar_t(92));out+=wchar_t(34);return out;
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc==2&&std::wstring(argv[1])==L"child"){Sleep(120000);return 0;}
    if(argc!=5)return 3;
    const fs::path module=argv[2],reply=argv[3];const auto mode=module.filename().wstring();
    const auto contains=[&](const wchar_t* text){return mode.find(text)!=std::wstring::npos;};
    if(contains(L"tree")){
        std::vector<wchar_t> file(32768);const auto size=GetModuleFileNameW(nullptr,file.data(),DWORD(file.size()));
        auto exe=std::wstring(file.data(),size),command=quote_argument(exe)+L" child";
        STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 44;
        auto marker=module;marker+=L".pid";{std::ofstream out(marker);out<<pi.dwProcessId;}
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    }
    if(contains(L"change")){auto marker=module;marker+=L".started";{std::ofstream out(marker);out<<"started";}Sleep(500);}
    if(contains(L"slow")||contains(L"timeout"))Sleep(120000);
    if(contains(L"crash")){RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,nullptr);return 45;}
    if(contains(L"exit"))return 42;
    if(contains(L"oversize")){std::ofstream out(reply,std::ios::binary);std::vector<char> data(5*1024*1024,'x');out.write(data.data(),data.size());return 0;}
    if(contains(L"invalid")){nexus::vst3::wire::atomic_write(reply,{1,2,3});return 0;}
    if(contains(L"spam")){const std::string chunk(4096,'x');for(int i=0;i<2048;++i)std::cout<<chunk;}
    if(contains(L"handles")){
        auto marker=module;marker+=L".handle";std::ifstream input(marker);uint64_t value=0;input>>value;
        SetEvent(reinterpret_cast<HANDLE>(uintptr_t(value)));
    }
    nexus::vst3::wire::Report report;report.nonce=fs::path(argv[4]).u8string();
    if(contains(L"nonce"))report.nonce=std::string(32,'0');
    if(contains(L"load-error"))report.error="Synthetic load failure";
    else{
        nexus::vst3::ClassInfo effect;effect.id="00112233445566778899AABBCCDDEEFF";effect.name="Fake effect";effect.vendor="NEXUS tests";
        effect.category="Audio Module Class";effect.subcategories="Fx|Tools";
        if(contains(L"desktop")){
            DWORD bytes=0;GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,nullptr,0,&bytes);
            std::vector<wchar_t> text(bytes/sizeof(wchar_t)+1);
            if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,text.data(),bytes,&bytes))return 46;
            effect.vendor=fs::path(text.data()).u8string();
        }
        if(contains(L"budget")){
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            if(!QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&info,sizeof(info),nullptr))return 47;
            effect.version=std::to_string(info.JobMemoryLimit);
        }
        report.classes.push_back(effect);
        auto instrument=effect;instrument.id="10112233445566778899AABBCCDDEEFF";instrument.name="Instrument metadata";instrument.subcategories="Instrument|Synth";
        instrument.effect=true;report.classes.push_back(instrument); // parent must derive policy, not trust this flag
    }
    if(contains(L"duplicate")||contains(L"badutf")){
        nexus::vst3::wire::Writer writer;writer.text("NEXUS-VST3-REPLY");writer.number(1);writer.text(report.nonce);writer.text("");
        writer.number(contains(L"duplicate")?2:1);
        for(int i=0;i<(contains(L"duplicate")?2:1);++i){
            writer.text(report.classes[0].id);
            if(contains(L"badutf")){writer.number(1);writer.data.push_back(255);}else writer.text("Duplicate");
            writer.text("vendor");writer.text("version");writer.text("Audio Module Class");writer.text("Fx");
        }
        nexus::vst3::wire::atomic_write(reply,writer.finish());return 0;
    }
    nexus::vst3::wire::atomic_write(reply,nexus::vst3::wire::encode_report(report));
    return report.error.empty()?0:2;
}
