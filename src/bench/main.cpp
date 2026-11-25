#include "bench/audio_file.hpp"
#include "bench/analysis.hpp"
#include <fstream>
#include <iomanip>
#include "dsp/basic.hpp"
#include "dsp/equalizer.hpp"
#include "dsp/loudness.hpp"
#include "dsp/dynamics.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
bool same_file(const std::string& a,const std::string& b) {
    return std::filesystem::absolute(a).lexically_normal()==std::filesystem::absolute(b).lexically_normal() ||
        (std::filesystem::exists(a) && std::filesystem::exists(b) && std::filesystem::equivalent(a,b));
}
double numeric(const char* text) {
    size_t used=0; const std::string value(text); const double number=std::stod(value,&used);
    if(used!=value.size() || !std::isfinite(number)) throw std::runtime_error("Invalid numeric argument");
    return number;
}
int analyze(int argc,char** argv) {
    const bool tone=std::string(argv[1])=="analyze-tone";
    if(argc!=(tone?6:5)) throw std::runtime_error("Invalid analysis arguments");
    const std::string reportPath=argv[tone?5:4];
    if(same_file(reportPath,argv[2]) || same_file(reportPath,argv[3])) throw std::runtime_error("Report must not overwrite audio input");
    const auto input=nexus::bench::read_wav(argv[2]), output=nexus::bench::read_wav(argv[3]);
    if(input.rate!=output.rate || input.channels.size()!=output.channels.size() || input.frames()!=output.frames())
        throw std::runtime_error("Analysis requires matching sample rate, channels and frames");
    const double frequency=tone?numeric(argv[4]):0;
    if(tone) {
        const double cycles=frequency*double(input.frames())/input.rate;
        if(frequency<=0 || frequency>=double(input.rate)/2 || std::abs(cycles-std::round(cycles))>1e-6)
            throw std::runtime_error("Tone analysis requires an integer number of periods; trim transients first");
    }
    // Construct all measurements before opening the output report.
    std::vector<double> measurements;
    for(size_t c=0;c<input.channels.size();++c) {
        const auto before=nexus::bench::levels(input.channels[c]);
        const auto after=nexus::bench::levels(output.channels[c]);
        measurements.insert(measurements.end(),{before.peak,before.rms,after.peak,after.rms,after.dc});
        if(tone) {
            measurements.push_back(nexus::bench::response_db(input.channels[c],output.channels[c],input.rate,frequency));
            measurements.push_back(nexus::bench::thd_ratio(output.channels[c],input.rate,frequency));
        } else measurements.push_back(double(nexus::bench::impulse_delay(input.channels[c],output.channels[c])));
    }
    std::ofstream report(reportPath);
    if(!report) throw std::runtime_error("Cannot write analysis report");
    report<<std::setprecision(12)<<"{\n  \"schema_version\": 1,\n  \"analysis\": \""<<(tone?"coherent-tone":"isolated-impulse")
          <<"\",\n  \"sample_rate\": "<<input.rate<<",\n  \"channels\": [\n";
    const size_t stride=tone?7:6;
    for(size_t c=0;c<input.channels.size();++c) {
        const auto base=c*stride;
        report<<"    {\"input_peak\": "<<measurements[base]<<", \"input_rms\": "<<measurements[base+1]
              <<", \"output_peak\": "<<measurements[base+2]<<", \"output_rms\": "<<measurements[base+3]
              <<", \"output_dc\": "<<measurements[base+4];
        if(tone) report<<", \"gain_db\": "<<measurements[base+5]<<", \"thd_ratio\": "<<measurements[base+6];
        else report<<", \"measured_delay_frames\": "<<measurements[base+5];
        report<<"}"<<(c+1<input.channels.size()?",":"")<<"\n";
    }
    report<<"  ]\n}\n"; report.flush();
    if(!report) throw std::runtime_error("Analysis report write failed");
    std::cout<<"Analysis report written\n"; return 0;
}
}
int main(int argc, char** argv) {
    try {
        if(argc==3&&std::string(argv[1])=="loudness"){
            auto input=nexus::bench::read_wav(argv[2]);
            nexus::LoudnessMeter meter({double(input.rate),input.channels.size(),1024});
            std::array<float*,8> planes{};
            for(size_t offset=0;offset<input.frames();){
                const size_t frames=(std::min)(size_t(1024),input.frames()-offset);
                for(size_t c=0;c<input.channels.size();++c)planes[c]=input.channels[c].data()+offset;
                if(!meter.process({planes.data(),input.channels.size(),frames}))throw std::runtime_error("Loudness analysis failed");
                offset+=frames;
            }
            const auto r=meter.read();
            auto value=[](float x,bool ready){return ready?std::to_string(x):std::string("not_ready");};
            std::cout<<"LUFS M="<<value(r.momentary,r.momentary_ready)<<" S="<<value(r.short_term,r.short_term_ready)
                     <<" I="<<value(r.integrated,r.integrated_ready)<<" sample_peak_dBFS="<<value(r.sample_peak,r.active)
                     <<" (100 ms updates; silence/below display floor=-120)\n";
            return 0;
        }
        if(argc>=2 && (std::string(argv[1])=="analyze-tone" || std::string(argv[1])=="analyze-impulse")) return analyze(argc,argv);
        if (argc == 7 && std::string(argv[1]) == "generate") {
            const auto rate = numeric(argv[4]);
            const auto channels = numeric(argv[5]);
            if (rate < 8000 || rate > 192000 || channels < 1 || channels > 8 || rate != std::floor(rate) || channels != std::floor(channels)) throw std::runtime_error("Unsupported format");
            nexus::bench::write_wav(argv[3], nexus::bench::generate(argv[2],
                static_cast<uint32_t>(rate), static_cast<size_t>(channels), numeric(argv[6])));
            std::cout << "Generated " << argv[2] << " test signal\n"; return 0;
        }
        const bool eqband=argc==7 && std::string(argv[1])=="eq-band";
        const bool effect=argc==6 && std::string(argv[1])=="effect";
        const bool dynamics=argc==10 && std::string(argv[1])=="dynamics";
        const bool plain=argc==4 && std::string(argv[1])=="process";
        if (!effect && !plain && !eqband && !dynamics) {
            std::cerr << "Usage: nexus_bench loudness input.wav\n"
                      << "       nexus_bench generate tone|impulse|sweep output.wav rate channels seconds\n"
                         "       nexus_bench process input.wav output.wav\n"
                         "       nexus_bench eq-band input.wav output.wav frequency_hz gain_db Q\n"
                         "       nexus_bench effect gain|balance|pan|mono|polarity|highpass|lowpass|width input.wav output.wav value\n"
                         "       nexus_bench dynamics compressor|expander|gate|limiter input.wav output.wav threshold_db ratio attack_ms release_ms makeup_db\n"
                         "       nexus_bench analyze-tone input.wav output.wav frequency_hz report.json\n"
                         "       nexus_bench analyze-impulse input.wav output.wav report.json\n"; return 2;
        }
        const char* inputPath=argv[(effect||dynamics)?3:2];
        const char* outputPath=argv[(effect||dynamics)?4:3];
        const auto in = std::filesystem::absolute(inputPath).lexically_normal();
        const auto out = std::filesystem::absolute(outputPath).lexically_normal();
        if (in == out || (std::filesystem::exists(out) && std::filesystem::equivalent(in, out)))
            throw std::runtime_error("Input and output must differ");
        auto audio = nexus::bench::read_wav(inputPath);
        const auto original = audio.channels;
        nexus::Engine engine;
        if(dynamics){
            const std::string id=argv[2];
            if(id!="compressor"&&id!="expander"&&id!="gate"&&id!="limiter")throw std::runtime_error("Unknown dynamics type");
            nexus::DynamicsSettings settings;
            const double values[]{numeric(argv[5]),numeric(argv[6]),numeric(argv[7]),numeric(argv[8]),numeric(argv[9])};
            for(size_t i=0;i<5;++i){
                const auto& field=nexus::dynamics_fields[i];
                if(values[i]<field.low||values[i]>field.high)throw std::runtime_error("Dynamics parameter out of range");
                settings.*(field.member)=float(values[i]);
            }
            settings.gate=id=="gate";
            auto module=std::make_unique<nexus::Dynamics>(id=="compressor"?nexus::DynamicsType::compressor:
                id=="limiter"?nexus::DynamicsType::limiter:nexus::DynamicsType::expander);
            if(!module->set_settings(settings))throw std::runtime_error("Invalid dynamics settings");
            engine.add(std::move(module));
        }
        if(eqband){
            const double f=numeric(argv[4]),g=numeric(argv[5]),q=numeric(argv[6]);
            if(f<20||f>20000||g<-18||g>18||q<.2||q>10)throw std::runtime_error("EQ range: 20..20000 Hz, -18..18 dB, Q 0.2..10");
            auto module=std::make_unique<nexus::Equalizer>();
            module->set_band(0,{float(f),float(g),float(q),true});engine.add(std::move(module));
        }
        if(effect){
            const std::string id=argv[2];const auto value=numeric(argv[5]);
            if(id=="gain"){
                auto module=std::make_unique<nexus::Gain>();
                if(value < -60 || value > 24 || !module->set_db(static_cast<float>(value)))throw std::runtime_error("Gain range is -60..24 dB");
                engine.add(std::move(module));
            }else if(id=="balance"){
                auto module=std::make_unique<nexus::Balance>();
                if(value < -1 || value > 1 || !module->set_position(static_cast<float>(value)))throw std::runtime_error("Balance range is -1..1");
                engine.add(std::move(module));
            }else if(id=="pan"){
                auto module=std::make_unique<nexus::Pan>();
                if(value < -1 || value > 1 || !module->set_position(static_cast<float>(value)))throw std::runtime_error("Pan range is -1..1");
                engine.add(std::move(module));
            }else if(id=="width"){
                auto module=std::make_unique<nexus::StereoWidth>();
                if(value<0||value>2||!module->set_width(float(value)))throw std::runtime_error("Width range is 0..2");
                engine.add(std::move(module));
            }else if(id=="mono"){
                if(value!=0 && value!=1)throw std::runtime_error("Mono value must be 0 or 1");
                auto module=std::make_unique<nexus::Mono>();module->set_enabled(value==1);engine.add(std::move(module));
            }else if(id=="polarity"){
                if(value!=0 && value!=1)throw std::runtime_error("Polarity value must be 0 or 1");
                auto module=std::make_unique<nexus::Polarity>();module->set_inverted(value==1);engine.add(std::move(module));
            }else if(id=="highpass" || id=="lowpass"){
                auto module=std::make_unique<nexus::PassFilter>(id=="highpass"?nexus::PassType::high_pass:nexus::PassType::low_pass);
                if(value < 20 || value > 20000 || !module->set_cutoff(static_cast<float>(value)))throw std::runtime_error("Cutoff range is 20..20000 Hz");
                module->set_enabled(true);engine.add(std::move(module));
            }else throw std::runtime_error("Unknown native effect");
        }
        engine.prepare({double(audio.rate), audio.channels.size(), 1024});
        std::vector<float*> planes(audio.channels.size());
        std::vector<double> timings;
        timings.reserve((audio.frames() + 1023) / 1024);
        for (size_t pos = 0; pos < audio.frames();) {
            const auto count = (std::min)(size_t(1024), audio.frames() - pos);
            for (size_t c = 0; c < planes.size(); ++c) planes[c] = audio.channels[c].data() + pos;
            const auto start = std::chrono::steady_clock::now();
            if (!engine.process({planes.data(), planes.size(), count})) throw std::runtime_error("Processing failed");
            const auto finish = std::chrono::steady_clock::now();
            timings.push_back(std::chrono::duration<double, std::micro>(finish - start).count());
            pos += count;
        }
        double peak = 0, square = 0, error = 0;
        for (size_t c = 0; c < planes.size(); ++c)
            for (size_t i = 0; i < audio.frames(); ++i) {
                const double x = audio.channels[c][i];
                if (!std::isfinite(x)) throw std::runtime_error("Non-finite output");
                peak = (std::max)(peak, std::abs(x)); square += x * x;
                error = (std::max)(error, std::abs(x - original[c][i]));
            }
        const double n = double(audio.frames()) * double(planes.size());
        std::sort(timings.begin(), timings.end());
        auto percentile = [&](double q) { return timings.empty() ? 0. :
            timings[static_cast<size_t>(q * double(timings.size() - 1))]; };
        nexus::bench::write_wav(outputPath, audio);
        std::cout << "Frames=" << audio.frames() << " rate=" << audio.rate << " channels=" << planes.size()
                  << "\nPeak=" << peak << " RMS=" << (n ? std::sqrt(square / n) : 0)
                  << " max_abs_error=" << error << "\nDeclared_latency_frames=" << engine.latency_frames()
                  << "\nBlock_us p50=" << percentile(.5) << " p95=" << percentile(.95)
                  << " max=" << percentile(1.) << "\nTimings are diagnostic, not a realtime guarantee.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; return 1; }
}