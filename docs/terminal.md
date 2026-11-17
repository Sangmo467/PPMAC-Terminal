# PmacTerminal 终端

本程序从 SetupTool 的终端页提取而来，只保留连接栏、终端标签与 Watch 面板。
终端与 Watch 面板共享 Backend 持有的同一个 PmacConnection / SDK Client。
所有连接、断开与地址/凭据输入都通过软件顶部连接栏完成。
新建标签不创建 SSH 连接；每个标签只保留各自的显示、历史和日志。
命令通过 BackendWorker 队列发送，使用 SDK 默认请求超时（超过 5 秒的复位命令由 SDK 内部放宽），GUI 不等待网络调用。
响应按请求 ID 路由回发起标签；重连时丢弃旧连接的返回结果。

应用退出和 Backend 析构时请求工作线程中断。连接、命令、符号文件 Shell 读取、维护面修复
使用 SDK 异步 Request，每次最多等待 50 ms 后检查中断；退出时请求取消，等到请求终态后再释放。
保留 SDK 默认业务超时与错误详情，不强制终止线程。重连循环及其等待间隔也响应中断，
退出后不再发起新的连接、硬件探测或终端请求。SDK Client/Runtime 的最终收尾仍遵循 SDK 契约；
取消不等于撤销控制器已收到的命令，不自动重发被取消的操作。

## 提词器

主连接建立后，BackendWorker 通过该连接读取控制器 Database 下的七份符号文件。
Rust 核心仅负责解析这些文本和本地候选查询，不接收连接凭据。
C ABI pmact_set_symbol_files 导入七份文本的 JSON 数组，[] 清空。
断开、换设备时清空索引，避免提示来自旧设备；新标签复用主连接的文件缓存。
刷新失败不影响命令收发，可点击“刷新提示”重试。

执行 `$$$` / `$$$***` 复位后，gpascii 重启会使 SDK 维护通道失效：普通命令仍可用，
但下一次符号文件读取会失败并把客户端置为 RECOVERY_REQUIRED(-27)。
BackendWorker 在复位响应返回后显式修复维护通道（必要时确认 unknown write）；
若 SDK 同时断开了传输，则用保存的凭据重建连接（最多重试 60 秒），
再重新读取七份符号文件，恢复复位后的结构提示。`$$$***` 会清空自定义变量，
变量需重新定义或保存后才会重新出现在提示中。

支持数组、成员、Gate/Chan、全局变量、程序、局部变量及帮助提示。
符号导入时向控制器查询 Gate1/Gate2/Gate3/GateIo 各实例的 `PartNum`，并探测每个 Gate 的有效 `Chan`：
只显示真实存在的实例（例如 `Gate3[1]`，其通道为 `Chan[0]`…`Chan[3]`），
不存在的 Gate 根与实例不参与提示；`Gate3[0].` 这类不存在的地址也不会列出成员。
同时解析符号表中的等效别名结构（`CK3WAX`/`CK3WMD`/`CK3WAD`/`CK3WGC`/`CK3WECS` 等），
在相同下标上探测 `PartNum`：检测到的别名实例与 Gate3 一并显示（如 `Gate3[3]` 与 `CK3WAX[3]`），未检测到的不显示。
其他逻辑数组仍给出 `Name[]`；已输入下标时保留下标，输入 `[` 但未给下标时默认 0。
接受数组候选后光标停在 `[]` 内：补完下标再输入 `.` 会自动放到 `]` 之后，重复输入 `]` 会跳过，避免得到 `Gate3[0.]` 或 `Gate3[0]]`。
输入以运算符结尾（如 `$$$***`）时没有可补全词元，不弹候选，避免列出整张一级符号表。
Enter/Tab 接受候选；数组候选光标进入 []；Shift+Enter 换行。
无候选时 Enter 发送，↑↓ 浏览历史。粘贴多行不会自动发送。
输入框支持多行；发送时按行依次执行，上一行响应返回后才发下一行。任一行出错即停止，未发送的行放回输入框。
Ctrl+C 遵循文本复制行为。

## 外观与输入

响应区藏青蓝底白字，警告/系统消息黄色，错误红色；命令输入框和提词器白底黑字，候选选中项蓝底白字。
终端与候选默认 14pt 半粗体，字体按钮同步调整输入、响应及候选。按钮采用 18px 半粗体，标签/状态采用 16px 半粗体；共享连接提示显式白字深灰底。候选项使用专用绘制器，编辑器持有焦点时仍保持选中项蓝底白字。
响应时间默认关闭；保留复制、清空、UTF-8 导出、日志、字体和打印。

Windows/macOS 顶层使用 QWidget，以 QQuickWidget 承载 QML 连接栏；终端与 Watch 作为同一顶层的普通
QWidget 子控件。Cocoa 对嵌套 QWidgetWindow 的普通按键会选择最外层 QWindow，故不能将终端的
QWidgetWindow 直接挂到 QQuickWindow 下，否则按键可能进入用户名/密码/IP 而无法进入终端或 Watch。
统一 QWidget 顶层后，QQuickWidget 负责 QML 与 QWidget 之间的焦点和输入法交接，不对离屏 QQuickWindow
执行自定义的窗口激活恢复。宿主的可见性、几何位置依据 renderWindowFor 返回的实际 QWidget 和偏移计算。
Windows 正常启动也使用同一宿主，切换其他窗口再返回时由 QWidget 恢复焦点，不运行旧的原生子窗口激活逻辑。
恢复目标由真实 focusObjectChanged 通知记录，连接栏获得焦点时清除，不使用历史 focusWidget() 推断。
Windows 下 QWidget::hasFocus() 可能与实际接收按键的控件不同步，输入法切换同时跟踪
QGuiApplication::focusObjectChanged。“自动英文”默认启用：命令框获得焦点、窗口重新激活
或按键时检查 GUI 线程的真实输入语言，需要时通过 ActivateKeyboardLayout 同步切换到英语布局；
原生宿主焦点延后建立时会延迟重试一次。离开命令框或关闭“自动英文”时恢复进入前的布局
（补全弹窗暂时拿走焦点时不恢复）；若用户已主动切换到其他非英文布局，则保留用户的选择。
macOS/Linux 不提供“自动英文”，不读取、切换或恢复系统输入源，也不屏蔽 IME 组合或提交。
输入 PMAC 命令时由用户手动选择英文输入法。输入法组合期间隐藏命令候选，
Enter/Tab/方向键交给文本编辑器和系统输入法，不发送命令、不接受命令补全、不浏览历史。
组合结束后的 Enter 恢复命令发送行为；粘贴内容保持原样。
仅 Windows 的“自动英文”关闭命令框 IME 组合输入，并取消已有预编辑。
连接/发送忙碌结束后，Qt 将输入框从只读切回可编辑时会重新启用输入法，
因此 Windows 编辑器在 ReadOnlyChange/EnabledChange 后重新应用策略，普通查询与参数化查询都返回 IME 禁用，
并拒绝迟到的预编辑/汉字提交事件。关闭“自动英文”后恢复正常 IME 输入。
Windows 不安装语言、不修改粘贴内容，也不周期性轮询；已经是英文布局时不重复切换。
Windows 工具栏显示“输入法: 英文/中文”，在焦点/按键/开关变化时读取真实布局并刷新。

Watch 的十进制整数直接按 64 位有符号/无符号数解析，十六进制和二进制位模式不经过 double，
保留超过 2^53 的整数低位。负浮点数先截断到有符号整数，再转换位模式；NaN、Infinity 和越界值
不强转为位模式，十六进制/定点/二进制显示回退原数值文本。

## 构建与验证

默认使用同级 pmac-terminal-rs/include 和 dist/pmac_terminal_core.dll。
核心需要包含 pmact_set_symbol_files；修改 Rust 后运行 cargo test --offline --lib，
再 cargo build --offline --release --lib 并将核心 DLL 更新到 dist。
package 目标自动带上核心 DLL、pmac SDK 和 Qt 运行库。
TerminalWidgets.inc 从上游 Qt 编辑器适配，收发改为共享 Backend。

离线集成检查：PmacTerminalApp.exe --terminal-smoke
macOS：dist/PmacTerminalApp.app/Contents/MacOS/PmacTerminalApp --terminal-smoke
自检覆盖 IME 可编辑状态变化、组合/提交、组合期间回车/Tab/方向键，以及组合结束后的命令发送。
另外覆盖 Watch 大整数/负数/非有限值格式化，以及 QML/终端焦点通知与隐藏恢复场景。
`--terminal-focus-smoke` 使用正式的 QWidget 宿主，向顶层窗口派发点击和按键，并切换到独立窗口再返回，
共 15 轮验证用户名/密码/IP → 终端 → Watch 编辑器 → 连接栏的真实控件输入路由，并检查平台默认 IP。
检查蓝色响应区、响应时间默认关闭、共享 Backend 绑定、
真实 Rust 符号导入、Enter/Tab 补全、数组光标、历史、复制、清空与标签生命周期。
额外连续模拟 1000 轮 $$$ 键入，检查 GUI 心跳持续运行；粘贴包含 $$$、
中文和 emoji 的多行内容，确认不会自动提交。

离线检查不连接控制器，不执行 $$$。发送后的控制器行为及真实超时仍需实机验证。

退出检查：`PmacTerminalApp.exe --backend-shutdown-smoke`（macOS 替换为 `.app` 内可执行文件）。
通过本机临时端口模拟卡住的 SSH 握手与连接失败后的重试，检查活动 SDK 请求可取消、
退出后不继续恢复/探测/发送，以及线程刚启动时立即销毁的生命周期。
