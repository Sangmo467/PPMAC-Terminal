cmake_minimum_required(VERSION 3.21.1)

project(PmacTerminalApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 COMPONENTS Gui Qml Quick)
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick Widgets PrintSupport)

if (Qt6_VERSION VERSION_LESS 6.2.0)
message(FATAL_ERROR "You need Qt 6.2.0 or newer to build the application.")
endif()

qt_add_executable(${CMAKE_PROJECT_NAME} src/main.cpp)

# 纯 GUI 程序：Windows 不显示控制台窗口，macOS 生成 .app
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
if (APPLE)
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "PmacTerminal"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.sangmo.pmacterminal"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "PmacTerminal.icns"
    )
endif ()

# 插件已在 src/import_qml_plugins.h 中显式 Q_IMPORT_QML_PLUGIN；
# 关闭自动扫描以兼容非 ASCII 源码路径（qmlimportscanner 对中文路径处理有缺陷）
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

if (${QT_VERSION_MINOR} GREATER_EQUAL 3)
qt6_standard_project_setup()
endif()

qt_add_resources(${CMAKE_PROJECT_NAME} "configuration"
    PREFIX "/"
    FILES
        qtquickcontrols2.conf
)

# 程序图标（窗口标题栏 / 任务栏）；exe 图标另见 icons/app.rc
qt_add_resources(${CMAKE_PROJECT_NAME} "appicon"
    PREFIX "/"
    FILES
        icons/PmacTerminal.png
)

# Windows exe 图标（资源管理器 / 快捷方式）
if (WIN32)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE icons/app.rc)
endif()

# macOS 应用图标：由 PNG 生成 .icns（需构建机自带 sips/iconutil）
if (APPLE)
    set(_icns "${CMAKE_CURRENT_BINARY_DIR}/PmacTerminal.icns")
    set(_iconset "${CMAKE_CURRENT_BINARY_DIR}/PmacTerminal.iconset")
    set(_icon_png "${CMAKE_CURRENT_SOURCE_DIR}/icons/PmacTerminal.png")
    add_custom_command(OUTPUT "${_icns}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_iconset}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_iconset}"
        COMMAND sips -z 16 16 "${_icon_png}" --out "${_iconset}/icon_16x16.png"
        COMMAND sips -z 32 32 "${_icon_png}" --out "${_iconset}/icon_16x16@2x.png"
        COMMAND sips -z 32 32 "${_icon_png}" --out "${_iconset}/icon_32x32.png"
        COMMAND sips -z 64 64 "${_icon_png}" --out "${_iconset}/icon_32x32@2x.png"
        COMMAND sips -z 128 128 "${_icon_png}" --out "${_iconset}/icon_128x128.png"
        COMMAND sips -z 256 256 "${_icon_png}" --out "${_iconset}/icon_128x128@2x.png"
        COMMAND sips -z 256 256 "${_icon_png}" --out "${_iconset}/icon_256x256.png"
        COMMAND sips -z 512 512 "${_icon_png}" --out "${_iconset}/icon_256x256@2x.png"
        COMMAND sips -z 512 512 "${_icon_png}" --out "${_iconset}/icon_512x512.png"
        COMMAND sips -z 1024 1024 "${_icon_png}" --out "${_iconset}/icon_512x512@2x.png"
        COMMAND iconutil -c icns "${_iconset}" -o "${_icns}"
        DEPENDS "${_icon_png}"
        VERBATIM)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${_icns}")
    set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# 界面与后台消息翻译（QML 与 C++ 共用同一份 JSON）。
# 新增语言：translations/ 下加 <code>.json 并在 languages.json 中登记，再把文件加到这里。
qt_add_resources(${CMAKE_PROJECT_NAME} "translations"
    PREFIX "/"
    FILES
        translations/languages.json
        translations/zh.json
        translations/zh_TW.json
        translations/en.json
        translations/hi.json
        translations/ja.json
        translations/es.json
        translations/de.json
        translations/fr.json
)

# Reuse the Rust terminal C ABI and its tested Qt editor.
set(PMACT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pmac-terminal-rs"
    CACHE PATH "PMAC terminal source/distribution")
if (WIN32)
    set(_pmact_core_name "pmac_terminal_core.dll")
elseif (APPLE)
    set(_pmact_core_name "libpmac_terminal_core.dylib")
else ()
    set(_pmact_core_name "libpmac_terminal_core.so")
endif ()
if (NOT PMACT_CORE_LIB)
    foreach (_candidate
             "${PMACT_ROOT}/dist/${_pmact_core_name}"
             "${PMACT_ROOT}/target/release/${_pmact_core_name}")
        if (EXISTS "${_candidate}")
            set(PMACT_CORE_LIB "${_candidate}")
            break()
        endif ()
    endforeach ()
endif ()
set(PMACT_CORE_LIB "${PMACT_CORE_LIB}" CACHE FILEPATH "Built Rust terminal core")
if (NOT PMACT_CORE_LIB OR NOT EXISTS "${PMACT_CORE_LIB}" OR NOT EXISTS "${PMACT_ROOT}/include/pmac_terminal.h")
    message(FATAL_ERROR
        "Build the Rust terminal core first: cargo build --release --lib --manifest-path "
        "${PMACT_ROOT}/Cargo.toml, or set PMACT_ROOT / PMACT_CORE_LIB.")
endif ()
if (NOT APPLE)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PMACT_CORE_LIB}" "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${_pmact_core_name}"
    )
endif ()

# PMAC 2.0.7 SDK（Windows MSVC x64 / macOS ARM64 / Linux x64）。仓库不包含预编译 SDK，
# 需下载后放到源码同级目录，或通过 -DPMAC_SDK_DIR=<路径> 指定。
if (WIN32)
    set(_pmac_sdk_default "${CMAKE_CURRENT_SOURCE_DIR}/pmac-2.0.7-x86_64-pc-windows-msvc-release")
    set(_pmac_native_lib "native/pmac.dll")
elseif (APPLE)
    set(_pmac_sdk_default "${CMAKE_CURRENT_SOURCE_DIR}/pmac-2.0.7-aarch64-apple-darwin-release")
    set(_pmac_native_lib "native/libpmac.dylib")
else ()
    set(_pmac_sdk_default "${CMAKE_CURRENT_SOURCE_DIR}/pmac-2.0.7-x86_64-unknown-linux-gnu-release")
    set(_pmac_native_lib "native/libpmac.so")
endif ()
set(PMAC_SDK_DIR "${_pmac_sdk_default}" CACHE PATH "PMAC 2.0.7 SDK directory")
if (NOT EXISTS "${PMAC_SDK_DIR}/cmake/PmacConfig.cmake" OR NOT EXISTS "${PMAC_SDK_DIR}/${_pmac_native_lib}")
    message(FATAL_ERROR
        "PMAC SDK not found at ${PMAC_SDK_DIR}. Download the matching pmac-2.0.7-*-release "
        "package and pass -DPMAC_SDK_DIR=<path>.")
endif ()

# C++ 后台层（独立 QML 模块，QML 中 import PmacTerminal.Backend）
add_subdirectory(src/backend)

# 部署 PMAC 运行库，随后由 package 收集到 dist。
if (NOT APPLE)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PMAC_SDK_DIR}/${_pmac_native_lib}"
            "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>"
    )
endif ()

# 自动部署 Qt 运行时并清理非必要文件，构建完成即得到可直接运行的干净目录
option(DEPLOY_SOFTWARE_OPENGL
       "Deploy opengl32sw.dll (20MB). Needed only on machines without a GPU / over plain RDP" OFF)
option(DEPLOY_VC_REDIST
       "Deploy vc_redist.x64.exe (25MB). Needed only for PCs without the MSVC runtime installed" OFF)
if (WIN32)
    get_target_property(WINDEPLOYQT_EXE Qt6::windeployqt IMPORTED_LOCATION)
endif ()
if (WIN32 AND WINDEPLOYQT_EXE)
    # --no-system-d3d-compiler 不带 D3Dcompiler_47.dll（4MB，Basic 样式用预编译着色器，不需要）
    # --no-system-dxc-compiler 不带 dxcompiler/dxil
    # --no-ffmpeg             不带 FFmpeg（工程不用多媒体）
    # --skip-plugin-types     不部署 qmltooling/generic/networkinformation 等插件
    if (DEPLOY_SOFTWARE_OPENGL)
        set(_NO_OPENGL_SW)
    else()
        set(_NO_OPENGL_SW --no-opengl-sw)
    endif()
    if (DEPLOY_VC_REDIST)
        set(_NO_VC_REDIST)
    else()
        set(_NO_VC_REDIST --no-compiler-runtime)
    endif()
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${WINDEPLOYQT_EXE}
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations
                ${_NO_OPENGL_SW}
                ${_NO_VC_REDIST}
                --no-system-d3d-compiler
                --no-system-dxc-compiler
                --skip-plugin-types qmltooling,generic,networkinformation,tls,iconengines,imageformats,platforminputcontexts
                --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/content
                $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        # 删除链接产物
        COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.lib
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.exp
        # 删除未使用的样式库与组件库
        COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/dxcompiler.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/dxil.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/D3Dcompiler_47.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6VirtualKeyboard.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6Svg.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickShapes.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickEffects.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6Quick3DUtils.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2Fusion.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2FusionStyleImpl.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2Imagine.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2ImagineStyleImpl.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2Material.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2MaterialStyleImpl.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2Universal.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2UniversalStyleImpl.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2FluentWinUI3StyleImpl.dll
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Qt6QuickControls2WindowsStyleImpl.dll
        # 删除未使用的插件目录与类型信息（plugins.qmltypes 仅供 IDE/qmllint，运行时无用）
        COMMAND ${CMAKE_COMMAND} -E rm -rf
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/translations
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qmltooling
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/tls
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/networkinformation
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/platforminputcontexts
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/generic
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/iconengines
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/imageformats
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/Fusion
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/Imagine
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/Material
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/Universal
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/FluentWinUI3
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/Windows
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Effects
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Shapes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/NativeStyle
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQml/WorkerScript
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQml/Models
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQml/StateMachine
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQml/XmlListModel
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Dialogs
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/LocalStorage
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Particles
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Scene2D
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Scene3D
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Timeline
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/VectorImage
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/VirtualKeyboard
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QML
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/tooling
        # plugins.qmltypes：类型元数据，只给工具链用
        COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQml/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/Basic/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Controls/impl/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Templates/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Layouts/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Window/plugins.qmltypes
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/qml/QtQuick/Window/quickwindow.qmltypes
    )
    # 旧版 windeployqt 可能不认识 --no-opengl-sw / --no-compiler-runtime，统一部署后清理。
    if (NOT DEPLOY_SOFTWARE_OPENGL)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                    $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/opengl32sw.dll
        )
    endif ()
    if (NOT DEPLOY_VC_REDIST)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                    $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/vc_redist.x64.exe
        )
    endif ()
endif ()

# macOS：业务库放入 Frameworks，再部署 Qt、修正依赖并验证签名。
if (APPLE)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    find_program(MACDEPLOYQT_EXE macdeployqt HINTS "${_qt_bin_dir}")
    if (NOT MACDEPLOYQT_EXE)
        message(FATAL_ERROR "macdeployqt not found. Check CMAKE_PREFIX_PATH / Qt installation.")
    endif ()
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DAPP_BUNDLE=$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>"
                "-DAPP_EXECUTABLE=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                "-DQML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/content"
                "-DPMAC_LIBRARY=${PMAC_SDK_DIR}/${_pmac_native_lib}"
                "-DTERMINAL_CORE_LIBRARY=${PMACT_CORE_LIB}"
                "-DMACDEPLOYQT=${MACDEPLOYQT_EXE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-deploy.cmake"
        COMMENT "Deploying and verifying the macOS app bundle"
    )
endif ()

# package 目标：把运行必需的文件收集到 dist/（不含构建中间产物），用于分发。
# ALL：随默认构建一起执行，Qt Creator 里每次“构建”后 dist/ 自动更新。
if (APPLE)
    add_custom_target(package ALL
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
        COMMAND ditto "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>"
                "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CMAKE_PROJECT_NAME}.app"
        DEPENDS ${CMAKE_PROJECT_NAME}
        COMMENT "Collecting macOS app bundle into dist/"
    )
else ()
    add_custom_target(package ALL
        COMMAND ${CMAKE_COMMAND}
                -D "SRC=$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>"
                -D "DST=${CMAKE_CURRENT_SOURCE_DIR}/dist"
                -D "EXE=${CMAKE_PROJECT_NAME}.exe"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/package.cmake
        DEPENDS ${CMAKE_PROJECT_NAME}
        COMMENT "Collecting runtime files into dist/"
    )
endif ()

# 翻译文件复制到 exe 旁（须晚于上面 windeployqt 清理步骤中对 translations 目录的删除）。
# 运行时优先读该目录，内置 qrc 仅作回退——不重新编译即可修改/新增语言。
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/translations
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/translations
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml
    Qt6::Widgets
    Qt6::PrintSupport
    PmacTerminalBackendplugin
)

# Windows：深色标题栏（DwmSetWindowAttribute）
if (WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE dwmapi)
endif()

# macOS：libpmac.dylib / Rust 核心位于 Contents/Frameworks。
if (APPLE)
    get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(_qt_lib_dir "${_qt_core_location}" DIRECTORY)
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks;${_qt_lib_dir}"
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/qmlmodules)

install(TARGETS ${CMAKE_PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
