#pragma once
#include "dsp/engine.hpp"
#include <array>
namespace nexus {
struct DynamicsSettings {
    float threshold_db=-18,ratio=4,attack_ms=10,release_ms=100,makeup_db=0,range_db=60,hold_ms=50;
    bool gate=false;
};
struct DynamicsField {const char* key;float DynamicsSettings::*member;float low,high;};
inline constexpr std::array<DynamicsField,7> dynamics_fields{{
    {"threshold_db",&DynamicsSettings::threshold_db,-80,0},{"ratio",&DynamicsSettings::ratio,1,20},
    {"attack_ms",&DynamicsSettings::attack_ms,.1f,200},{"release_ms",&DynamicsSettings::release_ms,10,3000},
    {"makeup_db",&DynamicsSettings::makeup_db,0,24},{"range_db",&DynamicsSettings::range_db,0,80},
    {"hold_ms",&DynamicsSettings::hold_ms,0,500}
}};
bool valid_dynamics(const DynamicsSettings&)noexcept;
bool same_dynamics(const DynamicsSettings&,const DynamicsSettings&)noexcept;
enum class DynamicsType {compressor,expander,limiter};
// Basic linked-peak, feed-forward models. No sidechain, lookahead or True Peak.
// Only set_settings() and gain_reduction_db() may run concurrently with audio.
// Parameters are independent lock-free targets, not transactional snapshots.
class Dynamics final:public Processor {
public:
    explicit Dynamics(DynamicsType type):type_(type){}
    bool set_settings(const DynamicsSettings&)noexcept;
    void prepare(const Format&)override;
    void reset()noexcept override;
    void process(Block)noexcept override;
    float gain_reduction_db()const noexcept override{return reduction_.load(std::memory_order_relaxed);}
private:
    DynamicsType type_;
    SmoothedParameter threshold_{-18},ratio_{4},makeup_{0},range_{60};
    std::atomic<float> attack_{10},release_{100},hold_{50};
    std::atomic<bool> gate_{false};
    std::atomic<float> reduction_{0};
    double rate_=48000,gain_db_=0,limiter_gain_=1;
    size_t held_=0;
    bool open_=false;
};
class StereoWidth final:public Processor {
public:
    bool set_width(float)noexcept;
    void prepare(const Format&)override;
    void reset()noexcept override{width_.reset();}
    void process(Block)noexcept override;
private:
    SmoothedParameter width_{1};
};
}
