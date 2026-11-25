#include "dsp/rack.hpp"
#include "dsp/latency.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <filesystem>
#include <stdexcept>
namespace nexus {
namespace {
const char* identifiers[]{"gain","equalizer","highpass","lowpass","balance","pan","mono","polarity","compressor","expander","limiter","stereo_width","vst3"};
const char* names[]{"Gain / Trim","Parametric EQ","High Pass","Low Pass","Balance","Pan (mono to stereo)","Mono","Polarity","Compressor","Gate / Expander","Sample-peak Limiter","Stereo Width","VST3 Effect"};
bool eq_equal(const EqSettings& a,const EqSettings& b){
    if(a.enabled!=b.enabled)return false;
    for(size_t i=0;i<4;++i){const auto& x=a.bands[i];const auto& y=b.bands[i];
        if(x.frequency!=y.frequency||x.gain_db!=y.gain_db||x.q!=y.q||x.enabled!=y.enabled)return false;
    }return true;
}
class Slot final : public Processor {
public:
    explicit Slot(const RackNode& node,const RackProcessorFactory& factory):id(node.id),kind(node.kind){
        std::unique_ptr<Processor> module;
        switch(kind){
        case ModuleKind::gain:module=std::make_unique<Gain>();break;
        case ModuleKind::equalizer:module=std::make_unique<Equalizer>();break;
        case ModuleKind::high_pass:module=std::make_unique<PassFilter>(PassType::high_pass);break;
        case ModuleKind::low_pass:module=std::make_unique<PassFilter>(PassType::low_pass);break;
        case ModuleKind::balance:module=std::make_unique<Balance>();break;
        case ModuleKind::pan:module=std::make_unique<Pan>();break;
        case ModuleKind::mono:module=std::make_unique<Mono>();break;
        case ModuleKind::polarity:module=std::make_unique<Polarity>();break;
        case ModuleKind::compressor:module=std::make_unique<Dynamics>(DynamicsType::compressor);break;
        case ModuleKind::expander:module=std::make_unique<Dynamics>(DynamicsType::expander);break;
        case ModuleKind::limiter:module=std::make_unique<Dynamics>(DynamicsType::limiter);break;
        case ModuleKind::stereo_width:module=std::make_unique<StereoWidth>();break;
        case ModuleKind::vst3:
            if(!factory)throw std::invalid_argument("VST3 host unavailable in this rack");
            module=factory(node);
            if(!module)throw std::invalid_argument("VST3 factory returned no processor");
            break;
        default:throw std::invalid_argument("Unknown module kind");
        }
        processor=module.get();engine.add(std::move(module));publish(node);
    }
    void publish(const RackNode& n)noexcept{
        engine.set_bypass(!n.enabled);enabled.store(n.enabled,std::memory_order_relaxed);
        switch(kind){
        case ModuleKind::gain:static_cast<Gain*>(processor)->set_db(n.value);break;
        case ModuleKind::equalizer:{
            auto* eq=static_cast<Equalizer*>(processor);eq->set_enabled(n.eq.enabled);
            for(size_t i=0;i<4;++i)eq->set_band(i,n.eq.bands[i]);break;
        }
        case ModuleKind::high_pass:case ModuleKind::low_pass:{
            auto* pass=static_cast<PassFilter*>(processor);pass->set_cutoff(n.value);pass->set_enabled(true);break;
        }
        case ModuleKind::balance:static_cast<Balance*>(processor)->set_position(n.value);break;
        case ModuleKind::pan:static_cast<Pan*>(processor)->set_position(n.value);break;
        case ModuleKind::mono:static_cast<Mono*>(processor)->set_enabled(true);break;
        case ModuleKind::polarity:static_cast<Polarity*>(processor)->set_inverted(true);break;
        case ModuleKind::compressor:case ModuleKind::expander:case ModuleKind::limiter:
            static_cast<Dynamics*>(processor)->set_settings(n.dynamics);break;
        case ModuleKind::stereo_width:static_cast<StereoWidth*>(processor)->set_width(n.value);break;
        default:break;
        }
    }
    void capture(RackNode& node){
        if(kind!=ModuleKind::vst3)return;
        auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor);
        if(!stateful)throw std::runtime_error("VST3 processor has no state capture hook");
        node.vst3=stateful->capture_vst3();
    }
    bool audio_ready()const{auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor);return !stateful||stateful->audio_ready();}
    bool requires_reload()const{auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor);return stateful&&stateful->requires_reload();}
    bool editor_dirty()const{auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor);return stateful&&stateful->editor_dirty();}
    bool open_editor(bool visible){auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor);return stateful&&stateful->open_editor(visible);}
    void close_editor(){if(auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor))stateful->close_editor();}
    bool service_control(){
        auto* stateful=dynamic_cast<RackStatefulProcessor*>(processor);
        return stateful&&stateful->service_control();
    }
    void prepare(const Format& f)override{engine.prepare(f);}
    void reset()noexcept override{engine.reset();}
    size_t latency_frames()const noexcept override{return engine.latency_frames();}
    void process(Block b)noexcept override{engine.process(b);}
    uint64_t id;ModuleKind kind;
private:
    Engine engine;Processor* processor=nullptr;std::atomic<bool> enabled{true};
public:
    float gain_reduction_db()const noexcept override{
        return enabled.load(std::memory_order_relaxed)?processor->gain_reduction_db():0;
    }
};
}
bool same_vst3(const Vst3Settings& a,const Vst3Settings& b){
    return a.path==b.path&&a.class_id==b.class_id&&a.name==b.name&&a.vendor==b.vendor&&a.has_state==b.has_state&&
        a.component_state==b.component_state&&a.controller_state==b.controller_state&&a.parameters==b.parameters;
}
bool is_dynamics(ModuleKind kind)noexcept{return kind==ModuleKind::compressor||kind==ModuleKind::expander||kind==ModuleKind::limiter;}
const char* module_id(ModuleKind kind){
    const auto i=static_cast<size_t>(kind);if(i>=static_cast<size_t>(ModuleKind::count))throw std::invalid_argument("Unknown module kind");
    return identifiers[i];
}
const char* module_name(ModuleKind kind){module_id(kind);return names[static_cast<size_t>(kind)];}
ModuleKind parse_module_id(const std::string& text){
    for(size_t i=0;i<static_cast<size_t>(ModuleKind::count);++i)if(text==identifiers[i])return static_cast<ModuleKind>(i);
    throw std::invalid_argument("Unsupported module type: "+text);
}
RackNode default_node(ModuleKind kind,uint64_t id){
    module_id(kind);RackNode result;result.kind=kind;result.id=id;
    if(kind==ModuleKind::high_pass)result.value=80;
    if(kind==ModuleKind::low_pass)result.value=12000;
    if(kind==ModuleKind::stereo_width)result.value=1;
    if(kind==ModuleKind::expander){result.dynamics.threshold_db=-45;result.dynamics.ratio=2;result.dynamics.attack_ms=5;result.dynamics.range_db=80;}
    if(kind==ModuleKind::limiter)result.dynamics.threshold_db=-1;
    return result;
}
void validate_rack(const RackConfig& nodes){
    if(nodes.size()>16)throw std::invalid_argument("Initial rack limit is 16 modules");
    std::set<uint64_t> ids;
    for(const auto& node:nodes){
        module_id(node.kind);
        if(!node.id||!ids.insert(node.id).second)throw std::invalid_argument("Invalid or duplicate module ID");
        if(node.kind==ModuleKind::vst3){
            const auto& v=node.vst3;
            if(v.name.size()>4096||v.vendor.size()>4096)throw std::invalid_argument("VST3 display metadata too long");
            if(v.path.empty()||v.path.size()>32768||v.path.find(char(0))!=std::string::npos||
               !std::filesystem::u8path(v.path).is_absolute())
                throw std::invalid_argument("VST3 path must be absolute");
            if(v.class_id.size()!=32||!std::all_of(v.class_id.begin(),v.class_id.end(),[](char c){
                return (c>='0'&&c<='9')||(c>='A'&&c<='F');}))
                throw std::invalid_argument("VST3 class ID must be 32 uppercase hex characters");
            if(v.component_state.size()>16*1024*1024||v.controller_state.size()>16*1024*1024||
               (!v.has_state&&(!v.component_state.empty()||!v.controller_state.empty())))
                throw std::invalid_argument("Invalid VST3 state size");
            if(v.parameters.size()>4096)throw std::invalid_argument("Too many VST3 parameters");
            std::set<uint32_t> parameter_ids;
            for(const auto& parameter:v.parameters)
                if(!parameter_ids.insert(parameter.first).second||!std::isfinite(parameter.second)||
                   parameter.second<0||parameter.second>1)
                    throw std::invalid_argument("Invalid or duplicate VST3 parameter");
        }
        if(!std::isfinite(node.value))throw std::invalid_argument("Non-finite module value");
        if(node.kind==ModuleKind::gain&&(node.value< -60||node.value>24))throw std::invalid_argument("Invalid gain");
        if((node.kind==ModuleKind::balance||node.kind==ModuleKind::pan)&&(node.value< -1||node.value>1))throw std::invalid_argument("Invalid position");
        if((node.kind==ModuleKind::high_pass||node.kind==ModuleKind::low_pass)&&(node.value<20||node.value>20000))throw std::invalid_argument("Invalid cutoff");
        if(node.kind==ModuleKind::stereo_width&&(node.value<0||node.value>2))throw std::invalid_argument("Invalid stereo width");
        if(!valid_dynamics(node.dynamics))throw std::invalid_argument("Invalid dynamics settings");
        for(const auto& band:node.eq.bands)if(!valid_eq_band(band))throw std::invalid_argument("Invalid EQ band");
    }
}
void validate_document(const RackDocument& doc){
    validate_rack(doc.a);validate_rack(doc.b);
    size_t remaining=32*1024*1024;
    for(const auto* nodes:{&doc.a,&doc.b})for(const auto& n:*nodes)if(n.kind==ModuleKind::vst3){
        for(const auto* bytes:{&n.vst3.component_state,&n.vst3.controller_state}){
            if(bytes->size()>remaining)throw std::invalid_argument("Rack document VST3 state exceeds 32 MiB");
            remaining-=bytes->size();
        }
    }
}
RackDocument default_document(){
    RackDocument doc;doc.a={default_node(ModuleKind::gain,1),default_node(ModuleKind::equalizer,2)};doc.b=doc.a;return doc;
}
bool same_rack(const RackConfig& a,const RackConfig& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)
        if(a[i].id!=b[i].id||a[i].kind!=b[i].kind||a[i].enabled!=b[i].enabled||a[i].value!=b[i].value||!eq_equal(a[i].eq,b[i].eq)||!same_dynamics(a[i].dynamics,b[i].dynamics)||!same_vst3(a[i].vst3,b[i].vst3))return false;
    return true;
}
bool contains_vst3(const RackDocument& doc)noexcept{
    for(const auto* nodes:{&doc.a,&doc.b})for(const auto& n:*nodes)if(n.kind==ModuleKind::vst3)return true;
    return false;
}
bool same_document(const RackDocument& a,const RackDocument& b){return a.bypass==b.bypass&&a.use_b==b.use_b&&same_rack(a.a,b.a)&&same_rack(a.b,b.b);}
namespace {
class RackChain final:public Processor {
public:
    Engine engine;
    void prepare(const Format& f)override{engine.prepare(f);}
    void reset()noexcept override{engine.reset();}
    void process(Block b)noexcept override{engine.process(b);}
    size_t latency_frames()const noexcept override{return engine.latency_frames();}
};
}
struct LiveRack::Graph{
    Engine engine;RackConfig config;std::vector<Slot*> slots;uint64_t revision;
    Graph(const Format& format,const RackConfig& values,uint64_t version,size_t budget,const RackProcessorFactory& factory,bool adaptive=false):config(values),revision(version){
        auto chain=std::make_unique<RackChain>();
        for(const auto& value:values){auto slot=std::make_unique<Slot>(value,factory);slots.push_back(slot.get());chain->engine.add(std::move(slot));}
        if(adaptive)engine.add(std::move(chain));
        else engine.add(std::make_unique<FixedLatency>(std::move(chain),budget));
        engine.prepare(format);
    }
    bool topology(const RackConfig& other)const{
        if(config.size()!=other.size())return false;
        for(size_t i=0;i<other.size();++i)if(config[i].id!=other[i].id||config[i].kind!=other[i].kind||!same_vst3(config[i].vst3,other[i].vst3))return false;
        return true;
    }
    void parameters(const RackConfig& other){
        for(auto* slot:slots)for(const auto& value:other)if(slot->id==value.id&&slot->kind==value.kind){slot->publish(value);break;}
    }
};
struct LiveRack::ControlScope {
    LiveRack& owner;std::unique_lock<std::recursive_mutex> lock;
    explicit ControlScope(LiveRack& value):owner(value),lock(value.control_){
        if((owner.external_busy_&&owner.external_busy_())||owner.foreign_busy_.exchange(true,std::memory_order_acq_rel))
            throw std::logic_error("Rack lifecycle call is already in progress");
    }
    ~ControlScope(){owner.foreign_busy_.store(false,std::memory_order_release);}
};
static_assert(std::atomic<uint64_t>::is_always_lock_free,"Diagnostic counters must be lock-free");
static_assert(std::atomic<unsigned>::is_always_lock_free,"Snapshot state must be lock-free");
static_assert(std::atomic<size_t>::is_always_lock_free,"Retirement indices must be lock-free");
static_assert(std::atomic<void*>::is_always_lock_free,"Graph publication must be lock-free");
LiveRack::LiveRack(const Format& f,const RackConfig& initial,bool initial_bypass,size_t latency_budget,RackProcessorFactory factory,bool adaptive_latency,bool(*external_busy)()noexcept):format_(f),factory_(std::move(factory)),adaptive_latency_(adaptive_latency),latency_budget_(latency_budget),bypass_(initial_bypass?1.f:0.f){
    external_busy_=external_busy;
    validate_rack(initial);
    auto graph=std::make_unique<Graph>(f,initial,++revision_,latency_budget_,factory_,adaptive_latency_);active_=graph.get();owners_.push_back(std::move(graph));
    if(adaptive_latency_)latency_budget_=active_->engine.latency_frames();
    reported_latency_.store(latency_budget_);
    dry_capacity_=adaptive_latency_?size_t(f.sample_rate*2):latency_budget_;
    input_.assign(f.channels*f.max_frames,0);old_=input_;dry_=input_;dry_delay_.assign(f.channels*dry_capacity_,0.f);
    fade_length_=static_cast<size_t>(f.sample_rate*.005);bypass_.prepare(f.sample_rate,5);
    applied_.store(revision_);
}
LiveRack::~LiveRack()=default;
void LiveRack::erase_graph(Graph* graph){
    for(auto it=owners_.begin();it!=owners_.end();++it)if(it->get()==graph){owners_.erase(it);return;}
}
void LiveRack::collect_locked(){
    auto position=read_.load(std::memory_order_relaxed);
    const auto end=write_.load(std::memory_order_acquire);
    while(position!=end){erase_graph(retired_[position]);position=(position+1)%retired_.size();}
    read_.store(position,std::memory_order_release);
}
void LiveRack::collect(){ControlScope scope(*this);collect_locked();}
size_t LiveRack::retained_graphs()const{if(control_busy())return 0;std::lock_guard<std::recursive_mutex> lock(control_);return owners_.size();}
float LiveRack::gain_reduction_db(uint64_t id)const{
    if(control_busy())return 0;
    std::lock_guard<std::recursive_mutex> lock(control_);
    const auto revision=applied_.load(std::memory_order_relaxed);
    for(const auto& graph:owners_)if(graph->revision==revision)
        for(const auto* slot:graph->slots)if(slot->id==id)return slot->gain_reduction_db();
    return 0;
}
void LiveRack::request_snapshot(){
    ControlScope scope(*this);collect_locked();
    if(pending_.load(std::memory_order_acquire))
        throw std::logic_error("Apply pending rack graph before state capture");
    unsigned expected=0;
    if(!snapshot_state_.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
        throw std::logic_error("Rack snapshot already in progress");
}
RackConfig LiveRack::capture_snapshot(){
    ControlScope scope(*this);
    if(snapshot_state_.load(std::memory_order_acquire)!=2)
        throw std::logic_error("Rack audio has not acknowledged snapshot pause");
    auto snapshot=active_->config;
    try{
        for(size_t i=0;i<active_->slots.size();++i)active_->slots[i]->capture(snapshot[i]);
        validate_rack(snapshot);
        active_->engine.prepare(format_);
        if(active_->engine.latency_frames()!=reported_latency_.load())
            throw std::runtime_error("Snapshot changed rack latency");
        active_->config=snapshot;
        snapshot_state_.store(3,std::memory_order_release);
        return snapshot;
    }catch(...){
        // A failed capture must not leave a silently suspended effect.
        // Resume if preparation succeeds; otherwise remain parked on aligned dry.
        try{active_->engine.prepare(format_);
            if(active_->engine.latency_frames()!=reported_latency_.load())throw std::runtime_error("Recovery changed rack latency");
            snapshot_state_.store(3,std::memory_order_release);
        }catch(...){faults_.fetch_add(1,std::memory_order_relaxed);snapshot_state_.store(6,std::memory_order_release);}
        throw;
    }
}
bool LiveRack::editor_dirty()const{
    if(control_busy())return false;
    std::lock_guard<std::recursive_mutex> lock(control_);
    const Graph* latest=nullptr;for(const auto& graph:owners_)if(!latest||graph->revision>latest->revision)latest=graph.get();
    if(latest)for(const auto* slot:latest->slots)if(slot->editor_dirty())return true;
    return false;
}
void LiveRack::open_editor(uint64_t id,bool visible){
    ControlScope scope(*this);collect_locked();
    if(snapshot_busy())throw std::runtime_error("Wait for rack state capture/reconfiguration before opening the plugin editor");
    Graph* latest=nullptr;for(auto& graph:owners_)if(!latest||graph->revision>latest->revision)latest=graph.get();
    if(latest)for(auto* slot:latest->slots)if(slot->id==id){
        if(!slot->open_editor(visible))throw std::runtime_error("Module has no VST3 editor");return;
    }
    throw std::runtime_error("VST3 module is no longer available in this rack");
}
bool LiveRack::service_control(){
    ControlScope scope(*this);collect_locked();
    if(snapshot_busy())return false;
    bool needed=false;
    const auto revision=applied_.load(std::memory_order_acquire);
    for(auto& graph:owners_)if(graph->revision==revision)
        for(auto* slot:graph->slots)needed=slot->service_control()||needed;
    return needed;
}
bool LiveRack::reconfigure_snapshot(){
    ControlScope scope(*this);
    if(snapshot_state_.load(std::memory_order_acquire)!=2)throw std::logic_error("Park audio before reconfiguring VST3");
    for(const auto* slot:active_->slots)if(slot->requires_reload())return false;
    // Reuse the component, controller and original editor. No state round-trip
    // or instance replacement is needed for latency/I/O changes.
    try{
        active_->engine.prepare(format_);
        const auto latency=active_->engine.latency_frames();
        if(!adaptive_latency_&&latency!=reported_latency_.load())throw std::runtime_error("Fixed latency changed during reconfiguration");
        resume_latency_.store(latency,std::memory_order_relaxed);
        snapshot_state_.store(3,std::memory_order_release);return true;
    }catch(...){snapshot_state_.store(6,std::memory_order_release);throw;}
}
RackConfig LiveRack::rebuild_snapshot(const RackConfig* replacement){
    ControlScope scope(*this);
    const auto state=snapshot_state_.load(std::memory_order_acquire);
    if(state!=2&&state!=6)
        throw std::logic_error("Park rack audio before rebuilding VST3 processors");
    auto config=replacement?*replacement:active_->config;
    if(!replacement)for(size_t i=0;i<active_->slots.size();++i)active_->slots[i]->capture(config[i]);
    validate_rack(config);
    auto graph=std::make_unique<Graph>(format_,config,++revision_,adaptive_latency_?0:latency_budget_,factory_,adaptive_latency_);
    for(auto& previous:owners_)for(auto* slot:previous->slots)slot->close_editor();
    auto* next=graph.get();owners_.push_back(std::move(graph));
    if(auto* unused=pending_.exchange(next,std::memory_order_acq_rel))erase_graph(unused);
    snapshot_state_.store(3,std::memory_order_release);
    return config;
}
void LiveRack::cancel_snapshot(){
    ControlScope scope(*this);
    auto state=snapshot_state_.load(std::memory_order_acquire);
    if(state==1){
        if(snapshot_state_.compare_exchange_strong(state,5,std::memory_order_acq_rel))return;
    }
    if(state==2){snapshot_state_.store(3,std::memory_order_release);return;}
    if(state==6){
        active_->engine.prepare(format_);
        if(active_->engine.latency_frames()!=reported_latency_.load())throw std::runtime_error("Recovery changed rack latency");
        snapshot_state_.store(3,std::memory_order_release);return;
    }
}
void LiveRack::publish(const RackConfig& config){
    validate_rack(config);RackConfig snapshot=config;
    ControlScope scope(*this);collect_locked();
    if(snapshot_state_.load(std::memory_order_acquire)!=0)
        throw std::logic_error("Rack edits are paused during state capture");
    Graph* latest=nullptr;
    for(auto& graph:owners_)if(!latest||graph->revision>latest->revision)latest=graph.get();
    if(latest&&latest->topology(config)){
        // Only atomic parameter setters run concurrently with processing.
        for(auto& graph:owners_)graph->parameters(config);
        latest->config=std::move(snapshot);return;
    }
    auto graph=std::make_unique<Graph>(format_,config,++revision_,adaptive_latency_?0:latency_budget_,factory_,adaptive_latency_);
    for(auto& previous:owners_)for(auto* slot:previous->slots)slot->close_editor();
    auto* next=graph.get();owners_.push_back(std::move(graph));
    // Parameter changes to retained IDs preserve state in the currently active chain.
    for(auto& existing:owners_)existing->parameters(config);
    if(auto* unused=pending_.exchange(next,std::memory_order_acq_rel))erase_graph(unused);
}
bool LiveRack::queue_room()const noexcept{
    return (write_.load(std::memory_order_relaxed)+1)%retired_.size()!=read_.load(std::memory_order_acquire);
}
void LiveRack::retire(Graph* graph)noexcept{
    const auto position=write_.load(std::memory_order_relaxed);retired_[position]=graph;
    write_.store((position+1)%retired_.size(),std::memory_order_release);
}
void LiveRack::finish_reconfiguration_audio(Block block,bool quarantined)noexcept{
    if(quarantined!=previous_quarantine_&&have_last_output_){
        reconfiguration_fade_=fade_length_;
        for(size_t c=0;c<block.channels;++c)reconfiguration_anchor_[c]=double(last_output_[c]);
    }
    previous_quarantine_=quarantined;
    for(size_t i=0;i<block.frames&&reconfiguration_fade_;++i){
        const auto mix=double(reconfiguration_fade_)/double(fade_length_);
        for(size_t c=0;c<block.channels;++c){
            const auto value=double(block.planes[c][i])*(1.-mix)+reconfiguration_anchor_[c]*mix;
            block.planes[c][i]=float(std::clamp(value,-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())));
        }
        --reconfiguration_fade_;
    }
    for(size_t c=0;c<block.channels;++c)last_output_[c]=block.planes[c][block.frames-1];
    have_last_output_=true;
}
bool LiveRack::process(Block block,bool allow_graph_switch)noexcept{
    if(block.channels!=format_.channels||block.frames>format_.max_frames)return false;
    if(!block.frames)return true;if(!block.planes)return false;
    for(size_t c=0;c<block.channels;++c)if(!block.planes[c])return false;
    auto snapshot_state=snapshot_state_.load(std::memory_order_acquire);
    if(snapshot_state==3||snapshot_state==5){
        const auto latency=resume_latency_.exchange(SIZE_MAX,std::memory_order_relaxed);
        if(latency!=SIZE_MAX){latency_budget_=latency;reported_latency_.store(latency,std::memory_order_release);}
        snapshot_warm_=latency_budget_;snapshot_state=4;
        snapshot_state_.store(4,std::memory_order_release);
    }
    if(snapshot_state!=2&&snapshot_state!=6&&allow_graph_switch&&!fading_&&queue_room()){
        if(auto* next=pending_.exchange(nullptr,std::memory_order_acq_rel)){
            const auto next_latency=next->engine.latency_frames();
            if(next_latency!=latency_budget_){
                // Different time origins cannot be crossfaded as aligned audio.
                // Switch at a packet boundary, retire on control, fade in the
                // new graph. OBS adapter handles timestamp discontinuity.
                retire(active_);active_=next;latency_budget_=next_latency;
                reported_latency_.store(next_latency,std::memory_order_release);
                applied_.store(active_->revision,std::memory_order_relaxed);
                latency_fade_=fade_length_;
            }else{fading_=active_;active_=next;fade_position_=0;warm_remaining_=latency_budget_;}
        }
    }
    bool bad_input=false;
    for(size_t c=0;c<block.channels;++c)for(size_t i=0;i<block.frames;++i){
        auto& sample=block.planes[c][i];if(!std::isfinite(sample)){sample=0;bad_input=true;}
        input_[c*format_.max_frames+i]=sample;
    }
    if(bad_input)faults_.fetch_add(1,std::memory_order_relaxed);
    // Global bypass and fault fallback share one continuously advancing history.
    for(size_t i=0;i<block.frames;++i){
        for(size_t c=0;c<block.channels;++c){
            const auto index=c*format_.max_frames+i;
            if(dry_capacity_){
                const auto read=(dry_position_+dry_capacity_-latency_budget_)%dry_capacity_;
                dry_[index]=latency_budget_?dry_delay_[c*dry_capacity_+read]:input_[index];
                dry_delay_[c*dry_capacity_+dry_position_]=input_[index];
            }else dry_[index]=input_[index];
        }
        if(dry_capacity_&&++dry_position_==dry_capacity_)dry_position_=0;
    }
    if(snapshot_state==2||snapshot_state==6){
        for(size_t c=0;c<block.channels;++c)
            std::copy_n(dry_.data()+c*format_.max_frames,block.frames,block.planes[c]);
        finish_reconfiguration_audio(block,true);return true;
    }
    bool quarantined=false;
    for(const auto* slot:active_->slots)quarantined=quarantined||!slot->audio_ready();
    if(!quarantined){
        if(!active_->engine.process(block))return false;
        // A restart may arrive concurrently or be raised by this process call.
        for(const auto* slot:active_->slots)quarantined=quarantined||!slot->audio_ready();
    }
    if(quarantined)for(size_t c=0;c<block.channels;++c)
        std::copy_n(dry_.data()+c*format_.max_frames,block.frames,block.planes[c]);
    bool bad_active=false;
    for(size_t c=0;c<block.channels;++c)for(size_t i=0;i<block.frames;++i)if(!std::isfinite(block.planes[c][i]))bad_active=true;
    if(bad_active){
        for(size_t c=0;c<block.channels;++c)std::copy_n(dry_.data()+c*format_.max_frames,block.frames,block.planes[c]);
        active_->engine.reset();faults_.fetch_add(1,std::memory_order_relaxed);
    }
    if(fading_){
        std::array<float*,8> planes{};
        for(size_t c=0;c<block.channels;++c){planes[c]=old_.data()+c*format_.max_frames;std::copy_n(input_.data()+c*format_.max_frames,block.frames,planes[c]);}
        fading_->engine.process({planes.data(),block.channels,block.frames});
        bool bad_old=false;
        for(size_t c=0;c<block.channels;++c)for(size_t i=0;i<block.frames;++i)if(!std::isfinite(planes[c][i]))bad_old=true;
        if(bad_old){
            for(size_t c=0;c<block.channels;++c)std::copy_n(dry_.data()+c*format_.max_frames,block.frames,planes[c]);
            fading_->engine.reset();faults_.fetch_add(1,std::memory_order_relaxed);
        }
        for(size_t i=0;i<block.frames;++i){
            float mix=0;
            if(warm_remaining_)--warm_remaining_;
            else mix=fade_position_>=fade_length_?1.f:float(++fade_position_)/float(fade_length_);
            for(size_t c=0;c<block.channels;++c){
                const float before=planes[c][i];auto& after=block.planes[c][i];after=mix==1?after:static_cast<float>(double(before)+(double(after)-before)*mix);
            }
        }
        if(fade_position_>=fade_length_){retire(fading_);fading_=nullptr;applied_.store(active_->revision,std::memory_order_relaxed);}
    }
    if(latency_fade_)for(size_t i=0;i<block.frames;++i){
        const float mix=1.f-float(latency_fade_)/float(fade_length_);
        for(size_t c=0;c<block.channels;++c)block.planes[c][i]*=mix;
        if(latency_fade_)--latency_fade_;
    }
    bypass_.begin_block();
    for(size_t i=0;i<block.frames;++i){
        const auto mix=bypass_.next();
        for(size_t c=0;c<block.channels;++c){auto& out=block.planes[c][i];const float dry=dry_[c*format_.max_frames+i];out=mix==1?dry:(mix==0?out:static_cast<float>(double(out)+(double(dry)-out)*mix));}
    }
    // Snapshot fades are independent of user bypass. The audio thread only
    // publishes parked after the final plugin call in this block has returned.
    for(size_t i=0;i<block.frames;++i){
        if(snapshot_state==1&&!fading_)
            snapshot_mix_=std::min(1.f,snapshot_mix_+1.f/float(fade_length_));
        else if(snapshot_state==4){
            if(snapshot_warm_)--snapshot_warm_;
            else snapshot_mix_=std::max(0.f,snapshot_mix_-1.f/float(fade_length_));
        }
        if(snapshot_mix_>0)for(size_t c=0;c<block.channels;++c){
            auto& wet=block.planes[c][i];const auto dry=dry_[c*format_.max_frames+i];
            wet=snapshot_mix_==1?dry:static_cast<float>(double(wet)+(double(dry)-wet)*snapshot_mix_);
        }
    }
    if(snapshot_state==1&&!fading_&&snapshot_mix_==1){
        unsigned requested=1;
        snapshot_state_.compare_exchange_strong(requested,2,std::memory_order_release,std::memory_order_relaxed);
    }
    else if(snapshot_state==4&&snapshot_mix_==0)
        snapshot_state_.store(0,std::memory_order_release);
    finish_reconfiguration_audio(block,quarantined);return true;
}
}
