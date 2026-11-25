#include "vst3/rack_factory.hpp"
#include "vst3/core.hpp"
#include <stdexcept>
namespace nexus::vst3 {
namespace {
class RackEffect final:public RackStatefulProcessor {
public:
    explicit RackEffect(const Vst3Settings& settings):effect_(settings.path,settings.class_id),settings_(settings){
        if(settings.has_state)effect_.restore_state({settings.class_id,settings.component_state,settings.controller_state});
    }
    void prepare(const Format& format)override{
        effect_.prepare(format);
        for(const auto& parameter:settings_.parameters)
            if(!effect_.set_parameter(parameter.first,parameter.second))
                throw std::invalid_argument("Unknown or read-only VST3 rack parameter");
        // Scene overrides apply once, not again after a preset reconfiguration.
        settings_.parameters.clear();
    }
    Vst3Settings capture_vst3()override{
        effect_.suspend();
        const auto state=effect_.capture_state();
        auto captured=settings_;
        captured.has_state=true;
        captured.component_state=state.component;captured.controller_state=state.controller;
        // The opaque state contains applied values, including processor feedback.
        captured.parameters.clear();settings_=captured;
        return captured;
    }
    bool service_control()override{effect_.service_control();return effect_.needs_reprepare();}
    bool audio_ready()const noexcept override{return !effect_.needs_reprepare();}
    bool requires_reload()const noexcept override{return effect_.requires_reload();}
    bool editor_dirty()const noexcept override{return effect_.editor_dirty();}
    bool open_editor(bool visible)override{effect_.open_editor(visible);return true;}
    void close_editor()override{effect_.close_editor();}
    void reset()noexcept override{effect_.reset();}
    void process(Block block)noexcept override{effect_.process(block);}
    size_t latency_frames()const noexcept override{return effect_.latency_frames();}
private:
    Effect effect_;
    Vst3Settings settings_;
};
}
RackProcessorFactory rack_factory(){
    return [](const RackNode& node)->std::unique_ptr<Processor>{
        if(node.kind!=ModuleKind::vst3)throw std::invalid_argument("VST3 factory requires a VST3 rack node");
        return std::make_unique<RackEffect>(node.vst3);
    };
}
}
