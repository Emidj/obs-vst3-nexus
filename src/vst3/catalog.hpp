#pragma once
#include "vst3/types.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>
namespace nexus::vst3 {
enum class ScanStatus:uint32_t {ready,load_error,abnormal_exit,timed_out,invalid_response,missing,changed,helper_error,cache_error,cancelled,interrupted,busy};
const char* scan_status_name(ScanStatus)noexcept;
struct ScanOptions {
    uint32_t timeout_ms=10000;
    uint64_t memory_limit_bytes=1024ull*1024*1024;
};
struct ScanResult {
    ScanStatus status=ScanStatus::helper_error;
    std::string path,fingerprint,detail;
    std::vector<ClassInfo> classes;
    uint32_t exit_code=0;
    uint64_t elapsed_ms=0;
    bool from_cache=false,worker_started=false,quarantined=false,cache_saved=false;
};
struct Discovery {std::vector<std::filesystem::path> modules;std::vector<std::string> warnings;bool truncated=false;};
std::vector<std::filesystem::path> standard_vst3_roots();
Discovery discover_vst3(const std::vector<std::filesystem::path>& roots);
// Blocking background/control operation; never call from audio or UI event handling.
// No SDK or plug-in loading in this library. One scan at a time per coordinator.
// Cache/results are local metadata, not a security boundary for malicious code.
class CatalogScanner {
public:
    CatalogScanner(std::filesystem::path worker,std::filesystem::path cache,ScanOptions options={});
    ScanResult scan(const std::filesystem::path& module,bool retry=false,const std::atomic<bool>* cancel=nullptr);
private:
    std::filesystem::path worker_,cache_;
    ScanOptions options_;std::mutex serial_;
};
}
