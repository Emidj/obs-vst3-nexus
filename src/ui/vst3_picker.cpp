#include "i18n/strings.hpp"
#include "ui/vst3_picker.hpp"
#include <QCloseEvent>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <thread>
#include <algorithm>
namespace nexus::ui {
struct Vst3Picker::Job{
    std::atomic<bool> cancel{false},complete{false};
    std::atomic<size_t> total{0},finished{0};
    std::vector<vst3::ClassInfo> effects;
    std::vector<std::string> errors;
    std::thread thread;
    ~Job(){if(thread.joinable())thread.join();}
};
std::vector<std::shared_ptr<Vst3Picker::Job>>& Vst3Picker::jobs(){
    static std::vector<std::shared_ptr<Job>> registry;return registry;
}
void Vst3Picker::shutdown_jobs(){
    for(const auto& j:jobs())j->cancel.store(true);
    for(const auto& j:jobs())if(j->thread.joinable())j->thread.join();
    jobs().clear();
}
Vst3Picker::Vst3Picker(std::filesystem::path worker,std::filesystem::path cache,QWidget* parent)
    :QDialog(parent),worker_(std::move(worker)),cache_(std::move(cache)){
    setObjectName("nexus_vst3_picker");setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(nexus::i18n::text("Add VST3 Effect"));resize(620,520);
    auto* layout=new QVBoxLayout(this);
    auto* hint=new QLabel(nexus::i18n::text("Select a 64-bit VST3 audio effect. Instruments are not supported."),this);
    hint->setWordWrap(true);layout->addWidget(hint);
    search_=new QLineEdit(this);search_->setObjectName("vst3_search");search_->setPlaceholderText(nexus::i18n::text("Search effects"));layout->addWidget(search_);
    auto* folderRow=new QHBoxLayout;
    folder_=new QLineEdit(this);folder_->setObjectName("vst3_folder");folder_->setPlaceholderText(nexus::i18n::text("Optional VST3 folder"));
    folderRow->addWidget(folder_,1);auto* browse=new QPushButton(nexus::i18n::text("Browse..."),this);folderRow->addWidget(browse);layout->addLayout(folderRow);
    connect(browse,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getExistingDirectory(this,nexus::i18n::text("VST3 effects folder"));
        if(!path.isEmpty())folder_->setText(path);
    });
    list_=new QListWidget(this);list_->setObjectName("vst3_effects");layout->addWidget(list_,1);
    status_=new QLabel(nexus::i18n::text("Choose a scan location."),this);status_->setObjectName("vst3_scan_status");
    status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    auto* buttons=new QHBoxLayout;
    auto* installed=new QPushButton(nexus::i18n::text("Scan installed"),this);buttons->addWidget(installed);
    auto* folder=new QPushButton(nexus::i18n::text("Scan folder"),this);folder->setObjectName("vst3_scan_folder");buttons->addWidget(folder);
    auto* retry=new QPushButton(nexus::i18n::text("Retry scan"),this);buttons->addWidget(retry);
    buttons->addStretch();add_=new QPushButton(nexus::i18n::text("Add"),this);add_->setObjectName("vst3_add");add_->setEnabled(false);buttons->addWidget(add_);
    auto* cancel=new QPushButton(nexus::i18n::text("Cancel"),this);buttons->addWidget(cancel);layout->addLayout(buttons);
    connect(installed,&QPushButton::clicked,this,[this]{start_scan();});
    connect(folder,&QPushButton::clicked,this,[this]{
        const auto path=folder_->text().trimmed();
        if(!path.isEmpty())start_scan({std::filesystem::u8path(path.toStdString())});
        else status_->setText(nexus::i18n::text("Choose a folder or use Scan installed."));
    });
    connect(retry,&QPushButton::clicked,this,[this]{start_scan(roots_,true);});
    connect(cancel,&QPushButton::clicked,this,&QDialog::close);
    connect(search_,&QLineEdit::textChanged,this,[this]{filter();});
    connect(list_,&QListWidget::currentRowChanged,this,[this]{filter();});
    connect(list_,&QListWidget::itemDoubleClicked,this,[this]{select();});
    connect(add_,&QPushButton::clicked,this,[this]{select();});
    auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this]{poll();});timer->start(40);
}
Vst3Picker::~Vst3Picker(){if(job_)job_->cancel.store(true);}
void Vst3Picker::closeEvent(QCloseEvent* event){if(job_)job_->cancel.store(true);QDialog::closeEvent(event);}
bool Vst3Picker::scanning()const noexcept{return job_&&!job_->complete.load(std::memory_order_acquire);}
void Vst3Picker::start_scan(std::vector<std::filesystem::path> roots,bool retry){
    if(scanning()){job_->cancel.store(true);status_->setText(nexus::i18n::text("Stopping scan. Retry when it finishes."));return;}
    auto& registry=jobs();
    for(const auto& j:registry)if(j->complete.load(std::memory_order_acquire)&&j->thread.joinable())j->thread.join();
    registry.erase(std::remove_if(registry.begin(),registry.end(),[](const auto& j){return j->complete.load(std::memory_order_acquire);}),registry.end());
    if(registry.size()>=4){status_->setText(nexus::i18n::text("Other scans are finishing. Please retry."));return;}
    std::error_code file_error;
    if(!std::filesystem::is_regular_file(worker_,file_error)){list_->clear();effects_.clear();add_->setEnabled(false);status_->setText(nexus::i18n::text("VST3 scan helper is missing."));return;}
    roots_=roots;effects_.clear();list_->clear();add_->setEnabled(false);
    auto job=std::make_shared<Job>();job_=job;registry.push_back(job);
    const auto worker=worker_,cache=cache_;
    status_->setText(nexus::i18n::text("Finding VST3 effects..."));
    try{job->thread=std::thread([job,roots=std::move(roots),worker,cache,retry]{
        auto error=[&](std::string text){if(job->errors.size()<32)job->errors.push_back(std::move(text));};
        try{
            const auto discovered=vst3::discover_vst3(roots.empty()?vst3::standard_vst3_roots():roots);
            for(const auto& warning:discovered.warnings)error(warning);
            if(discovered.truncated)error(nexus::i18n::text("Folder discovery limit reached."));
            job->total.store(discovered.modules.size());
            vst3::CatalogScanner scanner(worker,cache);
            for(const auto& module:discovered.modules){
                if(job->cancel.load())break;
                const auto result=scanner.scan(module,retry,&job->cancel);
                if(result.status==vst3::ScanStatus::ready){
                    for(const auto& item:result.classes)if(item.effect){
                        if(job->effects.size()==4096){error(nexus::i18n::text("Effect list limit reached."));break;}
                        job->effects.push_back(item);
                    }
                }else if(result.status!=vst3::ScanStatus::cancelled)
                    error(module.filename().u8string()+": "+vst3::scan_status_name(result.status)+" "+result.detail);
                job->finished.fetch_add(1);
                if(job->effects.size()>=4096)break;
            }
        }catch(const std::exception& e){error(e.what());}
        std::sort(job->effects.begin(),job->effects.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        job->complete.store(true,std::memory_order_release);
    });}catch(const std::exception& e){job->errors.push_back(e.what());job->complete.store(true,std::memory_order_release);}
}
void Vst3Picker::poll(){
    if(!job_)return;
    if(!job_->complete.load(std::memory_order_acquire)){
        status_->setText(QString(nexus::i18n::text("Scanning %1 / %2...")).arg(qulonglong(job_->finished.load())).arg(qulonglong(job_->total.load())));return;
    }
    effects_=job_->effects;list_->clear();
    for(size_t i=0;i<effects_.size();++i){
        const auto& e=effects_[i];auto* item=new QListWidgetItem(QString::fromStdString(e.name+" | "+e.vendor),list_);
        item->setData(Qt::UserRole,qulonglong(i));item->setToolTip(QString::fromStdString(e.path));
    }
    QString text=QString(nexus::i18n::text("Found %1 effect candidates. Compatibility is checked when adding.")).arg(qulonglong(effects_.size()));
    if(job_->cancel.load())text=nexus::i18n::text("Scan cancelled. ")+text;
    if(!job_->errors.empty()){
        text+=(QString("\n")+QString::fromUtf8(nexus::i18n::text("%1 scan issue(s)."))).arg(qulonglong(job_->errors.size()));
        for(size_t i=0;i<std::min(size_t(3),job_->errors.size());++i)text+="\n"+QString::fromStdString(job_->errors[i]).left(300);
    }
    status_->setText(text);job_.reset();filter();
}
void Vst3Picker::filter(){
    for(int i=0;i<list_->count();++i)list_->item(i)->setHidden(!list_->item(i)->text().contains(search_->text(),Qt::CaseInsensitive));
    auto* current=list_->currentItem();add_->setEnabled(current&&!current->isHidden()&&!scanning());
}
void Vst3Picker::select(){
    auto* item=list_->currentItem();if(!item||item->isHidden()||scanning())return;
    const auto index=item->data(Qt::UserRole).toULongLong();if(index>=effects_.size())return;
    const auto selected=effects_[size_t(index)];
    try{if(on_selected)on_selected(selected);}
    catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));return;}
    close();
}
}
