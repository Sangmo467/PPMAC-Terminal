foreach(_name APP_BUNDLE APP_EXECUTABLE QML_DIR PMAC_LIBRARY TERMINAL_CORE_LIBRARY MACDEPLOYQT)
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

file(COPY "${TERMINAL_CORE_LIBRARY}" DESTINATION "${_frameworks}")
run_command(install_name_tool -id "@rpath/libpmac_terminal_core.dylib" "${_bundled_core}")
run_command(codesign --force --sign - "${_bundled_pmac}")
run_command(codesign --force --sign - "${_bundled_core}")
run_command(codesign --force --deep --sign - "${APP_BUNDLE}")
run_command(codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}")
