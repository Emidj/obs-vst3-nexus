#include "obs/audio_timing.hpp"
#include <array>
#include <stdexcept>
#include <iostream>
using nexus::obs_bridge::AudioPacketTiming;
void check(bool value){if(!value)throw std::runtime_error("Audio packet timestamp compensation");}
int main(){try{
 size_t cases=0;
 for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t chunk:{1u,17u,257u,1024u,4096u}){
  AudioPacketTiming timing;uint64_t output=0;const uint64_t origin=10000000000ULL;
  for(uint64_t offset=0;offset<rate*3;offset+=chunk){
   const auto input=origin+offset*1000000000ULL/rate;
   check(timing.compensate(input,chunk,16,rate,output));check(output==input-16*1000000000ULL/rate);
  }++cases;
 }
 {
  AudioPacketTiming timing;uint64_t output=0;
  check(!timing.compensate(0,1024,16,48000,output));
  const uint64_t origin=10000000000ULL;
  check(timing.compensate(origin,1024,0,48000,output));
  check(!timing.compensate(origin+1024*1000000000ULL/48000,1024,96000,48000,output));
  // After enough packets, increased delay no longer overlaps prior output.
  for(uint64_t n=2;n<100;++n)timing.compensate(origin+n*1024*1000000000ULL/48000,1024,96000,48000,output);
  check(output>=origin+1024*1000000000ULL/48000);
  check(timing.compensate(origin+100*1024*1000000000ULL/48000,1024,0,48000,output));
  // An input clock reset starts a new stream rather than dropping forever.
  check(timing.compensate(5000000000ULL,1024,0,48000,output));
  check(!timing.compensate(0,1024,0,0,output));
  check(!timing.compensate(10000000000ULL,1024,96001,48000,output));
  check(!timing.compensate(UINT64_MAX,1024,0,48000,output));
 }
 {
  AudioPacketTiming first,second;uint64_t a=0,b=0;
  check(first.compensate(10000000000ULL,257,16,48000,a));
  check(second.compensate(a,257,32,48000,b));
  check(b==10000000000ULL-16*1000000000ULL/48000-32*1000000000ULL/48000);
 }
 // Windows device clocks commonly expose 100 ns ticks. Quantization must not
 // turn a continuous stream into discarded audio packets.
 for(uint32_t rate:{44100u,48000u,96000u}){
  AudioPacketTiming timing;uint64_t output=0,last=0;
  for(uint64_t n=0;n<10000;++n){
   const uint64_t input=((10000000000ULL+n*1024*1000000000ULL/rate)/100)*100;
   check(timing.compensate(input,1024,721,rate,output));
   if(n)check(output>=last);last=output+1024*1000000000ULL/rate;
  }
 }
 std::cout<<"PASS: "<<cases<<" timestamp lattices, delay growth/reduction, overlap, underflow, clock reset, overflow and chained filters\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
