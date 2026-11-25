#include "dsp/loudness.hpp"
#include <ebur128.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
extern "C" void nexus_ebur128_initialize_tables(void);
namespace nexus {
struct LoudnessMeter::State {
    ebur128_state* meter=nullptr;
    ~State(){if(meter)ebur128_destroy(&meter);}
};
namespace {
std::once_flag tables;
float display(double x)noexcept{return std::isfinite(x)?float((std::max)(-120.,(std::min)(120.,x))):-120.f;}
}
LoudnessMeter::LoudnessMeter(const Format& f):state_(std::make_unique<State>()),format_(f){
    if(!std::isfinite(f.sample_rate)||f.sample_rate<8000||f.sample_rate>192000||
       std::floor(f.sample_rate)!=f.sample_rate||f.channels<1||f.channels>2||
       !f.max_frames||f.max_frames>65536)throw std::invalid_argument("Loudness requires mono/stereo and a valid format");
    std::call_once(tables,nexus_ebur128_initialize_tables);
    state_->meter=ebur128_init(static_cast<unsigned>(f.channels),static_cast<unsigned long>(f.sample_rate),
        EBUR128_MODE_S|EBUR128_MODE_I|EBUR128_MODE_HISTOGRAM);
    if(!state_->meter)throw std::bad_alloc();
    ebur128_set_channel(state_->meter,0,f.channels==1?EBUR128_CENTER:EBUR128_LEFT);
    if(f.channels==2)ebur128_set_channel(state_->meter,1,EBUR128_RIGHT);
    interleaved_.resize(f.channels*f.max_frames);
    interval_=static_cast<size_t>((f.sample_rate+5)/10);
    for(auto& v:values_)v.store(-120,std::memory_order_relaxed);
}
LoudnessMeter::~LoudnessMeter()=default;
bool LoudnessMeter::process(Block b)noexcept{
    if(b.channels!=format_.channels||b.frames>format_.max_frames)return false;
    if(!b.frames)return true;
    if(!b.planes)return false;
    for(size_t c=0;c<b.channels;++c)if(!b.planes[c])return false;
    for(size_t c=0;c<b.channels;++c)for(size_t i=0;i<b.frames;++i)
        if(!std::isfinite(b.planes[c][i]))return false;
    for(size_t offset=0;offset<b.frames;){
        const size_t count=(std::min)(b.frames-offset,interval_-since_);
        for(size_t i=0;i<count;++i)for(size_t c=0;c<b.channels;++c){
            const float sample=b.planes[c][offset+i];interleaved_[i*b.channels+c]=sample;
            peak_=(std::max)(peak_,std::abs(double(sample)));
        }
        if(ebur128_add_frames_float(state_->meter,interleaved_.data(),count)!=EBUR128_SUCCESS)return false;
        frames_+=count;since_+=count;offset+=count;
        if(since_==interval_){
            unsigned ready=8;double v=0;
            if(frames_>=static_cast<uint64_t>(double(interval_*4))){
                if(ebur128_loudness_momentary(state_->meter,&v)==EBUR128_SUCCESS){values_[0].store(display(v),std::memory_order_relaxed);ready|=1;}
                if(ebur128_loudness_global(state_->meter,&v)==EBUR128_SUCCESS){values_[2].store(display(v),std::memory_order_relaxed);ready|=4;}
            }
            if(frames_>=static_cast<uint64_t>(double(interval_*30))){
                if(ebur128_loudness_shortterm(state_->meter,&v)==EBUR128_SUCCESS){values_[1].store(display(v),std::memory_order_relaxed);ready|=2;}
            }
            values_[3].store(display(peak_>0?20*std::log10(peak_):-120),std::memory_order_relaxed);
            ready_.store(ready,std::memory_order_release);since_=0;peak_=0;
        }
    }return true;
}
LoudnessReading LoudnessMeter::read()const noexcept{
    const auto r=ready_.load(std::memory_order_acquire);
    return {values_[0].load(std::memory_order_relaxed),values_[1].load(std::memory_order_relaxed),
        values_[2].load(std::memory_order_relaxed),values_[3].load(std::memory_order_relaxed),
        bool(r&1),bool(r&2),bool(r&4),bool(r&8)};
}
}
