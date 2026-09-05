include_guard(GLOBAL)

function(webview_gui_add_web_resources)
    set(options)
    set(oneValueArgs TARGET SOURCE_DIR NAMESPACE)
    cmake_parse_arguments(WEB "${options}" "${oneValueArgs}" "" ${ARGN})

    foreach(_required IN ITEMS TARGET SOURCE_DIR NAMESPACE)
        if(NOT WEB_${_required})
            message(FATAL_ERROR "webview_gui_add_web_resources requires ${_required}")
        endif()
    endforeach()
    if(TARGET "${WEB_TARGET}")
        message(FATAL_ERROR "webview_gui_add_web_resources target already exists: ${WEB_TARGET}")
    endif()
    if(NOT IS_DIRECTORY "${WEB_SOURCE_DIR}")
        message(FATAL_ERROR "Web UI source directory does not exist: ${WEB_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${WEB_SOURCE_DIR}/package.json" OR
       NOT EXISTS "${WEB_SOURCE_DIR}/package-lock.json" OR
       NOT EXISTS "${WEB_SOURCE_DIR}/vite.config.mjs")
        message(FATAL_ERROR
            "${WEB_SOURCE_DIR} must contain package.json, package-lock.json and vite.config.mjs")
    endif()

    find_program(WEBVIEW_GUI_NODE_EXECUTABLE NAMES node REQUIRED)
    find_program(WEBVIEW_GUI_NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)

    execute_process(
        COMMAND "${WEBVIEW_GUI_NODE_EXECUTABLE}" --version
        OUTPUT_VARIABLE _node_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _node_result
    )
    if(NOT _node_result EQUAL 0)
        message(FATAL_ERROR "Unable to execute Node.js: ${WEBVIEW_GUI_NODE_EXECUTABLE}")
    endif()
    string(REGEX REPLACE "^v" "" _node_version "${_node_version}")
    if(_node_version VERSION_LESS "20.19.0")
        message(FATAL_ERROR
            "Example web UIs require Node.js >= 20.19.0; found ${_node_version}")
    endif()

    set(_root "${CMAKE_CURRENT_BINARY_DIR}/web-resources/${WEB_TARGET}")
    set(_workspace "${_root}/workspace")
    set(_app "${_workspace}/app")
    set(_dist "${_root}/dist")
    set(_generated "${_root}/generated")
    set(_install_stamp "${_root}/npm-ci.stamp")
    set(_build_stamp "${_root}/vite.stamp")
    set(_header "${_generated}/${WEB_TARGET}_resources.h")
    set(_source "${_generated}/${WEB_TARGET}_resources.cpp")

    # CMake resolves WORKING_DIRECTORY before executing a custom command, so
    # this directory must already exist at generate time rather than relying on
    # the first command in the rule to create it.
    file(MAKE_DIRECTORY "${_workspace}")

    file(GLOB_RECURSE _web_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
        "${WEB_SOURCE_DIR}/*")
    list(FILTER _web_inputs EXCLUDE REGEX "/(node_modules|dist)/")

    add_custom_command(
        OUTPUT "${_install_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_workspace}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${WEB_SOURCE_DIR}/package.json" "${_workspace}/package.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${WEB_SOURCE_DIR}/package-lock.json" "${_workspace}/package-lock.json"
        COMMAND "${WEBVIEW_GUI_NPM_EXECUTABLE}" ci --ignore-scripts --no-audit --no-fund
        COMMAND "${CMAKE_COMMAND}" -E touch "${_install_stamp}"
        DEPENDS "${WEB_SOURCE_DIR}/package.json" "${WEB_SOURCE_DIR}/package-lock.json"
        WORKING_DIRECTORY "${_workspace}"
        COMMENT "Installing pinned frontend dependencies for ${WEB_TARGET}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${_header}" "${_source}" "${_build_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_app}" "${_dist}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_app}" "${_dist}" "${_generated}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${WEB_SOURCE_DIR}" "${_app}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_app}/node_modules" "${_app}/dist"
        COMMAND "${CMAKE_COMMAND}" -E env WEBVIEW_GUI_UI_SOURCEMAPS=0
            "${WEBVIEW_GUI_NPM_EXECUTABLE}" --prefix "${_workspace}" exec --
            vite build "${_app}" --config "${_app}/vite.config.mjs"
            --outDir "${_dist}" --emptyOutDir
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_DIR=${_dist}"
            "-DOUTPUT_HEADER=${_header}"
            "-DOUTPUT_SOURCE=${_source}"
            "-DNAMESPACE=${WEB_NAMESPACE}"
            "-DHEADER_BASENAME=${WEB_TARGET}_resources.h"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateWebResources.cmake"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_build_stamp}"
        DEPENDS ${_web_inputs} "${_install_stamp}"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateWebResources.cmake"
        COMMENT "Building and embedding Vite resources for ${WEB_TARGET}"
        VERBATIM
    )

    add_custom_target("${WEB_TARGET}_dist" DEPENDS "${_header}" "${_source}" "${_build_stamp}")
    add_library("${WEB_TARGET}" STATIC "${_source}")
    add_dependencies("${WEB_TARGET}" "${WEB_TARGET}_dist")
    target_include_directories("${WEB_TARGET}" PUBLIC "${_generated}")
    target_compile_features("${WEB_TARGET}" PUBLIC cxx_std_17)
    set_target_properties("${WEB_TARGET}" PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        WEBVIEW_GUI_WEB_DIST_DIR "${_dist}"
        WEBVIEW_GUI_WEB_DIST_STAMP "${_build_stamp}"
        WEBVIEW_GUI_WEB_DIST_TARGET "${WEB_TARGET}_dist"
    )
endfunction()
