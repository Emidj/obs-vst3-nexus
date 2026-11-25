#pragma once
#include <memory>
#include <cstddef>
namespace Steinberg {class IPlugView;}
namespace nexus::vst3 {
bool editor_callback_active()noexcept;
class EditorCallbackGuard {
public:EditorCallbackGuard()noexcept;~EditorCallbackGuard();
 EditorCallbackGuard(const EditorCallbackGuard&)=delete;
 EditorCallbackGuard& operator=(const EditorCallbackGuard&)=delete;
};
// A hook on the owning UI thread only, limited to the focused editor subtree.
class EditorKeyboard {
public:
 EditorKeyboard(void* window,Steinberg::IPlugView* view);
 ~EditorKeyboard();
 void focus(bool);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
size_t editor_keyboard_routes()noexcept;
}
