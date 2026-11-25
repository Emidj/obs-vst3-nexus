#pragma once
#include "dsp/engine.hpp"
#include <array>
#include <iir/RBJ.h>
namespace nexus {
class Gain final : public Processor {
public:
    bool set_db(float db) noexcept;
    void prepare(const Format&) override;
    void reset() noexcept override { amplitude_.reset(); }
    void process(Block) noexcept override;
private:
    SmoothedParameter amplitude_{1};
};
class Balance final : public Processor {
public:
    bool set_position(float value) noexcept;
    void prepare(const Format&) override;
    void reset() noexcept override { position_.reset(); }
    void process(Block) noexcept override;
private:
    SmoothedParameter position_;
};
class Mono final : public Processor {
public:
    void set_enabled(bool enabled) noexcept { mix_.set_target(enabled ? 1.f : 0.f); }
    void prepare(const Format&) override;
    void reset() noexcept override { mix_.reset(); }
    void process(Block) noexcept override;
private:
    SmoothedParameter mix_;
};
class Polarity final : public Processor {
public:
    void set_inverted(bool value) noexcept { scale_.set_target(value ? -1.f : 1.f); }
    void prepare(const Format&) override;
    void reset() noexcept override { scale_.reset(); }
    void process(Block) noexcept override;
private:
    SmoothedParameter scale_{1};
};
// Explicit mono-to-stereo panner: downmix (L+R)/2 then equal-power pan.
// Center is -3.01 dB per output. Not a transparent stereo balance control.
class Pan final : public Processor {
public:
    bool set_position(float value) noexcept;
    void prepare(const Format&) override;
    void reset() noexcept override { position_.reset(); }
    void process(Block) noexcept override;
private:
    SmoothedParameter position_;
};
enum class PassType { high_pass, low_pass };
class PassFilter final : public Processor {
public:
    explicit PassFilter(PassType type) : type_(type) {}
    bool set_cutoff(float hz) noexcept;
    void set_enabled(bool value) noexcept { mix_.set_target(value ? 1.f : 0.f); }
    void prepare(const Format&) override;
    void reset() noexcept override;
    void process(Block) noexcept override;
private:
    void coefficients(double hz) noexcept;
    PassType type_;
    SmoothedParameter cutoff_{1000}, mix_;
    double rate_ = 48000, previous_ = -1;
    std::array<Iir::DirectFormI,8> states_;
    Iir::RBJ::LowPass low_;
    Iir::RBJ::HighPass high_;
};
}