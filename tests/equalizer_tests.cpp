#include "dsp/equalizer.hpp"
#include "bench/analysis.hpp"
#include <algorithm>
#include <complex>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace nexus;
constexpr double pi=3.14159265358979323846;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
double reference(const EqSettings& settings,double rate,double frequency){
    double result=0;
    for(const auto& b:settings.bands)if(settings.enabled&&b.enabled){
        const double w=2*pi*(std::min)(double(b.frequency),rate*.45)/rate;
        const double A=std::pow(10.,double(b.gain_db)/40),alpha=std::sin(w)/(2*b.q);
        const auto z=std::exp(std::complex<double>(0,-2*pi*frequency/rate));
        const auto numerator=1+alpha*A-2*std::cos(w)*z+(1-alpha*A)*z*z;
        const auto denominator=1+alpha/A-2*std::cos(w)*z+(1-alpha/A)*z*z;
        result+=20*std::log10(std::abs(numerator/denominator));
    }
    return result;
}
void configure(Equalizer& eq,const EqSettings& values,double rate,size_t channels=2){
    eq.set_enabled(values.enabled);for(size_t i=0;i<4;++i)require(eq.set_band(i,values.bands[i]),"Band config");
    eq.prepare({rate,channels,257});
}
void process(Equalizer& eq,std::vector<float>& l,std::vector<float>& r,size_t block=257){
    for(size_t pos=0;pos<l.size();){const auto n=(std::min)(block,l.size()-pos);float* planes[]{l.data()+pos,r.data()+pos};eq.process({planes,2,n});pos+=n;}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        double worstCurve=0,worstAudio=0;size_t cases=0;
        for(double rate:{44100.,48000.,96000.}){
            EqSettings settings;Equalizer neutral;configure(neutral,settings,rate);
            std::vector<float> l(4096),r(4096);for(size_t i=0;i<l.size();++i){l[i]=float(std::sin(double(i)*.13));r[i]=float(std::cos(double(i)*.17));}
            const auto originalL=l,originalR=r;process(neutral,l,r);
            require(l==originalL&&r==originalR,"Zero dB EQ must be bit-exact");
            for(size_t band=0;band<4;++band)
                for(float db:{-18.f,-6.f,6.f,18.f})
                    for(float q:{.2f,1.f,10.f}){
                        EqSettings values;values.bands[band].gain_db=db;values.bands[band].q=q;
                        for(double frequency:{20.,100.,500.,1000.,5000.,10000.,18000.}){
                            const double error=std::abs(equalizer_response_db(values,rate,frequency)-reference(values,rate,frequency));
                            worstCurve=(std::max)(worstCurve,error);require(error<1e-5,"EQ response model mismatch");
                        }
                        ++cases;
                    }
            for(bool enabled:{false,true}){
                EqSettings values;values.enabled=enabled;
                values.bands={{{120,4,.7f,true},{500,-6,1.5f,true},{2000,9,2,true},{6000,-3,1,true}}};
                for(double frequency:{100.,500.,1000.,2000.,6000.,12000.}){
                    Equalizer eq;configure(eq,values,rate);
                    const auto frames=static_cast<size_t>(rate);
                    std::vector<float> input(frames*2);
                    for(size_t i=0;i<input.size();++i)input[i]=float(.01*std::sin(2*pi*frequency*double(i)/rate));
                    auto left=input,right=input;process(eq,left,right);
                    require(left==right,"Per-channel EQ states differ");
                    if(!enabled)require(left==input,"Disabled EQ not transparent");
                    const std::vector<float> in(input.begin()+frames,input.end()),out(left.begin()+frames,left.end());
                    const double measured=nexus::bench::response_db(in,out,rate,frequency);
                    const double error=std::abs(measured-reference(values,rate,frequency));
                    worstAudio=(std::max)(worstAudio,error);require(error<.02,"Measured EQ response exceeds tolerance");
                    ++cases;
                }
            }
            EqSettings values;values.bands[0].gain_db=12;Equalizer a,b;
            configure(a,values,rate);configure(b,values,rate);
            auto first=originalL,firstR=originalR,second=originalL,secondR=originalR;
            process(a,first,firstR,17);process(b,second,secondR,257);require(first==second&&firstR==secondR,"EQ block invariance");
            values.bands[0]={10000,-12,3,true};a.set_band(0,values.bands[0]);b.set_band(0,values.bands[0]);
            first=second=originalL;firstR=secondR=originalR;process(a,first,firstR,17);process(b,second,secondR,257);
            require(first==second&&firstR==secondR,"EQ ramp depends on block size");
            a.reset();first.assign(4096,0);firstR=first;process(a,first,firstR);
            for(float v:first)require(v==0,"EQ reset left history");
        }
        // Isolated channels: signal in left must never leak into the right.
        EqSettings isolation;isolation.bands[0].gain_db=12;Equalizer eq;configure(eq,isolation,48000);
        std::vector<float> l(4096,.1f),r(4096,0);process(eq,l,r);for(float x:r)require(x==0,"EQ crosstalk");
        EqBandSettings bad;bad.q=0;require(!eq.set_band(0,bad),"Invalid Q accepted");
        bad={1000,19,1,true};require(!eq.set_band(0,bad),"Invalid gain accepted");
        bad={std::numeric_limits<float>::quiet_NaN(),0,1,true};require(!eq.set_band(0,bad),"NaN frequency accepted");
        require(!eq.set_band(4,EqBandSettings{}),"Invalid band index accepted");
        // Low-rate cutoff clamping must also agree with the independent reference.
        EqSettings low;low.bands[3]={20000,18,.2f,true};
        require(std::abs(equalizer_response_db(low,8000,3000)-reference(low,8000,3000))<1e-5,"Low-rate clamp");
        std::ofstream out(argv[1]);if(!out)return 3;
        out<<std::setprecision(12)<<"{\"schema_version\":1,\"passed\":true,\"cases\":"<<cases
           <<",\"max_curve_error_db\":"<<worstCurve<<",\"max_measured_error_db\":"<<worstAudio<<"}\n";
        out.flush();require(bool(out),"Report write");
        std::cout<<"PASS: "<<cases<<" EQ cases; curve error "<<worstCurve<<" dB, measured error "<<worstAudio<<" dB\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}