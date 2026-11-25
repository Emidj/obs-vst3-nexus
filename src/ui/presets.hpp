#pragma once
#include "dsp/rack.hpp"
#include <QJsonObject>
#include <QString>
namespace nexus::ui {
QJsonObject rack_json(const RackDocument&);
RackDocument parse_rack_json(const QJsonObject&);
struct Preset {
    QString name,category,path;
    RackDocument document;
};
class PresetStore {
public:
    explicit PresetStore(QString directory):directory_(std::move(directory)){}
    std::vector<Preset> list()const;
    Preset save_as(const QString& name,const RackDocument&);
    Preset inspect_file(const QString&)const;
    Preset import_file(const QString&);
    Preset import_value(const Preset&);
    void export_file(const QString&,const QString& name,const RackDocument&)const;
private:
    Preset persist(const QString& category,const QString& name,const RackDocument&);
    QString directory_;
};
}