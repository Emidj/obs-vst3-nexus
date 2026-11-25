#include "ui/eq_widget.hpp"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace nexus::ui {
namespace {
const QColor colors[]{QColor("#4cb8ff"),QColor("#e9b949"),QColor("#a78bfa"),QColor("#4adea2")};
}
class EqCurve final : public QWidget {
public:
    explicit EqCurve(EqWidget* owner):QWidget(owner),owner_(owner){
        setObjectName("eq_curve");setMinimumSize(440,210);setMouseTracking(true);
        setAccessibleName("Equalizer response curve");
        setToolTip("Drag a numbered point to change frequency and gain. Display range: -24 to +24 dB.");
    }
protected:
    QRectF plot()const{return QRectF(42,16,width()-58,height()-48);}
    double maximum()const{return (std::min)(20000.,owner_->rate_*.45);}
    double x(double frequency)const{return plot().left()+std::log((std::min)(frequency,maximum())/20)/std::log(maximum()/20)*plot().width();}
    double y(double gain)const{return plot().center().y()-gain/48*plot().height();}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#141a22"));
        p.setFont(QFont(p.font().family(),9));
        for(int gain:{-24,-12,0,12,24}){
            p.setPen(gain==0?QColor("#526173"):QColor("#2c3542"));p.drawLine(QPointF(plot().left(),y(gain)),QPointF(plot().right(),y(gain)));
            p.setPen(QColor("#a9b5c4"));p.drawText(QRectF(0,y(gain)-8,36,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(gain));
        }
        for(double f:{20.,100.,1000.,10000.,20000.})if(f<=maximum()){
            p.setPen(QColor("#2c3542"));p.drawLine(QPointF(x(f),plot().top()),QPointF(x(f),plot().bottom()));
            p.setPen(QColor("#a9b5c4"));p.drawText(QRectF(x(f)-22,plot().bottom()+7,44,20),Qt::AlignCenter,f>=1000?QString::number(f/1000)+"k":QString::number(f));
        }
        QPainterPath path;
        for(int i=0;i<=240;++i){
            const double frequency=20*std::pow(maximum()/20,double(i)/240);
            const double db=equalizer_response_db(owner_->settings_,owner_->rate_,frequency);
            const QPointF point(x(frequency),y(std::clamp(db,-24.,24.)));
            if(i==0)path.moveTo(point);else path.lineTo(point);
        }
        p.setPen(QPen(owner_->settings_.enabled?QColor("#e7eef8"):QColor("#66717f"),2));p.drawPath(path);
        for(size_t i=0;i<4;++i){
            const auto& band=owner_->settings_.bands[i];const QPointF at(x(band.frequency),y(band.gain_db));
            p.setBrush(band.enabled&&owner_->settings_.enabled?colors[i]:QColor("#647080"));p.setPen(Qt::NoPen);p.drawEllipse(at,10,10);
            p.setPen(QColor("#10151c"));p.drawText(QRectF(at.x()-10,at.y()-10,20,20),Qt::AlignCenter,QString::number(i+1));
        }
    }
    void mousePressEvent(QMouseEvent* e)override{
        if(e->button()!=Qt::LeftButton)return;
        selected_=-1;
        for(size_t i=0;i<4;++i){
            const auto& b=owner_->settings_.bands[i];
            if(std::hypot(e->position().x()-x(b.frequency),e->position().y()-y(b.gain_db))<=15)selected_=int(i);
        }
    }
    void mouseMoveEvent(QMouseEvent* e)override{
        if(selected_<0 || !(e->buttons()&Qt::LeftButton))return;
        const auto area=plot();
        const double position=std::clamp((e->position().x()-area.left())/area.width(),0.,1.);
        const double gain=std::clamp((area.center().y()-e->position().y())*48/area.height(),-18.,18.);
        owner_->move_band(size_t(selected_),20*std::pow(maximum()/20,position),gain);
    }
    void mouseReleaseEvent(QMouseEvent*)override{selected_=-1;}
private:
    EqWidget* owner_;int selected_=-1;
};
EqWidget::EqWidget(QWidget* parent):QWidget(parent){
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(16,16,16,16);layout->setSpacing(12);
    auto* heading=new QHBoxLayout;enabled_=new QCheckBox("Equalizer",this);enabled_->setObjectName("eq_enabled");
    auto font=enabled_->font();font.setPointSize(font.pointSize()+2);font.setBold(true);enabled_->setFont(font);
    heading->addWidget(enabled_);heading->addStretch();
    auto* reset=new QPushButton("Reset",this);reset->setObjectName("eq_reset");heading->addWidget(reset);layout->addLayout(heading);
    curve_=new EqCurve(this);layout->addWidget(curve_,1);
    auto* grid=new QGridLayout;grid->setHorizontalSpacing(12);grid->setVerticalSpacing(8);
    grid->addWidget(new QLabel("Band",this),0,0);grid->addWidget(new QLabel("Frequency",this),0,1);
    grid->addWidget(new QLabel("Gain",this),0,2);grid->addWidget(new QLabel("Width (Q)",this),0,3);
    for(size_t i=0;i<4;++i){
        const int row=int(i)+1;
        active_[i]=new QCheckBox(QString::number(i+1),this);
        active_[i]->setObjectName(QString("eq_band_%1_enabled").arg(i));
        grid->addWidget(active_[i],row,0);
        frequency_[i]=new QDoubleSpinBox(this);frequency_[i]->setRange(20,20000);frequency_[i]->setDecimals(1);frequency_[i]->setSuffix(" Hz");frequency_[i]->setSingleStep(10);
        gain_[i]=new QDoubleSpinBox(this);gain_[i]->setRange(-18,18);gain_[i]->setDecimals(1);gain_[i]->setSuffix(" dB");gain_[i]->setSingleStep(.5);
        q_[i]=new QDoubleSpinBox(this);q_[i]->setRange(.2,10);q_[i]->setDecimals(2);q_[i]->setSingleStep(.1);
        q_[i]->setToolTip("Higher Q means a narrower band.");
        QDoubleSpinBox* controls[]{frequency_[i],gain_[i],q_[i]};
        const char* names[]{"frequency","gain","q"};
        for(int c=0;c<3;++c){
            controls[c]->setObjectName(QString("eq_band_%1_%2").arg(i).arg(names[c]));controls[c]->setMinimumWidth(112);
            grid->addWidget(controls[c],row,c+1);
            connect(controls[c],&QDoubleSpinBox::valueChanged,this,[this]{controls_changed();});
        }
        connect(active_[i],&QCheckBox::toggled,this,[this]{controls_changed();});
    }
    layout->addLayout(grid);
    auto* hint=new QLabel("Drag a point or adjust a band.",this);
    hint->setWordWrap(true);layout->addWidget(hint);
    connect(enabled_,&QCheckBox::toggled,this,[this]{controls_changed();});
    connect(reset,&QPushButton::clicked,this,[this]{set_settings(EqSettings{});if(on_change)on_change(settings_);});
    refresh();
}
void EqWidget::set_settings(const EqSettings& value){
    for(const auto& band:value.bands)if(!valid_eq_band(band))return;
    bool same=settings_.enabled==value.enabled;
    for(size_t i=0;i<4;++i){
        const auto& a=settings_.bands[i];const auto& b=value.bands[i];
        same=same&&a.frequency==b.frequency&&a.gain_db==b.gain_db&&a.q==b.q&&a.enabled==b.enabled;
    }
    if(same)return;
    settings_=value;refresh();
}
void EqWidget::set_sample_rate(double rate){if(std::isfinite(rate)&&rate>=8000&&rate<=192000){rate_=rate;curve_->update();}}
void EqWidget::refresh(){
    {const QSignalBlocker block(enabled_);enabled_->setChecked(settings_.enabled);}
    for(size_t i=0;i<4;++i){
        const QSignalBlocker a(active_[i]),f(frequency_[i]),g(gain_[i]),q(q_[i]);
        active_[i]->setChecked(settings_.bands[i].enabled);frequency_[i]->setValue(settings_.bands[i].frequency);
        gain_[i]->setValue(settings_.bands[i].gain_db);q_[i]->setValue(settings_.bands[i].q);
    }
    curve_->update();
}
void EqWidget::controls_changed(){
    settings_.enabled=enabled_->isChecked();
    for(size_t i=0;i<4;++i)settings_.bands[i]={float(frequency_[i]->value()),float(gain_[i]->value()),float(q_[i]->value()),active_[i]->isChecked()};
    curve_->update();if(on_change)on_change(settings_);
}
void EqWidget::move_band(size_t i,double frequency,double db){
    if(i>=4||!std::isfinite(frequency)||frequency<20||frequency>20000||!std::isfinite(db)||db<-18||db>18)return;
    settings_.bands[i].frequency=float(frequency);settings_.bands[i].gain_db=float(db);
    refresh();if(on_change)on_change(settings_);
}
}