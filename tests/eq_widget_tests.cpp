#include "ui/eq_widget.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <iostream>
#include <cmath>
#include <stdexcept>
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    if(argc!=2)return 2;
    try{
        app.setStyle("Fusion");
        QPalette palette;palette.setColor(QPalette::Window,QColor("#242b35"));palette.setColor(QPalette::WindowText,QColor("#ecf1f7"));
        palette.setColor(QPalette::Base,QColor("#18202a"));palette.setColor(QPalette::Text,QColor("#ecf1f7"));
        palette.setColor(QPalette::Button,QColor("#35404f"));palette.setColor(QPalette::ButtonText,QColor("#ecf1f7"));
        app.setPalette(palette);
        nexus::ui::EqWidget widget;widget.resize(700,500);int callbacks=0;
        widget.on_change=[&](const auto&){++callbacks;};
        widget.show();app.processEvents();
        auto* gain=widget.findChild<QDoubleSpinBox*>("eq_band_0_gain");require(gain,"Gain control missing");
        gain->setValue(6);require(callbacks==1 && widget.settings().bands[0].gain_db==6,"Numeric gain sync");
        auto* curve=widget.findChild<QWidget*>("eq_curve");require(curve,"Curve missing");
        auto position=[&](double hz,double db){
            return QPointF(42+std::log(hz/20)/std::log(1000.)*(curve->width()-58),
                           16+(curve->height()-48)/2.-db/48*(curve->height()-48));
        };
        const auto start=position(120,6),finish=position(240,3);
        QMouseEvent press(QEvent::MouseButtonPress,start,curve->mapToGlobal(start.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(curve,&press);
        QMouseEvent move(QEvent::MouseMove,finish,curve->mapToGlobal(finish.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(curve,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,finish,curve->mapToGlobal(finish.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(curve,&release);
        require(std::abs(widget.settings().bands[0].frequency-240)<.1 && std::abs(widget.settings().bands[0].gain_db-3)<.01,"Curve drag sync");
        auto* enabled=widget.findChild<QCheckBox*>("eq_enabled");enabled->setChecked(false);require(!widget.settings().enabled,"Enable sync");
        auto* reset=widget.findChild<QPushButton*>("eq_reset");reset->click();
        require(widget.settings().enabled && widget.settings().bands[0].frequency==120 && widget.settings().bands[0].gain_db==0,"Reset sync");
        nexus::EqSettings preview;preview.bands={{{120,4,.7f,true},{500,-3,1.5f,true},{2000,6,1,true},{6000,-2,.8f,true}}};
        widget.set_settings(preview);app.processEvents();
        QImage image(widget.size()*widget.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(widget.devicePixelRatioF());image.fill(Qt::transparent);widget.render(&image);
        require(image.save(argv[1]),"Cannot write preview image");
        std::cout<<"PASS: numeric controls, curve drag, enable, reset and offscreen render\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}