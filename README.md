# PmacTerminal

从 SetupTool 中独立提取的 PMAC 终端程序：顶部连接栏 + 多标签终端 + 左侧 Watch 面板。
界面使用 Qt 6 / QML 外壳，终端为原生 QWidget，命令补全由 Rust 编写的 `pmac-terminal-rs` 提供，
PMAC 通信使用 `pmac.dll`（PMAC 2.0.7 MSVC x64 SDK）。

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

- Windows 10/11 x64：Visual Studio 2022（MSVC）+ Qt 6.8.x MSVC 2022 64-bit
- macOS 12+（Apple Silicon）：Xcode Command Line Tools + Qt 6.8.x macOS（universal，arm64 原生运行）
- CMake ≥ 3.21、Rust stable（编译 `third_party/pmac-terminal-rs`）
- PMAC 2.0.7 SDK（按平台选择）：`x86_64-pc-windows-msvc` 或 `aarch64-apple-darwin`，
  包含 `include/`、`native/pmac.dll|libpmac.dylib`、`cmake/PmacConfig.cmake`
- Rust 终端核心：`cargo build --release --lib` 生成
  `pmac_terminal_core.dll`（Windows）/ `libpmac_terminal_core.dylib`（macOS）

仓库不含 PMAC 预编译 SDK，从
[SDK Releases](https://github.com/Sangmo467/communication-library-for-ppmac-rust/releases)
下载对应包，解压到源码同级目录或通过 `-DPMAC_SDK_DIR=<路径>` 指定。
Rust 终端核心源码已放在 `third_party/pmac-terminal-rs`，先构建一次：

```powershell
cargo build --release --lib --manifest-path third_party/pmac-terminal-rs/Cargo.toml
```

Windows 本地构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -DPMAC_SDK_DIR="D:/sdk/pmac-2.0.7-x86_64-pc-windows-msvc-release"

cmake --build build --config Release
```

macOS（Apple Silicon）本地构建：

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DPMAC_SDK_DIR="$PWD/pmac-2.0.7-aarch64-apple-darwin-release"
cmake --build build --parallel
```

- `PMAC_SDK_DIR`：PMAC 2.0.7 SDK 根目录（默认按平台查找源码同级同名目录）。
- `PMACT_ROOT`：Rust 终端核心源码目录（默认 `third_party/pmac-terminal-rs`），
  也可用 `PMACT_CORE_LIB` 单独指定已编译的核心库。
- macOS 构建自动运行 `macdeployqt`，产物为 `dist/PmacTerminalApp.app`。

## 输出

- Windows：自动部署 Qt 运行库到构建目录，并通过 `package` 目标收集到 `dist/`。
- macOS：构建完成后得到自包含的 `dist/PmacTerminalApp.app`。

## GitHub Actions 多平台构建

推送 `main`/`master` 或手动触发 `.github/workflows/build.yml`，CI 会：

1. 下载 PMAC 2.0.7 对应平台 SDK Release；
2. 编译 `third_party/pmac-terminal-rs` 终端核心；
3. 构建并部署应用，上传产物：
   - `PmacTerminal-windows-x64`（`PmacTerminal-2.0.7-windows-x64.zip`）
   - `PmacTerminal-macos-arm64`（`PmacTerminal-2.0.7-macos-arm64.zip`，内含 `.app`）

macOS 产物未使用开发者证书签名（仅 ad-hoc）。首次运行若被 Gatekeeper 拦截，
可右键“打开”，或执行 `xattr -dr com.apple.quarantine <app 路径>`。
macOS 版通过 Carbon TIS 实现与 Windows 相同的输入源自动切换：聚焦命令输入框切到
ABC/US 英文输入源，离开时恢复原输入法；状态栏显示当前输入源。

## 单文件分发

Release 构建完成后，使用本机已安装的 Enigma Virtual Box 控制台打包：

```powershell
powershell -NoProfile -File tools/package-singlefile.ps1
```

输出 `dist_single/PmacTerminalApp.exe`，分发时只需复制这个 EXE。
包内包含 Qt、PMAC、Rust 终端核心、QML、翻译以及 MSVC x64 运行库。
打包机需要安装 Enigma Virtual Box 与 Visual Studio 2022 的 x64 C++ 运行库分发文件；
非默认安装路径可通过 `-EnigmaConsole`、`-CrtDir` 指定。
脚本读取当前 `dist/`，每次更新程序或翻译后需重新构建并打包。

## 离线自检

```powershell
dist\PmacTerminalApp.exe --terminal-smoke
```

自检不连接控制器，覆盖终端/Watch 面板、语言下拉、符号补全与标签生命周期。
