#if defined(_WIN32)
#define WEBVIEW_GUI_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define WEBVIEW_GUI_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Deliberately leaky fixture used to prove that the export verifier rejects
// every repository-owned symbol outside the single plug-in ABI entry point.
WEBVIEW_GUI_TEST_EXPORT int webview_gui_plugin_test_entry()
{
    return 0;
}

WEBVIEW_GUI_TEST_EXPORT int unrelated_plugin_helper()
{
    return 42;
}
