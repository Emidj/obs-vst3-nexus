#pragma once
#include "dsp/parameter.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>
namespace nexus {
struct Format { double sample_rate = 48000; std::size_t channels = 2; std::size_t max_frames = 1024; };
struct Block { float* const* planes; std::size_t channels; std::size_t frames; };
class Processor {
public:
    virtual ~Processor() = default;
    virtual void prepare(const Format&) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(Block) noexcept = 0;
    virtual std::size_t latency_frames() const noexcept { return 0; }
    virtual float gain_reduction_db() const noexcept { return 0; }
};
// Structural changes, prepare/reset and destruction require stopped audio.
// Parameters may be published through SmoothedParameter from a control thread.
// Live graph replacement belongs to phase 6. Dynamic latency requires reprepare.
class Engine {
public:
    void add(std::unique_ptr<Processor>);
    void prepare(const Format&);
    void reset() noexcept;
    bool process(Block) noexcept;
    void set_bypass(bool value) noexcept { bypass_mix_.set_target(value ? 1.f : 0.f); }
    std::size_t latency_frames() const noexcept { return latency_; }
private:
    Format format_{};
    bool prepared_ = false;
    std::vector<std::unique_ptr<Processor>> modules_;
    SmoothedParameter bypass_mix_;
    std::vector<float> dry_, delay_;
    std::size_t latency_ = 0, delay_pos_ = 0;
};
}