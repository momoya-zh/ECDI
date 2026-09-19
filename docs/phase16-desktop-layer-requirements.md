# Phase 16 桌面驻留层（`WindowLayer::Desktop`）需求确认（v1.3）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-19（v1.2 修订 2026-09-19）
> 状态：**✅ 勘察完成 —— D0–D11 的方案方向可拍板，可进初步设计**。**§8 的 9 项勘察均已形成明确处置结论**：其中 **P0–P2 由真机实测闭环**（探针 `.workbuddy/spike/desktop_layer_probe.cpp`，4 形态 × 3 轮 + 用户目视确认），**P3 中涉及实现安全性的第 7 项与跨工具链一致性的第 8 项顺延至初设 / 验证阶段**（本阶段只给方向与约束，不给结论）。**其中 P0 的结论改变了 D1 的选项**——真因是 `WS_MINIMIZEBOX`（而非 `WS_POPUP` 本身），最优解因此从「换 `WS_POPUP`」变成「**只移除一个样式位**」
> 前置：`phase12-windowchrome-detailed-design.md` v1.5（**R10 语义 + `D-DESK-1` 契约**）· `desktopnest-roadmap.md` v1.7（**§5 G-1 登记** · **§7.1 E 路线取证** · §7.2 R-3）· `.workbuddy/spike/desktop_spike.cpp`（**路线 E 实测源码**，`--auto` 可无人值守复现）
> 一句话：把**已取证但未落地**的 `WindowLayer::Desktop` 真正实现进 `Win32PlatformWindow`——让「桌面驻留」从一句契约承诺变成可运行的能力。
> 一句话补充：本阶段**不新增任何公共 API**（`WindowLayer` 枚举与 `SetWindowLayer` 已于 Phase 12 就位）；真正的工作量在**平台实现层**与**窗口样式的一次性取舍**。
> v1.0：初稿（§1 K1–K14 勘察 + 三条会改形态的事实 · §2 技术路线 · §3 R1–R12 四组 · §4 决策点 D0–D10 · §5 非目标 8 项 · §6 测试三层 · §7 影响面 · §8 待勘察 6 项）
> v1.3（2026-09-19）**外部评审处置——3 处文档自洽性修正**（评审结论：「**方向已可进初设**，但有 3 处『已回答』与『仍待初设验证』的表述打架」，全部采纳）：
> ① **状态行 / §8 引言严谨化**——由「9 项**已由真机实测全部回答**」改为「**9 项均已形成明确处置结论**；P0–P2 真机闭环，**P3 中实现安全性（§8-7）与跨工具链一致性（§8-8）顺延初设/验证**」。⚠️ 原表述与 §8 表内第 7/8 项的「⏭ 顺延初设」**直接矛盾**，属实质严谨性缺陷。
> ② **§2「维持住」修正**——由「前台事件钩子 + **位置校验（兜底）** + 桌面句柄**重建自适应**」改为「**前台事件钩子 + 重插前位置判定 + 桌面窗口句柄即时重查**」。⚠️ 原表述与 D4「**不加 Timer**」及 D6「**不缓存**」矛盾（那是 v1.0 写作时的心跳兜底设想，v1.2 实测已否）。
> ③ **R9 修正**——由「每个窗口独立持有自己的钩子 / **句柄缓存**」改为「每窗口独立持有**维护钩子与维护状态**；**桌面窗口句柄不做长期缓存**」。⚠️ 与 D6 取 C（不缓存）矛盾。
> ④ **新增 §8.2「留给初步设计的问题清单」**——把散射在各处的初设输入汇总为 8 项表，并写明**边界纪律**（这些是**设计问题**，不是需求问题）。
> v1.2（2026-09-19）**事实勘察完成回写——P0 的结论改变了 D1 的选项**。9 项勘察全部有答案（4 形态 × 3 轮实测 + 用户目视确认，原始判据见 §8 结果列）：
> **★ 核心发现**：**「显示桌面」只最小化「可最小化窗口」** ⇒ 真因是 **`WS_MINIMIZEBOX`**，**不是 `WS_POPUP` 本身**。⇒ **D1 新增选项 A′（`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`）并改为首选倾向**——它保留 Alt+Space / Aero Snap / 最小化动画之外的**全部** Phase 12 红利，只放弃「最小化」，代价远小于换 `WS_POPUP`（C）。
> 其余**已可用实测判据钉死**：**D2**（不加 `WS_EX_NOACTIVATE`——`ex=0` 下点击/焦点/激活全部正常）· **D4**（只装前台钩子，**不加 Timer**——tick 把重插次数从 4 抬到 13 却零收益）· **D3**（取 B：配置期内 `SetWindowLongPtrW` 改样式位）· **D7**（`GetShellWindow()` ≡ `FindWindowW("Progman")`）· **D6**（不缓存成立，0.8 次/秒）。
> 另登记两条**新约束**：① **像素采样在非主显示器不可用**（G-4 的直接表现，见 §6）② **「重插前先判在位」**（fg-hook 无条件重插是 Win+D 瞬间抖动的来源，见 §7）。
> v1.1（2026-09-19）外部评审「**方向与边界认可，但不拍板——缺事实勘察**」——**8 项处置全部采纳**（无否决）：① **状态行**由「待评审」改为「**待事实勘察**」（评审结论）② §1.1 补**设计方针**（「先证明现有窗口形态能不能承载 Desktop 层，证明不了才改变窗口形态」+ 「spike 的实现细节 ≠ 框架的设计输入」纪律）③ **D1 顺序修正 + 补强**——v1.0 写作「倾向 C + 验证 A」，评审指出**顺序反了**；改为「**A 为首选假设 → 验证不通过才取 C**」，并补评审原创论点「**「系统标题栏存在」≠「用户看得到系统标题栏」**——Borderless 若已处理非客户区，则须实测而非凭样式定义判断」④ **D2 补强**（`WS_EX_TOOLWINDOW` 与「能否交互」**不是一回事**——它影响任务栏 / Alt+Tab / 窗口类别；`NOACTIVATE` 明确排除，理由 = 加上它反而要补「怎么还能交互」）⑤ **D5 补强**（显式记入 B 的风险方向：钩子生命周期**必须绑定 `Win32PlatformWindow`**，不能绑 `Application`、不能全局 singleton）⑥ **R10 由一句模糊表述细化为「钩子生命周期四态边界表」**（Set ⇒ 装 · Hide ⇒ **不卸载** · Show ⇒ **不重复装** · Release ⇒ **必卸**）⑦ **新增 D11**（钩子生命周期边界——评审提出的**最重要补项**，原话「**应该作为初设里的明确决策，而不是实现时临时决定**」）⑧ **§8 重构**为带 **P0–P3 优先级**的 9 项（原 6 项 + D6 成本核实 · D3 安全窗口 · 定时器可用性），勘察顺序由评审给定

---

## 1. 背景与现状勘察

### 1.1 立项由来

`desktopnest-roadmap.md` v1.7 §5 把 G-1 登记为**唯一阻断项**：

> **G-1** ★ **`WindowLayer::Desktop` 尚未实现——当前降级执行 `Bottom`**〔`Win32PlatformWindow.cpp:863-868`（`if (layer == Desktop)` 只记 Warning）→ L882 走 `SetWindowPos(…, HWND_BOTTOM, …)`；**该 Warning 文案仍写 "spike pending"**——而 spike 已于 2026-09-15 六条判据全过 ⇒ **文案过期 + 能力未交付**〕**影响 M1 / M4**（硬前置）

⇒ 本阶段的定性是「**兑现既有承诺**」，而非「探索新能力」：

| 维度 | 状态 |
|---|---|
| 语义契约 | ✅ **已定稿**（Phase 12 `WindowLayer.h` 头注释 + `D-DESK-1`） |
| 公共 API | ✅ **已就位**（`WindowLayer::Desktop` 枚举值 + `Window::SetWindowLayer` + `PlatformWindow::SetWindowLayer`） |
| 实现路线 | ✅ **已取证**（2026-09-15 spike，路线 E 六条判据 ①–⑥ 全过） |
| 平台实现 | ❌ **缺失**（降级执行 `Bottom`，仅记一条过期的 Warning） |

**⇒ 这是全项目唯一「已取证但未落地」的能力**。用户取舍原则（2026-09-19 明确）：「**已取证但未落地**」优先于任何新功能——拖延会让 spike 结论随 Windows 版本失效（spike 环境 = Win11 build 26220 / 25H2 系列）。

**⇒ 本阶段的设计方针（v1.1 补——外部评审归纳）**：**先证明「现有窗口形态能不能承载 Desktop 层」，证明不了才改变窗口形态**。这与项目一贯的「最小改动 / 平台实现内部解决 / 不为一个需求污染公共 API」路线一致。⚠️ 由此得出一条纪律：**spike 的实现细节 ≠ 框架的设计输入**——spike 验证的是「**路线**」（紧贴桌面窗口正上方 + 前台钩子），**不是「参数」**（`WS_POPUP` / `WS_EX_TOOLWINDOW` / `WS_EX_NOACTIVATE` 三件套）。这三件套每一项都必须在框架语境下**重新问一遍「它是必要的，还是探针的便利」**——见 §1.3 F-1。

### 1.2 现状勘察（全部带行号，2026-09-19 复核）

| # | 事实 | 出处 | 直接后果 |
|---|---|---|---|
| **K1** | **`Desktop` 档降级执行 `Bottom`**：`if (layer == WindowLayer::Desktop)` 只记一条 Warning，随后走通用分支 `SetWindowPos(m_hwnd, HWND_BOTTOM, …)` | `Win32PlatformWindow.cpp:863-870`（Warning）· `:880-885`（通用 `HWND_BOTTOM` 分支） | 核心卖点（Win+D 后仍可见）**不成立** |
| **K2** | **Warning 文案已过期**：文案仍写 `"not yet validated (spike pending)"` | `Win32PlatformWindow.cpp:868` | 与事实矛盾（spike 已结项）⇒ 必须随实现一并修正 |
| **K3** | **`WM_WINDOWPOSCHANGING` 是全档位共用的维护点**：`if (m_windowLayer != WindowLayer::Normal) wp->hwndInsertAfter = HWND_BOTTOM;` | `Win32PlatformWindow.cpp:405-424`（`:410` 判据 · `:416` 赋值） | **`Desktop` 与 `Bottom` 当前走同一分支**——两者目标 z 序位置**不同**（`Bottom` = `HWND_BOTTOM`；`Desktop` = **紧贴桌面窗口正上方**）⇒ 必须在此处分流 |
| **K4** | **窗口样式为 `WS_OVERLAPPEDWINDOW`，零扩展样式**：`CreateWindowExW(0, …, WS_OVERLAPPEDWINDOW, …)` | `Win32PlatformWindow.cpp:63-77`（`:64` 第一参 = 0 扩展样式 · `:67` 主样式） | ⚠️ **本阶段头号议题**——spike 的 E 路线用 `WS_POPUP \| WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE`（`desktop_spike.cpp:1053-1054`），与框架现状**三处不同**。详见 §1.3 |
| **K5** | **`SetWindowLayer` 是配置期 API**：`if (m_shown)` ⇒ Warning + 忽略 | `Win32PlatformWindow.cpp:846-853` | 样式取舍**必须在 `Show()` 之前完成** ⇒ 与「`CreateWindowExW` 在构造期一次性定样式」（`:63-77`）形成**时序紧张**——见 §1.3 F-2 |
| **K6** | **`m_windowLayer` 语义状态不随实现路径降级**（`D-DESK-1`） | `Win32PlatformWindow.h:116` 注释 · `Win32PlatformWindow.cpp:861` | 实现落地后本不变量**继续成立**（无需改动）；但 Phase 12 预留的「未来 `GetWindowLayer()` 返回 `Desktop`」约定**仍无消费者** |
| **K7** | **spike 的维护是「双层」的**：前台事件钩子（`EVENT_SYSTEM_FOREGROUND` 命中 `Progman`/`WorkerW`）+ 轮询位置校验 | `desktop_spike.cpp:605-616`（钩子）· `:1178-1187`（tick 校验，节流 250ms）· `:583-588`（`IsDirectlyAboveDesktop` 判据） | 框架侧**无心跳**（`Run()` 是纯 `GetMessageW` 阻塞循环，`Win32PlatformApplication.cpp:28-46`）⇒ 轮询兜底需要机制——见 §1.3 F-3 |
| **K8** | **桌面窗口句柄会失效**：`Progman` 被 explorer **销毁重建**（换壁纸 / 重启资源管理器） | `desktop_spike.cpp:555-580`（`RefreshDesktopHwnd` 全套防御 + 状态变化才打印） | **这是本阶段最容易写错的分支**：句柄失效时 `GetWindow(旧句柄, GW_HWNDPREV)` 返回 `NULL` ⇒ `SetWindowPos(hwnd, NULL, …)` 落到 `HWND_BOTTOM`——**比不重插更糟**（`desktop_spike.cpp:592-594` 已显式防御） |
| **K9** | **`SetWinEventHook` 需 `hInstance`**：spike 传 `nullptr`（`WINEVENT_OUTOFCONTEXT` 下合法） | `desktop_spike.cpp:626-628`（E 路线）· `:1042`（诊断钩子） | 框架侧有 `WindowClass::GetInstance()`（`Win32WindowClass.h:46`）可用——用 `nullptr` 亦可，但须核实四工具链一致性 |
| **K10** | **`WM_WINDOWPOSCHANGING` 只改 `hwndInsertAfter`**，不碰 `x/y/cx/cy/flags` | `Win32PlatformWindow.cpp:408-418` | ✅ 既有做法正确——**实现必须沿用**（否则会干扰最大化 / 还原几何） |
| **K11** | **框架无「窗口销毁后自动脱钩钩子」机制** | `Win32PlatformWindow::Release()`（`:187-197`）只 `DestroyWindow`；`~Win32PlatformWindow`（`:92-96`）调 `Release()` | 钩子是**每窗口**资源 ⇒ 必须在 `Release()` / 析构路径**显式 `UnhookWinEvent`**（否则句柄泄漏 + 回调打到已销毁对象） |
| **K12** | **测试设施无窗口级 z 序观测**：`RecordingBackend` 只记渲染命令 | `src/Tests/` 现有 24 文件（21 含用例） | 桌面档的**自动测试上限很低**（z 序是真机行为）——测试策略见 §6 |
| **K13** | **`Bottom` 档已有跨工具链验证的 z 序用例**（Phase 12） | `WindowChromeTests.cpp`（R10 相关用例）· `phase12-windowchrome-detailed-design.md` §5.3 T 系列 | 本阶段**不得回归** `Bottom` 语义（零回归底线） |
| **K14** | **`WS_EX_NOACTIVATE` 从未在框架内使用** | 全库 grep：`WS_EX_` 仅出现在 `Win32PlatformWindow.cpp` 的 DWM 相关处，无 `WS_EX_NOACTIVATE` / `WS_EX_TOOLWINDOW` | 若桌面档需要这两个样式，属**首次引入**——须评估对现有窗口行为（激活链 / 焦点 / Alt+Tab）的影响面 |

### 1.3 三条「会改形态」的事实

**F-1（头号）：窗口样式三处差异——桌面档不能直接复用现有窗口**

spike 的 E 路线窗口与框架窗口（`K4` / K14）逐项对照：

| 项 | spike（E 路线） | 框架现状（`CreateWindowExW(0, …, WS_OVERLAPPEDWINDOW, …)`） | 差异性质 |
|---|---|---|---|
| 主样式 | `WS_POPUP` | `WS_OVERLAPPEDWINDOW` | ⚠️ **结构性**——`WS_OVERLAPPEDWINDOW` 含 `WS_CAPTION` / `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MINIMIZEBOX` / `WS_MAXIMIZEBOX` |
| 扩展样式 | `WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE` | `0` | ⚠️ `WS_EX_NOACTIVATE` 缺失 ⇒ 点击桌面档窗口**会抢焦点**（`WS_EX_TOOLWINDOW` 还影响 Alt+Tab 与任务栏） |
| 创建时机 vs 样式决定 | spike 进程启动即定（`desktop_spike.cpp:1052-1054`） | **构造期 `CreateWindowExW` 一次性定**（`K4`），而 `SetWindowLayer` 在**构造之后**才可能被调用（`K5`） | ⚠️ **时序紧张**——样式在构造时已固化，层级却在此后才设定 |

**⇒ 这是本阶段最重要的结构判断**：Phase 12 曾以「**保留 `WS_OVERLAPPEDWINDOW`**」为红线（换取 Alt+Space / Aero Snap / 最小化动画等系统红利，见 `phase12-windowchrome-requirements.md` §1 与详设 §5.4 手测矩阵）。若桌面档必须换 `WS_POPUP`，则该红利在桌面档上**明确放弃**——这是一个需要用户拍板的**范围取舍**，不是实现细节。

⚠️ **同时须澄清一个常识性误判**：`WS_EX_NOACTIVATE` 的取舍**不等于**「桌面档不可交互」。spike 用它只是因为探针程序不需要交互；而 DesktopNest 的框**需要**被点击（成员列表 / 收缩展开按钮）⇒ `WS_EX_NOACTIVATE` **不应盲目照搬**。这条判定直接影响 F-1 的结论——见 D2。

> **★ v1.2 实测回写（2026-09-19，真机 3 轮 × 4 形态）——F-1 的焦点从「主样式」精确到「一个样式位」**
>
> 上表的「主样式」一行在实测后被**收窄**：决定「Win+D 后是否可见」的**不是 `WS_POPUP` vs `WS_OVERLAPPEDWINDOW` 这个整体选择**，而是二者差异中的**单个位 `WS_MINIMIZEBOX`**。
>
> | 形态（`--style`） | Win+D 生效时 | 判定 |
> |---|---|---|
> | `overlapped`（= 框架现状 `WS_OVERLAPPEDWINDOW`） | `rect=(-32000,-32000)` + `iconic=1` ⇒ **被最小化** | ❌ **FAIL**（两次独立运行各 3/3） |
> | **`overlapped-nominbox`**（`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`） | `rect` 不变、`iconic=0` ⇒ 原位可见 | ✅ **PASS**（3/3 + **用户目视确认**） |
> | `popup`（spike 形态） | `rect` 不变、`iconic=0` ⇒ 原位可见 | ✅ PASS（3/3 + **用户目视确认**） |
>
> **机制结论**：**「显示桌面」只最小化「可最小化窗口」**。⇒ 桌面档只需要**移除 `WS_MINIMIZEBOX` 一位**，`WS_CAPTION` / `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MAXIMIZEBOX` 全部可以保留。
>
> ⇒ **对 F-1 的影响**：上表「⚠️ 结构性」的措辞**过强**——差异是**一位可修的**，不是结构性的。由此**新增 D1 的选项 A′ 并改为首选**：它保住 Alt+Space / Aero Snap 等**除「最小化」之外的全部** Phase 12 红利，代价远小于换 `WS_POPUP`（C）。**「若必须换 `WS_POPUP` 则明确放弃系统红利」这一范围取舍，在 A′ 下不再需要**。
>
> ⚠️ **同表「扩展样式」一行仍未完全出清**：实测的两组 PASS 都是 `ex=0`（无 `WS_EX_TOOLWINDOW`），故 `WS_EX_TOOLWINDOW`（任务栏 / Alt+Tab 行为）**尚未实测**——留作初设决策项（见 D2）。

**F-2：样式决定时机与配置期契约的错位**

`SetWindowLayer` 是配置期 API（`K5`：`Show()` 之后 Warning + 忽略），而 `CreateWindowExW` 在**构造期**（`K4`）。若桌面档需改主样式，则存在两条路径（详见 D3）：

- **路径 A**：构造期不改样式，`SetWindowLayer(Desktop)` 时用 `SetWindowLongPtrW(GWL_STYLE / GWL_EXSTYLE)` 改 + `SetWindowPos(…, SWP_FRAMECHANGED)` 刷新 —— 但**未 `Show()` 的窗口改样式是安全的**（配置期语义恰好允许）
- **路径 B**：构造期就按「可能要桌面档」预置 —— 与 `WindowLayer` 的「配置期设定」语义冲突（构造时还不知道用户选哪个档）

**F-3：维护机制缺「心跳」**

spike 的双层维护（`K7`）中，前台钩子解决了「桌面层被抬升」的主要场景（实测 12 秒内自动重插 3 次），轮询只是**兜底**。但框架的 `Run()` 是纯阻塞 `GetMessageW`（`Win32PlatformApplication.cpp:28-46`）——**没有 tick**。

⇒ 三个候选（详见 D4）：
1. **只装前台钩子**（spike 的主要机制，实测已足够）——零新机制，但钩子漏触发时无兜底
2. **窗口级 `SetTimer`**（框架已有：`PlatformWindow::StartTimer`，`WindowMessageHandler.cpp:243` 翻译 `WM_TIMER` → `TimerEvent`）——但 `TimerEvent` 是**给 Widget 的事件**，平台层自用会污染事件语义
3. **平台层自建 `SetTimer` 且不走翻译器**（在 `Win32PlatformWindow::HandleMessage` 内消化，符合 R9 三步惯例第 ③ 步）——机制干净，但新增一个平台内部定时器

---

## 2. 技术路线（一段话定方向）

**分两半，边界清晰：**

- **「贴上去」= 平台实现**：`WM_WINDOWPOSCHANGING` 里把 `hwndInsertAfter` 从 `HWND_BOTTOM` 改成 `GetWindow(progman, GW_HWNDPREV)`（紧贴桌面窗口正上方）⇒ **公共 API 零变更**，兑现 `WindowLayer.h` 的「平台如何达成该层级是实现的自由」。这正是 spike 对框架意义的结论（`desktopnest-roadmap.md` §7.1 末段）。
- **「维持住」= 三段机制（v1.3 修正——与 D4 / D6 对齐）**：**前台事件钩子**（主，`EVENT_SYSTEM_FOREGROUND` 命中 `Progman`/`WorkerW` 时重新定位）+ **重插前位置判定**（`IsDirectlyAboveDesktop()`：**已在位则什么都不做**——这是 v1.2 实测得出的**正式设计输入**，不是可选优化：无条件重插正是 Win+D 瞬间抖动的来源，见 §8.1②）+ **桌面窗口句柄即时重查**（不缓存 ⇒ 天然免疫 `K8` 的「explorer 重建 `Progman` 导致旧句柄失效」，无需 `RefreshDesktopHwnd` 那套状态检测）。

**四个硬骨头**（对应 D1–D4；**v1.3：四项均已闭环**——前三项由实测/决策定案，第四项由实测否证）：

1. **窗口样式**（F-1）——✅ **已答（v1.2 实测）**：可沿用，只需移除 `WS_MINIMIZEBOX` ⇒ **D1 取 A′**。
2. **`WS_EX_NOACTIVATE` 的取舍**（F-1 澄清）——✅ **已答（v1.2 实测）**：`ex=0` 下交互完整，**不加** ⇒ **D2 取 B**；`WS_EX_TOOLWINDOW` 留初设。
3. **样式改动时机**（F-2）——✅ **已答（v1.3 决策）**：**配置期内 `SetWindowLongPtrW` 改样式位** ⇒ **D3 取 B**（改动时机本身不是问题——配置期语义已消解时序紧张；**改样式的安全性细节**顺延初设）。
4. **维护机制的兜底**（F-3）——钩子单独够不够（**v1.2 实测已答：够，不加心跳**）。

---

## 3. 需求条目

### 3.1 实现主体 · R1–R4

**R1 · `Desktop` 档真实生效**：`Window::SetWindowLayer(WindowLayer::Desktop)` 后，窗口的 z 序位置为「**紧贴桌面窗口正上方**」——即同时满足契约两侧：**被应用窗口覆盖**（应用窗口在它之上）与 **Win+D 后仍可见**（桌面层被抬升时它跟随）。**验收判据以 §1.1 的 ①–⑥ 为准**（spike 已定的六条人工判据）。

**R2 · `Bottom` 语义零回归**：`WindowLayer::Bottom` 与 `Normal` 的行为**逐位不变**（`K3` 的分支改造不得影响这两档）。⚠️ 现有的 `WM_WINDOWPOSCHANGING` 通用分支（`:410` 判据 `!= Normal`）**必须按档位分流**，而不是把 `Desktop` 塞进同一分支。

**R3 · 与 `Window` 解耦的维护生命周期**（对应 K8 / K11）：
- 桌面窗口句柄（`Progman`）失效 / 重建时**必须自适应**——刷新句柄后重插，**不得**因旧句柄失效而落到 `HWND_BOTTOM`；
- 窗口销毁（`Release()` / 析构）时**必须脱钩**全部平台钩子（无句柄泄漏、无回调打到已销毁对象）。

**R4 · 文案与注释同步**：`K2` 的过期文案（`"not yet validated (spike pending)"`）必须随实现一并修正——**不得留下与事实矛盾的日志**。`WindowLayer.h` 头注释中「spike 未通过前此档位不承诺可用」的措辞同步复核（`WindowLayer.h:25-26`）。

### 3.2 交互与样式 · R5–R7

**R5 · 桌面档的窗口样式**：桌面档的**最小必需样式集**——见 **D1** / **D2**。核心问题是「`WS_OVERLAPPEDWINDOW` 能否保留」，以及「扩展样式是否需要调整」。

**R6 · 交互能力**：桌面档窗口**必须保持框架既有的全部交互能力**——鼠标事件 / 键盘事件 / 焦点 / Widget 树命中**不得**因层级档位而降级。⚠️ 这是对 F-1 澄清的直接需求化：`WS_EX_NOACTIVATE` **不得**被理解为「桌面档的默认值」。

**R7 · 视觉无异常**：桌面档窗口**不得**出现「挖洞」/ 残影 / 重绘异常（spike 判据 ⑦）。特别是与 Phase 12 的 DWM 增强（阴影 / Win11 圆角，`ApplyDwmEnhancements`）的兼容性——样式变更后**须复验**。

### 3.3 覆盖范围 · R8–R10

**R8 · 与 `ChromeMode` 的正交性**：`WindowLayer` 与 `ChromeMode` 是**两个正交维度**（Phase 12 初设已定：`Normal` / `Borderless` × `Normal` / `Bottom` / `Desktop`）。需求上：**`Desktop` 档在两种 `ChromeMode` 下都应可用**——除非实现层有硬冲突（理论上 `Borderless` + `Desktop` 是最自然的组合）。⇒ 若确认存在冲突，须在初设显式记账并降级（而非静默）。

**R9 · 多窗口支持**：多个窗口可各自独立设置层级档位；一个窗口切档**不得**影响其它窗口的 z 序维护。**归属（v1.3 修正，与 D5 / D6 对齐）**：每个窗口独立持有自己的**维护钩子与维护状态**（不共享钩子）；**桌面窗口句柄不做长期缓存**（每窗口按需即时重查 `FindWindowW`）——`K11` 的生命周期归属。

**R10 · 钩子生命周期四态边界（v1.1 细化——外部评审补项）**：维护资源的安装 / 保留 / 卸载必须按状态明确，**不得留到实现期临时决定**（评审原话：应作为初设的明确决策）：

| 触发 | 钩子动作 | 依据 |
|---|---|---|
| `SetWindowLayer(Desktop)` | **安装** | 档位进入 Desktop |
| `SetWindowLayer(Normal / Bottom)` | **卸载** | 离开 Desktop ⇒ 无维护对象（配置期契约 `K5`：仅 `Show()` 前可调，故该转移只在未 `Show()` 时发生） |
| **`Hide()`** | **不卸载**（档位配置未变，仅不可见） | 与 `Hide()` 的既定语义务一致——`PlatformWindow.h:124-127`：「资源存活仅不可见（可再 `Show`）」 |
| **`Show()`**（Hide 之后） | **不重复安装**（幂等） | 避免重复钩子 ⇒ 重复重插 |
| **`Release()` / 析构** | **必须卸载** | `K11`：框架无自动脱钩机制——钩子回调打到已销毁 `this` 是 UB |

⇒ 倾向「**Hide 保留、Release 清理**」：生命周期最简（不需要「恢复」逻辑，「恢复」由钩子持续存在天然保证）。作为 **D11** 归初设定稿，但倾向已明确。

### 3.4 测试与验收 · R11–R12

**R11 · 自动测试**：可自动断言的部分**必须有用例**（`K12`：能力有限，需精确划定）；不可自动断言的部分**必须**给出**可复现的手测步骤**（spike 的 `--auto` 模式已证明「Win+D 可无人值守复现」）。⇒ 分层见 §6。

**R12 · 跨工具链**：四工具链（MSVC / Clang / ClangCL / MinGW）构建通过；**`FRAMEWORK_ASSERT` 生效的构建**下零断言（沿用项目验证纪律）。

---

## 4. 决策点（D0–D11——全部给倾向待拍板）

| # | 决策 | 选项 | **倾向** | 理由 |
|---|---|---|---|---|
| **D0** | 范围与形态 | **A** 独立 Phase（走五阶段 + 设计文档可评审）/ **B** 并入 DesktopNest M1 一并做 | **A** | 用户 2026-09-19 立项即选独立 Phase（roadmap §9 第 9 项的两选项之一是 B，本次明确取 A）。理由：这是**唯一「已取证但未落地」**的能力，留下**可评审的契约**对简历叙事与后续维护都有价值；并入 M1 会让「框架能力」与「应用逻辑」混在一份文档里，平台细节无处安放 |
| **D1** ★ | 窗口主样式（**本阶段核心拍板**） | **A** 保留 `WS_OVERLAPPEDWINDOW`（现状）/ **A′** `WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX` / **B** 全改 `WS_POPUP` / **C** 分档位：`Normal`/`Bottom` 保持现状，`Desktop` 用 `WS_POPUP` | **A′（实测 PASS，且代价最小）** | **★ 实测判决（v1.2——4 形态 × 3 轮真机）**：**A ❌ FAIL**（`rect=(-32000,-32000)` + `iconic=1`，被「显示桌面」最小化；两次独立运行各 3/3）· **A′ ✅ PASS**（`rect` 不变 + `iconic=0`，3/3 + **用户目视确认**）· **C ✅ PASS**（3/3 + **用户目视确认**）· B 无支持理由（与 C 同类但更粗）。**★ 真因 = `WS_MINIMIZEBOX`**——「显示桌面」**只最小化「可最小化窗口」**，故桌面档只需**移除这一位**，`WS_CAPTION` / `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MAXIMIZEBOX` 全部可保留。**为何 A′ 优于 C**：C 会连带失去 `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MAXIMIZEBOX` ⇒ Alt+Space 系统菜单 / Aero Snap / 最小化动画等 Phase 12 红利整批失效；**A′ 只失去「最小化」**——对桌面常驻物而言，不可最小化**本就是合理语义**（它不占任务栏、不该被收起来）。⚠️ **A′ 的语义后果须记账**（归初设）：任务栏右键「最小化」、`Win+↓` 等最小化入口对该窗口失效；`ChromeMode::Normal` 下系统标题栏的最小化按钮呈灰态。⚠️ 另注：**v1.1 评审确立的「先证明现状能承载、证明不了才改形态」顺序是对的**——A 被证否后，正确的下一步是**继续问「最小改动是什么」**（得到 A′），而不是一步跳到最粗的 C |
| **D2** ★ | `WS_EX_NOACTIVATE` 与 `WS_EX_TOOLWINDOW` | **A** 加 `WS_EX_NOACTIVATE`（照搬 spike）/ **B** **不加**（保持可激活）/ **C** 加 `WS_EX_TOOLWINDOW` 但不加 `WS_EX_NOACTIVATE` / **D** 两者都加 | **B（实测钉死）**；`WS_EX_TOOLWINDOW` 部分归初设 | ⚠️ **spike 用 `WS_EX_NOACTIVATE` 只是因为探针不需要交互**（`desktop_spike.cpp:17` 明写「窗口用 `WS_EX_NOACTIVATE`，不抢焦点，故热键为全局轮询」）——这是**探针的便利**，不是 E 路线的**必要条件**。**★ 实测判决（v1.2）**：`ex=0`（无任何扩展样式）下，`--caption 0` 的人工点击测出 `clicks=15 / focusEvents=2 / activateEvents=2`，坐标覆盖整个客户区；popup 形态启动即 `WM_ACTIVATE ACTIVE` + `WM_SETFOCUS` ⇒ **不加 `WS_EX_NOACTIVATE` 也能完整交互** ⇒ **A 排除、取 B**（评审判词：「spike 为了方便测试做的东西，不应该反向成为框架 API 的约束」）。**仍开口的一项**：`WS_EX_TOOLWINDOW`（影响任务栏 / Alt+Tab / 窗口类别，**与能否交互无关**）在两次 PASS 组中均为 `ex=0`，**尚未实测** ⇒ 留作初设决策（倾向：**加**——桌面常驻物不应占任务栏位；须在初设补验它不影响 Win+D 行为） |
| **D3** | 样式改动时机 | **A** 构造期按 `WindowLayer` 预置（需把层级提升为构造参数）/ **B** `SetWindowLayer(Desktop)` 时用 `SetWindowLongPtrW` 改样式位 / **C** 不改样式 | **B**（D1 取 A′ 后必须改样式位） | D1 取 A′ ⇒ **必须改样式**，故 C 不再成立。`K5`：`SetWindowLayer` 是**配置期 API** ⇒ 改动发生在 `Show()` **之前**，系统尚未显示窗口，**改样式是安全的**（这正是 F-2 的「时序紧张」被配置期语义消解之处）。A 需要把 `WindowLayer` 提升为构造参数 ⇒ **破坏 `SetWindowLayer` 的 API 形态**，且与 Phase 12 定稿的「配置期四件套统一生命周期」不一致。**初设须回答**：① 改样式位后是否需 `SWP_FRAMECHANGED`（`WS_MINIMIZEBOX` 不改变边框量，预计不需要，但须实测）② **幂等与可逆**——`SetWindowLayer(Normal)` 是否恢复 `WS_MINIMIZEBOX`（配置期内可逆；YAGNI 评估归初设） |
| **D4** | 维护机制兜底（F-3） | **A** 只装前台钩子 / **B** 钩子 + 窗口级 `SetTimer` 且走 `TimerEvent` 翻译器 / **C** 钩子 + 平台内部 `SetTimer`（不进翻译器） | **A（实测钉死：不加 Timer）** | **★ 实测判决（v1.2）**：`--tick` 与 `--no-tick` 对照——**tick=0 两组（nominbox / popup）均 PASS**（证明**前台钩子单独已足够**）；而 tick=1 组实测 `reinsert=13`（vs tick=0 的 **4**）、`FindWindowW` **78 次**（vs **9**）⇒ **tick 把重插次数抬到 3 倍多却零收益**，且**额外的 `SetWindowPos` 正是 Win+D 瞬间「闪烁一下」的来源之一**（用户目视）。⇒ **不加心跳**。⚠️ **B 明确不可取**（v1.1 已论证）：`TimerEvent` 是「派发给焦点 Widget」的框架事件（`Application::OnTimer` → `FindFocusedWidget`），平台层自用会**污染事件语义**（违反 Event 原则）。**配套优化（v1.2 新增，归 §7）**：前台钩子回调里**先判 `IsDirectlyAboveDesktop()`，在位则不动**——消除余下的不必要重插 |
| **D5** | 钩子安装形式 | **A** 每窗口一个 `SetWinEventHook` / **B** 应用级共享一个钩子 + 窗口注册表 | **A** | `K9`：`WINEVENT_OUTOFCONTEXT` 下 `hInstance` 可传 `nullptr`。每窗口一个钩子实现最简、生命周期归属清晰（`K11`：`Release()` 时脱钩本窗口的）。**B 的风险方向（v1.1 补——外部评审）**：钩子生命周期**必须绑定 `Win32PlatformWindow`**——不能绑 `Application`，也不能搞全局 singleton；B 恰恰把归属推向应用级，会让「窗口已 `Destroy` → 钩子仍在 → 回调继续打到 `this`」成为可能。**多窗口场景下 A 的钩子数 = 窗口数**，可接受（YAGNI：无大规模多窗口消费者） |
| **D6** | 桌面窗口句柄缓存 | **A** 每窗口缓存 `g_desktopHwnd`（spike 做法）/ **B** 进程级共享缓存 / **C** 不缓存（每次重查 `FindWindowW(L"Progman", nullptr)`） | **C（最简）** | `FindWindowW` 是廉价的（`desktop_spike.cpp:1180-1183` 做 250ms 节流只是为避免日志刷屏）。**不缓存 ⇒ 天然免疫 `K8` 的失效问题**（无需 `RefreshDesktopHwnd` 那套状态检测）。⚠️ 但须初设确认调用频率（若每个 `WM_WINDOWPOSCHANGING` 都调，需评估开销） |
| **D7** | 桌面窗口定位方式 | **A** `FindWindowW(L"Progman", nullptr)`（按类名）/ **B** `GetShellWindow()` / **C** 枚举顶层窗口按类名匹配 | **A** | spike 全程用 A 且实测通过（`desktop_spike.cpp:560` / `:619` / `:642`）。B 是官方 API 且更「正规」（MSDN：返回 shell 的桌面窗口），**但未在 spike 环境实测**——须初设核实两者是否恒等（`GetShellWindow()` 的返回值定义与 `Progman` 的对应关系）。⇒ 倾向 **A（已实测）+ 初设补验 B** |
| **D8** | `Bottom` 与 `Desktop` 的代码组织 | **A** `WM_WINDOWPOSCHANGING` 内 `switch` 分流 / **B** 抽两个私有辅助方法各自实现 / **C** 抽象成「目标 z 序位置」计算函数 | **C** | `K3` 的分支改造是核心 diff。C 让判据集中（`HWND TargetInsertAfter() const`）——`Normal` 返回「不改」（`SWP_NOZORDER`）、`Bottom` 返回 `HWND_BOTTOM`、`Desktop` 返回 `GetWindow(progman, GW_HWNDPREV)`。⚠️ 但**桌面句柄无效时的语义**须在 C 里显式处理（`K8`：返回 `NULL` 会落到 `HWND_BOTTOM` ⇒ 应「跳过本次修改」而非返回 `NULL`） |
| **D9** | 自动测试边界 | **A** 只测「不回归」（`Bottom` / `Normal` 语义 + 脱钩配对）/ **B** 在 A 基础上加「z 序目标计算函数」的纯逻辑测试 / **C** 尝试测真实 z 序 | **B** | `K12`：真实 z 序依赖桌面环境（`Progman` 存在、explorer 行为）⇒ **C 在 CI / 自动化中不可靠**。B 通过 D8 的 C 抽象把「目标值计算」变成**可注入纯函数**——句柄有效性 / `Normal` 分支 / `Bottom` 分支都可断言；`Desktop` 的**真实生效**归手测（`R11`：spike 的 `--auto` 已证明可无人值守复现） |
| **D10** | 手测判据复用 | **A** 直接复用 spike 的六条判据（`--auto` + 人工观察）/ **B** 重写一套判据 / **C** 用 ModelProbe 的 `--layer desktop` 通道手测 + 六条判据对照 | **C** | ModelProbe **已有** `--layer desktop` 参数通道（`examples/ModelProbe/main.cpp:247`，当前注释写「spike 未通过 → 降级 Bottom + Warning」——**该注释同样过期，随 R4 一并修正**）。用真消费者手测比独立探针更接近真实场景；六条判据（`--auto` 可复现 ①⑥）作为对照基线 |
| **D11** | 钩子生命周期边界（**v1.1 新增**——外部评审提出的最重要补项） | **A** `Hide` 保留钩子、`Release` 清理（= `R10` 的倾向）/ **B** `Hide` 即卸载、`Show` 重装 / **C** 不区分（只在 `Release` 清理——与 A 实为同义） | **A** | `R10` 已给出四态边界表。**关键依据是评审原话：「这个应该作为初设里的明确决策，而不是实现时临时决定」**。A / B 的差别只在 `Hide → Show` 路径：**A 零额外逻辑**（「恢复」由钩子持续存在天然保证）；**B 需要一个「重装」状态机 + 幂等判据**，而它解决的场景（隐藏期间桌面层被抬升）在窗口不可见时**本就无视觉后果**——只有 `Show` 之后才需要保证位置正确，而那正是钩子的正常工作。⇒ 归初设定稿，**倾向已明确为 A** |

---

## 5. 非目标（YAGNI 圈定）

- **`GetWindowLayer()` 查询 API**——Phase 12 `D-DESK-1` 已明确「YAGNI：无消费者」（`phase12-windowchrome-detailed-design.md` §1.1 表第 9 行）。本阶段**不新增**（虽然实现落地后该 API 的语义不再有「降级」歧义，但**仍无消费者**）
- **运行期切层**——`SetWindowLayer` 保持配置期契约（`K5`）。`D3` 的路径 B 只依赖「配置期内改样式」，**不**松开运行期判据
- **桌面窗口树（`Progman` 子树 / `WorkerW`）路线**——spike 已判死两条（A / D：`WS_EX_NOREDIRECTIONBITMAP` ⇒ 子树不参与合成；reparent 被 explorer 重建杀死）。**不重新探索**
- **`WS_EX_TOPMOST` 路线**——spike 实测能活过 Win+D，但恒在应用窗口**之上**，违反契约另一侧（`desktopnest-roadmap.md` §7.1）。**不可替代 E**
- **跨 DPI 空间的坐标换算修复**（spike 实测的 ×0.8 现象，`desktopnest-roadmap.md` §7.1 末段）——属 **G-4**（DPI 感知），不在本阶段
- **R-3（框内图标不可用）的任何缓解**——已定论为**结构性**（§7.2 四方案全不可行），只能靠应用层位置 / 面积缓解
- **桌面层级的多显示器差异**——不同显示器上的桌面窗口是否同一 `Progman`，未勘察（见 §7 待勘察项）
- **托盘 / 拖入的进一步集成**——Phase 14 已交付，本阶段零改动

---

## 6. 测试 / 验证方向

**分层原则**（`K12`：真实 z 序不可自动化；`R11`：可自动的部分必须自动）：

| 层 | 内容 | 判据 | 自动化 |
|---|---|---|---|
| **L1 逻辑（自动）** | `Desktop` 的目标 z 序位置计算（`D8` 的 C 抽象，经测试缝注入句柄） | `Normal` ⇒ 不修改 · `Bottom` ⇒ `HWND_BOTTOM` · `Desktop` + 有效句柄 ⇒ `GetWindow(progman, GW_HWNDPREV)` · `Desktop` + **无效句柄** ⇒ **跳过修改**（`K8` 核心分支） | ✅ |
| **L1 生命周期（自动）** | 钩子安装 / 脱钩配对（`R3` / `R10` / `K11`） | `SetWindowLayer(Desktop)` ⇒ 钩子 +1；`Set(Normal/Bottom)` ⇒ 卸载；`Release()` / 析构 ⇒ 钩子 -1；`Hide()` → `Show()` ⇒ **既不卸载也不重复安装**（`R10` 四态表） | ✅（经内部测试缝计数，参照 Phase 14 托盘 `NotifyShell` 函数指针先例） |
| **L2 零回归（自动）** | `Bottom` / `Normal` 档语义（`R2`） | 既有 z 序用例全过（`K13`） | ✅ |
| **L2 构建（自动）** | 四工具链 + `_DEBUG`（`R12`） | 构建通过、`ecdi_tests` 全过、零断言 | ✅ |
| **L3 真机（手测）** | spike 六条判据 ①–⑥ + 视觉判据 ⑦ | ① Win+D 后仍可见 · ② 十次切换稳定 · ③ 框外图标可点 · ④ 换壁纸后仍可见 · ⑤ explorer 重启后仍可见 · ⑥ 被应用窗口覆盖 · ⑦ 视觉无异常（无挖洞 / 残影） | ❌ 手测（`D10`：ModelProbe `--layer desktop` + 六条判据对照） |
| **L3 真机（手测）** | `R6` 交互不降级（F-1 澄清的直接验证） | 桌面档下鼠标点击 / 键盘输入 / 焦点 / Widget 命中全部正常；**点击框不抢焦点**（若 `D2` 选 C） | ❌ 手测 |
| **L3 真机（手测）** | `R7` DWM 兼容 | 样式变更后系统阴影 / Win11 圆角仍正常（`ApplyDwmEnhancements` 复验） | ❌ 手测 |
| **L3 真机（手测）** | `R8` `ChromeMode` 正交性 | `Normal` / `Borderless` 两档 × `Desktop` 均可（或记录冲突） | ❌ 手测 |
| **L3 真机（手测）** | 多窗口（`R9`） | 两个窗口分别设 `Desktop` / `Bottom`，各自维护不互扰 | ❌ 手测 |

> 📌 **`--auto` 的价值**：spike 的 `desktop_spike.cpp` 支持 `--auto`（自动注入 Win+D + 采样），已证明**判据 ①⑥ 可无人值守取证**（`desktopnest-roadmap.md` §7.1）。⇒ 若本阶段产出同类探针，应保留该模式以便后续 Windows 版本变更时**快速复验路线是否仍成立**——这是「spike 结论会随系统版本失效」这一风险的唯一对冲手段。

> 📌 **v1.2 实测补充——三条判据环境约束（本次踩坑后确立）**：① **像素类判据（9 点采样）只能在主屏使用**——系统级 DPI 感知下，非主显示器上 `GetWindowRect` 与 `GetPixel` 的坐标系错位（详见 §8.1①，即 **G-4**）；副屏上以 `rect` / `iconic` 为准。② **采样必须同时验证「前提真的发生了」**（Win+D 是否生效）——否则会把「前提未发生」误读为结论（§8 判据纪律）。③ **探针的进程级配置必须与被测框架一致**（DPI 感知级别 / `_DEBUG` / CRT 模式）——本次的教训是「探针未设 DPI 感知而框架是系统级感知」直接导致几何读数整体失真。④ 运行环境亦须洁净：探针窗口若被终端等前台窗口遮挡，`self-pixels` 会整体偏低（首轮多组的 `0/9` 即此因）。

---

## 7. 影响面

| 类别 | 项 |
|---|---|
| **新增 Public** | **零**——`WindowLayer` / `Window::SetWindowLayer` / `PlatformWindow::SetWindowLayer` 全部已就位（§1.1） |
| 修改 Public | **零**（倾向）——除非 `D1` 需要在 `WindowLayer.h` 补充样式语义注释（属注释级改动） |
| 修改 Internal | `Win32PlatformWindow.h`（`D8` 的辅助方法声明 + 钩子成员）· `Win32PlatformWindow.cpp`（`WM_WINDOWPOSCHANGING` **按档位分流** + **`SetWindowLayer(Desktop)` 移除 `WS_MINIMIZEBOX`**（`D1` 取 A′——含改后是否需 `SWP_FRAMECHANGED`、改样式的幂等与可逆）+ 钩子安装/脱钩（`R10` 四态）+ **重插前先判在位**（§8.1②）+ `K2` 过期文案修正 + `Release()` 脱钩）· `Win32PlatformApplication` **零改动**（`D4` 取 A——不加 Timer） |
| 修改 Demo | `examples/ModelProbe/main.cpp:247` 过期注释（`R4`）——⚠️ **AI 不得动 `main.cpp`**，须用户授权 |
| 测试 | `src/Tests/` 新增用例（`D9` 的 L1 两层）+ `RunAllTests.h` / `RunAllTests.cpp` 手工接线；⚠️ **测试替身同步**（若 `PlatformWindow` 加纯虚——**本阶段倾向零新增纯虚**，见 `phase14` §7 的风险先例） |
| **工具（不进仓库）** | `.workbuddy/spike/desktop_layer_probe.cpp`——Phase 16 事实勘察探针（`--auto` 可无人值守复现，`--style` / `--ex` / `--tick` / `--rounds` / `--secondary` 开关），被 `.gitignore` 的 `.workbuddy/` 覆盖。**建议保留**：Windows 版本变更时用它**快速复验本阶段结论是否仍成立**——这是「spike 结论会随系统版本失效」的唯一对冲手段 |
| 文档 | 新增本文件 + 初设 + 详设；`docs/README.md` 索引；`desktopnest-roadmap.md` **§5 G-1 标 ✅** + §9 第 9 项收口 + §10 修订记录；可能需 `roadmap-deferred.md` 记账（若发现新的延期项） |
| 明确不动 | 渲染四层（Widget → PaintContext → CommandBuffer → Renderer）· `WindowLayer` 枚举语义 · Phase 12 的四消息拦截骨架（NCCALCSIZE / NCHITTEST / NCACTIVATE / WINDOWPOSCHANGING **的 `Bottom` 部分**）· Phase 13 命中委托 · Phase 14 托盘 / 拖入 · 布局与尺寸体系 · 公共 API 面（净增 0 头） |

---

## 8. 事实勘察结果（**已完成**——2026-09-19 真机实测）

> **本节原为「进初设的闸门」**（v1.1 定性），现**9 项均已形成明确处置结论**（v1.3 措辞校准——**不是**「9 项全部实测回答」）：**P0–P2 由真机实测闭环**，**P3 的第 7 项（`SetWindowLongPtrW` 安全性）与第 8 项（跨工具链一致性）顺延初设 / 验证阶段**，第 9 项因 D4 已定而消解。D0–D11 的**方案方向**据此可拍板；顺延项见 **§8.2**。
>
> **工具**：`.workbuddy/spike/desktop_layer_probe.cpp`（**不进仓库**，被 `.gitignore` 覆盖）。它与 `desktop_spike.cpp` 的根本区别是**逐字对齐 ECDI 的窗口形态**——`WS_OVERLAPPEDWINDOW`（`Win32PlatformWindow.cpp:67`）+ 扩展样式 `0`（`:64`），并照抄三处 Borderless NC 拦截（`:274-296` / `:299-378` / `:381-402`）。**形态不一致则测的不是「现有形态」，实验无效。**
>
> **方法**：`--auto` 两轮阶段机（注入 Win+D → 1.5s 采样 → 还原 → 采样）× **默认 3 轮**，每轮输出自解释的 `VERDICT` 行并按**多数轮**汇总；另加**用户目视确认**。
>
> **判据纪律（本次两度踩坑后确立，值得沿用）**：① 采样必须**同时验证「前提真的发生了」**（`IsWinDEffective`：窗口被最小化 ∨ 不再前台）——否则会把「Win+D 未生效」误读成结论（首轮即出现一个 `9/9` 的假阳性）；② 判据**相对基线**（`px >= baselinePx`）而非绝对阈值，并把 **`iconic` 作为最强判据**（不依赖像素量具）。

| 优先 | # | 项 | **实测结果（2026-09-19）** | 判定 |
|---|---|---|---|---|
| **P0** | 1 | `WS_OVERLAPPEDWINDOW + Borderless + Desktop` 能否满足 spike 的可见性判据 | **A ❌ FAIL**：`rect=(-32000,-32000,-31840,-31972)` + `iconic=1` ⇒ 被「显示桌面」**最小化**（两次独立运行各 3/3）。**A′ ✅ PASS**：`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`，`rect` 不变 + `iconic=0`（3/3 + 用户目视）。**C ✅ PASS**：`WS_POPUP`（3/3 + 用户目视） | ✅ **真因 = `WS_MINIMIZEBOX`** ⇒ D1 新增 A′ 并改为首选 |
| **P1** | 2 | `Desktop + Borderless` 的交互能力（可激活 / 可点击 / 可获得焦点） | `ex=0` 下 `clicks=15 / focusEvents=2 / activateEvents=2`，点击坐标覆盖整个客户区（含 y=12/18）；popup 形态启动即 `WM_ACTIVATE ACTIVE` + `WM_SETFOCUS` | ✅ **正常** ⇒ D2 排除 `WS_EX_NOACTIVATE` |
| **P2** | 3 | 只装前台钩子是否已足够（D4 的取舍前提） | **tick=0 两组均 PASS** ⇒ 钩子单独已足够；tick=1 组 `reinsert=13` / `FindWindowW=78` vs tick=0 的 **4 / 9** ⇒ **3 倍多开销、零收益**，且是「闪烁一下」的来源之一（用户目视） | ✅ **足够** ⇒ D4 取 A，**不加 Timer** |
| P3 | 4 | `FindWindowW` 的调用成本（D6） | tick=0 时 **9 次 / 11.2s ≈ 0.8 次/秒**；tick=1 时 78 次 / 11s ≈ 7 次/秒（后者是 tick 自身造成的） | ✅ **可忽略** ⇒ D6 取 C（不缓存） |
| P3 | 5 | `GetShellWindow()` 与 `FindWindowW(L"Progman")` 是否恒等（D7） | **`equal=1`**——四组独立运行全部一致（两者返回同一 HWND，类名均为 `Progman`） | ✅ **恒等** ⇒ D7 可用官方 API |
| P3 | 6 | 多显示器下 `Progman` 的唯一性 | `total desktop-class = 17` 而 **`Progman` 只有 1 个**；其余 16 个是 **136×39 的 `WorkerW`**（IME / shell 辅助宿主，与 `desktop_spike.cpp:392` 的记录同款）。`Progman` 的 `rect=(-1920,0,1920,1080)` **跨双屏**，而 `monitors=2` | ✅ **唯一** ⇒ **D6/D7 的结论不需按显示器维度重做** |
| P3 | 7 | `SetWindowLongPtrW` 改样式的安全窗口（D3） | **未单独实测**——但它**只在「P0 判必须改样式」时才有意义**，而 P0 的结论正是「必须改」（A′）⇒ **升为初设必答项**（要求见 D3） | ⏭ **顺延初设** |
| P3 | 8 | `SetWinEventHook` 跨工具链一致性（K9） | **未测**——本轮探针只在 MinGW 下构建运行；`hInstance` 传 `nullptr` 在 `WINEVENT_OUTOFCONTEXT` 下为 MSDN 允许 | ⏭ **顺延四工具链验证** |
| P3 | 9 | 平台内部 `SetTimer` 的可用性（D4 选 C 时） | **不再需要**——D4 已由实测钉死为 A（不加 Timer） | ✅ **消解**（前提消失） |

### 8.1 勘察得出的两条新约束（v1.2 新增）

**① 像素采样在非主显示器上不可用**（**G-4 的直接表现**）：本机副屏为 125% 缩放，实测 `[cfg] window DPI = 96` 而 `self-pixels` 恒为 `4/9`，并出现 `center-hit=SELF` 却 `outside-sample=#0080A0`（窗口外 20px 取到本体底色）的**自相矛盾读数**。根因：进程为**系统级 DPI 感知**（非 Per-Monitor V2）⇒ 系统在非主显示器上仍做 **DPI 虚拟化** ⇒ `GetWindowRect`（虚拟坐标）与 `GetPixel`（物理屏幕坐标）**两套坐标系错位**。

⇒ **影响测试策略**：§6 的**像素类判据只能在主屏使用**；副屏上以 `rect` / `iconic` 为准。

⚠️ **这不是探针缺陷**——探针 v3 已 `SetProcessDPIAware()` **与框架对齐**，错位依旧，说明它是**框架当前 DPI 感知级别的固有性质**（正对应 **G-4**）。本次 P0 结论不依赖像素判据，故不受影响。

**② 「重插前先判在位」是必要的优化**：spike 路线 E 的 `InsertAboveDesktop` 是**无条件重插**——前台事件命中 `Progman` / `WorkerW` 即调 `SetWindowPos`，**不先判断自己是否已在位**（`desktop_spike.cpp:590-603`）。实测中用户观察到 Win+D 瞬间窗口「**闪烁一下**」，而 tick 组的重插次数是钩子组的 **3 倍多**。⇒ 初设应加：**先 `IsDirectlyAboveDesktop()`，在位则不动**（零成本，直接减少 Win+D 瞬间的抖动）。余下的那次闪烁源于系统重排（平台行为），只能靠此压到最小。

### 8.2 留给初步设计的问题清单（v1.3 新增——「已闭环 / 待初设」的分界）

**需求阶段已闭环的**（方案方向已定）：

| 已定 | 结论 | 依据 |
|---|---|---|
| 真因与样式 | `WS_MINIMIZEBOX` ⇒ **A′：`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`** | D1（实测） |
| 扩展样式 | **不加 `WS_EX_NOACTIVATE`** | D2（实测） |
| 维护机制 | **只装前台钩子，不加 Timer**；且**重插前先判在位** | D4（实测）+ §8.1② |
| 句柄策略 | **不缓存 `Progman` 句柄**（每窗口按需即时重查） | D6 |
| 钩子归属 | **每窗口一个钩子**，生命周期绑 `Win32PlatformWindow` | D5 |
| 钩子生命周期 | `Set` ⇒ 装 · `Hide` ⇒ **不卸载** · `Show` ⇒ **不重复装** · `Release` ⇒ **必卸** | D11 |
| 定位 API | `GetShellWindow()` ≡ `FindWindowW("Progman")` ⇒ 倾向官方 API | D7（实测） |

**顺延初设 / 实现验证的**（本阶段只给方向与约束，**不给结论**）：

| # | 待初设回答 | 来源 |
|---|---|---|
| 1 | `A′` 的具体改法：`SetWindowLongPtrW(GWL_STYLE, … & ~WS_MINIMIZEBOX)` 的**调用点** · **是否需 `SWP_FRAMECHANGED`** · **幂等与可逆**（`SetWindowLayer(Normal)` 是否恢复该位） | D3 + §8-7 |
| 2 | **`WS_EX_TOOLWINDOW` 的最终取舍**（任务栏 / Alt+Tab / 窗口类别）——须**补验它不影响 Win+D 行为** | D2 的开口项 |
| 3 | `SetWinEventHook` 的**成员、回调函数与生命周期落法**；含**四工具链一致性**（`hInstance` 传 `nullptr` vs `WindowClass::GetInstance()`） | R3 / R10 / D11 + §8-8（K9） |
| 4 | `TargetInsertAfter` / `IsDirectlyAboveDesktop` 的**内部结构**（目标 z 序的计算，`D8`） | D8 |
| 5 | 桌面窗口定位最终采用 `FindWindowW("Progman")` 还是 `GetShellWindow()` | D7 |
| 6 | **句柄无效时如何做到绝不误降级 `HWND_BOTTOM`** 的代码级保证（`K8` / `D8` 的「跳过修改」语义） | K8 / D8 |
| 7 | `Hide → Show → Release` 状态机的**具体落法**（`R10` 四态边界表 → 代码） | D11 |
| 8 | **测试缝形态**（钩子计数 / 样式回读）与四工具链下的测试策略 | §6 / D9 |

> ⚠️ **边界纪律**：上表全部是**设计问题**，不是需求问题——需求阶段对它们只承诺「方向」与「约束」，**结论归初设**。这是本阶段与下一阶段的**分界线**。

---

## 9. 修订记录

- v1.3（2026-09-19）**外部评审处置——3 处文档自洽性修正 + 1 项新增**（评审结论：「**方向已可进初设**，但有 3 处『已回答』与『仍待初设验证』的表述打架」；**全部采纳**）：
  - **① 严谨化「已答」的范围（实质缺陷）**：头部状态行与 §8 引言由「9 项**已由真机实测全部回答**」改为「**9 项均已形成明确处置结论**；P0–P2 真机闭环，**P3 中实现安全性（§8-7）与跨工具链一致性（§8-8）顺延初设/验证**」。⚠️ 原表述与 §8 表内第 7/8 项自带的「⏭ 顺延初设」**直接矛盾**——把「有处置结论」写成了「已实测回答」，是**过度声明**。
  - **② §2「维持住」与 D4/D6 对齐**：由「前台事件钩子 + **位置校验（兜底）** + 桌面句柄**重建自适应**」改为「**前台事件钩子 + 重插前位置判定 + 桌面窗口句柄即时重查**」。同时把「**重插前先判在位**」从 §8.1② 的备注**升格为正式设计输入**（评审指出：它比 spike 的「无条件重插」更干净，且实测已证无条件重插是 Win+D 瞬间抖动的来源）。
  - **③ R9 与 D6 对齐**：由「每个窗口独立持有自己的钩子 / **句柄缓存**」改为「每窗口独立持有**维护钩子与维护状态**；**桌面窗口句柄不做长期缓存**」。⚠️ 原「句柄缓存」是 v1.0 写作时的设想，D6 取 C（不缓存）后已成矛盾——**顺带肯定 D6 的价值**：不缓存把「explorer 重建 ⇒ 旧句柄失效」从**待处理的状态机**变成**结构上不存在的问题**。
  - **④ §2 四个硬骨头统一标注答案**（原文只标了第 4 项）：① 窗口样式 → A′ ② `WS_EX_NOACTIVATE` → 不加 ③ 样式改动时机 → D3 取 B（**改动时机**已答；**改动安全性的细节**顺延初设）④ 维护兜底 → 不加 Timer。
  - **⑤ 新增 §8.2「留给初步设计的问题清单」**：上半区列 7 项**已闭环**结论，下半区列 8 项**顺延初设**的问题（含 GPT 列出的 9 条对应的实际落点），并写明**边界纪律**——这些是设计问题，不是需求问题。
  - **⚠️ 一处编号更正（回应评审）**：评审建议中把「跨工具链钩子一致性」记为「**D5**」，实际它对应 **§8 第 8 项（`K9`）**；**D5 是「钩子安装形式」**（每窗口一个 vs 应用级共享）。本版按**实际编号**写入 §8.2 第 3 项。实质建议（顺延初设、不在需求阶段硬证）**完全采纳**。
  - **状态**：仍为 **✅ 勘察完成 —— 方案方向可拍板，可进初步设计**（本轮只修表述，**未改动任何 D 项结论**）。
- v1.2（2026-09-19）**事实勘察完成回写——P0 的结论改变了 D1 的选项**（需求稿定稿，**可进初步设计**）：
  - **§8 整章重写**：「待勘察项」→「**事实勘察结果**」——9 项逐条填入实测判据与判定；新增 **§8.1 两条新约束**（① 像素采样在非主显示器不可用 = **G-4 的直接表现**；② **重插前先判在位**）。
  - **★ 核心发现（改变了 D1 的形状）**：**「显示桌面」只最小化「可最小化窗口」** ⇒ 真因是 **`WS_MINIMIZEBOX`**，**不是 `WS_POPUP` 本身**。实测 4 形态 × 3 轮：`WS_OVERLAPPEDWINDOW` **FAIL**（`rect=(-32000,-32000)` + `iconic=1`，两次独立运行各 3/3）· **`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX` PASS** · `WS_POPUP` **PASS**（后两者另有**用户目视确认**：「其它页面被收起来，这个框会闪烁一下，不过仍然在原位」）。
  - **§1.3 F-1 回写**：对照表「主样式」一行**收窄**——原「⚠️ **结构性**」措辞**过强**，实测显示差异是**一位可修**的；补入三形态判决表 + 机制结论 + 「`WS_EX_TOOLWINDOW` 仍未实测」的显式开口。
  - **D1 更新**：**新增选项 A′ 并改为首选倾向**（保留除「最小化」外的**全部** Phase 12 红利；C 会连带失去 `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MAXIMIZEBOX`）；记账 A′ 的语义后果（任务栏「最小化」/ `Win+↓` 失效、Normal 档标题栏最小化按钮呈灰态）。**方法论留档**：A 被证否后正确的下一步是**继续问「最小改动是什么」**（得到 A′），而不是一步跳到最粗的 C——**这正是 v1.1 评审坚持「先证明现状能承载」所要的思考顺序**。
  - **D2 更新**：**实测钉死「不加 `WS_EX_NOACTIVATE`」**（`ex=0` 下 `clicks=15 / focusEvents=2 / activateEvents=2`，坐标覆盖整个客户区）；**`WS_EX_TOOLWINDOW` 仍未实测** ⇒ 留作初设决策（倾向：**加**，桌面常驻物不占任务栏位）。
  - **D3 更新**：倾向由「B / C 二择」改为**取 B 且明确为必需**（A′ 必须改样式位；配置期内改是安全的，F-2 的「时序紧张」被配置期语义消解）；新增两条初设必答（是否需 `SWP_FRAMECHANGED` · 改样式的幂等与可逆）。
  - **D4 更新**：**实测钉死「只装前台钩子、不加 Timer」**——tick 组 `reinsert=13` / `FindWindowW=78`（11s）vs 钩子组 `4` / `9`，**3 倍多开销零收益**，且是用户观察到的「闪烁一下」来源之一。
  - **D6 / D7 更新**：D6 取 C（不缓存）获实测支持（tick=0 时 **0.8 次/秒**）；D7 确证 **`GetShellWindow()` ≡ `FindWindowW("Progman")`**（`equal=1`，四组独立运行一致）。
  - **§6 补充**：新增三条判据环境约束（像素判据仅主屏 · 采样须验前提 · 探针进程级配置须与被测框架一致）+ 运行环境洁净要求。
  - **§7 更新**：`Win32PlatformWindow.cpp` 改动项具体化（**按档位分流** + **移除 `WS_MINIMIZEBOX`** + **重插前判在位**）；`Win32PlatformApplication` **零改动**；新增「工具（不进仓库）」行。
  - **状态**：**✅ 勘察完成 —— D0–D11 可拍板，可进初步设计**。
- v1.1（2026-09-19）**外部评审处置——8 项全部采纳（无否决）**。评审结论原文：「**范围 / 目标 / 非目标 / 验收边界已经很完整；当前真正缺的是 §8 的事实勘察。完成这些勘察后，就可以拍 D1–D11 并进入初步设计**」：
  - **① 状态行**：由「待评审」改为「**待事实勘察**」——这是本轮最重要的状态修正。评审明确「**还不能拍板进入初设**」，理由不是需求不完整，而是 **§8 的 6 项「设计前事实」尚未取得**。
  - **② §1.1 补设计方针**：**先证明「现有窗口形态能不能承载 Desktop 层」，证明不了才改变窗口形态**（评审归纳，与项目「最小改动 / 平台内部解决 / 不污染公共 API」路线一致）；并由此立一条纪律——**spike 的实现细节 ≠ 框架的设计输入**：spike 验证的是「路线」，不是「参数」。
  - **③ D1 顺序修正 + 补强（v1.0 的实质错误）**：v1.0 写作「倾向 C + 须验证 A」——评审指出**顺序反了**。改为「**A 为首选假设 → 验证不通过才取 C**」。补评审原创论点：**「系统标题栏存在」≠「用户看得到系统标题栏」**——Borderless 若已用 `WM_NCCALCSIZE` 处理非客户区，则 `WS_CAPTION` 是否妨碍视觉应当**实测而非凭样式定义判断**。downgrade `WS_POPUP` 的理由也写实：那等于**偷偷修改 Phase 12 的窗口行为契约**。
  - **④ D2 补强**：`WS_EX_TOOLWINDOW` 与「能否交互」**不是一回事**（它影响任务栏 / Alt+Tab / 窗口类别）⇒ 独立成 C 选项是对的。采纳评审对 `NOACTIVATE` 的判词：「**spike 为了方便测试做的东西，不应该反向成为框架 API 的约束**」。
  - **⑤ D5 补强**：显式记入 B 的风险方向——**钩子生命周期必须绑定 `Win32PlatformWindow`**，不能绑 `Application`、不能全局 singleton（评审：「不能出现 Window 已 Destroy → 钩子还在 → 回调继续打到 `this`」）。
  - **⑥ R10 细化（原为一句模糊表述）**：由「`Hide()` 后 `Show()` 维护应能恢复」升级为**钩子生命周期四态边界表**（Set ⇒ 装 · 设回 `Normal`/`Bottom` ⇒ 卸 · **Hide ⇒ 不卸载** · **Show ⇒ 不重复安装** · `Release()`/析构 ⇒ **必卸**），并给出每条的依据（含 `PlatformWindow.h:124-127` 的 `Hide` 语义）。
  - **⑦ 新增 D11**（钩子生命周期边界）——评审提出的**最重要补项**，原话：「这个应该作为初设里的**明确决策，而不是实现时临时决定**」。倾向 **A（`Hide` 保留、`Release` 清理）**：零额外逻辑（「恢复」由钩子持续存在天然保证），而 B 需要一个「重装」状态机去解决一个**窗口不可见时本就无视觉后果**的场景。
  - **⑧ §8 重构**：原 6 项扩为 **9 项**并加 **P0–P3 优先级**列 —— 新增 D6 成本核实（`FindWindowW` 频率）· D3 安全窗口（**仅在 P0 判必须改样式时才有意义**）· 平台内部 `SetTimer` 可用性；并加**勘察顺序**（评审给定：P0 → P1 → P2，三项决定其余）与一条 P0 前提澄清（`WS_OVERLAPPEDWINDOW` 管「有哪些系统能力」、`Borderless` 管「非客户区画不画」，两者不冲突）。
  - **保持不动的评审共识**：本阶段定性「兑现既有承诺」· spike 与框架需求分离 · D4 否掉「走 `TimerEvent`」（不污染框架事件模型）· D6 不缓存 `Progman`（把 K8 的失效问题**结构性消除**）· D8 的 `TargetInsertAfter()` 抽象（含「句柄无效 ⇒ **不修改**」这条关键分支，防 `Desktop` 意外降级成 `Bottom`）· D5 每窗口一钩子 · 测试三层分层（不幻想用 `RecordingBackend` 测 z 序）· R3 的生命周期要求。
  - **规模变化**：224 → 约 250 行；决策点 11 个（+D11）；待勘察项 9 项（+3，含优先级）。
- v1.0（2026-09-19）**需求确认初稿**（立项：`desktopnest-roadmap.md` v1.7 §5 G-1，用户 2026-09-19 拍板「G-1 立项」）：
  - §1 背景与现状勘察——立项定性「**兑现既有承诺**」而非探索新能力（语义契约 / 公共 API / 实现路线三者均已就位，缺的只有平台实现）· **K1–K14 现状勘察全部带行号**（2026-09-19 复核）· **三条会改形态的事实**：**F-1 窗口样式三处差异**（`WS_OVERLAPPEDWINDOW` vs spike 的 `WS_POPUP \| WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE`，含一条常识性澄清——`WS_EX_NOACTIVATE` 是**探针便利**而非路线必要条件）· **F-2 样式决定时机与配置期契约错位** · **F-3 维护机制缺心跳**。
  - §2 技术路线——「贴上去」= 平台实现（公共 API 零变更）· 「维持住」= 前台钩子 + 位置校验 + 桌面句柄重建自适应；四个硬骨头。
  - §3 需求条目 **R1–R12 四组**：实现主体 R1–R4（含「`Bottom` 语义零回归」与「句柄失效自适应」两条硬要求）· 交互与样式 R5–R7（含**交互能力不得降级**——F-1 澄清的需求化）· 覆盖范围 R8–R10（`ChromeMode` 正交 · 多窗口 · `Hide`/`Release` 关系）· 测试与验收 R11–R12。
  - §4 决策点 **D0–D10 全部给倾向待拍板**（范围形态 / 主样式 / `WS_EX_NOACTIVATE` / 改动时机 / 维护兜底 / 钩子形式 / 句柄缓存 / 定位方式 / 代码组织 / 测试边界 / 手测判据）。⚠️ **D1 / D2 是本阶段的真正取舍点**——其余八项在倾向明确后基本无争议。
  - §5 非目标（`GetWindowLayer` 仍不做 / 运行期切层 / 桌面窗口树路线不重探 / `TOP MOST` 不可替代 / 跨 DPI 归 G-4 / R-3 缓解不做 等 8 项）· §6 测试方向（**L1 自动 / L2 零回归 / L3 手测三层**，含 `--auto` 作为「系统版本变更时的对冲手段」）· §7 影响面（**公共 API 净增 0**——与 Phase 12/14/15 均不同，本阶段纯实现层）· §8 **待勘察项 6 条**。
  - **需求阶段边界自律**（skill 条 6）：本文档不含头文件草案与方法签名——全部归初步设计；`D8` 的「目标 z 序位置计算函数」以概念形如表述，不写签名。
