#include "dsp/engine.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace nexus;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class Scale final : public Processor {
public:
    explicit Scale(float factor) : factor_(factor) {}
    void prepare(const Format&) override {}
    void reset() noexcept override {}
    void process(Block b) noexcept override {
        for (std::size_t c = 0; c < b.channels; ++c)
            for (std::size_t i = 0; i < b.frames; ++i) b.planes[c][i] *= factor_;
    }
private:
    float factor_;
};
int main() {
    try {
        int cases = 0;
        for (double rate : {44100., 48000., 96000.})
            for (std::size_t channels : {1u, 2u, 8u})
                for (std::size_t frames : {0u, 1u, 17u, 256u, 1024u}) {
                    Engine engine;
                    engine.prepare({rate, channels, 1024});
                    std::vector<std::vector<float>> samples(channels, std::vector<float>(1024));
                    std::vector<float*> planes;
                    for (std::size_t c = 0; c < channels; ++c) {
                        for (std::size_t i = 0; i < 1024; ++i)
                            samples[c][i] = static_cast<float>(std::sin(double(i + c)) * 0.5);
                        planes.push_back(samples[c].data());
                    }
                    const auto original = samples;
                    check(engine.process({planes.data(), channels, frames}), "Valid block rejected");
                    check(samples == original, "Transparent path changed samples");
                    check(engine.latency_frames() == 0, "Empty rack latency");
                    ++cases;
                }
        Engine a, b;
        a.add(std::make_unique<Scale>(2.f));
        a.add(std::make_unique<Scale>(3.f));
        a.prepare({48000, 1, 4});
        b.prepare({48000, 1, 4});
        float samples[] = {1, -1, .5f, 0};
        float* planes[] = {samples};
        check(a.process({planes, 1, 4}) && samples[0] == 6, "Module chain failed");
        check(b.process({planes, 1, 4}) && samples[0] == 6, "Instances share processing");
        a.set_bypass(true);
        // Bypass now ramps over 5 ms; feed a stable signal until it settles.
        for (int block = 0; block < 60; ++block) {
            for (auto& value : samples) value = 1.f;
            check(a.process({planes, 1, 4}), "Bypass block rejected");
        }
        check(samples[3] == 1.f, "Smoothed bypass failed to settle");
        check(!a.process({planes, 1, 5}), "Oversized block accepted");
        check(!a.process({nullptr, 1, 1}), "Null planes accepted");
        float* missing[] = {nullptr};
        check(!a.process({missing, 1, 1}), "Null channel accepted");
        check(!a.process({planes, 2, 1}), "Wrong channels accepted");
        Engine unprepared;
        check(!unprepared.process({planes, 1, 4}), "Unprepared engine accepted");
        bool invalid = false;
        try { unprepared.prepare({0, 1, 4}); } catch (const std::invalid_argument&) { invalid = true; }
        check(invalid, "Invalid format accepted");
        bool edit = false;
        try { a.add(std::make_unique<Scale>(1.f)); } catch (const std::logic_error&) { edit = true; }
        check(edit, "Unsafe graph mutation accepted");
        std::cout << "PASS: " << cases << " transparent format/block cases; chain, isolation, bypass and invalid input checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}