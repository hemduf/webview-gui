add_library(webview_gui_example_polysynth_clap_core STATIC
    polysynth_plugin.cpp
)
target_include_directories(webview_gui_example_polysynth_clap_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_clap_core PUBLIC clap-helpers)
target_compile_features(webview_gui_example_polysynth_clap_core PUBLIC cxx_std_17)
set_target_properties(webview_gui_example_polysynth_clap_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(webview_gui_example_polysynth MODULE
    polysynth_entry.cpp
)
target_link_libraries(webview_gui_example_polysynth PRIVATE
    webview_gui_example_polysynth_clap_core
    webview-gui
)
target_compile_features(webview_gui_example_polysynth PRIVATE cxx_std_17)
set_target_properties(webview_gui_example_polysynth PROPERTIES
    OUTPUT_NAME "WebviewGuiPolySynth"
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(APPLE)
    set_target_properties(webview_gui_example_polysynth PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.webview-gui.example.polysynth"
        MACOSX_BUNDLE_BUNDLE_NAME "WebviewGuiPolySynth"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/../common/plugin.plist.in"
    )
elseif(WIN32 OR UNIX)
    set_target_properties(webview_gui_example_polysynth PROPERTIES SUFFIX ".clap")
endif()

if(WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT)
    find_program(WEBVIEW_GUI_EXAMPLES_POLYSYNTH_CLAP_VALIDATOR_EXECUTABLE
        NAMES clap-validator clap-validator.exe
        PATHS "${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT}/target/release"
        NO_DEFAULT_PATH
    )
    if(NOT WEBVIEW_GUI_EXAMPLES_POLYSYNTH_CLAP_VALIDATOR_EXECUTABLE)
        message(FATAL_ERROR
            "Pinned clap-validator executable was not found under "
            "${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT}/target/release")
    endif()

    if(APPLE)
        set(_webview_gui_polysynth_validation_path
            "$<TARGET_BUNDLE_DIR:webview_gui_example_polysynth>")
    else()
        set(_webview_gui_polysynth_validation_path
            "$<TARGET_FILE:webview_gui_example_polysynth>")
    endif()

    add_custom_target(webview_gui_example_polysynth_validate
        COMMAND ${CMAKE_COMMAND} -E echo
            "clap-validator commit: ${WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_REVISION}"
        COMMAND ${CMAKE_COMMAND} -E echo
            "PolySynth CLAP path: ${_webview_gui_polysynth_validation_path}"
        COMMAND "${WEBVIEW_GUI_EXAMPLES_POLYSYNTH_CLAP_VALIDATOR_EXECUTABLE}" --version
        COMMAND "${WEBVIEW_GUI_EXAMPLES_POLYSYNTH_CLAP_VALIDATOR_EXECUTABLE}"
            validate "${_webview_gui_polysynth_validation_path}" --only-failed
        DEPENDS webview_gui_example_polysynth
        VERBATIM
    )
endif()

add_executable(webview_gui_example_polysynth_voice_allocator_tests
    ../tests/polysynth_voice_allocator_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_voice_allocator_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_compile_features(webview_gui_example_polysynth_voice_allocator_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_note_scheduler_tests
    ../tests/polysynth_note_scheduler_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_note_scheduler_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_note_scheduler_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_note_scheduler_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_voice_lifecycle_tests
    ../tests/polysynth_voice_lifecycle_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_voice_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_voice_lifecycle_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_voice_lifecycle_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_voice_engine_tests
    ../tests/polysynth_voice_engine_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_voice_engine_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_voice_engine_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_voice_engine_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_amp_envelope_tests
    ../tests/polysynth_amp_envelope_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_amp_envelope_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_amp_envelope_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_amp_envelope_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_amp_envelope_parameter_tests
    ../tests/polysynth_amp_envelope_parameter_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_amp_envelope_parameter_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_amp_envelope_parameter_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_amp_envelope_parameter_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_filter_tests
    ../tests/polysynth_filter_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_filter_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_filter_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_filter_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_mix_controls_tests
    ../tests/polysynth_mix_controls_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_mix_controls_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_mix_controls_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_mix_controls_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_filter_envelope_tests
    ../tests/polysynth_filter_envelope_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_filter_envelope_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_filter_envelope_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_filter_envelope_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_oscillator_tuning_tests
    ../tests/polysynth_oscillator_tuning_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_oscillator_tuning_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_oscillator_tuning_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_oscillator_tuning_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_polyphonic_parameter_state_tests
    ../tests/polysynth_polyphonic_parameter_state_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_polyphonic_parameter_state_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_polyphonic_parameter_state_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_polyphonic_parameter_state_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_event_stream_tests
    ../tests/polysynth_event_stream_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_event_stream_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_event_stream_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_event_stream_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_parameter_model_tests
    ../tests/polysynth_parameter_model_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_parameter_model_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_parameter_model_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_parameter_model_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_parameter_event_tests
    ../tests/polysynth_parameter_event_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_parameter_event_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_parameter_event_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_parameter_event_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_note_expression_tests
    ../tests/polysynth_note_expression_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_note_expression_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_note_expression_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_note_expression_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_pan_expression_tests
    ../tests/polysynth_pan_expression_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_pan_expression_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_pan_expression_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_pan_expression_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_brightness_expression_tests
    ../tests/polysynth_brightness_expression_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_brightness_expression_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_brightness_expression_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_brightness_expression_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_expression_expression_tests
    ../tests/polysynth_expression_expression_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_expression_expression_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_expression_expression_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_expression_expression_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_pressure_expression_tests
    ../tests/polysynth_pressure_expression_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_pressure_expression_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_pressure_expression_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_pressure_expression_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_note_expression_stream_tests
    ../tests/polysynth_note_expression_stream_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_note_expression_stream_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_note_expression_stream_tests PRIVATE clap)
target_compile_features(webview_gui_example_polysynth_note_expression_stream_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_clap_ports_tests
    ../tests/polysynth_clap_ports_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_clap_ports_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_clap_ports_tests PRIVATE
    webview_gui_example_polysynth_clap_core
)
target_compile_features(webview_gui_example_polysynth_clap_ports_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_param_flush_polyphony_tests
    ../tests/polysynth_param_flush_polyphony_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_param_flush_polyphony_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_param_flush_polyphony_tests PRIVATE
    webview_gui_example_polysynth_clap_core
)
target_compile_features(webview_gui_example_polysynth_param_flush_polyphony_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_voice_info_tests
    ../tests/polysynth_voice_info_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_voice_info_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_voice_info_tests PRIVATE
    webview_gui_example_polysynth_clap_core
    webview-gui
)
target_compile_features(webview_gui_example_polysynth_voice_info_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_state_tests
    ../tests/polysynth_state_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_state_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_state_tests PRIVATE
    webview_gui_example_polysynth_clap_core
)
target_compile_features(webview_gui_example_polysynth_state_tests PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
add_executable(webview_gui_example_polysynth_state_snapshot_concurrency_tests
    ../tests/polysynth_state_snapshot_concurrency_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_state_snapshot_concurrency_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_state_snapshot_concurrency_tests PRIVATE
    webview_gui_example_polysynth_clap_core
    Threads::Threads
)
target_compile_features(webview_gui_example_polysynth_state_snapshot_concurrency_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_state_context_tests
    ../tests/polysynth_state_context_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_state_context_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_state_context_tests PRIVATE
    webview_gui_example_polysynth_clap_core
)
target_compile_features(webview_gui_example_polysynth_state_context_tests PRIVATE cxx_std_17)

add_executable(webview_gui_example_polysynth_tail_tests
    ../tests/polysynth_tail_tests.cpp
)
target_include_directories(webview_gui_example_polysynth_tail_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(webview_gui_example_polysynth_tail_tests PRIVATE
    webview_gui_example_polysynth_clap_core
)
target_compile_features(webview_gui_example_polysynth_tail_tests PRIVATE cxx_std_17)

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
add_test(NAME webview_gui_examples_polysynth_amp_envelope_parameter
    COMMAND webview_gui_example_polysynth_amp_envelope_parameter_tests)
add_test(NAME webview_gui_examples_polysynth_filter
    COMMAND webview_gui_example_polysynth_filter_tests)
add_test(NAME webview_gui_examples_polysynth_mix_controls
    COMMAND webview_gui_example_polysynth_mix_controls_tests)
add_test(NAME webview_gui_examples_polysynth_filter_envelope
    COMMAND webview_gui_example_polysynth_filter_envelope_tests)
add_test(NAME webview_gui_examples_polysynth_oscillator_tuning
    COMMAND webview_gui_example_polysynth_oscillator_tuning_tests)
add_test(NAME webview_gui_examples_polysynth_polyphonic_parameter_state
    COMMAND webview_gui_example_polysynth_polyphonic_parameter_state_tests)
add_test(NAME webview_gui_examples_polysynth_event_stream
    COMMAND webview_gui_example_polysynth_event_stream_tests)
add_test(NAME webview_gui_examples_polysynth_parameter_model
    COMMAND webview_gui_example_polysynth_parameter_model_tests)
add_test(NAME webview_gui_examples_polysynth_parameter_event
    COMMAND webview_gui_example_polysynth_parameter_event_tests)
add_test(NAME webview_gui_examples_polysynth_note_expression
    COMMAND webview_gui_example_polysynth_note_expression_tests)
add_test(NAME webview_gui_examples_polysynth_pan_expression
    COMMAND webview_gui_example_polysynth_pan_expression_tests)
add_test(NAME webview_gui_examples_polysynth_brightness_expression
    COMMAND webview_gui_example_polysynth_brightness_expression_tests)
add_test(NAME webview_gui_examples_polysynth_expression_expression
    COMMAND webview_gui_example_polysynth_expression_expression_tests)
add_test(NAME webview_gui_examples_polysynth_pressure_expression
    COMMAND webview_gui_example_polysynth_pressure_expression_tests)
add_test(NAME webview_gui_examples_polysynth_note_expression_stream
    COMMAND webview_gui_example_polysynth_note_expression_stream_tests)
add_test(NAME webview_gui_examples_polysynth_clap_ports
    COMMAND webview_gui_example_polysynth_clap_ports_tests)
add_test(NAME webview_gui_examples_polysynth_param_flush_polyphony
    COMMAND webview_gui_example_polysynth_param_flush_polyphony_tests)
add_test(NAME webview_gui_examples_polysynth_voice_info
    COMMAND webview_gui_example_polysynth_voice_info_tests)
add_test(NAME webview_gui_examples_polysynth_state
    COMMAND webview_gui_example_polysynth_state_tests)
add_test(NAME webview_gui_examples_polysynth_state_snapshot_concurrency
    COMMAND webview_gui_example_polysynth_state_snapshot_concurrency_tests)
add_test(NAME webview_gui_examples_polysynth_state_context
    COMMAND webview_gui_example_polysynth_state_context_tests)
add_test(NAME webview_gui_examples_polysynth_tail
    COMMAND webview_gui_example_polysynth_tail_tests)
