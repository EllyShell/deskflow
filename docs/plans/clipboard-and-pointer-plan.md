# Deskflow 剪贴板与指针体验改进计划

| 项 | 内容 |
|---|---|
| 文档类型 | 实施计划（Implementation Plan），不含已完成代码 |
| 版本 / 日期 | v1.0 / 2026-11-11 |
| 代码基线 | `0d43a9031`（`main`，2026-10-31） |
| 覆盖需求 | R1 文本剪贴板换行异常、R2 图片剪贴板无法粘贴、R3 macOS↔Windows 边缘迟滞、R4 双屏指针速度/DPI 不一致 |
| 目标读者 | 本项目维护者 / 交付工程师 |
| 状态 | 待评审 |

---

## 0. 摘要（TL;DR）

四个问题不是同一类问题，但共用一个根因模式：**Deskflow 在平台边界上做“隐式格式转换”，转换既不幂等、失败又静默**。

- **R1 文本多换行**：CRLF 从未被真正归一化，只做了“成对时删一个 `\r`”，因此任何已带 CR 的源数据都会在 Windows 端变成 `\r\r\n`（= 多一个空行）。修复点应放在内部入口，而不是逐个平台打补丁。
- **R2 图片粘不出来**：三层原因叠加 —— 默认 3 MB 上限（用户已改 32 MB）、平台图片格式覆盖极窄（X11 只有 `image/bmp`、macOS 只有 `com.microsoft.bmp`）、失败路径只写日志不告知用户。
- **R3 边缘迟滞**：切换延迟选项 + 一次跨机交接 + Windows 客户端“每帧同步握手”的注入路径 + macOS 事件 tap 可能被系统超时禁用。
- **R4 DPI/速度**：协议里根本没有 scale 概念，且主屏用“加速后的位置差”、副屏用“原始 HID 增量”，两套物理含义叠加，必然不一致。

计划分四个里程碑：**M0 观测与守卫（1.5 人日）→ M1 文本归一化 + 图片格式面（10 人日）→ M2 协议 PNG + 迟滞优化（9 人日）→ M3 DPI/速度一致性（7 人日）**，合计约 27.5 人日（单人估算，含测试）。

---

## 1. 背景与已确认的根因

### 1.1 问题 → 根因 → 代码锚点

| # | 用户现象 | 已确认根因 | 代码锚点 |
|---|---|---|---|
| R1 | 跨机复制后“每行之间多一个空行 / 空行越来越多” | 写 Windows 时对每个 `\n` 无条件补 `\r`；读回时只删除“紧跟 `\n` 的 `\r`”，孤立 `\r` 永久保留 → `A\r\nB` 稳定变成 `A\r\r\nB` | [MSWindowsClipboardAnyTextConverter.cpp:84–89](../../src/lib/platform/MSWindowsClipboardAnyTextConverter.cpp#L84-L89)、[同文件:94–121](../../src/lib/platform/MSWindowsClipboardAnyTextConverter.cpp#L94-L121) |
| R1 | 只有部分应用/方向出问题 | 只有 `text/plain;charset=UTF-8` 与 `text/plain;charset=utf-8` 两个 target 会清 `\r`，`UTF8_STRING`/`text/plain`/`STRING` 完全透传；Wayland 只处理 `\r\n` | [XWindowsClipboard.cpp:66–68](../../src/lib/platform/XWindowsClipboard.cpp#L66-L68)、[XWindowsClipboardUTF8Converter.cpp:48–67](../../src/lib/platform/XWindowsClipboardUTF8Converter.cpp#L48-L67)、[PortalClipboard.cpp:303–307](../../src/lib/platform/PortalClipboard.cpp#L303-L307) |
| R1 | macOS 端同样放大 | `\n`↔`\r` 一对一替换，`\r\n` 会变成 `\r\r` → 读回 `\n\n` | [OSXClipboardAnyTextConverter.cpp:43–59](../../src/lib/platform/OSXClipboardAnyTextConverter.cpp#L43-L59) |
| R1 | —（契约） | 内部约定文本/HTML 一律 LF，但没有任何一处强制 | [IClipboard.h:38–47](../../src/lib/deskflow/IClipboard.h#L38-L47) |
| R2 | 截图复制后无法粘贴（已排除 32 MB 上限） | 内部只有 `Bitmap`（未压缩 DIB）；平台原生格式覆盖极窄；转换失败返回空串后被静默跳过 | [MSWindowsClipboard.cpp:85–114](../../src/lib/platform/MSWindowsClipboard.cpp#L85-L114)、[XWindowsClipboardBMPConverter.cpp:54–55](../../src/lib/platform/XWindowsClipboardBMPConverter.cpp#L54-L55)、[OSXClipboardBMPConverter.cpp:47–50](../../src/lib/platform/OSXClipboardBMPConverter.cpp#L47-L50)、[PortalClipboard.h:29–33](../../src/lib/platform/PortalClipboard.h#L29-L33) |
| R2 | 大图被丢弃 | 默认上限 3 MB，图像体积 = 宽×高×4 字节；超限只写日志 | [Settings.cpp:387–388](../../src/lib/common/Settings.cpp#L387-L388)、[Server.cpp:1457–1459](../../src/lib/server/Server.cpp#L1457-L1459)、[ClipboardChunk.cpp:116–119](../../src/lib/deskflow/ClipboardChunk.cpp#L116-L119) |
| R2 | 粘出全黑/透明 | 32bpp DIB 带 alpha=0 被原样透传，由目标应用按 alpha 合成 | [MSWindowsClipboardBitmapConverter.cpp:109–115](../../src/lib/platform/MSWindowsClipboardBitmapConverter.cpp#L109-L115) |
| R3 | 边缘处“顿一下”才过去 | ① 切换延迟/双击选项（默认关，但配置可能残留）；② 跨机交接要隐藏/显示光标 + 一次 RTT；③ Windows 客户端每帧 `PostThreadMessage` + `waitForDesk()` 同步等待；④ macOS event tap 可能被系统超时禁用后重建 | [Config.cpp:541–547](../../src/lib/server/Config.cpp#L541-L547)、[OSXComputer.mm:819–834](../../src/lib/platform/OSXComputer.mm#L819-L834)、[MSWindowsDesks.cpp:303–311](../../src/lib/platform/MSWindowsDesks.cpp#L303-L311)、[同文件:891–900](../../src/lib/platform/MSWindowsDesks.cpp#L891-L900)、[OSXComputer.mm:1712–1716](../../src/lib/platform/OSXComputer.mm#L1712-L1716) |
| R4 | 两侧手感/速度不一致 | 协议只有屏幕矩形，没有 scale；主屏用加速后的位置差，副屏用原始 HID 增量；Windows core 未声明 DPI 感知 | [OSXComputer.mm:988–1026](../../src/lib/platform/OSXComputer.mm#L988-L1026)、[Server.cpp:1712–1723](../../src/lib/server/Server.cpp#L1712-L1723)、[MSWindowsComputer.cpp:1431–1445](../../src/lib/platform/MSWindowsComputer.cpp#L1431-L1445)、[deskflow-core.exe.manifest](../../src/apps/deskflow-core/deskflow-core.exe.manifest) |

### 1.2 现状链路（用于定位改动点）

**剪贴板（4 段）**

```
源应用 → [A 平台读] → [B 内部 marshall] → [C 分块传输 512KB] → [D 平台写] → 目标应用
         converter       Format::Text/HTML/Bitmap    StreamChunker      converter
```

- A：各平台 converter 的 `toIClipboard()`（平台 → 内部）
- B：`IClipboard::marshall()`，带 4 字节格式头与长度
- C：`StreamChunker`（512 KB/块）+ `ClipboardChunk`
- D：`MSWindowsClipboard::add()` / `IClipboard::copy()` → `fromIClipboard()`

**指针（4 段）**

```
主屏事件 → Server::onMouseMovePrimary（绝对坐标/加速）
        → switchComputer（leave + enter，跨机）
        → onMouseMoveSecondary（原始增量累加 + 边界钳制）
        → 客户端 mouseMove → 平台注入（Windows: PostThreadMessage + waitForDesk）
```

---

## 2. 目标与非目标

### 2.1 目标

1. 跨平台复制的纯文本**行数与原文本严格一致**，任意 CR/LF 组合往返后不再增加空行（幂等）。
2. 常见来源（浏览器、终端、Word/Teams/微信、截图工具）的**图片在 Windows↔Linux(Wayland)↔Windows 之间可粘贴**；传输体积下降一个数量级；失败对用户可见。
3. macOS→Windows 边缘切换的**可观测延迟**降到局域网可接受范围（目标 < 50 ms 的可测量交接耗时），并消除“每帧同步等待”带来的抖动。
4. 提供**可配置**的跨屏速度一致性手段（自动按 scale 换算 + 手动微调），默认不改变现有手感。

### 2.2 非目标

- 不做文件/文件夹剪贴板传输（`CF_HDROP` 等），不在本期范围。
- 不改变键位映射、输入法、快捷键广播等既有能力。
- 不在本期把 `Format::Bitmap` 的 DIB 语义彻底删除（老客户端兼容路径必须保留）。
- 不做 Windows 客户端多虚拟桌面的坐标模型重构（仅在 R3 中消除阻塞点）。

---

## 3. R1：文本剪贴板行尾归一化

### 3.1 设计原则

**在内部入口归一化一次，各平台只管“内部 LF → 本地格式”的单向转换。** 这样：
- 任一平台实现错误都不会污染内部数据；
- 老/新版本任意组合都只会变好，不会变坏。

### 3.2 方案比较

| 方案 | 做法 | 优点 | 缺点 | 结论 |
|---|---|---|---|---|
| A. 逐平台打补丁 | 分别修 Windows/macOS/X11/Wayland 转换函数 | 改动小 | 4 处易漏、契约仍无人保证、新增平台继续踩 | 否 |
| B. 内部入口归一化 | `Clipboard::add()` 对 Text/HTML 强制 LF；转换函数改成幂等工具 | 一处生效、契约落地、易测 | 需要定义“孤立 CR”语义 | **选 B（并保留 A 作为防御）** |
| C. 协议层携带行尾标记 | 传原始字节 + 行尾类型 | 零信息损失 | 协议变更大、平台仍要各自处理 | 否 |

### 3.3 具体设计

新增 `src/lib/deskflow/ClipboardText.h/.cpp`（或 `base/LineEndings.h`）：

```cpp
namespace deskflow::text {
std::string toLf(std::string_view in);    // CRLF→LF, 孤立 CR→LF，幂等
std::string toCrLf(std::string_view in);  // 先 toLf，再 LF→CRLF，幂等
std::string toCr(std::string_view in);    // 先 toLf，再 LF→CR（macOS 粘贴板）
}
```

接线点：

| 位置 | 现状 | 改为 |
|---|---|---|
| `Clipboard::add()` | 原样存储 | `Format::Text`/`HTML` 先 `toLf` |
| `MSWindowsClipboardAnyTextConverter::fromIClipboard()` | `convertLinefeedToWin32` | `toCrLf` |
| `MSWindowsClipboardAnyTextConverter::toIClipboard()` | `convertLinefeedToUnix` | `toLf` |
| macOS `convertLinefeedToMacOS/Unix` | 一对一替换 | `toCr` / `toLf` |
| X11 所有 text target | 仅两个 atom 清 `\r` | 统一 `toLf`（`m_normalize` 保留兼容） |
| Wayland `readSelectionIntoCache()` | `replace("\r\n","\n")` | 字节级 `toLf` |

### 3.4 语义决策（需要评审确认）

- **孤立 `\r` 的归属**：内部契约写的是“换行是 LF”，因此把孤立 CR 当换行处理（`\r` → `\n`）。这会改变“终端进度条”类文本（`\r` 覆盖同一行）的粘贴结果。
- 缓解：新增设置 `clipboard/normalizeLoneCr`（默认 `true`）。关闭时只做 CRLF→LF（isolated CR 保留，但**写出前**不允许出现 `\r\n` 组合，因此仍不会产生 `\r\r\n`）。

### 3.5 任务

| ID | 任务 | 改动文件 | 人日 |
|---|---|---|---|
| R1-1 | 新增行尾工具 + 单测（性质测试：随机 CR/LF 组合两次往返稳定） | 新增 `ClipboardText.*`、`src/unittests/deskflow/` | 1.0 |
| R1-2 | 接入 `Clipboard::add()`，并把 Windows/macOS 转换改为调用工具 | `Clipboard.cpp`、`MSWindowsClipboardAnyTextConverter.cpp`、`OSXClipboardAnyTextConverter.cpp` | 1.0 |
| R1-3 | X11/Wayland 读取路径统一；移除 per-atom 特例（保留兼容开关） | `XWindowsClipboard*.cpp`、`PortalClipboard.cpp` | 1.0 |
| R1-4 | `normalizeLoneCr` 设置项 + 文档 | `Settings.*`、`docs/Configuration.md` | 0.5 |

### 3.6 验收

- 单测：对 `{"a","\r","\n","\r\n","\r\r\n","\n\r"}` 全组合，`toCrLf(toLf(x))` 不再产生连续两个 CR；`toLf(toCrLf(y)) == y`（LF-only 时）。
- 手工：GTK 应用/终端/xterm 复制 3 行文本 → Windows 记事本粘贴，行数 = 3，无空行。
- 回归：老版本客户端 ↔ 新服务器、新客户端 ↔ 老服务器，两种组合都不出现 `\r\r\n`。

---

## 4. R2：图片剪贴板

### 4.1 分层策略

先做“**可见 + 不丢**”，再做“**格式面**”，最后做“**体积**”。避免一次性动协议。

| 层 | 目标 | 是否改协议 |
|---|---|---|
| L1 可见性 | 失败不再静默；日志与设置页说明清楚 | 否 |
| L2 默认值 | 3 MB → 32 MB（刚好容纳 4K DIB），并给出一次性提示 | 否 |
| L3 平台格式面 | X11 `image/png`、macOS `public.png/tiff`、Windows `PNG`/`CF_DIBV5` | 否 |
| L4 协议 PNG | `Format::BitmapPng` + 能力协商，体积降 10–20× | 是（minor 1.9） |
| L5 正确性 | alpha=0 黑图、top-down/V5/palette DIB 归一化 | 否 |

### 4.2 L1 可见性

- 超限：`LOG_WARN` + 1 秒节流（避免连点复制刷屏）。
- 转换为空 / marshall 后 0 字节：`LOG_INFO`（原为 DEBUG），附带格式名与原因码（`empty` / `unsupported` / `oversize`）。
- GUI：在“服务器配置 → 剪贴板”下方加静态说明：*图片以未压缩 DIB 传输，约 宽×高×4 字节（1080p ≈ 8 MB、4K ≈ 32 MB）；不足以容纳时请调大上限或启用 PNG 传输。*

### 4.3 L2 默认值

- `Settings::defaultValue(Server::ClipboardSize)`：3 → 32（[Settings.cpp:387–388](../../src/lib/common/Settings.cpp#L387-L388)）。单位是 MB，`Config.cpp:556` 会 ×1024 转成 KB 下发给两端。
- 代价：默认上限提高后，极端大图会带来约 2× 数据量的瞬时内存占用（发送端 marshall + 接收端缓存），因此只建议在 64 位桌面端提高，并在日志里记录单次最大传输量。
- 升级时如果检测到仍是默认值且用户从未改过，弹一次提示（state key 记录）。
- 不改 GUI 的 10000 MB 上限。

### 4.4 L3 平台格式面（不改协议）

| 平台 | 读（源应用 → Deskflow） | 写（Deskflow → 目标应用） | 备注 |
|---|---|---|---|
| Windows | `CF_DIB`、`CF_DIBV5`、`PNG` | `CF_DIB` + `PNG` + （可选）`CF_DIBV5` | WIC 编解码；alpha 缺失时改走 DIBV5 |
| X11 | `image/png`（优先）→ `image/bmp` | `image/png` + `image/bmp` | 需支持“同一 Format 多个 converter” |
| macOS | `public.png`、`public.tiff`、`com.microsoft.bmp` | `public.png` + `com.microsoft.bmp` | ImageIO 编解码 |
| Wayland | `image/png`（已有）、`image/bmp`（新增） | 同左 | portal 已支持 png |

实现要点：

1. **同一 `Format` 多 converter**：`MSWindowsClipboard::add()` 目前遇到第一个匹配 converter 就 `break`（[MSWindowsClipboard.cpp:94–104](../../src/lib/platform/MSWindowsClipboard.cpp#L94-L104)）。需要改为“遍历所有匹配 converter 全部写入”，X11 侧同理；`IClipboard::marshall()` 仍只存一份内部数据，不重复传输。
2. **编码依赖**：`Qt6::Gui` 目前只在 Linux/Wayland 分支链接（[platform/CMakeLists.txt:226](../../src/lib/platform/CMakeLists.txt#L226)）。Windows/macOS 若用 QImage 需新增链接（增大体积），否则分别用 **WIC**（Windows）与 **ImageIO**（macOS）。计划默认走原生 API，避免 core 引入 QtGui。
3. **读取优先级**：PNG 优先于 BMP（PNG 体积小、alpha 完整）；BMP 作为兜底保证老应用仍可粘贴。

### 4.5 L4 协议层 PNG（可选阶段）

- `IClipboard::Format` 追加 `BitmapPng`（放在 `TotalFormats` 之前，`TotalFormats` 递增）。老端 `unmarshall()` 会跳过未知格式（[IClipboard.cpp:63](../../src/lib/deskflow/IClipboard.cpp#L63)），表现为“图片丢失但不断连”。
- 能力协商：新增 option `kOptionClipboardPng`（0/1），由服务器在 `Config.cpp` 中按协议版本下发；未收到视为不支持。
- 发送策略：对端支持 → 发 `BitmapPng`；否则发 `Bitmap`（DIB）。**不双发**，避免流量翻倍。
- 接收策略：收到 `BitmapPng` 时**延迟解码**（存入内部为 PNG 字节，写平台剪贴板时再解码为 DIB），避免大图常驻两份内存。
- 上限语义：`m_maximumClipboardSize` 继续按“传输字节”计算，PNG 后同样 32 MB 可容纳更大的图。

### 4.6 L5 正确性

- 32bpp 全 0 alpha → 写 `CF_DIB` 前把 alpha 置 `0xFF`（或改走 `CF_DIBV5`），消除黑图。
- 复用已有 `normaliseMalformedMacDib` 思路，补齐：top-down、`BI_BITFIELDS`、8bpp palette、`BITMAPV5HEADER`（`biSize=124`）四种 fixture。

### 4.7 任务

| ID | 任务 | 改动文件 | 人日 |
|---|---|---|---|
| R2-1 | 日志节流 + 原因码 + 设置页说明 | `MSWindowsClipboard.cpp`、`IClipboard.cpp`、`ServerConfigDialog.ui/.cpp` | 0.5 |
| R2-2 | 默认上限 32 MB + 一次性提示 | `Settings.cpp`、`Settings.h` | 0.5 |
| R2-3 | Windows：读 `CF_DIBV5`/`PNG`，写 `PNG`（WIC） | `MSWindowsClipboardBitmapConverter.*` | 1.5 |
| R2-4 | X11：`image/png` converter + 同格式多 converter | 新增 `XWindowsClipboardPNGConverter.*`、`XWindowsClipboard.cpp` | 1.5 |
| R2-5 | macOS：`public.png`/`public.tiff` 读写（ImageIO） | `OSXClipboardBMPConverter.*` 或新增 `OSXClipboardPNGConverter.*` | 1.5 |
| R2-6 | DIB 正确性 fixture + alpha 修正 | `MSWindowsClipboardBitmapConverter.cpp`、`OSXClipboard*.cpp` | 1.0 |
| R2-7 | 协议 `BitmapPng` + 能力协商（可选） | `IClipboard.h`、`ProtocolTypes.h`、`Config.cpp` | 2.5 |
| R2-8 | 客户端侧 PNG 解码（WIC/ImageIO） | `MSWindows/Mac` 平台层 | 2.0 |

### 4.8 验收

- 手工矩阵（见 §8.3）：Windows→Linux X11/Wayland、Linux→Windows、macOS→Windows、Windows→Windows，四个方向粘到“画图/GIMP/LibreOffice/浏览器”是否成功。
- 1920×1080 PNG（约 200 KB）在 32 MB 上限下正常传输；DIB 路径同样正常。
- 老客户端（无 `BitmapPng` 能力）收到 DIB；新客户端互相之间走 PNG。
- 失败用例：超限时 GUI 有提示，日志有 `reason=oversize`。

---

## 5. R3：边缘迟滞

### 5.1 先测量，再优化

在 `Server::switchComputer()` 中打点（[Server.cpp:398](../../src/lib/server/Server.cpp#L398)）：

```
switch latency: edge=0.3ms leave=1.2ms enter=0.1ms first_move=18.4ms
```

- `edge`：`onMouseMovePrimary` 判定越界到调用 `switchComputer` 的耗时；
- `leave`：`m_active->leave()` 耗时（macOS 上是 `hideCursor` 的窗口服务器往返）；
- `first_move`：`enter` 发出到收到第一帧 `onMouseMoveSecondary` 的耗时（含 RTT）。

这一步是 M0 的交付物，后续优化都用它衡量。

### 5.2 改动清单

| ID | 改动 | 说明 | 人日 |
|---|---|---|---|
| R3-1 | 打点 + `effective switch options` 日志 | 连接后 INFO 输出 `delay/doubleTap/lockedCorners` 实际值，消除“以为关了其实开着” | 0.5 |
| R3-2 | Windows 注入去阻塞 | `MSWindowsDesks::sendMessage` 改为“共享最新坐标 + PostThreadMessage 唤醒”，desk 线程合并处理；`waitForDesk()` 仅保留在 desk 切换/形状更新等需要确定性的路径 | 2.0 |
| R3-3 | macOS `leave/enter` 瘦身 | 缓存 `CGSSetConnectionProperty` 的 CFString；删除 `hideCursor()` 里与 `leave()` 抵消的 `CGAssociateMouseAndMouseCursorPosition(true)`；`logCursorVisibility()` 降级为 VERBOSE+采样 | 1.0 |
| R3-4 | event tap 健康检查 | `kCGEventTapDisabledByTimeout` 计数、连续 N 次告警；AX 权限缺失时 GUI 提示 | 0.5 |
| R3-5 | 可选：`enter` 合并首次 warp | 避免“enter 一次 warp + 首帧 move 再一次 warp”的双跳 | 0.5 |

### 5.3 风险

- R3-2 若实现不当会丢失最后一次移动 → 必须实现“最新坐标覆盖 + 处理完清标志”，并保留最后一次 move 的补发。
- R3-3 删除 `CGAssociateMouseAndMouseCursorPosition(true)` 后，若某些 macOS 版本依赖它让 hide 生效，会出现“光标不隐藏” → 用设置项 `macos/hideCursorWorkaround` 可回滚。

### 5.4 验收

- 局域网（有线）打点数据：`first_move` < 50 ms，`leave` < 5 ms。
- 连续 200 次跨边缘往返无卡死、无光标残留。
- 触发 `tap timeout` 时日志可定位（不再静默）。

---

## 6. R4：DPI / 指针速度一致性

### 6.1 现状事实

| 侧 | 尺寸来源 | 单位 | 备注 |
|---|---|---|---|
| macOS | `CGDisplayBounds` | 点（缩放后逻辑分辨率） | [OSXComputer.mm:1337–1345](../../src/lib/platform/OSXComputer.mm#L1337-L1345) |
| Windows | `GetSystemMetrics(SM_CXVIRTUALSCREEN)` | DIP（进程 DPI-unaware 时被虚拟化） | [MSWindowsComputer.cpp:1431–1445](../../src/lib/platform/MSWindowsComputer.cpp#L1431-L1445)、manifest 无 `dpiAware` |

移动模型：
- 主屏：位置差（含系统加速）→ [OSXComputer.mm:990–1011](../../src/lib/platform/OSXComputer.mm#L990-L1011)
- 副屏：原始 HID 增量累加 + 钳制后再绝对定位 → [OSXComputer.mm:1013–1022](../../src/lib/platform/OSXComputer.mm#L1013-L1022)、[Server.cpp:1756–1875](../../src/lib/server/Server.cpp#L1756-L1875)

**结论：两侧同时存在“单位长度不同”与“加速模型不同”两重差异，只补其中一个不会有明显改善。**

### 6.2 设计

1. **形状上报扩展**：`IComputer` 增加 `getScaleInfo()`（逻辑宽高 + 物理宽高/ppi）。不改变 `getShape()` 语义，避免影响边界逻辑。
2. **协议传递**：新增 option `kOptionScreenScale`（每屏一个 ×1000 的整数），客户端 hello 后上报；未收到按 1.0 处理。
3. **服务器换算**：
   - 副屏移动：`dx' = round(dx * primaryScale / secondaryScale)`；
   - 进入坐标：按屏幕比例映射（复用 `mapToFraction`），而不是直接把主屏坐标发给副屏。
4. **加速一致性（可选，默认关）**：`Client::MouseLinearMode` 打开后，主屏也使用原始增量（macOS `kCGMouseEventDeltaX/Y`、X11 raw motion、Windows `WM_INPUT`），两侧统一为线性，再靠 scale 对齐。
5. **用户可调**：新增 `client/mouseScale`（默认 1.0，范围 0.2–5.0），GUI 在屏幕配置页加“指针速度”滑块；`DESKFLOW_MOUSE_ADJUSTMENT` 保留为全局紧急覆盖。
6. **Windows DPI 感知（评估项）**：给 core 加 per-monitor-v2 manifest，并把物理坐标换算为 DIP 后上报；需回归 `MSWindowsDesks` 的所有坐标使用点。

### 6.3 任务

| ID | 任务 | 人日 |
|---|---|---|
| R4-1 | `IComputer::getScaleInfo()` + 各平台实现（macOS `CGDisplayScreenSize`、Windows `GetDpiForMonitor`、X11 `Xft.dpi`/物理尺寸） | 2.0 |
| R4-2 | 协议 option + 服务器换算（移动与边界） | 1.5 |
| R4-3 | `MouseLinearMode` 线性模式 | 1.0 |
| R4-4 | `client/mouseScale` + GUI 滑块 + 文档 | 1.0 |
| R4-5 | Windows DPI-aware 评估与实现（可选） | 1.5 |

### 6.4 验收

- 提供测量脚本/步骤：在两侧各做 200 mm 水平移动，记录屏幕像素位移，换算后误差 < 10%。
- 默认配置（不开启新选项）手感与当前版本一致（回归）。
- 开启 `mouseScale=1.0` + 自动 scale 后，两侧穿越边界无速度突变。

---

## 7. 里程碑与排期

| 里程碑 | 内容 | 交付物 | 人日 |
|---|---|---|---|
| **M0** | 观测与守卫 | 剪贴板结构化日志、切换打点、实际选项日志 | 1.5 |
| **M1** | 文本归一化 + 图片格式面 | R1 全部（3.5）、R2-1..R2-6（6.5） | 10.0 |
| **M2** | 协议 PNG + 迟滞优化 | R2-7..R2-8（4.5）、R3 全部（4.5） | 9.0 |
| **M3** | DPI/速度一致性 | R4 全部 | 7.0 |
| **合计** | | | **27.5 人日** |

建议节奏：
- M0 与 M1 可并行（观测代码不冲突）。
- M2 的协议 PNG 需要与 Windows/macOS 解码一起交付，避免“能发不能收”。
- M3 最后做，因为它会改变现有手感，需要 M0 的打点数据做回归对照。

---

## 8. 测试策略

### 8.1 单元测试

| 模块 | 用例 | 位置 |
|---|---|---|
| 行尾工具 | 幂等性、随机组合性质测试 | 新增 `src/unittests/deskflow/ClipboardTextTests.*` |
| Windows 位图转换 | top-down / BI_BITFIELDS / V5 / palette / alpha=0 | [MSWindowsClipboardTests.cpp](../../src/unittests/platform/MSWindowsClipboardTests.cpp) |
| X11 位图 | `image/png` 与 `image/bmp` 互转、同格式多 converter | [XWindowsClipboardTests.cpp](../../src/unittests/platform/XWindowsClipboardTests.cpp) |
| macOS 位图 | `public.png`/`tiff` 包装、DIB 归一化 | [OSXClipboardTests.cpp](../../src/unittests/platform/OSXClipboardTests.cpp) |
| 协议 | `BitmapPng` 在老/新端的行为、上限边界 | `src/unittests/deskflow/ClipboardTests.cpp` |
| 换算 | scale 换算与边界映射 | 新增 `src/unittests/deskflow/MouseScaleTests.*` |

### 8.2 集成测试

- 现有 `Integ` 框架下增加“文本往返”“图片往返”两条链路（不依赖真实多机，用 mock stream）。
- 断言：内部 marshall 数据中 `\r` 数量为 0；`BitmapPng` 体积 < DIB 体积的 1/5。

### 8.3 手工矩阵（发布前必跑）

| 场景 | 来源 | 目标 | 期望 |
|---|---|---|---|
| 文本 | GTK / 终端 / xterm / 浏览器 | Windows 记事本 | 行数一致、无空行 |
| 文本 | Word / Teams | Linux 编辑器 | 段落数一致 |
| 图片 | Windows 截图工具 | Linux Wayland（浏览器） | 成功 |
| 图片 | Windows 截图工具 | Linux X11（GIMP） | 成功 |
| 图片 | Linux（PNG 源） | Windows 画图 | 成功 |
| 图片 | macOS 截图 | Windows 画图 | 成功（R2-5 后） |
| 迟滞 | macOS→Windows | — | `first_move` < 50 ms |
| 速度 | 两侧各 200 mm | — | 位移误差 < 10% |

### 8.4 日志断言

CI 中可用 mock stream 跑一次“复制→传输→写入”，断言日志含 `reason=...` 且不含 `exceeds limit`（在 32 MB 用例下）。

---

## 9. 兼容性与发布

| 变更 | 兼容策略 |
|---|---|
| R1 归一化 | 只影响内部表示；老端收到仍是合法 CRLF/LF 文本。任意组合安全。 |
| R2 平台格式 | 纯本地新增，不涉及协议；老应用仍走 BMP/DIB。 |
| R2 协议 PNG | 能力协商 + 缺省走 DIB；协议 minor 1.8 → 1.9，老端忽略未知格式。 |
| R3 注入去阻塞 | 行为等价，需回归虚拟桌面/UAC/全屏切换。 |
| R4 scale | 默认 1.0，未收到 option 视为 1.0；不改变现有配置行为。 |

发布顺序建议：M0/M1 作为 `fix` 版本先发；M2 的协议改动跟随一次 `feat` 版本；M3 作为可选能力（GUI 开关）再发。

---

## 10. 风险与回滚

| 风险 | 影响 | 缓解 | 回滚 |
|---|---|---|---|
| 孤立 CR 语义改变 | 终端进度条类文本粘贴变化 | `normalizeLoneCr` 开关 | 关开关 |
| PNG 编解码 CPU/内存 | 4K 截图转换卡顿 | 只在需要时转换、限制最大边长 | 关闭 PNG converter |
| 协议 PNG 兼容性 | 老端收不到图 | 能力协商、缺省 DIB | 服务端开关 `clipboard/pngDisabled` |
| Windows 去阻塞丢帧 | 鼠标跳动 | 最新坐标覆盖 + 末帧补发 + 开关 | 恢复 wait 路径 |
| macOS hide 失效 | 光标不隐藏 | `macos/hideCursorWorkaround` | 打开开关 |
| scale 换算改变手感 | 用户不适应 | 默认 1.0，需显式开启 | 置 1.0 |

---

## 11. 观测与度量

新增日志（DEBUG 级别，可用 `--debug` 打开）：

- `clipboard: format=%s bytes=%zu cr=%zu reason=%s`（读/写两端）
- `clipboard: dropped reason=oversize|unsupported|empty size=%zu limit=%zu`
- `switch latency: edge=%.1fms leave=%.1fms enter=%.1fms first_move=%.1fms`
- `effective switch options: delay=%dms doubleTap=%dms corners=0x%x`
- `mouse scale: primary=%.3f secondary=%.3f applied=%.3f`

度量目标：发布后从用户日志中统计 `reason=oversize` 占比、`first_move` 分位数，作为后续优化依据。

---

## 附录 A：不改代码就能做的缓解

| 问题 | 现在就能做 |
|---|---|
| 文本空行 | 复制后先粘到 VS Code/Notepad++，把 `\r\n` 替换成 `\n` 再复制；或临时关闭剪贴板共享验证归因 |
| 图片 | 上限已设 32 MB；仍失败时按“粘到 GIMP/画图”判断是格式问题；跨机传图改用文件/共享盘；Wayland 端最稳，X11 只认 `image/bmp`，macOS 端基本不可用 |
| 边缘迟滞 | 确认 `Switch after waiting` / `Switch on double tap within` 未勾选；有线网络；检查日志是否出现 `quartz event tap was disabled by timeout` |
| 速度不一致 | macOS 服务端设 `DESKFLOW_MOUSE_ADJUSTMENT=1.2~1.5`；两端关闭指针加速（Windows 取消“提高指针精确度”，macOS `defaults write -g com.apple.mouse.linear -bool true`） |

## 附录 B：日志速查表

| 日志 | 含义 | 处理 |
|---|---|---|
| `not sending clipboard data, exceeds limit: N KB` | 发送端超限 | 调大上限或改用 PNG |
| `not receiving clipboard data, exceeds limit` | 接收端超限 | 检查两端上限是否一致 |
| `bitmap: WxH bpp` | Windows 读到位图 | 有则说明识别成功，问题在对端格式面 |
| `added format 2 for target image/bmp` | X11 读到图 | 目标应用不认 BMP → 需要 `image/png` |
| `not adding 0 bytes to clipboard format: 2` | 转换出空数据 | 看转换器日志（CreateDIBSection 失败等） |
| `quartz event tap was disabled by timeout` | macOS tap 被禁用 | 检查 AX 权限/系统负载 |
| `cursor may not be visible` | 光标状态异常 | 关注 hide/show 配对 |

## 附录 C：验收清单

- [ ] R1：随机 CR/LF 组合性质测试通过
- [ ] R1：GTK/终端 → Windows 行数一致
- [ ] R2：四个方向 × 四个目标应用的矩阵通过
- [ ] R2：老客户端 ↔ 新服务器图片不崩（DIB 回退）
- [ ] R2：超限/空数据有 WARN 且 GUI 可见
- [ ] R3：`first_move` < 50 ms（有线 LAN）
- [ ] R3：连续 200 次跨边缘无异常
- [ ] R4：scale 换算误差 < 10%，默认配置无回归
- [ ] 文档：`docs/Configuration.md` 增补新选项

## 附录 D：待实测/开放问题

1. Windows 11 截图工具实际提供哪些剪贴板格式（`CF_DIB`/`CF_DIBV5`/`PNG`/`HTML`）。
2. Chrome/Firefox/Electron 在 X11 下提供的图片 target 列表（是否只有 `image/png`）。
3. GTK/Chromium 是否总是提供 `text/plain;charset=utf-8`（决定 R1-3 的收益）。
4. Windows 客户端 desk 线程在真实负载下的阻塞时长（决定 R3-2 的优先级）。
5. macOS 各版本 `CGDisplayBounds` 在缩放模式下的取整误差（影响 R4 的换算精度）。
6. `Qt6::Gui` 若引入 core，安装包体积增量是否可接受（决定 R2 用 WIC/ImageIO 还是 QImage）。

---

## 12. 实施进展（滚动更新）

### 2026-11-11

**已完成 R1（文本行尾归一化）**

- 新增 `src/lib/common/ClipboardText.{h,cpp}`：`toLf` / `toCrLf` / `toCr`，三者均为幂等实现。
- `Clipboard::add()` 对 `Text`/`HTML` 强制 LF，作为唯一内部入口。
- Windows / macOS / X11 / Wayland 四处平台转换改为调用同一工具，删除原先各自实现的两个 Windows 换行函数与两个 macOS 换行函数（净减约 89 行）。
- 新增 `src/unittests/deskflow/ClipboardTextTests.{h,cpp}`：10 个用例（幂等、`\r\r\n`、往返稳定等），远端执行结果 **10 passed / 0 failed**。

**构建与部署**

- 构建机：`10.66.66.5`（macOS 26.7.2 arm64），Homebrew Qt 6.12.0 + CMake 4.4.5 + Ninja。
- 源码以 tarball 上传到 `~/deskflow-build/src`；构建脚本 `~/deskflow-build/build.sh`，部署脚本 `~/deskflow-build/deploy.sh`。
- 产物：自包含 `Deskflow.app`（84 MB，Qt 框架已由 macdeployqt 打包），`deskflow-core --version` 输出 `v1.27.0, protocol v1.8`。
- 已安装到 `/Applications/Deskflow.app`；原包备份于 `~/deskflow-backup/Deskflow-1.27.0.app`（67 MB）。
- 构建期 `OSXKeyStateTests` 失败（环境相关，与本次改动无关），其余 30/31 通过。

**阻塞项**

新包为 ad-hoc 签名，macOS 辅助功能（TCC）授权与原 Developer ID 签名（Nick Bolton, 5QCJCX8C37）不匹配，需要在“系统设置 → 隐私与安全性 → 辅助功能”中人工重新勾选一次；否则 GUI 会在 `checkMacAssistiveDevices()` 处直接退出。

**现场发现**

`~/Library/Deskflow/Deskflow.conf` 中同时存在 `switchDelay=250` 与 `switchDoubleTap=250`，但同文件里 `enableSwitchDelay=false`、`enableSwitchDoubleTap=false`，因此这两个值**未生效**；真机日志 `effective switch options: delay=0ms doubleTap=0ms` 已确认。边缘迟滞应改从交接链路与 Windows 注入路径排查（R3-2/R3-3），而不是改这两个数值。

### 2026-11-11（M0 观测）

- `Clipboard::add()` 在归一化确实改变了字节数时输出 `clipboard: normalised line endings format=%d bytes=%zu->%zu`，可直接证明 R1 在真实复制中生效。
- `IClipboard::marshall()` / `unmarshall()` 输出 `clipboard: marshalled/unmarshalled formats=%u bytes=%u`，逐格式 `clipboard: format=%u bytes=%zu`（VERBOSE）。
- 丢弃路径带原因码：`clipboard: dropped reason=empty|unsupported format=%d`；两处超限告警补上实际 `size` 字节数。
- `Server::switchComputer()` 打点：`switch latency: leave=%.1fms enter=%.1fms`，随后的第一帧输入输出 `switch latency: first_move=%.1fms`（主/副屏两条路径都覆盖）。
- `Server::handleOptions()` 结束时输出实际生效值：`effective switch options: delay=%.0fms doubleTap=%.0fms clipboard=%d clipboardLimit=%zuKB`。
- 远端增量编译通过（0 error），构建期 CTest **31/31 通过**；`deploy.sh` 已重新打包并安装到 `/Applications/Deskflow.app`，二进制内可检索到上述日志字符串。

### 2026-11-11（R2-5 macOS 图片格式面）

- 新增 `src/lib/platform/OSXClipboardImageConverter.{h,cpp}`：基于 CoreGraphics + ImageIO 的图片转换器，构造参数是 UTI，同时支持 `public.png` 与 `public.tiff`。
  - 读：UTI 图片 → 规范化 32bpp `BI_RGB`、bottom-up DIB（alpha 强制 0xFF），失败路径输出 `clipboard: dropped reason=unsupported ... detail=...`。
  - 写：内部 DIB → 对应 UTI 编码；`OSXClipboard::add()` 会遍历所有匹配 converter，因此一次复制会同时写入 png / tiff / bmp 三种 flavor。
- `OSXClipboard` 转换器顺序改为 PNG → TIFF → BMP，解决“macOS 截图默认是 TIFF/PNG、而 Deskflow 只认 com.microsoft.bmp”导致的图片无法同步。
- 平台 CMake 增加 ImageIO 链接；新增 `formatConvert_Png` / `formatConvert_Tiff` 两个单测（2×2 DIB 往返、头部字段、像素与 alpha、非法数据拒绝），远端 **7 passed / 0 failed**，`ClipboardTextTests` 仍 10/10。
- 已部署（core 时间戳 17:27:47），二进制内含 `public.png` / `public.tiff` 注册信息。

### 2026-11-11（R2-2 默认上限 + 粘贴板端到端验证）

- `Settings::defaultValue(Server::ClipboardSize)` 由 3 MB 调整为 **32 MB**（恰好容纳 4K 截图的未压缩 DIB）。
- 新增 `OSXClipboardTests::bitmapRoundTrip`：走真实系统粘贴板完成 `add(Bitmap)` → `has(Bitmap)` → `get(Bitmap)` 全链路，验证 png/tiff/bmp 三种 flavor 写入后能按 PNG 读回并还原成同一份 DIB（含像素与 alpha 断言）。
- 远端结果：`OSXClipboardTests` **8 passed / 0 failed**，全量 ctest **31/31 通过**；`deploy.sh` 已重新部署。
- M1 的 macOS 侧（文本 + 图片）至此完成；Windows（`CF_DIBV5`/`PNG`）与 X11（`image/png`）仍需对应平台的构建环境。

### 2026-11-11（R3-3 / R3-4：macOS 输入路径瘦身与诊断）

- `OSXComputer::showCursor()` / `hideCursor()`：`SetsCursorInBackground` 的 `CFStringRef` 改为进程内只创建一次，去掉每次光标显隐的两次 CFString 分配/释放。
- `OSXComputer::enter()`：主屏分支删除与 `showCursor()` 重复的 `CGAssociateMouseAndMouseCursorPosition(true)`（同一次切换里原本调用两次）。
- `logCursorVisibility()`：仅在日志级别 ≥ VERBOSE 时才调用已废弃且不可靠的 `CGCursorIsVisible()`，避免切换热路径上一次窗口服务器往返。
- event tap 健康（R3-4）：`kCGEventTapDisabledByTimeout` 现在输出 `count` 与距上次的毫秒数，连续 ≥5 次追加 WARN 提示检查辅助功能权限/系统负载；`enable()` 重置计数。
- 远端结果：编译 0 error，全量 ctest **31/31 通过**，`deploy.sh` 已部署（core 时间戳刷新）。

### 2026-11-11（R2-1 可见性 + 真实 TIFF 粘贴板用例）

- 服务器配置界面剪贴板分组新增说明文案（图片以未压缩 DIB 传输，1080p≈8 MB、4K≈32 MB，上限不够时调大），并把界面默认值从 3 MB 同步为 32 MB。
- 新增 `OSXClipboardTests::readTiffOnly`：直接用 Carbon Pasteboard API 只写入 `public.tiff` flavor（模拟 macOS 截图应用的行为），再经 `OSXClipboard::has/get` 读回并校验像素，证明“只有 TIFF、没有 PNG/BMP”的真实场景可用。
- 远端结果：`OSXClipboardTests` **9 passed / 0 failed**，全量 ctest 通过；已部署（GUI 时间戳 17:42:39，包内含提示文案）。
- **Windows 构建环境准备中**：本机已有 MSVC 19.51 与 Python 3.13，正在后台下载 Qt 6.10.3（MSVC2022 x64）与 OpenSSL 3.5.9 安装包，为 R2-3（`CF_DIBV5`/`PNG`）编译验证做准备。


### 2026-11-11（真机验证：R1 端到端通过）

授权完成后在 10.66.66.5 上实测（Mac 为服务端、Windows 客户端在同一局域网）：

- 服务端日志确认生效配置：`effective switch options: delay=0ms doubleTap=0ms clipboard=1 clipboardLimit=32768KB`（32 MB 上限已应用，切换延迟未启用）。
- **文本端到端**：Mac 上 `printf 'one\r\ntwo\r\nthree\r\n' | pbcopy`，4 秒后 Windows 剪贴板内容为
  `one\r\ntwo\r\nthree\r\n`（逐字节：111,110,101,13,10,116,119,111,13,10,116,104,114,101,101,13,10）。
  即 3 行、单 CRLF、**没有** `\r\r\n`、没有多余空行 —— R1 在真实 macOS→Windows 链路通过。
- **图片**：在 Mac 上把 PNG 写入粘贴板后，服务端日志出现 `clipboard changed` 与 `grabbed clipboard`，但因当前活动电脑是主屏（Mac），剪贴板只在切换到客户端时下发，`marshalled formats=0` 属预期；图片端到端需要在指针位于 Windows 时再复测。转换层已由单测与粘帖板用例覆盖。
### 2026-11-11（Windows 构建环境：受阻记录）

- 本机具备 MSVC 19.51（VS 18 Community）与 Python 3.13；已克隆并 bootstrap vcpkg，openssl:x64-windows 正在源码编译。
- Qt 获取失败（三次）：
  1. 官方源：部分归档被重定向到 tp.jaist.ac.jp 后读取超时；
  2. TUNA 镜像 6.10.3：qtdeclarative/qtsvg 校验和不匹配；
  3. TUNA 镜像 6.9.3（仅 qtbase/qttools/qtsvg）：qtsvg 仍校验和不匹配；
  4. USTC 镜像：缺少 6.10.3 的仓库元数据。
- 判断：当前网络对大体积归档存在截断/镜像不一致，短时间内无法稳定获取 Qt 预编译包。
- 解除条件（任一即可）：提供可用的 Qt 6.7+ MSVC 2022 x64 预编译包（离线安装器或内网镜像，解压到 C:\Qt 即可）、或提供一台可正常下载的 Windows 构建机、或把 Windows 端构建交给 CI。
### 2026-11-11（Windows 构建打通 + R2-3 完成）

环境：VS 2026 Community 18.12（MSVC 19.52）+ Qt 6.10.3 msvc2022_64 + OpenSSL 3.5.4（源码编译）+ CMake 3.29 Ninja。

打通过程中解决的三个问题：

1. **Qt 获取**：aqtinstall 经官方源/TUNA/USTC 均因超时或归档校验和不匹配失败；改为 curl 直接下载 TUNA 归档（sha256 与官方一致）+ py7zr 解压到 `C:\Qt\6.10.3\msvc2022_64`。
2. **lupdate 崩溃 0xC0000139**：lupdate.exe 依赖 Qt6Qml.dll，本机 PATH 里只有 Perforce 自带的老版本 Qt6Qml，导致入口点不匹配；补装 qtdeclarative 归档后解决。
3. **OpenSSL**：
make 默认目标会编译 apps/tests（耗时过长），库文件就绪后改用 `nmake install_dev` 只安装头文件与导入库。

R2-3 实现（Windows 图片格式面）：

- 新增 `MSWindowsClipboardPNGConverter`（WIC 编解码）：读 “PNG” → 规范 32bpp BI_RGB DIB，写 DIB → PNG（alpha 强制 0xFF）。
- `MSWindowsClipboard::add()` 改为遍历所有匹配 converter 全部写入，一次复制同时提供 `CF_DIB` 与 `PNG`；读取仍优先 `CF_DIB`，PNG 作为兜底。
- 新增单测 `MSWindowsClipboardTests::pngRoundTrip`（2×2 DIB 经 PNG 往返、头部/像素/alpha、垃圾数据拒绝）。

验证结果：

- `deskflow-core.exe v1.27.0.9999 (0d43a903), protocol v1.8` 可运行；GUI `deskflow.exe` 链接成功。
- `MSWindowsClipboardTests` **18 passed / 0 failed**。
- 全量 ctest：**29/30 通过**，唯一失败是 `ArchNetworkTests::setNoDelayOnSocket_returnsPreviousValue`（环境/网络策略导致 `setsockopt(TCP_NODELAY)` 返回失败，与本次改动无关）。
### 2026-11-11（Windows 客户端部署与跨机验证）

部署：用新编译产物替换 `C:\Program Files\Deskflow`（用户确认不备份），同时部署 Qt 6.10.3 运行库（windeployqt：Core/Gui/Network/Svg/Widgets + platforms/styles/tls 插件）与 OpenSSL 3.5.4 的 `libcrypto-3-x64.dll`/`libssl-3-x64.dll`。部署后服务 `Deskflow` 运行中，进程为 `deskflow.exe`/`deskflow-core.exe`/`deskflow-daemon.exe`，客户端版本 `v1.27.0.9999 (0d43a903) protocol v1.8`。

跨机验证（Mac 服务端 ↔ Windows 客户端，同一链路）：

- **文本 双向**：Windows `Set-Clipboard` → Mac `pbpaste` 得到 `winone\r\nwintwo\r\n`（两行、单 CRLF）；Mac `pbcopy` → Windows 侧同样正确。R1 在替换后的新客户端上仍然成立。
- **图片 Windows→Mac（CF_DIB）**：Windows 用 `Clipboard::SetImage` 放入 64×48 位图 → Mac 粘贴板出现 `PNGf / TIFF / BMP` 三种格式，服务端日志 `added 12328 bytes to clipboard format: 2`（Bitmap）。
- **图片 Windows→Mac（仅 PNG flavor）**：Windows 只放 `PNG` 格式（无 CF_DIB）→ 服务端日志出现 `marshalled formats=1 bytes=12340`，说明客户端通过新增的 PNG 转换器解码成功并发送了 DIB。
- **图片 Mac→Windows**：本次未验证，原因是服务端只有在“活动电脑是客户端”时才下发剪贴板（日志 `send grab clipboard ... to wangyz` 后无数据传输，`m_active == primary`）；需要把鼠标移到 Windows 屏幕后再在 Mac 上复制图片。

运行侧注意：服务端日志 `received client "wangyz" info shape=-1440,-464 6560x2560` 显示 Windows 端上报的屏幕尺寸是 6560×2560（DPI 缩放后的坐标），这正是 R4 需要处理的比例信息。
### 2026-11-11（macOS 剪贴板“改了却不生效”根因修复）

用户实测「Mac 截图复制 → Windows 粘贴不出来」，用 Verbose 日志 + AppleScript/Swift 两种方式写入粘贴板复现，定位到两个问题（都在 `OSXComputer`）：

1. **变化检测失效**：`OSXComputer::checkClipboards` 依赖 Carbon 的 `PasteboardSynchronize` 返回 `kPasteboardModified`。在 macOS 26 上，其它进程写入粘贴板后该标志经常仍为 0（日志 `checking clipboard` 后跟 `flags: 0`），服务端因此收不到 `ClipboardGrabbed` 事件。改为轮询 `[[NSPasteboard generalPasteboard] changeCount]` 判断变化，变化时才调用 Carbon `synchronize()` 刷新引用。
2. **读取时引用过期**：切换电脑会触发 `onClipboardChanged` 重发剪贴板，此时 Carbon 引用可能还没同步，读到空内容（日志 `opening clipboard` → `closing clipboard` → `marshalled formats=0` → `ignored ... (unchanged)`）。改为在 `OSXComputer::getClipboard` 读取前显式 `m_pasteboard.synchronize()`。

验证（修复后，Info 日志级）：Mac 放 2560×1440 图片 → 切到 Windows 客户端 → 服务端 `clipboard: decoded png 2560x1440 into dib 14745640 bytes`、`marshalled formats=1 bytes=14745652` → Windows 粘贴板 `image: 2560x1440`，格式含 `DeviceIndependentBitmap / PNG / Bitmap`。

同批日志还记录到切换耗时：`switch latency: leave=3.7ms enter=127.0ms`（R3-3 的观测数据）。
### 2026-11-11（macOS 打包缺失 QtSvg 导致图标按钮空白）

现象：客户端界面里“导出配置”“重启”两个按钮只剩空白方块。

原因：这两个按钮本来就是**纯图标**按钮（`btnSaveServerConfig` 用 `QIcon::ThemeIcon::DocumentSaveAs`，`btnRestartCore` 用 `view-refresh`），图标来自随包的 SVG 图标主题。`macdeployqt` 只在可执行文件链接了 QtSvg 时才打包 QtSvg，而 GUI 并未直接链接它，于是 bundle 里只有 `iconengines/libqsvgicon.dylib` 却没有 `QtSvg.framework` 与 `imageformats/libqsvg.dylib`，SVG 图标无法渲染。

修复：`deploy.sh` 在 macdeployqt 之后补拷 `qtsvg/lib/QtSvg.framework`（注意 Homebrew 的 `Frameworks/QtSvg.framework` 是符号链接，必须用 `lib/` 真实路径）与 `share/qt/plugins/imageformats/libqsvg.dylib`，并用 `install_name_tool` 把两者的 QtGui/QtCore 引用改成 bundle 内 `@executable_path/../Frameworks`，最后重新 ad-hoc 签名。

验证：`QT_DEBUG_PLUGINS=1` 启动后日志出现
`Got keys from plugin meta data QList("svg", "svgz", "svg.gz")`，无插件加载错误。
### 2026-11-11（截图工具只给文件 URL 时的图片同步）

飞书截图（`Ctrl+Shift+C`）这类工具会把图片短暂放进粘贴板（`public.png`/`public.tiff`），随后把内容换成 `public.file-url` 指向临时文件。内部剪贴板只认 Text/HTML/Bitmap，于是“文件 URL”阶段读不到任何数据，表现为时好时坏。

新增 `OSXClipboardFileImageConverter`：

- `getOSXFormat()` = `public.file-url`，`getFormat()` = Bitmap；
- `toIClipboard` 用 `CGImageSourceCreateWithURL` 解码文件，经 `CGBitmapContext` 渲染成 32bpp BGRA 后**按行反转**成内部的自底向上 DIB（CGBitmapContext 第 0 行在顶部），alpha 强制 0xFF；
- 注册在 PNG/TIFF/BMP 之后，只有在这三种图像格式都不存在时才生效；`fromIClipboard` 恒返回空（内部剪贴板不产生文件 URL）。

验证：`OSXClipboardTests::fileUrlImage`（写入临时 PNG → 粘贴板只放 `public.file-url` → 读 Bitmap 比对像素）通过，`OSXClipboardTests` **10 passed**；真机上文件 URL 复制后 Windows 端得到 `image: 1024x1024`（`DeviceIndependentBitmap/PNG/Bitmap`）。
### 2026-11-11（用户确认）

用户在真机上确认：飞书截图 `Ctrl+Shift+C` → 移到 Windows → `Ctrl+V` **可以正常粘贴**。此前“时好时坏”的现象由飞书把粘贴板从图像数据切换成 `public.file-url` 导致，已由 `OSXClipboardFileImageConverter` 覆盖。

回归状态：

- macOS：`OSXClipboardTests` **10/10 通过**；全量 ctest 中 `OSXKeyStateTests` 的 `fakePollShift` / `fakePollCharWithModifier` 在 SSH 会话下失败，这两个用例依赖向 GUI 会话投递合成按键事件，与本次改动无关（未触及 `OSXKeyState`）。
- Windows：`MSWindowsClipboardTests` **18/18 通过**；全量 29/30，唯一失败 `ArchNetworkTests::setNoDelayOnSocket_returnsPreviousValue` 属环境网络策略。

部署：`/Applications/Deskflow.app`（macOS，含 QtSvg 打包与文件 URL 转换器）与 `C:\Program Files\Deskflow`（Windows，含 PNG 转换器）均为本次构建。
### 2026-11-11（R3-2 注入去阻塞 + R4 DPI 感知）

**R3-2（Windows 注入去阻塞）**

`MSWindowsDesks` 原来对**每一个**鼠标移动都 `PostThreadMessage` 后立刻 `waitForDesk()`，把 core 事件线程挂起等 desk 线程处理完，等于每个事件一次线程往返。改为合并模型：

- 新增 `postMouseMove` / `postMouseRelativeMove`：在 `m_moveMutex` 保护下只保存“最新绝对坐标”和“累计相对增量”，并且只有当没有排队中的唤醒消息时才 `PostThreadMessage`（每条合并消息最多一次唤醒）；
- desk 线程用 `takePendingMove` / `takePendingRelativeMove` 取出一次并注入，然后 **`continue`**，不再置位 `m_deskReady`——合并消息没有等待者，若照旧通知会让后续 `waitForDesk()` 被“假唤醒”；
- 按键、滚轮、按键按下/抬起、desk 切换、`enter/leave`、光标查询等仍走原来的 `sendMessage`（投递 + 等待），保持确定性与顺序。

**R4（DPI 感知）**

`deskflow-core.exe.manifest` 增加：

`xml
<dpiAware>true/pm</dpiAware>
<dpiAwareness>PerMonitorV2, PerMonitor</dpiAwareness>
`

此前 core 未声明 DPI 感知，进程被 Windows DPI 虚拟化：`GetCursorPos`/`SM_CXSCREEN` 返回缩放后坐标，客户端上报的屏幕尺寸与实际注入的像素空间不一致。

验证：部署后服务端日志中的客户端形状由 `6560x2560` 变为 `4000x2560`（物理像素），`mt.exe -inputresource:...;#1` 确认安装后的 exe 内含 `PerMonitorV2`；Windows 全量单测 29/30（唯一失败仍是 `ArchNetworkTests` 的网络策略用例）。

R4 剩余部分：主屏用“加速后的位置差”、副屏用原始 HID 增量（`OSXComputer.mm`），以及协议层 scale 字段，尚未处理。