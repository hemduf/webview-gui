#include "gain_plugin.h"

#include <cstring>

namespace webview_gui::examples::gain {
namespace {
bool CLAP_ABI gainEntryInit(const char *) { return true; }
void CLAP_ABI gainEntryDeinit() {}
const void *CLAP_ABI gainEntryGetFactory(const char *factoryId) {
    if (factoryId != nullptr && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return gainFactory();
    return nullptr;
}
} // namespace
} // namespace webview_gui::examples::gain

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    webview_gui::examples::gain::gainEntryInit,
    webview_gui::examples::gain::gainEntryDeinit,
    webview_gui::examples::gain::gainEntryGetFactory,
};
