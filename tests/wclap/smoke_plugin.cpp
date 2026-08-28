#include "clap/clap.h"
#include "webview-gui/clap-webview-gui.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr const char kPluginId[] = "com.webview-gui.wclap-smoke";
constexpr const char kStartUri[] = "/gui/index.html";
constexpr const char kHtml[] = R"html(<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>webview-gui WCLAP smoke</title></head>
<body><main id="status">WCLAP WebView smoke</main></body>
</html>)html";

const char *const kFeatures[] = {nullptr};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kPluginId,
    "webview-gui WCLAP smoke",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "",
    "",
    "0.1.0",
    "WASI/WCLAP ABI and clap.webview/3 smoke fixture",
    kFeatures,
};

struct SmokePlugin {
    clap_plugin_t plugin{};
    const clap_host_t *host = nullptr;
    webview_gui::ClapWebviewGui gui;

    explicit SmokePlugin(const clap_host_t *hostIn)
        : host(hostIn), gui(&plugin, hostIn) {
        plugin.desc = &kDescriptor;
        plugin.plugin_data = this;
        plugin.init = pluginInit;
        plugin.destroy = pluginDestroy;
        plugin.activate = pluginActivate;
        plugin.deactivate = pluginDeactivate;
        plugin.start_processing = pluginStartProcessing;
        plugin.stop_processing = pluginStopProcessing;
        plugin.reset = pluginReset;
        plugin.process = pluginProcess;
        plugin.get_extension = pluginGetExtension;
        plugin.on_main_thread = pluginOnMainThread;
    }

    static SmokePlugin &self(const clap_plugin_t *plugin) {
        return *static_cast<SmokePlugin *>(plugin->plugin_data);
    }

    static bool CLAP_ABI pluginInit(const clap_plugin_t *plugin) {
        auto &instance = self(plugin);
        instance.gui.init(plugin, instance.host);
        return instance.gui.setSize(480, 240);
    }

    static void CLAP_ABI pluginDestroy(const clap_plugin_t *plugin) {
        delete &self(plugin);
    }

    static bool CLAP_ABI pluginActivate(const clap_plugin_t *, double,
                                        uint32_t, uint32_t) {
        return true;
    }

    static void CLAP_ABI pluginDeactivate(const clap_plugin_t *) {}

    static bool CLAP_ABI pluginStartProcessing(const clap_plugin_t *) {
        return true;
    }

    static void CLAP_ABI pluginStopProcessing(const clap_plugin_t *) {}
    static void CLAP_ABI pluginReset(const clap_plugin_t *) {}

    static clap_process_status CLAP_ABI pluginProcess(const clap_plugin_t *,
                                                      const clap_process_t *) {
        // The WCLAP adapter owns no DSP path. A real processor can keep the same
        // allocation-free process() implementation used by its native CLAP.
        return CLAP_PROCESS_CONTINUE;
    }

    static const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *plugin,
                                                   const char *id) {
        if (!id)
            return nullptr;

        auto &instance = self(plugin);
        if (std::strcmp(id, CLAP_EXT_GUI) == 0)
            return instance.gui.extPluginGui;
        if (std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &webviewExtension;
        return nullptr;
    }

    static void CLAP_ABI pluginOnMainThread(const clap_plugin_t *) {}

    static int32_t CLAP_ABI webviewGetUri(const clap_plugin_t *,
                                          char *uri,
                                          uint32_t uriCapacity) {
        constexpr auto fullLength = static_cast<int32_t>(sizeof(kStartUri));
        if (!uri || uriCapacity < static_cast<uint32_t>(fullLength))
            return fullLength;
        std::memcpy(uri, kStartUri, sizeof(kStartUri));
        return fullLength;
    }

    static bool CLAP_ABI webviewGetResource(const clap_plugin_t *,
                                            const char *path,
                                            char *mime,
                                            uint32_t mimeCapacity,
                                            const clap_ostream_t *stream) {
        if (!path || std::strcmp(path, kStartUri) != 0 || !mime || !stream || !stream->write)
            return false;

        static constexpr char kMime[] = "text/html";
        if (mimeCapacity < sizeof(kMime))
            return false;
        std::memcpy(mime, kMime, sizeof(kMime));

        constexpr auto size = static_cast<uint64_t>(sizeof(kHtml) - 1);
        return stream->write(stream, kHtml, size) == static_cast<int64_t>(size);
    }

    static bool CLAP_ABI webviewReceive(const clap_plugin_t *,
                                        const void *,
                                        uint32_t) {
        return true;
    }

    inline static const webview_gui::clap_plugin_webview webviewExtension{
        webviewGetUri,
        webviewGetResource,
        webviewReceive,
    };
};

uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *) {
    return 1;
}

const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(
    const clap_plugin_factory_t *, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreatePlugin(const clap_plugin_factory_t *,
                                                  const clap_host_t *host,
                                                  const char *pluginId) {
    if (!host || !pluginId || std::strcmp(pluginId, kPluginId) != 0)
        return nullptr;

    auto *instance = new (std::nothrow) SmokePlugin(host);
    return instance ? &instance->plugin : nullptr;
}

const clap_plugin_factory_t kFactory{
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

bool CLAP_ABI entryInit(const char *) {
    return true;
}

void CLAP_ABI entryDeinit() {}

const void *CLAP_ABI entryGetFactory(const char *factoryId) {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kFactory;
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
