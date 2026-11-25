#include "dsp/equalizer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nexus {
namespace {
constexpr double pi=3.14159265358979323846;
void design(Iir::RBJ::BandShelf& filter,double rate,double frequency,double gain,double q) {
    const double fc=(std::min)(frequency,rate*.45),w=2*pi*fc/rate;
    // iir1 uses octave bandwidth; convert the requested RBJ Q at this frequency.
    const double bandwidth=2/std::log(2.)*std::sin(w)/w*std::asinh(1/(2*q));
    filter.setup(rate,fc,gain,bandwidth);
}
}
bool valid_eq_band(const EqBandSettings& band) noexcept {
    return std::isfinite(band.frequency) && band.frequency>=20 && band.frequency<=20000 &&
        std::isfinite(band.gain_db) && band.gain_db>=-18 && band.gain_db<=18 &&
        std::isfinite(band.q) && band.q>=.2f && band.q<=10;
}
double equalizer_response_db(const EqSettings& settings,double rate,double frequency) {
    if(!std::isfinite(rate)||rate<8000||rate>192000||!std::isfinite(frequency)||frequency<=0||frequency>=rate/2)
        throw std::invalid_argument("Invalid response frequency");
    double gain=0;
    for(const auto& band:settings.bands) {
        if(!valid_eq_band(band))throw std::invalid_argument("Invalid EQ band");
        if(settings.enabled && band.enabled && band.gain_db!=0) {
            Iir::RBJ::BandShelf filter;design(filter,rate,band.frequency,band.gain_db,band.q);
            gain+=20*std::log10(std::abs(filter.response(frequency/rate)));
        }
    }
    return gain;
}
bool Equalizer::set_band(size_t index,const EqBandSettings& value) noexcept {
    if(index>=band_count || !valid_eq_band(value))return false;
    auto& band=bands_[index];
    band.frequency.set_target(value.frequency);band.gain.set_target(value.gain_db);
    band.q.set_target(value.q);band.mix.set_target(value.enabled?1.f:0.f);
    return true;
}
void Equalizer::coefficients(Band& band,double frequency,double gain,double q) noexcept {
    design(band.design,format_.sample_rate,frequency,gain,q);
    band.previous_frequency=frequency;band.previous_gain=gain;band.previous_q=q;
}
void Equalizer::prepare(const Format& format) {
    if(!std::isfinite(format.sample_rate)||format.sample_rate<8000||format.sample_rate>192000||
       format.channels<1||format.channels>8||format.max_frames<1||format.max_frames>65536)
        throw std::invalid_argument("Unsupported EQ format");
    format_=format;dry_.assign(format.channels*format.max_frames,0.f);
    for(auto& band:bands_){
        band.frequency.prepare(format.sample_rate,20);band.gain.prepare(format.sample_rate,20);
        band.q.prepare(format.sample_rate,20);band.mix.prepare(format.sample_rate,10);
    }
    mix_.prepare(format.sample_rate,10);reset();
}
void Equalizer::reset() noexcept {
    for(auto& band:bands_){
        for(auto& state:band.states)state.reset();
        band.frequency.reset();band.gain.reset();band.q.reset();band.mix.reset();
        coefficients(band,band.frequency.next(),band.gain.next(),band.q.next());
    }
    mix_.reset();
}
void Equalizer::process(Block b) noexcept {
    for(size_t c=0;c<b.channels;++c)std::copy_n(b.planes[c],b.frames,dry_.data()+c*format_.max_frames);
    for(auto& band:bands_){
        band.frequency.begin_block();band.gain.begin_block();band.q.begin_block();band.mix.begin_block();
        for(size_t i=0;i<b.frames;++i){
            const double frequency=band.frequency.next(),gain=band.gain.next(),q=band.q.next();
            if(frequency!=band.previous_frequency||gain!=band.previous_gain||q!=band.previous_q)
                coefficients(band,frequency,gain,q);
            const float mix=band.mix.next();
            for(size_t c=0;c<b.channels;++c){
                const float x=b.planes[c][i];
                double y=band.states[c].filter(x,band.design);
                if(x==0 && std::abs(y)<1e-30){band.states[c].reset();y=0;}
                const float wet=gain==0?x:static_cast<float>(y);
                b.planes[c][i]=mix==0?x:(mix==1?wet:x+(wet-x)*mix);
            }
        }
    }
    mix_.begin_block();
    for(size_t i=0;i<b.frames;++i){
        const float mix=mix_.next();
        for(size_t c=0;c<b.channels;++c){
            const float dry=dry_[c*format_.max_frames+i];auto& wet=b.planes[c][i];
            wet=mix==0?dry:(mix==1?wet:dry+(wet-dry)*mix);
        }
    }
}
}