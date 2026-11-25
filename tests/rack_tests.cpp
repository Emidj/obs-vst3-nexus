#include "dsp/rack.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <stdexcept>
using namespace nexus;
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void fill(std::vector<float>& l,std::vector<float>& r,float a=1,float b=0){std::fill(l.begin(),l.end(),a);std::fill(r.begin(),r.end(),b);}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        const Format format{48000,2,1024};
        std::vector<float> left(1024),right(1024);float* planes[]{left.data(),right.data()};
        auto mono=default_node(ModuleKind::mono,1),balance=default_node(ModuleKind::balance,2);balance.value=1;
        LiveRack rack(format,{mono,balance});fill(left,right);check(rack.process({planes,2,1024}),"Initial rack");
        check(left.back()==0&&right.back()==.5f,"Module order");
        rack.publish({balance,mono});fill(left,right);check(rack.process({planes,2,1024}),"Reordered rack");
        check(left.back()==0&&right.back()==0,"Reordering did not change processing");
        rack.collect();check(rack.retained_graphs()==1,"Retired graph not reclaimed");
        auto gain=default_node(ModuleKind::gain,3);gain.value=-6;
        rack.publish({gain});fill(left,right,1,1);rack.process({planes,2,1024});
        check(std::abs(left.back()-float(std::pow(10.,-6./20)))<1e-6,"New chain gain");
        gain.enabled=false;rack.publish({gain});fill(left,right,1,1);rack.process({planes,2,1024});
        check(left.back()==1,"Per-module bypass");
        rack.set_bypass(true);rack.publish({mono});fill(left,right);rack.process({planes,2,1024});
        check(left.back()==1&&right.back()==0,"Rack bypass after graph change");
        rack.set_bypass(false);rack.publish({});fill(left,right,.25f,-.5f);rack.process({planes,2,1024});
        check(left.back()==.25f&&right.back()==-.5f,"Empty rack");
        // Superseded pending chains must not accumulate, even with stopped audio.
        for(size_t i=0;i<1000;++i){
            auto n=default_node(i%2?ModuleKind::mono:ModuleKind::polarity,i+100);
            rack.publish({n});check(rack.retained_graphs()<=3,"Pending chain ownership leak");
        }
        for(int i=0;i<4;++i){fill(left,right);rack.process({planes,2,1024});}
        rack.collect();check(rack.retained_graphs()==1,"Ownership not bounded after draining");
        auto initialGain=gain;initialGain.enabled=true;
        LiveRack initialBypass(format,{initialGain},true);fill(left,right,1,1);initialBypass.process({planes,2,1024});
        check(left.front()==1,"Restored bypass processed the first samples");
        LiveRack sanitizing(format,{});fill(left,right);left[0]=std::numeric_limits<float>::quiet_NaN();
        sanitizing.process({planes,2,1024});check(left[0]==0&&sanitizing.fault_count()==1,"Non-finite input containment");
        RackConfig excessive;
        for(uint64_t id=1;id<=16;++id){auto n=default_node(ModuleKind::equalizer,id);for(auto& band:n.eq.bands)band={1000,18,1,true};excessive.push_back(n);}
        LiveRack recovery(format,excessive);
        for(size_t block=0;block<40;++block){
            for(size_t i=0;i<1024;++i)left[i]=right[i]=float(.1*std::sin(2*3.14159265358979323846*1000*double(block*1024+i)/48000));
            recovery.process({planes,2,1024});for(float value:left)check(std::isfinite(value),"Non-finite output escaped rack");
        }check(recovery.fault_count()>0,"Overflow fixture did not exercise recovery");
        bool rejected=false;
        try{rack.publish({gain,gain});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Duplicate IDs accepted");
        rejected=false;auto bad=gain;bad.value=std::numeric_limits<float>::quiet_NaN();
        try{rack.publish({bad});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"NaN accepted");
        // Parameter publication must retain filter history (no graph replacement).
        auto eqNode=default_node(ModuleKind::equalizer,1);eqNode.eq.bands[0]={100,12,2,true};
        LiveRack stateful(format,{eqNode});Equalizer reference;
        reference.set_band(0,eqNode.eq.bands[0]);reference.prepare(format);
        std::vector<float> rl(1024),rr(1024);float* rp[]{rl.data(),rr.data()};
        for(size_t block=0;block<20;++block){
            for(size_t i=0;i<1024;++i)left[i]=right[i]=rl[i]=rr[i]=float(.01*std::sin(double(block*1024+i)*.019));
            if(block==10){eqNode.eq.bands[0].gain_db=-6;stateful.publish({eqNode});reference.set_band(0,eqNode.eq.bands[0]);}
            stateful.process({planes,2,1024});reference.process({rp,2,1024});
            check(left==rl&&right==rr,"Parameter edit reset DSP history");
        }
        LiveRack stressed(format,{});std::atomic<bool> start{false},done{false};std::atomic<unsigned> edits{0};size_t invalid=0;
        std::thread control([&]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            for(unsigned i=0;i<5000;++i){
                auto n=default_node(ModuleKind::gain,i+1);n.value=i%2?-6.f:0.f;
                stressed.publish(i%3?RackConfig{n}:RackConfig{});++edits;
            }done.store(true,std::memory_order_release);
        });
        start.store(true,std::memory_order_release);
        size_t blocks=0;
        while(!done.load(std::memory_order_acquire)||blocks<20000){
            fill(left,right,.1f,.1f);
            if(!stressed.process({planes,2,257}))++invalid;
            for(size_t i=0;i<257;++i)if(!std::isfinite(left[i])||left[i]<.04f||left[i]>.10001f)++invalid;
            ++blocks;
        }
        control.join();check(invalid==0,"Concurrent graph replacement output invalid");
        for(int i=0;i<5;++i){fill(left,right);stressed.process({planes,2,1024});}
        stressed.collect();check(stressed.retained_graphs()==1,"Concurrent retired graphs leaked");
        // A stable budget aligns dry, wet and replacement graphs sample-for-sample.
        size_t latency_cases=0;
        for(size_t budget:{size_t(0),size_t(16),size_t(513)})
        for(size_t chunk:{size_t(1),size_t(17),size_t(257)})
        for(bool initial_bypass:{false,true}){
            LiveRack aligned(format,{},initial_bypass,budget);
            check(aligned.latency_frames()==budget,"Rack latency report");
            uint64_t next_id=10;
            for(size_t offset=0,iteration=0;offset<8000;++iteration){
                const auto count=std::min(chunk,size_t(8000)-offset);
                if(iteration%7==0){
                    aligned.publish({default_node(ModuleKind::gain,next_id++)});
                    aligned.set_bypass(iteration%2==0);
                }
                for(size_t i=0;i<count;++i){
                    left[i]=float((offset+i)%127)*.001f;
                    right[i]=-left[i];
                }
                check(aligned.process({planes,2,count}),"Aligned graph process");
                for(size_t i=0;i<count;++i){
                    const float expected=offset+i<budget?0.f:float((offset+i-budget)%127)*.001f;
                    check(left[i]==expected&&right[i]==-expected,"Graph change or bypass broke latency alignment");
                }
                aligned.collect();check(aligned.retained_graphs()<=3,"Warming graph ownership leak");
                offset+=count;
            }
            ++latency_cases;
        }
        bool invalid_budget=false;
        try{LiveRack invalid(format,{},false,96001);}
        catch(const std::invalid_argument&){invalid_budget=true;}
        check(invalid_budget,"Excessive rack latency accepted");
        std::cout<<"Aligned LiveRack transition cases="<<latency_cases<<"\n";
        std::ofstream report(argv[1]);if(!report)return 3;
        report<<"{\"passed\":true,\"schema_version\":1,\"concurrent_edits\":"<<edits<<",\"audio_blocks\":"<<blocks<<",\"remaining_graphs\":"<<stressed.retained_graphs()<<"}\n";
        report.flush();check(bool(report),"Report write");
        std::cout<<"PASS: live rack order, transitions, state retention, bounded retirement and "<<edits<<" concurrent edits\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}