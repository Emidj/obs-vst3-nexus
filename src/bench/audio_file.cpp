#include "bench/audio_file.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
namespace nexus::bench {
namespace {
constexpr size_t limit = 64 * 1024 * 1024;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void put16(std::ostream& f, uint16_t v) {
    char b[]{char(v & 255), char(v >> 8)}; f.write(b, 2);
}
void put32(std::ostream& f, uint32_t v) {
    char b[]{char(v & 255), char((v >> 8) & 255), char((v >> 16) & 255), char(v >> 24)};
    f.write(b, 4);
}
void validate(const AudioFile& a) {
    require(a.rate >= 8000 && a.rate <= 192000, "Unsupported sample rate");
    require(!a.channels.empty() && a.channels.size() <= 8, "Unsupported channels");
    require(a.frames() <= limit / (4 * a.channels.size()), "Audio exceeds internal 64 MiB limit");
    for (const auto& channel : a.channels) {
        require(channel.size() == a.frames(), "Unequal channel lengths");
        for (float x : channel) require(std::isfinite(x), "Non-finite sample");
    }
}
}
AudioFile read_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    require(bool(f), "Cannot open input WAV");
    const auto length = f.tellg();
    require(length >= 12 && length <= std::streamoff(limit + 4096), "Invalid WAV size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    f.seekg(0); f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(f), "Truncated WAV");
    require(std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
            std::memcmp(bytes.data() + 8, "WAVE", 4) == 0, "Expected RIFF WAVE");
    const uint64_t end64 = uint64_t(u32(bytes.data() + 4)) + 8;
    require(end64 == bytes.size(), "RIFF size mismatch");
    const size_t end = bytes.size();
    size_t dataOffset = 0, dataSize = 0;
    uint16_t format = 0, channels = 0, bits = 0, align = 0;
    uint32_t rate = 0, byteRate = 0;
    bool haveFormat = false, haveData = false;
    for (size_t pos = 12; pos < end;) {
        require(end - pos >= 8, "Truncated chunk header");
        const uint32_t size = u32(bytes.data() + pos + 4);
        const size_t start = pos + 8;
        require(size <= end - start, "Truncated chunk");
        if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0) {
            require(!haveFormat && size >= 16, "Invalid format chunk");
            const auto* p = bytes.data() + start;
            format = u16(p); channels = u16(p + 2); rate = u32(p + 4);
            byteRate = u32(p + 8); align = u16(p + 12); bits = u16(p + 14);
            haveFormat = true;
        } else if (std::memcmp(bytes.data() + pos, "data", 4) == 0) {
            require(!haveData, "Multiple data chunks unsupported");
            dataOffset = start; dataSize = size; haveData = true;
        }
        const size_t padded = size_t(size) + (size & 1u);
        require(padded <= end - start, "Missing chunk padding");
        pos = start + padded;
    }
    require(haveFormat && haveData, "Missing format or data");
    require((format == 1 && bits == 16) || (format == 3 && bits == 32),
            "Supported WAV encodings: PCM16 and IEEE float32 (not extensible)");
    require(channels > 0 && channels <= 8 && rate >= 8000 && rate <= 192000, "Unsupported format");
    require(align == channels * (bits / 8) && byteRate == uint64_t(rate) * align &&
            dataSize % align == 0, "Invalid WAV alignment");
    const size_t frames = dataSize / align;
    require(frames <= limit / (4 * channels), "Decoded audio exceeds limit");
    AudioFile audio{rate, std::vector<std::vector<float>>(channels, std::vector<float>(frames))};
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < channels; ++c) {
            const auto* p = bytes.data() + dataOffset + i * align + c * (bits / 8);
            float value;
            if (format == 1) {
                const int raw = int(u16(p));
                value = float(raw >= 32768 ? raw - 65536 : raw) / 32768.f;
            } else {
                const uint32_t raw = u32(p);
                static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
                std::memcpy(&value, &raw, 4);
            }
            require(std::isfinite(value), "Non-finite sample");
            audio.channels[c][i] = value;
        }
    return audio;
}
void write_wav(const std::string& path, const AudioFile& audio) {
    validate(audio);
    const auto channels = static_cast<uint16_t>(audio.channels.size());
    const auto dataSize = static_cast<uint32_t>(audio.frames() * channels * 4);
    std::ofstream f(path, std::ios::binary);
    require(bool(f), "Cannot open output WAV");
    f.write("RIFF", 4); put32(f, 50 + dataSize); f.write("WAVEfmt ", 8);
    put32(f, 18); put16(f, 3); put16(f, channels); put32(f, audio.rate);
    put32(f, audio.rate * channels * 4); put16(f, channels * 4); put16(f, 32); put16(f, 0);
    f.write("fact", 4); put32(f, 4); put32(f, static_cast<uint32_t>(audio.frames()));
    f.write("data", 4); put32(f, dataSize);
    for (size_t i = 0; i < audio.frames(); ++i)
        for (const auto& channel : audio.channels) {
            uint32_t raw; std::memcpy(&raw, &channel[i], 4); put32(f, raw);
        }
    f.flush(); require(bool(f), "Output WAV write failed");
}
AudioFile generate(const std::string& kind, uint32_t rate, size_t channels, double seconds) {
    require(kind == "tone" || kind == "impulse" || kind == "sweep", "Unknown generator");
    require(rate >= 8000 && rate <= 192000 && channels > 0 && channels <= 8, "Unsupported generator format");
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 60, "Duration must be (0,60] seconds");
    const auto frames = static_cast<size_t>(double(rate) * seconds);
    require(frames > 0 && frames <= limit / (4 * channels), "Generator exceeds size limit");
    AudioFile a{rate, std::vector<std::vector<float>>(channels, std::vector<float>(frames))};
    constexpr double pi = 3.14159265358979323846;
    const double upper = (std::min)(20000., double(rate) * .45);
    const double slope = (upper - 20.) / seconds;
    for (size_t i = 0; i < frames; ++i) {
        const double t = double(i) / rate;
        const float value = kind == "impulse" ? (i == 0 ? .5f : 0.f) :
            float(.5 * std::sin(2 * pi * (kind == "tone" ? 1000 * t : 20 * t + .5 * slope * t * t)));
        for (auto& channel : a.channels) channel[i] = value;
    }
    return a;
}
}