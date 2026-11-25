#include "vst3/core.hpp"
#include "vst3/bounded.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
using namespace nexus;
namespace {
thread_local bool watching=false;thread_local size_t allocations=0,deallocations=0;
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void allocated(){if(watching)++allocations;}
void freed(void* p){if(watching&&p)++deallocations;}
}
void* operator new(size_t n){allocated();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{freed(p);std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
namespace {
template<class Task>void rejects(Task task){bool rejected=false;try{task();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid operation accepted");}
struct Audio {
    std::array<std::array<float,1024>,2> samples{};
    float* planes[2]{samples[0].data(),samples[1].data()};
    Block block(size_t channels,size_t frames){return {planes,channels,frames};}
    void fill(float value){for(auto& c:samples)c.fill(value);}
};
std::string class_id(const std::vector<vst3::ClassInfo>& items,const char* name){
    for(const auto& item:items)if(item.name==name)return item.id;
    throw std::runtime_error("Fixture class missing");
}
size_t blocks=0;
template<class ProcessorType>
std::vector<float> impulse(ProcessorType& effect,size_t channels,size_t chunk,size_t frames=2048){
    Audio audio;std::vector<float> output;output.reserve(frames);
    for(size_t offset=0;offset<frames;){
        const size_t count=(std::min)(chunk,frames-offset);audio.fill(0);
        if(offset==0)for(auto& c:audio.samples)c[0]=.8f;
        watching=true;effect.process(audio.block(channels,count));watching=false;
        for(size_t i=0;i<count;++i)output.push_back(audio.samples[0][i]);
        for(size_t c=1;c<channels;++c)for(size_t i=0;i<count;++i)check(audio.samples[c][i]==audio.samples[0][i],"Channels contaminated");
        offset+=count;++blocks;
    }return output;
}
}
int main(int argc,char** argv){
 try{
    check(argc==4,"Fixture paths/report required");
    const std::string fixture=argv[1],sdk=argv[2];
    const auto classes=vst3::inspect(fixture);
    const auto id=class_id(classes,"NEXUS Gain16 fixture");
    const auto instrument=class_id(classes,"Rejected instrument fixture");
    const auto noInput=class_id(classes,"Rejected no-input fixture");
    const auto only64=class_id(classes,"Float64-only fixture");
    for(const auto& item:classes)if(item.id==instrument)check(!item.effect,"Instrument eligible");
    rejects([&]{vst3::Effect bad(fixture,instrument);});
    rejects([&]{vst3::Effect bad(fixture,"bad-ID");});
    rejects([&]{vst3::Effect bad(fixture,std::string(32,'G'));});
    rejects([&]{vst3::Effect bad("not-a-plugin.dll",id);});
    {vst3::Effect bad(fixture,noInput);rejects([&]{bad.prepare({48000,2,1024});});}
    size_t cases=0;
    for(const auto& audioID:{id,only64})for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)})
    for(size_t chunk:{size_t(1),size_t(17),size_t(257),size_t(1024)}){
        vst3::Effect effect(fixture,audioID);check(effect.parameters().size()==2,"Metadata");
        check(effect.set_parameter(7,.25)&&!effect.set_parameter(8,.25)&&!effect.set_parameter(999,0),"Parameter IDs/read-only");
        check(!effect.set_parameter(7,std::numeric_limits<double>::quiet_NaN())&&!effect.set_parameter(7,1.1),"Normalized bounds");
        effect.prepare({rate,channels,1024});check(effect.latency_frames()==16,"Declared fixed latency");
        const auto output=impulse(effect,channels,chunk);
        for(size_t i=0;i<output.size();++i)check(output[i]==(i==16?.4f:0.f),"Gain/delay/partition");
        check(!effect.needs_reprepare()&&effect.faults()==0&&effect.read_parameter(8)==.25,"Normal process status/output parameter");
        rejects([&]{effect.capture_state();});
        effect.suspend();const auto state=effect.capture_state();check(state.component.size()==8&&state.controller.empty(),"Opaque component state");
        vst3::Effect restored(fixture,audioID);restored.restore_state(state);restored.prepare({rate,channels,1024});
        check(impulse(restored,channels,chunk)==output,"State roundtrip");
        vst3::Effect peer(fixture,audioID);peer.prepare({rate,channels,1024});
        const auto isolated=impulse(peer,channels,chunk);
        check(isolated[16]==.8f&&output[16]==.4f,"Instance state crossed");
        ++cases;
    }
    {
        auto fx=std::make_unique<vst3::Effect>(fixture,id);fx->set_parameter(7,.25);
        Engine engine;engine.add(std::move(fx));engine.set_bypass(true);engine.prepare({48000,2,1024});
        check(engine.latency_frames()==16,"Engine latency accumulation");
        const auto dry=impulse(engine,2,64);
        check(dry[0]==0&&dry[16]==.8f,"Latency-aligned bypass");
    }
    // Separate control producer; audio performs only bounded queue work.
    vst3::Effect automated(fixture,id);automated.prepare({48000,2,1024});
    std::atomic<bool> stop{false};std::atomic<size_t> edits{0};std::atomic<bool> ok{true};
    std::thread editor([&]{while(!stop.load(std::memory_order_acquire)){
        if(!automated.set_parameter(7,edits.load()%2?.25:.5))ok=false;++edits;
    }});
    Audio audio;
    for(size_t i=0;i<20000;++i){
        audio.fill(.1f);watching=true;automated.process(audio.block(2,64));watching=false;
        for(size_t s=0;s<64;++s)if(!std::isfinite(audio.samples[0][s]))ok=false;++blocks;
    }
    stop.store(true,std::memory_order_release);editor.join();
    check(ok&&edits>0&&!automated.needs_reprepare(),"Concurrent parameters");
    automated.set_parameter(7,.8);audio.fill(.1f);watching=true;automated.process(audio.block(2,64));watching=false;
    check(automated.dropped_points()>=8&&!automated.needs_reprepare(),"Bounded output queue overflow");
    for(int repeat=0;repeat<2;++repeat){
        automated.set_parameter(7,.625);audio.fill(.1f);automated.process(audio.block(2,64));
        check(std::abs(audio.samples[0][63]-.125f)<1e-7&&automated.read_parameter(7)==.5,"Writable processor feedback/repeated edit");
    }
    automated.set_parameter(7,.7);audio.fill(.1f);watching=true;automated.process(audio.block(2,64));watching=false;
    check(automated.rejected_host_requests()==1&&!automated.needs_reprepare(),"Allocating host request was not rejected in audio");
    automated.reset();audio.fill(.1f);automated.process(audio.block(2,64));
    check(automated.needs_reprepare()&&audio.samples[0][63]==0,"Reset must request stopped reprepare");
    automated.set_parameter(7,.5);automated.prepare({48000,2,1024});
    audio.fill(.1f);automated.process(audio.block(2,64));check(!automated.needs_reprepare(),"Stopped reprepare");
    automated.suspend();auto state=automated.capture_state();auto wrong=state;wrong.class_id=instrument;
    rejects([&]{automated.restore_state(wrong);});
    auto broken=state;broken.component.clear();rejects([&]{automated.restore_state(broken);});
    rejects([&]{automated.prepare({48000,2,1024});});
    for(double badValue:{.96,.99}){
        vst3::Effect brokenFx(fixture,id);brokenFx.set_parameter(7,badValue);brokenFx.prepare({48000,2,1024});
        audio.fill(.1f);brokenFx.process(audio.block(2,64));
        check(brokenFx.needs_reprepare()&&brokenFx.faults()>0,"Process fault was ignored");
        for(size_t c=0;c<2;++c)for(size_t i=0;i<64;++i)check(audio.samples[c][i]==0,"Fault block not silenced");
    }
    {
        vst3::Effect owned(fixture,id);std::atomic<bool> rejected{false};
        std::thread wrong([&]{try{owned.prepare({48000,2,1024});}catch(const std::exception&){rejected.store(true);}});wrong.join();
        check(rejected.load(),"Wrong-thread lifecycle call accepted");owned.prepare({48000,2,1024});
    }
    {
        vst3::Effect overflow(fixture,only64);overflow.set_parameter(7,.945);overflow.prepare({48000,2,1024});
        Audio block;block.fill(.1f);watching=true;overflow.process(block.block(2,64));watching=false;
        check(overflow.needs_reprepare()&&block.samples[0][0]==0,"Float64 conversion overflow not quarantined");
    }
    {
        vst3::Effect messages(fixture,id);messages.set_parameter(7,.52);messages.prepare({48000,2,1024});
        audio.fill(.1f);watching=true;messages.process(audio.block(2,64));watching=false;
        check(messages.rejected_host_requests()==1&&!messages.needs_reprepare(),"Wrong-thread notification not rejected");
        messages.set_parameter(7,.53);audio.fill(.1f);messages.process(audio.block(2,64));
        messages.suspend();check(!messages.capture_state().component.empty(),"UI-thread message forwarding failed");
        for(double trigger:{.54,.535,.536}){
            vst3::Effect changed(fixture,id);changed.set_parameter(7,trigger);changed.prepare({48000,2,1024});
            audio.fill(.1f);changed.process(audio.block(2,64));changed.service_control();
            check(changed.needs_reprepare(),"Processor feedback did not reach controller restart");
            changed.suspend();const auto state=changed.capture_state();
            vst3::Effect replacement(fixture,id);replacement.restore_state(state);replacement.prepare({48000,2,1024});
            check(replacement.latency_frames()==(trigger==.54?32:16),"Restart replacement latency");
        }
    }
    {
        vst3::Effect restartFx(fixture,id);restartFx.set_parameter(7,.875);restartFx.prepare({48000,2,1024});
        audio.fill(.1f);restartFx.process(audio.block(2,64));check(!restartFx.needs_reprepare(),"Premature restart");
        restartFx.suspend();restartFx.capture_state();check(!restartFx.needs_reprepare(),"Saving must not reload component state into controller");
        restartFx.set_parameter(7,.5);restartFx.prepare({48000,2,1024});
        audio.fill(.1f);restartFx.process(audio.block(2,64));check(!restartFx.needs_reprepare(),"Restart reprepare");
    }
    // Official SDK effect: artistic delay is not declared processing latency.
    const auto sdkId=class_id(vst3::inspect(sdk),"ADelay");
    for(double rate:{44100.,48000.,96000.})for(size_t channels:{size_t(1),size_t(2)}){
        vst3::Effect delay(sdk,sdkId);check(delay.set_parameter(100,.015625),"SDK delay parameter");
        delay.prepare({rate,channels,1024});const auto output=impulse(delay,channels,257);
        const size_t expected=size_t(.015625*rate);
        check(output[expected]==.8f&&delay.latency_frames()==0&&!delay.needs_reprepare(),"SDK ADelay audio");
        delay.suspend();const auto saved=delay.capture_state();
        vst3::Effect restored(sdk,sdkId);restored.restore_state(saved);restored.prepare({rate,channels,1024});
        check(impulse(restored,channels,17)==output,"SDK ADelay restored state");++cases;
    }
    // Modern bundle and legacy binary paths, including non-ASCII names.
    {
        const auto base=std::filesystem::absolute(std::filesystem::path(argv[3])).parent_path();
        const auto name=std::filesystem::u8path(u8"Nexus \u00f1 \u97f3.vst3");
        const auto bundle=base/"unicode-fixture"/name;
        const auto binary=bundle/"Contents"/"x86_64-win"/name;
        std::filesystem::create_directories(binary.parent_path());
        const auto source=std::filesystem::u8path(fixture)/"Contents"/"x86_64-win"/"NexusFixture.vst3";
        std::filesystem::copy_file(source,binary,std::filesystem::copy_options::overwrite_existing);
        const auto unicodeId=class_id(vst3::inspect(bundle.u8string()),"NEXUS Gain16 fixture");
        vst3::Effect unicodeFx(bundle.u8string(),unicodeId);unicodeFx.prepare({48000,2,1024});
        check(impulse(unicodeFx,2,64)[16]==.8f,"Unicode VST3 bundle");
        const auto flat=base/"unicode-fixture"/std::filesystem::u8path(u8"Flat \u00f1.vst3");
        std::filesystem::copy_file(source,flat,std::filesystem::copy_options::overwrite_existing);
        vst3::Effect flatFx(flat.u8string(),id);flatFx.prepare({48000,2,1024});
        check(impulse(flatFx,2,64)[16]==.8f,"Legacy single-file VST3");
    }
    // Host stream bounds and parameter queue ordering, independent of a vendor.
    {
        using namespace Steinberg;
        vst3::detail::Stream stream;uint8_t byte=1;int32 written=0;
        check(stream.write(&byte,1,&written)==kResultOk&&written==1,"State stream write");
        check(stream.seek(std::numeric_limits<int64>::max(),IBStream::kIBSeekCur,nullptr)!=kResultOk,"State seek overflow");
        check(stream.write(&byte,int32(vst3::detail::Stream::limit+1),&written)!=kResultOk&&stream.failed,"State cap");
        vst3::detail::Queue queue;queue.frames=64;int32 index=0;
        check(queue.addPoint(30,.3,index)==kResultOk&&queue.addPoint(2,.2,index)==kResultOk,"Queue insertion");
        int32 offset=0;double value=0;check(queue.getPoint(0,offset,value)==kResultOk&&offset==2,"Queue sorting");
        check(queue.addPoint(2,.4,index)==kResultOk&&queue.count==2,"Queue replacement");
        check(queue.addPoint(64,0,index)!=kResultOk&&queue.addPoint(0,-1,index)!=kResultOk,"Queue bounds");
    }
    check(allocations==0&&deallocations==0,"Host audio allocated or freed");
    std::ofstream report(argv[3]);
    report<<"{\n  \"passed\": true,\n  \"audio_cases\": "<<cases<<",\n  \"audio_blocks\": "<<blocks
          <<",\n  \"parameter_edits\": "<<edits.load()<<",\n  \"host_audio_allocations\": "<<allocations<<",\n  \"host_audio_deallocations\": "<<deallocations
          <<",\n  \"scope\": \"internal fixtures and official SDK ADelay; no OBS or vendor-wide qualification\"\n}\n";
    report.flush();check(bool(report),"Report write");
    std::cout<<"PASS: VST3 "<<cases<<" audio cases, "<<blocks<<" blocks, state/latency/isolation/errors and zero host C++ heap calls\n";return 0;
 }catch(const std::exception& e){watching=false;std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
