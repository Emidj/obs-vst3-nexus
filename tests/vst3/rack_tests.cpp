#include "vst3/rack_factory.hpp"
#include "vst3/core.hpp"
#include "vst3/keyboard.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <thread>
#include <chrono>
using namespace nexus;
namespace {
thread_local bool watching=false;
thread_local size_t allocations=0,deallocations=0;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid rack update accepted");}
}
void* operator new(size_t n){if(watching)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(watching&&p)++deallocations;std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
int main(int argc,char** argv){
 try{
    check(argc==2,"Fixture path required");
    std::string id,instrument;
    for(const auto& info:vst3::inspect(argv[1])){
        if(info.name=="NEXUS Gain16 fixture")id=info.id;
        if(info.name=="Rejected instrument fixture")instrument=info.id;
    }
    check(!id.empty()&&!instrument.empty(),"Fixture identities");
    auto node=default_node(ModuleKind::vst3,1);
    node.vst3.path=argv[1];node.vst3.class_id=id;node.vst3.parameters={{7,.25}};
    auto factory=vst3::rack_factory();
    std::array<float,257> left{},right{};float* planes[]{left.data(),right.data()};
    size_t cases=0;
    for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)})
    for(size_t chunk:{size_t(1),size_t(17),size_t(257)})for(bool bypass:{false,true}){
        LiveRack rack({rate,channels,257},{node},bypass,513,factory);
        for(size_t offset=0;offset<1600;){
            const auto count=std::min(chunk,size_t(1600)-offset);
            left.fill(0);right.fill(0);if(offset==0)left[0]=right[0]=.8f;
            watching=true;const auto ok=rack.process({planes,channels,count});watching=false;
            check(ok,"VST3 rack process");
            for(size_t c=0;c<channels;++c)for(size_t i=0;i<count;++i)
                check(planes[c][i]==(offset+i==513?(bypass?.8f:.4f):0.f),"Rack VST3 impulse alignment");
            offset+=count;
        }++cases;
    }
    // Native, one VST3 (16 samples), and two VST3 (32 samples) all
    // become the same identity processor at a shared budget of 513 samples.
    node.vst3.parameters={{7,.5}};
    LiveRack changing({48000,2,257},{},false,513,factory);
    uint64_t next_id=10;
    for(size_t block=0,offset=0;block<180;++block){
        if(block%9==0){
            auto a=node,b=node;a.id=next_id++;b.id=next_id++;
            changing.publish((block/9)%3==0?RackConfig{}:((block/9)%3==1?RackConfig{a}:RackConfig{a,b}));
            changing.set_bypass(block%2==0);
        }
        for(size_t i=0;i<257;++i){left[i]=float((offset+i)%127)*.001f;right[i]=-left[i];}
        watching=true;const auto ok=changing.process({planes,2,257});watching=false;
        check(ok,"Changing VST3 rack");
        for(size_t i=0;i<257;++i){
            const auto expected=offset+i<513?0.f:float((offset+i-513)%127)*.001f;
            check(left[i]==expected&&right[i]==-expected,"Different VST3 chain latencies lost alignment");
        }
        changing.collect();offset+=257;
    }
    // Failure while constructing a replacement must leave the live chain valid.
    auto bad=node;bad.id=900;bad.vst3.path+=".missing.vst3";
    rejects([&]{changing.publish({bad});});
    left.fill(.2f);right.fill(.2f);
    for(size_t i=0;i<8;++i){left.fill(.2f);right.fill(.2f);changing.process({planes,2,257});}
    check(left.back()==.2f&&right.back()==.2f,"Failed update replaced previous chain");
    bad=node;bad.vst3.parameters={{8,.5}};rejects([&]{changing.publish({bad});});
    bad=node;bad.vst3.parameters={{7,.5},{7,.25}};rejects([&]{changing.publish({bad});});
    bad=node;bad.vst3.class_id=instrument;rejects([&]{changing.publish({bad});});
    rejects([&]{LiveRack noHost({48000,2,257},{node},false,513);});
    rejects([&]{LiveRack noBudget({48000,2,257},{node},false,15,factory);});
    // Same ID, different VST3 settings requires a fresh prepared instance.
    auto edited=node;edited.id=42;edited.vst3.parameters={{7,.25}};
    changing.publish({edited});
    for(size_t i=0;i<8;++i){left.fill(.8f);right.fill(.8f);changing.process({planes,2,257});}
    changing.set_bypass(false);
    for(size_t i=0;i<8;++i){left.fill(.8f);right.fill(.8f);changing.process({planes,2,257});}
    check(left.back()==.4f&&right.back()==.4f,"VST3 setting replacement ignored");
    edited.enabled=false;changing.publish({edited});
    for(size_t i=0;i<8;++i){left.fill(.8f);right.fill(.8f);changing.process({planes,2,257});}
    check(left.back()==.8f&&right.back()==.8f,"VST3 per-module bypass");
    edited.enabled=true;changing.publish({edited});
    for(size_t i=0;i<8;++i){left.fill(.8f);right.fill(.8f);changing.process({planes,2,257});}
    check(left.back()==.4f&&right.back()==.4f,"VST3 per-module re-enable");
    // An explicitly supplied stopped component snapshot restores its gain.
    vst3::Effect source(node.vst3.path,id);
    source.set_parameter(7,.25);source.prepare({48000,2,257});
    left.fill(.8f);right.fill(.8f);source.process({planes,2,257});source.suspend();
    const auto state=source.capture_state();
    auto restored=node;restored.vst3.parameters.clear();restored.vst3.has_state=true;
    restored.vst3.component_state=state.component;restored.vst3.controller_state=state.controller;
    LiveRack saved({48000,2,257},{restored},false,16,factory);
    for(size_t i=0;i<3;++i){left.fill(.8f);right.fill(.8f);saved.process({planes,2,257});}
    check(left.back()==.4f&&right.back()==.4f,"Rack snapshot restoration");
    // Run audio on a separate thread while all VST3 lifecycle/state stays here.
    {
        auto feedback=node;feedback.vst3.parameters={{7,.625}};
        LiveRack live({48000,2,257},{feedback},false,513,factory);
        rejects([&]{live.capture_snapshot();});
        struct Runner {
            LiveRack& rack;std::atomic<bool> stop{false},valid{true};
            std::atomic<size_t> blocks{0},heap{0};std::thread thread;
            explicit Runner(LiveRack& r):rack(r),thread([this]{
                std::array<float,257> a{},b{};float* p[]{a.data(),b.data()};
                while(!stop.load()){
                    a.fill(.2f);b.fill(.2f);watching=true;
                    const auto ok=rack.process({p,2,257});watching=false;
                    if(!ok)valid.store(false);
                    for(float sample:a)if(!std::isfinite(sample))valid.store(false);
                    blocks.fetch_add(1);
                    std::this_thread::yield();
                }
                heap.store(allocations+deallocations);
            }){}
            void finish(){stop.store(true);if(thread.joinable())thread.join();}
            ~Runner(){finish();}
        } runner(live);
        auto wait=[&](auto predicate){
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!predicate()){
                check(std::chrono::steady_clock::now()<end,"Snapshot handshake timeout");
                std::this_thread::yield();
            }
        };
        wait([&]{return runner.blocks.load()>20;});
        for(size_t i=0;i<12;++i){
            live.request_snapshot();rejects([&]{live.request_snapshot();});
            rejects([&]{live.publish({});});
            wait([&]{return live.snapshot_ready();});
            const auto atPause=runner.blocks.load();
            wait([&]{return runner.blocks.load()>atPause+5;});
            const auto snapshot=live.capture_snapshot();
            check(snapshot.size()==1&&snapshot[0].vst3.has_state&&snapshot[0].vst3.parameters.empty(),"Live snapshot fields");
            LiveRack clone({48000,2,257},snapshot,false,513,factory);
            for(size_t n=0;n<6;++n){left.fill(.2f);right.fill(.2f);clone.process({planes,2,257});}
            check(left.back()==.2f,"Snapshot reapplied stale parameter over processor feedback");
            wait([&]{return !live.snapshot_busy();});
        }
        // Cancellation works both before acknowledgement and while parked.
        live.request_snapshot();live.cancel_snapshot();wait([&]{return !live.snapshot_busy();});
        live.request_snapshot();wait([&]{return live.snapshot_ready();});
        live.cancel_snapshot();wait([&]{return !live.snapshot_busy();});
        check(runner.valid.load(),"Snapshot concurrent output invalid");
        runner.finish();check(runner.heap.load()==0,"Snapshot allocated on audio");
        check(runner.blocks.load()>20,"Snapshot concurrent coverage");
    }
    {
        auto failing=node;failing.vst3.parameters={{7,.9}};
        LiveRack live({48000,2,257},{failing},false,513,factory);
        auto pump=[&]{left.fill(.2f);right.fill(.2f);check(live.process({planes,2,257}),"Failed snapshot audio");};
        for(size_t i=0;i<8;++i)pump();
        live.request_snapshot();
        for(size_t i=0;i<20&&!live.snapshot_ready();++i)pump();
        check(live.snapshot_ready(),"Failure snapshot acknowledgement");
        rejects([&]{live.capture_snapshot();});
        for(size_t i=0;i<20&&live.snapshot_busy();++i)pump();
        check(!live.snapshot_busy(),"Failed capture left plugin suspended");
        for(size_t i=0;i<8;++i)pump();
        check(std::abs(left.back()-.36f)<1e-6,"Capture failure changed effect gain");
        auto replacement=node;replacement.id=9000;live.publish({replacement});
        rejects([&]{live.request_snapshot();});
    }
    {
        // A lifecycle failure after capture must park on dry, then permit
        // explicit recovery without touching the failed processor in audio.
        bool fail_prepare=false;
        class Broken final:public RackStatefulProcessor{
        public:
            bool& fail;explicit Broken(bool& f):fail(f){}
            void prepare(const Format&)override{if(fail)throw std::runtime_error("Fixture prepare failure");}
            void reset()noexcept override{}
            void process(Block)noexcept override{}
            Vst3Settings capture_vst3()override{throw std::runtime_error("Fixture capture failure");}
        };
        LiveRack live({48000,2,257},{node},false,16,
            [&](const RackNode&)->std::unique_ptr<Processor>{return std::make_unique<Broken>(fail_prepare);});
        live.request_snapshot();
        for(size_t i=0;i<20&&!live.snapshot_ready();++i){left.fill(.2f);right.fill(.2f);live.process({planes,2,257});}
        check(live.snapshot_ready(),"Broken fixture pause");
        fail_prepare=true;rejects([&]{live.capture_snapshot();});
        check(live.snapshot_busy()&&!live.snapshot_ready()&&live.fault_count()==1,"Recovery failure was not quarantined");
        for(size_t i=0;i<3;++i){left.fill(.2f);right.fill(.2f);live.process({planes,2,257});check(left.back()==.2f,"Parked recovery did not output aligned dry");}
        rejects([&]{live.cancel_snapshot();});
        fail_prepare=false;live.cancel_snapshot();
        for(size_t i=0;i<20&&live.snapshot_busy();++i){left.fill(.2f);right.fill(.2f);live.process({planes,2,257});}
        check(!live.snapshot_busy(),"Explicit recovery did not resume");
        live.request_snapshot();
        for(size_t i=0;i<20&&!live.snapshot_ready();++i){left.fill(.2f);right.fill(.2f);live.process({planes,2,257});}
        fail_prepare=true;rejects([&]{live.capture_snapshot();});check(live.snapshot_failed(),"Failed snapshot not exposed");
        fail_prepare=false;const RackConfig empty;live.rebuild_snapshot(&empty);
        for(size_t i=0;i<20&&live.snapshot_busy();++i){left.fill(.2f);right.fill(.2f);live.process({planes,2,257});}
        check(!live.snapshot_busy(),"Replacing failed parked graph did not recover");
    }
    {
        LiveRack live({48000,2,257},{},false,513,factory);
        auto next=node;next.id=777;live.publish({next});
        left.fill(.2f);right.fill(.2f);live.process({planes,2,17});
        live.request_snapshot(); // Existing structural fade must finish first.
        for(size_t i=0;i<100&&!live.snapshot_ready();++i){
            left.fill(.2f);right.fill(.2f);live.process({planes,2,17});
        }
        check(live.snapshot_ready(),"Snapshot did not wait for structural fade");
        const auto snapshot=live.capture_snapshot();
        check(snapshot.size()==1&&snapshot[0].id==777,"Snapshot captured obsolete fading graph");
    }
    for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)})
    for(double trigger:{.54,.535,.536}){
        auto n=node;n.vst3.parameters={{7,trigger}};
        LiveRack live({rate,channels,257},{n},false,0,factory,true);
        check(live.latency_frames()==16,"Adaptive initial latency");
        left.fill(.2f);right.fill(.2f);watching=true;live.process({planes,channels,257});watching=false;
        check(live.service_control(),"Controller restart was not serviced");
        live.request_snapshot();
        for(size_t i=0;i<100&&!live.snapshot_ready();++i){left.fill(.2f);right.fill(.2f);watching=true;live.process({planes,channels,17});watching=false;}
        check(live.snapshot_ready(),"Adaptive restart pause");
        const auto rebuilt=live.rebuild_snapshot();check(rebuilt[0].vst3.has_state,"Rebuild did not preserve state");
        for(size_t i=0;i<20;++i){left.fill(.2f);right.fill(.2f);watching=true;live.process({planes,channels,257});watching=false;}
        check(live.latency_frames()==(trigger==.54?32:16)&&!live.snapshot_busy(),"Adaptive restart latency/resume");
        check(!live.service_control(),"Recreated instance requested endless restart");
        const float expected=.2f*float((trigger==.54?.55:(trigger==.535?.56:.57))*2);
        check(std::abs(left.back()-expected)<.000001f,"Adaptive restart lost applied parameter");
        // Returning to a native empty graph removes all plugin latency.
        live.publish({});left.fill(.2f);right.fill(.2f);live.process({planes,channels,257});
        check(live.latency_frames()==0,"Adaptive latency did not return to zero");
    }
    {
        LiveRack* owner=nullptr;bool entered=false;
        class Nested final:public RackStatefulProcessor {
        public:
            std::function<void()> callback;explicit Nested(std::function<void()> value):callback(std::move(value)){}
            void prepare(const Format&)override{}void reset()noexcept override{}void process(Block)noexcept override{}
            Vst3Settings capture_vst3()override{return {};}
            bool open_editor(bool)override{callback();return true;}
        };
        LiveRack nested({48000,2,257},{node},false,0,[&](const RackNode&)->std::unique_ptr<Processor>{return std::make_unique<Nested>([&]{
            entered=true;check(owner->control_busy(),"Nested GUI callback not guarded");
            check(!owner->editor_dirty()&&owner->gain_reduction_db(1)==0,"Reentrant diagnostics blocked");
            rejects([&]{owner->publish({});});rejects([&]{owner->service_control();});
        });});owner=&nested;
        nested.open_editor(node.id,false);check(entered&&!nested.control_busy(),"Editor lifecycle guard not released");nested.publish({});
    }
    {
        LiveRack callbackRack({48000,2,257},{},false,0,{},false,vst3::editor_callback_active);
        {vst3::EditorCallbackGuard callback;check(callbackRack.control_busy(),"Plugin keyboard callback not guarded");rejects([&]{callbackRack.publish({});});}
        check(!callbackRack.control_busy(),"Callback guard leaked");callbackRack.publish({});
    }
    check(allocations==0&&deallocations==0,"Host audio heap activity");
    std::cout<<"PASS: "<<cases<<" VST3 rack impulse cases, 180 structural blocks, rejection/state, 12 concurrent captures/cancellation/recovery and zero host C++ audio heap calls\n";
    return 0;
 }catch(const std::exception& e){watching=false;std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
