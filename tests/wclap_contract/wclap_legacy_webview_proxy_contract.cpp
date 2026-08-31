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
    inner.get_extension = fakeGetExtension;

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

    const auto *postInitWebview = static_cast<const webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, webview_gui::CLAP_EXT_WEBVIEW));
    if (!expect(postInitWebview == preInitWebview,
                "WCLAP WebView compatibility extension pointer changed after init"))
        return 9;
    if (!expect(state.realWebviewQueries == 1,
                "post-init compatibility lookup repeated real extension discovery"))
        return 10;

    if (!expect(postInitWebview->get_uri(plugin, uri, sizeof(uri)) > 0 &&
                    std::strcmp(uri, "/index.html") == 0,
                "post-init WebView URI did not forward to the real extension"))
        return 11;
    if (!expect(postInitWebview->get_resource(plugin, "/index.html", nullptr, 0, nullptr) &&
                    postInitWebview->receive(plugin, nullptr, 0),
                "post-init WebView callbacks did not forward to the real extension"))
        return 12;
    if (!expect(state.uriCalls == 1 && state.resourceCalls == 1 && state.receiveCalls == 1 &&
                    state.pointerIdentityFailures == 0,
                "real WebView callbacks did not preserve the inner plug-in pointer identity"))
        return 13;

    if (!expect(plugin->get_extension(plugin, "clap.test") == &state &&
                    state.pointerIdentityFailures == 0,
                "post-init non-WebView extension delegation changed plug-in pointer identity"))
        return 14;

    plugin->destroy(plugin);
    if (!expect(state.destroyed && state.pointerIdentityFailures == 0,
                "wrapped destroy did not preserve the inner plug-in pointer identity"))
        return 15;

    return 0;
}
