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
#include <cstring>

namespace choc::objc {

inline Class createDelegateClassForWebviewGuiPlugin(const char* baseClass,
                                                     const char* newClassName)
{
    if (baseClass != nullptr && newClassName != nullptr
        && std::strcmp(baseClass, "WKWebView") == 0
        && std::strcmp(newClassName, "CHOCWebView_") == 0)
        return (Class) objc_getClass("WKWebView");

    return createDelegateClass(baseClass, newClassName);
}

} // namespace choc::objc

namespace webview_gui::detail {

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
    // options are disabled by webview-gui's CHOC adapter.
    if (isSystemWKWebViewClass(cls))
        return YES;

    return ::class_addMethod(cls, selector, implementation, types);
}

inline void registerCHOCWebViewClassForPlugin(Class cls)
{
    // WKWebView is already registered by WebKit. Dynamic CHOC delegate classes
    // are still registered normally and are disposed by CHOC.
    if (!isSystemWKWebViewClass(cls))
        ::objc_registerClassPair(cls);
}

} // namespace webview_gui::detail

#define createDelegateClass createDelegateClassForWebviewGuiPlugin
#define class_addMethod webview_gui::detail::addCHOCWebViewMethodForPlugin
#define objc_registerClassPair webview_gui::detail::registerCHOCWebViewClassForPlugin

#include "choc/gui/choc_WebView.h"

#undef objc_registerClassPair
#undef class_addMethod
#undef createDelegateClass

#else

#include "choc/gui/choc_WebView.h"

#endif
