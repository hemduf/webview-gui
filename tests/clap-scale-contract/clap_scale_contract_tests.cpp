#include "webview-gui/clap-webview-gui.h"

#include <cstdio>

namespace {

const void *CLAP_ABI noPluginExtension(const clap_plugin_t *, const char *) {
    return nullptr;
}

const void *CLAP_ABI noHostExtension(const clap_host_t *, const char *) {
    return nullptr;
}

int fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

} // namespace

int main() {
    clap_plugin_t plugin{};
    plugin.get_extension = noPluginExtension;

    clap_host_t host{};
    host.get_extension = noHostExtension;

    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();

    if (gui.setScale(2.0))
        return fail("ClapWebviewGui::setScale() reported success without applying scale");

    if (!gui.extPluginGui)
        return fail("CLAP GUI extension proxy is missing");

    if (gui.extPluginGui->set_scale(&plugin, 2.0))
        return fail("clap_plugin_gui.set_scale reported success without applying scale");

    return 0;
}
