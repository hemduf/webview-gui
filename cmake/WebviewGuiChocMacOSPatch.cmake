function(webview_gui_apply_choc_macos_lifetime_patch source_file output_file)
    file(READ "${source_file}" WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT)

    set(WEBVIEW_GUI_CHOC_MACOS_OLD_TEARDOWN [=[    ~Pimpl()
    {
        CHOC_AUTORELEASE_BEGIN
        deletionChecker->deleted = true;
        objc_setAssociatedObject (delegate, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);
        objc_setAssociatedObject (webview, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);
        objc::call<void> (webview, "release");
        webview = {};
        objc::call<void> (manager, "removeScriptMessageHandlerForName:", objc::getNSString ("external"));
        objc::call<void> (manager, "release");
        manager = {};
        objc::call<void> (delegate, "release");
        delegate = {};
        CHOC_AUTORELEASE_END
    }]=])

    set(WEBVIEW_GUI_CHOC_MACOS_SAFE_TEARDOWN [=[    ~Pimpl()
    {
        CHOC_AUTORELEASE_BEGIN
        deletionChecker->deleted = true;
        objc_setAssociatedObject (delegate, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);
        objc_setAssociatedObject (webview, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);

        // WKWebView may keep weak delegate pointers and queued navigation work
        // alive until its own teardown. Detach every callback entry point while
        // the plug-in image and its dynamically generated delegate IMPs are
        // still loaded, then stop outstanding work before releasing the view.
        objc::call<void> (webview, "setUIDelegate:", (id) nil);
        objc::call<void> (webview, "setNavigationDelegate:", (id) nil);
        objc::call<void> (manager, "removeScriptMessageHandlerForName:", objc::getNSString ("external"));
        objc::call<void> (webview, "stopLoading");

        objc::call<void> (webview, "release");
        webview = {};
        objc::call<void> (manager, "release");
        manager = {};
        objc::call<void> (delegate, "release");
        delegate = {};
        CHOC_AUTORELEASE_END
    }]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_TEARDOWN}"
        WEBVIEW_GUI_CHOC_MACOS_TEARDOWN_OFFSET)
    if(WEBVIEW_GUI_CHOC_MACOS_TEARDOWN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC macOS WebView teardown changed; refusing to build without revalidating the plug-in unload patch")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_TEARDOWN}"
        "${WEBVIEW_GUI_CHOC_MACOS_SAFE_TEARDOWN}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # CHOC's WebView Pimpl owns a std::shared_ptr<DeletionChecker>. In an
    # unloadable header-only plug-in DSO, libc++'s make_shared control block is
    # a polymorphic template instantiation emitted by that DSO. The macOS ASan
    # multi-module gate has observed the surviving module release a checker whose
    # control-block dispatch/storage was associated with the peer image after
    # dlclose, producing a global-buffer-overflow in __shared_count::__release_shared.
    #
    # The checker only needs shared lifetime across the synchronous native binding
    # callback (so that deleting a WebView from its own callback remains safe).
    # Replace the macOS-only member with a tiny non-polymorphic intrusive holder:
    # copies retain plain heap state, and all retain/release code executes while
    # the owning plug-in image is necessarily resident. This preserves CHOC's
    # deletion-during-callback semantics without a cross-DSO shared_ptr control
    # block. Linux/Windows keep upstream CHOC's shared_ptr implementation.
    set(WEBVIEW_GUI_CHOC_MACOS_OLD_DELETION_CHECKER [=[    static constexpr const char* postMessageFn = "window.webkit.messageHandlers.external.postMessage";

    bool stillInitialising() const  { return false; }
    void* getViewHandle() const     { return (CHOC_OBJC_CAST_BRIDGED void*) webview; }

    std::shared_ptr<DeletionChecker> deletionChecker { std::make_shared<DeletionChecker>() };]=])

    set(WEBVIEW_GUI_CHOC_MACOS_SAFE_DELETION_CHECKER [=[    static constexpr const char* postMessageFn = "window.webkit.messageHandlers.external.postMessage";

    bool stillInitialising() const  { return false; }
    void* getViewHandle() const     { return (CHOC_OBJC_CAST_BRIDGED void*) webview; }

    struct PluginDeletionCheckerRef
    {
        struct Control
        {
            DeletionChecker checker;
            std::size_t references = 1;
        };

        PluginDeletionCheckerRef() : control (new Control()) {}

        PluginDeletionCheckerRef (const PluginDeletionCheckerRef& other) noexcept
            : control (other.control)
        {
            if (control != nullptr)
                ++control->references;
        }

        PluginDeletionCheckerRef& operator= (const PluginDeletionCheckerRef&) = delete;
        PluginDeletionCheckerRef (PluginDeletionCheckerRef&&) = delete;
        PluginDeletionCheckerRef& operator= (PluginDeletionCheckerRef&&) = delete;

        ~PluginDeletionCheckerRef()
        {
            if (control != nullptr && --control->references == 0)
                delete control;
        }

        DeletionChecker* operator->() const noexcept
        {
            return control != nullptr ? std::addressof (control->checker) : nullptr;
        }

        Control* control = nullptr;
    };

    PluginDeletionCheckerRef deletionChecker;]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_DELETION_CHECKER}"
        WEBVIEW_GUI_CHOC_MACOS_DELETION_CHECKER_OFFSET)
    if(WEBVIEW_GUI_CHOC_MACOS_DELETION_CHECKER_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC macOS deletion checker changed; refusing to build without revalidating the plug-in unload patch")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_DELETION_CHECKER}"
        "${WEBVIEW_GUI_CHOC_MACOS_SAFE_DELETION_CHECKER}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # Objective-C associated-object keys are pointer identities. A string literal
    # is unsuitable for independently loaded plug-in DSOs because literal storage
    # may be coalesced across images. Inject one internal-linkage byte into the
    # private CHOC implementation and use its address for every association. The
    # associations are cleared while the module is resident, before dlclose.
    set(WEBVIEW_GUI_CHOC_MACOS_OLD_APPLE_PREAMBLE [=[#elif CHOC_APPLE

#include "../platform/choc_ObjectiveCHelpers.h"

struct choc::ui::WebView::Pimpl]=])
    set(WEBVIEW_GUI_CHOC_MACOS_SAFE_APPLE_PREAMBLE [=[#elif CHOC_APPLE

#include "../platform/choc_ObjectiveCHelpers.h"

static char webviewGuiChocAssociatedObjectKey;

struct choc::ui::WebView::Pimpl]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_APPLE_PREAMBLE}"
        WEBVIEW_GUI_CHOC_MACOS_APPLE_PREAMBLE_OFFSET)
    if(WEBVIEW_GUI_CHOC_MACOS_APPLE_PREAMBLE_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC macOS implementation preamble changed; refusing to inject the plug-in-local associated-object key")
    endif()

    string(REGEX MATCHALL "\"choc_webview\""
        WEBVIEW_GUI_CHOC_MACOS_STRING_ASSOCIATION_KEYS
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    list(LENGTH WEBVIEW_GUI_CHOC_MACOS_STRING_ASSOCIATION_KEYS
        WEBVIEW_GUI_CHOC_MACOS_STRING_ASSOCIATION_KEY_COUNT)
    if(NOT WEBVIEW_GUI_CHOC_MACOS_STRING_ASSOCIATION_KEY_COUNT EQUAL 5)
        message(FATAL_ERROR
            "Pinned CHOC macOS associated-object usage changed; expected exactly five choc_webview key references")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_APPLE_PREAMBLE}"
        "${WEBVIEW_GUI_CHOC_MACOS_SAFE_APPLE_PREAMBLE}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    string(REPLACE
        "\"choc_webview\""
        "&webviewGuiChocAssociatedObjectKey"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # Keep the source-generation contract fail-closed: no string-literal key may
    # survive in the private macOS CHOC copy after patching.
    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "\"choc_webview\""
        WEBVIEW_GUI_CHOC_MACOS_STRING_ASSOCIATION_KEY_OFFSET)
    if(NOT WEBVIEW_GUI_CHOC_MACOS_STRING_ASSOCIATION_KEY_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Plug-in-safe macOS CHOC still uses the string-literal associated-object key choc_webview")
    endif()

    file(WRITE "${output_file}" "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
endfunction()
