#include "gain_plugin.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace webview_gui::examples::gain {
namespace {

using GainBase = clap::helpers::Plugin<
    clap::helpers::MisbehaviourHandler::Terminate,
    clap::helpers::CheckingLevel::Minimal>;

static_assert(std::atomic<float>::is_always_lock_free,
              "Gain parameter snapshots must be lock-free on supported targets");
static_assert(std::atomic<bool>::is_always_lock_free,
              "Bypass parameter snapshots must be lock-free on supported targets");

const char *const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kGainPluginId,
    "webview-gui Gain",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "",
    "",
    "0.1.0",
    "Stereo Gain reference effect",
    kFeatures,
};

bool copyText(char *destination, std::size_t capacity, const char *text) noexcept {
    if (!destination || capacity == 0 || !text)
        return false;
    const int written = std::snprintf(destination, capacity, "%s", text);
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

class GainPlugin final : public GainBase {
public:
    explicit GainPlugin(const clap_host_t *host)
        : GainBase(&kDescriptor, host) {
        syncParameterSnapshots();
    }

    ~GainPlugin() override = default;

protected:
    clap_process_status process(const clap_process_t *processData) noexcept override {
        if (!processData || !processor_.process(*processData))
            return CLAP_PROCESS_ERROR;
        syncParameterSnapshots();
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }

    bool audioPortsInfo(uint32_t index,
                        bool isInput,
                        clap_audio_port_info_t *info) const noexcept override {
        if (index != 0 || !info)
            return false;
        *info = {};
        info->id = isInput ? kGainInputPortId : kGainOutputPortId;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? kGainOutputPortId : kGainInputPortId;
        return copyText(info->name, sizeof(info->name), isInput ? "Stereo In" : "Stereo Out");
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return 2; }

    bool paramsInfo(uint32_t paramIndex, clap_param_info_t *info) const noexcept override {
        if (!info || paramIndex >= 2)
            return false;
        *info = {};
        if (paramIndex == 0) {
            info->id = kGainParamId;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
            info->min_value = GainProcessor::kMinimumGainDb;
            info->max_value = GainProcessor::kMaximumGainDb;
            info->default_value = 0.0;
            return copyText(info->name, sizeof(info->name), "Gain");
        }
        info->id = kBypassParamId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
                      CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return copyText(info->name, sizeof(info->name), "Bypass");
    }

    bool paramsValue(clap_id paramId, double *value) noexcept override {
        if (!value)
            return false;
        if (paramId == kGainParamId) {
            *value = static_cast<double>(gainDbSnapshot_.load(std::memory_order_relaxed));
            return true;
        }
        if (paramId == kBypassParamId) {
            *value = bypassSnapshot_.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            return true;
        }
        return false;
    }

    bool paramsValueToText(clap_id paramId,
                           double value,
                           char *display,
                           uint32_t size) noexcept override {
        if (!display || size == 0 || !std::isfinite(value))
            return false;
        int written = -1;
        if (paramId == kGainParamId &&
            value >= GainProcessor::kMinimumGainDb && value <= GainProcessor::kMaximumGainDb) {
            written = std::snprintf(display, size, "%.2f dB", value);
        } else if (paramId == kBypassParamId && value >= 0.0 && value <= 1.0) {
            written = std::snprintf(display, size, "%s",
                                    static_cast<int32_t>(value) != 0 ? "On" : "Off");
        }
        return written >= 0 && static_cast<uint32_t>(written) < size;
    }

    bool paramsTextToValue(clap_id paramId,
                           const char *display,
                           double *value) noexcept override {
        if (!display || !value)
            return false;
        if (paramId == kBypassParamId) {
            if (std::strcmp(display, "On") == 0 || std::strcmp(display, "1") == 0) {
                *value = 1.0;
                return true;
            }
            if (std::strcmp(display, "Off") == 0 || std::strcmp(display, "0") == 0) {
                *value = 0.0;
                return true;
            }
            return false;
        }
        if (paramId != kGainParamId)
            return false;
        char *end = nullptr;
        const double parsed = std::strtod(display, &end);
        if (end == display || !std::isfinite(parsed) ||
            parsed < GainProcessor::kMinimumGainDb || parsed > GainProcessor::kMaximumGainDb)
            return false;
        while (*end == ' ' || *end == '\t')
            ++end;
        if (end[0] == 'd' && end[1] == 'B')
            end += 2;
        while (*end == ' ' || *end == '\t')
            ++end;
        if (*end != '\0')
            return false;
        *value = parsed;
        return true;
    }

    void paramsFlush(const clap_input_events_t *in,
                     const clap_output_events_t *) noexcept override {
        if (!in || !in->size || !in->get)
            return;
        const uint32_t count = in->size(in);
        for (uint32_t index = 0; index < count; ++index) {
            const auto *header = in->get(in, index);
            if (!header || header->size < sizeof(clap_event_header_t))
                continue;
            if (header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                header->type != CLAP_EVENT_PARAM_VALUE ||
                header->size < sizeof(clap_event_param_value_t))
                continue;
            const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
            (void)processor_.applyParameterValue(event);
        }
        syncParameterSnapshots();
    }

    int32_t getParamIndexForParamId(clap_id paramId) const noexcept override {
        if (paramId == kGainParamId)
            return 0;
        if (paramId == kBypassParamId)
            return 1;
        return -1;
    }

private:
    void syncParameterSnapshots() noexcept {
        gainDbSnapshot_.store(static_cast<float>(processor_.processor().gainDb()),
                              std::memory_order_relaxed);
        bypassSnapshot_.store(processor_.processor().bypassed(), std::memory_order_relaxed);
    }

    GainEventProcessor processor_{};
    std::atomic<float> gainDbSnapshot_{0.0f};
    std::atomic<bool> bypassSnapshot_{false};
};

uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *) { return 1; }

const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(
    const clap_plugin_factory_t *, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreatePlugin(
    const clap_plugin_factory_t *, const clap_host_t *host, const char *pluginId) {
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version) ||
        std::strcmp(pluginId, kGainPluginId) != 0)
        return nullptr;
    auto *instance = new (std::nothrow) GainPlugin(host);
    return instance ? instance->clapPlugin() : nullptr;
}

const clap_plugin_factory_t kFactory{
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

} // namespace

const clap_plugin_factory_t *gainFactory() noexcept { return &kFactory; }

} // namespace webview_gui::examples::gain
