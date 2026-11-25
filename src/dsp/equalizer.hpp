#pragma once
#include "dsp/engine.hpp"
#include <array>
#include <iir/RBJ.h>
namespace nexus {
struct EqBandSettings { float frequency=1000, gain_db=0, q=1; bool enabled=true; };
struct EqSettings {
    bool enabled=true;
    std::array<EqBandSettings,4> bands{{{120,0,1,true},{500,0,1,true},{2000,0,1,true},{6000,0,1,true}}};
};
bool valid_eq_band(const EqBandSettings&) noexcept;
// Target response for plotting on a control/UI thread. Never reads DSP state.
double equalizer_response_db(const EqSettings&,double rate,double frequency);
class Equalizer final : public Processor {
public:
    static constexpr size_t band_count=4;
    bool set_band(size_t index,const EqBandSettings&) noexcept;
    void set_enabled(bool enabled) noexcept { mix_.set_target(enabled?1.f:0.f); }
    void prepare(const Format&) override;
    void reset() noexcept override;
    void process(Block) noexcept override;
private:
    struct Band {
        explicit Band(float hz):frequency(hz){}
        SmoothedParameter frequency,gain,q{1},mix{1};
        Iir::RBJ::BandShelf design;
        std::array<Iir::DirectFormI,8> states;
        double previous_frequency=-1,previous_gain=0,previous_q=0;
    };
    void coefficients(Band&,double frequency,double gain,double q) noexcept;
    std::array<Band,4> bands_{{Band(120),Band(500),Band(2000),Band(6000)}};
    SmoothedParameter mix_{1};
    std::vector<float> dry_;
    Format format_{};
};
}