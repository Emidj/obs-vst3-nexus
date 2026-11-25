#include "obs/vst3_runtime.hpp"
#include "vst3/rack_factory.hpp"
#include "vst3/keyboard.hpp"
#include <QApplication>
#include <QPointer>
#include <QThread>
#include <QObject>
#include <QTimer>
#include <algorithm>
#include <mutex>
#include <stdexcept>
namespace nexus::obs_bridge {
namespace {
QPointer<QObject> dispatcher;
std::mutex retired_mutex,dispatch_mutex;
std::vector<LiveRack*> retired;
std::vector<std::function<bool()>> watchers,pending_watchers;
bool dispatching_watchers=false;
void drain(){
    if(vst3::editor_callback_active())return;
    vst3::EditorCallbackGuard deletion;
    std::vector<LiveRack*> pending;
    {std::lock_guard<std::mutex> lock(retired_mutex);pending.swap(retired);}
    for(auto* rack:pending)delete rack;
}
}
bool is_vst3_control_thread()noexcept{return qApp&&QThread::currentThread()==qApp->thread();}
void require_vst3_control_thread(){
    if(!is_vst3_control_thread())
        throw std::runtime_error("Experimental VST3 lifecycle requires the OBS UI thread");
}
bool initialize_vst3_runtime(){
    if(!qApp||QThread::currentThread()!=qApp->thread())return false;
    std::lock_guard<std::mutex> lock(dispatch_mutex);
    if(!dispatcher){
        dispatcher=new QObject(qApp);
        auto* timer=new QTimer(dispatcher);timer->setInterval(20);
        QObject::connect(timer,&QTimer::timeout,dispatcher,[]{
            if(dispatching_watchers)return;
            dispatching_watchers=true;
            struct Reset{~Reset(){dispatching_watchers=false;}} reset;
            for(auto& watch:pending_watchers)watchers.push_back(std::move(watch));
            pending_watchers.clear();
            watchers.erase(std::remove_if(watchers.begin(),watchers.end(),[](auto& watch){return !watch();}),watchers.end());
            drain();
        });timer->start();
    }
    return true;
}
void shutdown_vst3_runtime(){
    if(!qApp)return;
    auto finish=[]{
        QObject* closing=nullptr;
        {std::lock_guard<std::mutex> lock(dispatch_mutex);closing=dispatcher.data();dispatcher=nullptr;}
        delete closing;watchers.clear();pending_watchers.clear();drain();
    };
    if(QThread::currentThread()==qApp->thread())finish();
    else QMetaObject::invokeMethod(qApp,finish,Qt::BlockingQueuedConnection);
}
bool post_vst3_control(std::function<void()> task){
    std::lock_guard<std::mutex> lock(dispatch_mutex);
    return dispatcher&&QMetaObject::invokeMethod(dispatcher,[task=std::move(task)]{task();},Qt::QueuedConnection);
}
void watch_vst3_source(std::function<bool()> watch){
    post_vst3_control([watch=std::move(watch)]()mutable{pending_watchers.push_back(std::move(watch));});
}
void Vst3RackDelete::operator()(LiveRack* rack)const noexcept{
    if(!rack)return;
    if(qApp&&QThread::currentThread()==qApp->thread()&&!vst3::editor_callback_active()){vst3::EditorCallbackGuard deletion;delete rack;return;}
    // OBS has stopped audio for this filter before destroying its context.
    // Keep deletion on the lifecycle thread, even from the OBS destroy queue.
    {std::lock_guard<std::mutex> lock(retired_mutex);retired.push_back(rack);}
    post_vst3_control([]{drain();});
}
Vst3Rack make_vst3_rack(const Format& format,const RackConfig& nodes,bool bypass){
    // An empty bootstrap owns no plugin and can be prepared by OBS workers.
    if(!nodes.empty())require_vst3_control_thread();
    // Adaptive graphs publish their declared latency at audio packet boundaries.
    return Vst3Rack(new LiveRack(format,nodes,bypass,0,vst3::rack_factory(),true,vst3::editor_callback_active));
}
}
