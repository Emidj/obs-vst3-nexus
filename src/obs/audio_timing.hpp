#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
namespace nexus::obs_bridge {
// Audio-consumer only. Does not allocate, lock or change the input packet.
class AudioPacketTiming {
public:
 bool compensate(uint64_t input,uint32_t frames,size_t latency,uint32_t rate,uint64_t& output)noexcept{
  if(!rate||latency>uint64_t(rate)*2)return false;
  if(initialized_&&(input<last_input_||input-last_input_>1000000000ULL))last_end_=0;
  last_input_=input;initialized_=true;
  const uint64_t delay=uint64_t(latency)*1000000000ULL/rate;
  if(input<delay)return false;
  output=input-delay;
  // Permit one nanosecond due to independent frame-duration rounding.
  if(output<last_end_&&last_end_-output>1){
   // Device timestamps may be quantized (e.g. Windows 100 ns ticks).
   // Do not discard a whole processed packet for sub-sample clock rounding.
   // A real latency transition or larger overlap keeps the existing rejection.
   const uint64_t sample=(1000000000ULL+rate-1)/rate;
   if(latency!=last_latency_||last_end_-output>sample)return false;
   output=last_end_;
  }
  const uint64_t duration=uint64_t(frames)*1000000000ULL/rate;
  if(duration>std::numeric_limits<uint64_t>::max()-output)return false;
  last_end_=output+duration;last_latency_=latency;return true;
 }
private:size_t last_latency_=0;uint64_t last_input_=0,last_end_=0;bool initialized_=false;
};
}
