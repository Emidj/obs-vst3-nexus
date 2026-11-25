#pragma once
#include "vst3/types.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>
namespace nexus::vst3::wire {
constexpr size_t max_packet=4*1024*1024;
struct Report {std::string nonce,error;std::vector<ClassInfo> classes;};
std::string random_token();
std::wstring native_path(const std::filesystem::path&);
std::string hash_file(const std::filesystem::path&,uint64_t limit=1024ull*1024*1024);
std::string hash_bytes(const std::vector<uint8_t>&);
std::vector<uint8_t> read_file(const std::filesystem::path&,size_t maximum=max_packet);
void atomic_write(const std::filesystem::path&,const std::vector<uint8_t>&);
bool valid_text(const std::string&);
std::vector<uint8_t> encode_report(const Report&);
Report decode_report(const std::vector<uint8_t>&);
class Writer {
public:
    std::vector<uint8_t> data;
    void number(uint64_t value);
    void text(const std::string&,size_t maximum=4096);
    void classes(const std::vector<ClassInfo>&);
    std::vector<uint8_t> finish();
};
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>&);
    uint64_t number();
    std::string text(size_t maximum=4096);
    std::vector<ClassInfo> classes();
    void end()const;
private:
    const std::vector<uint8_t>& data_;size_t position_=0,limit_=0;
};
}
