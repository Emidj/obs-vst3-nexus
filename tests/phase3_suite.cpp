#include "bench/analysis.hpp"
#include "bench/audio_file.hpp"
#include "dsp/basic.hpp"
#include "dsp/equalizer.hpp"
#include "dsp/rack.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#endif
namespace {
thread_local bool watch = false;
thread_local size_t allocations = 0, deallocations = 0;
void allocated() { if (watch) ++allocations; }
void released(void* p) { if (watch && p) ++deallocations; }
}
void* operator new(size_t n) {
    allocated(); if (auto* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { released(p); std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
void* operator new(size_t n, std::align_val_t align) {
    allocated();
    const size_t a = static_cast<size_t>(align);
#ifdef _WIN32
    auto* p = _aligned_malloc(n ? n : 1, a);
#else
    auto* p = std::aligned_alloc(a, ((n ? n : 1) + a - 1) / a * a);
#endif
    if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n, std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p, std::align_val_t) noexcept {
    released(p);
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p,a); }
void operator delete(void* p, size_t, std::align_val_t a) noexcept { ::operator delete(p,a); }
void operator delete[](void* p, size_t, std::align_val_t a) noexcept { ::operator delete(p,a); }

using namespace nexus;
using namespace nexus::bench;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
template<class F> void rejects(F action) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "Invalid input was accepted");
}
class Delay final : public Processor {
public:
    explicit Delay(size_t delay) : delay_(delay) {}
    void prepare(const Format& f) override { ring_.assign(f.channels * delay_,0); channels_ = f.channels; pos_ = 0; }
    void reset() noexcept override { std::fill(ring_.begin(),ring_.end(),0.f); pos_ = 0; }
    size_t latency_frames() const noexcept override { return delay_; }
    void process(Block b) noexcept override {
        for (size_t i = 0; i < b.frames; ++i) {
            for (size_t c = 0; c < channels_; ++c) {
                auto& slot = ring_[c * delay_ + pos_];
                const float x = b.planes[c][i]; b.planes[c][i] = slot; slot = x;
            }
            pos_ = (pos_ + 1) % delay_;
        }
    }
private:
    size_t delay_, channels_ = 0, pos_ = 0;
    std::vector<float> ring_;
};
class SmoothGain final : public Processor {
public:
    SmoothedParameter value{1.f};
    void prepare(const Format& f) override { value.prepare(f.sample_rate, 10); }
    void reset() noexcept override { value.reset(); }
    void process(Block b) noexcept override {
        value.begin_block();
        for (size_t i=0;i<b.frames;++i) {
            const auto gain=value.next();
            for (size_t c=0;c<b.channels;++c) b.planes[c][i]*=gain;
        }
    }
};
struct Result { std::string name, error; bool pass; };
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::vector<Result> results;
    std::map<std::string,double> metrics;
    auto run = [&](const char* name, auto task) {
        try { task(); results.push_back({name,"",true}); std::cout<<"PASS: "<<name<<"\n"; }
        catch(const std::exception& e) {
            watch=false; results.push_back({name,e.what(),false}); std::cerr<<"FAIL: "<<name<<": "<<e.what()<<"\n";
        }
    };
    run("coherent_response_and_thd_references",[&] {
        double worstGain=0,worstThd=0;
        for (uint32_t rate : {44100u,48000u,96000u})
            for (double hz : {20.,100.,1000.,5000.,10000.}) {
                std::vector<float> input(rate), output(rate);
                for(size_t i=0;i<rate;++i) {
                    input[i]=float(.5*std::sin(2*pi*hz*double(i)/rate));
                    output[i]=.25f*input[i];
                }
                const double error=std::abs(response_db(input,output,rate,hz)-20*std::log10(.25));
                worstGain=(std::max)(worstGain,error);
                require(error<1e-5,"Gain response outside 0.00001 dB tolerance");
                // Periodic 2-tap FIR has exact magnitude |cos(pi*f/fs)|.
                for(size_t i=0;i<rate;++i) output[i]=.5f*(input[i]+input[(i+rate-1)%rate]);
                require(std::abs(response_db(input,output,rate,hz)-20*std::log10(std::cos(pi*hz/rate)))<1e-4,"FIR frequency reference");
                if(hz==1000.) {
                    for(size_t i=0;i<rate;++i)
                        output[i]=float(.5*std::sin(2*pi*hz*double(i)/rate)+.05*std::sin(4*pi*hz*double(i)/rate));
                    const double errorThd=std::abs(thd_ratio(output,rate,hz)-.1);
                    worstThd=(std::max)(worstThd,errorThd);
                    require(errorThd<1e-6,"Known 10 percent THD reference");
                    require(thd_ratio(input,rate,hz)<1e-6,"Clean sine distortion floor");
                }
            }
        metrics["response_max_error_db"]=worstGain; metrics["thd_ratio_max_error"]=worstThd;
    });
    run("dynamics_attack_release_references",[&] {
        double maxError=0;
        for(double rate:{44100.,48000.,96000.}) {
            std::vector<float> attack(static_cast<size_t>(rate*.2)),release(attack.size());
            for(size_t i=0;i<attack.size();++i) {
                attack[i]=float(1-std::exp(-double(i)/rate/.01));
                release[i]=float(std::exp(-double(i)/rate/.04));
            }
            const double a=step_crossing_seconds(attack,rate,0,0,1,1-std::exp(-1.));
            const double r=step_crossing_seconds(release,rate,0,1,0,1-std::exp(-1.));
            const double error=(std::max)(std::abs(a-.01),std::abs(r-.04));
            maxError=(std::max)(maxError,error);
            require(error<=1.01/rate,"Dynamics time must agree within one sample");
        }
        metrics["dynamics_max_error_seconds"]=maxError;
    });
    run("measured_delay_and_aligned_bypass",[&] {
        for(double rate:{44100.,48000.,96000.}) {
            Engine e;
            e.add(std::make_unique<Delay>(7)); e.add(std::make_unique<Delay>(13));
            e.prepare({rate,2,257});
            std::vector<float> impulse(4096,0),output(4096,0);
            impulse[31]=.5f;
            std::vector<float> left(257),right(257);
            float* planes[]{left.data(),right.data()};
            for(size_t pos=0;pos<impulse.size();) {
                const auto n=(std::min)(size_t(257),impulse.size()-pos);
                std::copy_n(impulse.data()+pos,n,left.data()); std::copy_n(impulse.data()+pos,n,right.data());
                if(pos>512) e.set_bypass((pos/257)%2!=0);
                require(e.process({planes,2,n}),"Delay process rejected");
                require(std::equal(left.begin(),left.begin()+n,right.begin()),"Bypass channels diverged");
                std::copy_n(left.data(),n,output.data()+pos); pos+=n;
            }
            require(impulse_delay(impulse,output)==20 && e.latency_frames()==20,"Measured versus declared latency");
            // Continuous signal must remain exactly delayed across both bypass transitions.
            e.reset();
            std::vector<float> history;
            for(size_t block=0;block<40;++block) {
                for(size_t i=0;i<257;++i) left[i]=right[i]=float(std::sin(double(block*257+i)*.031));
                history.insert(history.end(),left.begin(),left.end());
                e.set_bypass(block%2==0);
                require(e.process({planes,2,257}),"Aligned bypass process");
                for(size_t i=0;i<257;++i) {
                    const size_t at=block*257+i;
                    require(std::abs(left[i]-(at>=20?history[at-20]:0.f))<1e-6,"Bypass changed delay alignment");
                }
            }
            e.reset();
            std::fill(left.begin(),left.end(),0.f); std::fill(right.begin(),right.end(),0.f);
            require(e.process({planes,2,257}),"Reset process");
            for(float v:left) require(v==0,"Reset left stale history");
        }
        metrics["measured_latency_frames"]=20;
    });
    run("parameter_ramp_block_invariance_and_retarget",[&] {
        for(double rate:{44100.,48000.,96000.}) {
            SmoothedParameter a(0),b(0);
            a.prepare(rate,10); b.prepare(rate,10); a.set_target(1); b.set_target(1);
            std::vector<float> whole(2000),split(2000);
            a.begin_block(); for(auto& x:whole) x=a.next();
            for(size_t pos=0;pos<split.size();) {
                const auto n=(std::min)(size_t(17),split.size()-pos);
                b.begin_block(); for(size_t i=0;i<n;++i) split[pos+i]=b.next(); pos+=n;
            }
            require(whole==split,"Ramp depends on block size");
            require(whole.back()==1,"Ramp never settles");
            require(!a.set_target(std::numeric_limits<float>::quiet_NaN()),"NaN parameter accepted");
            a.set_target(-1); a.begin_block(); const auto first=a.next();
            require(first<1 && first>.9,"Retarget discontinuity");
            a.reset(); a.begin_block(); require(a.next()==-1,"Parameter reset");
        }
    });
    run("zero_audio_heap_and_concurrent_parameter_stress",[&] {
        Engine e; auto module=std::make_unique<SmoothGain>(); auto* gain=module.get();
        e.add(std::move(module)); e.prepare({48000,2,257});
        std::vector<float> left(257,1),right(257,1); float* planes[]{left.data(),right.data()};
        constexpr size_t blocks=100000;
        std::vector<double> timings(blocks);
        std::atomic<bool> start{false},done{false};
        std::thread control([&]{
            while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
            size_t i=0;
            while(!done.load(std::memory_order_relaxed)) {
                gain->value.set_target(float(i%101)/100.f);
                e.set_bypass(i%3==0); ++i;
            }
        });
        size_t bad=0,deadlineMisses=0;
        allocations=deallocations=0; start.store(true,std::memory_order_release);
        for(size_t block=0;block<blocks;++block) {
            std::fill(left.begin(),left.end(),1.f); std::fill(right.begin(),right.end(),1.f);
            const size_t n=block%257+1;
            watch=true; const auto t0=std::chrono::steady_clock::now();
            const bool ok=e.process({planes,2,n});
            const auto t1=std::chrono::steady_clock::now(); watch=false;
            if(!ok) ++bad;
            for(size_t i=0;i<n;++i) if(!std::isfinite(left[i]) || left[i]<0 || left[i]>1.00001f || left[i]!=right[i]) ++bad;
            timings[block]=std::chrono::duration<double,std::micro>(t1-t0).count();
            if(timings[block]>double(n)/48000.*1e6) ++deadlineMisses;
        }
        done.store(true,std::memory_order_relaxed); control.join();
        require(bad==0,"Stress output invalid");
        require(allocations==0 && deallocations==0,"Heap activity on the audio thread");
        std::sort(timings.begin(),timings.end());
        metrics["stress_blocks"]=double(blocks);
        metrics["audio_heap_allocations"]=double(allocations);
        metrics["audio_heap_deallocations"]=double(deallocations);
        metrics["block_us_p50"]=timings[blocks/2]; metrics["block_us_p95"]=timings[blocks*95/100];
        metrics["block_us_max"]=timings.back(); metrics["observed_deadline_misses"]=double(deadlineMisses);
    });
    run("bypass_ramp_continuity",[&] {
        for(double rate:{44100.,48000.,96000.}) {
            Engine e; auto gain=std::make_unique<SmoothGain>();
            gain->value.set_target(0); e.add(std::move(gain)); e.prepare({rate,1,1024});
            std::vector<float> signal(1024,1);float* p[]{signal.data()};
            e.set_bypass(true);
            require(e.process({p,1,1024}),"Ramp process");
            const auto rampFrames=static_cast<size_t>(rate*.005);
            require(signal[0]>0 && signal[0]<.01f,"Abrupt bypass onset");
            require(signal[rampFrames-1]==1.f && signal.back()==1.f,"Bypass duration");
            for(size_t i=1;i<signal.size();++i)
                require(signal[i]>=signal[i-1] && signal[i]-signal[i-1]<=1.01/double(rampFrames),"Bypass discontinuity");
            e.set_bypass(false);std::fill(signal.begin(),signal.end(),1.f);
            require(e.process({p,1,1024}) && signal.back()==0.f,"Bypass return");
        }
    });
    run("phase4_native_chain_automation_heap_and_benchmark",[&] {
        Engine e;
        auto gain=std::make_unique<Gain>();auto* g=gain.get();e.add(std::move(gain));
        auto balance=std::make_unique<Balance>();auto* b=balance.get();e.add(std::move(balance));
        auto mono=std::make_unique<Mono>();auto* m=mono.get();e.add(std::move(mono));
        auto polarity=std::make_unique<Polarity>();auto* p=polarity.get();e.add(std::move(polarity));
        auto hp=std::make_unique<PassFilter>(PassType::high_pass);auto* high=hp.get();e.add(std::move(hp));
        auto lp=std::make_unique<PassFilter>(PassType::low_pass);auto* low=lp.get();e.add(std::move(lp));
        e.prepare({48000,2,257});
        std::vector<float> left(257),right(257);float* planes[]{left.data(),right.data()};
        constexpr size_t blocks=20000;std::vector<double> timings(blocks);
        size_t bad=0;allocations=deallocations=0;
        std::atomic<bool> start{false},done{false};
        std::thread controls([&]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            size_t k=0;
            while(!done.load(std::memory_order_relaxed)){
                g->set_db(float(int(k%85)-60));b->set_position(float(int(k%201)-100)/100.f);
                m->set_enabled(k%2!=0);p->set_inverted(k%3==0);
                high->set_cutoff(k%2?20.f:20000.f);low->set_cutoff(k%2?20000.f:20.f);
                high->set_enabled(k%3!=0);low->set_enabled(k%3!=1);++k;
            }
        });
        start.store(true,std::memory_order_release);
        for(size_t block=0;block<blocks;++block){
            const size_t n=block%257+1;
            for(size_t i=0;i<n;++i){
                left[i]=float(.125*std::sin(double(block*257+i)*.17));
                right[i]=float(.125*std::cos(double(block*257+i)*.13));
            }
            watch=true;const auto t0=std::chrono::steady_clock::now();
            const bool ok=e.process({planes,2,n});
            const auto t1=std::chrono::steady_clock::now();watch=false;
            if(!ok)++bad;
            for(size_t i=0;i<n;++i)if(!std::isfinite(left[i]) || !std::isfinite(right[i]) ||
                std::abs(left[i])>64 || std::abs(right[i])>64)++bad;
            timings[block]=std::chrono::duration<double,std::micro>(t1-t0).count();
        }
        done.store(true,std::memory_order_relaxed);controls.join();
        require(bad==0,"Native chain automation instability");
        require(allocations==0 && deallocations==0,"Native chain audio heap activity");
        std::sort(timings.begin(),timings.end());
        metrics["phase4_stress_blocks"]=double(blocks);
        metrics["phase4_audio_allocations"]=double(allocations);metrics["phase4_audio_deallocations"]=double(deallocations);
        metrics["phase4_block_us_p50"]=timings[blocks/2];metrics["phase4_block_us_p95"]=timings[blocks*95/100];
        metrics["phase4_block_us_max"]=timings.back();
    });
    run("phase5_equalizer_concurrent_automation_and_heap",[&]{
        Engine e;auto module=std::make_unique<Equalizer>();auto* eq=module.get();e.add(std::move(module));
        e.prepare({48000,2,257});
        constexpr size_t blocks=20000;std::vector<double> times(blocks);
        std::vector<float> left(257),right(257);float* planes[]{left.data(),right.data()};
        std::atomic<bool> start{false},done{false};size_t bad=0;allocations=deallocations=0;double peak=0;
        std::thread controls([&]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            size_t k=0;while(!done.load(std::memory_order_relaxed)){
                for(size_t i=0;i<4;++i)eq->set_band(i,{k%2?20.f:20000.f,k%3?18.f:-18.f,k%2?.2f:10.f,k%5!=0});
                eq->set_enabled(k%7!=0);++k;
            }
        });
        start.store(true,std::memory_order_release);
        for(size_t block=0;block<blocks;++block){
            const size_t n=block%257+1;
            for(size_t i=0;i<n;++i){left[i]=float(.0001*std::sin(double(block*257+i)*.17));right[i]=float(.0001*std::cos(double(block*257+i)*.13));}
            watch=true;const auto begin=std::chrono::steady_clock::now();const bool ok=e.process({planes,2,n});
            const auto end=std::chrono::steady_clock::now();watch=false;if(!ok)++bad;
            for(size_t i=0;i<n;++i){
                if(!std::isfinite(left[i])||!std::isfinite(right[i]))++bad;
                peak=(std::max)(peak,(std::max)(std::abs(double(left[i])),std::abs(double(right[i]))));
            }
            times[block]=std::chrono::duration<double,std::micro>(end-begin).count();
        }
        done.store(true,std::memory_order_relaxed);controls.join();
        require(bad==0,"EQ automation produced non-finite audio");
        require(peak<64,"EQ automation runaway");
        require(allocations==0&&deallocations==0,"EQ audio heap activity");
        std::sort(times.begin(),times.end());
        metrics["phase5_stress_blocks"]=double(blocks);metrics["phase5_audio_allocations"]=double(allocations);
        metrics["phase5_audio_deallocations"]=double(deallocations);metrics["phase5_automation_peak"]=peak;
        metrics["phase5_block_us_p50"]=times[blocks/2];metrics["phase5_block_us_p95"]=times[blocks*95/100];metrics["phase5_block_us_max"]=times.back();
    });
    run("phase6_live_edit_audio_heap_guard",[&]{
        LiveRack rack({48000,2,257},{});
        std::vector<float> left(257),right(257);float* planes[]{left.data(),right.data()};
        std::atomic<bool> start{false},done{false};size_t bad=0;std::atomic<size_t> edits{0};
        std::thread controls([&]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            for(size_t i=0;i<2000;++i){
                auto gain=default_node(ModuleKind::gain,i+1);gain.value=i%2?-6.f:0.f;
                auto hp=default_node(ModuleKind::high_pass,i+3000);hp.value=80;
                rack.publish(i%3?RackConfig{gain,hp}:RackConfig{});++edits;
            }done.store(true,std::memory_order_release);
        });
        allocations=deallocations=0;start.store(true,std::memory_order_release);
        size_t blocks=0;
        while(!done.load(std::memory_order_acquire)||blocks<30000){
            for(size_t i=0;i<257;++i)left[i]=right[i]=float(.01*std::sin(double(blocks*257+i)*.07));
            watch=true;const bool ok=rack.process({planes,2,257});watch=false;
            if(!ok)++bad;
            for(float value:left)if(!std::isfinite(value)||std::abs(value)>1)++bad;
            ++blocks;
        }
        controls.join();
        for(int i=0;i<4;++i){std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);watch=true;rack.process({planes,2,257});watch=false;}
        require(bad==0,"Live rack output invalid");require(allocations==0&&deallocations==0,"Live editing allocated or freed on audio thread");
        rack.collect();require(rack.retained_graphs()==1,"Live graph ownership leak");
        metrics["phase6_edits"]=double(edits.load());metrics["phase6_audio_blocks"]=double(blocks);
        metrics["phase6_audio_allocations"]=double(allocations);metrics["phase6_audio_deallocations"]=double(deallocations);
    });
    run("phase8_latency_graph_audio_heap_guard",[&]{
        LiveRack rack({48000,2,257},{},false,513);
        std::vector<float> left(257),right(257);float* planes[]{left.data(),right.data()};
        allocations=deallocations=0;
        for(size_t block=0;block<5000;++block){
            if(block%3==0){
                rack.publish({default_node(ModuleKind::gain,block+1)});
                rack.set_bypass(block%2==0);
            }
            std::fill(left.begin(),left.end(),.1f);std::fill(right.begin(),right.end(),-.1f);
            watch=true;const auto ok=rack.process({planes,2,257});watch=false;
            require(ok,"Latency rack block rejected");rack.collect();
        }
        require(allocations==0&&deallocations==0,"Latency transitions used audio heap");
        metrics["phase8_latency_audio_blocks"]=5000;
        metrics["phase8_latency_audio_allocations"]=double(allocations);
        metrics["phase8_latency_audio_deallocations"]=double(deallocations);
    });
    run("invalid_analysis_and_core_inputs",[&] {
        rejects([]{levels({});});
        rejects([]{tone_amplitude({0,1},48000,24000);});
        rejects([]{thd_ratio(std::vector<float>(100,0),48000,1000);});
        rejects([]{impulse_delay({0,0},{0,0});});
        rejects([]{step_crossing_seconds({0,0},48000,0,0,1,.5);});
        rejects([]{Engine e;e.prepare({48000,2,65537});});
        rejects([]{Engine e;e.add(std::make_unique<Delay>(96001));e.prepare({48000,1,1});});
        rejects([]{Engine e;e.add(nullptr);});
        Engine e;e.prepare({48000,1,16});
        float one=1;float* good[]{&one};float* invalid[]{nullptr};
        require(!e.process({invalid,1,1}) && !e.process({good,1,17}) && one==1,"Invalid block changed audio");
    });
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) {
        metrics["working_set_bytes"]=double(memory.WorkingSetSize);
        metrics["peak_working_set_bytes"]=double(memory.PeakWorkingSetSize);
    }
#endif
    run("phase7_dynamics_audio_heap_guard",[&]{
        auto c=nexus::default_node(nexus::ModuleKind::compressor,1);
        auto e=nexus::default_node(nexus::ModuleKind::expander,2);
        auto w=nexus::default_node(nexus::ModuleKind::stereo_width,3);
        auto l=nexus::default_node(nexus::ModuleKind::limiter,4);
        nexus::LiveRack rack({48000,2,128},{c,e,w,l});
        std::array<float,128> left{},right{};float* planes[]{left.data(),right.data()};
        std::atomic<bool> stop{false};std::atomic<size_t> edits{0};std::atomic<bool> valid{true};
        std::thread editor([&]{
            try{
                while(!stop.load(std::memory_order_acquire)){
                    const size_t n=edits.load();auto config=nexus::RackConfig{c,e,w,l};
                    config[0].dynamics.threshold_db=-float(n%60);config[0].dynamics.ratio=1+float(n%20);
                    config[1].dynamics.gate=(n%2)==0;config[1].dynamics.hold_ms=float(n%100);
                    config[2].value=float(n%201)/100;config[3].dynamics.threshold_db=-1-float(n%12);
                    if(n%31==0)std::swap(config[0],config[1]);
                    rack.publish(config);if(!std::isfinite(rack.gain_reduction_db(1)))valid=false;++edits;
                }
            }catch(...){valid=false;}
        });
        size_t blocks=0;allocations=deallocations=0;bool good=true;
        for(;(blocks<20000||edits.load()<2000)&&valid.load();++blocks){
            left.fill(.8f);right.fill(-.2f);watch=true;
            const bool ok=rack.process({planes,2,128});
            watch=false;good=good&&ok;
            for(float sample:left)good=good&&std::isfinite(sample);
        }
        stop.store(true,std::memory_order_release);editor.join();
        metrics["phase7_dynamics_blocks"]=double(blocks);metrics["phase7_dynamics_edits"]=double(edits.load());
        metrics["phase7_dynamics_allocations"]=double(allocations);metrics["phase7_dynamics_deallocations"]=double(deallocations);
        require(good&&valid.load(),"Dynamics concurrent processing failure");
        require(allocations==0&&deallocations==0,"Dynamics audio heap activity");
    });
    const bool passed=std::all_of(results.begin(),results.end(),[](const auto& r){return r.pass;});
    std::ofstream report(argv[1]);
    if(!report) return 3;
    report<<std::setprecision(12)<<"{\n  \"schema_version\": 1,\n  \"passed\": "<<(passed?"true":"false")
          <<",\n  \"scope\": \"DSP foundations, native modules and live rack; no VST3\",\n  \"tests\": [\n";
    for(size_t i=0;i<results.size();++i) {
        const auto& r=results[i];
        report<<"    {\"name\": \""<<r.name<<"\", \"passed\": "<<(r.pass?"true":"false")<<"}"<<(i+1<results.size()?",":"")<<"\n";
    }
    report<<"  ],\n  \"metrics\": {\n";
    size_t i=0;for(const auto& m:metrics) report<<"    \""<<m.first<<"\": "<<m.second<<(++i<metrics.size()?",":"")<<"\n";
    report<<"  }\n}\n"; report.flush();
    return passed && report ? 0 : 1;
}