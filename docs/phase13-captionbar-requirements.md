# Phase 13 CaptionBar 自绘标题栏 需求确认（v1.1）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-13（v1.1 同日外部评审修订）
> 状态：**✅ 已实现并验收（2026-09-14 ~ 09-15）**——证据：详设 `phase13-captionbar-detailed-design.md` **v1.4**（四构建 188/188 + ModelProbe 手测 + A 项全部通过）；原状态「✅ v1.1 外部评审通过（可进入初步设计）」（D0–D9 评审建议与本文倾向一致，含 D9「可交互」判定 + D7 职责二分）已按此实现
> 前置：`phase12-windowchrome-requirements.md` v1.2 §R5（CaptionBar 推迟项）· `window-ownership.md` v1.1（所有权契约）
> 一句话：在 Borderless 窗口里补上「看得见摸得着」的标题栏——标题文本 + 最小化/最大化/关闭三按钮。地基（四消息拦截 / 九宫格命中 / 运行期 API / 状态事件）Phase 12 已全部就位，本阶段只补 **Widget 本体 + 一条 NCHITTEST 委托接缝**。
> v1.1 修订：D0 措辞保留（认可提前解锁 + 明记「满足立法意图而非字面条件」）· **D7 由「解耦倾向」升级为「职责二分定案」** · **新增 D9（HitTest 命中 ≠ 应阻止拖拽——标题 Label 拖拽保留）** · §2 补第三条硬骨头 · §6 补拖拽回退用例 · §7 补 D9-A 成本

---

## 1. 背景与现状勘察

### 1.1 R5 推迟由来（Phase 12 需求 v1.2 裁决原文）

> **决策：Phase 12 只做 Window Chrome，不做 CaptionBar 控件**（推翻 v1.1 的「倾向 b」）……
> **CaptionBar 控件推迟**：待真实项目（ModelProbe / DesktopNest / Demo）重复实现标题栏 ≥2 次后再立项（「二次用例出现再抽象」）

### 1.2 解锁条件复核（D0——请评审首先拍板）

字面上的「**≥2 次重复实现**」尚未发生；但该门槛的**立法意图**（防无消费者的投机抽象）已被满足：

| 消费者 | 事实 |
|---|---|
| **ModelProbe** | Borderless 模式**没有关闭按钮**（2026-09-12 接入 `--borderless` 实测时暴露——关窗只剩 Alt+F4 / 任务栏右键）；现有「窗口控制」按钮行是**临时替代品**（三个裸按钮 + 无标题文本），正是「手搓半个标题栏」 |
| **DesktopNest** | 规划文档已定——桌面常驻窗口**必然**自绘标题栏 |

⇒ **≥2 个真实消费者需要它，且其一已实测暴露功能缺口**。是否认可此为解锁，见 D0。

### 1.3 已有 vs 缺（Phase 12 交付盘点）

| 已有（零新增） | 缺（本阶段范围） |
|---|---|
| 四消息拦截（NCCALCSIZE / NCHITTEST / NCACTIVATE / WINDOWPOSCHANGING） | **CaptionBar Widget 本体**（标题文本 + 三按钮） |
| `captionHeight` 命中区（拖拽 / 双击最大化·还原，系统路径） | **NCHITTEST ↔ Widget 树委托**（按钮在 NC 区收不到鼠标——§2） |
| 运行期 `Minimize / Maximize / Restore` + `WindowStateChangedEvent` | **Window 状态查询**（max 按钮二态图标需要「当前是什么状态」） |
| 绘制能力（文本 / DrawLine / DrawRect / DrawRoundedRect / 圆角抗锯齿） | **close 按钮语义接线**（复用关闭请求路径） |
| Hover 机制 / Button·Label 控件 / 样式系统 / 9.7·9.8 布局 | 悬停/按下视觉态组装 |

---

## 2. 技术路线（一段话定方向）

自绘标题栏 = **普通 Widget 位于客户区顶部**，不是「在非客户区里画东西」。两条推论决定本阶段形态：

1. **命中是唯一的硬骨头**——Borderless 下 `y < captionHeight` 区域整体返回 `HTCAPTION`（NC 区），放在那里的按钮**永远收不到鼠标**。必须让 `WM_NCHITTEST` 在 caption 区先问 Widget 树：命中可交互控件 → `HTCLIENT`，未命中 → `HTCAPTION`（拖拽路径不变）。一旦返回 `HTCLIENT`，按钮的鼠标 / 悬停 / 点击全部落入**既有 Widget 机制**，零新增。
2. **不换路线**——不引入 `WS_POPUP`、不重写 NCCALCSIZE、不动 Phase 12 的四消息拦截骨架；拖拽与双击最大化继续走 `HTCAPTION` 系统路径。

3. **命中语义是第二个硬骨头**——「`HitTest` 命中」**≠**「应返回 `HTCLIENT`」。CaptionBar 的**标题文本**是 Label：它**会被 HitTest 命中**，但用户拖标题文字时**必须仍能拖动窗口**。⇒ R2 的委托判据是「**该点是否可交互（消费鼠标输入）**」，而非「命中是否非空」；判定来源见 D9。此条不解决，手感会出现「标题文字区域拖不动」的诡异割裂。


---

## 3. 需求条目

### R1：CaptionBar Widget
标题文本（Label 语义）+ 三按钮（最小化 / 最大化·还原二态 / 关闭）。按钮图形**矢量自绘**（`DrawLine` / `DrawRect`——Phase 8 能力对口，零资源依赖）。CaptionBar 是**普通 Widget**：可 `AddChild` 进任意树，参与 9.7/9.8 布局（`fillCrossAxis` 拉宽、随窗口 resize/最大化跟随——现成）。

### R2：NCHITTEST ↔ Widget 树委托（本阶段核心）
`WM_NCHITTEST` 的 caption 分支改造：**先问 Host（Widget 树命中），命中「可交互控件」返回 `HTCLIENT`，否则 `HTCAPTION`**。优先级保持 Phase 12 定案：**resize 区 > （caption 区内：可交互控件 > `HTCAPTION`）**。平台层不认识 Widget——委托契约走 Host（R9 惯例第三次应用，形态见 D2）。

⚠️ **「可交互」不是「HitTest 非空」（评审 §R2 追问——本阶段最易错的语义）**：

| 位置 | HitTest | 应返回 | 理由 |
|---|---|---|---|
| CaptionBar 的**标题 Label** | ✅ 命中 Label | **`HTCAPTION`** | 纯显示控件不消费鼠标；拖标题文字必须能移动窗口 |
| CaptionBar 的**按钮** | ✅ 命中 Button | **`HTCLIENT`** | 消费鼠标（点击语义）——必须收到 Down/Up/Click |
| CaptionBar **空白处** | ❌ 未命中 / 命中 Bar 容器 | **`HTCAPTION`** | 空白区即拖拽区 |
| `captionHeight` 内**未被 Bar 覆盖**的条带 | ❌ | **`HTCAPTION`** | 见 D7 定案 |

⇒ 委托判据 = **「该点是否落在消费鼠标输入的控件上」**（判定来源见 **D9**）。若简单写成「HitTest 非空即 HTCLIENT」，标题栏会出现「文字区拖不动、空白区能拖」的割裂手感。

### R3：Window 状态查询 API
max 按钮的二态图标需要「**当前**是什么状态」，而状态事件只回答「**变了**成什么」。新增查询（概念形如 `GetWindowState`，签名归初设）——这是 T7「事件表示状态事实」语义的**查询面**，其它消费者同样受益。

### R4：关闭按钮语义
close 按钮**发关闭请求**（走 `OnWindowCloseRequested`——与系统 X 完全同通道、可被应用拦截），不直接 `Release()`。基类默认实现已是销毁窗口，零新逻辑。

### R5：悬停 / 按下视觉态
悬停高亮走既有 Hover 机制；**按下态**为新增评估项（Button 有 pressed 概念可借鉴——边界归初设）。

### R6：布局集成
CaptionBar 挂在 RootWidget 树内（如 VLayout 首行），**不特判**——9.7 `SetStretch` / 9.8 AutoSize / fillCrossAxis 全部现成。

### R7：`captionHeight` 与 Bar 高度的关系
命中区高度（`SetCaptionHeight`）与 CaptionBar 实际高度是**两个量**——关系（解耦 / 联动）见 D7。

### R8：回归底线
NCHITTEST 是本阶段改动**风险最高点**：既有 T0–T7a 全部依赖它（T2 九宫格 / T3 模式隔离 / T5 单位 / T6 边界）。要求：**零回归 + 新增委托用例**。

---

## 4. 决策点（D0–D9——评审建议已全部对齐，D9 为评审追问新增）

| # | 决策 | 选项 | **倾向** | 理由 |
|---|---|---|---|---|
| **D0** | 解锁条件（§1.2） | 认可提前立项 / 严格等「≥2 次重复实现」 | **认可** | ModelProbe 缺口是实测事实；DesktopNest 需求明确——意图已满足 |
| **D1** | CaptionBar 归属形态 | **A** 独立公共 Widget（用户组合挂载）/ **B** `SetChromeMode(Borderless)` 自动内建 | **A** | 组合哲学（Widget System Composite）；「Window 不替用户决定内容」先例（9.7 D4——布局显式设于 demo 入口）；B 会让 Window 反向依赖 Widget 域 |
| **D2** | 命中委托契约形态 | **A** `PlatformWindowHost` 加一条虚方法（客户区命中查询，签名归初设）/ **B** Window 维护「NC 排除矩形列表」推给平台 | **A** | R9 惯例第三次应用：平台**问契约**、不认识 Widget；B 需要双向同步 = 第二真相源风险 |
| **D3** | 头域归属 | `include/ECDI/Window/CaptionBar.h` / `Widget/` 域 | **Window 域** | 消费 Window API（状态查询 + 关闭请求）；Widget 域控件不认识 Window（TextBox 是刻意例外） |
| **D4** | 按钮图形 | 矢量自绘 / 位图图标资源 | **矢量自绘** | 零资源依赖（框架无第三方原则同源）；Phase 8 能力对口；缩放无损 |
| **D5** | 样式深度 | 构造参数 / StyleOverride vs 接 Theme·StyleField | **前者** | Theme 二次用例再接（YAGNI）；Phase 9 决策先例 |
| **D6** | 状态获取 | **新增 Window 查询 API** / CaptionBar 自记忆最后事件 | **新增** | 「状态是事实」的查询面；自记忆 = 状态双份维护（R7 评审否决过同款方案） |
| **D7** | `captionHeight` ↔ Bar 高度 | 解耦（各自设置）/ 联动 | **解耦 · 语义已定案**（见 §4.1） | 联动 = 隐式同步；两数职责本就不同——`captionHeight` 是**行为区**、Bar 高度是**实体区** |
| **D8** | close 行为 | 发关闭请求（可拦截）/ 直接 `Release()` | **发请求** | 与系统 X 同通道；`OnWindowCloseRequested` 现成；ModelProbe 的后端清理正挂在这条路径上 |
| **D9** | **「可交互」判定来源**（评审 §R2 追问新增——本阶段最重要接口边界） | **A** 控件自声明（Widget 加一条「是否消费鼠标输入」的能力声明，交互控件 override；默认否）/ **B** CaptionBar 自答（Bar 内部判断该点是否落在按钮上）/ **C** 控件类型白名单（Window 侧硬编码） | **A** | 与 `ContainsPoint` / `CanFocus` 同族的 Template Method 惯例；成本极小（默认**非纯虚** ⇒ **零破坏**，约 4 个控件 override）；B 把通用问题局部化在 CaptionBar（未来其它 chrome 消费者要重写）；C 硬编码类型 = 违反开闭 + 破坏分层。⚠️ **不复用 `CanFocus`**——那是键盘焦点语义，复用会让两个概念互相绑架 |

---

### 4.1 D7 语义定案（评审 §D7 追问 → 两个数职责二分）

| 量 | 职责 |
|---|---|
| `captionHeight`（`SetCaptionHeight`） | **系统标题栏行为区域**——范围内**未落在可交互控件上**时返回 `HTCAPTION`（拖拽 / 双击最大化·还原 / 系统菜单） |
| CaptionBar 高度（Widget 实体） | **可交互实体范围**——控件实际覆盖区；命中可交互控件 → `HTCLIENT` |

**三层判定顺序（锁死）**：resize 区（最高，T2 原断言保持）→ caption 区内**可交互控件** → `captionHeight` 内未命中 → **`HTCAPTION`** → 超出 `captionHeight` → 正常 client（`HTCLIENT`）。

**两个推论（写死，避免实现自行猜）**：① 两数不一致时，`captionHeight` 内**无 Bar 覆盖的条带仍是拖拽区**；② Bar 高于 `captionHeight` 时，超出部分**不参与拖拽**（已是普通 client 区），但命中可交互控件仍为 `HTCLIENT`。

**不联动**：框架不把 Bar 高度隐式写回命中区；建议使用方设同值（视觉与手感一致）。

## 5. 非目标（YAGNI 圈定）

- **Snap Layouts**（Win11 悬停 max 键的布局菜单）——自绘按钮的固有缺失，**接受并记录**（Phase 12 初设已预告）
- 标签页式标题栏 / 一窗多 CaptionBar / 可拖出浮动 Bar
- Aero 动画定制 / DWM caption 区域重绘
- Theme 深度集成（D5 已圈——二次用例再立项）
- caption 拖拽行为变更（`HTCAPTION` 系统路径原样保持）
- 每显示器 DPI（框架级 Deferred，`roadmap-deferred.md` 记账）

---

## 6. 测试 / 验证方向

| 组 | 内容 | 判据 |
|---|---|---|
| **命中委托**（新增 T 系） | 按钮矩形中心 → `HTCLIENT`；同 y 的空白 caption 区 → `HTCAPTION`；resize 区（`x<inset`）仍 `HTLEFT` 等 | T2 原断言**全部保持**（优先级不变） |
| **拖拽回退**（新增 T 系——D9 回归锚） | 标题 Label 覆盖区中心 → **`HTCAPTION`**（命中 Label 但不消费鼠标）；同 Bar 内按钮中心 → **`HTCLIENT`**；Bar 空白 → **`HTCAPTION`** | 三态区分，防「HitTest 非空即 HTCLIENT」回退 |
| **四按钮四路径** | `SendMessage` 探针（T 系同款、不 Show、零闪窗）→ min / max / close 各自生效 | 状态事件回流断言（minimized / maximized / 关闭请求触发） |
| **状态查询** | 查询值 == 最后一次事件的状态 | 一致性 |
| **回归** | T0–T7a 全量 | **零回归**（重点 T2 / T3 / T5 / T6） |
| **手测** | ModelProbe 挂 CaptionBar：拖拽 / 双击 / hover / close 拦截 | 视觉 + DebugView 日志 |

---

## 7. 影响面

| 类别 | 项 |
|---|---|
| 新增 Public | `CaptionBar.h`（**85 → 86**） |
| 修改 Public | `Window.h`（+状态查询）；`PlatformWindowHost.h`（+1 虚——**测试替身 ×2 必须同步补 override**：`AnimationTests.cpp` / `ProgressBarTests.cpp`，skill 条 33 预先列入） |
| 修改 Internal | `Win32PlatformWindow`（NCHITTEST caption 分支：先问 Host）；`Window.cpp`（Host 实现 → `RootWidget::HitTest`） |
| 消费者 | ModelProbe 挂 CaptionBar（评估替换临时「窗口控制」按钮行） |
| 文档 | README 索引 + `phase12-windowchrome-requirements.md` §R5 状态回写（推迟项 → 已立项） |
| 若 D9 选 A | `Widget.h` +1 条**非纯虚**能力声明（默认 false ⇒ **无破化**）+ 约 4 个交互控件 override（Button / CheckBox / Radio / TextBox）——**无测试替身同步问题** |
| 明确不动 | B 契约（所有权）、渲染四层、B1–B5、NCCALCSIZE / NCACTIVATE / WINDOWPOSCHANGING 三分支 |

---

## 8. 修订记录

- v1.2（2026-09-14）实现落地状态同步（补记）：原头部记「✅ v1.1 外部评审通过（可进入初步设计）」——D0–D9 已于 2026-09-14 实现（`phase13-captionbar-detailed-design.md` v1.2 为实施依据），静态自查通过（A6：`IsClientInteractiveAt` override = 2、`GetWindowState` override = 3、BOM 全绿）；测试用例 183 → **188**（新增 `CaptionBarTests.cpp` 5 用例）。用户侧 A1–A5（编译 / 四工具链 / 手测）待执行。
- v1.1（2026-09-13）**外部评审「通过需求评审，可进入初步设计」——D0–D8 建议与本文倾向全部一致 + 评审追问 2 项已落**：
  - **D9 新增「可交互」判定来源（评审 §R2 追问——本阶段最重要接口边界）**：评审指出「`HitTest` 命中 ≠ 应返回 `HTCLIENT`」——CaptionBar 的**标题 Label 会被命中**，但拖标题文字**必须仍能移动窗口**。**修订**：① §2 补第三条硬骨头（命中语义）；② R2 重写为「命中**可交互控件**才 `HTCLIENT`」+ 四行反例表；③ 新增 **D9**（A 控件自声明（倾向，非纯虚默认 false ⇒ 零破坏）/ B CaptionBar 自答 / C 类型白名单），并写明**不复用 `CanFocus`**（键盘焦点语义，复用会让两个概念互相绑架）。
  - **D7 由「解耦倾向」升级为「职责二分定案」（评审 §D7 追问）**——新增 §4.1：`captionHeight` = **系统标题栏行为区域**；CaptionBar 高度 = **可交互实体范围**；三层判定顺序锁死（resize > 可交互控件 > captionHeight 内未命中 → `HTCAPTION` > 超界 → `HTCLIENT`）；两个推论写死（Bar 外条带仍是拖拽区 / Bar 高于 captionHeight 的部分不参与拖拽）；不联动 + 建议同值。
  - **D0 保留（评审认可）**：认可提前解锁，同时**保留「满足原门槛立法意图、而非字面条件」的记录**（§1.2 写法获评审明确肯定）。
  - **测试与影响面补强**：§6 新增「拖拽回退」用例组作为 D9 回归锚；§7 补「D9 选 A」成本（`Widget.h` +1 非纯虚 ⇒ **无破化、无替身同步**）。
  - **评审确认不变**：Phase 12→13 切分、D1 归属 A、D2 契约 A（避免「Widget 树真相 + 平台矩形真相」双状态）、D3 Window 域、D4 矢量、D5 StyleOverride、D6 查询 API、D8 关闭请求（避免系统 X 可拦截而自绘 X 绕过拦截的 API 分裂）。
- v1.0（2026-09-13）**需求确认初稿**：
  - 背景（R5 推迟原文 + 解锁条件复核）· 技术路线（普通 Widget + NCHITTEST 委托，不换 Phase 12 路线）· R1–R8（Widget / 命中委托 / 状态查询 / close 语义 / 视觉态 / 布局 / captionHeight 关系 / 回归底线）· **决策点 D0–D8 全部给倾向待拍板**（D0 解锁条件 / D1 归属形态 / D2 委托契约 / D3 头域 / D4 矢量图形 / D5 样式深度 / D6 状态查询 / D7 高度解耦 / D8 close 走关闭请求）· 非目标（Snap Layouts 等六项圈定）· 测试方向（命中委托新 T 系 + T0–T7a 零回归）· 影响面（85→86 + Host 虚方法 + 替身同步预先列入）。
  - **需求阶段边界自律**（skill 条 6）：本文档不含头文件草案与方法签名——全部归初步设计。
