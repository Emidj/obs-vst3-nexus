#pragma once
#include "dsp/engine.hpp"
#include "vst3/types.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nexus::vst3 {
struct ParameterInfo {
    uint32_t id=0;std::string title,units;
    int32_t steps=0;bool read_only=false;
};
struct State {std::string class_id;std::vector<uint8_t> component,controller;};
std::vector<std::string> installed_paths(); // filesystem discovery only; no module load
std::vector<ClassInfo> inspect(const std::string& path); // explicit module load, control only
// Core with optional LiveRack factory and OBS bridge. Main mono/stereo, float32 preferred or float64 conversion,
// inactive auxiliary buses supported. No instrument/event/MIDI routing.
// All module/lifecycle/state calls: serialized control/UI thread.
// Audio and parameter producers must be stopped for prepare/restore/destruction.
// set_parameter/read_parameter/status may run concurrently with process.
// reset() only requests a stopped reprepare; it never calls plugin lifecycle
// from audio. Instances reporting changed I/O/latency/restart are quarantined.
class Effect final:public Processor {
public:
    Effect(const std::string& path,const std::string& class_id);
    ~Effect()override;
    Effect(const Effect&)=delete;
    Effect& operator=(const Effect&)=delete;
    void prepare(const Format&)override;
    void reset()noexcept override;
    void process(Block)noexcept override;
    size_t latency_frames()const noexcept override;
    void suspend();
    State capture_state();
    void restore_state(const State&);
    const std::vector<ParameterInfo>& parameters()const noexcept;
    bool set_parameter(uint32_t id,double normalized)noexcept;
    double read_parameter(uint32_t id)const noexcept;
    // Synchronize processor feedback to controller values on UI.
    void service_control();
    void open_editor(bool visible=true);
    void close_editor();
    void* editor_window()const noexcept;
    bool editor_dirty()const noexcept;
    bool needs_reprepare()const noexcept;
    bool requires_reload()const noexcept;
    uint64_t faults()const noexcept;
    uint64_t dropped_points()const noexcept;
    uint64_t rejected_host_requests()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
