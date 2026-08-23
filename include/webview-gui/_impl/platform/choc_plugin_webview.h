#pragma once

#include "choc/platform/choc_Platform.h"
#include "../plugin_support.h"

#if CHOC_APPLE

// CHOC's macOS WebView normally creates a process-global Objective-C subclass
// of WKWebView. CHOC intentionally leaves that class registered because WebKit
// KVO may still reference it during process shutdown. That is acceptable for an
// application, but not for an unloadable audio plug-in module because the class
// methods (IMPs) are emitted into the plug-in image.
//
// For webview-gui's plug-in-safe profile we disable the two features implemented
// by that subclass (acceptsFirstMouse: and performKeyEquivalent:) and arrange for
// CHOC to use the system WKWebView class instead. Delegate classes remain dynamic,
// but CHOC disposes those during module teardown after all WebViews are destroyed.

// choc_ObjectiveCHelpers.h is normally included from choc_WebView.h after its
// standard-library prerequisites. We intentionally include it early so we can
// wrap a few runtime calls, therefore provide those prerequisites here.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <dlfcn.h>

#include "choc/platform/choc_ObjectiveCHelpers.h"

namespace webview_gui::detail {

static void chocPluginModuleAnchor() {}

inline const void* chocPluginModuleIdentity()
{
    Dl_info info{};
    auto anchor = reinterpret_cast<const void*>(&chocPluginModuleAnchor);
    if (dladdr(anchor, &info) != 0 && info.dli_fbase != nullptr)
        return info.dli_fbase;

    return anchor;
}

inline bool isSystemWKWebViewClass(Class cls)
{
    return cls != nullptr && cls == (Class) objc_getClass("WKWebView");
}

inline bool isCHOCWebViewDelegateClass(Class cls)
{
    if (!cls) return false;
    const char* name = class_getName(cls);
    return name != nullptr
        && std::strncmp(name, "CHOCWebViewDelegate_", std::strlen("CHOCWebViewDelegate_")) == 0;
}

inline BOOL addCHOCWebViewMethodForPlugin(Class cls,
                                          SEL selector,
                                          IMP implementation,
                                          const char* types)
{
    // Do not mutate Apple's process-global WKWebView class. The corresponding
    // CHOC options are disabled by webview-gui's adapter.
    if (isSystemWKWebViewClass(cls))
        return YES;

    return ::class_addMethod(cls, selector, implementation, types);
}

using NavigationDecisionHandler = void (^)(long);

inline void installPluginNavigationPolicy(Class cls)
{
    if (!isCHOCWebViewDelegateClass(cls)) return;

    const auto selector = sel_registerName("webView:decidePolicyForNavigationAction:decisionHandler:");
    if (class_getInstanceMethod(cls, selector) != nullptr) return;

    ::class_addMethod(
        cls,
        selector,
        (IMP) (+[](id, SEL, id, id navigationAction, NavigationDecisionHandler decisionHandler)
        {
            if (!decisionHandler) return;

            using namespace choc::objc;
            const id request = navigationAction ? call<id>(navigationAction, "request") : nil;
            const id url = request ? call<id>(request, "URL") : nil;
            const id absoluteString = url ? call<id>(url, "absoluteString") : nil;
            const auto absolute = absoluteString ? getString(absoluteString) : std::string{};

            // WKNavigationActionPolicyCancel = 0, WKNavigationActionPolicyAllow = 1.
            decisionHandler(isTrustedPluginURL(absolute) ? 1L : 0L);
        }),
        "v@:@@@?"
    );
}

inline void registerCHOCWebViewClassForPlugin(Class cls)
{
    // WKWebView is already registered by WebKit. Dynamic CHOC delegate classes
    // are registered normally, gain the strict local-navigation policy, and are
    // disposed by CHOC when the module shuts down.
    if (!isSystemWKWebViewClass(cls)) {
        installPluginNavigationPolicy(cls);
        ::objc_registerClassPair(cls);
    }
}

inline void setCHOCWebViewAssociatedObjectForPlugin(id object,
                                                     const void*,
                                                     id value,
                                                     objc_AssociationPolicy policy)
{
    ::objc_setAssociatedObject(object, chocPluginModuleIdentity(), value, policy);
}

inline id getCHOCWebViewAssociatedObjectForPlugin(id object, const void*)
{
    return ::objc_getAssociatedObject(object, chocPluginModuleIdentity());
}

} // namespace webview_gui::detail

namespace choc::objc {

inline Class createDelegateClassForWebviewGuiPlugin(const char* baseClass,
                                                     const char* newClassName)
{
    if (baseClass != nullptr && newClassName != nullptr
        && std::strcmp(baseClass, "WKWebView") == 0
        && std::strcmp(newClassName, "CHOCWebView_") == 0)
        return (Class) objc_getClass("WKWebView");

    if (baseClass == nullptr || newClassName == nullptr)
        return nullptr;

    static std::atomic<unsigned long long> counter{0};
    char className[256] = {};
    const auto moduleToken = reinterpret_cast<std::uintptr_t>(webview_gui::detail::chocPluginModuleIdentity());

    for (unsigned int attempt = 0; attempt < 64; ++attempt) {
        const auto serial = counter.fetch_add(1, std::memory_order_relaxed);
        std::snprintf(className, sizeof(className), "%sWG_%llx_%llu",
                      newClassName,
                      static_cast<unsigned long long>(moduleToken),
                      serial);

        if (auto cls = objc_allocateClassPair(objc_getClass(baseClass), className, 0))
            return cls;
    }

    return nullptr;
}

inline id getPluginSafeNSStringForWebviewGui(const char* value)
{
    // CHOC's general-purpose WebView sends Access-Control-Allow-Origin: *.
    // In the plug-in profile, narrow that wildcard to the one trusted origin.
    if (value != nullptr && std::strcmp(value, "*") == 0)
        return getNSString("choc://choc.choc");

    return getNSString(value);
}

inline id getPluginSafeNSStringForWebviewGui(const std::string& value)
{
    return getPluginSafeNSStringForWebviewGui(value.c_str());
}

} // namespace choc::objc

#define createDelegateClass createDelegateClassForWebviewGuiPlugin
#define class_addMethod webview_gui::detail::addCHOCWebViewMethodForPlugin
#define objc_registerClassPair webview_gui::detail::registerCHOCWebViewClassForPlugin
#define objc_setAssociatedObject webview_gui::detail::setCHOCWebViewAssociatedObjectForPlugin
#define objc_getAssociatedObject webview_gui::detail::getCHOCWebViewAssociatedObjectForPlugin
#define getNSString getPluginSafeNSStringForWebviewGui

#include "choc/gui/choc_WebView.h"

#undef getNSString
#undef objc_getAssociatedObject
#undef objc_setAssociatedObject
#undef objc_registerClassPair
#undef class_addMethod
#undef createDelegateClass

#else

#include "choc/gui/choc_WebView.h"

#endif
