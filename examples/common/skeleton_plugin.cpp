#include "skeleton_plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <cstring>
#include <new>

namespace webview_gui::examples {
namespace {

using SkeletonBase = clap::helpers::Plugin<
    clap::helpers::MisbehaviourHandler::Terminate,
    clap::helpers::CheckingLevel::Minimal>;

const char *const kFeatures[] = {nullptr};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kSkeletonPluginId,
    "webview-gui CLAP skeleton",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "",
    "",
    "0.1.0",
    "Minimal clap-helpers-backed example foundation",
    kFeatures,
};

class SkeletonPlugin final : public SkeletonBase {
public:
    explicit SkeletonPlugin(const clap_host_t *host)
        : SkeletonBase(&kDescriptor, host) {}

    ~SkeletonPlugin() override = default;

protected:
    clap_process_status process(const clap_process_t *) noexcept override {
        // The foundation intentionally has no DSP yet. The important RT
        // contract is that this callback remains bounded and performs no heap,
        // lock, filesystem, WebView or host-main-thread work.
        return CLAP_PROCESS_SLEEP;
    }
};

uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *) {
    return 1;
}

const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(
    const clap_plugin_factory_t *, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreatePlugin(
    const clap_plugin_factory_t *, const clap_host_t *host, const char *pluginId) {
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version) ||
        std::strcmp(pluginId, kSkeletonPluginId) != 0) {
        return nullptr;
    }

    auto *instance = new (std::nothrow) SkeletonPlugin(host);
    return instance ? instance->clapPlugin() : nullptr;
}

const clap_plugin_factory_t kFactory{
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

} // namespace

const clap_plugin_factory_t *skeletonFactory() noexcept {
    return &kFactory;
}

} // namespace webview_gui::examples
