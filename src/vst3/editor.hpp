#pragma once
#include <memory>
#include <string>
namespace Steinberg {namespace Vst {class IEditController;}}
namespace nexus::vst3 {
// UI/control-thread lifetime; owns the native parent and the original view.
class EditorWindow {
public:
    EditorWindow(Steinberg::Vst::IEditController*,const std::string&,bool visible);
    ~EditorWindow();
    void show();
    void* handle()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
