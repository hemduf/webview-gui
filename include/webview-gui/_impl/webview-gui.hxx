#pragma once

#if defined(WEBVIEW_GUI_WEBVIEW_ONLY) || defined(__EMSCRIPTEN__) || defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
#	include "./platform/webview-only.h"
#elif defined(__has_include) && (__has_include("choc/gui/choc_WebView.h") || __has_include("./platform/choc/gui/choc_WebView.h"))
#	include "./platform/choc.h"
#else
#	include "./platform/not-supported.h"
#endif
