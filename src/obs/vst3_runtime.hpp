#pragma once
#include "dsp/rack.hpp"
namespace nexus::obs_bridge {
bool initialize_vst3_runtime();
void shutdown_vst3_runtime();
void require_vst3_control_thread();
bool is_vst3_control_thread()noexcept;
bool post_vst3_control(std::function<void()>);
void watch_vst3_source(std::function<bool()>);
struct Vst3RackDelete {void operator()(LiveRack*)const noexcept;};
using Vst3Rack=std::unique_ptr<LiveRack,Vst3RackDelete>;
Vst3Rack make_vst3_rack(const Format&,const RackConfig&,bool bypass);
}
