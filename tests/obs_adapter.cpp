#include "../src/obs/plugin.cpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    if (!obs_startup("en-US", nullptr, nullptr)) return 2;
    int cases = 0;
    try {
        for (uint32_t rate : {44100u, 48000u, 96000u}) {
            for (auto speakers : {SPEAKERS_MONO, SPEAKERS_STEREO}) {
                obs_audio_info config{};
                config.samples_per_sec = rate;
                config.speakers = speakers;
                require(obs_reset_audio(&config), "Audio reset");
                auto* settings = obs_data_create();
                auto* instance = create(settings, nullptr);
                require(instance != nullptr, "Filter creation");
                const size_t channels = get_audio_channels(speakers);
                for (size_t frames : {0u, 1u, 1024u, 2501u}) {
                    std::vector<std::vector<float>> samples(channels, std::vector<float>(2501));
                    obs_audio_data block{};
                    block.frames = static_cast<uint32_t>(frames);
                    block.timestamp = 987654321;
                    for (size_t c = 0; c < channels; ++c) {
                        for (size_t i = 0; i < 2501; ++i)
                            samples[c][i] = float(std::sin(double(i + c)) * .5);
                        block.data[c] = reinterpret_cast<uint8_t*>(samples[c].data());
                    }
                    const auto original = samples;
                    require(audio(instance, &block) == &block, "Buffer identity");
                    require(samples == original, "Transparent samples");
                    require(block.frames == frames && block.timestamp == 987654321, "Metadata");
                    obs_data_set_bool(settings, "bypass", true);
                    update(instance, settings);
                    require(audio(instance, &block) == &block && samples == original, "Bypass");
                    obs_data_set_bool(settings, "bypass", false);
                    update(instance, settings);
                    ++cases;
                }
                require(audio(instance, nullptr) == nullptr, "Null block");
                destroy(instance);
                obs_data_release(settings);
            }
        }
        auto* gainSettings=obs_data_create();obs_data_set_double(gainSettings,"gain_db",-6);
        auto* gainInstance=create(gainSettings,nullptr);require(gainInstance!=nullptr,"Gain filter creation");
        std::vector<float> gainL(1024,1.f),gainR(1024,1.f);
        obs_audio_data gainBlock{};gainBlock.frames=1024;
        gainBlock.data[0]=reinterpret_cast<uint8_t*>(gainL.data());
        gainBlock.data[1]=reinterpret_cast<uint8_t*>(gainR.data());
        audio(gainInstance,&gainBlock);
        require(std::abs(gainL.back()-float(std::pow(10.,-6./20)))<1e-6,"Initial gain not applied");
        obs_data_set_double(gainSettings,"gain_db",6);update(gainInstance,gainSettings);
        std::fill(gainL.begin(),gainL.end(),1.f);std::fill(gainR.begin(),gainR.end(),1.f);
        audio(gainInstance,&gainBlock);
        require(std::abs(gainL.back()-float(std::pow(10.,6./20)))<1e-6,"Gain ramp failed");
        obs_data_set_double(gainSettings,"gain_db",999);update(gainInstance,gainSettings);
        std::fill(gainL.begin(),gainL.end(),1.f);std::fill(gainR.begin(),gainR.end(),1.f);
        audio(gainInstance,&gainBlock);
        require(gainL.back()==1.f,"Invalid stored gain did not fall back to neutral");
        destroy(gainInstance);obs_data_release(gainSettings);
        auto* eqSettings=obs_data_create();
        obs_data_set_double(eqSettings,"eq_band_0_frequency",1000);
        obs_data_set_double(eqSettings,"eq_band_0_gain",6);
        obs_data_set_double(eqSettings,"eq_band_0_q",1);
        auto* eqInstance=create(eqSettings,nullptr);require(eqInstance!=nullptr,"EQ filter create");
        constexpr size_t rate=96000;
        std::vector<float> eqL(rate*2),eqR(rate*2);
        for(size_t i=0;i<eqL.size();++i)eqL[i]=eqR[i]=float(.01*std::sin(2*3.14159265358979323846*1000*double(i)/rate));
        obs_audio_data eqBlock{};eqBlock.frames=static_cast<uint32_t>(eqL.size());
        eqBlock.data[0]=reinterpret_cast<uint8_t*>(eqL.data());eqBlock.data[1]=reinterpret_cast<uint8_t*>(eqR.data());
        audio(eqInstance,&eqBlock);
        double squares=0;for(size_t i=rate;i<eqL.size();++i)squares+=double(eqL[i])*eqL[i];
        const double measured=20*std::log10(std::sqrt(squares/rate)/(.01/std::sqrt(2.)));
        require(std::abs(measured-6)<.001,"EQ settings did not reach audio");
        destroy(eqInstance);obs_data_release(eqSettings);
        std::cout<<"PASS: EQ +6 dB measured through the OBS audio adapter\n";
        nexus::RackDocument document;
        auto trim=nexus::default_node(nexus::ModuleKind::gain,11);trim.value=-6;
        auto mono=nexus::default_node(nexus::ModuleKind::mono,12);
        auto balance=nexus::default_node(nexus::ModuleKind::balance,13);balance.value=1;
        document.a={trim,mono};document.b={mono,balance};document.use_b=true;
        auto* rackSettings=obs_data_create();nexus::obs_bridge::write_rack(rackSettings,document);
        auto* live=create(rackSettings,nullptr);require(live,"Rack scene creation");
        std::vector<float> rackL(1024,1.f),rackR(1024,0.f);
        obs_audio_data rackBlock{};rackBlock.frames=1024;rackBlock.data[0]=reinterpret_cast<uint8_t*>(rackL.data());rackBlock.data[1]=reinterpret_cast<uint8_t*>(rackR.data());
        audio(live,&rackBlock);require(rackL.back()==0&&rackR.back()==.5f,"Rack B order not applied");
        document.use_b=false;nexus::obs_bridge::write_rack(rackSettings,document);update(live,rackSettings);
        std::fill(rackL.begin(),rackL.end(),1.f);std::fill(rackR.begin(),rackR.end(),0.f);audio(live,&rackBlock);
        const float expected=.5f*float(std::pow(10.,-6./20));
        require(std::abs(rackL.back()-expected)<1e-6&&rackL.back()==rackR.back(),"Rack A/B live change");
        auto* serialized=obs_data_create_from_json(obs_data_get_json(rackSettings));
        require(nexus::same_document(document,nexus::obs_bridge::read_rack(serialized)),"Rack scene JSON roundtrip");
        obs_data_release(serialized);
        obs_data_set_int(rackSettings,"rack_version",99);update(live,rackSettings);
        require(*obs_data_get_string(rackSettings,"rack_error"),"Unsupported scene error not reported");
        std::fill(rackL.begin(),rackL.end(),1.f);std::fill(rackR.begin(),rackR.end(),0.f);audio(live,&rackBlock);
        require(std::abs(rackL.back()-expected)<1e-6,"Rejected scene changed active audio");
        obs_data_set_int(rackSettings,"rack_version",1);
        auto* nodes=obs_data_get_array(rackSettings,"rack_a");auto* duplicate=obs_data_array_item(nodes,0);
        obs_data_array_push_back(nodes,duplicate);obs_data_release(duplicate);
        obs_data_set_array(rackSettings,"rack_a",nodes);obs_data_array_release(nodes);
        bool rejected=false;try{nexus::obs_bridge::read_rack(rackSettings);}catch(const std::exception&){rejected=true;}
        require(rejected,"Duplicate scene node accepted");
        destroy(live);obs_data_release(rackSettings);
        std::cout<<"PASS: versioned rack scene, A/B audio, JSON restore and rejected-state preservation\n";
        obs_audio_info meterFormat{};meterFormat.samples_per_sec=48000;meterFormat.speakers=SPEAKERS_STEREO;
        require(obs_reset_audio(&meterFormat),"Meter calibration format");
        nexus::RackDocument meterDocument;
        auto* meterSettings=obs_data_create();nexus::obs_bridge::write_rack(meterSettings,meterDocument);
        auto* metered=create(meterSettings,nullptr);require(metered,"Metered filter creation");
        std::vector<float> meterL(1024),meterR(1024);
        obs_audio_data meterBlock{};meterBlock.frames=1024;
        meterBlock.data[0]=reinterpret_cast<uint8_t*>(meterL.data());meterBlock.data[1]=reinterpret_cast<uint8_t*>(meterR.data());
        for(size_t n=0;n<192;++n){
            for(size_t i=0;i<1024;++i)meterL[i]=meterR[i]=float(.1*std::sin(6.283185307179586*1000*double(n*1024+i)/48000));
            audio(metered,&meterBlock);
        }
        calldata_t meterStatus;calldata_init(&meterStatus);loudness(metered,&meterStatus);
        require(calldata_bool(&meterStatus,"available")&&calldata_bool(&meterStatus,"short_term_ready")&&
            std::abs(calldata_float(&meterStatus,"short_term")+20)<.08,"Output loudness through OBS adapter");
        calldata_free(&meterStatus);destroy(metered);obs_data_release(meterSettings);
        std::cout<<"PASS: output LUFS and sample-peak telemetry through OBS adapter\n";
        auto compressor=nexus::default_node(nexus::ModuleKind::compressor,101);
        compressor.dynamics.threshold_db=-24;compressor.dynamics.ratio=4;compressor.dynamics.attack_ms=.1f;
        auto expander=nexus::default_node(nexus::ModuleKind::expander,102);expander.enabled=false;expander.dynamics.gate=true;
        auto width=nexus::default_node(nexus::ModuleKind::stereo_width,103);
        auto limiter=nexus::default_node(nexus::ModuleKind::limiter,104);limiter.dynamics.threshold_db=-6;
        nexus::RackDocument dynamicDoc;dynamicDoc.a={compressor,expander,width,limiter};dynamicDoc.b=dynamicDoc.a;
        auto* dynamicSettings=obs_data_create();nexus::obs_bridge::write_rack(dynamicSettings,dynamicDoc);
        auto* restored=obs_data_create_from_json(obs_data_get_json(dynamicSettings));
        require(nexus::same_document(dynamicDoc,nexus::obs_bridge::read_rack(restored)),"Dynamics scene roundtrip");obs_data_release(restored);
        auto* dynamicInstance=create(dynamicSettings,nullptr);require(dynamicInstance,"Dynamics filter creation");
        for(size_t block=0;block<64;++block){
            std::fill(meterL.begin(),meterL.end(),1.f);std::fill(meterR.begin(),meterR.end(),.25f);
            audio(dynamicInstance,&meterBlock);
        }
        require(std::abs(20*std::log10(meterL.back())+18)<.001,"Compressor through adapter");
        require(std::abs(meterR.back()/meterL.back()-.25f)<1e-6,"Adapter stereo linkage");
        calldata_init(&meterStatus);calldata_set_string(&meterStatus,"id","101");reduction(dynamicInstance,&meterStatus);
        require(calldata_float(&meterStatus,"db")>17.9,"Adapter gain reduction");calldata_free(&meterStatus);
        auto* dynamicNodes=obs_data_get_array(dynamicSettings,"rack_a");auto* first=obs_data_array_item(dynamicNodes,0);
        obs_data_set_string(first,"dyn_ratio","invalid");obs_data_release(first);obs_data_set_array(dynamicSettings,"rack_a",dynamicNodes);obs_data_array_release(dynamicNodes);
        update(dynamicInstance,dynamicSettings);require(*obs_data_get_string(dynamicSettings,"rack_error"),"Invalid dynamics scene accepted");
        std::fill(meterL.begin(),meterL.end(),1.f);std::fill(meterR.begin(),meterR.end(),.25f);audio(dynamicInstance,&meterBlock);
        require(std::abs(20*std::log10(meterL.back())+18)<.001,"Rejected dynamics changed audio");
        destroy(dynamicInstance);obs_data_release(dynamicSettings);
        std::cout<<"PASS: dynamics scene restore, stereo link, reduction and invalid-state preservation\n";
        obs_shutdown();
        std::cout << "PASS: " << cases << " adapter audio cases (including >1024 frame splitting)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        obs_shutdown();
        return 1;
    }
}