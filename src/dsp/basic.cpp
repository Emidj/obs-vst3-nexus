#include "dsp/basic.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nexus {
namespace {
bool range(float value,float min,float max) noexcept { return std::isfinite(value) && value>=min && value<=max; }
void format_valid(const Format& f) {
    if(!std::isfinite(f.sample_rate) || f.sample_rate<8000 || f.sample_rate>192000 ||
       f.channels<1 || f.channels>8 || f.max_frames<1 || f.max_frames>65536)
        throw std::invalid_argument("Unsupported processor format");
}
void stereo_or_mono(const Format& f) {
    format_valid(f);
    if(f.channels>2) throw std::invalid_argument("Routing module supports mono/stereo only");
}
constexpr double pi=3.14159265358979323846;
}
bool Gain::set_db(float db) noexcept {
    return range(db,-60,24) && amplitude_.set_target(static_cast<float>(std::pow(10.,double(db)/20)));
}
void Gain::prepare(const Format& f){format_valid(f);amplitude_.prepare(f.sample_rate,10);}
void Gain::process(Block b) noexcept {
    amplitude_.begin_block();
    for(size_t i=0;i<b.frames;++i){const float g=amplitude_.next();for(size_t c=0;c<b.channels;++c)b.planes[c][i]*=g;}
}
bool Balance::set_position(float p) noexcept {return range(p,-1,1) && position_.set_target(p);}
void Balance::prepare(const Format& f){stereo_or_mono(f);position_.prepare(f.sample_rate,10);}
void Balance::process(Block b) noexcept {
    position_.begin_block();
    for(size_t i=0;i<b.frames;++i){
        const float p=position_.next();
        if(b.channels==2){b.planes[0][i]*=p>0?1-p:1;b.planes[1][i]*=p<0?1+p:1;}
    }
}
void Mono::prepare(const Format& f){stereo_or_mono(f);mix_.prepare(f.sample_rate,10);}
void Mono::process(Block b) noexcept {
    mix_.begin_block();
    for(size_t i=0;i<b.frames;++i){
        const float mix=mix_.next();
        if(b.channels==2 && mix!=0){
            const float l=b.planes[0][i],r=b.planes[1][i],mono=l*.5f+r*.5f;
            b.planes[0][i]=mix==1?mono:l+(mono-l)*mix;
            b.planes[1][i]=mix==1?mono:r+(mono-r)*mix;
        }
    }
}
void Polarity::prepare(const Format& f){format_valid(f);scale_.prepare(f.sample_rate,5);}
void Polarity::process(Block b) noexcept {
    scale_.begin_block();
    for(size_t i=0;i<b.frames;++i){const float g=scale_.next();for(size_t c=0;c<b.channels;++c)b.planes[c][i]*=g;}
}
bool Pan::set_position(float p) noexcept {return range(p,-1,1) && position_.set_target(p);}
void Pan::prepare(const Format& f){
    format_valid(f);if(f.channels!=2)throw std::invalid_argument("Pan requires stereo output");
    position_.prepare(f.sample_rate,10);
}
void Pan::process(Block b) noexcept {
    position_.begin_block();
    for(size_t i=0;i<b.frames;++i){
        const float p=position_.next();const double angle=(double(p)+1)*pi/4;
        const float mono=b.planes[0][i]*.5f+b.planes[1][i]*.5f;
        b.planes[0][i]=p==1?0.f:mono*static_cast<float>(std::cos(angle));
        b.planes[1][i]=p==-1?0.f:mono*static_cast<float>(std::sin(angle));
    }
}
bool PassFilter::set_cutoff(float hz) noexcept {return range(hz,20,20000) && cutoff_.set_target(hz);}
void PassFilter::coefficients(double hz) noexcept {
    if(type_==PassType::low_pass)low_.setup(rate_,hz);
    else high_.setup(rate_,hz);
    previous_=hz;
}
void PassFilter::prepare(const Format& f){
    format_valid(f);rate_=f.sample_rate;
    cutoff_.prepare(rate_,20);mix_.prepare(rate_,10);reset();
}
void PassFilter::reset() noexcept {
    for(auto& state:states_)state.reset();
    cutoff_.reset();mix_.reset();
    coefficients((std::min)(double(cutoff_.next()),rate_*.45));
}
void PassFilter::process(Block b) noexcept {
    cutoff_.begin_block();mix_.begin_block();
    for(size_t i=0;i<b.frames;++i){
        const double hz=(std::min)(double(cutoff_.next()),rate_*.45);
        if(hz!=previous_)coefficients(hz);
        const float mix=mix_.next();
        const Iir::Biquad& design=type_==PassType::low_pass?static_cast<const Iir::Biquad&>(low_):static_cast<const Iir::Biquad&>(high_);
        for(size_t c=0;c<b.channels;++c){
            const float x=b.planes[c][i];
            double y=states_[c].filter(double(x),design);
            if(x==0 && std::abs(y)<1e-30){states_[c].reset();y=0;}
            b.planes[c][i]=mix==0?x:(mix==1?static_cast<float>(y):x+(static_cast<float>(y)-x)*mix);
        }
    }
}
}