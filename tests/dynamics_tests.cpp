#include "dsp/dynamics.hpp"
#include "dsp/rack.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace nexus;
namespace {
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
double amplitude(double db){return std::pow(10.,db/20);}
double db(double v){return 20*std::log10(std::abs(v));}
struct Audio {
    std::array<std::array<float,1024>,2> data{};
    std::array<float*,2> pointers{data[0].data(),data[1].data()};
    void fill(float left,float right){data[0].fill(left);data[1].fill(right);}
    Block block(size_t channels,size_t frames){return {pointers.data(),channels,frames};}
};
float constant(Processor& processor,size_t channels,float input,size_t frames){
    Audio audio;float last=0;
    while(frames){const auto n=(std::min)(frames,size_t(1024));audio.fill(input,input*.25f);
        processor.process(audio.block(channels,n));last=audio.data[0][n-1];frames-=n;}
    return last;
}
std::vector<float> render(DynamicsType type,size_t chunk){
    Dynamics module(type);DynamicsSettings s;s.threshold_db=-18;s.ratio=4;s.attack_ms=2;s.release_ms=50;
    check(module.set_settings(s),"Render settings");module.prepare({48000,2,1024});
    Audio audio;std::vector<float> result;result.reserve(16384);
    for(size_t offset=0;offset<8192;){
        const auto n=(std::min)(chunk,8192-offset);
        for(size_t i=0;i<n;++i){const float v=float(.8*std::sin(.17*double(offset+i)));audio.data[0][i]=v;audio.data[1][i]=-v*.25f;}
        module.process(audio.block(2,n));
        for(size_t i=0;i<n;++i){result.push_back(audio.data[0][i]);result.push_back(audio.data[1][i]);}
        offset+=n;
    }return result;
}
}
int main(int argc,char** argv){
 try{
    size_t cases=0;double error=0;
    for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)})
    for(float threshold:{-36.f,-18.f,0.f})for(float ratio:{1.f,2.f,4.f,20.f})
    for(double inputDb:{-60.,-30.,-12.,0.,12.}){
        DynamicsSettings s;s.threshold_db=threshold;s.ratio=ratio;s.attack_ms=.1f;s.release_ms=10;
        for(auto type:{DynamicsType::compressor,DynamicsType::expander}){
            Dynamics module(type);check(module.set_settings(s),"Settings");module.prepare({rate,channels,1024});
            const float input=float(amplitude(inputDb));
            const double measured=db(constant(module,channels,input,size_t(rate*.3)))-db(input);
            const double level=db(input);
            const double expected=type==DynamicsType::compressor?
                (level>threshold?(threshold-level)*(1-1/double(ratio)):0):
                (level<threshold?(std::max)(-double(s.range_db),(level-threshold)*(ratio-1)):0);
            error=(std::max)(error,std::abs(measured-expected));check(std::abs(measured-expected)<.0001,"Static dynamics curve");
            check(module.latency_frames()==0,"Dynamics latency");++cases;
        }
    }
    for(double rate:{44100.,48000.,96000.}){
        DynamicsSettings s;s.threshold_db=-24;s.ratio=4;s.attack_ms=10;s.release_ms=40;
        Dynamics compressor(DynamicsType::compressor);compressor.set_settings(s);compressor.prepare({rate,1,1024});
        const size_t n=size_t(std::round(rate*.01));
        const double measured=db(constant(compressor,1,1,n));
        const double expected=-18*(1-std::exp(-double(n)/(rate*.01)));
        check(std::abs(measured-expected)<.00001,"Attack time constant");
        const size_t releaseFrames=size_t(std::round(rate*.04));
        const double after=db(constant(compressor,1,.001f,releaseFrames)/.001f);
        check(std::abs(after-measured*std::exp(-double(releaseFrames)/(rate*.04)))<.00002,"Release time constant");
        compressor.reset();const double reset=db(constant(compressor,1,1,n));
        check(std::abs(reset-measured)<.00001,"Dynamics reset");
        Dynamics limiter(DynamicsType::limiter);s.threshold_db=-1;s.release_ms=100;limiter.set_settings(s);limiter.prepare({rate,2,1024});
        Audio audio;
        for(size_t block=0;block<20;++block){
            for(size_t i=0;i<1024;++i){audio.data[0][i]=(i%7==0?5.f:.01f);audio.data[1][i]=-audio.data[0][i]*.25f;}
            limiter.process(audio.block(2,1024));
            for(size_t i=0;i<1024;++i){
                check(std::abs(audio.data[0][i])<=amplitude(-1)+1e-7,"Limiter overshoot");
                check(audio.data[1][i]==-audio.data[0][i]*.25f,"Stereo link");
            }
        }
        // A lower ceiling is effective at the next block, without a ramp.
        s.threshold_db=-12;limiter.set_settings(s);audio.fill(1,.25f);limiter.process(audio.block(2,1024));
        check(audio.data[0][0]<=amplitude(-12)+1e-7,"Lowered limiter ceiling");
        limiter.reset();audio.fill(std::numeric_limits<float>::max(),-std::numeric_limits<float>::max());limiter.process(audio.block(2,1024));
        check(std::isfinite(audio.data[0][0])&&std::abs(audio.data[0][0])<=amplitude(-12)+1e-7,"Limiter large finite input");
        // Gate hysteresis/hold; output remains open inside the 3 dB band.
        Dynamics gate(DynamicsType::expander);s={};s.gate=true;s.threshold_db=-20;s.attack_ms=.1f;s.release_ms=10;s.range_db=60;s.hold_ms=20;
        gate.set_settings(s);gate.prepare({rate,1,1024});
        check(std::abs(db(constant(gate,1,.01f,1)/.01f)+60)<.0001,"Gate starts closed");
        constant(gate,1,.2f,size_t(rate*.1));
        check(std::abs(constant(gate,1,.09f,size_t(rate*.1))/.09f-1)<1e-6,"Gate hysteresis");
        check(std::abs(constant(gate,1,.05f,size_t(rate*.02))/.05f-1)<1e-6,"Gate hold");
        const double firstClosed=db(constant(gate,1,.05f,1)/.05f);
        check(std::abs(firstClosed+60*(1-std::exp(-1/(rate*.01))))<.00002,"Gate hold sample boundary");
        check(db(constant(gate,1,.05f,size_t(rate*.3))/.05f)<-59.99,"Gate closure/range");
        check(std::abs(constant(gate,1,.2f,size_t(rate*.1))/.2f-1)<1e-6,"Gate reopen");
    }
    // Output makeup is independent of reported reduction.
    Dynamics makeup(DynamicsType::compressor);DynamicsSettings makeupSettings;makeupSettings.threshold_db=-24;makeupSettings.ratio=4;makeupSettings.makeup_db=6;makeupSettings.attack_ms=.1f;
    makeup.set_settings(makeupSettings);makeup.prepare({48000,1,1024});
    check(std::abs(db(constant(makeup,1,1,4800))+12)<.0001&&std::abs(makeup.gain_reduction_db()-18)<.0001,"Makeup versus reduction");
    // All eight channels share the detector, without cross-channel summing.
    Dynamics multichannel(DynamicsType::limiter);DynamicsSettings multiSettings;multiSettings.threshold_db=-6;
    multichannel.set_settings(multiSettings);multichannel.prepare({48000,8,1024});
    std::array<std::array<float,17>,8> multi{};std::array<float*,8> multiPlanes{};
    for(size_t c=0;c<8;++c){multi[c].fill(c==7?1.f:.5f);multiPlanes[c]=multi[c].data();}
    multichannel.process({multiPlanes.data(),8,17});
    check(std::abs(multi[7][0]-amplitude(-6))<1e-7&&multi[0][0]==multi[7][0]*.5f,"Multichannel linked detector");
    for(auto type:{DynamicsType::compressor,DynamicsType::expander,DynamicsType::limiter}){
        const auto reference=render(type,1024);
        check(reference==render(type,17)&&reference==render(type,257),"Block invariance");
    }
    StereoWidth width;check(!width.set_width(-1)&&!width.set_width(3)&&!width.set_width(std::numeric_limits<float>::quiet_NaN()),"Width bounds");
    width.prepare({48000,2,1024});Audio audio;audio.fill(1,-.5f);width.process(audio.block(2,1024));
    check(audio.data[0][0]==1&&audio.data[1][0]==-.5f,"Width unity");
    width.set_width(0);width.reset();audio.fill(1,-.5f);width.process(audio.block(2,1024));
    check(audio.data[0][0]==.25f&&audio.data[1][0]==.25f,"Width mono");
    width.set_width(2);width.reset();audio.fill(1,-.5f);width.process(audio.block(2,1024));
    check(audio.data[0][0]==1.75f&&audio.data[1][0]==-1.25f,"Width expansion");
    bool rejected=false;try{width.prepare({48000,1,1024});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Width mono format");
    for(const auto& field:dynamics_fields){
        for(float v:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),field.low-1,field.high+1}){
            DynamicsSettings s;s.*(field.member)=v;Dynamics module(DynamicsType::compressor);
            check(!valid_dynamics(s)&&!module.set_settings(s),"Invalid dynamic setting");
        }
    }
    // Order matters: width can create peaks, so the limiter must be last to cap them.
    auto w=default_node(ModuleKind::stereo_width,1);w.value=2;
    auto l=default_node(ModuleKind::limiter,2);l.dynamics.threshold_db=-6;
    LiveRack rack({48000,2,1024},{w,l});audio.fill(1,-1);check(rack.process(audio.block(2,1024)),"Native dynamics live graph");
    check(std::abs(audio.data[0][0])<=amplitude(-6)+1e-7&&rack.gain_reduction_db(2)>11.9,"Rack limit and reduction telemetry");
    l.enabled=false;rack.publish({w,l});check(rack.gain_reduction_db(2)==0,"Bypassed reduction");
    std::ofstream report(argc>1?argv[1]:"phase7-dynamics-report.json");
    report<<"{\n  \"passed\": true,\n  \"static_curve_cases\": "<<cases<<",\n  \"max_curve_error_db\": "<<error<<"\n}\n";
    report.flush();check(bool(report),"Report write");
    std::cout<<"PASS: "<<cases<<" static curves, attack/release, gate hysteresis/hold, limiter ceilings, width, state and block invariance; max error "<<error<<" dB\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
