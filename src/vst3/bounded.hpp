#pragma once
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace nexus::vst3::detail {
using namespace Steinberg;
using namespace Steinberg::Vst;
// Borrowed objects owned by their enclosing host. A plug-in must not retain
// process queues beyond the callback; ref calls cannot free/allocate them.
template<class Interface>class Borrowed:public Interface {
public:
    tresult PLUGIN_API queryInterface(const TUID iid,void** out)override{
        if(!out)return kInvalidArgument;*out=nullptr;
        if(FUnknownPrivate::iidEqual(iid,Interface::iid)||FUnknownPrivate::iidEqual(iid,FUnknown::iid)){
            *out=static_cast<Interface*>(this);return kResultOk;
        }return kNoInterface;
    }
    uint32 PLUGIN_API addRef()override{return 1;}
    uint32 PLUGIN_API release()override{return 1;}
};
class Stream final:public Borrowed<IBStream> {
public:
    static constexpr size_t limit=16*1024*1024;
    Stream()=default;
    explicit Stream(const std::vector<uint8_t>& bytes){
        if(bytes.size()>limit)throw std::invalid_argument("VST3 state exceeds 16 MiB");
        data=bytes;
    }
    tresult PLUGIN_API read(void* buffer,int32 count,int32* readCount)override{
        if(readCount)*readCount=0;
        if(count<0||(!buffer&&count))return kInvalidArgument;
        const auto n=(std::min)(size_t(count),data.size()-position);
        if(n)std::memcpy(buffer,data.data()+position,n);
        position+=n;if(readCount)*readCount=int32(n);return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer,int32 count,int32* written)override{
        if(written)*written=0;
        if(count<0||(!buffer&&count)||size_t(count)>limit-position){failed=true;return kInvalidArgument;}
        const auto end=position+size_t(count);if(end>data.size())data.resize(end);
        if(count)std::memcpy(data.data()+position,buffer,size_t(count));
        position=end;if(written)*written=count;return kResultOk;
    }
    tresult PLUGIN_API seek(int64 offset,int32 mode,int64* result)override{
        int64 base=0;
        if(mode==kIBSeekCur)base=int64(position);else if(mode==kIBSeekEnd)base=int64(data.size());
        else if(mode!=kIBSeekSet)return kInvalidArgument;
        if(offset< -base||offset>int64(data.size())-base)return kInvalidArgument;
        position=size_t(base+offset);if(result)*result=int64(position);return kResultOk;
    }
    tresult PLUGIN_API tell(int64* result)override{
        if(!result)return kInvalidArgument;*result=int64(position);return kResultOk;
    }
    std::vector<uint8_t> data;size_t position=0;bool failed=false;
};
class Queue final:public Borrowed<IParamValueQueue> {
public:
    ParamID id=0;bool used=false;int32 count=0,frames=0;
    std::array<std::pair<int32,double>,32> points{};
    uint64_t rejected=0;
    ParamID PLUGIN_API getParameterId()override{return id;}
    int32 PLUGIN_API getPointCount()override{return count;}
    tresult PLUGIN_API getPoint(int32 index,int32& offset,ParamValue& value)override{
        if(index<0||index>=count)return kInvalidArgument;offset=points[size_t(index)].first;value=points[size_t(index)].second;return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset,ParamValue value,int32& index)override{
        if(offset<0||offset>=(std::max)(1,frames)||!std::isfinite(value)||value<0||value>1){++rejected;return kInvalidArgument;}
        int32 position=0;while(position<count&&points[size_t(position)].first<offset)++position;
        if(position<count&&points[size_t(position)].first==offset){points[size_t(position)].second=value;index=position;return kResultOk;}
        if(count==int32(points.size())){++rejected;return kResultFalse;}
        for(int32 i=count;i>position;--i)points[size_t(i)]=points[size_t(i-1)];
        points[size_t(position)]={offset,value};++count;index=position;return kResultOk;
    }
};
class Changes final:public Borrowed<IParameterChanges> {
public:
    std::vector<Queue> queues;std::vector<size_t> used;uint64_t rejected=0;
    void setup(const std::vector<uint32_t>& ids){
        queues.clear();queues.resize(ids.size());used.clear();used.reserve(ids.size());
        for(size_t i=0;i<ids.size();++i)queues[i].id=ids[i];
    }
    void clear(int32 frames)noexcept{
        used.clear();rejected=0;
        for(auto& q:queues){q.used=false;q.count=0;q.frames=frames;q.rejected=0;}
    }
    int32 PLUGIN_API getParameterCount()override{return int32(used.size());}
    IParamValueQueue* PLUGIN_API getParameterData(int32 index)override{
        return index<0||size_t(index)>=used.size()?nullptr:&queues[used[size_t(index)]];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id,int32& index)override{
        auto it=std::lower_bound(queues.begin(),queues.end(),id,[](const auto& q,ParamID value){return q.id<value;});
        if(it==queues.end()||it->id!=id){++rejected;return nullptr;}
        const size_t position=size_t(it-queues.begin());
        if(!it->used){
            if(used.size()==used.capacity()){++rejected;return nullptr;}
            it->used=true;used.push_back(position);
        }
        for(size_t i=0;i<used.size();++i)if(used[i]==position){index=int32(i);break;}
        return &*it;
    }
    uint64_t drops()const noexcept{
        uint64_t total=rejected;for(const auto& q:queues)total+=q.rejected;return total;
    }
};
}
