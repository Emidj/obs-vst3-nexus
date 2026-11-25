#pragma once
#include "dsp/engine.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nexus {
// Pads a processor to an explicitly selected, immutable latency budget.
// Construct/prepare/reset/destroy with audio stopped. Never changes the budget
// while rendering; a graph exceeding it must be rejected on the control thread.
class FixedLatency final : public Processor {
public:
    FixedLatency(std::unique_ptr<Processor> processor, size_t budget)
        : processor_(std::move(processor)), budget_(budget) {
        if(!processor_)throw std::invalid_argument("Missing latency processor");
    }
    void prepare(const Format& format) override {
        prepared_=false;
        if(!std::isfinite(format.sample_rate)||format.sample_rate<8000||
           format.sample_rate>192000||!format.channels||format.channels>8||
           !format.max_frames||format.max_frames>65536||
           budget_>static_cast<size_t>(format.sample_rate*2))
            throw std::invalid_argument("Invalid latency format or budget");
        processor_->prepare(format);
        source_latency_=processor_->latency_frames();
        if(source_latency_>budget_)throw std::invalid_argument("Processor exceeds latency budget");
        padding_=budget_-source_latency_;
        delay_.assign(format.channels*padding_,0);
        format_=format;position_=0;quarantined_=false;prepared_=true;
    }
    void reset() noexcept override {
        processor_->reset();std::fill(delay_.begin(),delay_.end(),0.f);position_=0;
    }
    size_t latency_frames() const noexcept override {return budget_;}
    float gain_reduction_db() const noexcept override {return processor_->gain_reduction_db();}
    void process(Block block) noexcept override {
        if(!prepared_||block.channels!=format_.channels||block.frames>format_.max_frames||
           !block.planes)return;
        for(size_t c=0;c<block.channels;++c)if(!block.planes[c])return;
        // A plugin changing latency asynchronously is quarantined; never render
        // misaligned audio. Reprepare on control is required for recovery.
        if(quarantined_||processor_->latency_frames()!=source_latency_){
            quarantined_=true;
            for(size_t c=0;c<block.channels;++c)std::fill_n(block.planes[c],block.frames,0.f);
            return;
        }
        processor_->process(block);
        if(!padding_)return;
        for(size_t i=0;i<block.frames;++i){
            for(size_t c=0;c<block.channels;++c){
                auto& history=delay_[c*padding_+position_];
                std::swap(history,block.planes[c][i]);
            }
            if(++position_==padding_)position_=0;
        }
    }
private:
    std::unique_ptr<Processor> processor_;
    size_t budget_,source_latency_=0,padding_=0,position_=0;
    Format format_{};
    bool prepared_=false,quarantined_=false;
    std::vector<float> delay_;
};
}
