#pragma once
#include <cstddef>
#include <vector>
namespace nexus::bench {
struct Levels { double peak = 0, rms = 0, dc = 0; };
Levels levels(const std::vector<float>& signal);
// Rectangular coherent-tone projection. Caller must use integer periods and
// exclude transients; this is not a general-purpose spectrum/THD+N estimator.
double tone_amplitude(const std::vector<float>& signal, double rate, double frequency);
double thd_ratio(const std::vector<float>& signal, double rate, double fundamental, unsigned harmonics = 5);
double response_db(const std::vector<float>& input, const std::vector<float>& output, double rate, double frequency);
// Positive, whole-sample delay from an isolated nonzero input impulse to the
// largest output peak. Intended for delay fixtures, not dispersive filters.
std::size_t impulse_delay(const std::vector<float>& input, const std::vector<float>& output);
double step_crossing_seconds(const std::vector<float>& output, double rate, std::size_t start,
                             double initial, double final, double fraction);
}