# PmacTerminal

从 SetupTool 中独立提取的 PMAC 终端程序：顶部连接栏 + 多标签终端 + 左侧 Watch 面板。
界面使用 Qt 6 / QML 外壳，终端为原生 QWidget，命令补全由 Rust 编写的 `pmac-terminal-rs` 提供，
PMAC 通信使用 `pmac.dll`（PMAC 2.0.6 MSVC x64 SDK）。

## 功能

- PMAC 连接、断开、固件版本与连接状态显示
- 多标签终端：历史、复制/清空/导出/日志/字体/打印
- 命令提词器：数组、成员、Gate/Chan、全局变量、程序、局部变量与帮助提示
- Watch 面板：周期查询、拖动排序、查询行/按钮行、浮点/十六进制/定点/二进制掩码显示
- 8 种界面语言（简体中文、繁體中文、English、हिन्दी、日本語、Español、Deutsch、Français）

## 目录说明

| 路径 | 内容 |
| --- | --- |
| `src/` | C++ 入口与 `PmacTerminal.Backend` 后台模块 |
| `content/` | QML 外壳（连接栏、状态条、终端页宿主） |
| `imports/PmacTerminal/` | QML 单例（Constants、LanguageManager） |
| `translations/` | 多语言 JSON（运行时优先读取 exe 旁目录） |
| `docs/` | 终端设计说明 |
| `tools/` | 打包脚本 |

以下内容**不纳入版本库**：`build/`、`dist/`、预编译的 PMAC SDK、`pmac-terminal-rs`，详见 `.gitignore`。

## 构建依赖

- Windows 10/11 x64，Visual Studio 2022（MSVC）
- Qt 6.8.x MSVC 2022 64-bit
- CMake ≥ 3.21
- PMAC 2.0.6 MSVC x64 SDK：包含 `include/`、`native/pmac.dll`、`cmake/PmacConfig.cmake`
- `pmac-terminal-rs` 构建产物：`dist/pmac_terminal_core.dll` 与 `include/pmac_terminal.h`

仓库不含上述预编译依赖。下载/构建后通过 CMake 参数指定：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -DPMAC_SDK_DIR="D:/sdk/pmac-2.0.6-x86_64-pc-windows-msvc-release" `
  -DPMACT_ROOT="D:/work/pmac-terminal-rs"

cmake --build build --config Release
```

- `PMAC_SDK_DIR`：PMAC 2.0.6 SDK 根目录（默认查找源码同级同名目录）。
- `PMACT_ROOT`：`pmac-terminal-rs` 仓库/分发包根目录（默认 `../pmac-terminal-rs`），也可用 `PMACT_CORE_DLL` 单独指定核心 DLL。

## 输出

- 构建后自动部署 Qt 运行库到构建目录，并通过 `package` 目标收集到 `dist/`。

## 离线自检

```powershell
dist\PmacTerminalApp.exe --terminal-smoke
```

自检不连接控制器，覆盖终端/Watch 面板、语言下拉、符号补全与标签生命周期。
