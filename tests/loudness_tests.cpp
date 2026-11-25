#include "dsp/loudness.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
namespace {
thread_local bool watching=false;
thread_local size_t allocations=0,deallocations=0;
void check(bool x,const char* s){if(!x)throw std::runtime_error(s);}
void allocation(){if(watching)++allocations;}
void deallocation(void* p){if(p&&watching)++deallocations;}
constexpr double pi=3.14159265358979323846;
size_t blocks=0;
}
extern "C" void* nexus_ebur_malloc(size_t n){allocation();return std::malloc(n);}
extern "C" void* nexus_ebur_calloc(size_t n,size_t s){allocation();return std::calloc(n,s);}
extern "C" void* nexus_ebur_realloc(void* p,size_t n){allocation();deallocation(p);return std::realloc(p,n);}
extern "C" void nexus_ebur_free(void* p){deallocation(p);std::free(p);}
void* operator new(size_t n){allocation();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{deallocation(p);std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
namespace {
nexus::LoudnessReading signal(int rate,size_t channels,size_t chunk,double amplitude,double seconds=4.){
    nexus::LoudnessMeter meter({double(rate),channels,1024});
    check(!meter.read().active,"Premature active meter");
    std::array<std::array<float,1024>,2> samples{};
    std::array<float*,2> planes{samples[0].data(),samples[1].data()};
    const size_t frames=size_t(seconds*rate);
    for(size_t offset=0;offset<frames;){
        const size_t count=(std::min)(chunk,frames-offset);
        for(size_t i=0;i<count;++i)for(size_t c=0;c<channels;++c)
            samples[c][i]=float(amplitude*std::sin(2*pi*1000*double(offset+i)/rate));
        const auto before=samples[0][count-1];
        watching=true;const bool ok=meter.process({planes.data(),channels,count});watching=false;
        check(ok,"Valid signal rejected");check(samples[0][count-1]==before,"Meter changed audio");
        ++blocks;offset+=count;
    }
    return meter.read();
}
}
int main(int argc,char** argv){
    try{
        size_t cases=0;double error=0;std::array<float,2> reference{};
        for(int rate:{44100,48000,96000})for(size_t channels:{size_t(1),size_t(2)}){
            float partition=0;
            for(size_t chunk:{size_t(17),size_t(256),size_t(1024)}){
                const auto r=signal(rate,channels,chunk,.1);
                check(r.active&&r.momentary_ready&&r.short_term_ready&&r.integrated_ready,"Incomplete time windows");
                // Independent standard calibration: 1 kHz at -20 dBFS peak is
                // approximately -23.0 LUFS mono / -20.0 LUFS identical stereo.
                const double expected=channels==1?-23.:-20.;
                const double e=std::abs(r.short_term-expected);error=(std::max)(error,e);
                check(e<.08&&std::abs(r.momentary-expected)<.08&&std::abs(r.integrated-expected)<.12,"1 kHz calibration");
                check(std::abs(r.sample_peak+20)<.01,"Sample-peak calibration");
                if(chunk==17)partition=r.integrated;else check(std::abs(partition-r.integrated)<1e-5,"Block partition changed integrated loudness");
                if(rate==48000)reference[channels-1]=r.short_term;
                ++cases;
            }
        }
        check(std::abs((reference[1]-reference[0])-3.0103)<.001,"Mono/stereo weighting");
        const auto quieter=signal(48000,2,1024,.05);
        check(std::abs(quieter.short_term-reference[1]+6.0206)<.002,"Level scaling");
        // Test absolute and relative gates, rather than merely replaying library output.
        const auto below=signal(48000,1,1024,.00001);
        check(below.integrated_ready&&below.integrated==-120&&below.short_term<-90,"Absolute loudness gate");
        // After a stable loud section, add a much quieter section. The
        // integrated relative gate should reject it, while S follows the tail.
        nexus::LoudnessMeter gated({48000,1,1024});
        std::array<float,1024> gateSamples{};float* gatePlanes[]{gateSamples.data()};
        for(size_t offset=0;offset<48000*8;){
            const size_t count=(std::min)(size_t(1024),48000*8-offset);
            for(size_t i=0;i<count;++i){
                const auto frame=offset+i;
                gateSamples[i]=float((frame<48000*4?.1:.001)*std::sin(2*pi*1000*double(frame)/48000));
            }
            watching=true;const bool ok=gated.process({gatePlanes,1,count});watching=false;
            check(ok,"Relative-gate input");++blocks;offset+=count;
        }
        const auto gatedResult=gated.read();
        check(std::abs(gatedResult.integrated+23)<.5&&std::abs(gatedResult.short_term+63)<.08,"Relative loudness gate");
        const auto silent=signal(48000,2,1024,0);
        check(silent.integrated==-120&&silent.sample_peak==-120,"Silence floor");
        nexus::LoudnessMeter meter({48000,2,1024});
        std::array<float,1024> left{},right{};float* p[]{left.data(),right.data()};
        check(meter.process({nullptr,2,0}),"Empty meter block");
        check(!meter.process({nullptr,2,10})&&!meter.process({p,1,10})&&!meter.process({p,2,1025}),"Invalid block accepted");
        for(int j=0;j<18;++j)check(meter.process({p,2,1024}),"Warm-up");
        check(!meter.read().momentary_ready,"Momentary ready before 400 ms");
        for(int j=0;j<2;++j)check(meter.process({p,2,1024}),"400 ms");
        check(meter.read().momentary_ready&&!meter.read().short_term_ready,"Window readiness");
        left[9]=std::numeric_limits<float>::quiet_NaN();check(!meter.process({p,2,1024}),"NaN accepted");left[9]=0;
        left[9]=std::numeric_limits<float>::infinity();check(!meter.process({p,2,1024}),"Infinity accepted");left[9]=0;
        for(int j=0;j<5000;++j){watching=true;const bool ok=meter.process({p,2,1024});watching=false;check(ok,"Silence stress");++blocks;}
        // Instance construction while another instance processes: tables are
        // initialized once, never rewritten underneath the audio consumer.
        bool other_ok=true;std::thread control([&]{
            try{for(int j=0;j<100;++j){nexus::LoudnessMeter other({44100,1,1024});check(!other.read().active,"Instance contamination");}}
            catch(...){other_ok=false;}
        });
        for(int j=0;j<5000;++j){watching=true;const bool ok=meter.process({p,2,1024});const auto snapshot=meter.read();watching=false;
            check(ok&&std::isfinite(snapshot.integrated),"Concurrent lifecycle");++blocks;}
        control.join();check(other_ok,"Concurrent constructor");
        bool rejected=false;try{nexus::LoudnessMeter bad({48000,8,1024});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Unknown channel mapping accepted");
        check(allocations==0&&deallocations==0,"Heap activity in audio");
        std::ofstream report(argc>1?argv[1]:"phase7-loudness-report.json");
        report<<"{\n  \"passed\": true,\n  \"reference_cases\": "<<cases<<",\n  \"audio_blocks\": "<<blocks
              <<",\n  \"max_calibration_error_lu\": "<<error<<",\n  \"audio_allocations\": "<<allocations
              <<",\n  \"audio_deallocations\": "<<deallocations<<"\n}\n";
        report.flush();check(bool(report),"Report write");
        std::cout<<"PASS: "<<cases<<" loudness reference cases, "<<blocks<<" audio blocks, zero C/C++ heap calls; max calibration error "<<error<<" LU\n";
        return 0;
    }catch(const std::exception& e){watching=false;std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
