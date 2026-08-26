if(NOT DEFINED MODULE OR NOT EXISTS "${MODULE}")
    message(FATAL_ERROR "MODULE must name the built plug-in qualification module")
endif()

if(PLATFORM STREQUAL "Windows")
    # The Windows CI profile currently uses MinGW/Ninja, where dumpbin is not
    # on PATH. CMake's configured GNU objdump can read the PE export directory
    # directly and, unlike a full symbol-table scan, reports the ABI boundary
    # that a DAW/other DLL can actually resolve.
    if(NOT DEFINED OBJDUMP OR OBJDUMP STREQUAL "")
        message(FATAL_ERROR "CMAKE_OBJDUMP is required for the Windows export scan")
    endif()
    execute_process(
        COMMAND "${OBJDUMP}" -p "${MODULE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE exports
        ERROR_VARIABLE errors
    )
elseif(PLATFORM STREQUAL "Darwin")
    execute_process(
        COMMAND "${NM}" -gU "${MODULE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE exports
        ERROR_VARIABLE errors
    )
else()
    execute_process(
        COMMAND "${NM}" -D --defined-only "${MODULE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE exports
        ERROR_VARIABLE errors
    )
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "symbol scan failed (${result}): ${errors}")
endif()

set(exported_symbols)

if(PLATFORM STREQUAL "Windows")
    # objdump -p contains several PE tables. Only parse the export name-pointer
    # table; other bracketed rows describe RVAs rather than exported names.
    string(FIND "${exports}" "[Ordinal/Name Pointer] Table" export_table_offset)
    if(export_table_offset EQUAL -1)
        message(FATAL_ERROR "PE export name table was not found:\n${exports}")
    endif()

    string(SUBSTRING "${exports}" ${export_table_offset} -1 export_name_table)
    string(REPLACE "\r\n" "\n" export_name_table "${export_name_table}")
    string(REPLACE "\r" "\n" export_name_table "${export_name_table}")
    string(REPLACE "\n" ";" export_lines "${export_name_table}")

    set(seen_export_name FALSE)
    foreach(line IN LISTS export_lines)
        if(line MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+([^ \t]+)[ \t]*$")
            list(APPEND exported_symbols "${CMAKE_MATCH_1}")
            set(seen_export_name TRUE)
        elseif(seen_export_name)
            string(STRIP "${line}" stripped_line)
            if(stripped_line STREQUAL "")
                break()
            endif()
        endif()
    endforeach()
else()
    string(REPLACE "\r\n" "\n" normalized_exports "${exports}")
    string(REPLACE "\r" "\n" normalized_exports "${normalized_exports}")
    string(REPLACE "\n" ";" export_lines "${normalized_exports}")

    foreach(line IN LISTS export_lines)
        # nm output is: address, symbol type, symbol name. The qualification
        # modules deliberately use C ABI entry points so no demangling is needed.
        if(line MATCHES "^[^ \t]+[ \t]+[A-Za-z][ \t]+([^ \t]+)[ \t]*$")
            set(symbol "${CMAKE_MATCH_1}")
            if(PLATFORM STREQUAL "Darwin" AND symbol MATCHES "^_")
                string(SUBSTRING "${symbol}" 1 -1 symbol)
            endif()
            list(APPEND exported_symbols "${symbol}")
        endif()
    endforeach()
endif()

list(REMOVE_DUPLICATES exported_symbols)
set(allowed_export "webview_gui_plugin_test_entry")
list(FIND exported_symbols "${allowed_export}" allowed_export_index)
if(allowed_export_index EQUAL -1)
    message(FATAL_ERROR
        "expected plug-in ABI entry point was not exported; parsed exports: ${exported_symbols}\n${exports}")
endif()

foreach(symbol IN LISTS exported_symbols)
    if(NOT symbol STREQUAL allowed_export)
        message(FATAL_ERROR
            "unexpected exported symbol '${symbol}'; only '${allowed_export}' is allowed:\n${exports}")
    endif()
endforeach()
