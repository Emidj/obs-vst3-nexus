#include "dsp/basic.hpp"
#include "bench/analysis.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace nexus;
constexpr double pi=3.14159265358979323846;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double actual,double expected,double tolerance,const char* text){check(std::abs(actual-expected)<=tolerance,text);}
template<class P> void prepare(P& p,double rate,size_t channels){p.prepare({rate,channels,257});}
template<class P> void process(P& p,std::vector<float>& l,std::vector<float>& r,size_t block=257){
    for(size_t at=0;at<l.size();){
        const size_t n=(std::min)(block,l.size()-at);
        float* planes[]{l.data()+at,r.data()+at};p.process({planes,2,n});at+=n;
    }
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        size_t cases=0;double worstFilter=0;
        for(double rate:{44100.,48000.,96000.}){
            for(float db:{-60.f,-6.f,0.f,6.f,24.f}){
                Gain g;check(g.set_db(db),"Valid gain rejected");prepare(g,rate,2);
                std::vector<float> l(1024,.125f),r(1024,-.25f);process(g,l,r);
                const double amp=std::pow(10.,double(db)/20);
                near(l.back(),.125*amp,1e-6,"Gain amplitude");near(r.back(),-.25*amp,1e-6,"Gain sign");++cases;
            }
            for(float position:{-1.f,0.f,1.f}){
                Balance b;b.set_position(position);prepare(b,rate,2);
                std::vector<float> l(1024,1),r(1024,1);process(b,l,r);
                near(l.back(),position>0?0:1,0,"Balance left");near(r.back(),position<0?0:1,0,"Balance right");
                Pan pan;pan.set_position(position);prepare(pan,rate,2);
                std::fill(l.begin(),l.end(),1.f);std::fill(r.begin(),r.end(),1.f);process(pan,l,r);
                near(double(l.back())*l.back()+double(r.back())*r.back(),1,1e-6,"Pan constant power");
                if(position==0)near(l.back(),std::sqrt(.5),1e-6,"Pan center law");
                if(position==1)check(l.back()==0,"Pan right endpoint");
                if(position==-1)check(r.back()==0,"Pan left endpoint");++cases;
            }
            Mono mono;mono.set_enabled(true);prepare(mono,rate,2);
            std::vector<float> l(1024,1),r(1024,-1);process(mono,l,r);
            check(l.back()==0 && r.back()==0,"Mono opposite polarity cancellation");
            std::fill(l.begin(),l.end(),1.f);std::fill(r.begin(),r.end(),0.f);process(mono,l,r);
            check(l.back()==.5f && r.back()==.5f,"Mono arithmetic average");
            Polarity polarity;polarity.set_inverted(true);prepare(polarity,rate,2);process(polarity,l,r);
            check(l.back()==-.5f && r.back()==-.5f,"Polarity inversion");++cases;
            for(PassType type:{PassType::low_pass,PassType::high_pass})
                for(double hz:{20.,100.,1000.,4000.,10000.}){
                    PassFilter filter(type);filter.set_cutoff(1000);filter.set_enabled(true);prepare(filter,rate,2);
                    const auto count=static_cast<size_t>(rate);
                    std::vector<float> input(count*2),left(count*2),right(count*2);
                    for(size_t i=0;i<input.size();++i)input[i]=float(.25*std::sin(2*pi*hz*double(i)/rate));
                    left=right=input;process(filter,left,right);
                    check(left==right,"Filter channel state mismatch");
                    std::vector<float> inputTail(input.begin()+count,input.end()),outputTail(left.begin()+count,left.end());
                    const double ratio=std::tan(pi*hz/rate)/std::tan(pi*1000/rate);
                    const double magnitude=1/std::sqrt(1+std::pow(type==PassType::low_pass?ratio:1/ratio,4));
                    const double measured=nexus::bench::response_db(inputTail,outputTail,rate,hz);
                    const double error=std::abs(measured-20*std::log10(magnitude));
                    worstFilter=(std::max)(worstFilter,error);check(error<.01,"Filter analytic response outside 0.01 dB tolerance");
                    filter.reset();left.assign(1024,0);right.assign(1024,0);process(filter,left,right);
                    for(float x:left)check(x==0,"Filter reset left stale history");
                    PassFilter disabled(type);prepare(disabled,rate,2);
                    left=input;right=input;process(disabled,left,right);
                    check(left==input && right==input,"Disabled filter is not exact passthrough");++cases;
                }
            // Constant settings must produce the same samples regardless of block boundaries.
            PassFilter a(PassType::low_pass),b(PassType::low_pass);
            a.set_enabled(true);b.set_enabled(true);prepare(a,rate,2);prepare(b,rate,2);
            l.resize(4096);for(size_t i=0;i<l.size();++i)l[i]=float(std::sin(double(i)*.17));
            auto first=l,second=l,firstR=l,secondR=l;process(a,first,firstR,257);process(b,second,secondR,17);
            check(first==second,"Filter block invariance");
            // Shared enable/cutoff ramps must also be block-size independent.
            a.set_enabled(false);b.set_enabled(false);a.set_cutoff(10000);b.set_cutoff(10000);
            first=second=firstR=secondR=l;process(a,first,firstR,257);process(b,second,secondR,17);
            check(first==second,"Filter ramp block invariance");++cases;
        }
        // Native mono operations must not manufacture a second plane.
        float data[]{1,-.5f,0};float* plane[]{data};
        Balance balance;prepare(balance,48000,1);balance.set_position(1);balance.process({plane,1,3});
        Mono mono;prepare(mono,48000,1);mono.set_enabled(true);mono.process({plane,1,3});
        check(data[0]==1 && data[1]==-.5f,"Mono routing unexpectedly changed samples");
        Gain gain;check(!gain.set_db(25) && !gain.set_db(-61) && !gain.set_db(std::numeric_limits<float>::infinity()),"Invalid gain accepted");
        check(!balance.set_position(2),"Invalid balance accepted");
        PassFilter pass(PassType::low_pass);check(!pass.set_cutoff(0) && !pass.set_cutoff(20001) && !pass.set_cutoff(std::numeric_limits<float>::quiet_NaN()),"Invalid cutoff accepted");
        bool rejected=false;try{Pan pan;prepare(pan,48000,1);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Pan without stereo accepted");
        // Additional low-rate safety: clamp effective cutoff below Nyquist.
        pass.set_cutoff(20000);pass.set_enabled(true);prepare(pass,8000,1);
        for(int k=0;k<100;++k){pass.process({plane,1,3});for(float x:data)check(std::isfinite(x),"Low-rate filter instability");}
        std::ofstream report(argv[1]);if(!report)return 3;
        report<<std::setprecision(12)<<"{\"schema_version\":1,\"passed\":true,\"cases\":"<<cases
              <<",\"max_filter_response_error_db\":"<<worstFilter<<",\"tolerance_db\":0.01}\n";
        report.flush();check(bool(report),"Report write failed");
        std::cout<<"PASS: "<<cases<<" basic DSP cases; filter maximum response error "<<worstFilter<<" dB\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}