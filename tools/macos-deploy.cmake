foreach(_name APP_BUNDLE APP_EXECUTABLE QML_DIR TRANSLATIONS_DIR PMAC_LIBRARY TERMINAL_CORE_LIBRARY MACDEPLOYQT)
    if(NOT DEFINED ${_name} OR "${${_name}}" STREQUAL "")
        message(FATAL_ERROR "Missing required argument: ${_name}")
    endif()
endforeach()

function(run_command)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _result)
    if(NOT _result STREQUAL "0")
        string(JOIN " " _command ${ARGN})
        message(FATAL_ERROR "Command failed (${_result}): ${_command}")
    endif()
endfunction()

set(_contents "${APP_BUNDLE}/Contents")
set(_frameworks "${_contents}/Frameworks")
set(_macos "${_contents}/MacOS")
set(_bundled_pmac "${_frameworks}/libpmac.dylib")
set(_bundled_core "${_frameworks}/libpmac_terminal_core.dylib")
file(MAKE_DIRECTORY "${_frameworks}")
file(REMOVE "${_macos}/libpmac.dylib" "${_macos}/libpmac_terminal_core.dylib")
file(COPY "${PMAC_LIBRARY}" DESTINATION "${_frameworks}")
# 清理旧版本放在 MacOS 下的资源；所有 bundle 内容必须在签名前就位。
file(REMOVE_RECURSE "${_macos}/translations")
file(COPY "${TRANSLATIONS_DIR}" DESTINATION "${_contents}/Resources")

run_command(install_name_tool -id "@rpath/libpmac.dylib" "${_bundled_pmac}")
execute_process(
    COMMAND otool -L "${APP_EXECUTABLE}"
    OUTPUT_VARIABLE _dependencies
    ERROR_VARIABLE _otool_error
    RESULT_VARIABLE _otool_result
)
if(NOT _otool_result STREQUAL "0")
    message(FATAL_ERROR "otool failed: ${_otool_error}")
endif()
string(REPLACE "\n" ";" _dependency_lines "${_dependencies}")
set(_pmac_dependency "")
foreach(_line IN LISTS _dependency_lines)
    if(_line MATCHES "^[ \t]*(.+libpmac\\.dylib)[ \t]+\\(")
        set(_pmac_dependency "${CMAKE_MATCH_1}")
        break()
    endif()
endforeach()
if(NOT _pmac_dependency)
    message(FATAL_ERROR "The application does not link to libpmac.dylib")
endif()
if(NOT _pmac_dependency STREQUAL "@rpath/libpmac.dylib")
    run_command(install_name_tool -change "${_pmac_dependency}"
                "@rpath/libpmac.dylib" "${APP_EXECUTABLE}")
endif()

run_command("${MACDEPLOYQT}" "${APP_BUNDLE}" "-qmldir=${QML_DIR}"
            -always-overwrite -codesign=-)

execute_process(
    COMMAND otool -l "${APP_EXECUTABLE}"
    OUTPUT_VARIABLE _load_commands
    ERROR_VARIABLE _otool_error
    RESULT_VARIABLE _otool_result
)
if(NOT _otool_result STREQUAL "0")
    message(FATAL_ERROR "otool failed: ${_otool_error}")
endif()
string(FIND "${_load_commands}" "path @executable_path/../Frameworks" _frameworks_rpath)
if(_frameworks_rpath EQUAL -1)
    run_command(install_name_tool -add_rpath "@executable_path/../Frameworks"
                "${APP_EXECUTABLE}")
endif()

file(COPY "${TERMINAL_CORE_LIBRARY}" DESTINATION "${_frameworks}")
run_command(install_name_tool -id "@rpath/libpmac_terminal_core.dylib" "${_bundled_core}")

execute_process(
    COMMAND otool -L "${APP_EXECUTABLE}"
    OUTPUT_VARIABLE _dependencies
    ERROR_VARIABLE _otool_error
    RESULT_VARIABLE _otool_result
)
if(NOT _otool_result STREQUAL "0")
    message(FATAL_ERROR "otool failed: ${_otool_error}")
endif()
string(REPLACE "\n" ";" _dependency_lines "${_dependencies}")
set(_qt_modules "")
foreach(_line IN LISTS _dependency_lines)
    if(_line MATCHES "^[ \t]*@rpath/(Qt[A-Za-z0-9_]+)\\.framework/Versions/A/([A-Za-z0-9_]+)[ \t]+\\(")
        if(NOT CMAKE_MATCH_1 STREQUAL CMAKE_MATCH_2)
            message(FATAL_ERROR "Mismatched Qt framework dependency: ${_line}")
        endif()
        list(APPEND _qt_modules "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^[ \t]*[^ \t]*Qt[^ \t]*\\.framework")
        message(FATAL_ERROR "Invalid Qt framework dependency after macdeployqt: ${_line}")
    endif()
endforeach()
foreach(_required_module QtCore QtQuick)
    list(FIND _qt_modules "${_required_module}" _module_index)
    if(_module_index EQUAL -1)
        message(FATAL_ERROR "Missing Qt dependency: ${_required_module}")
    endif()
endforeach()
foreach(_module IN LISTS _qt_modules)
    set(_framework_binary
        "${_frameworks}/${_module}.framework/Versions/A/${_module}")
    if(NOT EXISTS "${_framework_binary}")
        message(FATAL_ERROR "Missing bundled Qt framework: ${_framework_binary}")
    endif()
endforeach()
foreach(_library IN ITEMS "${_bundled_pmac}" "${_bundled_core}")
    if(NOT EXISTS "${_library}")
        message(FATAL_ERROR "Missing bundled library: ${_library}")
    endif()
endforeach()

run_command(codesign --force --sign - "${_bundled_pmac}")
run_command(codesign --force --sign - "${_bundled_core}")
run_command(codesign --force --deep --sign - "${APP_BUNDLE}")
run_command(codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}")
