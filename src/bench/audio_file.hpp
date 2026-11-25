#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace nexus::bench {
struct AudioFile {
    uint32_t rate = 48000;
    std::vector<std::vector<float>> channels;
    size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
};
AudioFile read_wav(const std::string& path);
void write_wav(const std::string& path, const AudioFile& audio);
AudioFile generate(const std::string& kind, uint32_t rate, size_t channels, double seconds);
}