cmake_minimum_required(VERSION 3.24)
project(webview_gui_examples LANGUAGES C CXX)

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

if(WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH)
    add_executable(webview_gui_example_polysynth_voice_allocator_tests
        tests/polysynth_voice_allocator_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_voice_allocator_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_compile_features(webview_gui_example_polysynth_voice_allocator_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_polysynth_note_scheduler_tests
        tests/polysynth_note_scheduler_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_note_scheduler_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_link_libraries(webview_gui_example_polysynth_note_scheduler_tests PRIVATE clap)
    target_compile_features(webview_gui_example_polysynth_note_scheduler_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_polysynth_voice_lifecycle_tests
        tests/polysynth_voice_lifecycle_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_voice_lifecycle_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_link_libraries(webview_gui_example_polysynth_voice_lifecycle_tests PRIVATE clap)
    target_compile_features(webview_gui_example_polysynth_voice_lifecycle_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_polysynth_voice_engine_tests
        tests/polysynth_voice_engine_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_voice_engine_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_link_libraries(webview_gui_example_polysynth_voice_engine_tests PRIVATE clap)
    target_compile_features(webview_gui_example_polysynth_voice_engine_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_polysynth_amp_envelope_tests
        tests/polysynth_amp_envelope_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_amp_envelope_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_link_libraries(webview_gui_example_polysynth_amp_envelope_tests PRIVATE clap)
    target_compile_features(webview_gui_example_polysynth_amp_envelope_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_polysynth_filter_tests
        tests/polysynth_filter_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_filter_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_link_libraries(webview_gui_example_polysynth_filter_tests PRIVATE clap)
    target_compile_features(webview_gui_example_polysynth_filter_tests PRIVATE cxx_std_17)

    add_executable(webview_gui_example_polysynth_mix_controls_tests
        tests/polysynth_mix_controls_tests.cpp
    )
    target_include_directories(webview_gui_example_polysynth_mix_controls_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/polysynth
    )
    target_link_libraries(webview_gui_example_polysynth_mix_controls_tests PRIVATE clap)
    target_compile_features(webview_gui_example_polysynth_mix_controls_tests PRIVATE cxx_std_17)

    add_test(NAME webview_gui_examples_polysynth_voice_allocator
        COMMAND webview_gui_example_polysynth_voice_allocator_tests)
    add_test(NAME webview_gui_examples_polysynth_note_scheduler
        COMMAND webview_gui_example_polysynth_note_scheduler_tests)
    add_test(NAME webview_gui_examples_polysynth_voice_lifecycle
        COMMAND webview_gui_example_polysynth_voice_lifecycle_tests)
    add_test(NAME webview_gui_examples_polysynth_voice_engine
        COMMAND webview_gui_example_polysynth_voice_engine_tests)
    add_test(NAME webview_gui_examples_polysynth_amp_envelope
        COMMAND webview_gui_example_polysynth_amp_envelope_tests)
    add_test(NAME webview_gui_examples_polysynth_filter
        COMMAND webview_gui_example_polysynth_filter_tests)
    add_test(NAME webview_gui_examples_polysynth_mix_controls
        COMMAND webview_gui_example_polysynth_mix_controls_tests)
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
