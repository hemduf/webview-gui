#pragma once

#include "choc/platform/choc_Platform.h"

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

#include "choc/platform/choc_ObjectiveCHelpers.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace webview_gui::detail {

// Internal-linkage anchor. Every translation unit gets an anchor, but dladdr()
// resolves all of them to the same Mach-O image base for a given plug-in module.
static void chocPluginModuleAnchor() {}

inline const void* chocPluginModuleIdentity()
{
    Dl_info info{};
    auto anchor = reinterpret_cast<const void*>(&chocPluginModuleAnchor);
    if (dladdr(anchor, &info) != 0 && info.dli_fbase != nullptr)
        return info.dli_fbase;

    // Fallback still gives this translation unit a unique address. The normal
    // macOS plug-in path should always resolve dli_fbase.
    return anchor;
}

inline bool isSystemWKWebViewClass(Class cls)
{
    return cls != nullptr && cls == (Class) objc_getClass("WKWebView");
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

inline void registerCHOCWebViewClassForPlugin(Class cls)
{
    // WKWebView is already registered by WebKit. Dynamic CHOC delegate classes
    // are registered normally and disposed by CHOC.
    if (!isSystemWKWebViewClass(cls))
        ::objc_registerClassPair(cls);
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

    // CHOC's upstream helper uses a truncated timestamp. For a plug-in process,
    // derive the name from the Mach-O image base plus a local monotonic counter.
    // Even if this header is instantiated in multiple translation units, a name
    // collision simply causes objc_allocateClassPair() to fail and the loop tries
    // the next serial.
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

} // namespace choc::objc

#define createDelegateClass createDelegateClassForWebviewGuiPlugin
#define class_addMethod webview_gui::detail::addCHOCWebViewMethodForPlugin
#define objc_registerClassPair webview_gui::detail::registerCHOCWebViewClassForPlugin
#define objc_setAssociatedObject webview_gui::detail::setCHOCWebViewAssociatedObjectForPlugin
#define objc_getAssociatedObject webview_gui::detail::getCHOCWebViewAssociatedObjectForPlugin

#include "choc/gui/choc_WebView.h"

#undef objc_getAssociatedObject
#undef objc_setAssociatedObject
#undef objc_registerClassPair
#undef class_addMethod
#undef createDelegateClass

#else

#include "choc/gui/choc_WebView.h"

#endif
