#pragma once

#include "choc/platform/choc_Platform.h"
#include "../plugin_support.h"
#include "../origin_policy.h"
#include "../resource_security.h"

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
// but their names are stable per module and their IMPs are neutralised before
// unload, so the runtime never keeps pointers into an unloaded plug-in image.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <dlfcn.h>

#include "choc/platform/choc_ObjectiveCHelpers.h"

namespace webview_gui::detail {

static void chocPluginModuleAnchor() {}

inline std::uint64_t chocPluginStableModuleToken()
{
    Dl_info info{};
    const auto anchor = reinterpret_cast<const void*>(&chocPluginModuleAnchor);
    const char* path = nullptr;

    if (dladdr(anchor, &info) != 0)
        path = info.dli_fname;

    if (path == nullptr)
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(anchor));

    std::uint64_t hash = 14695981039346656037ull;
    for (auto p = reinterpret_cast<const unsigned char*>(path); *p != 0; ++p) {
        hash ^= static_cast<std::uint64_t>(*p);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline const void* chocPluginAssociatedObjectKey()
{
    static const unsigned char key = 0;
    return &key;
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

inline Method findOwnCHOCWebViewMethod(Class cls, SEL selector)
{
    unsigned int count = 0;
    Method* methods = class_copyMethodList(cls, &count);
    Method found = nullptr;

    for (unsigned int i = 0; i < count; ++i) {
        if (method_getName(methods[i]) == selector) {
            found = methods[i];
            break;
        }
    }

    std::free(methods);
    return found;
}

inline BOOL addCHOCWebViewMethodForPlugin(Class cls,
                                          SEL selector,
                                          IMP implementation,
                                          const char* types)
{
    if (isSystemWKWebViewClass(cls))
        return YES;

    if (auto method = findOwnCHOCWebViewMethod(cls, selector)) {
        method_setImplementation(method, implementation);
        return YES;
    }

    return ::class_addMethod(cls, selector, implementation, types);
}

using NavigationDecisionHandler = void (^)(long);

inline void installPluginNavigationPolicy(Class cls)
{
    if (!isCHOCWebViewDelegateClass(cls)) return;

    const auto selector = sel_registerName("webView:decidePolicyForNavigationAction:decisionHandler:");

    addCHOCWebViewMethodForPlugin(
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

            decisionHandler(isTrustedAppleLinuxPluginURL(absolute) ? 1L : 0L);
        }),
        "v@:@@@@"
    );
}

inline void registerCHOCWebViewClassForPlugin(Class cls)
{
    if (!isSystemWKWebViewClass(cls)) {
        installPluginNavigationPolicy(cls);

        const char* name = class_getName(cls);
        if (name != nullptr && objc_getClass(name) != cls)
            ::objc_registerClassPair(cls);
    }
}

inline IMP processResidentFallbackIMP()
{
    auto nsObject = (Class) objc_getClass("NSObject");
    auto method = class_getInstanceMethod(nsObject, sel_registerName("class"));
    return method != nullptr ? method_getImplementation(method) : nullptr;
}

inline void disposeCHOCWebViewClassForPlugin(Class cls)
{
    if (!isCHOCWebViewDelegateClass(cls)) {
        ::objc_disposeClassPair(cls);
        return;
    }

    const auto fallback = processResidentFallbackIMP();
    if (fallback == nullptr)
        return;

    unsigned int count = 0;
    Method* methods = class_copyMethodList(cls, &count);
    for (unsigned int i = 0; i < count; ++i)
        method_setImplementation(methods[i], fallback);
    std::free(methods);
}

inline void setCHOCWebViewAssociatedObjectForPlugin(id object,
                                                     const void*,
                                                     id value,
                                                     objc_AssociationPolicy policy)
{
    ::objc_setAssociatedObject(object, chocPluginAssociatedObjectKey(), value, policy);
}

inline id getCHOCWebViewAssociatedObjectForPlugin(id object, const void*)
{
    return ::objc_getAssociatedObject(object, chocPluginAssociatedObjectKey());
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

    char className[256] = {};
    const auto moduleToken = webview_gui::detail::chocPluginStableModuleToken();
    const auto expectedBase = (Class) objc_getClass(baseClass);

    for (unsigned int attempt = 0; attempt < 64; ++attempt) {
        std::snprintf(className, sizeof(className), "%sWG_%llx_%u",
                      newClassName,
                      static_cast<unsigned long long>(moduleToken),
                      attempt);

        if (auto existing = (Class) objc_getClass(className)) {
            if (class_getSuperclass(existing) == expectedBase)
                return existing;
            continue;
        }

        if (auto cls = objc_allocateClassPair(expectedBase, className, 0))
            return cls;
    }

    return nullptr;
}

inline id getPluginSafeNSStringForWebviewGui(const char* value)
{
    if (value != nullptr && std::strcmp(value, "*") == 0)
        return getNSString(webview_gui::detail::appleLinuxPluginResourceAllowOrigin.data());

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
#define objc_disposeClassPair webview_gui::detail::disposeCHOCWebViewClassForPlugin
#define objc_setAssociatedObject webview_gui::detail::setCHOCWebViewAssociatedObjectForPlugin
#define objc_getAssociatedObject webview_gui::detail::getCHOCWebViewAssociatedObjectForPlugin
#define getNSString getPluginSafeNSStringForWebviewGui

#include "choc/gui/choc_WebView.h"

#undef getNSString
#undef objc_getAssociatedObject
#undef objc_setAssociatedObject
#undef objc_disposeClassPair
#undef objc_registerClassPair
#undef class_addMethod
#undef createDelegateClass

#elif CHOC_WINDOWS

#include "./windows_plugin_runtime.h"

#ifdef GetModuleHandle
#undef GetModuleHandle
#endif
#define GetModuleHandle(...) webview_gui::detail::windowsPluginModuleHandle()
#define GetTickCount() webview_gui::detail::nextWindowsClassToken()
#include "choc/gui/choc_DesktopWindow.h"
#undef GetTickCount
#undef GetModuleHandle

#define CoInitialize(...) webview_gui::detail::suppressedCHOCWebViewCoInitialize(nullptr)
#include "choc/gui/choc_WebView.h"
#undef CoInitialize

#ifndef WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD
#error webview-gui requires its generated plugin-safe CHOC WebView2 bridge patch on Windows
#endif
#undef WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD

#elif CHOC_LINUX

// CHOC's general-purpose custom-scheme backend emits wildcard CORS. Interpose
// only that header-level Soup call in the private plugin copy and narrow it to
// the exact local origin. libsoup has already declared the real function before
// the macro is enabled, avoiding changes to any system header declarations.
#include <libsoup/soup.h>
#include <cstring>

namespace webview_gui::detail {

inline void appendPluginSafeSoupHeader(SoupMessageHeaders* headers,
                                       const char* name,
                                       const char* value)
{
    if (name != nullptr && value != nullptr
        && std::strcmp(name, "Access-Control-Allow-Origin") == 0
        && std::strcmp(value, "*") == 0)
        value = appleLinuxPluginResourceAllowOrigin.data();

    ::soup_message_headers_append(headers, name, value);
}

} // namespace webview_gui::detail

#define soup_message_headers_append webview_gui::detail::appendPluginSafeSoupHeader
#include "choc/gui/choc_WebView.h"
#undef soup_message_headers_append

#else

#include "choc/gui/choc_WebView.h"

#endif
