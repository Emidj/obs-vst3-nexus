#include "vst3/core.hpp"
#include "vst3/editor.hpp"
#include "vst3/bounded.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <thread>
namespace nexus::vst3 {
namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;
using detail::Stream;
void demand(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool effect_class(const VST3::Hosting::ClassInfo& info){
    return effect_candidate(info.category(),info.subCategoriesString());
}
std::string absolute_path(const std::string& input){
    demand(!input.empty()&&input.find(char(0))==std::string::npos,"Invalid VST3 module path");
    auto p=std::filesystem::absolute(std::filesystem::u8path(input)).lexically_normal();
    auto extension=p.extension().u8string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    demand(extension==".vst3","Only VST3 modules are accepted");
    demand(std::filesystem::exists(p),"VST3 module does not exist");return p.u8string();
}
VST3::Hosting::Module::Ptr open_module(const std::string& path){
    std::string error;auto module=Module::create(absolute_path(path),error);
    if(!module)throw std::runtime_error(error);
    demand(module->getFactory().classCount()<=4096,"Too many VST3 classes");return module;
}
thread_local bool rendering=false;
class Application final:public HostApplication {
public:
    Application(){
        // Do not advertise MIDI support or program/unit handling that is absent.
        auto* support=getPlugInterfaceSupport();
        support->removePlugInterfaceSupported(IMidiMapping::iid);
        support->removePlugInterfaceSupported(IUnitInfo::iid);support->removePlugInterfaceSupported(IUnitData::iid);
        support->removePlugInterfaceSupported(IProgramListData::iid);support->removePlugInterfaceSupported(IEditController2::iid);
    }
    std::atomic<uint64_t> rejected{0};
    tresult PLUGIN_API getName(String128 name)override{
        constexpr char16_t label[]=u"OBS Audio Rack NEXUS";
        std::fill_n(name,128,0);std::copy(std::begin(label),std::end(label),name);return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid,TUID interfaceID,void** object)override{
        if(!object)return kInvalidArgument;
        if(rendering){*object=nullptr;rejected.fetch_add(1,std::memory_order_relaxed);return kOutOfMemory;}
        return HostApplication::createInstance(cid,interfaceID,object);
    }
};
class ControlConnection final:public ConnectionProxy {
public:
    ControlConnection(IConnectionPoint* source,Application& app):ConnectionProxy(source),app_(app),control_(std::this_thread::get_id()){}
    tresult PLUGIN_API notify(IMessage* message)override{
        // ivstmessage.h explicitly requires UI-thread & Connected. Retaining
        // a mutable IMessage for later delivery would not preserve its value.
        if(rendering||std::this_thread::get_id()!=control_){
            app_.rejected.fetch_add(1,std::memory_order_relaxed);return kResultFalse;
        }
        return ConnectionProxy::notify(message);
    }
private:Application& app_;const std::thread::id control_;
};
Application& factory_application(){static Application app;return app;}
}
std::vector<std::string> installed_paths(){return VST3::Hosting::Module::getModulePaths();}
std::vector<ClassInfo> inspect(const std::string& path){
    auto module=open_module(path);module->getFactory().setHostContext(&factory_application());std::vector<ClassInfo> result;
    for(const auto& info:module->getFactory().classInfos())
        result.push_back({module->getPath(),info.ID().toString(),info.name(),info.vendor(),info.version(),info.category(),info.subCategoriesString(),effect_class(info)});
    return result;
}
struct Effect::Impl {
    using SdkInfo=VST3::Hosting::ClassInfo;
    struct Handler final:detail::Borrowed<IComponentHandler>,IComponentHandler2 {
        Impl* owner=nullptr;
        tresult PLUGIN_API queryInterface(const TUID iid,void** out)override{
            if(!out)return kInvalidArgument;
            if(FUnknownPrivate::iidEqual(iid,IComponentHandler2::iid)){*out=static_cast<IComponentHandler2*>(this);addRef();return kResultOk;}
            return detail::Borrowed<IComponentHandler>::queryInterface(iid,out);
        }
        uint32 PLUGIN_API addRef()override{return 1;}
        uint32 PLUGIN_API release()override{return 1;}
        tresult PLUGIN_API setDirty(TBool dirty)override{
            if(rendering||std::this_thread::get_id()!=owner->control_thread){owner->app.rejected.fetch_add(1);return kResultFalse;}
            // A clean hint cannot acknowledge data which the host has not saved.
            if(dirty)owner->editor_edits.fetch_add(1,std::memory_order_release);
            return kResultOk;
        }
        tresult PLUGIN_API requestOpenEditor(FIDString)override{return kNotImplemented;}
        tresult PLUGIN_API startGroupEdit()override{return kNotImplemented;}
        tresult PLUGIN_API finishGroupEdit()override{return kNotImplemented;}
        tresult PLUGIN_API beginEdit(ParamID)override{return kResultOk;}
        tresult PLUGIN_API endEdit(ParamID)override{return kResultOk;}
        tresult PLUGIN_API performEdit(ParamID id,ParamValue value)override{if(!owner->set(id,value))return kResultFalse;owner->editor_edits.fetch_add(1);return kResultOk;}
        tresult PLUGIN_API restartComponent(int32 flags)override{
            owner->restart.fetch_or(uint32_t(flags),std::memory_order_release);
            constexpr uint32_t notifications=kParamValuesChanged|kParamTitlesChanged|kIoTitlesChanged;
            if(uint32_t(flags)&~notifications)owner->reprepare.store(true,std::memory_order_release);
            if(flags&kParamValuesChanged)owner->editor_edits.fetch_add(1,std::memory_order_release);
            return kResultOk;
        }
    };
    VST3::Hosting::Module::Ptr module;
    Application app;
    Handler handler;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> controller;
    IPtr<ControlConnection> component_proxy,controller_proxy;
    SdkInfo info;std::unique_ptr<EditorWindow> editor;
    std::vector<ParameterInfo> metadata;
    std::unique_ptr<std::atomic<double>[]> targets,observed;
    std::vector<double> applied,controller_applied;
    const std::thread::id control_thread=std::this_thread::get_id();
    detail::Changes input_changes,output_changes;
    HostProcessData data;
    ProcessContext context{};
    Format format{};
    bool component_initialized=false,controller_initialized=false,single=false,active=false,processing=false,unusable=false;
    size_t latency=0;int64 cursor=0;int32 sample_size=kSample32;
    std::atomic<bool> reprepare{false};
    std::atomic<uint32_t> restart{0};
    std::atomic<uint64_t> errors{0},dropped{0},editor_edits{0},captured_edits{0};
    Impl(){handler.owner=this;}
    ~Impl(){
        editor.reset();
        stop();
        if(controller)controller->setComponentHandler(nullptr);
        if(component_proxy)component_proxy->disconnect();
        if(controller_proxy)controller_proxy->disconnect();
        component_proxy.reset();controller_proxy.reset();
        if(controller_initialized&&controller)controller->terminate();
        if(component_initialized&&component)component->terminate();
        processor.reset();controller.reset();component.reset();
    }
    void require_control()const{demand(std::this_thread::get_id()==control_thread,"VST3 lifecycle requires its owning control thread");}
    void stop(){
        if(processing&&processor){processor->setProcessing(false);processing=false;}
        if(active&&component){component->setActive(false);active=false;}
    }
    void load(const std::string& path,const std::string& id){
        module=open_module(path);bool found=false;
        demand(id.size()==32&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');}),"Invalid VST3 class ID");
        const auto uid=VST3::UID::fromString(id);demand(bool(uid),"Invalid VST3 class ID");
        for(const auto& item:module->getFactory().classInfos())if(item.ID()==*uid){info=item;found=true;break;}
        demand(found&&effect_class(info),"Class is not a supported VST3 effect");
        module->getFactory().setHostContext(&factory_application());
        component=module->getFactory().createInstance<IComponent>(info.ID());demand(bool(component),"Cannot create VST3 component");
        demand(component->initialize(&app)==kResultOk,"VST3 component initialization failed");component_initialized=true;
        processor=U::cast<IAudioProcessor>(component);demand(bool(processor),"VST3 class has no audio processor");
        controller=U::cast<IEditController>(component);single=bool(controller);
        if(!controller){
            TUID controllerID{};if(component->getControllerClassId(controllerID)==kResultOk){
                controller=module->getFactory().createInstance<IEditController>(VST3::UID(controllerID));
                demand(bool(controller),"Cannot create VST3 controller");
                demand(controller->initialize(&app)==kResultOk,"VST3 controller initialization failed");controller_initialized=true;
            }
        }
        if(controller){
            demand(controller->setComponentHandler(&handler)==kResultOk,"VST3 controller rejected host handler");
            if(!single){
                auto a=U::cast<IConnectionPoint>(component),b=U::cast<IConnectionPoint>(controller);
                if(a&&b){
                    component_proxy=owned(new ControlConnection(a,app));controller_proxy=owned(new ControlConnection(b,app));
                    demand(component_proxy->connect(b)==kResultOk&&controller_proxy->connect(a)==kResultOk,"VST3 component/controller connection failed");
                }
            }
            // Seed the controller from processor state before reading parameters.
            Stream stream;
            if(component->getState(&stream)==kResultOk&&!stream.failed){
                stream.position=0;controller->setComponentState(&stream);
            }
        }
        read_metadata();
    }
    void read_metadata(){
        const auto count=controller?controller->getParameterCount():0;
        demand(count>=0&&count<=4096,"VST3 parameter count exceeds bounded core");
        metadata.clear();metadata.reserve(size_t(count));
        for(int32 i=0;i<count;++i){
            Steinberg::Vst::ParameterInfo p{};demand(controller->getParameterInfo(i,p)==kResultOk,"Cannot read VST3 parameter");
            const double value=controller->getParamNormalized(p.id);
            demand(std::isfinite(value)&&value>=0&&value<=1&&p.stepCount>=0,"Invalid VST3 parameter metadata");
            metadata.push_back({p.id,StringConvert::convert(p.title,128),StringConvert::convert(p.units,128),p.stepCount,bool(p.flags&Steinberg::Vst::ParameterInfo::kIsReadOnly)});
        }
        std::sort(metadata.begin(),metadata.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        for(size_t i=1;i<metadata.size();++i)demand(metadata[i-1].id!=metadata[i].id,"Duplicate VST3 parameter ID");
        targets=std::make_unique<std::atomic<double>[]>(metadata.size());observed=std::make_unique<std::atomic<double>[]>(metadata.size());
        applied.assign(metadata.size(),std::numeric_limits<double>::quiet_NaN());
        controller_applied=applied;
        std::vector<uint32_t> ids;ids.reserve(metadata.size());
        for(size_t i=0;i<metadata.size();++i){
            const double value=controller->getParamNormalized(metadata[i].id);
            demand(std::isfinite(value)&&value>=0&&value<=1,"VST3 parameter changed to an invalid value");
            targets[i].store(value);observed[i].store(value);controller_applied[i]=value;ids.push_back(metadata[i].id);
        }
        input_changes.setup(ids);output_changes.setup(ids);
    }
    size_t index(uint32_t id)const noexcept{
        const auto it=std::lower_bound(metadata.begin(),metadata.end(),id,[](const auto& p,uint32_t v){return p.id<v;});
        return it==metadata.end()||it->id!=id?metadata.size():size_t(it-metadata.begin());
    }
    bool set(uint32_t id,double value)noexcept{
        if(!std::isfinite(value)||value<0||value>1)return false;
        const auto i=index(id);if(i==metadata.size()||metadata[i].read_only)return false;
        targets[i].store(value,std::memory_order_relaxed);return true;
    }
    void setup(const Format& f){
        demand(!unusable,"VST3 state restore failed: recreate this instance");
        demand(std::isfinite(f.sample_rate)&&f.sample_rate>=8000&&f.sample_rate<=192000&&
            (f.channels==1||f.channels==2)&&f.max_frames>0&&f.max_frames<=65536,"VST3 core requires mono/stereo and a valid format");
        stop();format=f;data.unprepare();
        const auto requested=restart.load(std::memory_order_acquire);
        demand(!(requested&kReloadComponent),"VST3 requested component reload: recreate instance");
        if(requested){
            struct Previous {uint32_t id;double target,observed;};
            std::vector<Previous> previous;previous.reserve(metadata.size());
            for(size_t i=0;i<metadata.size();++i)previous.push_back({metadata[i].id,targets[i].load(),observed[i].load()});
            read_metadata();
            if(!(requested&kParamValuesChanged))for(const auto& old:previous){
                const auto i=index(old.id);if(i<metadata.size()&&!metadata[i].read_only){
                    targets[i].store(old.target);observed[i].store(old.observed);
                }
            }
        }
        sample_size=processor->canProcessSampleSize(kSample32)==kResultOk?kSample32:kSample64;
        demand(processor->canProcessSampleSize(sample_size)==kResultOk,"VST3 supports neither float32 nor float64 processing");
        const auto main=f.channels==1?SpeakerArr::kMono:SpeakerArr::kStereo;
        std::array<SpeakerArrangement,8> in{},out{};
        const int32 nIn=component->getBusCount(kAudio,kInput),nOut=component->getBusCount(kAudio,kOutput);
        demand(nIn>=1&&nOut>=1&&nIn<=8&&nOut<=8,"Unsupported VST3 bus count");
        for(auto direction:{kInput,kOutput}){
            const auto count=direction==kInput?nIn:nOut;
            for(int32 i=0;i<count;++i){
                BusInfo bus{};demand(component->getBusInfo(kAudio,direction,i,bus)==kResultOk,"Cannot query VST3 bus");
                demand(bus.channelCount>0&&bus.channelCount<=8,"VST3 bus exceeds bounded channel count");
                demand((i==0&&bus.busType==kMain)||(i>0&&bus.busType==kAux),"Unsupported VST3 main bus routing");
                demand(!(bus.flags&BusInfo::kIsControlVoltage),"Control-voltage buses are not supported");
                auto& arrangement=direction==kInput?in[size_t(i)]:out[size_t(i)];
                demand(processor->getBusArrangement(direction,i,arrangement)==kResultOk,"Cannot query VST3 speaker arrangement");
            }
            const int32 eventCount=component->getBusCount(kEvent,direction);
            demand(eventCount>=0&&eventCount<=64,"Unsupported VST3 event bus count");
            for(int32 i=0;i<eventCount;++i)demand(component->activateBus(kEvent,direction,i,false)==kResultOk,"Cannot disable VST3 event bus");
        }
        in[0]=out[0]=main;
        demand(processor->setBusArrangements(in.data(),nIn,out.data(),nOut)==kResultOk,"VST3 rejected requested mono/stereo arrangement");
        for(auto direction:{kInput,kOutput}){
            for(int32 i=0;i<(direction==kInput?nIn:nOut);++i){
                BusInfo bus{};demand(component->getBusInfo(kAudio,direction,i,bus)==kResultOk&&bus.channelCount>0&&bus.channelCount<=8,"VST3 changed bus layout unexpectedly");
                if(i==0)demand(size_t(bus.channelCount)==f.channels,"VST3 returned a different main channel count");
                demand(component->activateBus(kAudio,direction,i,i==0)==kResultOk,"VST3 bus activation failed");
            }
        }
        ProcessSetup setup{};setup.processMode=kRealtime;setup.symbolicSampleSize=sample_size;setup.maxSamplesPerBlock=int32(f.max_frames);setup.sampleRate=f.sample_rate;
        demand(processor->setupProcessing(setup)==kResultOk,"VST3 setupProcessing failed");
        demand(data.prepare(*component,int32(f.max_frames),sample_size),"Cannot allocate VST3 bus buffers");
        data.processMode=kRealtime;data.inputParameterChanges=&input_changes;data.outputParameterChanges=&output_changes;
        data.inputEvents=nullptr;data.outputEvents=nullptr;data.processContext=&context;
        active=true;demand(component->setActive(true)==kResultOk,"VST3 setActive failed");
        processing=true;const auto result=processor->setProcessing(true);
        demand(result==kResultOk||result==kNotImplemented,"VST3 setProcessing failed");
        demand(component->getBusCount(kAudio,kInput)==nIn&&component->getBusCount(kAudio,kOutput)==nOut,"VST3 changed buses during activation");
        for(auto direction:{kInput,kOutput})for(int32 i=0;i<(direction==kInput?nIn:nOut);++i){
            BusInfo bus{};SpeakerArrangement arrangement=0;
            const auto& buffer=direction==kInput?data.inputs[i]:data.outputs[i];
            demand(component->getBusInfo(kAudio,direction,i,bus)==kResultOk&&
                bus.channelCount==buffer.numChannels&&!(bus.flags&BusInfo::kIsControlVoltage),"VST3 changed channel layout during activation");
            demand(processor->getBusArrangement(direction,i,arrangement)==kResultOk,"Cannot verify activated VST3 arrangement");
            if(i==0)demand(arrangement==main,"VST3 activated an unexpected speaker layout");
        }
        latency=processor->getLatencySamples();demand(latency<=size_t(f.sample_rate*2),"VST3 latency exceeds two-second core budget");
        for(auto& value:applied)value=std::numeric_limits<double>::quiet_NaN();
        cursor=0;restart.store(0,std::memory_order_release);reprepare.store(false,std::memory_order_release);
    }
    void service(){
        demand(std::this_thread::get_id()==control_thread,"VST3 controller service requires its lifecycle thread");
        constexpr uint32_t notifications=kParamValuesChanged|kParamTitlesChanged|kIoTitlesChanged;
        const auto notice=restart.fetch_and(~notifications,std::memory_order_acq_rel)&notifications;
        // Presets can replace controller values without individual performEdit calls.
        // Import them before feeding processor observations back to the controller.
        if(controller&&(notice&kParamValuesChanged))for(size_t i=0;i<metadata.size();++i){
            const auto value=controller->getParamNormalized(metadata[i].id);
            demand(std::isfinite(value)&&value>=0&&value<=1,"Invalid VST3 preset parameter");
            if(!metadata[i].read_only)targets[i].store(value,std::memory_order_relaxed);
            observed[i].store(value,std::memory_order_relaxed);controller_applied[i]=value;
        }
        // Parameter/bus titles are not displayed by this host. They do not
        // require stopping audio or reconstructing the plugin.
        if(controller)for(size_t i=0;i<metadata.size();++i){
            const double value=observed[i].load(std::memory_order_relaxed);
            if(value!=controller_applied[i]&&targets[i].load(std::memory_order_relaxed)==value){
                controller->setParamNormalized(metadata[i].id,value);controller_applied[i]=value;
            }
        }
    }
    void fault()noexcept{errors.fetch_add(1,std::memory_order_relaxed);reprepare.store(true,std::memory_order_release);}
    void render(Block block)noexcept{
        if(block.frames==0)return;
        if(block.channels!=format.channels||block.frames>format.max_frames||!block.planes){fault();return;}
        for(size_t c=0;c<block.channels;++c)if(!block.planes[c]){fault();return;}
        auto silence=[&]{for(size_t c=0;c<block.channels;++c)std::fill_n(block.planes[c],block.frames,0.f);};
        if(!processing||reprepare.load(std::memory_order_acquire)){silence();return;}
        for(size_t c=0;c<block.channels;++c)for(size_t i=0;i<block.frames;++i)if(!std::isfinite(block.planes[c][i])){fault();silence();return;}
        data.numSamples=int32(block.frames);input_changes.clear(data.numSamples);output_changes.clear(data.numSamples);
        for(size_t i=0;i<metadata.size();++i)if(!metadata[i].read_only){
            const double value=targets[i].load(std::memory_order_relaxed);
            if(value!=applied[i]){
                int32 queueIndex=0,point=0;
                auto* queue=input_changes.addParameterData(metadata[i].id,queueIndex);
                if(queue)queue->addPoint(0,value,point);
                applied[i]=value;observed[i].store(value,std::memory_order_relaxed);
            }
        }
        for(int32 b=0;b<data.numInputs;++b){
            auto& bus=data.inputs[b];bus.silenceFlags=b?HostProcessData::kAllChannelsSilent:0;
            for(int32 c=0;c<bus.numChannels;++c){
                if(sample_size==kSample64){
                    if(b==0)std::copy_n(block.planes[size_t(c)],block.frames,bus.channelBuffers64[c]);
                    else std::fill_n(bus.channelBuffers64[c],block.frames,0.);
                }else{
                    if(b==0)std::copy_n(block.planes[size_t(c)],block.frames,bus.channelBuffers32[c]);
                    else std::fill_n(bus.channelBuffers32[c],block.frames,0.f);
                }
            }
        }
        for(int32 b=0;b<data.numOutputs;++b){
            auto& bus=data.outputs[b];bus.silenceFlags=0;
            for(int32 c=0;c<bus.numChannels;++c){
                if(sample_size==kSample64)std::fill_n(bus.channelBuffers64[c],block.frames,0.);
                else std::fill_n(bus.channelBuffers32[c],block.frames,0.f);
            }
        }
        context={};context.sampleRate=format.sample_rate;context.projectTimeSamples=cursor;context.continousTimeSamples=cursor;
        context.state=ProcessContext::kPlaying|ProcessContext::kContTimeValid;
        tresult result=kInternalError;
        const bool wasRendering=rendering;rendering=true;
        try{result=processor->process(data);}catch(...){rendering=wasRendering;fault();silence();return;}
        rendering=wasRendering;
        cursor+=int64(block.frames);
        dropped.fetch_add(input_changes.drops()+output_changes.drops(),std::memory_order_relaxed);
        if(result!=kResultOk||reprepare.load(std::memory_order_acquire)){fault();silence();return;}
        for(size_t c=0;c<block.channels;++c){
            if(data.outputs[0].silenceFlags&(uint64(1)<<c))std::fill_n(block.planes[c],block.frames,0.f);
            else for(size_t i=0;i<block.frames;++i){
                const double value=sample_size==kSample64?data.outputs[0].channelBuffers64[c][i]:data.outputs[0].channelBuffers32[c][i];
                if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max()){fault();silence();return;}
                block.planes[c][i]=float(value);
            }
        }
        for(const auto i:output_changes.used){
            auto& q=output_changes.queues[i];if(q.count){
                const double value=q.points[size_t(q.count-1)].second;
                observed[i].store(value,std::memory_order_relaxed);
                if(!metadata[i].read_only){
                    double expected=applied[i];
                    targets[i].compare_exchange_strong(expected,value,std::memory_order_relaxed);
                    applied[i]=value;
                }
            }
        }
    }
};
Effect::Effect(const std::string& path,const std::string& id):impl_(std::make_unique<Impl>()){impl_->load(path,id);}
Effect::~Effect()=default;
void Effect::prepare(const Format& format){
    impl_->require_control();
    try{impl_->setup(format);}catch(...){impl_->stop();impl_->reprepare.store(true);throw;}
}
void Effect::reset()noexcept{impl_->reprepare.store(true,std::memory_order_release);}
void Effect::process(Block block)noexcept{impl_->render(block);}
size_t Effect::latency_frames()const noexcept{return impl_->latency;}
void Effect::suspend(){impl_->require_control();impl_->stop();}
State Effect::capture_state(){
    impl_->require_control();
    demand(!impl_->active&&!impl_->processing,"Suspend VST3 audio before saving state");
    impl_->service();
    const auto edit=impl_->editor_edits.load();
    Stream component;demand(impl_->component->getState(&component)==kResultOk&&!component.failed,"Cannot capture VST3 component state");
    Stream controller;
    if(impl_->controller){
        const auto result=impl_->controller->getState(&controller);
        demand((result==kResultOk||result==kNotImplemented)&&!controller.failed,"Cannot capture VST3 controller state");
        if(result==kNotImplemented)controller.data.clear();
    }
    impl_->captured_edits.store(edit);
    return {impl_->info.ID().toString(),std::move(component.data),std::move(controller.data)};
}
void Effect::restore_state(const State& state){
    impl_->require_control();
    demand(!impl_->active&&!impl_->processing,"Suspend VST3 audio before restoring state");
    demand(state.class_id==impl_->info.ID().toString(),"VST3 state belongs to a different class");
    Stream component(state.component),controller(state.controller);
    try{
        demand(impl_->component->setState(&component)==kResultOk,"VST3 rejected component state");
        if(impl_->controller){
            component.position=0;demand(impl_->controller->setComponentState(&component)==kResultOk,"VST3 controller rejected component state");
            if(!state.controller.empty())demand(impl_->controller->setState(&controller)==kResultOk,"VST3 rejected controller state");
        }
        impl_->read_metadata();impl_->captured_edits.store(impl_->editor_edits.load());impl_->reprepare.store(true);
    }catch(...){impl_->unusable=true;impl_->reprepare.store(true);throw;}
}
const std::vector<ParameterInfo>& Effect::parameters()const noexcept{return impl_->metadata;}
bool Effect::set_parameter(uint32_t id,double value)noexcept{return impl_->set(id,value);}
double Effect::read_parameter(uint32_t id)const noexcept{
    const auto i=impl_->index(id);return i==impl_->metadata.size()?std::numeric_limits<double>::quiet_NaN():impl_->observed[i].load(std::memory_order_relaxed);
}
void Effect::service_control(){impl_->service();}
void Effect::open_editor(bool visible){
    impl_->require_control();
    impl_->service();
    if(impl_->controller)for(size_t i=0;i<impl_->metadata.size();++i)if(!impl_->metadata[i].read_only){
        const auto target=impl_->targets[i].load(std::memory_order_relaxed);
        impl_->controller->setParamNormalized(impl_->metadata[i].id,target);impl_->controller_applied[i]=target;
    }
    if(impl_->editor&&impl_->editor->handle()){if(visible)impl_->editor->show();return;}
    impl_->editor.reset();impl_->editor=std::make_unique<EditorWindow>(impl_->controller.get(),impl_->info.name(),visible);
}
void Effect::close_editor(){impl_->require_control();impl_->editor.reset();}
bool Effect::editor_dirty()const noexcept{return impl_->editor_edits.load()!=impl_->captured_edits.load();}
void* Effect::editor_window()const noexcept{return impl_->editor?impl_->editor->handle():nullptr;}
bool Effect::needs_reprepare()const noexcept{return impl_->reprepare.load(std::memory_order_acquire);}
uint64_t Effect::faults()const noexcept{return impl_->errors.load(std::memory_order_relaxed);}
uint64_t Effect::dropped_points()const noexcept{return impl_->dropped.load(std::memory_order_relaxed);}
uint64_t Effect::rejected_host_requests()const noexcept{return impl_->app.rejected.load(std::memory_order_relaxed);}
static_assert(std::atomic<double>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free,"VST3 core requires lock-free telemetry and parameters");
}

namespace nexus::vst3 { bool Effect::requires_reload()const noexcept{return impl_->unusable||(impl_->restart.load(std::memory_order_acquire)&Steinberg::Vst::kReloadComponent)!=0;} }
