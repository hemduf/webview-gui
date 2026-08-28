if(NOT DEFINED MODULE OR NOT EXISTS "${MODULE}")
    message(FATAL_ERROR "MODULE must name the built plug-in qualification module")
endif()

set(windows_export_tool "")
if(PLATFORM STREQUAL "Windows")
    # Prefer the native MSVC export scanner when it is available. CMake may
    # expose an LLVM/GNU objdump in an MSVC/Ninja environment whose PE output
    # differs across toolchain versions; dumpbin has the canonical MSVC
    # /EXPORTS layout parsed below. Keep objdump as the non-MSVC fallback.
    find_program(DUMPBIN_EXECUTABLE
        NAMES dumpbin dumpbin.exe
        HINTS
            "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
            "$ENV{VCToolsInstallDir}/bin/Hostx86/x86")

    if(DUMPBIN_EXECUTABLE)
        set(windows_export_tool "dumpbin")
        execute_process(
            COMMAND "${DUMPBIN_EXECUTABLE}" /NOLOGO /EXPORTS "${MODULE}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE exports
            ERROR_VARIABLE errors)
    else()
        if(DEFINED OBJDUMP
           AND NOT OBJDUMP STREQUAL ""
           AND NOT OBJDUMP MATCHES "-NOTFOUND$"
           AND EXISTS "${OBJDUMP}")
            set(OBJDUMP_EXECUTABLE "${OBJDUMP}")
        else()
            find_program(OBJDUMP_EXECUTABLE NAMES llvm-objdump objdump)
        endif()

        if(NOT OBJDUMP_EXECUTABLE)
            message(FATAL_ERROR
                "Windows export scan requires dumpbin or llvm-objdump/objdump; "
                "CMAKE_OBJDUMP='${OBJDUMP}', VCToolsInstallDir='$ENV{VCToolsInstallDir}'")
        endif()

        set(windows_export_tool "objdump")
        execute_process(
            COMMAND "${OBJDUMP_EXECUTABLE}" -p "${MODULE}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE exports
            ERROR_VARIABLE errors)
    endif()
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

if(PLATFORM STREQUAL "Windows" AND windows_export_tool STREQUAL "objdump")
    # GNU objdump and LLVM llvm-objdump intentionally use different PE export
    # layouts. Parse only the dedicated export-name table in either format so
    # import/debug tables cannot be mistaken for plug-in ABI symbols.
    string(REPLACE "\r\n" "\n" normalized_exports "${exports}")
    string(REPLACE "\r" "\n" normalized_exports "${normalized_exports}")
    string(FIND "${normalized_exports}" "[Ordinal/Name Pointer] Table" gnu_export_table_offset)

    if(NOT gnu_export_table_offset EQUAL -1)
        string(SUBSTRING "${normalized_exports}" ${gnu_export_table_offset} -1 export_name_table)
        string(REPLACE "\n" ";" export_lines "${export_name_table}")

        set(seen_export_name FALSE)
        foreach(line IN LISTS export_lines)
            # GNU binutils has two PE name-table layouts in current toolchains:
            #   [ 0] exported_symbol
            #   [ 0] +base[ 1] 0000 exported_symbol
            # The GitHub Windows runner currently uses the latter through
            # CMAKE_OBJDUMP. Keep both forms explicit and scoped to the export
            # name table so import/debug rows cannot be accepted as ABI names.
            if(line MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+\\+base\\[[ \t]*[0-9]+\\][ \t]+[0-9A-Fa-f]+[ \t]+([^ \t]+)[ \t]*$")
                list(APPEND exported_symbols "${CMAKE_MATCH_1}")
                set(seen_export_name TRUE)
            elseif(line MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+([^ \t]+)[ \t]*$")
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
        # LLVM's PE printer emits:
        #   Export Table:
        #    Ordinal      RVA  Name
        #          1   0x1234  exported_symbol
        # Scope parsing to that table and capture exactly the first name token.
        string(REPLACE "\n" ";" export_lines "${normalized_exports}")
        set(in_llvm_export_table FALSE)
        set(in_llvm_export_rows FALSE)
        foreach(line IN LISTS export_lines)
            if(line MATCHES "^[ \t]*Export Table:[ \t]*$")
                set(in_llvm_export_table TRUE)
            elseif(in_llvm_export_table
                   AND line MATCHES "^[ \t]*Ordinal[ \t]+RVA[ \t]+Name[ \t]*$")
                set(in_llvm_export_rows TRUE)
            elseif(in_llvm_export_rows
                   AND line MATCHES "^[ \t]*[0-9]+[ \t]+0x[0-9A-Fa-f]+[ \t]+([^ \t]+)([ \t].*)?$")
                list(APPEND exported_symbols "${CMAKE_MATCH_1}")
            elseif(in_llvm_export_rows)
                string(STRIP "${line}" stripped_line)
                if(stripped_line STREQUAL "" AND exported_symbols)
                    break()
                endif()
            endif()
        endforeach()

        if(NOT in_llvm_export_rows)
            message(FATAL_ERROR "PE export table was not found:\n${exports}")
        endif()
    endif()
elseif(PLATFORM STREQUAL "Windows")
    # dumpbin /EXPORTS rows are: ordinal, hex hint, RVA, exported name. Start
    # only after its column header so summary/import text cannot be mistaken for
    # a plug-in ABI symbol. Forwarded exports still expose the fourth token.
    string(REPLACE "\r\n" "\n" normalized_exports "${exports}")
    string(REPLACE "\r" "\n" normalized_exports "${normalized_exports}")
    string(REPLACE "\n" ";" export_lines "${normalized_exports}")
    set(in_export_rows FALSE)
    foreach(line IN LISTS export_lines)
        if(line MATCHES "^[ \t]*ordinal[ \t]+hint[ \t]+RVA[ \t]+name[ \t]*$")
            set(in_export_rows TRUE)
        elseif(in_export_rows
               AND line MATCHES "^[ \t]*[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t=]+)")
            list(APPEND exported_symbols "${CMAKE_MATCH_1}")
        elseif(in_export_rows)
            string(STRIP "${line}" stripped_line)
            if(stripped_line STREQUAL "")
                # dumpbin emits a blank line after the export rows.
                if(exported_symbols)
                    break()
                endif()
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
