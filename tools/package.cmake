# 由 CMake 的 package 目标调用（cmake -P）：
# 把构建目录中运行必需的文件收集到干净的 DST 目录。
# 输入变量：SRC（exe 所在目录）、DST（输出目录）、EXE（可执行文件名）

if (NOT SRC OR NOT DST OR NOT EXE)
    message(FATAL_ERROR "package.cmake: SRC / DST / EXE must be set")
endif ()

file(REMOVE_RECURSE "${DST}")
file(MAKE_DIRECTORY "${DST}")

# 可执行文件与所有顶层 DLL（Qt、pmac 等）
file(GLOB _dlls "${SRC}/*.dll")
file(COPY "${SRC}/${EXE}" ${_dlls} DESTINATION "${DST}")

# 运行时目录：QML 模块、平台插件、翻译
foreach (_dir qml platforms translations printsupport styles)
    if (IS_DIRECTORY "${SRC}/${_dir}")
        file(COPY "${SRC}/${_dir}" DESTINATION "${DST}")
    endif ()
endforeach ()

message(STATUS "Package collected in ${DST}")
