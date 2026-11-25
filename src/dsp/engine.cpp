#include "dsp/engine.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nexus {
void Engine::add(std::unique_ptr<Processor> module) {
    if (prepared_) throw std::logic_error("Graph edits require a stopped engine");
    if (!module) throw std::invalid_argument("Null processor");
    modules_.push_back(std::move(module));
}
void Engine::prepare(const Format& format) {
    prepared_ = false;
    if (!std::isfinite(format.sample_rate) || format.sample_rate < 8000 || format.sample_rate > 192000 ||
        format.channels == 0 || format.channels > 8 || format.max_frames == 0 || format.max_frames > 65536)
        throw std::invalid_argument("Invalid audio format");
    format_ = format;
    latency_ = 0;
    const auto budget = static_cast<std::size_t>(format.sample_rate * 2.);
    for (auto& module : modules_) {
        module->prepare(format);
        const auto delay = module->latency_frames();
        if (delay > budget - latency_) throw std::invalid_argument("Initial core latency exceeds 2 second safety budget");
        latency_ += delay;
    }
    dry_.assign(format.channels * format.max_frames, 0.f);
    delay_.assign(format.channels * latency_, 0.f);
    delay_pos_ = 0;
    bypass_mix_.prepare(format.sample_rate, 5.);
    prepared_ = true;
}
void Engine::reset() noexcept {
    for (auto& module : modules_) module->reset();
    std::fill(delay_.begin(), delay_.end(), 0.f);
    delay_pos_ = 0; bypass_mix_.reset();
}
bool Engine::process(Block block) noexcept {
    if (!prepared_ || block.channels != format_.channels || block.frames > format_.max_frames) return false;
    if (!block.frames) return true;
    if (!block.planes) return false;
    for (std::size_t c = 0; c < block.channels; ++c) if (!block.planes[c]) return false;
    // A processor changing its latency must request a stopped reprepare, never silently drift.
    std::size_t reported = 0;
    for (const auto& module : modules_) {
        const auto delay = module->latency_frames();
        if (delay > latency_ - reported) return false;
        reported += delay;
    }
    if (reported != latency_) return false;
    for (std::size_t i = 0; i < block.frames; ++i) {
        for (std::size_t c = 0; c < block.channels; ++c) {
            const float input = block.planes[c][i];
            auto& dry = dry_[c * format_.max_frames + i];
            if (latency_) {
                auto& delayed = delay_[c * latency_ + delay_pos_];
                dry = delayed; delayed = input;
            } else dry = input;
        }
        if (latency_) delay_pos_ = (delay_pos_ + 1) % latency_;
    }
    // Keep processors running in bypass, preserving their history and tails.
    for (auto& module : modules_) module->process(block);
    bypass_mix_.begin_block();
    for (std::size_t i = 0; i < block.frames; ++i) {
        const float mix = bypass_mix_.next();
        for (std::size_t c = 0; c < block.channels; ++c) {
            auto& wet = block.planes[c][i];
            const float dry = dry_[c * format_.max_frames + i];
            wet = mix == 1.f ? dry : (mix == 0.f ? wet : static_cast<float>(double(wet) + (double(dry) - wet) * mix));
        }
    }
    return true;
}
}