function(webview_gui_apply_clap_wrapper_macos_string_patch source_file)
    if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR
            "Pinned clap-wrapper macOS helper is missing: ${source_file}")
    endif()

    file(READ "${source_file}" WEBVIEW_GUI_CLAP_WRAPPER_MACOS_CONTENT)

    set(WEBVIEW_GUI_CLAP_WRAPPER_MACOS_OLD_CONVERSION [=[  std::string result;

  CFIndex length = CFStringGetLength(aString);
  CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  result.reserve(maxSize);

  if (CFStringGetCString(aString, result.data(), maxSize, kCFStringEncodingUTF8))
  {
    return result;
  }]=])

    set(WEBVIEW_GUI_CLAP_WRAPPER_MACOS_SAFE_CONVERSION [=[  CFIndex length = CFStringGetLength(aString);
  CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> result(static_cast<std::size_t>(maxSize));

  if (CFStringGetCString(aString, result.data(), maxSize, kCFStringEncodingUTF8))
  {
    return std::string(result.data());
  }]=])

    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_SAFE_CONVERSION}"
        WEBVIEW_GUI_CLAP_WRAPPER_MACOS_SAFE_OFFSET)
    if(NOT WEBVIEW_GUI_CLAP_WRAPPER_MACOS_SAFE_OFFSET EQUAL -1)
        return()
    endif()

    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_OLD_CONVERSION}"
        WEBVIEW_GUI_CLAP_WRAPPER_MACOS_OLD_OFFSET)
    if(WEBVIEW_GUI_CLAP_WRAPPER_MACOS_OLD_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned clap-wrapper macOS CFString conversion changed; refusing to build without revalidating the compatibility patch")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_OLD_CONVERSION}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_SAFE_CONVERSION}"
        WEBVIEW_GUI_CLAP_WRAPPER_MACOS_CONTENT
        "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_CONTENT}")

    file(WRITE "${source_file}" "${WEBVIEW_GUI_CLAP_WRAPPER_MACOS_CONTENT}")
endfunction()
