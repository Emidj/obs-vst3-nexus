#pragma once
#include "dsp/basic.hpp"
#include "dsp/equalizer.hpp"
#include "dsp/dynamics.hpp"
#include <cstdint>
#include <string>
#include <mutex>
#include <list>
#include <functional>
namespace nexus {
enum class ModuleKind { gain, equalizer, high_pass, low_pass, balance, pan, mono, polarity, compressor, expander, limiter, stereo_width, vst3, count };
struct Vst3Settings {
    std::string path,class_id,name,vendor;
    bool has_state=false;
    std::vector<uint8_t> component_state,controller_state;
    std::vector<std::pair<uint32_t,double>> parameters;
};
bool same_vst3(const Vst3Settings&,const Vst3Settings&);
struct RackNode {
    uint64_t id=1;
    ModuleKind kind=ModuleKind::gain;
    bool enabled=true;
    float value=0;
    EqSettings eq;
    DynamicsSettings dynamics;
    Vst3Settings vst3;
};
// Lifecycle hook, called only by parked LiveRack on its control thread.
class RackStatefulProcessor:public Processor {
public:
    virtual Vst3Settings capture_vst3()=0;
    virtual bool service_control(){return false;}
    virtual bool requires_reload()const noexcept{return true;}
    virtual bool audio_ready()const noexcept{return true;}
    virtual bool open_editor(bool){return false;}
    virtual bool editor_dirty()const noexcept{return false;}
    virtual void close_editor(){}
};
using RackProcessorFactory=std::function<std::unique_ptr<Processor>(const RackNode&)>;
using RackConfig=std::vector<RackNode>;
struct RackDocument {
    RackConfig a,b;
    bool use_b=false;
    bool bypass=false;
    const RackConfig& active()const{return use_b?b:a;}
    RackConfig& active(){return use_b?b:a;}
};
bool is_dynamics(ModuleKind)noexcept;
const char* module_id(ModuleKind);
const char* module_name(ModuleKind);
ModuleKind parse_module_id(const std::string&);
RackNode default_node(ModuleKind,uint64_t id);
void validate_rack(const RackConfig&);
void validate_document(const RackDocument&);
RackDocument default_document();
bool same_rack(const RackConfig&,const RackConfig&);
bool same_document(const RackDocument&,const RackDocument&);
bool contains_vst3(const RackDocument&)noexcept;
// One audio consumer; serialized control producers. Constructor/destructor
// require stopped audio. All graph allocation/reclamation stays on control.
class LiveRack {
public:
    // Fixed-budget default preserves native filter timing. Optional adaptive
    // graphs declare their own latency; changes switch at packet boundaries
    // with a fade-in and require output timestamp compensation by the caller.
    LiveRack(const Format&,const RackConfig&,bool initial_bypass=false,size_t latency_budget=0,RackProcessorFactory factory={},bool adaptive_latency=false,bool(*external_busy)()noexcept=nullptr);
    ~LiveRack();
    void publish(const RackConfig&);
    void collect();
    // Nonblocking handshake: request on control, continue audio until ready,
    // then capture on the lifecycle control thread. No waiting in process().
    void request_snapshot();
    bool snapshot_ready()const noexcept{return snapshot_state_.load(std::memory_order_acquire)==2;}
    bool snapshot_failed()const noexcept{return snapshot_state_.load(std::memory_order_acquire)==6;}
    bool snapshot_busy()const noexcept{return snapshot_state_.load(std::memory_order_acquire)!=0;}
    RackConfig capture_snapshot();
    // Parked audio only: reconstruct processors, including reload/I/O/latency.
    RackConfig rebuild_snapshot(const RackConfig* replacement=nullptr);
    bool reconfigure_snapshot();
    bool service_control();
    void open_editor(uint64_t id,bool visible=true);
    bool editor_dirty()const;
    bool control_busy()const noexcept{return foreign_busy_.load(std::memory_order_acquire)||(external_busy_&&external_busy_());}
    void cancel_snapshot();
    void set_bypass(bool value)noexcept{bypass_.set_target(value?1.f:0.f);}
    bool process(Block,bool allow_graph_switch=true)noexcept;
    size_t retained_graphs()const;
    size_t latency_frames()const noexcept{return reported_latency_.load(std::memory_order_acquire);}
    float gain_reduction_db(uint64_t id)const;
    uint64_t fault_count()const noexcept{return faults_.load(std::memory_order_relaxed);}
    uint64_t applied_revision()const noexcept{return applied_.load(std::memory_order_relaxed);}
private:
    struct Graph;
    struct ControlScope;
    void collect_locked();
    void finish_reconfiguration_audio(Block,bool)noexcept;
    void erase_graph(Graph*);
    bool queue_room()const noexcept;
    void retire(Graph*)noexcept;
    Format format_;
    RackProcessorFactory factory_;
    mutable std::recursive_mutex control_;
    std::atomic<bool> foreign_busy_{false};
    bool(*external_busy_)()noexcept=nullptr;
    std::list<std::unique_ptr<Graph>> owners_;
    std::atomic<Graph*> pending_{nullptr};
    Graph* active_=nullptr;
    Graph* fading_=nullptr;
    std::array<Graph*,32> retired_{};
    std::atomic<size_t> read_{0},write_{0};
    size_t fade_position_=0,fade_length_=0;
    std::vector<float> input_,old_,dry_,dry_delay_;
    const bool adaptive_latency_=false;
    size_t latency_budget_=0,dry_capacity_=0,dry_position_=0,warm_remaining_=0;
    std::atomic<size_t> reported_latency_{0};
    std::atomic<size_t> resume_latency_{SIZE_MAX};
    size_t latency_fade_=0;
    bool previous_quarantine_=false,have_last_output_=false;
    size_t reconfiguration_fade_=0;
    std::array<float,8> last_output_{};
    std::array<double,8> reconfiguration_anchor_{};
    // 0 idle, 1 fading dry, 2 parked, 3 resume requested, 4 warming/fading wet,
    // 5 cancellation requested, 6 parked after failed recovery.
    std::atomic<unsigned> snapshot_state_{0};
    float snapshot_mix_=0;
    size_t snapshot_warm_=0;
    SmoothedParameter bypass_;
    uint64_t revision_=0;
    std::atomic<uint64_t> applied_{0},faults_{0};
};
}