# Phase 14 托盘与拖入接缝 需求确认（v1.2）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-15
> 状态：✅ **已实施并验收**（2026-09-17）——D0–D11 全部落实；**R4 的事件映射经实测定稿**（见 §3.1 R4 回写块）
> 前置：`phase12-windowchrome-detailed-design.md` v1.5（**R9 惯例定稿 `D-SEAM-1`**）· `desktopnest-roadmap.md` v1.5（§5 Phase 14 规格 · §7.1 R-1 取证结论）
> 一句话：让框架从「窗口框架」迈向「桌面常驻应用框架」——补上**托盘图标（应用级）**与**文件拖入（窗口级）**两条 shell 集成通道，全部落在平台层内部，公共 API **零 Win32 类型**。
> 一句话补充：本阶段真正的价值不是「多两个 API」，而是**第一次为「应用级平台能力」定形态**——R9 三步惯例此前只覆盖窗口级。
> v1.0：初稿（§1 四条勘察事实 · §2 技术路线 · §3 R1–R13 三组 · §4 决策点 D0–D9 · §5 非目标 · §6 测试方向 · §7 影响面）
> v1.1（2026-09-16）外部评审「**方向通过，补齐契约后进初设**」——**6 项建议全部采纳**（① R11 按**完整事件链路**重写，原漏 `PlatformWindow → Window` 段且把 `EventRouter` 基类关系写成顺序；② UIPI 由「预期失败」改为「平台约束验证」；③ R1 补生命周期归属；④ D9 显式化为**双态模型**；⑤ D3 补 sink 生命周期前置；⑥ D6 拆出**同步/异步**为独立决策点 **D10**）；另补 D2 硬契约（隐藏 HWND 永不入 `Application::m_windows`）、D4 的 `HICON` 归属、**新增 D11**（托盘状态机语义）、R13 命名倾向、§6 测试分层原则

---

## 1. 背景与现状勘察

### 1.1 立项由来

`desktopnest-roadmap.md` v1.0 §5 定 Phase 14 = 「托盘与拖入接缝」，并写明这是 **Phase 12 R9 惯例的首次真正消费**。R9 于 2026-09-12 定稿为「**惯例而非抽象**」（`D-SEAM-1`）：

> ① `PlatformWindow` 加一个能力 `virtual`（接口即契约、零消息号）→ ② `Window` 加公共方法透传（应用层唯一入口）→ ③ 平台消息在具体实现的 `HandleMessage` 内消化。**三步都不新建接缝类 / 消息注册机制。**

⇒ 本阶段必须实测这条惯例**够不够用**。而托盘比 CaptionBar 硬：它是**异步回调消息**（`Shell_NotifyIcon` 的回调由系统发到指定 HWND），且**语义上是应用级而非窗口级**——这两个差异都指向 R9 惯例的盲区。

### 1.2 四条勘察事实（决定本阶段形态，全部有据）

| # | 事实 | 出处 | 直接后果 |
|---|---|---|---|
| **F1** | **message-only window 不接收广播消息**——*"A message-only window … is not visible, has no z-order, cannot be enumerated, and **does not receive broadcast messages**"*；Raymond Chen 补充：它在内部被当作 `HWND_MESSAGE` 的子窗口，**"Enumeration and broadcasting is done to top-level windows"** | MSDN「Window Size and Position」· Raymond Chen「What kind of messages can a message-only window receive?」 | 托盘承载窗口**不能**用 message-only window（它看起来是最干净的方案）⇒ 必须是**普通（可隐藏的）顶层窗口** |
| **F2** | **`TaskbarCreated` 由 shell 注册并广播给所有顶层窗口**——*"When the taskbar is created, it will register a message with the `TaskbarCreated` string and then **broadcast this message to all top-level windows**. When your taskbar application receives this message, it should assume that any taskbar icons it added have been removed and add them again."* | MSDN「Taskbar Creation Notification」 | explorer 重建后可**自愈**（重新 `NIM_ADD`）——这与 §7.1 的 A 路线被销毁形成**鲜明对照**，见下条 |
| **F3** | **UIPI/MIC 阻塞拖入**——*"UIPI blocks Windows messages being sent from process with a lower MIC level to one running at a higher MIC level. **Drag-and-drop is implemented via Windows messages.** … You can use `ChangeWindowsMessageFilterEx` … **Unfortunately, this isn't recommended** … The best solution is to **only use drag and drop between the same MIC levels**."* | Microsoft Learn（归档博客「Why Doesn't Drag-and-Drop work when my Application is Running Elevated?」） | 提权运行的应用**收不到**来自 explorer 的 `WM_DROPFILES`——这是平台级约束，非实现缺陷 |
| **F4** | **「最后一个窗口关闭 = 应用退出」是当前框架语义**（`Application.cpp:115-118`：`if (m_windows.empty()) Exit();`）；且 **`PlatformWindow` / `Window` 只有 `Show()`，没有 `Hide()`** | 源码实证 | **「关闭主窗口 → 最小化到托盘、应用继续运行」这一最基本的托盘用法，当前框架上走不通** ⇒ 见 §3.3 前置缺口 |

**F2 与 R-1 的对照（本阶段的战略依据）**：桌面层 A 路线（reparent 到 `WorkerW`）被 explorer 重建**杀死**，根因是**窗口层级关系**被外部进程持有；而托盘图标是**注册关系**——explorer 重建后系统会**主动广播通知**，应用据此重新注册即可。⇒ **同为 shell 集成，注册式可自愈、层级式不可**。这条判据应写回 `desktopnest-roadmap.md`（见 §7 影响面）。

### 1.3 已有 vs 缺（勘察盘点）

| 已有（可复用） | 缺（本阶段范围） |
|---|---|
| **R9 三步惯例**（`D-SEAM-1` 定稿，窗口级） | **应用级能力的对称形态**（托盘需要，R9 未覆盖） |
| `PlatformWindow` 能力面（Show/Release/Chrome/层级/状态/剪贴板/Timer） | **`Hide()`**（Show 的对称缺失——F4） |
| `PlatformWindowHost::OnEvent` 事件上行（翻译器 → 框架） | **`PlatformApplication` → 应用层的事件上行通道**（托盘回调要走的路径） |
| `EventType` 枚举 + `EventRouter` 虚方法体系 | **托盘事件 / 拖入事件**类型 |
| `Win32WindowClass`（自定义类名 + WndProc，已支持多实例） | **承载托盘回调的隐藏窗口**（新窗口类实例） |
| `LoadImageW` 图标加载先例（`kAppIconId = 102`，`Win32WindowClass.cpp:58`） | 托盘 HICON 的获取通道 |
| `WindowClass` 单例 + `SetDeferredCleanup` 的 `std::function` 注入先例 | — |
| `Run / RequestExit`（消息循环下沉，7.1.5） | — |
| **`WM_DROPFILES` / `Shell_NotifyIcon` / `NOTIFYICONDATA`** | **全部零命中**（roadmap §3 grep 实证） |

---

## 2. 技术路线（一段话定方向 + 三个硬骨头）

**两条通道的分层形态不同——这是本阶段最重要的结构判断：**

- **拖入 = 窗口级**（`DragAcceptFiles(hwnd, TRUE)` 是每窗口的开关）⇒ 走 **R9 惯例标准三步**：`PlatformWindow` 加能力 → `Window` 透传 → `Win32PlatformWindow::HandleMessage` 消化 `WM_DROPFILES` 后抛 Framework Event。**零新概念**。
- **托盘 = 应用级**（任务栏通知区里**一个应用一个图标位**，与窗口数量无关）⇒ R9 三步惯例**不适用**，需要一个**对称的应用级分支**：`PlatformApplication` 加能力 → `Application` 透传 → `Win32PlatformApplication` 消化回调消息后抛事件。**这是本阶段必须定下来的东西**——否则后续所有「应用级 shell 集成」（单实例、全局热键、开机自启）都要重新讨论一遍。

**三个硬骨头：**

1. **承载窗口选型**——托盘回调需要一个 HWND 收消息，而 F1 排除了最干净的 message-only 方案；F2 又要求它必须能收广播。⇒ 正解是**框架自建的隐藏顶层窗口**（Raymond Chen 的建议正是"自己造一个隐藏窗口（非 message-only）"），且它与任何 `Window` 的生命周期解耦。
2. **HDROP 生命周期**——`WM_DROPFILES` 的 `wParam` 是 `HDROP`，用完必须 `DragFinish`。若把它原样抛给应用层，等于把 Win32 类型 + 释放责任一起外泄（**同时违反分层律与生命周期契约**）。⇒ 平台层必须**解析成 UTF-8 路径列表 + `DragFinish` 之后**再抛事件，应用层**永远看不到 `HDROP`**。
3. **explorer 重建自愈（F2）**——`TaskbarCreated` 是平台细节，若要求每个应用自己处理，等于把平台细节推给消费者（与「框架吸收平台细节」的既有立场矛盾）。⇒ 倾向**框架内建自动重加**（见 D9）。

---

## 3. 需求条目

### 3.1 托盘（应用级）· R1–R7

**R1 · 托盘图标生命周期**：添加 / 更新 / 移除；**析构与退出时必须移除**（`NIM_DELETE`）——**幽灵图标**（进程已退出但图标滞留、需鼠标划过才消失）是托盘实现的经典缺陷，本阶段列为**硬要求**。**所有权归属（v1.1 补）**：图标生命周期**由 `PlatformApplication` 拥有**——应用销毁时必须在**承载窗口销毁之前**同步尝试对每个活跃图标 `NIM_DELETE`（责任主体在平台层，非消费者）。**幂等要求（v1.1 补）**：移除必须容忍「Shell 实际状态与框架内部状态不一致」（正常退出 / explorer 重建 / 应用主动移除 都会造成错位）⇒ **移除未注册项为 no-op**（不抛异常、不记错误）；重复 Add 的语义见 **D11**。

**R2 · 图标来源**：概念形如「设置托盘图标（资源 / 文件二选一）」。来源选项见 **D4**。图标尺寸由系统按通知区 DPI 选取（多尺寸 ico 自动命中）。

**R3 · 悬停提示文本**：可设置 / 可更新（`szTip`）。

**R4 · 交互事件**：左键单击 / 左键双击 / 右键（含键盘激活路径）。事件**携带锚点坐标**（用于右键菜单定位与"就地弹窗"）。回调消息版本影响事件语义（见 **D5**）。

> **v1.2 实测回写（2026-09-17 手测——实施期定稿）**：v4 下的真实映射与本节设想有四处不同，均已按实测落到详设 §3.3：
> ① 左键单击 = `WM_LBUTTONUP`（**不是** `NIN_SELECT`）；
> ② `NIN_SELECT` 与 `WM_LBUTTONUP` **同源重复**——只认后者，否则一次单击上报两次；
> ③ 双击 = 系统 `WM_LBUTTONDBLCLK`，且其后**紧跟一个属双击序列的 `UP`**（须吞除，否则一次双击报 3 个事件）；
> ④ 锚点坐标为**屏幕坐标**（A5 实测确认：与同刻 `GetCursorPos()` 重合）。

**R5 · 右键菜单**：概念形如「在托盘图标处弹出原生菜单，返回被选中的项 ID」。范围见 **D6**；**调用形态（同步返回 vs 事件驱动）见 D10**。

**R6 · explorer 重建自愈**：见 **D9**。

**R7 · 与 `Window` 解耦**：托盘图标**不绑定任何窗口**——窗口全部关闭、销毁、重建都不影响图标存在（承载窗口由框架自建并管理）。⇒ 这是"应用级"的直接推论，也是与 roadmap §5「M4 常驻」的衔接点。

### 3.2 拖入（窗口级）· R8–R11

**R8 · 窗口级开关**：概念形如「启用 / 停用本窗口的文件拖入」。默认**关闭**（不改变既有窗口行为——零回归底线）。

**R9 · 拖入事件形态**：事件携带 **UTF-8 路径列表** + **落点坐标**（客户区坐标）。**`HDROP` 绝不出现在公共 API**（§2 硬骨头 2）。是否区分"文件 / 文件夹"、是否去重，归初设。

**R10 · 生命周期闭合**：`DragFinish` 在平台层内完成，且**在事件抛出之前**——保证事件消费者拿到的是**已脱离系统资源**的纯数据（应用侧零释放责任）。

**R11 · 派发路径（v1.1 按实测链路重写）**：DropEvent 由平台层生成后必须走**既有事件上行链路**，不得建立独立 Drop 回调通道：

```
WM_DROPFILES
  → Win32PlatformWindow::HandleMessage（解析 HDROP → UTF-8 路径列表 + DragFinish）
  → Framework Event（拖入事件）
  → PlatformWindowHost::OnEvent（Window 实现）
  → Window::OnEvent → m_application.OnEvent(event)     [Window.cpp:448-452]
  → Application::OnXxx（EventRouter 基类虚方法）
  → HitTest（RootWidget::HitTest——按落点逆序）
  → Dispatch → Bubbling（Application 内 while + GetParent）
```

> 📌 **v1.1 更正**：v1.0 原文写作「`EventRouter` → `Application` → `HitTest` → Bubbling」，有两处不准确——① **漏了 `PlatformWindow → Window` 整段**；② **`EventRouter` 是 `Application` 的基类（`Application` 继承 `EventRouter`），不是前驱环节**。此处已按源码实测（`Window.cpp:448-452` · `Application.cpp:181-226`）重写。

⇒ 拖到哪个控件上方，事件自然到达谁（例如拖到 TextBox 上 vs 拖到空白区）。**"拖拽经过"（drag-over）明确不支持**——那要 `IDropTarget`，已列为非目标。

### 3.3 常驻语义前置（F4）· R12–R13

**R12 · `Hide()`**：`Show()` 的对称补全（窗口级，走 R9 惯例标准三步）。

**R13 · 退出策略**：当前"最后一个窗口关闭 ⇒ `Exit()`"（`Application.cpp:116`）使"最小化到托盘"无法实现。需要一个**显式开关**（概念形如「最后窗口关闭时是否退出」，默认 `true` = 零行为变更），让常驻应用能活到托盘交互结束。⇒ 归属见 **D8**。**命名倾向（v1.1 补）**：采用**直接描述触发条件**的命名（概念形如 `SetQuitOnLastWindowClosed(bool)`，默认 `true`），**避免双重否定式命名**（如 `SetStayAlive(false)`——极易读错）。

> ⚠️ **R12 / R13 是本阶段的"隐性前置"**：不做这两条，托盘做出来也无处可用——用户点关闭按钮，应用直接退出，托盘图标随之蒸发（而 R1 的"退出时必须移除"会让这个过程看起来"很正常"，掩盖问题）。**建议将它们纳入本阶段**，理由见 D8。

---

## 4. 决策点（D0–D11——全部给倾向待拍板）

| # | 决策 | 选项 | **倾向** | 理由 |
|---|---|---|---|---|
| **D0** | 范围与实现顺序 | **A** 一个 Phase 三组 R（拖入 → 常驻前置 → 托盘）/ **B** 拆两个 Phase（拖入 / 托盘）/ **C** 只做拖入，托盘另立 | **A（顺序：拖入 → R12/R13 → 托盘）** | 拖入是 R9 标准形态、最简，先做可验证惯例；常驻前置是托盘前提；托盘最难、放最后暴露风险。三组同属"shell 集成"，共享文档与测试设施，拆开反而重复 |
| **D1** | 托盘能力挂载点 | **A** `PlatformApplication`（应用级）/ **B** `PlatformWindow`（窗口级，复用 R9）/ **C** 新建独立平台抽象（如 `PlatformTray`） | **A** | 托盘语义是"每个应用一个图标位"，挂窗口级语义错误（多窗口时谁持有？）；C 引入新抽象类＝R9 裁决明确拒绝的方向。A 同时把「应用级能力」这条惯例分支立起来（§2） |
| **D2** | 承载窗口形态 | **A** 框架自建**隐藏顶层窗口**（独立窗口类 + WndProc）/ **B** 复用第一个 `Window` 的 HWND / **C** message-only window | **A** | **C 已被 F1 否证**（收不到 `TaskbarCreated` 广播 ⇒ 无法自愈）；**B 有双重缺陷**：承载者销毁即回调无门（且图标不自动消失 ⇒ 幽灵图标），多窗口时选择不确定。A 与窗口生命周期彻底解耦（R7）。**★ 硬契约（v1.1 补）**：该隐藏顶层 HWND 是 `Win32PlatformApplication` 的**内部平台资源，不是 ECDI `Window`**——**永不进入 `Application::m_windows`**，不参与窗口枚举 / 所有权登记 / 事件派发，由 `Win32PlatformApplication` 自行创建、持有、销毁；**不新增独立平台对象类**（与 R9「接缝是惯例不是抽象」一致） |
| **D3** | 事件上行通道（Application 侧） | **A** 复刻 `SetDeferredCleanup` 的 `std::function` 注入（概念形如 `SetEventSink`）/ **B** 新建 `PlatformApplicationHost` 抽象类（对称 `PlatformWindowHost`） | **A** | 与既有先例**同款模式**（`PlatformApplication` 已有 `std::function` 注入），零新抽象类；B 违反 R9 裁决"不新建接缝类"，且为一个回调引入抽象类不成比例。**★ 生命周期前置（v1.1 补，归初设闭合）**：sink 三问必须在初设回答——① 谁设置 / 何时设置（建议 `Application` 构造后、承载窗口创建前）；② 何时清空（建议 `PlatformApplication` 销毁、承载窗口销毁**之前**）；③ **保证 sink 被调用时 `Application` 必然存活**（**禁止异步回调访问已销毁的 `Application`**）。销毁顺序**不得留到实现阶段临时决定** |
| **D4** | 托盘图标来源 | **A** exe 资源 ID（`int`）/ **B** `.ico` 文件路径（UTF-8）/ **C** 框架 `Image` → 转 HICON / **D** 复用窗口类图标 | **A（默认同 `kAppIconId`）** | 单文件产物友好（DesktopNest 目标形态）；与 `Win32WindowClass.cpp:58` 既有加载先例同源；零外部文件依赖（框架"无第三方/无外部资源"原则同源）。C 最"框架味"但成本高（`CreateIconIndirect` + 掩码/alpha），二次用例再说；B 引入文件依赖；D 语义受限（应用换图标要先 `WM_SETICON`）。**★ 资源生命周期（v1.1 补）**：`HICON` 的加载与销毁全部收在平台层内部——资源 ID → `LoadImageW` → 平台持有 → `NIM_DELETE` 后按 `LR_SHARED` 与否决定是否 `DestroyIcon`；**`HICON` 生命周期不得散落在 `Application` / 托盘状态 / 窗口类之间**，公共 API 只出现资源 ID |
| **D5** | 回调消息版本 | **A** `NOTIFYICON_VERSION_4`（`NIM_SETVERSION`）/ **B** 传统版本（`wParam`=图标 ID、`lParam`=鼠标消息） | **A** | 版本 4 下 `lParam` 携带**通知事件码**（`NIN_SELECT` / `NIN_KEYSELECT` / `NIN_POPUPOPEN` / `WM_CONTEXTMENU`），`wParam` 携带**锚点坐标**——键盘激活路径与就地弹菜单都需要它；传统版本只有鼠标消息、无坐标。MSDN 明确两者语义差异 |
| **D6** | 右键菜单边界 | **A** 框架提供"原生菜单 + 文本/ID 列表 + 返回选中 ID"（`TPM_RETURNCMD`）/ **B** 只抛事件、菜单交应用 / **C** 自绘菜单 | **A** | **B 物理上不可行**——应用层拿不到 HWND，无法调 `TrackPopupMenu`；C 是独立大工程（自绘菜单＝另一套命中/滚/键盘导航），YAGNI。A 中 `TPM_RETURNCMD` 让结果**不经 `WM_COMMAND`**，返回值语义干净。菜单**只支持一级 + 纯文本 + ID**（图标/勾选/子菜单不做）；**调用形态见 D10** |
| **D7** | UIPI 立场（F3） | **A** 记录为已知约束、**不**做处理 / **B** 提供 `ChangeWindowMessageFilterEx` 白名单开关 / **C** A + 文档明示 | **C** | 官方立场明确（*"isn't recommended"*、*"best solution is to only use drag and drop between the same MIC levels"*）；白名单有**安全含义**，框架不该替应用做安全决策。⇒ 约束写进文档（非目标 + 记账），实现零负担 |
| **D8** | R12/R13 归属 | **A** 纳入本阶段 / **B** 另立阶段 / **C** 不做（要求应用自己规避） | **A** | **C 不成立**：`Hide()` 缺失 + `Exit()` 自动触发，应用侧**无任何手段**规避（既不能隐藏窗口，也不能阻止退出）。B 会把托盘变成"做出来但不可用"；且 R12/R13 各自都是 R9 惯例标准三步、成本极低。⇒ 纳入本阶段 |
| **D9** | explorer 重建自愈归属 | **A** 框架内建（自动 `NIM_ADD` + 停用状态同步）/ **B** 抛 Framework Event 让应用重加 | **A** | `TaskbarCreated` 是**平台细节**；要求应用处理＝把平台细节推给消费者（与"框架吸收平台细节"立场矛盾）。框架内建后应用**零感知**，且 R1 的"生命周期归框架"语义才自洽。**★ 双态模型（v1.1 显式化）**：必须区分 **desired state**（应用意图）与 **shell registration state**（Shell 实际）——`TaskbarCreated` **只按 desired state 恢复**：desired=ON 且 shell=OFF（explorer 重建）⇒ 重新 `NIM_ADD` ✅；desired=ON 且 shell=ON ⇒ 幂等（按 D11）；**desired=OFF（应用已主动移除）且 shell=OFF ⇒ 绝不重加** ❌——这是最容易写错的分支，v1.0 记的「停用状态同步」即指此 |
| **D10** | 托盘菜单调用形态（v1.1 新增） | **A** **同步 API**：`ShowTrayMenu(...)` 内部 `TrackPopupMenu(TPM_RETURNCMD)`，直接**返回选中项 ID**（`0` = 未选中 / 取消）/ **B** 事件驱动：抛「菜单请求」事件，应用弹菜单后回传结果 | **A** | `TPM_RETURNCMD` 使结果不经 `WM_COMMAND`，返回值语义干净；B 需「请求—回传」两段式协议 + 状态挂起，为一级菜单引入不成比例，且应用层拿不到 `HWND`（D6 已述）。⚠️ 同步语义的**限制**（非缺陷）：菜单显示期间消息循环由 `TrackPopupMenu` 接管 ⇒ 该期间不派发框架事件（系统模态菜单固有行为） |
| **D11** | 托盘状态机语义（v1.1 新增） | **A** 宽容：`Add` 重复 = 隐式 `Update` · `Update` 未注册 = 自动 `Add` · `Remove` 未注册 = no-op / **B** 严格：三者均拒绝 + Warning | **A** | 托盘是**幂等配置**语义而非事务语义——应用常在「窗口重建 / 配置重载」路径上重复设置图标，宽容语义让这些路径无需自维护状态；B 会使 R7「与 Window 解耦」变成「应用必须自己跟踪图标状态」，与「框架吸收复杂度」立场矛盾。状态集 = `Absent` / `Active` 两态；**完整转移表归初设**（需求阶段只定语义边界——skill 条 6） |

---

## 5. 非目标（YAGNI 圈定）

- **`IDropTarget` / `IDropSource`**（OLE 拖放）——roadmap 已定：拖入走 `DragAcceptFiles`；**拖出整个砍掉**；拖拽经过的**光标反馈**不做
- **拖入的 UIPI 白名单**（D7 定案：只记录约束）
- **自绘托盘菜单 / 带图标菜单 / 勾选项 / 子菜单**
- **通知气泡**（`NIF_INFO` / `NIF_SHOWTIP`）——Win10+ 已由操作中心接管，语义与视觉均已迁移
- **托盘图标动画 / 多图标 / GUID 标识**（`guidItem`——MSDN 社区明确"限制更多、问题更多，建议只用 nID"）
- **自定义 tooltip 窗口**（`NIF_SHOWTIP` 的自绘提示）
- **全局热键 / 单实例 / 开机自启**（同属"应用级 shell 集成"，但各自独立——本阶段只立**惯例分支**，不做能力）
- **`Window` 的多显示器 DPI 感知**（框架级 Deferred，`roadmap-deferred.md` 记账）
- **菜单的键盘导航增强 / 主题化**（走系统原生外观）

---

## 6. 测试 / 验证方向

| 组 | 内容 | 判据 |
|---|---|---|
| **拖入（自动）** | `WM_DROPFILES` 探针注入（构造 `HDROP` 或直接发消息）→ 事件内容 | 路径列表 UTF-8 正确、落点坐标正确、`HDROP` 已释放 |
| **拖入（自动）** | 未启用拖入的窗口收到 `WM_DROPFILES` | 无事件、无异常（默认关闭语义） |
| **事件派发（自动）** | 拖到子控件上方 / 空白区 | 走既有 HitTest + Bubbling 路径（与鼠标事件同族断言） |
| **托盘（自动）** | 承载窗口创建 / 销毁；`NIM_ADD` / `NIM_DELETE` 调用配对 | **析构路径不留图标**（R1 硬要求）——可用替身探针 |
| **托盘（自动）** | `TaskbarCreated` 消息（可 `SendMessage` 模拟广播）→ 重加 | 重加被触发、停用态不重加（D9） |
| **常驻（自动）** | R13 开关两态：最后窗口关闭 → 退出 / 不退出 | 两态都可断言（消息循环退出条件） |
| **回归** | 既有全部用例 | **零回归**（重点：窗口销毁路径 / 事件分派 / `PlatformApplication` 契约） |
| **手测** | ModelProbe 接入托盘（现成图标资源）+ 拖入日志 | DebugView 日志 + 通知区观察 |
| **手测** | **explorer 重启后图标仍在**（任务管理器重启资源管理器） | 与 §7.1 的 E 路线手测同款方法——**这次预期是"自愈成功"**（F2） |
| **手测** | 提权运行下的拖入行为（F3 约束复核） | **平台约束验证（v1.1 措辞修正）**：确认 ECDI 的行为描述与官方一致——即「不承诺跨完整性级别拖入成功」，结果以系统消息过滤行为为准；**不把「失败」写成硬验收判据**（平台行为变化时不应导致假失败） |

> 📌 **测试分层原则（v1.1 补）**：托盘自动测试**不得依赖真实通知区域 / explorer 当前状态 / 桌面环境**——① 平台层托盘状态机（`Absent` / `Active` 转移）② Shell API 调用（`NIM_ADD` / `NIM_DELETE` 配对，走替身探针）③ `TaskbarCreated` 恢复逻辑（合成消息）三层各自可断言；**真实 explorer 重启保留为手测**。拖入的 `HDROP` 生命周期同理——自动测试断言「事件返回后 `HDROP` 已被 `DragFinish`」，必要时经**内部平台测试缝**计数，而非在测试里窥探系统堆。

---

## 7. 影响面

| 类别 | 项 |
|---|---|
| 新增 Public | 托盘相关头（概念归属待初设定：`Window/` 域 or 新增域）+ 2 个事件（托盘 / 拖入）——**净增约 2–3 个**（86 → 88~89） |
| 修改 Public | `PlatformWindow.h`（+`Hide` +拖入开关）；`PlatformApplication.h`（+托盘能力 +`std::function` 事件注入）；`Window.h`（+`Hide` +拖入开关透传）；`Application.h`（+托盘透传 +退出策略开关）；`EventType.h`（+2 枚举）；`EventRouter.h`（+2 虚方法） |
| ⚠️ **测试替身同步** | `PlatformApplication` / `PlatformWindow` 若加**纯虚**，替身必须同步补 override（skill 条 33 先例：`AnimationTests.cpp` / `ProgressBarTests.cpp`；Phase 12 的 10 方法、Phase 13 的 `IsClientInteractiveAt` 都触发过）。⇒ **倾向用非纯虚或独立能力接口规避**，归初设评估 |
| 修改 Internal | `Win32PlatformApplication`（承载窗口 + 托盘状态 + 回调消化 + 广播自愈）；`Win32PlatformWindow`（`WM_DROPFILES` 分支 + `DragAcceptFiles`）；`Win32WindowClass`（新窗口类实例：托盘宿主） |
| 文档 | 新增本文件；README 索引同步；**`desktopnest-roadmap.md` §5 Phase 14 状态回写 + §7 补「注册式 vs 层级式」判据（F2 与 R-1 的对照）** |
| 明确不动 | 渲染四层（Widget → PaintContext → CommandBuffer → Renderer）、`Window` 所有权契约、Phase 12 四消息拦截骨架、Phase 13 命中委托、布局与尺寸体系 |

---

## 8. 修订记录

- v1.2（2026-09-17）**实施并验收——R4 事件映射按实测回写**：v4 下真实映射与需求初稿设想四处不同（左键 = `WM_LBUTTONUP` / `NIN_SELECT` 同源去重 / 双击 = 系统 `DBLCLK` 且尾部 `UP` 须吞除 / 锚点为屏幕坐标），全部落详设 §3.3 并附实测依据。状态转「已实施并验收」。
- v1.1（2026-09-16）**外部评审「方向通过，补齐契约后进初设」——6 项建议全部采纳（1 项含事实更正）+ 4 项补充**：
  - **① R11 重写（含更正）**：按**源码实测链路**改写为 `WM_DROPFILES → Win32PlatformWindow::HandleMessage → Framework Event → PlatformWindowHost::OnEvent → Window::OnEvent → Application::OnEvent（EventRouter 基类）→ HitTest → Dispatch → Bubbling`。**v1.0 原文有两处不准确**——漏 `PlatformWindow → Window` 整段、把 `EventRouter`（`Application` 的基类）写成前驱环节。⚠️ 评审称原文「会绕过层次」——**方向对但论断不准确**：文档只是链路写不全，未主张绕过（本轮唯一需更正评审结论之处）。
  - **② UIPI 测试判据**：由「预期失败」改为「**平台约束验证**」——不把平台约束写成硬验收（系统行为变化时不应假失败）。
  - **③ R1**：补**所有权归属**（生命周期归 `PlatformApplication`，承载窗口销毁**之前**同步 `NIM_DELETE`）+ **幂等要求**（移除未注册项 = no-op）。
  - **④ D9**：显式化为**双态模型**（`desired state` ≠ `shell registration state`），`TaskbarCreated` 只按 desired 恢复；**desired=OFF 时绝不重加**是最易写错分支。
  - **⑤ D3**：补 **sink 生命周期前置**（设置/清空时点 + 禁止异步回调访问已销毁 `Application`），销毁顺序归初设且不得临时决定。
  - **⑥ D6 → 拆出 D10**：托盘菜单**调用形态**（同步返回 ID vs 事件驱动）升为独立决策点（与 D6 的「范围边界」正交）。
  - **补充 4 项**：D2 硬契约（隐藏 HWND **永不进入** `Application::m_windows`、不新增独立平台对象类）· D4 `HICON` 资源生命周期归属 · **D11 新增**（托盘状态机语义：宽容 vs 严格，状态集 `Absent`/`Active` 两态，完整转移表归初设）· R13 命名倾向（`SetQuitOnLastWindowClosed`，避开双重否定）· §6 测试分层原则。
  - **保持不动的评审共识**：`PlatformApplication` 挂载点（D1）· 隐藏顶层窗口（D2）· `std::function` 注入（D3 方向）· EXE 资源 ID（D4）· `NOTIFYICON_VERSION_4`（D5）· 不处理 UIPI（D7）· R12/R13 纳入本阶段（D8）· 框架内建自愈（D9 方向）。
  - **评审明确反对的扩张**（记为范围纪律）：不因已有 `PlatformApplication` 而顺手塞 全局热键 / 单实例 / 开机自启 / Toast / Shell 集成管理器——继续留在 §5 非目标。
- v1.0（2026-09-15）**需求确认初稿**：
  - §1 背景与现状勘察——立项由来（R9 惯例首次消费）· **四条勘察事实 F1–F4**（message-only 不接收广播 / `TaskbarCreated` 只有顶层窗口收得到 / UIPI 阻塞拖入 / 「最后窗口关闭即退出」+ 无 `Hide()`）**全部带出处** · F2 与 §7.1 R-1 的「注册式 vs 层级式」对照 · 已有 vs 缺盘点。
  - §2 技术路线——**两条通道分层形态不同**（拖入窗口级走 R9 标准三步 / 托盘应用级需扩展惯例分支）· 三个硬骨头（承载窗口选型 / `HDROP` 生命周期 / explorer 重建自愈）。
  - §3 需求条目 **R1–R13 三组**：托盘 R1–R7（含**幽灵图标硬要求** + 与 `Window` 解耦）· 拖入 R8–R11（`HDROP` 绝不出公共 API + 走既有派发路径）· **常驻语义前置 R12–R13**（`Hide()` + 退出策略——标注为"隐性前置"）。
  - §4 决策点 **D0–D9 全部给倾向待拍板**（范围与顺序 / 挂载点 / 承载窗口 / 事件上行通道 / 图标来源 / 消息版本 / 菜单边界 / UIPI 立场 / 前置归属 / 自愈归属）。
  - §5 非目标（OLE 拖放、自绘菜单、气泡提示、GUID 标识、全局热键等 9 项圈定）· §6 测试方向（含 **explorer 重启手测**——与 R-1 手测同款方法但**预期相反**）· §7 影响面（含测试替身同步风险与规避倾向）。
  - **需求阶段边界自律**（skill 条 6）：本文档不含头文件草案与方法签名——全部归初步设计；`WindowLayer` 式的"概念形如"仅用于表达意图。
