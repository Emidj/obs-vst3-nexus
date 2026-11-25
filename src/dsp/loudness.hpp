#pragma once
#include "dsp/engine.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
namespace nexus {
static_assert(std::atomic<unsigned>::is_always_lock_free,"Meter flags require lock-free atomics");
struct LoudnessReading {
    float momentary=-120,short_term=-120,integrated=-120,sample_peak=-120;
    bool momentary_ready=false,short_term_ready=false,integrated_ready=false,active=false;
};
// One audio consumer. Construct/destroy with audio stopped; read() is UI-safe.
// Independent telemetry atomics; no audio modification or buffering latency.
class LoudnessMeter final {
public:
    explicit LoudnessMeter(const Format&);
    ~LoudnessMeter();
    LoudnessMeter(const LoudnessMeter&)=delete;
    LoudnessMeter& operator=(const LoudnessMeter&)=delete;
    bool process(Block)noexcept;
    LoudnessReading read()const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
    Format format_;
    std::vector<float> interleaved_;
    uint64_t frames_=0;
    size_t since_=0,interval_=0;
    double peak_=0;
    std::array<std::atomic<float>,4> values_;
    std::atomic<unsigned> ready_{0};
};
}
