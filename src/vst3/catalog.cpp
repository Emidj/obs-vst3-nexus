#include "vst3/catalog.hpp"
#include "vst3/scan_wire.hpp"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <stdexcept>
namespace nexus::vst3 {
namespace {
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
void need(bool v,const char* text){if(!v)throw std::runtime_error(std::string(text)+" (Windows "+std::to_string(GetLastError())+")");}
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
bool reparse(const fs::path& p){
    const DWORD flags=GetFileAttributesW(wire::native_path(p).c_str());need(flags!=INVALID_FILE_ATTRIBUTES,"Cannot query file attributes");
    return (flags&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
}
fs::path normalized(const fs::path& p){
    auto result=fs::weakly_canonical(fs::absolute(p)).lexically_normal();
    if(result.filename().empty()&&result!=result.root_path())result=result.parent_path();return result;
}
bool extension(const fs::path& p){
    auto e=p.extension().wstring();for(auto& c:e)if(c>=L'A'&&c<=L'Z')c=wchar_t(c-L'A'+L'a');return e==L".vst3";
}
bool within(const fs::path& child,const fs::path& parent){
    auto c=child.wstring(),p=parent.wstring();
    if(c.size()<p.size()||CompareStringOrdinal(c.data(),int(p.size()),p.data(),int(p.size()),TRUE)!=CSTR_EQUAL)return false;
    return c.size()==p.size()||c[p.size()]==wchar_t(92)||c[p.size()]==L'/';
}
bool quarantined(ScanStatus status){
    return status==ScanStatus::load_error||status==ScanStatus::abnormal_exit||status==ScanStatus::timed_out||status==ScanStatus::invalid_response||status==ScanStatus::interrupted;
}
std::vector<uint8_t> bytes(const std::string& s){return {s.begin(),s.end()};}
std::string helper_fingerprint(const fs::path& path){
    wire::Writer w;w.text("nexus-vst3-helper-1");w.text(path.u8string(),32768);w.text(wire::hash_file(path));
    return wire::hash_bytes(w.data);
}
std::string fingerprint(const fs::path& module){
    wire::Writer writer;writer.text("nexus-vst3-win64-fingerprint-1");
    const bool bundle=fs::is_directory(module);const auto binary=bundle?module/"Contents"/"x86_64-win"/module.filename():module;
    need(fs::is_regular_file(binary),"VST3 x64 binary missing");
    writer.text(wire::hash_file(binary));
    std::vector<fs::path> files;
    if(bundle){
        size_t entries=0;
        for(fs::recursive_directory_iterator it(module),end;it!=end;++it){
            need(++entries<=16384&&it.depth()<=32,"VST3 bundle enumeration limit exceeded");
            need(!reparse(it->path()),"Linked entries inside VST3 bundle require explicit resolution");
            if(it->is_regular_file())files.push_back(it->path());
        }
    }else files.push_back(binary);
    std::sort(files.begin(),files.end());
    for(const auto& file:files){
        writer.text(bundle?file.lexically_relative(module).generic_u8string():file.filename().u8string(),32768);
        writer.number(fs::file_size(file));writer.number(uint64_t(fs::last_write_time(file).time_since_epoch().count()));
    }
    return wire::hash_bytes(writer.data);
}
std::wstring quote(const std::wstring& value){
    std::wstring result(1,wchar_t(34));size_t slashes=0;
    for(wchar_t c:value){
        if(c==wchar_t(92)){++slashes;continue;}
        result.append(c==wchar_t(34)?slashes*2+1:slashes,wchar_t(92));slashes=0;result+=c;
    }
    result.append(slashes*2,wchar_t(92));result+=wchar_t(34);return result;
}
struct RunDirectory {
    fs::path folder,reply;
    explicit RunDirectory(const fs::path& cache,const std::string& nonce){
        fs::create_directories(cache);auto runs=cache/"runs";
        folder=runs/("scan-"+nonce);
        // Windows process creation still rejects an overlong current directory.
        // Keep durable cache at its requested location, but use a private short
        // temporary run folder when necessary. Never reuse a shared working dir.
        if(folder.native().size()>=240){
            runs=normalized(fs::temp_directory_path());folder=runs/("nexus-vst3-scan-"+nonce);
        }
        need(folder.native().size()<240,"Scan working directory too long; choose a shorter temporary directory");
        fs::create_directories(runs);
        need(!reparse(runs),"Scan runs directory must not be a link");
        need(CreateDirectoryW(wire::native_path(folder).c_str(),nullptr)!=FALSE,"Cannot create scan run directory");
        reply=folder/"reply.bin";
    }
    ~RunDirectory(){
        if(folder.empty())return;
        const auto attrs=GetFileAttributesW(wire::native_path(folder).c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES)return;
        // Never recursively remove a directory written to by foreign code.
        if(!(attrs&FILE_ATTRIBUTE_REPARSE_POINT))DeleteFileW(wire::native_path(reply).c_str());
        RemoveDirectoryW(wire::native_path(folder).c_str()); // extra files are retained, not traversed/deleted
    }
};
struct Desktop {
    HDESK value=nullptr;
    explicit Desktop(const std::wstring& name){
        const auto previous=GetThreadDesktop(GetCurrentThreadId());
        value=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_SWITCHDESKTOP,nullptr);
        need(value!=nullptr,"Cannot create private scan desktop");
        if(!SetThreadDesktop(previous)){CloseDesktop(value);value=nullptr;need(false,"Cannot preserve caller desktop");}
    }
    ~Desktop(){if(value)CloseDesktop(value);}
};
struct ProcessGroup {
    Handle job,process,thread;bool stopped=false;
    bool stop()noexcept{
        if(stopped)return true;stopped=true;
        if(!job.value)return true;
        TerminateJobObject(job.value,0xE0000001);
        const auto deadline=Clock::now()+std::chrono::seconds(2);
        do{
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if(QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)&&accounting.ActiveProcesses==0)return true;
            Sleep(5);
        }while(Clock::now()<deadline);
        return false;
    }
    ~ProcessGroup(){stop();}
};
struct Attributes {
    std::vector<uint8_t> storage;LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    Attributes(){
        SIZE_T count=0;InitializeProcThreadAttributeList(nullptr,2,0,&count);need(count>0,"Cannot size scan process attributes");
        storage.resize(count);auto* p=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        need(InitializeProcThreadAttributeList(p,2,0,&count)!=FALSE,"Cannot create scan process attributes");list=p;
    }
    ~Attributes(){if(list)DeleteProcThreadAttributeList(list);}
};
struct WorkerExit {ScanStatus status=ScanStatus::helper_error;DWORD code=0;bool started=false;std::string detail;};
WorkerExit run_worker(const fs::path& worker,const fs::path& module,const RunDirectory& run,
                      const std::string& nonce,const ScanOptions& options,const std::atomic<bool>* cancel,bool& started){
    auto desktopName=L"NexusScan_"+std::wstring(nonce.begin(),nonce.end());
    Desktop desktop(desktopName);ProcessGroup group;
    group.job.value=CreateJobObjectW(nullptr,nullptr);need(group.job.value!=nullptr,"Cannot create scan job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION|
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS|JOB_OBJECT_LIMIT_JOB_MEMORY|JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    limits.BasicLimitInformation.ActiveProcessLimit=16;limits.BasicLimitInformation.PriorityClass=BELOW_NORMAL_PRIORITY_CLASS;
    limits.JobMemoryLimit=SIZE_T(options.memory_limit_bytes);
    need(SetInformationJobObject(group.job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Cannot set scan job limits");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle null;null.value=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    need(null.value!=INVALID_HANDLE_VALUE,"Cannot redirect scan handles");
    Attributes attributes;HANDLE allowed[]{null.value},jobs[]{group.job.value};
    need(UpdateProcThreadAttribute(attributes.list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),nullptr,nullptr)!=FALSE,"Cannot whitelist scan handles");
    need(UpdateProcThreadAttribute(attributes.list,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,jobs,sizeof(jobs),nullptr,nullptr)!=FALSE,"Cannot associate scan job at process creation");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes.list;
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=null.value;
    startup.StartupInfo.lpDesktop=desktopName.data();
    auto command=quote(worker.wstring())+L" inspect-worker "+quote(module.wstring())+L" "+quote(run.reply.wstring())+L" "+quote(std::wstring(nonce.begin(),nonce.end()));
    PROCESS_INFORMATION process{};
    need(CreateProcessW(wire::native_path(worker).c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT|BELOW_NORMAL_PRIORITY_CLASS,
        nullptr,wire::native_path(run.folder).c_str(),&startup.StartupInfo,&process)!=FALSE,"Cannot create scan worker");
    group.process.value=process.hProcess;group.thread.value=process.hThread;
    need(ResumeThread(group.thread.value)!=DWORD(-1),"Cannot resume scan worker");
    WorkerExit result;result.started=started=true;const auto deadline=Clock::now()+std::chrono::milliseconds(options.timeout_ms);
    for(;;){
        if(cancel&&cancel->load(std::memory_order_relaxed)){result.status=ScanStatus::cancelled;result.detail="Scan cancelled";break;}
        if(Clock::now()>=deadline){result.status=ScanStatus::timed_out;result.detail="Worker exceeded time limit";break;}
        const DWORD wait=WaitForSingleObject(group.process.value,20);
        if(wait==WAIT_OBJECT_0){
            need(GetExitCodeProcess(group.process.value,&result.code)!=FALSE,"Cannot read worker exit code");
            result.status=result.code==0?ScanStatus::ready:(result.code==2?ScanStatus::load_error:ScanStatus::abnormal_exit);break;
        }
        need(wait==WAIT_TIMEOUT,"Worker wait failed");
    }
    if(!group.stop()){result.status=ScanStatus::helper_error;result.detail="Cannot confirm scan job cleanup";}
    return result;
}
std::vector<uint8_t> encode_cache(const ScanResult& r,const std::string& helper){
    wire::Writer w;w.text("NEXUS-VST3-CACHE");w.number(1);w.text(r.path,32768);w.text(r.fingerprint,64);w.text(helper,64);
    w.number(uint32_t(r.status));w.number(r.exit_code);w.text(r.detail,16384);w.classes(r.classes);return w.finish();
}
ScanResult decode_cache(const std::vector<uint8_t>& bytes,const std::string& helper,const std::string& path,const std::string& fingerprint,bool& matches){
    wire::Reader r(bytes);need(r.text()=="NEXUS-VST3-CACHE"&&r.number()==1,"Unsupported scan cache");
    ScanResult result;result.path=r.text(32768);result.fingerprint=r.text(64);const auto previousHelper=r.text(64);
    const auto status=r.number(),exit=r.number();need(status<=uint32_t(ScanStatus::interrupted)&&exit<=MAXDWORD,"Invalid cached scan status");
    result.status=ScanStatus(status);result.exit_code=DWORD(exit);result.detail=r.text(16384);result.classes=r.classes();r.end();
    need(result.status==ScanStatus::ready||result.classes.empty(),"Failed cache contains classes");
    matches=(result.status==ScanStatus::ready||quarantined(result.status))&&result.path==path&&result.fingerprint==fingerprint&&previousHelper==helper;
    for(auto& c:result.classes)c.path=path;
    result.from_cache=true;result.quarantined=quarantined(result.status);result.cache_saved=true;return result;
}
}
const char* scan_status_name(ScanStatus status)noexcept{
    switch(status){
    case ScanStatus::ready:return "ready";case ScanStatus::load_error:return "load_error";
    case ScanStatus::abnormal_exit:return "abnormal_exit";case ScanStatus::timed_out:return "timed_out";
    case ScanStatus::invalid_response:return "invalid_response";case ScanStatus::missing:return "missing";
    case ScanStatus::changed:return "changed";case ScanStatus::helper_error:return "helper_error";
    case ScanStatus::cache_error:return "cache_error";case ScanStatus::cancelled:return "cancelled";case ScanStatus::interrupted:return "interrupted";case ScanStatus::busy:return "busy";
    }return "unknown";
}
CatalogScanner::CatalogScanner(fs::path worker,fs::path cache,ScanOptions options):
    worker_(normalized(worker)),cache_(normalized(cache)),options_(options){
    if(worker.empty()||cache.empty())throw std::invalid_argument("Worker and cache paths must be explicit");
    if(options.timeout_ms<50||options.timeout_ms>120000||options.memory_limit_bytes<64ull*1024*1024||options.memory_limit_bytes>8ull*1024*1024*1024)
        throw std::invalid_argument("Invalid scan budgets");
}
ScanResult CatalogScanner::scan(const fs::path& input,bool retry,const std::atomic<bool>* cancel){
    std::lock_guard<std::mutex> lock(serial_);const auto begin=Clock::now();ScanResult result;
    bool pending=false;fs::path cacheFile;std::string helper;Handle cacheLock;
    auto finish=[&]{result.elapsed_ms=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count());return result;};
    try{
        if(cancel&&cancel->load()){result.status=ScanStatus::cancelled;return finish();}
        const auto module=normalized(input);result.path=module.u8string();
        if(!extension(module)||!fs::exists(module)){result.status=ScanStatus::missing;result.detail="VST3 module unavailable";return finish();}
        if(fs::is_directory(module)&&within(cache_,module)){result.status=ScanStatus::cache_error;result.detail="Cache must be outside VST3 bundle";return finish();}
        if(!fs::is_regular_file(worker_)){result.status=ScanStatus::helper_error;result.detail="Scan worker unavailable";return finish();}
        result.fingerprint=fingerprint(module);helper=helper_fingerprint(worker_);
        if(fs::exists(cache_)&&!fs::is_directory(cache_)){result.status=ScanStatus::cache_error;result.detail="Cache path is not a directory";return finish();}
        cacheFile=cache_/(wire::hash_bytes(bytes(result.path))+".cache");
        try{fs::create_directories(cache_);}catch(...){result.status=ScanStatus::cache_error;result.detail="Cannot create cache directory";return finish();}
        auto lockFile=cacheFile;lockFile+=L".lock";
        const auto attributes=GetFileAttributesW(wire::native_path(lockFile).c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){
            result.status=ScanStatus::cache_error;result.detail="Cache lock must not be a link";return finish();
        }
        cacheLock.value=CreateFileW(wire::native_path(lockFile).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(cacheLock.value==INVALID_HANDLE_VALUE){
            const auto error=GetLastError();
            result.status=(error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION)?ScanStatus::busy:ScanStatus::cache_error;
            result.detail=result.status==ScanStatus::busy?"Another coordinator is inspecting this module":"Cannot lock cache entry";
            return finish();
        }
        if(!retry&&fs::exists(fs::path(wire::native_path(cacheFile)))){
            try{
                bool matches=false;auto saved=decode_cache(wire::read_file(cacheFile),helper,result.path,result.fingerprint,matches);
                if(matches){result=std::move(saved);return finish();}
            }catch(const std::exception&){result.status=ScanStatus::cache_error;result.detail="Cache is invalid; explicit retry required";return finish();}
        }
        if(cancel&&cancel->load()){result.status=ScanStatus::cancelled;return finish();}
        try{
            fs::create_directories(cache_);
            auto marker=result;marker.status=ScanStatus::interrupted;marker.detail="Previous inspection did not complete; explicit retry required";
            wire::atomic_write(cacheFile,encode_cache(marker,helper));pending=true;
        }catch(const std::exception& e){result.status=ScanStatus::cache_error;result.detail=std::string("Cannot persist scan marker; worker not started: ")+e.what();return finish();}
        const auto nonce=wire::random_token();RunDirectory run(cache_,nonce);
        auto exit=run_worker(worker_,module,run,nonce,options_,cancel,result.worker_started);
        result.status=exit.status;result.exit_code=exit.code;result.detail=exit.detail;
        if(result.status==ScanStatus::ready||result.status==ScanStatus::load_error){
            try{
                const auto response=wire::decode_report(wire::read_file(run.reply));
                need(response.nonce==nonce,"Stale scan response");
                if(result.status==ScanStatus::ready&&response.error.empty())result.classes=response.classes;
                else if(result.status==ScanStatus::load_error&&!response.error.empty())result.detail=response.error;
                else throw std::runtime_error("Worker status/response mismatch");
            }catch(const std::exception&){result.status=ScanStatus::invalid_response;result.detail="Invalid or missing worker response";}
        }
        if(result.status==ScanStatus::ready||quarantined(result.status)){
            if(!fs::exists(module)||fingerprint(module)!=result.fingerprint||helper_fingerprint(worker_)!=helper){
                result.status=ScanStatus::changed;result.detail="Module or worker changed during inspection";result.classes.clear();
            }
        }
        result.quarantined=quarantined(result.status);
        for(auto& c:result.classes)c.path=result.path;
    }catch(const std::exception& e){
        result.status=ScanStatus::helper_error;result.detail=e.what();result.classes.clear();result.quarantined=false;
    }
    if(pending){
        try{wire::atomic_write(cacheFile,encode_cache(result,helper));result.cache_saved=true;}
        catch(const std::exception&){result.detail+=" Cache could not be saved; interrupted marker retained.";}
    }
    return finish();
}
std::vector<fs::path> standard_vst3_roots(){
    std::vector<fs::path> roots;
    for(const auto* id:{&FOLDERID_UserProgramFilesCommon,&FOLDERID_ProgramFilesCommon}){
        PWSTR text=nullptr;if(SHGetKnownFolderPath(*id,0,nullptr,&text)==S_OK){roots.push_back(fs::path(text)/"VST3");CoTaskMemFree(text);}
    }
    std::vector<wchar_t> executable(32768);const DWORD length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if(length&&length<executable.size())roots.push_back(fs::path(std::wstring(executable.data(),length)).parent_path()/"VST3");
    return roots;
}
Discovery discover_vst3(const std::vector<fs::path>& roots){
    Discovery result;std::set<fs::path> seen;size_t count=0;
    auto warning=[&](const std::string& text){if(result.warnings.size()<256)result.warnings.push_back(text);else result.truncated=true;};
    std::vector<std::pair<fs::path,size_t>> pending;
    for(const auto& root:roots)pending.push_back({root,0});
    while(!pending.empty()&&!result.truncated){
        auto current=std::move(pending.back());pending.pop_back();
        try{
            const auto path=normalized(current.first);
            if(!fs::exists(path)){warning("Root unavailable: "+path.u8string());continue;}
            if(extension(path)){
                if(seen.insert(path).second)result.modules.push_back(path);
                if(result.modules.size()>=4096)result.truncated=true;
                continue;
            }
            if(!fs::is_directory(path)){warning("Not a scan folder: "+path.u8string());continue;}
            if(current.second>32){warning("Discovery depth limit");continue;}
            std::error_code error;fs::directory_iterator it(path,error),end;
            if(error){warning("Folder unavailable: "+path.u8string());continue;}
            while(it!=end){
                if(++count>100000){result.truncated=true;break;}
                const auto entry=it->path();
                if(extension(entry)||(!reparse(entry)&&it->is_directory()))pending.push_back({entry,current.second+1});
                it.increment(error);if(error){warning("Folder enumeration incomplete: "+path.u8string());break;}
            }
        }catch(const std::exception&){warning("Discovery entry unavailable");}
    }
    std::sort(result.modules.begin(),result.modules.end());return result;
}
}
