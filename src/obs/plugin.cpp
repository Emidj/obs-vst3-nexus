#include <obs-module.h>
#include "obs/rack_settings.hpp"
#ifdef NEXUS_SINGLE_VST3
#include "obs/single_settings.hpp"
#include "i18n/strings.hpp"
#endif
#ifndef NEXUS_SINGLE_VST3
#include "dsp/loudness.hpp"
#endif
#ifdef NEXUS_OBS_VST3
#include "obs/vst3_runtime.hpp"
#include "obs/audio_timing.hpp"
#include <chrono>
#include <thread>
#include <optional>
#include <condition_variable>
#endif
#ifdef NEXUS_EQ_UI
namespace nexus::ui {void add_single_properties(obs_properties_t*,obs_source_t*);void open_rack_editor(obs_source_t*,const char*);void close_rack_editors();}
#endif
#include <algorithm>
#include <array>
OBS_DECLARE_MODULE()
#ifdef NEXUS_SINGLE_VST3
MODULE_EXPORT void obs_module_set_locale(const char* locale){nexus::i18n::set_locale(locale);}
#endif
MODULE_EXPORT const char* obs_module_description(void){
#ifdef NEXUS_SINGLE_VST3
return "OBS VST3 NEXUS - one VST3 effect per audio filter";
#else
return "OBS Audio Rack NEXUS - native modular audio rack";
#endif
}
namespace {
nexus::RackDocument read_document(obs_data_t* settings){
#ifdef NEXUS_SINGLE_VST3
return nexus::obs_bridge::read_single(settings);
#else
return nexus::obs_bridge::read_rack(settings);
#endif
}
void write_document(obs_data_t* settings,const nexus::RackDocument& document){
#ifdef NEXUS_SINGLE_VST3
nexus::obs_bridge::write_single(settings,document);
#else
nexus::obs_bridge::write_rack(settings,document);
#endif
}
#ifdef NEXUS_OBS_VST3
struct SaveRequest {
    std::mutex mutex;std::condition_variable ready;
    std::atomic<bool> cancelled{false};bool done=false;
    std::shared_ptr<obs_data_t> result{obs_data_create(),obs_data_release};
};
struct RackUpdate {
    nexus::RackDocument document;
    std::string error;
    std::shared_ptr<obs_data_t> settings;
    uint64_t revision=0;
};
#endif
struct Filter {
#ifdef NEXUS_OBS_VST3
    nexus::obs_bridge::Vst3Rack rack;
    nexus::RackDocument accepted_document;
    bool rack_initialized=false;
    bool servicing=false,service_failed=false;
    unsigned service_attempts=0;
    std::chrono::steady_clock::time_point service_deadline{};
    obs_audio_data output{};nexus::obs_bridge::AudioPacketTiming timing;
    std::mutex updates_mutex;
    std::optional<RackUpdate> pending_update;
    bool update_scheduled=false;
    std::weak_ptr<SaveRequest> save_request;
    std::atomic<uint64_t> requested_revision{0},applied_control_revision{0},control_updates{0};
#else
    std::unique_ptr<nexus::LiveRack> rack;
#endif
#ifndef NEXUS_SINGLE_VST3
    std::unique_ptr<nexus::LoudnessMeter> meter;
#endif
    obs_source_t* source=nullptr;
    uint32_t sample_rate=0;
    size_t channels=0;
};
void diagnostics(void* data,calldata_t* output){
    const auto& filter=*static_cast<Filter*>(data);
    calldata_set_int(output,"recovery_events",static_cast<long long>(filter.rack->fault_count()));
#ifdef NEXUS_OBS_VST3
    calldata_set_int(output,"control_revision",static_cast<long long>(filter.applied_control_revision.load()));
    calldata_set_int(output,"control_updates",static_cast<long long>(filter.control_updates.load()));
    calldata_set_bool(output,"control_pending",filter.requested_revision.load()!=filter.applied_control_revision.load());
    calldata_set_bool(output,"snapshot_busy",filter.rack->snapshot_busy());
    calldata_set_bool(output,"control_busy",filter.rack->control_busy());
    calldata_set_bool(output,"editor_dirty",filter.rack->editor_dirty());
    calldata_set_int(output,"latency_frames",static_cast<long long>(filter.rack->latency_frames()));
#endif
}
#ifndef NEXUS_SINGLE_VST3
void loudness(void* data,calldata_t* output){
    const auto& filter=*static_cast<Filter*>(data);
    calldata_set_bool(output,"available",bool(filter.meter));
    if(!filter.meter)return;
    const auto r=filter.meter->read();
    calldata_set_float(output,"momentary",r.momentary);calldata_set_float(output,"short_term",r.short_term);
    calldata_set_float(output,"integrated",r.integrated);calldata_set_float(output,"sample_peak",r.sample_peak);
    calldata_set_bool(output,"momentary_ready",r.momentary_ready);calldata_set_bool(output,"short_term_ready",r.short_term_ready);
    calldata_set_bool(output,"integrated_ready",r.integrated_ready);calldata_set_bool(output,"active",r.active);
}
#endif
void reduction(void* data,calldata_t* output){
    try{
        const auto* raw=calldata_string(output,"id");
        if(!raw){calldata_set_float(output,"db",0);return;}
        const std::string text=raw;size_t used=0;
        const auto id=std::stoull(text,&used);
        if(used!=text.size()||text.empty()||text[0]=='-')return;
        calldata_set_float(output,"db",static_cast<Filter*>(data)->rack->gain_reduction_db(id));
    }catch(const std::exception&){calldata_set_float(output,"db",0);}
}
const char* name(void*){
#ifdef NEXUS_SINGLE_VST3
return "VST 3 NEXUS";
#else
return "Audio Rack NEXUS (development)";
#endif
}
#ifdef NEXUS_OBS_VST3
void apply_update(Filter& filter,RackUpdate request){
    nexus::obs_bridge::require_vst3_control_thread();
    if(request.revision!=filter.requested_revision.load())return;
    std::string error=request.error;
    try{
        if(!error.empty())throw std::invalid_argument(error);
        if(filter.rack->snapshot_busy()||filter.rack->control_busy()){
            if(filter.rack->snapshot_failed()||(filter.service_failed&&filter.rack->snapshot_ready()))filter.rack->rebuild_snapshot(&request.document.active());
            else{
                std::lock_guard<std::mutex> pending(filter.updates_mutex);
                if(request.revision==filter.requested_revision.load()){
                    obs_data_set_bool(request.settings.get(),"rack_pending",true);
                    filter.pending_update=std::move(request);
                }return;
            }
        }else filter.rack->publish(request.document.active());
        filter.rack->set_bypass(request.document.bypass);
        filter.service_failed=false;filter.service_attempts=0;
        filter.accepted_document=std::move(request.document);filter.rack_initialized=true;
        filter.control_updates.fetch_add(1);
    }catch(const std::exception& e){error=e.what();}
    // Serialize completion status with producers publishing their pending flag.
    std::lock_guard<std::mutex> completion(filter.updates_mutex);
    if(request.revision!=filter.requested_revision.load())return;
    if(!error.empty()){
        // Preserve the saved request if initial loading failed: there is no
        // previously accepted document to restore yet. Audio stays dry.
        if(filter.rack_initialized)write_document(request.settings.get(),filter.accepted_document);
        blog(LOG_WARNING,"[Audio Rack NEXUS] Keeping accepted chain: %s",error.c_str());
    }
    obs_data_set_string(request.settings.get(),"rack_error",error.c_str());
    obs_data_set_bool(request.settings.get(),"rack_pending",false);
    filter.applied_control_revision.store(request.revision);
}
void apply_pending(void* data,calldata_t*){
    if(!nexus::obs_bridge::is_vst3_control_thread())return;
    auto& filter=*static_cast<Filter*>(data);std::optional<RackUpdate> pending;
    {std::lock_guard<std::mutex> lock(filter.updates_mutex);
        pending=std::move(filter.pending_update);filter.pending_update.reset();filter.update_scheduled=false;}
    if(pending)apply_update(filter,std::move(*pending));
}
void update(void* data,obs_data_t* settings){
    if(!data)return;auto& filter=*static_cast<Filter*>(data);
    RackUpdate request;
    request.revision=filter.requested_revision.fetch_add(1)+1;
    obs_data_addref(settings);request.settings=std::shared_ptr<obs_data_t>(settings,obs_data_release);
    try{request.document=read_document(settings);}
    catch(const std::exception& e){request.error=e.what();}
    if(nexus::obs_bridge::is_vst3_control_thread()){
        {std::lock_guard<std::mutex> lock(filter.updates_mutex);filter.pending_update.reset();}
        apply_update(filter,std::move(request));return;
    }
    bool schedule=false;
    {std::lock_guard<std::mutex> lock(filter.updates_mutex);
        if(request.revision!=filter.requested_revision.load())return;
        filter.pending_update=std::move(request);
        obs_data_set_bool(settings,"rack_pending",true);
        if(!filter.update_scheduled){filter.update_scheduled=true;schedule=true;}
    }
    if(!schedule)return;
    auto weak=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(filter.source),obs_weak_source_release);
    if(!nexus::obs_bridge::post_vst3_control([weak]{
        if(auto* source=obs_weak_source_get_source(weak.get())){
            calldata_t args;calldata_init(&args);
            proc_handler_call(obs_source_get_proc_handler(source),"nexus_apply_pending",&args);
            calldata_free(&args);obs_source_release(source);
        }
    })){
        std::lock_guard<std::mutex> lock(filter.updates_mutex);
        filter.update_scheduled=false;filter.pending_update.reset();
        obs_data_set_bool(settings,"rack_pending",false);
        obs_data_set_string(settings,"rack_error","VST3 control dispatcher is unavailable");
        filter.applied_control_revision.store(filter.requested_revision.load());
    }
}
void open_vst3_editor(void* data,calldata_t* args){
    try{
        nexus::obs_bridge::require_vst3_control_thread();
        const char* value=calldata_string(args,"id");if(!value)throw std::runtime_error("Missing VST3 module id");
        const std::string text=value;size_t used=0;const auto id=std::stoull(text,&used);
        if(text.empty()||text[0]=='-'||used!=text.size())throw std::runtime_error("Invalid VST3 module id");
        static_cast<Filter*>(data)->rack->open_editor(id,calldata_bool(args,"visible"));calldata_set_string(args,"error","");
    }catch(const std::exception& error){calldata_set_string(args,"error",error.what());}
}
void service_control(void* data,calldata_t*){
    if(!nexus::obs_bridge::is_vst3_control_thread())return;
    auto& filter=*static_cast<Filter*>(data);
    if(filter.rack->control_busy())return;
    try{
        if(filter.servicing){
            if(filter.rack->snapshot_ready()){
                if(++filter.service_attempts>3)throw std::runtime_error("VST3 repeatedly failed; rack parked on dry audio. Change the effect to recover");
                auto document=filter.accepted_document;
                if(!filter.rack->reconfigure_snapshot())document.active()=filter.rack->rebuild_snapshot();
                filter.accepted_document=document;filter.servicing=false;
                auto* settings=obs_source_get_settings(filter.source);
                {std::lock_guard<std::mutex> lock(filter.updates_mutex);
                    if(filter.requested_revision.load()==filter.applied_control_revision.load())
                        write_document(settings,document);
                    obs_data_set_string(settings,"rack_error","");}
                obs_data_release(settings);
            }else if(std::chrono::steady_clock::now()>filter.service_deadline){
                filter.rack->cancel_snapshot();filter.servicing=false;
                throw std::runtime_error("VST3 reconfiguration needs active source audio");
            }
        }else if(!filter.rack->snapshot_busy()){
            apply_pending(data,nullptr);
            if(!filter.service_failed&&filter.rack->service_control()){
                filter.rack->request_snapshot();filter.servicing=true;
                filter.service_deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
            }else if(!filter.service_failed){filter.service_attempts=0;}
        }else if(filter.service_failed&&filter.rack->snapshot_ready())apply_pending(data,nullptr);
    }catch(const std::exception& e){
        filter.servicing=false;filter.service_failed=true;
        auto* settings=obs_source_get_settings(filter.source);
        obs_data_set_string(settings,"rack_error",e.what());obs_data_release(settings);
        blog(LOG_WARNING,"[Audio Rack NEXUS] VST3 service: %s",e.what());
    }
}
#else
void update(void* data,obs_data_t* settings){
    if(!data)return;auto& filter=*static_cast<Filter*>(data);
    try{
        const auto document=read_document(settings);
        filter.rack->publish(document.active());filter.rack->set_bypass(document.bypass);
        obs_data_set_string(settings,"rack_error","");
    }catch(const std::exception& e){
        obs_data_set_string(settings,"rack_error",e.what());
        blog(LOG_WARNING,"[Audio Rack NEXUS] Keeping previous chain: %s",e.what());
    }
}
#endif
#ifdef NEXUS_OBS_VST3
void save(void* data,obs_data_t* settings);
void capture_on_ui(void* data,calldata_t* args){
    auto* request=static_cast<SaveRequest*>(calldata_ptr(args,"request"));
    if(!request||request->cancelled.load())return;
    auto& filter=*static_cast<Filter*>(data);
    // Use isolated result storage: a caller timeout must never allow a late
    // lifecycle callback to modify OBS settings being serialized elsewhere.
    write_document(request->result.get(),filter.accepted_document);
    save(data,request->result.get());
}
void save_from_worker(Filter& filter,obs_data_t* settings){
    auto request=std::make_shared<SaveRequest>();
    const auto revision=filter.requested_revision.load();
    {
        std::lock_guard<std::mutex> lock(filter.updates_mutex);
        if(!filter.save_request.expired()){
            obs_data_set_string(settings,"rack_error","Another VST3 state capture is pending; previous state retained");return;
        }
        filter.save_request=request;
    }
    auto weak=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(filter.source),obs_weak_source_release);
    const bool posted=nexus::obs_bridge::post_vst3_control([weak,request]{
        if(!request->cancelled.load()){
            if(auto* source=obs_weak_source_get_source(weak.get())){
                calldata_t args;calldata_init(&args);calldata_set_ptr(&args,"request",request.get());
                if(!proc_handler_call(obs_source_get_proc_handler(source),"nexus_capture_state",&args))
                    obs_data_set_string(request->result.get(),"rack_error","VST3 capture callback unavailable");
                calldata_free(&args);obs_source_release(source);
            }else obs_data_set_string(request->result.get(),"rack_error","Source disappeared before VST3 capture");
        }
        {std::lock_guard<std::mutex> lock(request->mutex);request->done=true;}
        request->ready.notify_all();
    });
    std::unique_lock<std::mutex> wait(request->mutex);
    const bool completed=posted&&request->ready.wait_for(wait,std::chrono::milliseconds(250),[&]{return request->done;});
    if(!completed)request->cancelled.store(true);
    std::lock_guard<std::mutex> completion(filter.updates_mutex);
    if(!completed){
        obs_data_set_string(settings,"rack_error",posted?
            "VST3 capture timed out waiting for UI; previous saved state retained":"VST3 control dispatcher is unavailable");return;
    }
    const auto* error=obs_data_get_string(request->result.get(),"rack_error");
    if(filter.requested_revision.load()!=revision){
        obs_data_set_string(settings,"rack_error","Rack changed during worker capture; previous saved state retained");return;
    }
    if(!*error)write_document(settings,read_document(request->result.get()));
    obs_data_set_string(settings,"rack_error",error);
}
void save(void* data,obs_data_t* settings){
    if(!data)return;auto& filter=*static_cast<Filter*>(data);bool requested=false;
    if(!nexus::obs_bridge::is_vst3_control_thread()){
        try{save_from_worker(filter,settings);}catch(const std::exception& e){obs_data_set_string(settings,"rack_error",e.what());}
        return;
    }
    try{
        nexus::obs_bridge::require_vst3_control_thread();
        if(filter.rack->control_busy())throw std::runtime_error("VST3 lifecycle call in progress; previous saved state retained");
        if(!filter.rack_initialized)throw std::runtime_error("Initial VST3 load failed; saved plugin state retained");
        if(filter.servicing||filter.service_failed)throw std::runtime_error("VST3 reconfiguration must finish before state capture");
        apply_pending(data,nullptr);
        const auto capture_revision=filter.requested_revision.load();
        if(filter.applied_control_revision.load()!=capture_revision)
            throw std::runtime_error("Pending rack changes must finish before state capture");
        auto document=filter.accepted_document;
        if(!nexus::contains_vst3(document)){
            write_document(settings,document);obs_data_set_string(settings,"rack_error","");return;
        }
        filter.rack->request_snapshot();requested=true;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
        while(!filter.rack->snapshot_ready()){
            if(std::chrono::steady_clock::now()>=deadline)
                throw std::runtime_error("VST3 state capture needs active source audio; previous saved state retained");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        document.active()=filter.rack->capture_snapshot();
        std::lock_guard<std::mutex> completion(filter.updates_mutex);
        if(filter.requested_revision.load()!=capture_revision)
            throw std::runtime_error("Rack changed during state capture; snapshot was not written");
        write_document(settings,document);filter.accepted_document=document;
        obs_data_set_string(settings,"rack_error","");
    }catch(const std::exception& e){
        if(requested)try{nexus::obs_bridge::require_vst3_control_thread();filter.rack->cancel_snapshot();}catch(...){}
        if(filter.rack->snapshot_failed())filter.service_failed=true;
        obs_data_set_string(settings,"rack_error",e.what());
        blog(LOG_WARNING,"[Audio Rack NEXUS] VST3 state not refreshed: %s",e.what());
    }
}
#endif
void defaults(obs_data_t* settings){
    obs_data_set_default_bool(settings,"bypass",false);
#ifndef NEXUS_SINGLE_VST3
    obs_data_set_default_double(settings,"gain_db",0);
#endif
#ifndef NEXUS_SINGLE_VST3
    nexus::obs_bridge::defaults(settings);
#endif
}
#if defined(NEXUS_EQ_UI) && !defined(NEXUS_SINGLE_VST3)
bool open_editor(obs_properties_t*,obs_property_t*,void* data){
    if(data){
        auto* directory=obs_module_config_path("presets");
        nexus::ui::open_rack_editor(static_cast<Filter*>(data)->source,directory);bfree(directory);
    }return false;
}
#endif
obs_properties_t* properties(void* data){
    auto* props=obs_properties_create();obs_properties_add_bool(props,"bypass",
#ifdef NEXUS_SINGLE_VST3
    nexus::i18n::text("Bypass")
#else
    "Bypass"
#endif
    );
#ifdef NEXUS_EQ_UI
    #ifdef NEXUS_SINGLE_VST3
    nexus::ui::add_single_properties(props,data?static_cast<Filter*>(data)->source:nullptr);
#else
    obs_properties_add_button(props,"open_rack_editor","Open Rack Editor",open_editor);
#endif
#endif
    return props;
}
void* create(obs_data_t* settings,obs_source_t* source){
    try{
        auto filter=std::make_unique<Filter>();filter->source=source;
        obs_audio_info info{};if(!obs_get_audio_info(&info))return nullptr;
        filter->sample_rate=info.samples_per_sec;filter->channels=get_audio_channels(info.speakers);
        const auto document=read_document(settings);
#ifdef NEXUS_OBS_VST3
        filter->rack_initialized=false;
        filter->accepted_document={};
        filter->rack=nexus::obs_bridge::make_vst3_rack(
            nexus::Format{double(filter->sample_rate),filter->channels,1024},{},document.bypass);
        obs_data_set_bool(settings,"rack_pending",false);obs_data_set_string(settings,"rack_error","");
#else
        filter->rack=std::make_unique<nexus::LiveRack>(nexus::Format{double(filter->sample_rate),filter->channels,1024},document.active(),document.bypass);
#endif
#ifndef NEXUS_SINGLE_VST3
        if(filter->channels<=2)filter->meter=std::make_unique<nexus::LoudnessMeter>(
            nexus::Format{double(filter->sample_rate),filter->channels,1024});
#endif
#ifdef NEXUS_OBS_VST3
        if(source){
            proc_handler_add(obs_source_get_proc_handler(source),"void nexus_apply_pending()",apply_pending,filter.get());
            proc_handler_add(obs_source_get_proc_handler(source),"void nexus_capture_state(in ptr request)",capture_on_ui,filter.get());
            proc_handler_add(obs_source_get_proc_handler(source),"void nexus_service_control()",service_control,filter.get());
            proc_handler_add(obs_source_get_proc_handler(source),"void nexus_open_vst3_editor(in string id, in bool visible, out string error)",open_vst3_editor,filter.get());
            auto weak=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(source),obs_weak_source_release);
            nexus::obs_bridge::watch_vst3_source([weak]{
                auto* current=obs_weak_source_get_source(weak.get());if(!current)return false;
                calldata_t args;calldata_init(&args);
                proc_handler_call(obs_source_get_proc_handler(current),"nexus_service_control",&args);
                calldata_free(&args);obs_source_release(current);return true;
            });
        }
#endif
        if(source)proc_handler_add(obs_source_get_proc_handler(source),
            "void nexus_gain_reduction(in string id, out float db)",reduction,filter.get());
#ifndef NEXUS_SINGLE_VST3
        if(source)proc_handler_add(obs_source_get_proc_handler(source),
            "void nexus_loudness_status(out bool available, out float momentary, out float short_term, out float integrated, out float sample_peak, out bool momentary_ready, out bool short_term_ready, out bool integrated_ready, out bool active)",loudness,filter.get());
#endif
        if(source)proc_handler_add(obs_source_get_proc_handler(source),"void nexus_rack_status(out int recovery_events)",diagnostics,filter.get());
#ifdef NEXUS_OBS_VST3
        // Register callbacks before posting. The queued task retains only a weak
        // source, never the partly constructed Filter or a blocking UI request.
        update(filter.get(),settings);
#endif
#ifdef NEXUS_SINGLE_VST3
        if(source){
            auto weak_name=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(source),obs_weak_source_release);
            nexus::obs_bridge::post_vst3_control([weak_name]{
                if(auto* current=obs_weak_source_get_source(weak_name.get())){
                    if(std::string(obs_source_get_name(current))=="VST3 NEXUS (development)")obs_source_set_name(current,"VST 3 NEXUS");
                    obs_source_release(current);
                }
            });
        }
#endif
        return filter.release();
    }catch(const std::exception& e){blog(LOG_ERROR,"[Audio Rack NEXUS] Could not prepare rack: %s",e.what());return nullptr;}
}
void destroy(void* data){delete static_cast<Filter*>(data);}
obs_audio_data* audio(void* data,obs_audio_data* block){
    if(!data||!block)return block;auto& filter=*static_cast<Filter*>(data);
    auto* output=obs_get_audio();
    if(!output||audio_output_get_channels(output)!=filter.channels||audio_output_get_sample_rate(output)!=filter.sample_rate)return block;
    std::array<float*,MAX_AV_PLANES> planes{};
    for(size_t c=0;c<filter.channels;++c)if(!block->data[c])return block;
    for(size_t offset=0;offset<block->frames;){
        const size_t frames=(std::min)(size_t(1024),size_t(block->frames)-offset);
        for(size_t c=0;c<filter.channels;++c)planes[c]=reinterpret_cast<float*>(block->data[c])+offset;
#ifdef NEXUS_OBS_VST3
        filter.rack->process({planes.data(),filter.channels,frames},offset==0);
#else
        filter.rack->process({planes.data(),filter.channels,frames});
#endif
#ifndef NEXUS_SINGLE_VST3
        if(filter.meter)filter.meter->process({planes.data(),filter.channels,frames});
#endif
        offset+=frames;
    }
#ifdef NEXUS_OBS_VST3
#ifdef NEXUS_SINGLE_VST3
    // Keep OBS capture timing untouched. The single-effect host must never
    // discard a processed packet because a device clock has jittered.
    return block;
#else
    const auto latency=filter.rack->latency_frames();
    filter.output=*block;
    if(!filter.timing.compensate(block->timestamp,block->frames,latency,filter.sample_rate,filter.output.timestamp))return nullptr;
    return &filter.output;
#endif
#else
    return block;
#endif
}
}
bool obs_module_load(void){
#ifdef NEXUS_OBS_VST3
    if(!nexus::obs_bridge::initialize_vst3_runtime())return false;
#endif
    obs_source_info info{};
#ifdef NEXUS_SINGLE_VST3
    info.id="nexus_vst3";
#else
    info.id="nexus_audio_rack";
#endif
    info.type=OBS_SOURCE_TYPE_FILTER;info.output_flags=OBS_SOURCE_AUDIO;
    info.get_name=name;info.create=create;info.destroy=destroy;info.get_defaults=defaults;info.get_properties=properties;
#ifdef NEXUS_OBS_VST3
    info.save=save;
#endif
    info.update=update;info.filter_audio=audio;obs_register_source(&info);return true;
}
#ifdef NEXUS_EQ_UI
void obs_module_unload(void){
    nexus::ui::close_rack_editors();
#ifdef NEXUS_OBS_VST3
    nexus::obs_bridge::shutdown_vst3_runtime();
#endif
}
#endif