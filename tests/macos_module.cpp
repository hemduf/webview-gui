#include "webview-gui/webview-gui.h"

#if !defined(__APPLE__)
#error macOS-only test module
#endif

#include <cstring>
#include <string>

extern "C" __attribute__((visibility("default"))) const char* webview_gui_test_runtime_class_name(const char* role)
{
    static thread_local std::string name;
    name = webview_gui::_objc::runtimeClassName(role ? role : "Unknown");
    return name.c_str();
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_register_runtime_classes()
{
    auto messageClass = webview_gui::WebviewGui::Impl::createMessageHandlerClass();
    auto schemeClass = webview_gui::WebviewGui::Impl::createSchemeHandlerClass();
    return messageClass != nullptr && schemeClass != nullptr;
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_runtime_classes_match_module()
{
    const auto messageName = webview_gui::_objc::runtimeClassName("WKScriptMessageHandler");
    const auto schemeName = webview_gui::_objc::runtimeClassName("WKURLSchemeHandler");
    return objc_getClass(messageName.c_str()) != nullptr
        && objc_getClass(schemeName.c_str()) != nullptr;
}
