#include "dsp/latency.hpp"
#include <iostream>
using namespace nexus;
class Delay final:public Processor {
public:
    explicit Delay(size_t n):n_(n){}
    void prepare(const Format& f)override{data.assign(f.channels*n_,0);pos=0;}
    void reset()noexcept override{std::fill(data.begin(),data.end(),0.f);pos=0;}
    size_t latency_frames()const noexcept override{return n_;}
    void process(Block b)noexcept override{
        if(!n_)return;
        for(size_t i=0;i<b.frames;++i){
            for(size_t c=0;c<b.channels;++c)std::swap(data[c*n_+pos],b.planes[c][i]);
            if(++pos==n_)pos=0;
        }
    }
    size_t n_,pos=0;std::vector<float> data;
};
void check(bool value){if(!value)throw std::runtime_error("Latency alignment failed");}
int main(){
    try{
        size_t cases=0;
        for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)})
        for(size_t source:{size_t(0),size_t(16),size_t(257)})
        for(size_t chunk:{size_t(1),size_t(17),size_t(257)})
        for(bool bypass:{false,true}){
            Engine engine;
            engine.add(std::make_unique<FixedLatency>(std::make_unique<Delay>(source),513));
            engine.set_bypass(bypass);engine.prepare({rate,channels,257});
            check(engine.latency_frames()==513);
            std::vector<float> left(257),right(257);float* planes[]{left.data(),right.data()};
            for(size_t offset=0;offset<1500;){
                const auto count=std::min(chunk,1500-offset);
                for(size_t c=0;c<channels;++c)for(size_t i=0;i<count;++i)
                    planes[c][i]=offset+i==0?float(c+1):0;
                check(engine.process({planes,channels,count}));
                for(size_t c=0;c<channels;++c)for(size_t i=0;i<count;++i)
                    check(planes[c][i]==(offset+i==513?float(c+1):0));
                offset+=count;
            }
            engine.reset();++cases;
        }
        bool rejected=false;
        try{FixedLatency invalid(std::make_unique<Delay>(17),16);invalid.prepare({});}
        catch(const std::invalid_argument&){rejected=true;}check(rejected);
        auto changing=std::make_unique<Delay>(0);auto* raw=changing.get();
        FixedLatency guarded(std::move(changing),16);guarded.prepare({});
        raw->n_=1;float a[1]{1},b[1]{1};float* planes[]{a,b};
        guarded.process({planes,2,1});check(a[0]==0&&b[0]==0);
        raw->n_=0;guarded.reset();a[0]=b[0]=1;
        guarded.process({planes,2,1});check(a[0]==0&&b[0]==0);
        guarded.prepare({});a[0]=b[0]=1;guarded.process({planes,2,1});
        for(size_t i=0;i<16;++i){
            a[0]=b[0]=0;guarded.process({planes,2,1});
            check(a[0]==(i==15?1.f:0.f)&&b[0]==a[0]);
        }
        std::cout<<"Aligned impulse and bypass cases="<<cases<<"; budget rejection and latency mutation containment passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what();return 1;}
}
