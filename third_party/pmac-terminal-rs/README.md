# PMAC Terminal — Rust core / C ABI / Qt

独立 Windows x64 终端仓库。通信固定使用 PMAC 2.0.5 SDK 的 `pmac.dll`，Rust 核心编译为 `pmac_terminal_core.dll`。提供 Rust 桌面界面及可编译的 Qt Widgets 多标签界面。

连接后，核心通过 SDK 的 SSH Shell 读取控制器 `/var/ftp/usrflash/Database/` 下的七份符号文件，在内存建立补全及帮助索引。**无需本地 MySQL，不持久化设备符号数据，不需要原 IDE 或 .NET。**

## 使用

构建产物位于 `dist/`：

- `qt/pmac-terminal-qt.exe`：Qt 多标签终端，点击“新建终端”可建立多个独立会话。
- `pmac-terminal.exe`：Rust / egui 界面，点击“新窗口”启动独立终端进程。
- `pmac_terminal_core.dll`、`pmac_terminal_core.dll.lib`、`include/pmac_terminal.h`：供 Qt 或其他程序调用。
- `pmac.dll`：指定的原生通信 SDK，需保留在程序旁。

输入地址、用户名和密码后连接；默认允许未验证主机密钥，也可取消勾选并配置 SHA256 主机指纹。密码不写入设置或日志。连接完成自动获取提示；项目重新下载后点击“刷新提示”。

支持命令发送、响应颜色、错误文本、响应时间、命令历史、数组/成员补全、帮助文本、独立终端、复制全部/选中文本、清空响应、UTF-8 文本导出和日志。Qt 界面支持字体选择与系统打印对话框。清空操作仅影响当前终端显示。

Qt：Shift+Enter 换行；提示弹出且有选中项时 Enter/Tab 接受候选，提示关闭时 Enter 发送，↑↓选择，Esc 关闭。未弹出提示时 ↑↓浏览历史；输入区无选中文本时 Ctrl+C 重建连接。Rust 界面采用相同规则：提示列表打开时 Enter/Tab 接受候选，提示关闭时 Enter 发送。带 `[]` 的候选接受后光标自动进入第一对空括号；无独立发送按钮。响应区使用原版 Navy 藏青背景、White 响应、Yellow 系统消息、Red 错误及 Windows GradientActiveCaption 已发送命令色；输入区 White 背景、Black 文字。多行输入显式发送，不因粘贴自动执行。

## 构建

需要 Rust MSVC x64 工具链；Qt 界面另外需要 Qt 6 Widgets/PrintSupport MSVC x64、Visual Studio 2022 C++ 与 CMake。

```powershell
./scripts/build.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64 -Offline
# 只构建 Rust 界面与 DLL：
./scripts/build.ps1 -Offline
```

依赖缓存未就绪时去掉 `-Offline`。所有源文件使用 UTF-8 无 BOM。SDK DLL 已放在 vendor 目录；程序可用 `PMAC_LIBRARY` 指定其他位置，版本及位数不符会返回错误。

## Qt 接入

完整示例在 `examples/qt/main.cpp`，通过 `QLibrary` 动态加载 DLL，不依赖 Rust 类型或编译器 ABI。每个终端调用一次 `pmact_create()`，得到独立 `uint64_t` 句柄。命令后台执行，Qt 用 QTimer 轮询 UTF-8 JSON 事件；补全接口只查询内存，不发送命令。

接口及内存约定见 [C-API.md](docs/C-API.md)；功能对照及限制见 [PARITY.md](docs/PARITY.md)。

## 验证

```powershell
cargo test --offline
# 离线 SDK 加载检查：
./dist/pmac-terminal.exe --check
# 实机只读测试：先在当前进程安全设置 PMAC_PASSWORD，其他变量如下。
$env:PMAC_HOST = '192.168.0.200'
$env:PMAC_USER = 'root'
$env:PMAC_KNOWN_HOSTS = 'C:/secure/known_hosts'
py ./scripts/test_dll.py
# 没有 known_hosts 时，测试可显式设置 PMACT_ALLOW_UNVERIFIED=1。
# Qt 控件自检（不连接设备；测试后恢复原剪贴板）：
./dist/qt/pmac-terminal-qt.exe --ui-smoke
```

已在提供的控制器实测：命令四级索引 193 / 1413 / 618 / 19，用户变量 370、程序 24、局部变量 225，共 2862 条。后续复测时控制器的三个用户符号文件为空，系统索引仍为 2243 条；数量随设备项目而变化，不做硬编码。通过两个同时连接的 DLL 会话验证读取、补全、帮助、断开隔离和释放；未执行运动、保存、复位或重启。

17 项 Rust 测试覆盖 ABI 布局、UTF-8 边界、转义换行、错误数据、1 万组特殊输入、超长输入、8 会话并行句柄。另通过 DLL 实测 2000 组特殊输入和非法 UTF-8；Qt 自检覆盖多标签、复制及清空。测试通过不等于能承诺任何错误宿主指针或所有固件变种都不会崩溃。

## 结构

- `sdk.rs`：指定 SDK 的 C ABI、请求生命周期、错误正文、取消及释放。
- `symbols.rs`：控制器符号文件读取与解析；字段布局已通过实机核对。
- `completion.rs`：UTF-8 安全的前缀、数组、别名、成员、Gate/Chan、用户变量及局部变量补全。
- `worker.rs`：每会话独立后台通信线程。
- `ffi.rs`：稳定 C ABI、不透明句柄、JSON 事件和调用方缓冲区。
- `main.rs`：Rust 界面；`examples/qt`：Qt 界面。

第三方 SDK 的许可证和声明在 `vendor/pmac/licenses/`。Qt 发行版的授权条件由所选 Qt 构建决定；本地部署脚本使用机器上已有的 Qt 安装。