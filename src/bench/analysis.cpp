#include "bench/analysis.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nexus::bench {
namespace {
constexpr double pi = 3.14159265358979323846;
void valid(const std::vector<float>& signal) {
    if (signal.empty()) throw std::invalid_argument("Empty analysis signal");
    for (float x : signal) if (!std::isfinite(x)) throw std::invalid_argument("Non-finite analysis signal");
}
void frequency_valid(double rate, double frequency) {
    if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(frequency) ||
        frequency <= 0 || frequency >= rate / 2) throw std::invalid_argument("Invalid analysis frequency");
}
}
Levels levels(const std::vector<float>& signal) {
    valid(signal);
    Levels result;
    double sumSquares = 0;
    for (float sample : signal) {
        const double x = sample;
        result.peak = (std::max)(result.peak, std::abs(x));
        result.dc += x; sumSquares += x * x;
    }
    result.dc /= double(signal.size()); result.rms = std::sqrt(sumSquares / double(signal.size()));
    return result;
}
double tone_amplitude(const std::vector<float>& signal, double rate, double frequency) {
    valid(signal); frequency_valid(rate, frequency);
    double real = 0, imag = 0;
    const double angle = 2 * pi * frequency / rate;
    for (std::size_t i = 0; i < signal.size(); ++i) {
        real += signal[i] * std::cos(angle * double(i));
        imag -= signal[i] * std::sin(angle * double(i));
    }
    return 2 * std::hypot(real, imag) / double(signal.size());
}
double thd_ratio(const std::vector<float>& signal, double rate, double fundamental, unsigned harmonics) {
    frequency_valid(rate, fundamental);
    if (harmonics < 2 || harmonics > 64 || fundamental * 2 >= rate / 2) throw std::invalid_argument("Invalid harmonic count");
    const double amplitude = tone_amplitude(signal, rate, fundamental);
    if (amplitude < 1e-12) throw std::invalid_argument("No measurable fundamental");
    double energy = 0;
    for (unsigned h = 2; h <= harmonics && fundamental * h < rate / 2; ++h) {
        const double value = tone_amplitude(signal, rate, fundamental * h);
        energy += value * value;
    }
    return std::sqrt(energy) / amplitude;
}
double response_db(const std::vector<float>& input, const std::vector<float>& output, double rate, double frequency) {
    if (input.size() != output.size()) throw std::invalid_argument("Response length mismatch");
    const auto a = tone_amplitude(input, rate, frequency);
    const auto b = tone_amplitude(output, rate, frequency);
    if (a < 1e-12 || b < 1e-12) throw std::invalid_argument("Response below analysis floor");
    return 20 * std::log10(b / a);
}
std::size_t impulse_delay(const std::vector<float>& input, const std::vector<float>& output) {
    valid(input); valid(output);
    if (input.size() != output.size()) throw std::invalid_argument("Impulse length mismatch");
    std::size_t count = 0, inputPeak = 0, outputPeak = 0;
    double magnitude = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != 0) { ++count; inputPeak = i; }
        if (std::abs(double(output[i])) > magnitude) {
            magnitude = std::abs(double(output[i])); outputPeak = i;
        }
    }
    if (count != 1 || magnitude == 0 || outputPeak < inputPeak)
        throw std::invalid_argument("Expected isolated input impulse and nonnegative output delay");
    return outputPeak - inputPeak;
}
double step_crossing_seconds(const std::vector<float>& output, double rate, std::size_t start,
                             double initial, double final, double fraction) {
    valid(output);
    if (!std::isfinite(rate) || rate <= 0 || start >= output.size() ||
        !std::isfinite(initial) || !std::isfinite(final) || initial == final ||
        !std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
        throw std::invalid_argument("Invalid step analysis");
    const double threshold = initial + (final - initial) * fraction;
    for (std::size_t i = start; i < output.size(); ++i)
        if (final > initial ? output[i] >= threshold : output[i] <= threshold)
            return double(i - start) / rate;
    throw std::invalid_argument("Step never reaches threshold");
}
}