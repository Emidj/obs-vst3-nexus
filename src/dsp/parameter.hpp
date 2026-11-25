#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>
#include <stdexcept>
namespace nexus {
// Only set_target() may be called concurrently with the audio thread.
// prepare/reset/begin_block/next belong to the owning audio lifecycle.
class SmoothedParameter {
public:
    explicit SmoothedParameter(float initial = 0) : target_(initial), current_(initial), destination_(initial) {
        if (!std::isfinite(initial)) throw std::invalid_argument("Non-finite initial parameter");
    }
    bool set_target(float value) noexcept {
        if (!std::isfinite(value)) return false;
        target_.store(value, std::memory_order_relaxed);
        return true;
    }
    void prepare(double rate, double milliseconds) {
        if (!std::isfinite(rate) || rate < 8000 || rate > 192000 ||
            !std::isfinite(milliseconds) || milliseconds < 0 || milliseconds > 10000)
            throw std::invalid_argument("Invalid smoothing format");
        duration_ = static_cast<std::size_t>(rate * milliseconds / 1000.);
        reset();
    }
    void reset() noexcept {
        current_ = destination_ = target_.load(std::memory_order_relaxed);
        remaining_ = 0; increment_ = 0;
    }
    void begin_block() noexcept {
        const float requested = target_.load(std::memory_order_relaxed);
        if (requested == destination_) return;
        destination_ = requested;
        remaining_ = duration_;
        if (!remaining_) current_ = destination_;
        else increment_ = (double(destination_) - current_) / double(remaining_);
    }
    float next() noexcept {
        if (remaining_) {
            current_ += increment_;
            if (!--remaining_) current_ = destination_;
        }
        return static_cast<float>(current_);
    }
private:
    std::atomic<float> target_;
    double current_ = 0, increment_ = 0;
    float destination_ = 0;
    std::size_t duration_ = 0, remaining_ = 0;
};
static_assert(std::atomic<float>::is_always_lock_free, "Audio parameters require lock-free float atomics");
}