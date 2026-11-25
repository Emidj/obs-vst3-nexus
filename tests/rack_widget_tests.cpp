#include <QAction>
#include "ui/rack_widget.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPalette>
#include <QPushButton>
#include <QPointer>
#include <QTemporaryDir>
#include <QToolButton>
#include <iostream>
#include <stdexcept>
using namespace nexus;
using namespace nexus::ui;
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,"Malformed data accepted");}
void events(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=3)return 2;
    try{
        app.setStyle("Fusion");
        QPalette palette;palette.setColor(QPalette::Window,QColor("#242b35"));palette.setColor(QPalette::WindowText,QColor("#ecf1f7"));
        palette.setColor(QPalette::Base,QColor("#18202a"));palette.setColor(QPalette::Text,QColor("#ecf1f7"));
        palette.setColor(QPalette::Button,QColor("#35404f"));palette.setColor(QPalette::ButtonText,QColor("#ecf1f7"));app.setPalette(palette);
        QTemporaryDir temp(QString::fromUtf8(argv[1])+"/rack-ui-XXXXXX");check(temp.isValid(),"Test directory");
        auto doc=default_document();doc.a[0].id=UINT64_MAX;doc.b[1].eq.bands[0].gain_db=6;doc.use_b=true;doc.bypass=true;
        check(same_document(doc,parse_rack_json(rack_json(doc))),"JSON roundtrip / 64-bit ID");
        auto bad=rack_json(doc);bad["schema_version"]=3;rejects([&]{parse_rack_json(bad);});
        bad=rack_json(doc);auto list=bad["a"].toArray();list.append(list[0]);bad["a"]=list;rejects([&]{parse_rack_json(bad);});
        bad=rack_json(doc);list=bad["a"].toArray();auto node=list[0].toObject();node["type"]="unknown-vst";list[0]=node;bad["a"]=list;rejects([&]{parse_rack_json(bad);});
        PresetStore store(temp.path());const auto factories=store.list();check(factories.size()==4,"Factory catalog");
        auto saved=store.save_as("My / unicode preset",doc);check(saved.category=="My Presets"&&QFile::exists(saved.path),"Save As");
        check(same_rack(saved.document.active(),doc.active())&&saved.document.bypass,"Preset content");
        auto again=store.save_as("My / unicode preset",doc);check(saved.path!=again.path&&QFile::exists(saved.path),"Save As overwrote another preset");
        const auto exported=temp.path()+"/export.json";store.export_file(exported,"Export",doc);
        const auto imported=store.import_file(exported);check(imported.category=="Imported","Imported category");
        check(same_rack(imported.document.active(),doc.active()),"Imported content");
        const auto broken=temp.path()+"/broken.json";{QFile file(broken);file.open(QIODevice::WriteOnly);file.write("{ broken");}
        const auto before=store.list().size();rejects([&]{store.import_file(broken);});check(store.list().size()==before,"Invalid import persisted");
        RackWidget widget(temp.path(),2);widget.resize(780,900);widget.show();events();
        int callbacks=0;widget.on_change=[&](const RackDocument& state){validate_rack(state.active());++callbacks;};
        const auto initial=widget.document();
        auto unsupported=default_document();auto internalVst=default_node(ModuleKind::vst3,1);
        internalVst.vst3.path="C:/InternalFixture.vst3";internalVst.vst3.class_id=std::string(32,'A');
        unsupported.a={internalVst};unsupported.b=unsupported.a;
        validate_rack(unsupported.a);
        check(same_document(unsupported,parse_rack_json(rack_json(unsupported))),"VST3 configuration roundtrip");
        check(!widget.findChild<QAction*>("add_vst3"),"Unfinished VST3 selector exposed");
        rejects([&]{widget.set_document(unsupported);});
        const auto internalFile=temp.path()+"/internal-vst3.json";
        store.export_file(internalFile,"Internal VST3",unsupported);
        const auto presetCount=store.list().size();
        rejects([&]{widget.import_preset(internalFile);});
        check(same_document(initial,widget.document()),"Rejected VST3 import changed rack");
        check(store.list().size()==presetCount,"Rejected VST3 import persisted a copy");


        widget.add_module(ModuleKind::high_pass);events();check(widget.document().active().size()==3,"Add effect");
        const auto id=widget.document().active().back().id;
        auto* cutoff=widget.findChild<QDoubleSpinBox*>(QString("rack_value_%1").arg(id));check(cutoff,"Native controls");
        cutoff->setValue(120);check(widget.document().active().back().value==120,"Parameter sync");
        widget.move_module(id,-1);events();check(widget.document().active()[1].id==id,"Move effect");
        widget.remove_module(id);events();check(same_rack(initial.active(),widget.document().active()),"Remove effect");
        widget.select_side(true);widget.add_module(ModuleKind::mono);events();
        check(widget.document().a.size()==2&&widget.document().b.size()==3,"A/B independence");
        widget.copy_to_other();check(same_rack(widget.document().a,widget.document().b),"Copy A/B");
        auto* active=widget.findChild<QCheckBox*>("rack_active");active->setChecked(false);check(widget.document().bypass,"Global bypass");
        auto factory=store.list()[1];widget.apply_preset(factory);events();check(same_rack(widget.document().active(),factory.document.active()),"Apply Factory");
        const auto uiSave=widget.save_as("UI save");check(uiSave.category=="My Presets","UI Save As");
        widget.import_preset(exported);events();check(same_rack(widget.document().active(),doc.active()),"UI import");
        const auto validState=widget.document();rejects([&]{widget.import_preset(broken);});check(same_document(validState,widget.document()),"Invalid import changed rack");
        // Mono source cannot accept the stereo panner, including imported files.
        RackWidget mono(temp.path()+"/mono",1);rejects([&]{mono.add_module(ModuleKind::pan);});
        auto pan=default_document();pan.a={default_node(ModuleKind::pan,1)};pan.b=pan.a;
        const auto panFile=temp.path()+"/pan.json";store.export_file(panFile,"Pan",pan);
        rejects([&]{mono.import_preset(panFile);});
        check(!QDir(temp.path()+"/mono/Imported").exists(),"Rejected mono import wrote files");
        // Dynamics controls, complete persistence and migration of pre-dynamics presets.
        auto dynamicsDoc=default_document();
        for(auto kind:{ModuleKind::compressor,ModuleKind::expander,ModuleKind::limiter,ModuleKind::stereo_width})
            dynamicsDoc.a.push_back(default_node(kind,dynamicsDoc.a.size()+1));
        dynamicsDoc.a[2].dynamics.threshold_db=-27;dynamicsDoc.a[3].dynamics.gate=true;dynamicsDoc.b=dynamicsDoc.a;
        check(same_document(dynamicsDoc,parse_rack_json(rack_json(dynamicsDoc))),"Dynamics JSON roundtrip");
        auto malformed=rack_json(dynamicsDoc);auto modules=malformed["a"].toArray();auto dynamicNode=modules[2].toObject();
        dynamicNode.remove("dynamics");modules[2]=dynamicNode;malformed["a"]=modules;rejects([&]{parse_rack_json(malformed);});
        auto legacy=rack_json(default_document());
        for(const auto key:{"a","b"}){auto nodes=legacy[key].toArray();for(int i=0;i<nodes.size();++i){auto item=nodes[i].toObject();item.remove("dynamics");nodes[i]=item;}legacy[key]=nodes;}
        check(same_document(default_document(),parse_rack_json(legacy)),"Legacy preset compatibility");
        rejects([&]{mono.add_module(ModuleKind::stereo_width);});
        widget.set_document(default_document());events();widget.add_module(ModuleKind::compressor);events();
        auto* threshold=widget.findChild<QDoubleSpinBox*>("rack_dynamics_3_threshold_db");check(threshold,"Compressor control");
        threshold->setValue(-27);check(widget.document().active().back().dynamics.threshold_db==-27,"Compressor control sync");
        widget.set_gain_reduction(3,6);check(widget.findChild<QLabel*>("rack_reduction_3")->text()=="Reduction 6.0 dB","Gain reduction display");
        widget.add_module(ModuleKind::expander);events();auto* gateMode=widget.findChild<QCheckBox*>("rack_gate_4");check(gateMode,"Gate mode");
        gateMode->setChecked(true);events();check(widget.document().active().back().dynamics.gate&&widget.findChild<QDoubleSpinBox*>("rack_dynamics_4_hold_ms"),"Gate hold control");
        widget.add_module(ModuleKind::limiter);events();check(widget.findChild<QDoubleSpinBox*>("rack_dynamics_5_threshold_db"),"Limiter ceiling");
        widget.add_module(ModuleKind::stereo_width);events();auto* width=widget.findChild<QDoubleSpinBox*>("rack_value_6");check(width,"Width control");
        width->setValue(150);check(widget.document().active().back().value==1.5f,"Width percent conversion");
        const auto dynSaved=widget.save_as("Dynamics");widget.apply_preset(dynSaved);events();
        check(widget.document().active().size()==6&&widget.document().active()[3].dynamics.gate,"Dynamics preset Save As");
        widget.set_document(default_document());events();
        widget.set_source_name("Source: DJ MASTER");
        widget.set_loudness({-18,-19,-20,-1,true,true,true,true},true);
        auto* loudness=widget.findChild<QLabel*>("rack_loudness");
        check(loudness&&loudness->text().contains("-20.0 LUFS")&&loudness->text().contains("-1.0 dBFS"),"Loudness display");
        widget.set_loudness({},false);check(loudness->text().contains("unavailable"),"Loudness layout boundary");
        widget.set_loudness({-18,-19,-20,-1,true,true,true,true},true);
        auto preview=widget.document();preview.a[1].eq.bands[0].gain_db=3;preview.a[1].eq.bands[1].gain_db=-2;widget.set_document(preview);events();
        auto* expand=widget.findChild<QToolButton*>("rack_expand_2");if(expand&&expand->text()=="+")expand->click();events();
        QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);widget.render(&image);check(image.save(argv[2]),"Preview render");
        widget.set_document(default_document());events();widget.add_module(ModuleKind::compressor);events();
        widget.set_gain_reduction(3,6);
        QImage dynamicsImage(widget.size(),QImage::Format_ARGB32_Premultiplied);dynamicsImage.fill(Qt::transparent);widget.render(&dynamicsImage);
        check(dynamicsImage.save(QString::fromUtf8(argv[1])+"/dynamics-editor-preview.png"),"Dynamics preview render");
        {
            RackWidget fresh(temp.path()+"/fresh-base");auto latest=default_document();bool captured=false;
            fresh.before_edit=[&](bool capture){captured=captured||capture;return latest;};fresh.on_change=[&](const RackDocument& value){latest=value;};
            latest.a[0].value=-12;fresh.add_module(ModuleKind::compressor);
            check(latest.a.size()==3&&latest.a[0].value==-12,"Structural edit overwrote newer source settings");
            latest.a[0].value=-9;fresh.findChild<QCheckBox*>("rack_active")->setChecked(false);
            check(latest.bypass&&latest.a[0].value==-9,"Bypass used stale source settings");
            auto reset=store.list().front();reset.document=default_document();reset.name="Explicit reset";fresh.apply_preset(reset);
            check(captured&&latest.active()[0].value==0&&latest.active().size()==2,"Explicit preset was merged with old values");
            latest.a[0].value=-15;captured=false;fresh.copy_to_other();
            check(captured&&latest.b[0].value==-15,"Copy A/B used stale displayed values");
        }
        {
            QPointer<RackWidget> closing=new RackWidget(temp.path()+"/closed-during-edit");auto* raw=closing.data();
            raw->before_edit=[raw](bool){delete raw;return default_document();};raw->copy_to_other();
            check(closing.isNull(),"Editor closed during capture remained alive");
        }
        check(callbacks>=8,"Editor callbacks");
        std::cout<<"PASS: presets, strict JSON, Factory protection, import/export, editor order, parameters and A/B\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}