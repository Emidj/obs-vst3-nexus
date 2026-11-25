#include "dsp/dynamics.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nexus {
namespace {
void format_valid(const Format& f){
    if(!std::isfinite(f.sample_rate)||f.sample_rate<8000||f.sample_rate>192000||
       f.channels<1||f.channels>8||!f.max_frames||f.max_frames>65536)throw std::invalid_argument("Unsupported dynamics format");
}
double pole(double rate,double ms)noexcept{return std::exp(-1000./(rate*ms));}
}
bool valid_dynamics(const DynamicsSettings& s)noexcept{
    for(const auto& f:dynamics_fields){const float v=s.*(f.member);if(!std::isfinite(v)||v<f.low||v>f.high)return false;}
    return true;
}
bool same_dynamics(const DynamicsSettings& a,const DynamicsSettings& b)noexcept{
    for(const auto& f:dynamics_fields)if(a.*(f.member)!=b.*(f.member))return false;
    return a.gate==b.gate;
}
bool Dynamics::set_settings(const DynamicsSettings& s)noexcept{
    if(!valid_dynamics(s))return false;
    threshold_.set_target(s.threshold_db);ratio_.set_target(s.ratio);makeup_.set_target(s.makeup_db);range_.set_target(s.range_db);
    attack_.store(s.attack_ms,std::memory_order_relaxed);release_.store(s.release_ms,std::memory_order_relaxed);
    hold_.store(s.hold_ms,std::memory_order_relaxed);gate_.store(s.gate,std::memory_order_relaxed);return true;
}
void Dynamics::prepare(const Format& f){
    format_valid(f);rate_=f.sample_rate;
    // Threshold has no ramp in the limiter: a lower ceiling applies immediately.
    threshold_.prepare(rate_,type_==DynamicsType::limiter?0:10);
    ratio_.prepare(rate_,10);makeup_.prepare(rate_,10);range_.prepare(rate_,10);reset();
}
void Dynamics::reset()noexcept{
    threshold_.reset();ratio_.reset();makeup_.reset();range_.reset();
    gain_db_=type_==DynamicsType::expander&&gate_.load(std::memory_order_relaxed)?-double(range_.next()):0;
    limiter_gain_=1;held_=0;open_=false;reduction_.store(0,std::memory_order_relaxed);
}
void Dynamics::process(Block b)noexcept{
    threshold_.begin_block();ratio_.begin_block();makeup_.begin_block();range_.begin_block();
    const double attack=pole(rate_,attack_.load(std::memory_order_relaxed));
    const double release=pole(rate_,release_.load(std::memory_order_relaxed));
    const size_t hold=static_cast<size_t>(rate_*hold_.load(std::memory_order_relaxed)/1000.);
    const bool gate=gate_.load(std::memory_order_relaxed);
    double maximum=0;
    for(size_t i=0;i<b.frames;++i){
        double peak=0;
        for(size_t c=0;c<b.channels;++c)peak=(std::max)(peak,std::abs(double(b.planes[c][i])));
        const double threshold=threshold_.next(),ratio=ratio_.next(),makeup=makeup_.next(),range=range_.next();
        double scale=1,reduction=0;
        if(type_==DynamicsType::limiter){
            const double ceiling=std::pow(10.,threshold/20.);
            const double required=peak>ceiling?ceiling/peak:1.;
            limiter_gain_=(std::min)(required,release*limiter_gain_+(1-release));
            if(1-limiter_gain_<1e-15)limiter_gain_=1;
            scale=limiter_gain_;reduction=scale>0?-20*std::log10(scale):800;
        }else{
            const double level=peak>1e-30?20*std::log10(peak):-600.;
            double target=0;
            if(type_==DynamicsType::compressor){
                if(level>threshold)target=(threshold-level)*(1-1/ratio);
                const double a=target<gain_db_?attack:release;
                gain_db_=target+a*(gain_db_-target);
                scale=std::pow(10.,(gain_db_+makeup)/20.);
            }else{
                if(gate){
                    // Opens at threshold; closes 3 dB lower, after hold expires.
                    if(level>=threshold){open_=true;held_=hold;}
                    else if(open_&&level<threshold-3){if(held_)--held_;else open_=false;}
                    else if(open_)held_=hold;
                    target=open_?0:-range;
                }else if(level<threshold)target=(std::max)(-range,(level-threshold)*(ratio-1));
                const double a=target>gain_db_?attack:release;
                gain_db_=target+a*(gain_db_-target);
                scale=std::pow(10.,gain_db_/20.);
            }
            if(std::abs(gain_db_)<1e-12)gain_db_=0;
            reduction=-gain_db_;
        }
        maximum=(std::max)(maximum,reduction);
        for(size_t c=0;c<b.channels;++c)b.planes[c][i]=float(double(b.planes[c][i])*scale);
    }
    reduction_.store(float(maximum),std::memory_order_relaxed);
}
bool StereoWidth::set_width(float width)noexcept{
    return std::isfinite(width)&&width>=0&&width<=2&&width_.set_target(width);
}
void StereoWidth::prepare(const Format& f){
    format_valid(f);if(f.channels!=2)throw std::invalid_argument("Stereo Width requires two channels");
    width_.prepare(f.sample_rate,10);
}
void StereoWidth::process(Block b)noexcept{
    width_.begin_block();
    for(size_t i=0;i<b.frames;++i){
        const double width=width_.next();if(width==1)continue;
        const double l=b.planes[0][i],r=b.planes[1][i],mid=(l+r)*.5,side=(l-r)*.5*width;
        b.planes[0][i]=float(mid+side);b.planes[1][i]=float(mid-side);
    }
}
}
