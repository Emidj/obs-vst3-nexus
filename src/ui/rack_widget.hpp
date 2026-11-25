#pragma once
#include "ui/presets.hpp"
#include "dsp/loudness.hpp"
#include <QWidget>
#include <functional>
class QAction;class QCheckBox;class QComboBox;class QLabel;class QScrollArea;class QPushButton;class QToolButton;
namespace nexus::ui {
class RackWidget final : public QWidget {
public:
    explicit RackWidget(QString preset_directory,size_t channels=2,QWidget* parent=nullptr);
    const RackDocument& document()const{return document_;}
    void set_document(const RackDocument&);
    void set_sample_rate(double);
    void add_module(ModuleKind);
    void set_vst3_chooser(std::function<void()>);
    void add_vst3(const Vst3Settings&);
    void remove_module(uint64_t);
    void move_module(uint64_t,int direction);
    void select_side(bool b);
    void copy_to_other();
    void apply_preset(const Preset&);
    Preset save_as(const QString&);
    Preset import_preset(const QString&);
    void export_preset(const QString&)const;
    void set_source_name(const QString&);
    void set_status(const QString&);
    void set_gain_reduction(uint64_t,float);
    void set_loudness(const LoudnessReading&,bool available);
    std::function<void(const RackDocument&)> on_change;
    std::function<RackDocument()> before_snapshot;
    std::function<RackDocument(bool capture_dirty)> before_edit;
    std::function<void(uint64_t)> on_open_vst3_editor;
private:
    void rebuild();
    bool prepare_edit(bool capture_dirty=false);
    bool base_refresh_=false;
    void presets_refresh();
    void changed();
    void validate_layout(const RackDocument&)const;
    RackNode* node(uint64_t);
    RackDocument document_=default_document();
    PresetStore store_;
    size_t channels_;
    double rate_=48000;
    uint64_t expanded_=2;
    QString name_="Custom";
    std::vector<Preset> presets_;
    QCheckBox* active_;
    QComboBox* presets_box_;
    QLabel* source_;QLabel* status_;QLabel* meter_;
    QScrollArea* scroll_;
    QPushButton* a_;QPushButton* b_;
    QToolButton* add_;
    QAction* vst3_action_=nullptr;
    std::function<void()> choose_vst3_;
};
}