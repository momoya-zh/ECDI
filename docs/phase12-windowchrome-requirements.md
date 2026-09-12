# Phase 12 WindowChrome 需求确认（v1.2）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-08（v1.1：2026-09-11 追加 R9/R10；v1.2：2026-09-11 外部评审 14 条全采纳；2026-09-12 §7 影响面措辞回写）
> 状态：**v1.2 外部评审通过——可进初设**（6 决策已拍板；R10 `Desktop` 档 spike 前置）
> 前置：Phase 10 库化 ✅ / Phase 11 图片解码 ✅（81 Public 头）
> 一句话：自定义标题栏/无边框窗口——应用不再受系统灰标题栏限制，客户区即整窗
> v1.1 触发：DesktopNest 规划（`docs/desktopnest-roadmap.md`）发现本阶段须追加 2 条需求（R9 平台消息扩展接缝 / R10 窗口层级能力）——否则 Phase 13（托盘与拖入）与桌面常驻类应用无路可走

---

## 1. 背景与现状勘察

| 项 | 现状 |
|---|---|
| PlatformWindow 抽象面 | Show/Release/Invalidate/GetClientSize/RenderContext/IME caret/Clipboard/Timer——**零 chrome 概念**（grep 实证） |
| 消息处理 | 框架从未碰过 `WM_NCCALCSIZE`/`WM_NCHITTEST`/`WM_NCACTIVATE`——全新领域 |
| 窗口像素测试先例 | RendererTests 已有 `WS_POPUP` 真窗口 + HWND 像素读回模式（视觉验收接缝现成） |
| 立项动机 | GUI 框架的标志能力：应用自绘标题栏（品牌化 UI），系统灰栏消失 |

**v1.1 追加勘察（2026-09-11）**：

| 项 | 现状 |
|---|---|
| 窗口 z 序控制 | `PlatformWindow` 仅 `Show()`——**无任何层级/z 序 API**（读 `PlatformWindow.h` 实证） |
| 应用层消息可达性 | `PlatformWindowHost` 仅 `OnPaint`/`OnResized`/`OnExitSizeMove`/`OnEvent`/IME 五类回调——**应用层完全够不着窗口消息**（grep 实证：全项目无任何注册自定义消息处理的接口） |
| 桌面层宿主 | `WorkerW`/`Progman`/`FindWindowW` 零命中 |

## 2. 技术路线（业界标准：保留样式 + 改 NCCALCSIZE）

**不走 `WS_POPUP`**（丢系统动画/Aero Snap/最小化动画）——保留 `WS_OVERLAPPEDWINDOW` 全样式，通过消息拦截实现无边框：

```text
WM_NCCALCSIZE   → 返回 0：客户区 = 整个窗口（系统标题栏/边框消失）
WM_NCHITTEST    → 命中测试：标题栏区返回 HTCAPTION（拖动/双击最大化），
                  边缘 inset 返回 HTLEFT..HTBOTTOMRIGHT（八向缩放）
WM_NCACTIVATE   → 返回 DefWindowProc 结果但禁掉非客户区重绘（防闪烁）
DWM             → DwmExtendFrameIntoClientArea（1px 阴影保留）/ Win11 圆角属性
最大化修正      → 最大化时 NCCALCSIZE 补偿负边距（Windows 默认把窗口撑出屏幕一圈）
```

## 3. 需求条目

### R1：Chrome 模式 API（Public——经 Window 透传）

`Window` 层公开 API（PlatformWindow 抽象同步扩展虚接口，Win32 实现——依赖方向单向律）：

```cpp
window.SetChromeMode(ChromeMode::Borderless);   // 形态：枚举 + 分解配置项（CaptionHeight/ResizeInset）
```

**切换时机（v1.2 补充——评审 §14）**：明确「是否允许运行时切换」。倾向**允许**（Normal ↔ Borderless），但须在初设处理：样式变更 + 框架重算（`SetWindowPos(SWP_FRAMECHANGED)`）+ DWM 状态 + 重绘 + 命中测试基线 + 最大化态迁移。若初设判定成本过高，可降级为「仅创建后初始化期设置，运行时切换不承诺」——初设拍板。

### R2：无边框客户区

`WM_NCCALCSIZE`（wParam=TRUE 时返回 0）——客户区扩满窗口；样式保留 `WS_OVERLAPPEDWINDOW`（系统缩放动画/Aero Snap 全保留）

### R3：命中测试（拖动/缩放/双击最大化）

- 标题栏区（`captionHeight` 可配置）→ `HTCAPTION`：拖动移动、双击最大化/还原——**系统免费获得**
- 四边 + 四角 inset（默认 8，可配置）→ 八向缩放命中
- **单位（v1.2 修正——评审 §9）**：`captionHeight`/`resizeInset` 一律**逻辑坐标**（DIP，与框架尺寸体系一致）——**禁止**在公共 API 以设备像素表达；Win32 命中测试时按 DPI 缩放换算（ECDI 当前 DPI 缩放未落地 = 1:1 映射，未来 DPI 落地时 API 语义不变）
- HTCAPTION 区自动获得 Aero Snap（拖到屏幕边缘分屏）——零额外代码

### R4：最大化修正

最大化时 Windows 会将窗口外扩一圈（边框尺寸）——`WM_NCCALCSIZE` 中检测最大化状态并收缩客户区。

**验收基准（v1.2 补充——评审 §10）**：最终判据**围绕 `MONITORINFO.rcWork` 验证**——最大化后客户区**不覆盖任务栏**、**不残留系统边框空白**。不得简单写成 `if (maximized) clientRect = monitorRect`（那是 `rcMonitor` 语义——会盖任务栏）；具体收缩算法初设定。

### R5：CaptionBar 范围（v1.2 定案——评审 §7：**MVP 不做 Widget**）

**决策：Phase 12 只做 Window Chrome，不做 CaptionBar 控件**（推翻 v1.1 的「倾向 b」）：

- 理由：一旦做 Widget，立即牵连 Button 样式 / 四态 / 图标 / DPI / 主题 / 标题布局 / 最大化状态同步 / 非激活态 / 系统菜单 / 自定义按钮扩展——把「WindowChrome 阶段」膨胀成「Chrome + 新 Widget + 新 Theme + 状态同步」，违背 YAGNI
- MVP 交付：`ChromeMode` + `CaptionHeight` + `ResizeInset` 三件套——框架负责**窗口行为**，应用负责**UI 摆放**：

```text
Window
└── RootWidget
    ├── 标题区（应用自己放 Label/Button/自定义控件——HTCAPTION 拖动系统免费）
    └── 内容区
```

- **CaptionBar 控件推迟**：待真实项目（ModelProbe / DesktopNest / Demo）重复实现标题栏 ≥2 次后再立项（「二次用例出现再抽象」）
- 这也更能体现 ECDI 定位：**框架负责窗口行为，应用决定 UI**

### R6：DWM 增强（决策点 §3.2）

- **需求层目标（v1.2 修正——评审 §11）**：无边框窗口仍应保留**系统级视觉层次/阴影**；Win11 上叠加系统圆角
- **实现方式不在需求层写死**（`DwmExtendFrameIntoClientArea` 的 margin 值、`DWMWA_WINDOW_CORNER_PREFERENCE` 等属初设细节——不同 Windows 版本视觉效果不保证一致，须实测）
- 失败容忍：DWM API 不可用/失败仅日志，不中断（老系统优雅降级）

### R7：窗口状态 API 与事件（v1.2 定案——评审 §8）

**最小化/最大化/还原是 Window 状态，不是标题栏状态**——`CaptionBar 内部直调 API` 选项**否决**（否则状态双份维护、必失同步）：

```cpp
// Window 层状态 API（应用/未来的 CaptionBar 都调它）
window.Minimize();
window.Maximize();
window.Restore();
// 状态变化 → 事件（新增 WindowEvent 子类；应用可据此切换自绘按钮形态）
// WindowStateChanged（minimized / maximized / restored）
```

- 调用链：`应用按钮 → Window.Maximize() → PlatformWindow → Win32 → 系统状态变化 → WindowStateChanged 事件回流`
- 关闭：复用既有 `WindowCloseRequested`（自绘关闭钮触发同一事件链——应用拦截一致）

### R8：兼容性底线

- IME 候选窗 / DPI——行为与有边框模式一致
- `WM_NCACTIVATE` 防闪烁处理
- 多窗口：chrome 状态 per-Window 独立
- **系统交互手测矩阵（v1.2 补充——评审 §12）**：Alt+F4 / **Alt+Space（系统菜单——已改 NCCALCSIZE/NCACTIVATE/NCHITTEST，须单独验证）** / Win+↑ / Win+↓ / Win+← / Win+→ / 双击标题区 / 拖动标题区 / 边缘八向 resize

### R9：平台消息扩展接缝（v1.1 追加——Phase 13 前置）

**根因**：`PlatformWindowHost` 现仅暴露 `OnPaint` / `OnResized` / `OnExitSizeMove` / `OnEvent` / IME 五类回调（`PlatformWindowHost.h` 实证），**应用层与同级平台能力均够不着窗口消息**。而后续托盘（`Shell_NotifyIcon` 的图标回调消息）与拖入（`WM_DROPFILES`）**必须落在窗口过程内**——无此接缝则 Phase 13 整体无法实现。

- **形态定案（v1.2——评审 §5/§6：候选①否决）**：**不做裸消息注册**（`RegisterRawMessageHandler(msgId, fn)` 会把 Win32 消息号泄漏进 ECDI 公共 API——与 Phase 10「Public API 零平台类型」直接冲突）
- **定案形态 = 平台能力扩展点**（而非「消息扩展接口」）：

```text
Window
 ├── Chrome 能力（本阶段）
 ├── WindowLayer（本阶段 R10）
 ├── Tray 能力（Phase 13——window.SetTrayIcon(...)）
 ├── DropFiles 能力（Phase 13——window.SetDropFilesEnabled(...)）
 └── …（每加能力 = 加一个能力式接口，不进消息号）
```

- **契约约束**：能力接口内部消化 Win32 消息（`Shell_NotifyIcon` 回调消息、`WM_DROPFILES` 落窗口过程）；公共面**不得**出现 HWND / 消息号 / WPARAM/LPARAM
- **行为底线**：未消费的消息必须走 `DefWindowProc`，不得改变既有行为（零回归）
- 本阶段仅确定**接缝形态与骨架**（不实现托盘/拖放本体——Phase 13 落）

### R10：窗口层级能力（v1.1 追加——桌面常驻应用前置）

**根因**：`PlatformWindow` 抽象面仅 `Show()`，**无任何 z 序/层级控制**（读头文件实证）。桌面整理类应用（DesktopNest）要求"被应用窗口覆盖，但 Win+D 后仍可见"，本质是窗口层级问题。

```cpp
enum class WindowLayer { Normal, Bottom, Desktop };
window.SetWindowLayer(WindowLayer::Desktop);
```

| 档位 | 语义 | 实现 | 风险 |
|---|---|---|---|
| `Normal` | 默认，无特殊处理 | — | 无 |
| `Bottom` | 被所有应用窗口覆盖；**Win+D 后一起被藏** | `WM_WINDOWPOSCHANGING` 中**持续**强制 `hwndInsertAfter = HWND_BOTTOM`（非一次性 `SetWindowPos`——其他程序会把我们顶下来） | 低 |
| — | **语义措辞（v1.2 修正——评审 §3）**：`Bottom` = 「框架**持续维持**该窗口处于非置顶窗口中的底部位置」；**不承诺**阻止所有外部窗口管理行为造成的瞬时 z-order 变化（Windows z 序是动态的，受激活/拥有关系/系统策略影响）——避免「绝对永远最底」的过强承诺 | — | — |
| `Desktop` | 被应用窗口覆盖，且 **Win+D 后仍可见**（等同桌面图标行为） | **未验证**。候选：① reparent 到桌面层 `WorkerW`/`Progman`（Wallpaper Engine / Fences 路线）② `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` 监测 `WorkerW` 变前台时临时置顶 | **高——须前置 spike** |

- **触发来源**：DesktopNest 选定 `Desktop` 档。决策依据（Rainmeter 官方文档佐证）：`Bottom` 与 `On Desktop` 是**两个正交档位**——前者 Win+D 会被藏、后者不会，详见 `docs/desktopnest-roadmap.md` §1.1
- **已知副作用**：置底/Desktop 档窗口位于桌面图标层**之上**（z 序：应用窗口 > 本窗口 > 桌面图标层 > 壁纸），会盖住其下方桌面图标（推定，待实测）
- **降级路径**：`Desktop` 档 spike 失败 → 退回 `Bottom` 档，并回写 DesktopNest 决策文档

## 4. 决策点（v1.2 全部拍板——外部评审）

| # | 决策 | 结果 |
|---|---|---|
| 1 | R5 范围 | ✅ **MVP 不做 CaptionBar Widget**——只做 Window Chrome（三件套 API）；控件待二次用例（评审 §7） |
| 2 | R6 DWM | ✅ 进 MVP——需求层只写「保留系统阴影/层次 + Win11 圆角」，实现参数归初设（评审 §11） |
| 3 | API 形态 | ✅ `ChromeMode` 枚举 + 分解配置（`CaptionHeight`/`ResizeInset`） |
| 4 | 窗口状态事件 | ✅ **Window 层状态 API（Minimize/Maximize/Restore）+ `WindowStateChanged` 事件**；否决「CaptionBar 内部直调」（评审 §8） |
| 5 | R9 接缝形态 | ✅ **平台能力扩展点**（能力式接口）；**否决裸消息注册**（Win32 消息号不得进公共 API）（评审 §5/§6） |
| 6 | R10 Desktop 档 | ✅ `Bottom` 无条件进 MVP；**`Desktop` 先 spike**（WorkerW reparent vs WinEventHook）——出结果才纳入，失败退 Bottom 并回写 DesktopNest 决策 |
| 7 | ChromeMode 切换时机（新增） | 🔸 倾向允许运行时切换（含 SWP_FRAMECHANGED/DWM/命中基线处理）；初设若判定成本过高可降级为「仅初始化期」——初设拍板（评审 §14） |

## 5. 非目标（YAGNI 圈定）

- MDI 子窗口 chrome
- Mica/Acrylic 毛玻璃材质（Win11 DWM 材质——另立）
- 自绘窗口动画（最小化/恢复动画跟随系统）
- 跨平台 chrome（Linux 窗管差异大——抽象接口先立，实现仅 Win32）
- 布局系统改造（chrome 模式下客户区即整窗，标题栏由应用/控件自己布局——Layout 已能表达）
- **完整 OLE 拖放**（`IDropTarget` / `IDropSource`）——R9 接缝只为 `WM_DROPFILES` 级能力开口，完整拖放另立阶段
- **R10 `Desktop` 档的实现细节**——本阶段只定档位语义与接口，具体路线归 spike

## 6. 测试/验证方向

- 自动：
  - **Normal vs Chrome 对照断言（v1.2 补充——评审 §13）**：普通窗口 `GetClientRect != GetWindowRect`；Chrome 窗口 `GetClientRect == GetWindowRect`——对照才有意义
  - **NCHITTEST 九宫格覆盖**：Caption / Left / Right / Top / Bottom / 四角 / Client（`SendMessage WM_NCHITTEST` 带坐标 → 返回 HT* 断言）
  - 最大化修正：客户区在 `rcWork` 内（不盖任务栏、无边框残留空白）
- 渲染：RendererTests 窗口像素先例复用（chrome 窗口 BackBuffer 读回）
- 视觉：VisualTest 式人工核窗口（自定义标题栏外观 + 拖动/缩放/双击/Snap 手测清单）
- **R10 `Desktop` 档：前置 spike**（独立小程序验证 reparent vs WinEvent hook 在目标 Win11 版本上的有效性与稳定性）——spike 通过才写验收项
- **R9 零回归**：未注册消息走 `DefWindowProc`，既有测试全绿即证
- 四工具链 + VS

## 7. 影响面

| 区 | 范围 |
|---|---|
| include/ECDI/Platform/ | PlatformWindow 虚接口扩展（chrome API + R10 `SetWindowLayer`）；R9 消息接缝（形态待定，见决策点 5） |
| include/ECDI/Window/ | Window 公共 API 透传 + ChromeMode/事件 + R10 `WindowLayer` |
| src/Platform/Win32/ | Win32PlatformWindow（NCCALCSIZE/NCHITTEST/NCACTIVATE 拦截 + R10 WINDOWPOSCHANGING 持续强制 + R9 消息分发——**均落 `HandleMessage` 状态同步区，不进 `WindowMessageHandler`**，见初设 §3.0 裁决）+ Dwmapi 链接 |
| Widget（若 R5 选 b） | CaptionBar 控件 + 样式 |
| 链接依赖 | 新增 `Dwmapi.lib`；R10 的 reparent 路线另需 `User32`（已在） |
| docs/ | phase12-windowchrome-* 三件套 + 索引；跨引用 `desktopnest-roadmap.md` |

## 8. 修订记录

- v1.2（2026-09-11）**外部评审（14 条）全采纳——可进初设**：① R5 **MVP 不做 CaptionBar Widget**（推翻 v1.1「倾向 b」——防 Chrome 阶段膨胀成 Chrome+Widget+Theme+状态同步；待二次用例再抽象）；② R7 **Window 状态 API + WindowStateChanged 事件**（否决「CaptionBar 内部直调」——状态不属于标题栏）；③ R9 **定案能力式扩展点，否决裸消息注册**（Win32 消息号不得进公共 API——Phase 10 分层律）；④ R10 `Bottom` 语义措辞修正（「持续维持底部」而非「绝对最底」——不承诺阻止外部窗口管理的瞬时变化）；⑤ R3 `captionHeight`/`resizeInset` 明确**逻辑坐标**（禁设备像素）；⑥ R4 最大化验收基准锁定 `MONITORINFO.rcWork`（禁 rcMonitor 简化）；⑦ R6 DWM 需求层去实现参数（归初设）；⑧ R8 系统交互手测矩阵（Alt+Space 单独验证）；⑨ R1 补 ChromeMode 运行时切换决策项；⑩ §6 测试增强（Normal/Chrome 对照 + NCHITTEST 九宫格）；⑪ §4 决策点全部拍板（7 项）。**R10 `Desktop` 档 spike 前置——未出结果前不承诺**。
- v1.2 后续（2026-09-12）**§7 影响面措辞回写**：初设 §3.0 裁决「四个 NC 消息**不进** `WindowMessageHandler`」（非 Event，落 `Win32PlatformWindow::HandleMessage` 状态同步区）⇒ §7 原表述「Win32PlatformWindow + WindowMessageHandler（…拦截）」已修正为该裁决的写法（初设 §3.0 末行原注「应在详设/实现阶段回写修正」由此兑现）。

- v1.0（2026-09-08）需求确认初稿：现状勘察（抽象面零 chrome/消息零拦截/像素测试先例）+ 技术路线（保留 WS_OVERLAPPEDWINDOW + NCCALCSIZE/HITTEST 拦截——非 WS_POPUP）+ R1-R8（chrome API/无边框/命中/最大化修正/CaptionBar 分歧/DWM/事件/兼容底线）+ 4 决策点（范围/DWM/API 形态/事件形态）+ 非目标（MDI/毛玻璃/跨平台）+ 测试方向 + 影响面。待评审。
- v1.1（2026-09-11）DesktopNest 规划回灌：① 追加 §1 勘察两项（窗口 z 序控制缺失、应用层消息不可达——均 grep/读头文件实证）；② 新增 **R9 平台消息扩展接缝**（Phase 13 前置，托盘与拖入的唯一通路）与 **R10 窗口层级能力**（`Normal`/`Bottom`/`Desktop` 三档，`Desktop` 档路线未验证须前置 spike）；③ 决策点 4 → 6（新增接缝形态、Desktop 档是否进 MVP）；④ 非目标补 OLE 完整拖放与 Desktop 档实现细节；⑤ 测试方向补 R10 spike 与 R9 零回归；⑥ 影响面表补 R10 接口与链接依赖。
