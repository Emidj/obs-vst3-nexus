#include <obs.h>
#include <iostream>
#include <windows.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <filesystem>
namespace {
const char* input_name(void*) { return "NEXUS test signal"; }
void* input_create(obs_data_t*,obs_source_t*) { return reinterpret_cast<void*>(1); }
void input_destroy(void*) {}
struct Capture {
    std::array<float,1024> left{},right{};
    unsigned calls=0;
    bool equal=true;
};
void captured(void* p,obs_source_t*,const audio_data* audio,bool) {
    auto& state=*static_cast<Capture*>(p); ++state.calls;
    if(audio->frames!=1024 || !audio->data[0] || !audio->data[1]) {state.equal=false;return;}
    const auto* l=reinterpret_cast<const float*>(audio->data[0]);
    const auto* r=reinterpret_cast<const float*>(audio->data[1]);
    for(size_t i=0;i<1024;++i)
        if(l[i]!=state.left[i] || r[i]!=state.right[i]) state.equal=false;
}
bool pipeline_and_state() {
    obs_source_info info{};info.id="nexus_test_signal";info.type=OBS_SOURCE_TYPE_INPUT;
    info.output_flags=OBS_SOURCE_AUDIO;info.get_name=input_name;info.create=input_create;info.destroy=input_destroy;
    obs_register_source(&info);
    auto* settings=obs_data_create();
    auto* filter=obs_source_create_private("nexus_audio_rack","pipeline filter",settings);
    auto* source=obs_source_create_private("nexus_test_signal","pipeline signal",nullptr);
    if(!filter || !source) {
        if(filter)obs_source_release(filter);
        if(source)obs_source_release(source);
        obs_data_release(settings);return false;
    }
    obs_source_filter_add(source,filter);
    Capture capture;
    obs_source_add_audio_capture_callback(source,captured,&capture);
    const auto time=os_gettime_ns();
    for(size_t block=0;block<4;++block) {
        obs_data_set_bool(settings,"bypass",block%2!=0);
        obs_source_update(filter,settings);
        for(size_t i=0;i<1024;++i) {
            capture.left[i]=float(std::sin(double(i+block*1024)*.1)*.5);
            capture.right[i]=float(std::cos(double(i+block*1024)*.07)*.25);
        }
        obs_source_audio input{};input.frames=1024;input.speakers=SPEAKERS_STEREO;
        input.format=AUDIO_FORMAT_FLOAT_PLANAR;input.samples_per_sec=48000;
        input.timestamp=time+block*1024*1000000000ULL/48000;
        input.data[0]=reinterpret_cast<const uint8_t*>(capture.left.data());
        input.data[1]=reinterpret_cast<const uint8_t*>(capture.right.data());
        obs_source_output_audio(source,&input);
    }
    // Warm up the actual loaded DLL after publishing a gain and bypass change,
    // then compare its steady output against known amplitudes.
    obs_source_remove_audio_capture_callback(source,captured,&capture);
    obs_data_set_double(settings,"gain_db",6);obs_data_set_bool(settings,"bypass",false);
    obs_source_update(filter,settings);
    std::array<float,1024> inputL{},inputR{};inputL.fill(.125f);inputR.fill(.25f);
    obs_source_audio gained{};gained.frames=1024;gained.speakers=SPEAKERS_STEREO;
    gained.format=AUDIO_FORMAT_FLOAT_PLANAR;gained.samples_per_sec=48000;
    gained.timestamp=time+4*1024*1000000000ULL/48000;
    gained.data[0]=reinterpret_cast<const uint8_t*>(inputL.data());
    gained.data[1]=reinterpret_cast<const uint8_t*>(inputR.data());
    obs_source_output_audio(source,&gained);
    const float amplitude=static_cast<float>(std::pow(10.,6./20));
    capture.left.fill(.125f*amplitude);capture.right.fill(.25f*amplitude);
    obs_source_add_audio_capture_callback(source,captured,&capture);
    gained.timestamp=time+5*1024*1000000000ULL/48000;obs_source_output_audio(source,&gained);
    obs_data_set_double(settings,"eq_band_0_frequency",1000);obs_data_set_double(settings,"eq_band_0_gain",6);
    obs_data_set_double(settings,"eq_band_0_q",2);obs_data_set_bool(settings,"eq_band_2_enabled",false);
    obs_source_update(filter,settings);
    auto* saved=obs_save_source(filter);
    auto* decoded=obs_data_create_from_json(obs_data_get_json(saved));
    auto* restored=obs_load_source(decoded);
    auto* restoredSettings=restored?obs_source_get_settings(restored):nullptr;
    bool ok=capture.equal && capture.calls==5 && restoredSettings && !obs_data_get_bool(restoredSettings,"bypass") && obs_data_get_double(restoredSettings,"gain_db")==6 && obs_data_get_double(restoredSettings,"eq_band_0_gain")==6 && obs_data_get_double(restoredSettings,"eq_band_0_frequency")==1000 && obs_data_get_double(restoredSettings,"eq_band_0_q")==2 && !obs_data_get_bool(restoredSettings,"eq_band_2_enabled");
    if(restoredSettings)obs_data_release(restoredSettings);
    if(restored)obs_source_release(restored);
    obs_data_release(decoded);obs_data_release(saved);
    obs_source_remove_audio_capture_callback(source,captured,&capture);
    obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);
    obs_wait_for_destroy_queue();
    std::cout<<(ok?"PASS":"FAIL")<<": real libobs source -> loaded filter -> capture; measured +6 dB gain and serialized gain/bypass/EQ restore\n";
    return ok;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    wchar_t library[MAX_PATH]{};
    GetModuleFileNameW(GetModuleHandleW(L"obs.dll"), library, MAX_PATH);
    std::wcout << L"Loaded libobs: " << library << L"\n";
    std::cout << "Runtime version: " << obs_get_version_string() << "\n";
    // The real OBS UI loads Qt before plugins. Reproduce that loader state in
    // this otherwise headless host, without creating a QApplication.
#ifdef NEXUS_EQ_UI
    for(const wchar_t* name:{L"Qt6Core.dll",L"Qt6Gui.dll",L"Qt6Widgets.dll"}){
        const auto candidate=std::filesystem::path(library).parent_path()/name;
        const auto handle=LoadLibraryW(std::filesystem::exists(candidate)?candidate.c_str():name);
        if(!handle){std::wcerr<<L"Qt preload failed: "<<name<<L" error="<<GetLastError()<<L"\n";return 7;}
        wchar_t path[MAX_PATH]{};GetModuleFileNameW(handle,path,MAX_PATH);
        std::wcout<<L"Loaded Qt: "<<path<<L"\n";
    }
#endif
    if (!obs_startup("en-US", nullptr, nullptr)) return 3;
    obs_audio_info audio{};
    audio.samples_per_sec = 48000;
    audio.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&audio)) { obs_shutdown(); return 4; }
    obs_module_t* module = nullptr;
    if (obs_open_module(&module, argv[1], ".") != MODULE_SUCCESS ||
        !obs_init_module(module)) { obs_shutdown(); return 5; }
    bool ok = true;
    for (int i = 0; i < 100; ++i) {
        auto* settings = obs_data_create();
        obs_data_set_bool(settings, "bypass", i % 2 != 0);
        auto* source = obs_source_create_private("nexus_audio_rack", "test rack", settings);
        if (!source) { obs_data_release(settings); ok = false; break; }
        auto* props = obs_source_properties(source);
        ok = ok && props && obs_properties_get(props, "bypass");
        obs_properties_destroy(props);
        obs_data_set_bool(settings, "bypass", i % 2 == 0);
        obs_source_update(source, settings);
        obs_source_release(source);
        obs_data_release(settings);
    }
    obs_wait_for_destroy_queue();
    ok = pipeline_and_state() && ok;
    obs_shutdown();
    std::cout << (ok ? "PASS" : "FAIL") << ": module load, 100 filter lifecycles and properties\n";
    return ok ? 0 : 6;
}