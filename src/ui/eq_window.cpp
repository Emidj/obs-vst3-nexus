#include "ui/eq_widget.hpp"
#include "obs/eq_settings.hpp"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <QVBoxLayout>
#include <vector>
#include <memory>
namespace nexus::ui {
namespace {
class Window final : public QDialog {
public:
    explicit Window(obs_source_t* source):weak_(obs_source_get_weak_source(source)){
        setAttribute(Qt::WA_DeleteOnClose);setWindowTitle("NEXUS Equalizer");resize(700,510);
        auto* layout=new QVBoxLayout(this);panel_=new EqWidget(this);layout->addWidget(panel_);
        auto* credit=new QLabel("a plugin from <a href=\"https://github.com/Emidj\">DjEmiMas</a>",this);
        credit->setOpenExternalLinks(true);credit->setAlignment(Qt::AlignRight);layout->addWidget(credit);
        panel_->on_change=[this](const EqSettings& values){
            if(auto* source=obs_weak_source_get_source(weak_)){
                auto* settings=obs_source_get_settings(source);obs_bridge::write(settings,values);
                obs_source_update(source,settings);obs_data_release(settings);obs_source_release(source);
            }else close();
        };
        auto* timer=new QTimer(this);
        QObject::connect(timer,&QTimer::timeout,this,[this]{refresh();});timer->start(250);refresh();
    }
    ~Window()override{obs_weak_source_release(weak_);}
    bool matches(obs_source_t* source)const{return obs_weak_source_references_source(weak_,source);}
private:
    void refresh(){
        auto* source=obs_weak_source_get_source(weak_);if(!source){close();return;}
        auto* settings=obs_source_get_settings(source);panel_->set_settings(obs_bridge::read(settings));
        obs_data_release(settings);obs_source_release(source);
        obs_audio_info info{};if(obs_get_audio_info(&info))panel_->set_sample_rate(info.samples_per_sec);
    }
    obs_weak_source_t* weak_;
    EqWidget* panel_;
};
std::vector<QPointer<Window>> windows;
QPointer<QObject> dispatcher;
}
void open_equalizer(obs_source_t* source){
    if(!source || !qApp || QThread::currentThread()!=qApp->thread())return;
    if(!dispatcher)dispatcher=new QObject(qApp);
    auto weak=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(source),obs_weak_source_release);
    QMetaObject::invokeMethod(dispatcher,[weak]{
        auto* live=obs_weak_source_get_source(weak.get());if(!live)return;
        windows.erase(std::remove_if(windows.begin(),windows.end(),[](const auto& p){return p.isNull();}),windows.end());
        for(const auto& window:windows)if(window && window->matches(live)){
            window->showNormal();window->raise();window->activateWindow();obs_source_release(live);return;
        }
        auto* window=new Window(live);windows.push_back(window);window->show();obs_source_release(live);
    },Qt::QueuedConnection);
}
void close_equalizers(){
    if(!qApp)return;
    // OBS unloads modules on shutdown; no queued callbacks may outlive the DLL.
    auto close=[]{
        // Deleting the module context cancels queued opens and releases weak handles.
        if(dispatcher)delete dispatcher.data();
        for(auto& window:windows)if(window)delete window.data();
        windows.clear();
    };
    if(QThread::currentThread()==qApp->thread())close();
    else QMetaObject::invokeMethod(qApp,close,Qt::BlockingQueuedConnection);
}
}