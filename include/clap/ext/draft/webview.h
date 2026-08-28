#pragma once

// Compatibility shim for consumers pinned to a CLAP SDK which predates the
// draft clap.webview/3 header. If the consumer places a newer CLAP SDK include
// directory before webview-gui, the SDK's canonical header is used instead.
// These declarations intentionally mirror the CLAP 1.2.x draft ABI.

#include "clap/plugin.h"
#include "clap/stream.h"

static CLAP_CONSTEXPR const char CLAP_EXT_WEBVIEW[] = "clap.webview/3";

// clap.gui API constant. The pointer in clap_window must be NULL, but sizing
// methods remain useful. This uses logical size; do not call set_scale().
static const CLAP_CONSTEXPR char CLAP_WINDOW_API_WEBVIEW[] = "webview";

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_plugin_webview {
   int32_t(CLAP_ABI *get_uri)(const clap_plugin_t *plugin,
                              char *uri,
                              uint32_t uri_capacity);

   bool(CLAP_ABI *get_resource)(const clap_plugin_t  *plugin,
                                const char           *path,
                                char                 *mime,
                                uint32_t              mime_capacity,
                                const clap_ostream_t *data_stream);

   bool(CLAP_ABI *receive)(const clap_plugin_t *plugin,
                           const void *buffer,
                           uint32_t size);
} clap_plugin_webview_t;

typedef struct clap_host_webview {
   bool(CLAP_ABI *send)(const clap_host_t *host,
                        const void *buffer,
                        uint32_t size);
} clap_host_webview_t;

#ifdef __cplusplus
}
#endif
