# C++ 后台层，QML 中通过 import PmacTerminal.Backend 使用
qt_add_library(PmacTerminalBackend STATIC)
set_target_properties(PmacTerminalBackend PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

qt_add_qml_module(PmacTerminalBackend
    URI "PmacTerminal.Backend"
    VERSION 1.0
    RESOURCE_PREFIX "/"
    SOURCES
        TerminalPage.h
        TerminalPage.cpp
        TerminalWidgets.h
        WatchPanel.h
        WatchPanel.cpp
        NativeMenu.h
        NativeMenu.cpp
        RuntimePaths.h
        PmacConnection.h
        Backend.h
        Backend.cpp
        Translator.h
        Translator.cpp
)

# 使用顶层通过 -DPMAC_SDK_DIR 指定的 pmac 2.0.7 SDK（按平台选择默认目录）。
# 清除上一次配置缓存的 Pmac_DIR，否则 SDK 升级后 find_package 仍会命中旧版本目录。
unset(Pmac_DIR CACHE)
if(NOT PMAC_SDK_DIR)
    if(WIN32)
        set(PMAC_SDK_DIR "${CMAKE_SOURCE_DIR}/pmac-2.0.7-x86_64-pc-windows-msvc-release")
    elseif(APPLE)
        set(PMAC_SDK_DIR "${CMAKE_SOURCE_DIR}/pmac-2.0.7-aarch64-apple-darwin-release")
    else()
        set(PMAC_SDK_DIR "${CMAKE_SOURCE_DIR}/pmac-2.0.7-x86_64-unknown-linux-gnu-release")
    endif()
endif()
find_package(Pmac CONFIG REQUIRED PATHS "${PMAC_SDK_DIR}/cmake" NO_DEFAULT_PATH)
set_target_properties(Pmac::pmac PROPERTIES IMPORTED_GLOBAL TRUE)
target_compile_features(PmacTerminalBackend PUBLIC cxx_std_17)
target_link_libraries(PmacTerminalBackend PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::Quick
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Network
    Pmac::pmac
)

target_include_directories(PmacTerminalBackend PRIVATE "${PMACT_ROOT}/include")
target_compile_definitions(PmacTerminalBackend PRIVATE PMACT_DYNAMIC)
if(MSVC)
    target_compile_options(PmacTerminalBackend PRIVATE /utf-8)
endif()
if(WIN32)
    target_link_libraries(PmacTerminalBackend PUBLIC user32 imm32)
endif()
# macOS 使用系统输入法，不切换输入源或屏蔽 IME。
