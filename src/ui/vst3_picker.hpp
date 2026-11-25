#pragma once
#include "vst3/catalog.hpp"
#include <QDialog>
#include <memory>
#include <functional>
class QListWidget;class QLineEdit;class QLabel;class QPushButton;
namespace nexus::ui {
class Vst3Picker final:public QDialog {
public:
    Vst3Picker(std::filesystem::path worker,std::filesystem::path cache,QWidget* parent=nullptr);
    ~Vst3Picker()override;
    void start_scan(std::vector<std::filesystem::path> roots={},bool retry=false);
    bool scanning()const noexcept;
    std::function<void(const vst3::ClassInfo&)> on_selected;
private:
    struct Job;
    void poll();
    void filter();
    void select();
    void closeEvent(QCloseEvent*)override;
    std::filesystem::path worker_,cache_;
    std::vector<std::filesystem::path> roots_;
    std::shared_ptr<Job> job_;
    std::vector<vst3::ClassInfo> effects_;
    QListWidget* list_;QLineEdit* search_;QLineEdit* folder_;QLabel* status_;QPushButton* add_;
    static std::vector<std::shared_ptr<Job>>& jobs();
public:
    static void shutdown_jobs();
};
}
