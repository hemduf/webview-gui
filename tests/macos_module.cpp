#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(__APPLE__)
#error macOS-only test module
#endif

#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

void copyClassName(id object, char* output, std::size_t capacity)
{
    if (!output || capacity == 0) return;
    output[0] = '\0';
    if (!object) return;

    auto cls = object_getClass(object);
    const char* name = cls ? class_getName(cls) : nullptr;
    if (!name) return;

    std::strncpy(output, name, capacity - 1);
    output[capacity - 1] = '\0';
}

choc::ui::WebView::Options makePluginOptions()
{
    choc::ui::WebView::Options options;
    options.acceptsFirstMouseClick = false;
    options.enableDefaultClipboardKeyShortcutsInSafari = false;
    options.transparentBackground = true;
    return options;
}

struct RetainedWebViewsState {
    // Keep state captured by bridge callbacks before the view owners so member
    // destruction tears down every callback-owning WebView first.
    std::vector<std::size_t> receivedMessageCounts;
    bool acceptingMessages = false;
    bool unexpectedMessage = false;
    std::size_t drainCompletions = 0;
    bool drainFailed = false;
    std::vector<std::unique_ptr<choc::ui::WebView>> views;
};

void clearRetainedWebViews(RetainedWebViewsState& state)
{
    // Bridge callbacks capture state fields. Destroy the views/callbacks before
    // clearing the storage they refer to.
    state.views.clear();
    state.receivedMessageCounts.clear();
    state.acceptingMessages = false;
    state.unexpectedMessage = false;
    state.drainCompletions = 0;
    state.drainFailed = false;
}

class ScopedHostAttachment {
public:
    explicit ScopedHostAttachment(id childToAttach)
        : child(childToAttach)
    {
        auto nsViewClass = reinterpret_cast<id>(objc_getClass("NSView"));
        if (!nsViewClass || !child)
            return;

        auto allocated = choc::objc::call<id>(nsViewClass, "alloc");
        if (!allocated)
            return;

        parent = choc::objc::call<id>(allocated, "init");
        if (!parent) {
            choc::objc::call<void>(allocated, "release");
            return;
        }

        choc::objc::call<void>(parent, "addSubview:", child);
        if (choc::objc::call<id>(child, "superview") != parent) {
            choc::objc::call<void>(parent, "release");
            parent = nullptr;
        }
    }

    ScopedHostAttachment(const ScopedHostAttachment&) = delete;
    ScopedHostAttachment& operator=(const ScopedHostAttachment&) = delete;

    ~ScopedHostAttachment()
    {
        detach();
        if (parent)
            choc::objc::call<void>(parent, "release");
    }

    [[nodiscard]] bool attached() const noexcept
    {
        return parent != nullptr && child != nullptr;
    }

    bool detach()
    {
        if (!child)
            return parent != nullptr;

        if (parent && choc::objc::call<id>(child, "superview") == parent)
            choc::objc::call<void>(child, "removeFromSuperview");

        return choc::objc::call<id>(child, "superview") == nullptr;
    }

private:
    id parent = nullptr;
    id child = nullptr;
};

bool retainWebViews(RetainedWebViewsState& state,
                    std::size_t count,
                    char* delegateClass,
                    std::size_t delegateCapacity)
{
    clearRetainedWebViews(state);
    auto& views = state.views;
    state.receivedMessageCounts.assign(count, 0);
    views.reserve(count);

    char firstDelegate[256] = {};

    for (std::size_t i = 0; i < count; ++i) {
        auto view = std::make_unique<choc::ui::WebView>(makePluginOptions());
        if (!view->loadedOK() || !view->getViewHandle()) {
            clearRetainedWebViews(state);
            return false;
        }

        auto webview = reinterpret_cast<id>(view->getViewHandle());
        auto webviewClass = object_getClass(webview);
        if (webviewClass != objc_getClass("WKWebView")) {
            clearRetainedWebViews(state);
            return false;
        }

        auto delegate = choc::objc::call<id>(webview, "navigationDelegate");
        char currentDelegate[256] = {};
        copyClassName(delegate, currentDelegate, sizeof(currentDelegate));
        if (currentDelegate[0] == '\0') {
            clearRetainedWebViews(state);
            return false;
        }

        if (i == 0) {
            std::strncpy(firstDelegate, currentDelegate, sizeof(firstDelegate) - 1);
        } else if (std::strcmp(firstDelegate, currentDelegate) != 0) {
            clearRetainedWebViews(state);
            return false;
        }

        // Exercise CHOC's real JS -> native binding path. Each WebView owns its
        // own binding callback so cross-view routing changes a different counter.
        if (!view->bind("__webviewGuiIsolationMessage",
                        [&state, i](const choc::value::ValueView&)
                        {
                            if (i >= state.receivedMessageCounts.size()) {
                                state.unexpectedMessage = true;
                                return choc::value::Value{false};
                            }

                            if (!state.acceptingMessages) {
                                state.unexpectedMessage = true;
                                return choc::value::Value{false};
                            }

                            ++state.receivedMessageCounts[i];
                            return choc::value::Value{true};
                        })) {
            clearRetainedWebViews(state);
            return false;
        }

        views.push_back(std::move(view));
    }

    if (delegateClass && delegateCapacity > 0) {
        std::strncpy(delegateClass, firstDelegate, delegateCapacity - 1);
        delegateClass[delegateCapacity - 1] = '\0';
    }

    return count == 0 || firstDelegate[0] != '\0';
}

bool exerciseRetainedWebViews(RetainedWebViewsState& state, std::size_t passes)
{
    if (state.views.empty() || passes == 0)
        return false;

    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (auto& view : state.views) {
            if (!view || !view->loadedOK() || !view->getViewHandle())
                return false;

            auto webview = reinterpret_cast<id>(view->getViewHandle());

            // Exercise host-visible WKWebView state synchronously so the test
            // proves the still-loaded module is usable after its peer unloads.
            choc::objc::call<void>(webview, "setHidden:", YES);
            if (choc::objc::call<BOOL>(webview, "isHidden") != YES)
                return false;

            choc::objc::call<void>(webview, "setHidden:", NO);
            if (choc::objc::call<BOOL>(webview, "isHidden") != NO)
                return false;
        }
    }

    return true;
}

bool exchangeRetainedMessages(RetainedWebViewsState& state, std::size_t messagesPerView)
{
    if (state.views.empty() || messagesPerView == 0
        || state.receivedMessageCounts.size() != state.views.size()
        || state.unexpectedMessage)
        return false;

    const auto before = state.receivedMessageCounts;
    state.acceptingMessages = true;

    bool allQueued = true;
    for (auto& view : state.views) {
        if (!view || !view->loadedOK() || !view->getViewHandle()) {
            allQueued = false;
            continue;
        }

        for (std::size_t message = 0; message < messagesPerView; ++message) {
            if (!view->evaluateJavascript(
                    "if(typeof window.__webviewGuiIsolationMessage==='function')"
                    "window.__webviewGuiIsolationMessage();"))
                allQueued = false;
        }
    }

    const auto receivedExactlyExpected = [&state, &before, messagesPerView]
    {
        if (state.unexpectedMessage)
            return false;
        for (std::size_t i = 0; i < state.receivedMessageCounts.size(); ++i) {
            if (state.receivedMessageCounts[i] != before[i] + messagesPerView)
                return false;
        }
        return true;
    };

    for (int attempt = 0; attempt < 500 && !receivedExactlyExpected(); ++attempt)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);

    const bool messagesOK = allQueued && receivedExactlyExpected();
    state.acceptingMessages = false;

    // CHOC resolves each native binding call by queueing JavaScript after the
    // native callback returns. Submit one completion-bearing evaluation per
    // view only after all native callbacks have arrived, then wait for those
    // completions as a barrier. This prevents queued response work from crossing
    // release/dlclose even when the message-count assertion has already passed.
    const auto drainTarget = state.drainCompletions + state.views.size();
    state.drainFailed = false;
    bool drainsQueued = true;
    for (auto& view : state.views) {
        if (!view || !view->evaluateJavascript(
                "0",
                [&state](const std::string& error, const choc::value::ValueView&)
                {
                    if (!error.empty())
                        state.drainFailed = true;
                    ++state.drainCompletions;
                }))
            drainsQueued = false;
    }

    for (int attempt = 0;
         attempt < 500 && state.drainCompletions < drainTarget;
         ++attempt)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);

    return messagesOK
        && drainsQueued
        && state.drainCompletions == drainTarget
        && !state.drainFailed
        && !state.unexpectedMessage
        && receivedExactlyExpected();
}

bool exerciseRetainedHostLifecycle(RetainedWebViewsState& state)
{
    if (state.views.empty())
        return false;

    std::vector<std::unique_ptr<ScopedHostAttachment>> attachments;
    attachments.reserve(state.views.size());

    // Keep every parent and child alive simultaneously. This mirrors an audio
    // host with many open editors rather than serially attaching one temporary
    // view at a time.
    for (std::size_t i = 0; i < state.views.size(); ++i) {
        auto& view = state.views[i];
        if (!view || !view->loadedOK() || !view->getViewHandle())
            return false;

        auto webview = reinterpret_cast<id>(view->getViewHandle());
        auto attachment = std::make_unique<ScopedHostAttachment>(webview);
        if (!attachment->attached())
            return false;

        const choc::objc::CGFloat width = static_cast<choc::objc::CGFloat>(320 + (i % 7));
        const choc::objc::CGFloat height = static_cast<choc::objc::CGFloat>(180 + (i % 5));
        const choc::objc::CGRect expectedFrame{{0, 0}, {width, height}};
        choc::objc::call<void>(webview, "setFrame:", expectedFrame);

        const auto actualFrame = choc::objc::call<choc::objc::CGRect>(webview, "frame");
        if (actualFrame.size.width != width || actualFrame.size.height != height)
            return false;

        choc::objc::call<void>(webview, "setHidden:", NO);
        if (choc::objc::call<BOOL>(webview, "isHidden") != NO)
            return false;

        choc::objc::call<void>(webview, "setHidden:", YES);
        if (choc::objc::call<BOOL>(webview, "isHidden") != YES)
            return false;

        choc::objc::call<void>(webview, "setHidden:", NO);
        if (choc::objc::call<BOOL>(webview, "isHidden") != NO)
            return false;

        attachments.push_back(std::move(attachment));
    }

    // Explicitly detach before the WebView owners are cleared by the caller.
    // The RAII destructor repeats this defensively on every early-return path.
    for (auto& attachment : attachments) {
        if (!attachment || !attachment->detach())
            return false;
    }

    return true;
}

} // namespace

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_create_runtime_class_names(
    char* webviewClass,
    std::size_t webviewCapacity,
    char* delegateClass,
    std::size_t delegateCapacity)
{
    choc::ui::WebView view{makePluginOptions()};
    if (!view.loadedOK() || !view.getViewHandle())
        return false;

    auto webview = reinterpret_cast<id>(view.getViewHandle());
    auto delegate = choc::objc::call<id>(webview, "navigationDelegate");

    copyClassName(webview, webviewClass, webviewCapacity);
    copyClassName(delegate, delegateClass, delegateCapacity);
    return webviewClass && webviewClass[0] != '\0'
        && delegateClass && delegateClass[0] != '\0';
}

extern "C" __attribute__((visibility("default"))) void* webview_gui_test_create_retained_state()
{
    return new (std::nothrow) RetainedWebViewsState{};
}

extern "C" __attribute__((visibility("default"))) void webview_gui_test_destroy_retained_state(
    void* opaqueState)
{
    delete static_cast<RetainedWebViewsState*>(opaqueState);
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_retain_webviews(
    void* opaqueState,
    std::size_t count,
    char* delegateClass,
    std::size_t delegateCapacity)
{
    auto* state = static_cast<RetainedWebViewsState*>(opaqueState);
    return state != nullptr
        && retainWebViews(*state, count, delegateClass, delegateCapacity);
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_exercise_retained_webviews(
    void* opaqueState,
    std::size_t passes)
{
    auto* state = static_cast<RetainedWebViewsState*>(opaqueState);
    return state != nullptr
        && exerciseRetainedWebViews(*state, passes);
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_exchange_retained_messages(
    void* opaqueState,
    std::size_t messagesPerView)
{
    auto* state = static_cast<RetainedWebViewsState*>(opaqueState);
    return state != nullptr
        && exchangeRetainedMessages(*state, messagesPerView);
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_exercise_retained_host_lifecycle(
    void* opaqueState)
{
    auto* state = static_cast<RetainedWebViewsState*>(opaqueState);
    return state != nullptr
        && exerciseRetainedHostLifecycle(*state);
}

extern "C" __attribute__((visibility("default"))) void webview_gui_test_release_webviews(
    void* opaqueState)
{
    if (auto* state = static_cast<RetainedWebViewsState*>(opaqueState))
        clearRetainedWebViews(*state);
}
