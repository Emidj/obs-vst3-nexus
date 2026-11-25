#include "vst3/catalog.hpp"
#include "vst3/scan_wire.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
using namespace nexus::vst3;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void write(const fs::path& p,const std::string& bytes="fixture"){std::ofstream out(p,std::ios::binary);out<<bytes;if(!out)throw std::runtime_error("Cannot write fixture");}
std::string desktop(){
    DWORD n=0;GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,nullptr,0,&n);
    std::vector<wchar_t> name(n/sizeof(wchar_t)+1);
    check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,name.data(),n,&n)!=FALSE,"Desktop query");
    return fs::path(name.data()).u8string();
}
bool exited(DWORD pid){
    HANDLE p=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!p)return true;
    const bool dead=WaitForSingleObject(p,2000)==WAIT_OBJECT_0;CloseHandle(p);return dead;
}
DWORD pid_file(const fs::path& module){
    auto file=module;file+=L".pid";DWORD pid=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    do{std::ifstream in(file);in>>pid;if(pid)return pid;Sleep(5);}while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Child PID marker missing");
}
std::wstring quote(const std::wstring& s){
    std::wstring out(1,wchar_t(34));size_t n=0;
    for(auto c:s){if(c==wchar_t(92)){++n;continue;}out.append(c==wchar_t(34)?n*2+1:n,wchar_t(92));n=0;out+=c;}
    out.append(n*2,wchar_t(92));out+=wchar_t(34);return out;
}
struct Child {
    PROCESS_INFORMATION p{};
    ~Child(){if(p.hProcess){if(WaitForSingleObject(p.hProcess,0)!=WAIT_OBJECT_0){TerminateProcess(p.hProcess,88);WaitForSingleObject(p.hProcess,2000);}CloseHandle(p.hProcess);}if(p.hThread)CloseHandle(p.hThread);}
};
}
int wmain(int argc,wchar_t** argv){
    if(argc==5&&std::wstring(argv[1])==L"pending-mode"){
        CatalogScanner scanner(argv[2],argv[3]);scanner.scan(argv[4]);return 0;
    }
    try{
        check(argc==7,"worker, fake worker, normal/crash/hang fixtures, report required");
        const fs::path worker=argv[1],fake=argv[2],original=argv[3],reportPath=argv[6];
        const auto root=fs::absolute(reportPath).parent_path()/("scanner-fixtures-"+wire::random_token());
        fs::create_directories(root);const auto initialDesktop=desktop();size_t checks=0;
        auto expect=[&](bool value,const char* message){check(value,message);++checks;};
        const auto name=fs::u8path(u8"Nexus \u00f1 \u97f3 & spaces.vst3");
        const auto bundle=root/name,binary=bundle/"Contents"/"x86_64-win"/name;
        fs::create_directories(binary.parent_path());fs::copy_file(original,binary);
        expect(GetModuleHandleW(name.c_str())==nullptr,"Fixture already loaded in parent");
        const auto cache=root/"cache-real";CatalogScanner real(worker,cache);
        const auto first=real.scan(bundle);
        expect(first.status==ScanStatus::ready&&first.worker_started&&first.cache_saved&&!first.from_cache,"Real worker scan");
        expect(first.classes.size()==5,"Real class table");
        bool instrument=false;for(const auto& c:first.classes)if(c.name=="Rejected instrument fixture")instrument=!c.effect;
        expect(instrument,"Instrument policy not applied");
        for(const auto& c:first.classes)expect(c.path==fs::weakly_canonical(bundle).u8string(),"Child path was not replaced with parent request");
        auto longCache=root/"long-cache";
        while(longCache.native().size()<225)longCache/="long";
        CatalogScanner longPaths(worker,longCache);
        const auto deep=longPaths.scan(bundle);
        if(deep.status!=ScanStatus::ready)std::cerr<<"Long path scan: "<<scan_status_name(deep.status)<<" "<<deep.detail<<"\n";
        expect(deep.status==ScanStatus::ready&&deep.worker_started&&deep.cache_saved,"Long cache/reply path scan");
        const auto longPacket=longCache/(std::string(120,'p')+".bin");
        const std::vector<uint8_t> packet{0,1,255};
        wire::atomic_write(longPacket,packet);
        expect(wire::read_file(longPacket)==packet,"Long packet path read/write");
        expect(wire::hash_file(longPacket)==wire::hash_bytes(packet),"Long packet path fingerprint");
        const auto deepHit=longPaths.scan(bundle);
        expect(deepHit.status==ScanStatus::ready&&deepHit.from_cache&&!deepHit.worker_started,"Long cache path read");
        const auto hit=real.scan(bundle);expect(hit.from_cache&&!hit.worker_started&&hit.status==ScanStatus::ready,"Success cache");
        expect(real.scan(fs::path(bundle.wstring()+wchar_t(92))).from_cache,"Trailing bundle separator changed cache identity");
        expect(GetModuleHandleW(name.c_str())==nullptr,"Parent loaded VST3 DLL");
        write(bundle/"metadata.test","resource-update");
        expect(real.scan(bundle).worker_started,"Bundle resource update did not invalidate cache");
        const auto missing=real.scan(root/"missing.vst3");
        expect(missing.status==ScanStatus::missing&&!missing.worker_started&&!missing.quarantined,"Missing module policy");
        CatalogScanner absent(root/"missing-worker.exe",root/"absent-cache");
        expect(absent.scan(bundle).status==ScanStatus::helper_error,"Missing helper");
        CatalogScanner inside(worker,bundle/"cache");
        expect(inside.scan(bundle).status==ScanStatus::cache_error&&!fs::exists(bundle/"cache"),"Cache inside plugin bundle was created");
        CatalogScanner adverse(worker,root/"cache-adverse",{1000});
        const auto nativeCrash=adverse.scan(argv[4]);
        expect(nativeCrash.status==ScanStatus::abnormal_exit&&nativeCrash.quarantined&&nativeCrash.worker_started,"Real VST3 factory crash escaped supervisor");
        expect(!adverse.scan(argv[4]).worker_started,"Real crashed VST3 was reloaded");
        const auto nativeHang=adverse.scan(argv[5]);
        expect(nativeHang.status==ScanStatus::timed_out&&nativeHang.quarantined&&nativeHang.worker_started,"Real VST3 factory hang escaped timeout");
        expect(!adverse.scan(argv[5]).worker_started,"Real hanging VST3 was reloaded");
        const auto dummy=root/"dummy";fs::create_directories(dummy);
        auto module=[&](const wchar_t* name){auto file=dummy/name;write(file);return file;};
        const auto badNative=module(L"not-a-native.vst3");
        const auto load=real.scan(badNative);
        expect(load.status==ScanStatus::load_error&&load.quarantined&&load.cache_saved,"Real DLL load failure");
        expect(real.scan(badNative).from_cache,"Real load failure quarantine");
        CatalogScanner synthetic(fake,root/"cache-fake",{1500});
        const auto blockedCache=root/"blocked-cache";write(blockedCache,"owned");
        CatalogScanner blocked(fake,blockedCache);
        expect(blocked.scan(bundle).status==ScanStatus::cache_error&&!blocked.scan(bundle).worker_started,"Invalid cache started worker");
        const auto dll=module(L"excluded.dll");expect(!synthetic.scan(dll).worker_started,"Non-VST3 file was executed");
        const auto privateDesktop=module(L"desktop.vst3");const auto desktopResult=synthetic.scan(privateDesktop);
        expect(desktopResult.status==ScanStatus::ready&&desktopResult.classes[0].vendor.rfind("NexusScan_",0)==0&&desktopResult.classes[0].vendor!=initialDesktop,"Worker desktop was not private");
        const auto budgetModule=module(L"budget.vst3");const auto budget=synthetic.scan(budgetModule);
        expect(budget.status==ScanStatus::ready&&budget.classes[0].version==std::to_string(1024ull*1024*1024),"Worker job memory budget missing");
        const auto ready=module(L"ready.vst3");
        const auto readyResult=synthetic.scan(ready);
        expect(readyResult.status==ScanStatus::ready&&readyResult.classes.size()==2&&readyResult.classes[0].effect&&!readyResult.classes[1].effect,"Synthetic worker protocol");
        const std::pair<const wchar_t*,ScanStatus> failures[]{
            {L"load-error.vst3",ScanStatus::load_error},{L"crash.vst3",ScanStatus::abnormal_exit},
            {L"exit.vst3",ScanStatus::abnormal_exit},{L"invalid.vst3",ScanStatus::invalid_response},
            {L"nonce.vst3",ScanStatus::invalid_response},{L"duplicate.vst3",ScanStatus::invalid_response},
            {L"badutf.vst3",ScanStatus::invalid_response},{L"oversize.vst3",ScanStatus::invalid_response}
        };
        for(const auto& entry:failures){
            const auto file=module(entry.first);const auto result=synthetic.scan(file);
            expect(result.status==entry.second&&result.quarantined&&result.cache_saved,"Failure classification/cache");
            CatalogScanner reopened(fake,root/"cache-fake",{1500});const auto cached=reopened.scan(file);
            expect(cached.from_cache&&!cached.worker_started&&cached.quarantined,"Persisted quarantine relaunched worker");
        }
        const auto slow=module(L"tree-timeout.vst3");const auto timeout=synthetic.scan(slow);
        expect(timeout.status==ScanStatus::timed_out&&timeout.quarantined,"Timeout failed");
        expect(exited(pid_file(slow)),"Timed-out worker descendant survived");
        expect(!synthetic.scan(slow).worker_started,"Timeout cache ignored");
        expect(synthetic.scan(slow,true).worker_started,"Explicit retry ignored");
        const auto orphan=module(L"tree-ready.vst3");expect(synthetic.scan(orphan).status==ScanStatus::ready,"Tree success");
        expect(exited(pid_file(orphan)),"Successful inspection left worker descendant");
        const auto cancelModule=module(L"slow-cancel.vst3");std::atomic<bool> cancel{false};
        std::thread canceller([&]{Sleep(100);cancel=true;});const auto cancelled=synthetic.scan(cancelModule,false,&cancel);canceller.join();
        expect(cancelled.status==ScanStatus::cancelled&&!cancelled.quarantined&&cancelled.worker_started,"Cancellation policy");
        std::atomic<bool> preCancelled{true};
        expect(!synthetic.scan(cancelModule,false,&preCancelled).worker_started,"Pre-cancelled request started worker");
        const auto changing=module(L"change.vst3");auto started=changing;started+=L".started";
        std::thread updater([&]{
            const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!fs::exists(started)&&std::chrono::steady_clock::now()<limit)Sleep(5);
            std::ofstream out(changing,std::ios::app);out<<"updated";
        });
        const auto changed=synthetic.scan(changing);updater.join();
        expect(changed.status==ScanStatus::changed&&!changed.quarantined&&changed.classes.empty(),"Module changed during inspection was cached ready");
        expect(synthetic.scan(changing).status==ScanStatus::ready,"Stable changed module retry");
        // Corrupt cache never causes implicit execution.
        const auto normalized=fs::weakly_canonical(ready).u8string();
        const auto fileKey=wire::hash_bytes(std::vector<uint8_t>(normalized.begin(),normalized.end()));
        const auto cachedFile=root/"cache-fake"/(fileKey+".cache");wire::atomic_write(cachedFile,{0});
        const auto corrupt=synthetic.scan(ready);
        expect(corrupt.status==ScanStatus::cache_error&&!corrupt.worker_started&&wire::read_file(cachedFile).size()==1,"Corrupt cache auto-retried/overwritten");
        expect(synthetic.scan(ready,true).status==ScanStatus::ready,"Explicit cache repair");
        write(ready,"changed-primary");expect(synthetic.scan(ready).worker_started,"Binary update did not invalidate cache");
        const auto workerCopy=root/"fake worker copy.exe";fs::copy_file(fake,workerCopy);
        CatalogScanner versioned(workerCopy,root/"version-cache",{1500});
        expect(versioned.scan(ready).status==ScanStatus::ready&&versioned.scan(ready).from_cache,"Version cache setup");
        {std::ofstream out(workerCopy,std::ios::binary|std::ios::app);out.put('x');}
        expect(versioned.scan(ready).worker_started,"Changed worker hash did not invalidate cache");
        const auto spam=module(L"spam.vst3");expect(synthetic.scan(spam).status==ScanStatus::ready,"Worker console output affected protocol");
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        HANDLE event=CreateEventW(&security,TRUE,FALSE,nullptr);check(event!=nullptr,"Test event");
        const auto handles=module(L"handles.vst3");auto marker=handles;marker+=L".handle";
        {std::ofstream out(marker);out<<uint64_t(reinterpret_cast<uintptr_t>(event));}
        const auto handlesResult=synthetic.scan(handles);const auto leaked=WaitForSingleObject(event,0);CloseHandle(event);
        expect(handlesResult.status==ScanStatus::ready&&leaked==WAIT_TIMEOUT,"Unlisted inheritable handle leaked to worker");
        // A coordinator killed mid-scan leaves a durable interrupted marker;
        // its job must kill the worker even though no C++ cleanup runs.
        const auto pendingModule=module(L"tree-slow-pending.vst3"),pendingCache=root/"pending-cache";
        std::vector<wchar_t> own(32768);const auto ownSize=GetModuleFileNameW(nullptr,own.data(),DWORD(own.size()));
        const auto exe=std::wstring(own.data(),ownSize);
        auto command=quote(exe)+L" pending-mode "+quote(fake.wstring())+L" "+quote(pendingCache.wstring())+L" "+quote(pendingModule.wstring());
        STARTUPINFOW si{};si.cb=sizeof(si);Child coordinator;
        check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&coordinator.p)!=FALSE,"Cannot start coordinator fixture");
        auto pendingPid=pendingModule;pendingPid+=L".pid";
        const auto waitLimit=std::chrono::steady_clock::now()+std::chrono::seconds(4);
        while(!fs::exists(pendingPid)&&std::chrono::steady_clock::now()<waitLimit)Sleep(10);
        const auto descendant=pid_file(pendingModule);
        CatalogScanner whileActive(fake,pendingCache);const auto busy=whileActive.scan(pendingModule);
        expect(busy.status==ScanStatus::busy&&!busy.worker_started&&!busy.quarantined,"Active pending scan was treated as interrupted");
        check(TerminateProcess(coordinator.p.hProcess,87)!=FALSE,"Cannot terminate owned coordinator fixture");
        check(WaitForSingleObject(coordinator.p.hProcess,2000)==WAIT_OBJECT_0,"Coordinator fixture survived");
        expect(exited(descendant),"Coordinator death left descendant");
        CatalogScanner recovered(fake,pendingCache);const auto interrupted=recovered.scan(pendingModule);
        expect(interrupted.status==ScanStatus::interrupted&&interrupted.quarantined&&interrupted.from_cache&&!interrupted.worker_started,"Interrupted marker not durable");
        const auto discovery=root/"discovery";fs::create_directories(discovery/"One.vst3"/"Contents"/"x86_64-win");
        write(discovery/"One.vst3"/"Contents"/"x86_64-win"/"One.vst3");write(discovery/"Two.VST3");write(discovery/"ignored.dll");
        const auto found=discover_vst3({discovery,discovery/"One.vst3",root/"absent-folder"});
        expect(found.modules.size()==2&&!found.truncated&&!found.warnings.empty(),"Discovery duplicated bundle binary or omitted warning");
        DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
        for(int i=0;i<10;++i)expect(synthetic.scan(ready,true).status==ScanStatus::ready,"Repeated forced scan");
        GetProcessHandleCount(GetCurrentProcess(),&after);
        expect(after<=before+2,"Scanner handle leak");
        expect(desktop()==initialDesktop,"Caller desktop changed");
        expect(GetModuleHandleW(name.c_str())==nullptr,"Parent mapped scanned DLL");
        std::ofstream report(reportPath);
        report<<"{\n  \"passed\": true,\n  \"checks\": "<<checks<<",\n  \"parent_loaded_plugin\": false,\n  \"caller_desktop_preserved\": true,\n  \"handles_before\": "<<before<<",\n  \"handles_after\": "<<after
              <<",\n  \"test_directory\": \""<<root.filename().string()<<"\"\n}\n";
        report.flush();check(bool(report),"Report failed");
        std::cout<<"PASS: scanner "<<checks<<" checks, child-process isolation, quarantine, timeout/cancel, durable interruption, Unicode, cache and job cleanup\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
