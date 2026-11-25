#include "bench/audio_file.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace nexus::bench;
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
template<class F> void rejected(F action, const char* text) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, text);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto dir = std::filesystem::path(argv[1]) /
        ("wav-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    try {
        // Independently specified PCM16 fixture: -1, 0, 0.5, 32767/32768.
        const unsigned char pcm[]{
            'R','I','F','F',44,0,0,0,'W','A','V','E','f','m','t',' ',
            16,0,0,0,1,0,1,0,128,187,0,0,0,119,1,0,2,0,16,0,
            'd','a','t','a',8,0,0,0,0,128,0,0,0,64,255,127};
        const auto input = (dir / "known.wav").string();
        { std::ofstream f(input, std::ios::binary); f.write(reinterpret_cast<const char*>(pcm), sizeof(pcm)); }
        auto decoded = read_wav(input);
        check(decoded.rate == 48000 && decoded.channels.size() == 1 && decoded.frames() == 4, "PCM format");
        check(decoded.channels[0] == std::vector<float>({-1.f, 0.f, .5f, 32767.f/32768}), "PCM known amplitudes");
        const auto output = (dir / "float.wav").string();
        write_wav(output, decoded);
        check(read_wav(output).channels == decoded.channels, "Float exact roundtrip");
        // Accept an unknown odd-sized chunk with valid padding.
        std::vector<unsigned char> padded(pcm, pcm + sizeof(pcm));
        const unsigned char junk[]{'J','U','N','K',1,0,0,0,42,0};
        padded.insert(padded.begin() + 12, junk, junk + sizeof(junk));
        padded[4] = 54;
        { std::ofstream f(input, std::ios::binary); f.write(reinterpret_cast<const char*>(padded.data()), static_cast<std::streamsize>(padded.size())); }
        check(read_wav(input).channels == decoded.channels, "Unknown padded chunk");
        { std::ofstream f(input, std::ios::binary); f.write(reinterpret_cast<const char*>(pcm), sizeof(pcm)-1); }
        rejected([&]{ read_wav(input); }, "Truncated file accepted");
        auto badAlign = std::vector<unsigned char>(pcm, pcm + sizeof(pcm));
        badAlign[32] = 4;
        { std::ofstream f(input, std::ios::binary); f.write(reinterpret_cast<const char*>(badAlign.data()), static_cast<std::streamsize>(badAlign.size())); }
        rejected([&]{ read_wav(input); }, "Invalid alignment accepted");
        decoded.channels[0][0] = std::numeric_limits<float>::quiet_NaN();
        rejected([&]{ write_wav(output, decoded); }, "Non-finite samples accepted");
        decoded.channels.push_back({0});
        rejected([&]{ write_wav(output, decoded); }, "Unequal channels accepted");
        for (uint32_t rate : {44100u,48000u,96000u}) {
            auto tone = generate("tone", rate, 2, 1.);
            double square = 0;
            for (float x : tone.channels[0]) square += double(x)*x;
            check(std::abs(std::sqrt(square/rate) - std::sqrt(.125)) < 1e-6, "Known tone RMS");
            check(tone.channels[0] == tone.channels[1], "Generator channel mismatch");
            auto impulse = generate("impulse", rate, 1, .01);
            check(impulse.channels[0][0] == .5f, "Impulse amplitude");
            for (size_t i = 1; i < impulse.frames(); ++i) check(impulse.channels[0][i] == 0, "Impulse tail");
            auto sweep = generate("sweep", rate, 1, .1);
            for (float x : sweep.channels[0]) check(std::isfinite(x) && std::abs(x) <= .5f, "Sweep bounds");
        }
        rejected([&]{ generate("bad",48000,1,1); }, "Invalid generator accepted");
        rejected([&]{ generate("tone",48000,0,1); }, "Invalid channels accepted");
        rejected([&]{ generate("tone",48000,1,0); }, "Invalid duration accepted");
        std::filesystem::remove_all(dir);
        std::cout << "PASS: known PCM16, float WAV, padded chunks, malformed inputs and signal reference checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove_all(dir);
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}