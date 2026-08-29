cmake_minimum_required(VERSION 3.24)
project(webview_gui_examples LANGUAGES C CXX)

# clap-wrapper documents C++17 as its default, but when it is embedded through
# FetchContent its top-level-only default is not applied. Set the example tree's
# language standards explicitly so the wrapper's C++ and Objective-C++ sources
# get the contract they require on every supported platform.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

include(CTest)
include(FetchContent)

option(WEBVIEW_GUI_EXAMPLES_BUILD_GAIN "Build the Gain reference example" OFF)
option(WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH "Build the PolySynth reference example" OFF)
option(WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS "Enable pinned native wrapper integration" OFF)

set(WEBVIEW_GUI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH
    "Path to the webview-gui repository root")

if(NOT TARGET webview-gui)
    add_subdirectory("${WEBVIEW_GUI_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/webview-gui-under-test" EXCLUDE_FROM_ALL)
endif()

# Keep example dependencies deterministic and independent from normal library
# consumers. These exact commits are reviewed together with the example code.
set(WEBVIEW_GUI_EXAMPLES_CLAP_REVISION
    "a47f6badb49d948fd009998f28309cdab78979c9" CACHE STRING
    "Pinned free-audio/clap revision")
set(WEBVIEW_GUI_EXAMPLES_CLAP_HELPERS_REVISION
    "c35dd4906bd8efbb900cb2b89e680fed463cc8b1" CACHE STRING
    "Pinned free-audio/clap-helpers revision")
set(WEBVIEW_GUI_EXAMPLES_CLAP_WRAPPER_REVISION
    "1cca996e96f29ab2be7ae9f8cfe532bbc92e1dd6" CACHE STRING
    "Pinned free-audio/clap-wrapper revision")
set(WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_REVISION
    "152b9823e992d782c5c1fd33bca0295478b919aa" CACHE STRING
    "Pinned free-audio/clap-validator 0.4.1 revision")
set(WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT "" CACHE PATH
    "Path to a built pinned clap-validator checkout; empty disables validator target")

set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(webview_gui_example_clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG ${WEBVIEW_GUI_EXAMPLES_CLAP_REVISION}
)
FetchContent_MakeAvailable(webview_gui_example_clap)

set(CLAP_HELPERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLAP_HELPERS_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(CLAP_HELPERS_NO_CLAP_IS_FATAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(webview_gui_example_clap_helpers
    GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
    GIT_TAG ${WEBVIEW_GUI_EXAMPLES_CLAP_HELPERS_REVISION}
)
FetchContent_MakeAvailable(webview_gui_example_clap_helpers)

add_library(webview_gui_example_skeleton_core STATIC
    common/skeleton_plugin.cpp
)
target_include_directories(webview_gui_example_skeleton_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/common
)
target_link_libraries(webview_gui_example_skeleton_core PUBLIC clap-helpers)
target_compile_features(webview_gui_example_skeleton_core PUBLIC cxx_std_17)
set_target_properties(webview_gui_example_skeleton_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(webview_gui_example_skeleton MODULE
    common/skeleton_entry.cpp
)
target_link_libraries(webview_gui_example_skeleton PRIVATE
    webview_gui_example_skeleton_core
    webview-gui
)
target_compile_features(webview_gui_example_skeleton PRIVATE cxx_std_17)
set_target_properties(webview_gui_example_skeleton PROPERTIES
    OUTPUT_NAME "WebviewGuiSkeleton"
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(APPLE)
    set_target_properties(webview_gui_example_skeleton PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.webview-gui.example.skeleton"
        MACOSX_BUNDLE_BUNDLE_NAME "WebviewGuiSkeleton"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/common/plugin.plist.in"
    )
elseif(WIN32 OR UNIX)
    set_target_properties(webview_gui_example_skeleton PROPERTIES SUFFIX ".clap")
endif()

add_executable(webview_gui_example_skeleton_tests
    tests/skeleton_factory_tests.cpp
)
target_link_libraries(webview_gui_example_skeleton_tests PRIVATE
    webview_gui_example_skeleton_core
)
target_compile_features(webview_gui_example_skeleton_tests PRIVATE cxx_std_17)

add_test(NAME webview_gui_examples_skeleton_factory
    COMMAND webview_gui_example_skeleton_tests)

if(WEBVIEW_GUI_EXAMPLES_BUILD_GAIN)
    add_library(webview_gui_example_gain_clap_core STATIC
        gain/gain_plugin.cpp
    )
    target_include_directories(webview_gui_example_gain_clap_core PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/gain
    )
    target_link_libraries(webview_gui_example_gain_clap_core
        PUBLIC clap-helpers
        PRIVATE webview-gui
    )
    target_compile_features(webview_gui_example_gain_clap_core PUBLIC cxx_std_17)
    set_target_properties(webview_gui_example_gain_clap_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    add_library(webview_gui_example_gain MODULE
        gain/gain_entry.cpp
    )
    target_link_libraries(webview_gui_example_gain PRIVATE
        webview_gui_example_gain_clap_core
        webview-gui
    )
    target_compile_features(webview_gui_example_gain PRIVATE cxx_std_17)
    set_target_properties(webview_gui_example_gain PROPERTIES
        OUTPUT_NAME "WebviewGuiGain"
        PREFIX ""
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    if(APPLE)
        set_target_properties(webview_gui_example_gain PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION clap
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.webview-gui.example.gain"
            MACOSX_BUNDLE_BUNDLE_NAME "WebviewGuiGain"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/common/plugin.plist.in"
        )
    elseif(WIN32 OR UNIX)
        set_target_properties(webview_gui_example_gain PROPERTIES SUFFIX ".clap")
    endif()

    if(WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT)
        find_program(WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_EXECUTABLE
            NAMES clap-validator clap-validator.exe
            PATHS "${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT}/target/release"
            NO_DEFAULT_PATH
        )
        if(NOT WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_EXECUTABLE)
            message(FATAL_ERROR
                "Pinned clap-validator executable was not found under "
                "${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT}/target/release")
        endif()

        if(APPLE)
            set(_webview_gui_gain_validation_path
                "$<TARGET_BUNDLE_DIR:webview_gui_example_gain>")
        else()
            set(_webview_gui_gain_validation_path
                "$<TARGET_FILE:webview_gui_example_gain>")
        endif()

        add_custom_target(webview_gui_example_gain_validate
            COMMAND ${CMAKE_COMMAND} -E echo
                "clap-validator commit: ${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_REVISION}"
            COMMAND ${CMAKE_COMMAND} -E echo
                "Gain CLAP path: ${_webview_gui_gain_validation_path}"
            COMMAND "${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_EXECUTABLE}" --version
            COMMAND "${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_EXECUTABLE}"
                validate "${_webview_gui_gain_validation_path}" --only-failed
            DEPENDS webview_gui_example_gain
            VERBATIM
        )
    endif()

    add_executable(webview_gui_example_gain_core_tests
        tests/gain_core_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_core_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
    )
    target_link_libraries(webview_gui_example_gain_core_tests PRIVATE clap)
    target_compile_features(webview_gui_example_gain_core_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_meter_tests
        tests/gain_meter_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_meter_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
        ${CMAKE_CURRENT_LIST_DIR}/common
    )
    target_link_libraries(webview_gui_example_gain_meter_tests PRIVATE clap)
    target_compile_features(webview_gui_example_gain_meter_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_clap_tests
        tests/gain_clap_adapter_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_clap_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
        ${CMAKE_CURRENT_LIST_DIR}/common
    )
    target_link_libraries(webview_gui_example_gain_clap_tests PRIVATE
        webview_gui_example_gain_clap_core
    )
    target_compile_features(webview_gui_example_gain_clap_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_state_host_sync_tests
        tests/gain_state_host_sync_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_state_host_sync_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
        ${CMAKE_CURRENT_LIST_DIR}/common
    )
    target_link_libraries(webview_gui_example_gain_state_host_sync_tests PRIVATE
        webview_gui_example_gain_clap_core
    )
    target_compile_features(webview_gui_example_gain_state_host_sync_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_webview_resource_tests
        tests/gain_webview_resource_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_webview_resource_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
    )
    target_link_libraries(webview_gui_example_gain_webview_resource_tests PRIVATE
        webview_gui_example_gain_clap_core
    )
    target_compile_features(webview_gui_example_gain_webview_resource_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_webview_param_tests
        tests/gain_webview_param_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_webview_param_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
        ${CMAKE_CURRENT_LIST_DIR}/common
    )
    target_link_libraries(webview_gui_example_gain_webview_param_tests PRIVATE
        webview_gui_example_gain_clap_core
    )
    target_compile_features(webview_gui_example_gain_webview_param_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_webview_backpressure_tests
        tests/gain_webview_backpressure_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_webview_backpressure_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
    )
    target_link_libraries(webview_gui_example_gain_webview_backpressure_tests PRIVATE clap)
    target_compile_features(webview_gui_example_gain_webview_backpressure_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_gain_native_gui_tests
        tests/gain_native_gui_tests.cpp
    )
    target_include_directories(webview_gui_example_gain_native_gui_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gain
    )
    target_link_libraries(webview_gui_example_gain_native_gui_tests PRIVATE
        webview_gui_example_gain_clap_core
        webview-gui
    )
    target_compile_features(webview_gui_example_gain_native_gui_tests PRIVATE cxx_std_17)

    add_test(NAME webview_gui_examples_gain_core
        COMMAND webview_gui_example_gain_core_tests)
    add_test(NAME webview_gui_examples_gain_meter
        COMMAND webview_gui_example_gain_meter_tests)
    add_test(NAME webview_gui_examples_gain_clap_adapter
        COMMAND webview_gui_example_gain_clap_tests)
    add_test(NAME webview_gui_examples_gain_state_host_sync
        COMMAND webview_gui_example_gain_state_host_sync_tests)
    add_test(NAME webview_gui_examples_gain_webview_resource
        COMMAND webview_gui_example_gain_webview_resource_tests)
    add_test(NAME webview_gui_examples_gain_webview_param
        COMMAND webview_gui_example_gain_webview_param_tests)
    add_test(NAME webview_gui_examples_gain_webview_backpressure
        COMMAND webview_gui_example_gain_webview_backpressure_tests)
    add_test(NAME webview_gui_examples_gain_native_gui
        COMMAND webview_gui_example_gain_native_gui_tests)
endif()

# #28 only prepares the pinned native wrapper integration point. Concrete VST3,
# AU and standalone products are owned by #34, and WCLAP remains on the
# repository-owned #30 packaging path rather than clap-wrapper's WCLAP helper.
if(WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS)
    FetchContent_Declare(webview_gui_example_clap_wrapper
        GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
        GIT_TAG ${WEBVIEW_GUI_EXAMPLES_CLAP_WRAPPER_REVISION}
    )
    FetchContent_MakeAvailable(webview_gui_example_clap_wrapper)

    if(APPLE)
        include("${WEBVIEW_GUI_SOURCE_DIR}/cmake/WebviewGuiClapWrapperMacOSPatch.cmake")
        webview_gui_apply_clap_wrapper_macos_string_patch(
            "${webview_gui_example_clap_wrapper_SOURCE_DIR}/src/detail/clap/mac_helpers.mm")
    endif()

    # clap-wrapper must reuse the already-pinned CLAP target instead of creating
    # or downloading a second SDK. Avoid forcing any clap-wrapper cache knobs here
    # so an embedding project keeps ownership of its global wrapper configuration.
    if(NOT TARGET base-sdk-clap)
        message(FATAL_ERROR
            "Pinned clap-wrapper did not reuse the existing example CLAP SDK target")
    endif()

    if(NOT COMMAND target_add_vst3_wrapper
       OR NOT COMMAND target_add_auv2_wrapper
       OR NOT COMMAND target_add_auv3_wrapper
       OR NOT COMMAND target_add_standalone_wrapper)
        message(FATAL_ERROR
            "Pinned clap-wrapper revision does not expose the expected native wrapper API")
    endif()

    add_test(NAME webview_gui_examples_wrapper_foundation
        COMMAND ${CMAKE_COMMAND}
            "-DWRAPPER_REVISION=${WEBVIEW_GUI_EXAMPLES_CLAP_WRAPPER_REVISION}"
            "-DWRAPPER_SOURCE_DIR=${webview_gui_example_clap_wrapper_SOURCE_DIR}"
            "-DCLAP_SOURCE_DIR=${webview_gui_example_clap_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/tests/wrapper_foundation_contract.cmake")
endif()
