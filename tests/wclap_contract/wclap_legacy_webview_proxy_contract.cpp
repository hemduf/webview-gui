#include "webview-gui/wclap-legacy-webview-proxy.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

struct FakeState {
    const clap_plugin_t *expectedPlugin = nullptr;
    bool initialized = false;
    bool destroyed = false;
    std::uint32_t pointerIdentityFailures = 0;
    std::uint32_t strictPreinitQueries = 0;
    std::uint32_t realWebviewQueries = 0;
    std::uint32_t activateCalls = 0;
    std::uint32_t deactivateCalls = 0;
    std::uint32_t startCalls = 0;
    std::uint32_t stopCalls = 0;
    std::uint32_t resetCalls = 0;
    std::uint32_t processCalls = 0;
    std::uint32_t mainThreadCalls = 0;
    std::uint32_t uriCalls = 0;
    std::uint32_t resourceCalls = 0;
    std::uint32_t receiveCalls = 0;
};

FakeState *stateFor(const clap_plugin_t *plugin) noexcept {
    return plugin ? static_cast<FakeState *>(plugin->plugin_data) : nullptr;
}

FakeState *checkedStateFor(const clap_plugin_t *plugin) noexcept {
    auto *state = stateFor(plugin);
    if (state && plugin != state->expectedPlugin)
        ++state->pointerIdentityFailures;
    return state;
}

int32_t CLAP_ABI realGetUri(const clap_plugin_t *plugin,
                            char *uri,
                            std::uint32_t capacity) {
    auto *state = checkedStateFor(plugin);
    if (!state || !state->initialized)
        return -1;
    ++state->uriCalls;
    constexpr char kUri[] = "/index.html";
    if (capacity == 0)
        return static_cast<int32_t>(sizeof(kUri));
    if (!uri || capacity < sizeof(kUri))
        return -1;
    std::memcpy(uri, kUri, sizeof(kUri));
    return static_cast<int32_t>(sizeof(kUri));
}

bool CLAP_ABI realGetResource(const clap_plugin_t *plugin,
                              const char *,
                              char *,
                              std::uint32_t,
                              const clap_ostream_t *) {
    auto *state = checkedStateFor(plugin);
    if (!state || !state->initialized)
        return false;
    ++state->resourceCalls;
    return true;
}

bool CLAP_ABI realReceive(const clap_plugin_t *plugin,
                          const void *,
                          std::uint32_t) {
    auto *state = checkedStateFor(plugin);
    if (!state || !state->initialized)
        return false;
    ++state->receiveCalls;
    return true;
}

const webview_gui::clap_plugin_webview kRealWebview{
    realGetUri,
    realGetResource,
    realReceive,
};

bool CLAP_ABI fakeInit(const clap_plugin_t *plugin) {
    auto *state = checkedStateFor(plugin);
    if (!state || state->initialized)
        return false;
    state->initialized = true;
    return true;
}

void CLAP_ABI fakeDestroy(const clap_plugin_t *plugin) {
    if (auto *state = checkedStateFor(plugin))
        state->destroyed = true;
}

bool CLAP_ABI fakeActivate(const clap_plugin_t *plugin,
                           double,
                           std::uint32_t,
                           std::uint32_t) {
    auto *state = checkedStateFor(plugin);
    if (!state)
        return false;
    ++state->activateCalls;
    return true;
}

void CLAP_ABI fakeDeactivate(const clap_plugin_t *plugin) {
    if (auto *state = checkedStateFor(plugin))
        ++state->deactivateCalls;
}

bool CLAP_ABI fakeStartProcessing(const clap_plugin_t *plugin) {
    auto *state = checkedStateFor(plugin);
    if (!state)
        return false;
    ++state->startCalls;
    return true;
}

void CLAP_ABI fakeStopProcessing(const clap_plugin_t *plugin) {
    if (auto *state = checkedStateFor(plugin))
        ++state->stopCalls;
}

void CLAP_ABI fakeReset(const clap_plugin_t *plugin) {
    if (auto *state = checkedStateFor(plugin))
        ++state->resetCalls;
}

clap_process_status CLAP_ABI fakeProcess(const clap_plugin_t *plugin,
                                         const clap_process_t *) {
    auto *state = checkedStateFor(plugin);
    if (!state)
        return CLAP_PROCESS_ERROR;
    ++state->processCalls;
    return CLAP_PROCESS_CONTINUE;
}

void CLAP_ABI fakeOnMainThread(const clap_plugin_t *plugin) {
    if (auto *state = checkedStateFor(plugin))
        ++state->mainThreadCalls;
}

const void *CLAP_ABI fakeGetExtension(const clap_plugin_t *plugin, const char *id) {
    auto *state = checkedStateFor(plugin);
    if (!state || !id)
        return nullptr;
    if (!state->initialized) {
        ++state->strictPreinitQueries;
        return nullptr;
    }
    if (std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0) {
        ++state->realWebviewQueries;
        return &kRealWebview;
    }
    if (std::strcmp(id, "clap.test") == 0)
        return state;
    return nullptr;
}

bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    FakeState state{};
    clap_plugin_t inner{};
    state.expectedPlugin = &inner;
    inner.plugin_data = &state;
    inner.init = fakeInit;
    inner.destroy = fakeDestroy;
    inner.activate = fakeActivate;
    inner.deactivate = fakeDeactivate;
    inner.start_processing = fakeStartProcessing;
    inner.stop_processing = fakeStopProcessing;
    inner.reset = fakeReset;
    inner.process = fakeProcess;
    inner.get_extension = fakeGetExtension;
    inner.on_main_thread = fakeOnMainThread;

    const auto *plugin = webview_gui::wrapLegacyWclapWebviewPlugin(&inner);
    if (!expect(plugin && plugin != &inner,
                "legacy WCLAP compatibility wrapper was not created"))
        return 1;

    // The historical pinned bridge asks for clap.webview/3 before calling
    // clap_plugin.init(). This one extension must be answered without touching
    // the strict inner plug-in.
    const auto *preInitWebview = static_cast<const webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, webview_gui::CLAP_EXT_WEBVIEW));
    if (!expect(preInitWebview && preInitWebview->get_uri &&
                    preInitWebview->get_resource && preInitWebview->receive,
                "pre-init WCLAP WebView compatibility extension is incomplete"))
        return 2;
    if (!expect(state.strictPreinitQueries == 0,
                "pre-init WebView compatibility query reached the strict inner plug-in"))
        return 3;

    char uri[32]{};
    if (!expect(preInitWebview->get_uri(plugin, uri, sizeof(uri)) == -1 &&
                    !preInitWebview->get_resource(plugin, "/index.html", nullptr, 0, nullptr) &&
                    !preInitWebview->receive(plugin, nullptr, 0),
                "pre-init WebView callbacks did not fail closed"))
        return 4;
    if (!expect(state.uriCalls == 0 && state.resourceCalls == 0 && state.receiveCalls == 0,
                "pre-init WebView callback reached the real extension"))
        return 5;

    // No broad lifecycle relaxation: any other pre-init extension query still
    // delegates to the original strict get_extension implementation. The inner
    // implementation must receive its original clap_plugin_t pointer rather than
    // the host-facing compatibility table.
    if (!expect(plugin->get_extension(plugin, "clap.test") == nullptr &&
                    state.strictPreinitQueries == 1 &&
                    state.pointerIdentityFailures == 0,
                "non-WebView delegation did not preserve the inner plug-in pointer identity"))
        return 6;

    if (!expect(plugin->init(plugin), "wrapped plug-in init failed"))
        return 7;
    if (!expect(state.initialized && state.realWebviewQueries == 1 &&
                    state.pointerIdentityFailures == 0,
                "init/WebView discovery did not preserve the inner plug-in pointer identity"))
        return 8;

    // All CLAP core callbacks remain host-facing through the proxy table, but
    // delegated implementations must receive the original plug-in pointer. This
    // matters for valid implementations which use pointer identity/container_of
    // rather than relying exclusively on plugin_data.
    clap_process_t process{};
    if (!expect(plugin->activate(plugin, 48000.0, 1, 64) &&
                    plugin->start_processing(plugin) &&
                    plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
                "wrapped active/process lifecycle failed"))
        return 9;
    plugin->stop_processing(plugin);
    plugin->reset(plugin);
    plugin->deactivate(plugin);
    plugin->on_main_thread(plugin);
    if (!expect(state.activateCalls == 1 && state.startCalls == 1 &&
                    state.processCalls == 1 && state.stopCalls == 1 &&
                    state.resetCalls == 1 && state.deactivateCalls == 1 &&
                    state.mainThreadCalls == 1 && state.pointerIdentityFailures == 0,
                "CLAP core callback delegation did not preserve inner plug-in pointer identity"))
        return 10;

    const auto *postInitWebview = static_cast<const webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, webview_gui::CLAP_EXT_WEBVIEW));
    if (!expect(postInitWebview == preInitWebview,
                "WCLAP WebView compatibility extension pointer changed after init"))
        return 11;
    if (!expect(state.realWebviewQueries == 1,
                "post-init compatibility lookup repeated real extension discovery"))
        return 12;

    if (!expect(postInitWebview->get_uri(plugin, uri, sizeof(uri)) > 0 &&
                    std::strcmp(uri, "/index.html") == 0,
                "post-init WebView URI did not forward to the real extension"))
        return 13;
    if (!expect(postInitWebview->get_resource(plugin, "/index.html", nullptr, 0, nullptr) &&
                    postInitWebview->receive(plugin, nullptr, 0),
                "post-init WebView callbacks did not forward to the real extension"))
        return 14;
    if (!expect(state.uriCalls == 1 && state.resourceCalls == 1 && state.receiveCalls == 1 &&
                    state.pointerIdentityFailures == 0,
                "real WebView callbacks did not preserve the inner plug-in pointer identity"))
        return 15;

    const unsigned char payload = 0x42;
    if (!expect(!postInitWebview->receive(plugin, nullptr, 1) &&
                    !postInitWebview->receive(
                        plugin,
                        &payload,
                        static_cast<std::uint32_t>(webview_gui::detail::maxMessageBytes + 1u)) &&
                    state.receiveCalls == 1,
                "legacy WCLAP WebView accepted an invalid or oversized bridge message"))
        return 16;
    if (!expect(postInitWebview->receive(plugin, &payload, 1) && state.receiveCalls == 2,
                "legacy WCLAP WebView rejected a valid bounded bridge message"))
        return 17;

    if (!expect(plugin->get_extension(plugin, "clap.test") == &state &&
                    state.pointerIdentityFailures == 0,
                "post-init non-WebView extension delegation changed plug-in pointer identity"))
        return 18;

    plugin->destroy(plugin);
    if (!expect(state.destroyed && state.pointerIdentityFailures == 0,
                "wrapped destroy did not preserve the inner plug-in pointer identity"))
        return 19;

    // The compatibility layer must not synthesize core capabilities which the
    // inner table did not expose. Preserving nullness makes validator/host
    // diagnostics observe the same core callback topology as the wrapped plug-in.
    FakeState sparseState{};
    clap_plugin_t sparse{};
    sparseState.expectedPlugin = &sparse;
    sparse.plugin_data = &sparseState;
    sparse.init = fakeInit;
    sparse.destroy = fakeDestroy;
    sparse.get_extension = fakeGetExtension;

    const auto *sparsePlugin = webview_gui::wrapLegacyWclapWebviewPlugin(&sparse);
    if (!expect(sparsePlugin && !sparsePlugin->activate && !sparsePlugin->deactivate &&
                    !sparsePlugin->start_processing && !sparsePlugin->stop_processing &&
                    !sparsePlugin->reset && !sparsePlugin->process &&
                    !sparsePlugin->on_main_thread,
                "legacy WCLAP proxy synthesized missing CLAP core callbacks"))
        return 20;
    sparsePlugin->destroy(sparsePlugin);
    if (!expect(sparseState.destroyed && sparseState.pointerIdentityFailures == 0,
                "sparse wrapped destroy did not preserve inner plug-in pointer identity"))
        return 21;

    return 0;
}
