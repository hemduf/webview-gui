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

function(webview_gui_apply_clap_wrapper_uikit_constant_patch source_file)
    if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR
            "Pinned clap-wrapper UIKit source is missing: ${source_file}")
    endif()

    file(READ "${source_file}" WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_CONTENT)

    set(WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_LEGACY_BLOCK [=[#ifndef CLAP_WINDOW_API_UIKIT
#define CLAP_WINDOW_API_UIKIT "uikit"
#endif]=])

    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_CONTENT}"
        "#define CLAP_WINDOW_API_UIKIT"
        WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_DEFINE_OFFSET)
    if(WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_DEFINE_OFFSET EQUAL -1)
        return()
    endif()

    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_LEGACY_BLOCK}"
        WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_LEGACY_OFFSET)
    if(WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_LEGACY_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned clap-wrapper UIKit compatibility shim changed; refusing to build without revalidating the compatibility patch")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_LEGACY_BLOCK}"
        ""
        WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_CONTENT
        "${WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_CONTENT}")

    file(WRITE "${source_file}" "${WEBVIEW_GUI_CLAP_WRAPPER_UIKIT_CONTENT}")
endfunction()

function(webview_gui_apply_clap_wrapper_auv3_host_switch_patch source_file)
    if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR
            "Pinned clap-wrapper AUv3 host source is missing: ${source_file}")
    endif()

    file(READ "${source_file}" WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT)

    # Xcode 26 rejects the pinned 0.16.0 source because control can jump from
    # the NotDetermined case across the lifetime of an Objective-C block capture.
    # Scope that case explicitly. Check both markers before mutating so a future
    # upstream change fails closed instead of leaving a half-patched source file.
    set(WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_OPEN [=[    case AVAuthorizationStatusNotDetermined:
      [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio]=])
    set(WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_CLOSE [=[                               }];
      break;
    default:]=])
    set(WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_OPEN [=[    case AVAuthorizationStatusNotDetermined: {
      [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio]=])
    set(WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_CLOSE [=[                               }];
      break;
    }
    default:]=])

    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_OPEN}"
        WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_OPEN_OFFSET)
    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_CLOSE}"
        WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_CLOSE_OFFSET)
    if(NOT WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_OPEN_OFFSET EQUAL -1 AND
       NOT WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_CLOSE_OFFSET EQUAL -1)
        return()
    endif()

    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_OPEN}"
        WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_OPEN_OFFSET)
    string(FIND "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_CLOSE}"
        WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_CLOSE_OFFSET)
    if(WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_OPEN_OFFSET EQUAL -1 OR
       WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_CLOSE_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned clap-wrapper AUv3 microphone-permission switch changed; refusing to build without revalidating the Xcode compatibility patch")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_OPEN}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_OPEN}"
        WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}")
    string(REPLACE
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_OLD_CLOSE}"
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_SAFE_CLOSE}"
        WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT
        "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}")

    file(WRITE "${source_file}" "${WEBVIEW_GUI_CLAP_WRAPPER_AUV3_HOST_CONTENT}")
endfunction()
