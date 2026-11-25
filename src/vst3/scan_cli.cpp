#include "vst3/catalog.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
int wmain(int argc,wchar_t** argv){
    try{
        if(argc>=2&&std::wstring(argv[1])==L"discover"){
            std::vector<fs::path> roots;for(int i=2;i<argc;++i)roots.emplace_back(argv[i]);
            const auto found=nexus::vst3::discover_vst3(roots.empty()?nexus::vst3::standard_vst3_roots():roots);
            for(const auto& path:found.modules)std::cout<<path.u8string()<<"\n";
            for(const auto& warning:found.warnings)std::cerr<<warning<<"\n";
            return found.truncated?2:0;
        }
        fs::path cache;bool retry=false;uint32_t timeout=10000;std::vector<fs::path> modules;
        for(int i=1;i<argc;++i){
            const std::wstring arg=argv[i];
            if(arg==L"--cache"&&i+1<argc)cache=argv[++i];
            else if(arg==L"--retry")retry=true;
            else if(arg==L"--timeout-ms"&&i+1<argc){
                size_t used=0;const std::wstring value=argv[++i];const auto n=std::stoul(value,&used);
                if(used!=value.size()||n<50||n>120000)throw std::runtime_error("Invalid timeout");timeout=uint32_t(n);
            }else modules.emplace_back(arg);
        }
        if(cache.empty()||modules.empty()){
            std::cerr<<"Usage: nexus_vst3_scan discover [folder ...]\n"
                <<"       nexus_vst3_scan --cache folder [--retry] [--timeout-ms N] module.vst3 ...\n";return 2;
        }
        std::vector<wchar_t> executable(32768);const DWORD size=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
        if(!size||size>=executable.size())throw std::runtime_error("Cannot locate scanner executable");
        const auto worker=fs::path(std::wstring(executable.data(),size)).parent_path()/L"nexus_vst3_worker.exe";
        nexus::vst3::CatalogScanner scanner(worker,cache,{timeout});
        int exit=0;
        for(const auto& module:modules){
            const auto result=scanner.scan(module,retry);
            std::cout<<nexus::vst3::scan_status_name(result.status)<<" | cached="<<result.from_cache<<" quarantined="<<result.quarantined
                <<" worker="<<result.worker_started<<" | "<<result.path<<"\n";
            for(const auto& c:result.classes)if(c.effect)std::cout<<"  candidate "<<c.id<<" | "<<c.name<<" | "<<c.vendor<<"\n";
            if(!result.detail.empty())std::cout<<"  "<<result.detail<<"\n";
            if(result.status!=nexus::vst3::ScanStatus::ready)exit=1;
        }return exit;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 2;}
}
