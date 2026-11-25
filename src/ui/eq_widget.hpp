#pragma once
#include "dsp/equalizer.hpp"
#include <QWidget>
#include <functional>
class QCheckBox;
class QDoubleSpinBox;
namespace nexus::ui {
class EqCurve;
class EqWidget final : public QWidget {
public:
    explicit EqWidget(QWidget* parent=nullptr);
    void set_settings(const EqSettings&);
    void set_sample_rate(double);
    const EqSettings& settings() const {return settings_;}
    std::function<void(const EqSettings&)> on_change;
    void move_band(size_t band,double frequency,double db);
private:
    void controls_changed();
    void refresh();
    EqSettings settings_;
    double rate_=48000;
    EqCurve* curve_;
    QCheckBox* enabled_;
    std::array<QCheckBox*,4> active_{};
    std::array<QDoubleSpinBox*,4> frequency_{},gain_{},q_{};
    friend class EqCurve;
};
}