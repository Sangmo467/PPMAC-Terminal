# C ABI / Qt 接口

Windows x64：应用加载 `pmac_terminal_core.dll`，该 DLL 再加载指定的 `pmac.dll`。使用头文件 `include/pmac_terminal.h`；可链接 `.dll.lib`，也可像 Qt 示例一样使用 `QLibrary::resolve`。

所有导出使用 C / __cdecl，句柄为 uint64_t，长度为 size_t，文本为明确字节长度的 UTF-8。字符串不附加 NUL。只传调用期间有效的内存，不能传无效指针、虚假容量或重叠缓冲区。

## 调用顺序

1. `pmact_create(dll_path, path_len, &handle)`：每个终端创建一个句柄；路径为空则从宿主 EXE 旁加载 pmac.dll。此步骤不连接设备。
2. `pmact_connect(handle, json, json_len)`：后台连接并自动读取七份符号文件。
3. 轮询 `pmact_poll_event()`，收到 `symbols_loaded` 后可查询补全；收到 `ready` 后可提交下一项设备操作。
4. `pmact_send()` 提交命令；从 `response` 获取文本与耗时，从 `error` 获取 SDK 状态、执行确定性及错误正文。
5. `pmact_suggest()` 查询光标前的输入文本，返回候选及帮助。
6. `pmact_refresh_symbols()` 重读控制器的符号文件；`pmact_disconnect()` 断开当前会话并清除它的索引。
7. `pmact_destroy()` 请求取消在途请求、停止线程并释放资源。销毁所有句柄后，宿主才能卸载 DLL。销毁可能等待原生清理，应由宿主选择合适的线程执行。

连接 JSON：

```json
{
  "host": "192.168.0.200",
  "port": 22,
  "username": "root",
  "password": "<由界面取得，不保存>",
  "known_hosts": "C:/secure/known_hosts",
  "pinned_sha256": "",
  "private_key": "",
  "accept_any_host_key": true,
  "echo": null
}
```

`echo=null` 保留设备回显设置，其他值可为 0..3。Qt 示例使用 null。按照用户要求，默认允许未验证主机密钥；调用者可显式设置 false 并提供 known_hosts 或指纹。

## 两阶段缓冲区

```cpp
size_t required = 0;
int32_t rc = pmact_poll_event(h, nullptr, 0, &required);
if (rc == PMACT_BUFFER_TOO_SMALL) {
    std::vector<uint8_t> bytes(required);
    rc = pmact_poll_event(h, bytes.data(), bytes.size(), &required);
    // 根据 required 解析 UTF-8；不要依赖 NUL 结尾。
}
```

缓冲区不足不消耗事件，可重复查询长度。对同一句柄的事件查询/复制必须由宿主串行执行，避免另一线程先取走事件。`pmact_suggest()` 的结果在符号刷新完成后可能变化，若复制仍提示容量不足，应按最新长度重新分配。无事件返回 `PMACT_PENDING` 且 required=0。

输入最多 16 MiB；Qt 输入按 QString/UTF-8 字节长度转换，中文和补充平面字符不使用字节中点截断。特殊字符只参与字符串处理，不作为 SQL、Shell 或格式字符串执行。获取符号的 Shell 命令仅包含程序固定的文件名。

宿主对每个句柄最多提交一项在途设备操作；忙碌返回 `PMACT_BUSY`，不会偷偷排队或重发。长时间不读取事件会触发背压。不同句柄可以并行连接或提交命令。

## JSON 事件

```json
{"type":"connected","host":"192.168.0.200"}
{"type":"progress","text":"正在读取 pp_swtbl0.sym"}
{"type":"symbols_loaded","levels":[193,1413,618,19],"globals":370,"programs":24,"locals":225}
{"type":"ready","connected":true}
{"type":"response","text":"Sys.ServoTime=...","elapsed_ms":1.25}
{"type":"error","text":"PMAC status=..., certainty=..., request=...: ..."}
{"type":"disconnected"}
```

补全结果：`[{"text":"ServoCtrl","help":"..."}, ...]`。传入光标前的完整文本，例如 `P1=Motor[1].Serv`。Qt 根据当前词/点号边界替换候选；DLL 不修改宿主编辑框。

UI 自己持有响应文本、选择状态及历史。Qt 示例用 QClipboard 复制、QPlainTextEdit::clear 清空，所以复制和清空不会发送设备命令，也不会影响其他终端。

## 状态码

| 值 | 含义 |
|---|---|
| 0 | 成功或异步操作提交成功 |
| 1 | 暂无事件 |
| -1 | 参数、JSON、UTF-8 或输入长度无效 |
| -2 | 无效或已销毁句柄 |
| -3 | 输出缓冲区不足 |
| -4 | 会话忙或需先排空事件 |
| -5 | 内部错误或资源清理失败 |
| -6 | 尚未连接 |

设备操作失败通过 error 事件报告，原生 PMAC 的状态码保留在正文中。错误和超时不会触发命令自动重发。导出边界使用 catch_unwind，但它无法防御宿主传入坏指针，也不是进程隔离；宿主必须遵守头文件约定。
## Host-owned connection / imported symbols

pmact_set_symbol_files(handle, json, len) imports a UTF-8 JSON array containing
the seven symbol-file texts in the FILES order; [] clears the index. The host
fetches these files with its own existing connection. This call parses locally
and neither opens an SDK connection nor sends commands. Use an idle handle.
pmact_suggest then queries the imported index normally. Invalid file data returns
PMACT_INVALID_ARGUMENT and leaves the previous index intact.