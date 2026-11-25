#include "ui/rack_widget.hpp"
#include "ui/eq_widget.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QAction>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace nexus::ui {
namespace {
void attempt(QWidget* parent,const std::function<void()>& task){
    QPointer<QWidget> guard(parent);try{task();}catch(const std::exception& e){if(guard)QMessageBox::warning(guard,"Audio Rack NEXUS",QString::fromUtf8(e.what()));}
}
void normalize(RackConfig& config){
    for(auto& node:config)if(node.kind==ModuleKind::equalizer&&!node.eq.enabled){node.enabled=false;node.eq.enabled=true;}
}
}
RackWidget::RackWidget(QString directory,size_t channels,QWidget* parent):QWidget(parent),store_(std::move(directory)),channels_(channels){
    setObjectName("nexus_rack_editor");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(16,16,16,16);layout->setSpacing(12);
    auto* title=new QLabel("NEXUS Audio Rack",this);auto font=title->font();font.setPointSize(font.pointSize()+5);font.setBold(true);title->setFont(font);layout->addWidget(title);
    active_=new QCheckBox("Rack enabled",this);active_->setObjectName("rack_active");layout->addWidget(active_);
    connect(active_,&QCheckBox::toggled,this,[this](bool on){if(!prepare_edit())return;document_.bypass=!on;changed();});
    source_=new QLabel("Source",this);source_->setObjectName("rack_source");layout->addWidget(source_);
    meter_=new QLabel(this);meter_->setObjectName("rack_loudness");meter_->setWordWrap(true);
    meter_->setToolTip("Output measurement. M: 400 ms. S: 3 s. I: since filter creation, with loudness gating. Peak: sample peak over 100 ms, not True Peak.");
    layout->addWidget(meter_);set_loudness({},true);
    auto* bar=new QHBoxLayout;presets_box_=new QComboBox(this);presets_box_->setObjectName("rack_presets");presets_box_->setMinimumContentsLength(24);bar->addWidget(presets_box_,1);
    auto* save=new QPushButton("Save As",this);save->setObjectName("rack_save_as");bar->addWidget(save);
    auto* import=new QPushButton("Import",this);bar->addWidget(import);auto* exportButton=new QPushButton("Export",this);bar->addWidget(exportButton);layout->addLayout(bar);
    auto* compare=new QHBoxLayout;a_=new QPushButton("A",this);b_=new QPushButton("B",this);a_->setObjectName("rack_a");b_->setObjectName("rack_b");
    a_->setCheckable(true);b_->setCheckable(true);
    const QString selectedStyle="QPushButton:checked { background-color: #286d98; color: white; font-weight: bold; }";a_->setStyleSheet(selectedStyle);b_->setStyleSheet(selectedStyle);compare->addWidget(new QLabel("Compare",this));compare->addWidget(a_);compare->addWidget(b_);
    auto* copy=new QPushButton("Copy to other",this);copy->setObjectName("rack_copy");compare->addWidget(copy);compare->addStretch();
    add_=new QToolButton(this);add_->setText("Add Effect");add_->setObjectName("rack_add");add_->setPopupMode(QToolButton::InstantPopup);
    auto* menu=new QMenu(add_);
    for(int i=0;i<static_cast<int>(ModuleKind::count);++i){
        const auto kind=static_cast<ModuleKind>(i);if(kind==ModuleKind::vst3)continue;auto* action=menu->addAction(module_name(kind));
        action->setObjectName(QString("add_%1").arg(module_id(kind)));
        if(((kind==ModuleKind::pan||kind==ModuleKind::stereo_width)&&channels_!=2)||((kind==ModuleKind::mono||kind==ModuleKind::balance)&&channels_>2))action->setEnabled(false);
        connect(action,&QAction::triggered,this,[this,kind]{attempt(this,[&]{add_module(kind);});});
    }
    add_->setMinimumWidth(110);presets_box_->setAccessibleName("Preset");
    add_->setMenu(menu);compare->addWidget(add_);layout->addLayout(compare);
    scroll_=new QScrollArea(this);scroll_->setWidgetResizable(true);scroll_->setFrameShape(QFrame::NoFrame);layout->addWidget(scroll_,1);
    status_=new QLabel(this);status_->setObjectName("rack_status");status_->setWordWrap(true);layout->addWidget(status_);
    auto* credit=new QLabel("a plugin from <a href=\"https://github.com/Emidj\">DjEmiMas</a>",this);credit->setOpenExternalLinks(true);credit->setAlignment(Qt::AlignRight);layout->addWidget(credit);
    connect(a_,&QPushButton::clicked,this,[this]{select_side(false);});connect(b_,&QPushButton::clicked,this,[this]{select_side(true);});
    connect(copy,&QPushButton::clicked,this,[this]{copy_to_other();});
    connect(presets_box_,&QComboBox::activated,this,[this](int index){if(index>0)attempt(this,[&]{apply_preset(presets_.at(size_t(index-1)));});});
    connect(save,&QPushButton::clicked,this,[this]{
        QPointer<RackWidget> guard(this);bool ok=false;const auto name=QInputDialog::getText(this,"Save As","Preset name",QLineEdit::Normal,name_,&ok);
        if(guard&&ok)attempt(this,[&]{save_as(name);});
    });
    connect(import,&QPushButton::clicked,this,[this]{
        QPointer<RackWidget> guard(this);const auto file=QFileDialog::getOpenFileName(this,"Import preset",{},"NEXUS presets (*.json)");
        if(guard&&!file.isEmpty())attempt(this,[&]{import_preset(file);});
    });
    connect(exportButton,&QPushButton::clicked,this,[this]{
        QPointer<RackWidget> guard(this);const auto file=QFileDialog::getSaveFileName(this,"Export preset","Audio Rack.json","NEXUS presets (*.json)");
        if(guard&&!file.isEmpty())attempt(this,[&]{export_preset(file);});
    });
    presets_refresh();rebuild();set_status("Close this window to keep processing audio.");
}
void RackWidget::validate_layout(const RackDocument& document)const{
    validate_document(document);
    if(contains_vst3(document)&&!choose_vst3_)throw std::invalid_argument("VST3 processing is not available in this editor yet");
    if(contains_vst3(document)&&channels_!=1&&channels_!=2)throw std::invalid_argument("VST3 effects currently require a mono or stereo source");
    for(const auto* config:{&document.a,&document.b})for(const auto& node:*config){
        if((node.kind==ModuleKind::pan||node.kind==ModuleKind::stereo_width)&&channels_!=2)throw std::invalid_argument("This effect requires a stereo source");
        if((node.kind==ModuleKind::mono||node.kind==ModuleKind::balance)&&channels_>2)throw std::invalid_argument("Routing module requires mono or stereo");
    }
}
void RackWidget::set_document(const RackDocument& document){
    validate_layout(document);auto value=document;normalize(value.a);normalize(value.b);
    if(same_document(value,document_))return;
    document_=std::move(value);name_="Custom";presets_refresh();rebuild();
}
void RackWidget::set_sample_rate(double rate){
    if(std::isfinite(rate)&&rate>=8000&&rate<=192000&&rate!=rate_){rate_=rate;rebuild();}
}
bool RackWidget::prepare_edit(bool capture_dirty){
    if(!before_edit)return true;
    const auto callback=before_edit;QPointer<RackWidget> guard(this);
    try{
        auto current=callback(capture_dirty);if(!guard)return false;validate_layout(current);normalize(current.a);normalize(current.b);
        if(!same_document(current,document_)){document_=std::move(current);base_refresh_=true;}
        return true;
    }catch(const std::exception& error){if(guard){rebuild();set_status(QString::fromUtf8(error.what()));}return false;}
}
RackNode* RackWidget::node(uint64_t id){if(!prepare_edit())return nullptr;for(auto& n:document_.active())if(n.id==id)return &n;return nullptr;}
void RackWidget::changed(){
    if(base_refresh_)rebuild();
    name_="Custom";presets_box_->setItemText(0,"Custom");presets_box_->setCurrentIndex(0);
    if(on_change)on_change(document_);
}
void RackWidget::set_vst3_chooser(std::function<void()> choose){
    choose_vst3_=std::move(choose);
    if(!vst3_action_){
        vst3_action_=add_->menu()->addAction("VST3...");
        vst3_action_->setObjectName("add_vst3");
        connect(vst3_action_,&QAction::triggered,this,[this]{if(choose_vst3_)choose_vst3_();});
    }
    vst3_action_->setVisible(bool(choose_vst3_));
    vst3_action_->setEnabled(channels_==1||channels_==2);
    vst3_action_->setToolTip("VST3 effects currently support mono and stereo sources.");
}
void RackWidget::add_vst3(const Vst3Settings& settings){
    if(!prepare_edit())return;
    auto copy=document_;uint64_t id=1;
    for(const auto& n:copy.active()){if(n.id==UINT64_MAX)throw std::invalid_argument("Module ID space exhausted");id=(std::max)(id,n.id+1);}
    auto effect=default_node(ModuleKind::vst3,id);effect.vst3=settings;
    copy.active().push_back(std::move(effect));validate_layout(copy);
    document_=std::move(copy);expanded_=id;rebuild();changed();
}
void RackWidget::add_module(ModuleKind kind){
    if(!prepare_edit())return;
    auto copy=document_;uint64_t id=1;
    for(const auto& n:copy.active()){if(n.id==UINT64_MAX)throw std::invalid_argument("Module ID space exhausted");id=(std::max)(id,n.id+1);}
    copy.active().push_back(default_node(kind,id));validate_layout(copy);
    document_=std::move(copy);expanded_=id;rebuild();changed();
}
void RackWidget::remove_module(uint64_t id){
    if(!prepare_edit())return;
    auto& config=document_.active();config.erase(std::remove_if(config.begin(),config.end(),[&](const auto& n){return n.id==id;}),config.end());
    if(expanded_==id)expanded_=0;rebuild();changed();
}
void RackWidget::move_module(uint64_t id,int direction){
    if(!prepare_edit())return;
    auto& config=document_.active();
    for(size_t i=0;i<config.size();++i)if(config[i].id==id){
        const auto next=static_cast<long long>(i)+direction;
        if(next>=0&&next<static_cast<long long>(config.size()))std::swap(config[i],config[size_t(next)]);
        break;
    }rebuild();changed();
}
void RackWidget::select_side(bool b){
    if(!prepare_edit())return;
    if(document_.use_b==b){if(base_refresh_)rebuild();return;}document_.use_b=b;rebuild();changed();
}
void RackWidget::copy_to_other(){
    if(!prepare_edit(true))return;
    if(document_.use_b)document_.a=document_.b;else document_.b=document_.a;
    changed();
}
void RackWidget::set_source_name(const QString& name){source_->setText(name);}
void RackWidget::set_loudness(const LoudnessReading& r,bool available){
    if(!available){meter_->setText("Output loudness: unavailable for this source layout.");return;}
    auto value=[](float x,bool ready){return !ready?QString("--"):(x<=-120?QString("below -120"):QString::number(x,'f',1));};
    meter_->setText(QString("Output (last audio) | M %1  S %2  I %3 LUFS | Peak %4 dBFS")
        .arg(value(r.momentary,r.momentary_ready),value(r.short_term,r.short_term_ready),
             value(r.integrated,r.integrated_ready),value(r.sample_peak,r.active)));
}
void RackWidget::set_gain_reduction(uint64_t id,float db){
    if(auto* label=findChild<QLabel*>(QString("rack_reduction_%1").arg(id)))
        label->setText(QString("Reduction %1 dB").arg(std::isfinite(db)?db:0,0,'f',1));
}
void RackWidget::set_status(const QString& text){status_->setText(text);}
void RackWidget::apply_preset(const Preset& preset){
    const auto selected=preset;
    if(!prepare_edit(true))return;
    auto copy=document_;copy.active()=selected.document.active();copy.bypass=selected.document.bypass;validate_layout(copy);normalize(copy.active());
    document_=std::move(copy);name_=selected.name;rebuild();presets_refresh();if(on_change)on_change(document_);
}
Preset RackWidget::save_as(const QString& name){QPointer<RackWidget> guard(this);const auto callback=before_snapshot;auto snapshot=callback?callback():document_;if(!guard)throw std::runtime_error("Rack editor closed during capture");auto preset=store_.save_as(name,snapshot);name_=preset.name;presets_refresh();return preset;}
Preset RackWidget::import_preset(const QString& path){
    // Validate source layout before persisting or changing the active rack.
    const auto inspected=store_.inspect_file(path);auto candidate=document_;candidate.active()=inspected.document.active();validate_layout(candidate);
    auto preset=store_.import_value(inspected);
    QPointer<RackWidget> guard(this);apply_preset(preset);if(guard)presets_refresh();return preset;
}
void RackWidget::export_preset(const QString& path)const{QPointer<const RackWidget> guard(this);const auto callback=before_snapshot;auto snapshot=callback?callback():document_;if(!guard)throw std::runtime_error("Rack editor closed during capture");store_.export_file(path,name_,snapshot);}
void RackWidget::presets_refresh(){
    presets_=store_.list();const QSignalBlocker blocker(presets_box_);presets_box_->clear();presets_box_->addItem(name_=="Custom"?"Custom":"Current: "+name_);
    for(const auto& p:presets_)presets_box_->addItem(p.category+" / "+p.name);
    presets_box_->setCurrentIndex(0);
}
void RackWidget::rebuild(){
    base_refresh_=false;
    {const QSignalBlocker active(active_);active_->setChecked(!document_.bypass);}
    {const QSignalBlocker a(a_),b(b_);a_->setChecked(!document_.use_b);b_->setChecked(document_.use_b);}
    add_->setEnabled(document_.active().size()<16);
    auto* container=new QWidget;auto* list=new QVBoxLayout(container);list->setContentsMargins(0,0,0,0);list->setSpacing(10);
    const auto& config=document_.active();
    if(config.empty()){auto* empty=new QLabel("No effects. Add an effect to begin.",container);empty->setAlignment(Qt::AlignCenter);list->addWidget(empty);}
    for(size_t index=0;index<config.size();++index){
        const auto value=config[index];const auto id=value.id;
        auto* card=new QFrame(container);card->setFrameShape(QFrame::StyledPanel);card->setObjectName(QString("rack_node_%1").arg(id));
        auto* cardLayout=new QVBoxLayout(card);auto* header=new QHBoxLayout;
        auto* expand=new QToolButton(card);expand->setText(expanded_==id?"-":"+");expand->setObjectName(QString("rack_expand_%1").arg(id));header->addWidget(expand);
        const auto display=value.kind==ModuleKind::vst3?
            (value.vst3.name.empty()?QFileInfo(QString::fromStdString(value.vst3.path)).completeBaseName():QString::fromStdString(value.vst3.name)):QString(module_name(value.kind));
        auto* enabled=new QCheckBox(display,card);enabled->setChecked(value.enabled);enabled->setObjectName(QString("rack_enabled_%1").arg(id));header->addWidget(enabled,1);
        if(is_dynamics(value.kind)){
            auto* reduction=new QLabel("Reduction -- dB",card);reduction->setObjectName(QString("rack_reduction_%1").arg(id));
            reduction->setMinimumWidth(reduction->fontMetrics().horizontalAdvance("Reduction 80.0 dB")+8);
            reduction->setToolTip("Maximum gain reduction over the last processed block, before makeup gain.");
            header->addWidget(reduction);
        }
        auto* up=new QPushButton("Up",card);up->setEnabled(index>0);up->setObjectName(QString("rack_up_%1").arg(id));header->addWidget(up);
        auto* down=new QPushButton("Down",card);down->setEnabled(index+1<config.size());down->setObjectName(QString("rack_down_%1").arg(id));header->addWidget(down);
        auto* remove=new QPushButton("Remove",card);remove->setObjectName(QString("rack_remove_%1").arg(id));header->addWidget(remove);cardLayout->addLayout(header);
        connect(expand,&QToolButton::clicked,this,[this,id]{expanded_=expanded_==id?0:id;rebuild();});
        connect(enabled,&QCheckBox::toggled,this,[this,id](bool on){if(auto* n=node(id)){n->enabled=on;changed();}});
        connect(up,&QPushButton::clicked,this,[this,id]{move_module(id,-1);});connect(down,&QPushButton::clicked,this,[this,id]{move_module(id,1);});
        connect(remove,&QPushButton::clicked,this,[this,id]{remove_module(id);});
        if(expanded_==id){
            if(value.kind==ModuleKind::vst3){
                auto* info=new QLabel(QString("VST3 | %1\n%2").arg(QString::fromStdString(value.vst3.vendor),QString::fromStdString(value.vst3.path)),card);
                info->setWordWrap(true);info->setTextFormat(Qt::PlainText);cardLayout->addWidget(info);
                auto* open=new QPushButton("Open plugin interface",card);open->setObjectName(QString("vst3_open_%1").arg(id));
                open->setEnabled(bool(on_open_vst3_editor));cardLayout->addWidget(open);
                connect(open,&QPushButton::clicked,this,[this,id]{if(on_open_vst3_editor)on_open_vst3_editor(id);});
            }else if(value.kind==ModuleKind::equalizer){
                auto* eq=new EqWidget(card);eq->set_sample_rate(rate_);eq->set_settings(value.eq);
                eq->findChild<QCheckBox*>("eq_enabled")->hide();
                eq->on_change=[this,id](const EqSettings& settings){if(auto* n=node(id)){n->eq=settings;changed();}};
                cardLayout->addWidget(eq);
            }else if(is_dynamics(value.kind)){
                if(value.kind==ModuleKind::expander){
                    auto* gate=new QCheckBox("Noise Gate",card);gate->setObjectName(QString("rack_gate_%1").arg(id));gate->setChecked(value.dynamics.gate);
                    cardLayout->addWidget(gate);
                    connect(gate,&QCheckBox::toggled,this,[this,id](bool v){if(auto* n=node(id)){n->dynamics.gate=v;changed();rebuild();}});
                }
                for(const auto& field:dynamics_fields){
                    const std::string key=field.key;
                    bool visible=key=="threshold_db"||key=="release_ms";
                    if(value.kind!=ModuleKind::limiter)visible=visible||key=="attack_ms";
                    if(value.kind==ModuleKind::compressor)visible=visible||key=="ratio"||key=="makeup_db";
                    if(value.kind==ModuleKind::expander)visible=visible||key=="range_db"||
                        (value.dynamics.gate?key=="hold_ms":key=="ratio");
                    if(!visible)continue;
                    auto* parameter=new QDoubleSpinBox(card);parameter->setObjectName(QString("rack_dynamics_%1_%2").arg(id).arg(field.key));
                    parameter->setRange(field.low,field.high);parameter->setDecimals(1);parameter->setSingleStep(key=="ratio"?.1:1);
                    QString label;
                    if(key=="threshold_db"){label=value.kind==ModuleKind::limiter?"Ceiling":"Threshold";parameter->setSuffix(" dB");}
                    else if(key=="ratio"){label="Ratio";parameter->setSuffix(":1");}
                    else if(key=="attack_ms"){label="Attack";parameter->setSuffix(" ms");}
                    else if(key=="release_ms"){label="Release";parameter->setSuffix(" ms");}
                    else if(key=="hold_ms"){label="Hold";parameter->setSuffix(" ms");}
                    else{label=key=="makeup_db"?"Output gain":"Maximum reduction";parameter->setSuffix(" dB");}
                    parameter->setValue(value.dynamics.*(field.member));
                    auto* row=new QHBoxLayout;auto* caption=new QLabel(label,card);
                    caption->setMinimumWidth(caption->fontMetrics().horizontalAdvance("Maximum reduction")+8);
                    row->addWidget(caption);row->addWidget(parameter,1);cardLayout->addLayout(row);
                    const auto member=field.member;
                    connect(parameter,&QDoubleSpinBox::valueChanged,this,[this,id,member](double v){if(auto* n=node(id)){n->dynamics.*member=float(v);changed();}});
                }
                auto* hint=new QLabel(value.kind==ModuleKind::limiter?"Sample peak only. Place last to cap the rack output.":
                    (value.kind==ModuleKind::compressor?"Linked peak detector. Attack allows transients.":
                     "Linked peak detector. Gate closes 3 dB below its opening threshold."),card);
                hint->setWordWrap(true);cardLayout->addWidget(hint);
            }else if(value.kind==ModuleKind::mono||value.kind==ModuleKind::polarity){
                cardLayout->addWidget(new QLabel(value.kind==ModuleKind::mono?"Average left and right into both channels.":"Invert the polarity of all channels.",card));
            }else{
                auto* parameter=new QDoubleSpinBox(card);parameter->setObjectName(QString("rack_value_%1").arg(id));
                QString label;
                if(value.kind==ModuleKind::gain){parameter->setRange(-60,24);parameter->setDecimals(1);parameter->setSuffix(" dB");label="Level";}
                else if(value.kind==ModuleKind::high_pass||value.kind==ModuleKind::low_pass){parameter->setRange(20,20000);parameter->setDecimals(1);parameter->setSuffix(" Hz");label="Cutoff";}
                else if(value.kind==ModuleKind::stereo_width){parameter->setRange(0,200);parameter->setDecimals(0);parameter->setSuffix(" %");label="Width";}
                else{parameter->setRange(-1,1);parameter->setDecimals(2);parameter->setSingleStep(.05);label="Left / Right";}
                parameter->setValue(value.kind==ModuleKind::stereo_width?value.value*100:value.value);
                auto* row=new QHBoxLayout;row->addWidget(new QLabel(label,card));row->addWidget(parameter,1);cardLayout->addLayout(row);
                if(value.kind==ModuleKind::stereo_width){
                    auto* hint=new QLabel("100% preserves stereo; 0% folds to mono. Widening can increase peaks and reduce mono compatibility.",card);
                    hint->setWordWrap(true);cardLayout->addWidget(hint);
                }
                if(value.kind==ModuleKind::pan)cardLayout->addWidget(new QLabel("Mixes the input to mono, then pans to stereo.",card));
                connect(parameter,&QDoubleSpinBox::valueChanged,this,[this,id](double v){if(auto* n=node(id)){n->value=float(n->kind==ModuleKind::stereo_width?v/100:v);changed();}});
            }
        }list->addWidget(card);
    }
    list->addStretch();
    if(auto* old=scroll_->takeWidget()){old->hide();old->deleteLater();}
    scroll_->setWidget(container);
}
}