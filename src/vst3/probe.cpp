#include "vst3/core.hpp"
#include "bench/audio_file.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
double number(const std::string& text){
    size_t used=0;const auto value=std::stod(text,&used);
    if(used!=text.size()||!std::isfinite(value))throw std::runtime_error("Invalid numeric argument");return value;
}
std::string display(std::string text){
    for(auto& c:text)if(static_cast<unsigned char>(c)<32)c=' ';return text;
}
}
int wmain(int argc,wchar_t** argv){
    try{
        std::vector<std::string> args;for(int i=0;i<argc;++i)args.push_back(std::filesystem::path(argv[i]).u8string());
        if(argc==2&&args[1]=="paths"){
            for(const auto& path:nexus::vst3::installed_paths())std::cout<<path<<"\n";
            return 0;
        }
        if(argc==3&&args[1]=="inspect"){
            for(const auto& item:nexus::vst3::inspect(args[2]))
                std::cout<<item.id<<" | "<<(item.effect?"effect candidate":"excluded")<<" | "<<display(item.name)<<" | "<<display(item.vendor)<<" | "<<display(item.version)<<" | "<<display(item.subcategories)<<"\n";
            return 0;
        }
        if((argc==6||argc==8)&&args[1]=="render"){
            const auto inputPath=std::filesystem::u8path(args[4]),outputPath=std::filesystem::u8path(args[5]);
            if(std::filesystem::absolute(inputPath).lexically_normal()==std::filesystem::absolute(outputPath).lexically_normal()||
                (std::filesystem::exists(outputPath)&&std::filesystem::equivalent(inputPath,outputPath)))
                throw std::runtime_error("Input and output must differ");
            auto audio=nexus::bench::read_wav(args[4]);
            auto effect=std::make_unique<nexus::vst3::Effect>(args[2],args[3]);auto* status=effect.get();
            if(argc==8){
                size_t used=0;const auto id=std::stoull(args[6],&used);
                if(used!=args[6].size()||args[6][0]=='-'||id>std::numeric_limits<uint32_t>::max()||
                    !effect->set_parameter(uint32_t(id),number(args[7])))throw std::runtime_error("Invalid or read-only VST3 parameter");
            }
            nexus::Engine engine;engine.add(std::move(effect));engine.prepare({double(audio.rate),audio.channels.size(),1024});
            std::array<float*,8> planes{};
            for(size_t offset=0;offset<audio.frames();){
                const auto count=(std::min)(size_t(1024),audio.frames()-offset);
                for(size_t c=0;c<audio.channels.size();++c)planes[c]=audio.channels[c].data()+offset;
                if(!engine.process({planes.data(),audio.channels.size(),count})||status->needs_reprepare())
                    throw std::runtime_error("VST3 processing failed or requested reconfiguration; output not written");
                offset+=count;
            }
            nexus::bench::write_wav(args[5],audio);
            std::cout<<"PASS: VST3 render frames="<<audio.frames()<<" channels="<<audio.channels.size()
                <<" rate="<<audio.rate<<" declared_latency_frames="<<engine.latency_frames()<<" dropped_parameter_points="<<status->dropped_points()
                <<" rejected_host_requests="<<status->rejected_host_requests()<<"\nNo tail extension or automatic output-time compensation; internal diagnostic, not OBS qualification.\n";
            return 0;
        }
        std::cerr<<"Usage: nexus_vst3_probe paths\n"
            <<"       nexus_vst3_probe inspect plugin.vst3\n"
            <<"       nexus_vst3_probe render plugin.vst3 class_id input.wav output.wav [parameter_id normalized_value]\n";
        return 2;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}
}
