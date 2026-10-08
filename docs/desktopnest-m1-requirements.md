# DesktopNest M1 需求稿（观感里程碑）v1.0

> 阶段：**需求（待评审）**｜日期：2026-10-08
> 编号体系：**沿 `desktopnest-roadmap.md` M1/M2/M3/M4 里程碑编号，不占 Phase 号**（应用侧 `examples/DesktopNest`，框架公共 API 预期 +0）——2026-10-08 用户拍板
> 上游：`docs/desktopnest-roadmap.md` v1.17（§1 需求收敛 / §1.2 合并框模型 / §6 里程碑 / §9 待决问题）
> ★ **§9 四问本轮拍板（2026-10-08 用户）**：§9-4 菜单 = **自绘**（G-5 就地消解，框架零立项）· §9-5 框 = **可拖动、不可缩放**（G-6 M1 部分就地消解）· §9-7 应用名 = **DesktopNest**（占位名转正）· 文档编号 = **M1 体系不占 Phase 号**

---

## 1. 目标与非目标

### 1.1 M1 目标：观感验证

在桌面上立起 **DesktopNest 框**的完整交互观感——On Desktop 档常驻、二态折叠/展开、成员列表浮层（切成员/收编/拆出）、框拖动。**数据用假数据**（内存模型，无真实文件操作），零数据风险。

一句话验收：**用户在桌面上看到一个可拖动的圆角框，能折叠它、能弹出成员列表、能收编/拆出/切换成员，Win+D 后依然在，行为与 §1.2 模型一致。**

### 1.2 非目标（本轮明确不做）

| 项 | 去向 |
|---|---|
| 真实文件列表 / 图标提取 / 双击打开 / 拖入落盘 | M2（拖入落盘协议） |
| 文件移动、同名冲突、回收站、还原回桌面、卸载保护 | M3（数据安全，**最高风险**） |
| 托盘交互、多屏+DPI 位置持久化、`ReadDirectoryChangesW` | M4（常驻） |
| 原生 shell 菜单（`CreatePopupMenu` / `IContextMenu`） | 本轮 §9-4 已拍板**自绘**；原生路线若 M2+ 需要，届时另议（须框架能力） |
| 框缩放 | §9-5 已拍板**不可缩放** |
| 控件间拖放（框里一项拖进另一个框） | G-6 全量语义，M2+ 视需求 |
| 富文本 / 动画原语 / 阴影（#53 / #54） | 记账维持 |

---

## 2. 范围与能力映射

### 2.1 框架侧前置——全部就绪，**框架 Phase = 0**

| 能力 | 证据 | 状态 |
|---|---|---|
| On Desktop 档常驻 | `WindowLayer::Desktop` + `Window::SetWindowLayer`（Phase 16；E 路线六判据全过 = roadmap §7.1） | ✅ |
| 无边框 + 自绘标题条 | `ChromeMode`（Phase 12）+ `CaptionBar`（Phase 13） | ✅ |
| 圆角 / 布局 / 动画 / 主题 | `DrawRoundedRect` + V/H·List 布局（Phase 25 C-VIS）+ `AnimationManager` | ✅ |
| **拖动手势（按键状态）** | ★★ **`MouseEvent::IsButtonDown()`（`MouseEvent.h:61`，Phase 19）**——`MouseMoveEvent` **已携带** `pressedButtons`；`Window::SetCaptureWidget`（`Window.h:187`）做拖出窗口续收 | ✅ |
| **浮层（成员列表）** | **自绘路线**（§9-4 拍板）：z 序天然（`AddChild` 顺序）+ `HitTest` 逆序 + `SetCaptureWidget` 点外关闭 | ✅（应用侧组装） |

★★ **勘误（G-6 证据过期）**：`desktopnest-roadmap.md` §5 G-6 行与 §3 勘察表所记「`MouseMoveEvent` 连按键状态都没有（= 缺陷 D-1 / #41）」**已过期**——Phase 19 已为 `MouseEvent` 基类补齐 `pressedButtons` 维度 + `IsButtonDown()` 语义接口（构造注释明示「Phase 19：另携带……两维」）。**M1 拖动手势零框架缺口**。#41 的原缺陷登记（当时属实）维持历史状态不变；本勘误只修正 roadmap 对现状的描述。**须同步回写 roadmap v1.18**（见 §6）。

⚠️ **沿用 roadmap G-5 已记的坑**：浮层容器**不得用 `Panel`**（`Panel::ContainsPoint()` 恒 false ⇒ 点浮层空白会穿透 HitTest 到下层），须用裸 `Widget` 或让可交互部件当子。

### 2.2 应用侧新增（`examples/DesktopNest/`）

| 件 | 内容 |
|---|---|
| `DesktopNest.cpp/.h` | 框（Box）与浮层（MemberListPopup）的组装：二态、成员列表、拖动、收编/拆出/切换 |
| `main.cpp` | 窗口创建（`ChromeMode::Borderless` + `SetWindowLayer(Desktop)`）+ 假数据装配（2~3 个框，其中一个含 2 成员的合并框） |
| `CMakeLists.txt` | 沿 ModelProbe 模板（`target_link_libraries(... ECDI)` + UNICODE + MinGW `-static`）；**根 CMake 追加 1 行 `add_subdirectory`**（应用侧 CMake 影响面 = +1 行，框架 CMake 0） |
| 假数据模型 | 内存结构：`BoxId → {title, state, placement, members[], active}`（§8 草案字段子集：id/title/state/members/active；**placement 持久化不做**——M1 不落盘） |

---

## 3. 用户故事与行为规格（K1–K7 带锚点）

### K1 框的常驻观感
- 无边框圆角窗（`ChromeMode::Borderless` + `DrawRoundedRect` 背景），标题条自绘（`CaptionBar` 或自绘条——实施期按 Phase 13 复用成本裁决，倾向复用）。
- `SetWindowLayer(WindowLayer::Desktop)` ⇒ 被应用窗口覆盖、Win+D 后仍可见（Phase 16 已验证，M1 直接消费）。
- 尺寸默认 220×320（roadmap §8.1 示例值），不随成员切换改变（§1.2 默认 2 冻结）。

### K2 二态折叠/展开
- 右侧按钮 = 唯一二态开关：展开（标题条+内容）/ 折叠（仅标题条）。折叠只改几何高度，内容子树可见性同步（**T30-7 已钉住的框架行为**——`CollapsiblePanel` 路线直接复用，或自组装时按同语义）。
- 切换动画走 `AnimationManager`（缓动可选，时长 ≤200ms，可关）。

### K3 成员列表浮层（自绘，§9-4 拍板）
- **入口**：标题条**左侧**按钮点击弹出；浮层**不改变框尺寸**（§1.2 已定）。
- **两分区**：①「显示哪个成员」单选（勾选即切换，标题+内容整体换成该成员）；②「成员管理」=「拆出「<当前成员>」」（当前成员≠自身时可用）+「加入成员…」（列出桌面上其他**独立**框，选中即被本框收编）。
- **浮层实现纪律**：裸 `Widget` 容器（G-5 坑）；点浮层外关闭（`SetCaptureWidget` 路线）；浮层 z 序 = 最后 `AddChild`。
- **合并框视觉标识**：左侧按钮加成员数徽标（如 `3`，§1.2 默认 4）。

### K4 收编 / 拆出 / 切换（纯视图，零文件操作）
- **收编**：把对方 id 追加进 `members`；被收编框窗口消失，其 placement/state 保留（内存模型）。
- **拆出**：从 `members` 移除 → 重新成为独立窗口；位置优先恢复被收编前 placement，无记忆或冲突时**一律级联偏移**（§9-6 本轮简化拍板：**不判冲突、一律级联**——M1 假数据阶段不值得做重叠判定）。
- **切换成员**：`active` 指向换人；框尺寸不变、内容滚动（M1 内容用假列表，滚动行为可后置到内容超出时才验）。
- 合并语义 = 纯视图（roadmap §8.4 冻结）：**不触碰任何 storage 概念**。

### K5 框拖动（可拖动、不可缩放，§9-5 拍板）
- 标题条按下拖动整框（`OnMouseButtonDown` 记起点 + `IsButtonDown(Left)` 续拖 + `SetCaptureWidget` 保证拖出窗口不失）。
- 拖动中不做对齐吸附；松手即落（位置仅在内存，不落盘——落盘是 M4）。
- 不可缩放：无边框边缘不做 resize 命中；`WM_NCHITTEST` 相关门类（Phase 12 已有）不需要——M1 直接给整框禁用缩放语义（消费 `ChromeMode` 既有契约，无框架改动）。

### K6 假数据装配
- 启动即建 3 个框：A（普通，工作文档）、B（普通，学习资料）、C（合并框，收编了 C2；或运行期用「加入成员」从 A/B 现场收编一次以演示）——精确装配表实施期定，验收以「能演示收编→拆出→切换全回路」为准。
- 多开（同进程多窗）：DesktopNest 框 = 每框一个顶层 `Window`（`Desktop` 档多窗口结构上支持——Phase 16 `DesktopLayerTests` T16-4 双窗口用例在案）。

### K7 应用与工程
- 应用名 **DesktopNest**（§9-7 拍板转正）；落 `examples/DesktopNest/`；目标名 `desktopnest`。
- 框架侧改动 = **0**（公共 API +0 / 框架 CMake 0）；应用侧 = 新目录 4 文件 + 根 CMake +1 行 `add_subdirectory(examples/DesktopNest)`。
- 主屏缩放≠100% 时观感正常（DIP 契约随框架，无应用侧特殊处理；不做多屏 M1 验收——多屏是 M4）。

---

## 4. 验收标准（A1–A5）

- **A1 观感主回路（人工）**：桌面常驻（Win+D 可见、被应用覆盖）、圆角、折叠/展开、浮层弹出、收编→拆出→切换全回路演示成功；框拖动顺滑、拖出窗口边缘不丢（capture 生效）。
- **A2 模型一致性（自动）**：收编/拆出/切换后内存模型状态断言（`members`/`active` 与 UI 一致；被收编框窗口销毁、拆出重建 placement 恢复正确、无记忆时级联偏移）。
- **A3 浮层纪律（自动）**：点浮层空白**不穿透**（裸 Widget 容器纪律的回归锚）；点外部关闭；浮层弹出期间主框尺寸逐位不变。
- **A4 拖动语义（自动+人工）**：`IsButtonDown` 门控（无按键时 Move 不触发拖动——Phase 19 语义的消费锚）；拖动后框位置与鼠标位移一致（DIP 精度）。
- **A5 既有全绿**：框架测试套件 356/356 不动（零框架改动 ⇒ 计数不变）；doc_lint 无新增缺陷。

---

## 5. 影响面与风险

| 维度 | 值 |
|---|---|
| 框架公共头 / API | **0 / 0**（M1 全部消费既有能力） |
| 框架 CMake | 0；应用侧 +1 行 `add_subdirectory` |
| 测试 | 新增 DesktopNest 模型级用例 ~4–6 条（A2/A3/A4 自动部分；沿 ModelProbeTests「demo 实现链进测试」先例）——精确数实施期定 |
| 文档 | 本稿 + roadmap v1.18 回写（§6）+ docs/README/根 README 行（M1 落盘时同步） |
| 风险 | **低**——零文件操作、零框架改动、全部消费已验证能力；最大不确定点 = CaptionBar 复用 vs 自绘（实施期裁决，两者都无框架改动） |

**风险继承**：R-3（框内图标功能阻断）结构性存在，M1 即按「框倾向贴边/贴角」摆放假数据；R-4（z 序被打断）框架侧 `WM_WINDOWPOSCHANGING` 持续强制已覆盖。

---

## 6. 台账联动（本稿落盘时）

1. `desktopnest-roadmap.md` → **v1.18**：§9 表 4/5/7 三行标拍板结果 + §9-6 简化裁决（一律级联）+ §5 G-5/G-6 行标「M1 就地消解（自绘/IsButtonDown）」+ **G-6 证据勘误**（Phase 19 已补按键维度）+ §6 M1 行标「✅ 需求稿 v1.0」。
2. `docs/README.md`：文档计数 170→171 + M1 需求稿行。
3. 根 `README.md`：Project layout 或 Status 注一句应用侧 M1 启动（不动 Phase 表）。
4. memory 同步。

---

## 7. 修订记录

- v1.0（2026-10-08）初稿。§9 四问拍板（自绘 / 可拖动不可缩放 / DesktopNest / M1 体系）+ **G-6 证据勘误**（Phase 19 `IsButtonDown` 已在——roadmap G-6 行「无按键状态」过期）+ §9-6 简化（拆出落点一律级联，不判重叠）+ K1–K7 / A1–A5 / 影响面（框架 0 改动）。
