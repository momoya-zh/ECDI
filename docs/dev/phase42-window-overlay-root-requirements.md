# Phase 42 · Window 级浮层根（#81 Overlay Root + Light Dismiss）· 需求稿

> 状态：**v1.1 已吸收审核意见 / 闸门已裁决**（2026-10-30）。上游 = `roadmap-deferred.md` v2.67 的 **#81**（原账本顺位 ④，账本记「等第二消费者」判据已满足，待做区顺位待下次选型重估）。
> ★ **审核吸收**：v1.0 经 DeepSeek（WorkBuddy）对码亲核，5 项 P0 + 5 项 P1 + 6 项 P2 全部成立并完整吸收入本稿（含 5 处「既有单根假设」清算、锚点跟随本期落地、P0-5 构造期 UB 雷区前置防御）。
> ★ **闸门裁决**：用户于 2026-10-30 裁定三大闸门取 **A / A / A**（双根 + 便捷管理 / 三级枚举 / 独立头 `OverlayTypes.h`），附加裁定「**锚点跟随本期做**」。
> ★ 全部行号已对 HEAD `c863575` 亲核；基线用例 **556 / 556**，公共头 **100**。

**红线（三条，任一破了本 Phase 即失败）**

1. **主内容树（`m_rootWidget`）既有布局与渲染 O(1) 零退化**：零浮层存在时，`PaintFrame`、`HitTest`、布局结算与事件派发仅增加 O(1) 指针判空，零逐帧动态分配，零可见帧时变化；主树原有几何与渲染行为 100% 逐位不变。
2. **裁剪隔离与零逃逸**：浮层绝对**不继承**触发它的锚点控件（或祖先容器如 `ScrollView`）的 `PushClip` 裁剪栈；浮层内部绘制与交互完全逃逸出祖先边界，但仍严格受限于宿主窗口客户区（Window Client Bounds）。
3. **零业务代码的 Light Dismiss（点外关闭）与焦点恢复**：点击浮层外部时自动关闭浮层、按 ESC 自动关闭顶层浮层；关闭后焦点自动安全恢复至打开前的控件，业务侧零手写上溯递归。

---

## §1 输入与问题陈述

### 1.1 账本原文（#81，roadmap v2.67）

> **Window 级 Overlay / 浮层根缺失**——浮层只能挂在普通子树里 ⇒ 落在 `ScrollView` 等裁剪容器内必被 `PushClip` 裁断；z-order = 添加序、无运行期可调 API；「点击浮层外关闭」无框架契约，每个业务窗口自己写命中上溯。

账本将其定性为「**缺失能力**」。在 v2.66 / v2.67 中明确记录：
- 消费证据已全面爆发：仓内已有 `DesktopNest` 1 处，外部真实工程（`ECDI_fake_harness`）已有 6 处浮层消费者；
- 外部工程为了绕开该缺口，**逐位复刻了 `VerticalLayout(0, true)` 写成 `HarnessRootLayout`** 伪造浮层根与豁免集；并在运行时动态 `RemoveChild` 转移挂载。

### 1.2 为什么必须在 Window 级解决（五大架构冲突）

浮层（Popup、Dropdown、ContextMenu、Tooltip、Modal Dialog、Drawer Sheet）在现代 GUI 中具有与普通流动内容控件根本不同的物理特性：

| # | 架构冲突 | 现状表现与机理（已亲核） | 为什么子树内无法自愈 |
|---|---|---|---|
| **①** | **祖先裁剪栈强行截断** | `Widget::Paint` 对每个节点执行 `ctx.PushClip(self)`（`Widget.cpp:610`）；`ScrollView` 对视口区域强制入栈。挂在子树内的下拉框/Tooltip 只要超出视口边界，**必然被底层 GDI/GPU 裁切栈强行截断** | 后端裁剪栈是严格压栈语义，子节点没有任何 API 能跳出祖先的 ClipStack |
| **②** | **流动容器强行挤压** | 普通布局（如 `VerticalLayout`）全量遍历并统计可见子控件尺寸（`fixedTotal += child->GetHeight()`）。全窗浮层（如遮罩、全屏 Sheet）一旦 `SetVisible(true)`，`remaining` 算式立刻被挤压归零，**导致主界面内容区瞬间塌陷为 0×0** | 布局算法对所有子一视同仁，无法区分「内容」与「悬浮脱模层」 |
| **③** | **z-order 只能靠 DOM 添加序** | `Window` 仅有 `m_rootWidget` 一个根；`Widget::Paint` 严格按 `m_children` 正序绘制，`HitTest` 按逆序命中（`Widget.cpp:337`）。浮层要处于顶层，**必须永远处于 children 的最后一位** | 只要业务后续有动态 `AddChild`（如加载列表项、动态通知栏），浮层就会被当场盖住 |
| **④** | **Light Dismiss 契约缺失** | 点外收起依赖业务在顶层节点自己重载 `OnMouseButtonDown`，自己用 `while(node = node->GetParent())` 上溯判断落点是否在浮层内 | 若底层兄弟控件调用了 `event.Consume()` 拦截冒泡，祖先根本收不到 Down 事件，点外收起当场失效 |
| **⑤** | **焦点流转与恢复孤岛** | 浮层内的输入框（如搜索下拉、模态输入框）获得焦点后，用户按 Tab 键会直接穿透导航到底层主窗口控件；浮层关闭后，底层控件无法恢复先前焦点，焦点彻底丢失 | 焦点系统目前只遍历 `m_rootWidget` 单一前序链，缺乏上下文堆栈 |

### 1.3 真实消费者证据矩阵（仓内 1 处 + 消费工程 6 处真实代码）

| 场景编号 | 真实业务站点 | 真实代码事实与现行妥协 |
|---|---|---|
| **S1（仓内）** | `examples/DesktopNest/BoxView.cpp:56-91` | `BoxRoot::OnMouseButtonDown` 手写点外关闭判据：因 `Widget::Contains` 是 private（`Widget.h:452`，历史注记 `:280`），手写 6 行 `for (Widget* node = hit; ...)` 上溯；且必须手写 `hit == m_entryButton` 豁免防止开关冲突 |
| **S2（仓内）** | `examples/DesktopNest/BoxView.cpp:213-220` | `m_popup` 必须作为 `BoxRoot` 的**最后一个 `AddChild`**；尺寸与绝对坐标 `Metrics::kCaptionHeight` 全部手算硬编码，无法自适应 |
| **S3（工程）** | `ECDI_fake_harness/gui/src/AppUiChrome.cpp:58-118` | **逐位复刻** `VerticalLayout(0, true)` 为 `HarnessRootLayout`，新造 `Exempt(Widget*)` 豁免集，只为让遮罩与 Sheet 不挤压主区 |
| **S4（工程）** | `ECDI_fake_harness/gui/src/AppUiChrome.cpp:155-172` | 运行时动态挪动子树：`root.AddChild(m_scroll->RemoveChild(m_hud.settingsMask))`，从滚动容器强行拆解挂到 root 末尾以逃逸裁剪 |
| **S5（工程）** | `ECDI_fake_harness/gui/src/AppHud.cpp:456-497` | Tooltip 挂在 `m_scroll` 内部，因顶栏按钮在视口上方，Tooltip 负 y 坐标会被直接裁切，业务被迫手写 `clamp(y, 0, ...)` 强行贴顶扭曲位置 |
| **S6（工程）** | `ECDI_fake_harness/App.h:246` | 模型下拉选择浮窗（Model Selector Dropdown），需要悬浮于侧栏与转录区之上 |
| **S7（工程）** | 预览遮罩与 Panel（Preview Overlay） | 预览图弹层需要模态置顶，点外阻断或关闭 |

---

## §2 方案选型与核心机理

### 2.1 候选方案对比

| 方案 | 架构形态 | 优点 | 致命缺陷 / 否决理由 | 结论 |
|---|---|---|---|---|
| **方案 A**（采纳） | **Window 级轻量 Overlay 系统**（Window 持有独立浮层根容器 + 分层栈 + 统一交互管线） | 纯 C++ 框架层自绘自管理；坐标系统统一（DIP）；零系统窗口负担；完美复用 Phase 41 结算管线 | 需要严密处理双根下的 HitTest、Paint、Focus、Hover、Host 契约 | **✅ 采纳**（最合 ECDI 轻量自绘定位） |
| **方案 B** | **独立顶级 Win32 平台窗口**（`WS_POPUP`） | 天然不受父窗口边界和裁剪限制 | 平台窗口开销大；跨屏 DPI 换算断层；焦点激活状态撕裂（主窗口标题栏失焦变灰）；跨平台不可移植；与 `window-ownership` 深度纠缠 | **❌ 坚决否决** |
| **方案 C** | **仅改造主布局 Layout（引入布局豁免属性）** | 生产代码动量极小 | **完全解不掉祖先 `PushClip` 裁剪破坏**；依然需要依赖末位 DOM 序保证 z-order；Light Dismiss 依然散落各处 | **❌ 否决**（扬汤止沸） |

### 2.2 方案 A 的核心运行拓扑

```
        ┌─────────────────── Window (客户区 0,0 - W,H) ───────────────────┐
        │                                                                  │
        │  【主内容树 Layer 0】               【浮层树 Layer 1 (Overlay)】 │
        │   m_rootWidget                      m_overlayRoot (Panel 语义)   │
        │   ┌─────────────────────────┐       ┌────────────────────────┐   │
        │   │  CaptionBar             │       │  [Tooltip 层 (非命中)] │   │
        │   │  MainView               │       │  [Modal 层 (含遮罩)]   │   │
        │   │  ┌───────────────────┐  │       │  [Normal 层 (下拉/菜单)│   │
        │   │  │ ScrollView (Clip) │  │       └────────────────────────┘   │
        │   │  │  [AnchorButton]───┼──┼────────────────────┐               │
        │   │  └───────────────────┘  │                    │ 锚点映射      │
        │   └─────────────────────────┘                    ▼ (绝对坐标)    │
        │                                             [Popup Content]      │
        └──────────────────────────────────────────────────────────────────┘
```

1. **渲染管线（`Window::PaintFrame`）**：
   - 帧首结算：先结算主内容根 `m_rootWidget->SettleLayoutIfDirty()`，再结算浮层根 `m_overlayRoot->SettleLayoutIfDirty()`；
   - 绘制流编排：`m_rootWidget->Paint(ctx, 0, 0)` 先画完整主界面；随后在**干净的初始客户区裁切栈**下调用 `m_overlayRoot->Paint(ctx, 0, 0)`。浮层天然覆盖于主界面上方，且**绝不可能被主界面内部的任何局部 `PushClip` 截断**。
2. **窗口级命中统一入口（`HitTestWindow` 架构）**：
   - 将 `Application::FindTargetWidget`、`Window::IsClientInteractiveAt`、`Window::OnQueryCursor` 三处命中查询收敛至统一私有助手 `HitTestWindow(x, y)`；
   - 优先探测 `m_overlayRoot->HitTest(x, y)`：若命中浮层子节点，直接返回浮层目标；
   - 仅当浮层未命中（且未被模态遮罩阻断）时，才回落至 `m_rootWidget->HitTest(x, y)`。
3. **坐标映射与锚点跟随**：
   - 借助已有的 `Widget::GetAbsolutePosition()`，锚点控件向窗口客户区投影绝对坐标；
   - 浮层由框架在布局结算后按绝对坐标自动摆位并执行边缘防溢出翻转（Flip-then-Clamp）。

---

## §3 核心设计决策（D42-1 .. D42-9）

### D42-1 · `m_overlayRoot` 的基类与容器语义冻结（吸收 P0-1）

1. **基类类型冻结为 `Panel`**（或必须覆写 `ContainsPoint` 返回 `false`）：
   - `Widget::HitTest`（`Widget.cpp:337-382`）算法为「子节点全未命中则检测自身 `ContainsPoint`」。若 `m_overlayRoot` 默认命中自身，会导致全窗空白区恒定命中 `m_overlayRoot`，**导致主内容树永久失去所有鼠标输入**；
   - `Panel` 明确 `ContainsPoint` 恒 `false`（`Panel.h:47-50`），背景默认完全透明（`DefaultTheme.cpp:52-55`），完美充当非命中隐形根容器。
2. **根可见性守卫**：
   - `m_overlayRoot` 恒保持 `IsVisible() == true`，**严禁用 `SetVisible(false)` 表达「无浮层」**（防止其子树无法接收失效或意外跳过）。
3. **模态独占阻断归属澄清**：
   - 模态阻断必须由业务/便捷接口铺设一张全客户区尺寸的遮罩控件（Mask）承担；框架保证遮罩及其上层优先命中，`m_overlayRoot` 自身不承担空白处命中阻断。

### D42-2 · 浮层管理 API 与所有权契约（吸收 P2-4 / 闸门 1-A 裁决）

`Window` 提供轻量便捷管理接口，契约三项冻结：
1. **所有权形态**：**移交式所有权（`std::unique_ptr<Widget>`）**。
   - `Window` 的 `m_overlayRoot` 拥有浮层 Widget 的生命周期；
   - 浮层关闭时自动从树上移除并安全延迟析构。
2. **返回值契约**：返回 `bool`（成功打开返回 true，若已有同层互斥浮层或参数非法返回 false），不引入重量级句柄类型，遵循 YAGNI。
3. **关闭回调契约**：在 `OverlayOptions` 中提供 `std::function<void()> onClosed`，浮层关闭并脱树后触发。
4. **低层原语并存**：公开 `Widget& GetOverlayRoot() noexcept;`，高级消费者或自定义容器可直接对其操作。

### D42-3 · 多浮层三级分层系统（吸收 P1-3 / 闸门 2-A 裁决）

引入 `OverlayLayer` 三级枚举（定义于 `ECDI/Window/OverlayTypes.h`）：
```cpp
enum class OverlayLayer {
    Normal = 0,  // 普通浮层（下拉选择、气泡弹窗、右键菜单）
    Modal = 1,   // 模态浮层（全屏半透明遮罩、居中对话框、设置抽屉）
    Tooltip = 2  // 悬浮提示层（恒定最高置顶，不被下拉或弹窗遮挡）
};
```
- **层间次序**：严格满足 `Tooltip > Modal > Normal > Content`；
- **层内次序**：同一层内按添加序（后加居上）；
- **Tooltip 层命中契约（P1-3 强约束）**：
  Tooltip 层浮层默认 `ConsumeMouseInput == false` 且 `ContainsPoint == false`（纯视觉呈现），**绝对不抢夺鼠标事件，不参与 Light Dismiss 判定**，避免击穿模态阻断。

### D42-4 · Light Dismiss、嵌套与级联关闭（吸收 P1-2）

1. **判定条件**：
   鼠标按下时，若命中点不属于当前活动浮层子树，且不属于 `OverlayOptions::exemptWidget`（触发锚点按钮），判定为点外点击。
2. **穿透策略（`LightDismissPolicy`）**：
   - `PassThrough`（默认）：关闭当前浮层，同时该鼠标事件继续向下传递派发给实际命中控件；
   - `Consume`：关闭当前浮层，该鼠标事件被消费阻断，不向下穿透；
   - `None`：不启用点外关闭（强模态对话框）。
3. **嵌套与级联关闭契约（P1-2 补全）**：
   - **所有者链（Owner Chain）**：若浮层 A 打开期间在其内部控件回调中打开了浮层 B，B 的逻辑 parent 记为 A；
   - **顶层定义**：最高非空层中最后挂载的浮层为活动顶层；
   - **关闭规则**：点外关闭严格遵循 **LIFO（后开先关）**；父浮层关闭时，其下所有子浮层**自动级联关闭**。
4. **ESC 键规则**：
   顶层浮层若 `closeOnEsc == true`（默认 true），`KeyDownEvent(KeyCode::Escape)` 优先拦截并关闭最顶层浮层。

### D42-5 · 焦点捕获（Focus Trap）与恢复（Focus Restore）

1. **打开落焦**：若浮层内含有 `CanFocus()` 控件，自动转焦并记录前置焦点指针 `m_previousFocusedWidget`；
2. **关闭回焦**：浮层关闭时，若 `m_previousFocusedWidget` 依然在树且 `IsEnabled()`，自动恢复焦点，否则安全清空；
3. **Focus Trap（模态焦点环）**：在存在 `Modal` 浮层时，`Window::FocusNext` 收集的焦点候选集自动收窄至该模态浮层子树内，Tab 键完全在该子树内部循环，严禁跳到底层主窗口。

### D42-6 · 成员声明位置与 Phase 20 崩溃防御（吸收 P0-5）

- **成员声明顺序**：
  在 `Window.h` 中，`m_overlayRoot` **必须声明在 `m_platformWindow` 之前**（紧随 `m_rootWidget` 之后）：
  ```cpp
  std::unique_ptr<Widget> m_rootWidget;
  std::unique_ptr<Widget> m_overlayRoot;   // ★ 必须早于 m_platformWindow
  std::unique_ptr<PlatformWindow> m_platformWindow;
  ```
  **硬先例（Phase 20 / 0xC000041D）**：`PlatformWindow` 构造期 Windows 同步派发 `WM_SIZE` 回调直达 `Window::OnResized`。若声明在后，回调访问未构造的 `m_overlayRoot` 必触发 UB 崩溃；
- **判空守卫**：`Window::OnResized` 与 `PaintFrame` 对 `m_overlayRoot` 的访问必须严格判空。

### D42-7 · 派发链断言与调用点修正（吸收 P1-4）

`Application.cpp:230` 现有断言保持不变：
```cpp
FRAMEWORK_ASSERT(root == nullptr || chain.back() == root);
```
**拒绝放宽断言为永真**。将 `Application.cpp` 中 9 处调用点（`:378 / 405 / 437 / 478 / 505 / 531 / 571 / 600 / 650`）硬编码的 `&window.GetRootWidget()` 统一改为：
```cpp
target->GetRootNode()
```
利用 Phase 41 引入的 `Widget::GetRootNode()` 动态解析实际根（内容根或浮层根），既消除断言失败，又完整保住 Debug 断言的判别力。

### D42-8 · 既有「单根假设」清算表（吸收 P0-2 / P0-3 / P0-4）

框架现存 5 处硬编码单根逻辑，必须在本 Phase 统一清算升级：

| 站点编号 | 源码位置 | 现状单根代码 | 升级为多根/任一 UI 根的方案 | 消除的严重隐患 |
|---|---|---|---|---|
| **S-ROOT-1** | `Window.cpp:661-684` (`IsWidgetInTree`) | `current == m_rootWidget.get()` | `current == m_rootWidget.get() \|\| current == m_overlayRoot.get()` | 消除浮层输入框无法获得焦点的问题（P0-2） |
| **S-ROOT-2** | `Window.cpp:907` (`FocusNext`) | `CollectFocusables(m_rootWidget.get(), ...)` | 模态时收窄至当前模态浮层，非模态时若有浮层则按内容根+浮层根合并收集 | 消除浮层无法参与 Tab 导航的问题（P0-2） |
| **S-ROOT-3** | `HoverTracker.cpp:32-58` (`IsInTree`) | `current == m_treeRoot` | 增加 `SetSecondaryTreeRoot(Widget*)` 或由 Window 统一判在树 | 消除鼠标离开浮层按钮时 hover 状态常亮粘住的问题（P0-3） |
| **S-ROOT-4** | `Window.cpp:486-500` (`IsClientInteractiveAt`) | `m_rootWidget->HitTest(x,y)` | 统一调用私有 `HitTestWindow(x,y)`，先浮层后内容 | 消除 Borderless 窗口浮层区域被误当成可拖拽标题栏的问题（P0-4） |
| **S-ROOT-5** | `Window.cpp:574-588` (`OnQueryCursor`) | `m_rootWidget->HitTest(x,y)` | 统一调用私有 `HitTestWindow(x,y)`，先浮层后内容 | 消除浮层中文本/手型光标失效恒为 Arrow 的问题（P0-4） |

### D42-9 · 锚点跟随与自动重定位（吸收 P1-1 / 用户附加裁定「本期做」）

用户裁定锚点跟随本期实现，契约五条定案：
1. **配置字段**：`OverlayOptions` 包含 `const Widget* anchor = nullptr;` 与 `AnchorPlacement placement = AnchorPlacement::Below;`（`Below` / `Above` / `Left` / `Right`）；
2. **执行时机**：在 `Window::PaintFrame` 中，主树与浮层树 `SettleLayoutIfDirty` 结算完成后、`Paint` 启动前统一触发重定位；
3. **坐标映射**：调用 `anchor->GetAbsolutePosition()` 取得锚点窗口绝对坐标；
4. **防溢出翻转（Flip-then-Clamp）**：若在指定方向展开超出客户区边界，自动翻转到对侧（如 Below 超出则翻转为 Above）；若对侧依然超出，则 Clamp 贴合客户区边缘；
5. **脱树生命周期守卫**：若检测到 `anchor` 已经脱树（`!IsWidgetInTree(anchor)`），浮层自动隐蔽或关闭，防止悬浮幽灵浮层。

---

## §4 详细规范与行为契约

### 4.1 核心公共类型规范（`ECDI/Window/OverlayTypes.h`，头数 100 → 101）

```cpp
#pragma once
#include <functional>
#include "ECDI/Widget/Widget.h"

namespace ECDI {

enum class OverlayLayer {
    Normal = 0,
    Modal = 1,
    Tooltip = 2
};

enum class LightDismissPolicy {
    PassThrough = 0,
    Consume = 1,
    None = 2
};

enum class AnchorPlacement {
    Below = 0,
    Above = 1,
    Left = 2,
    Right = 3
};

struct OverlayOptions {
    OverlayLayer layer = OverlayLayer::Normal;
    LightDismissPolicy lightDismiss = LightDismissPolicy::PassThrough;
    bool closeOnEsc = true;
    const Widget* exemptWidget = nullptr;     // 触发锚点按钮豁免
    const Widget* anchorWidget = nullptr;     // 跟随锚点控件
    AnchorPlacement placement = AnchorPlacement::Below;
    std::function<void()> onClosed = nullptr; // 关闭后回调
};

} // namespace ECDI
```

### 4.2 详细行为契约清单

- **C42-GEO-1（宿主边界对齐）**：`m_overlayRoot` 在 `Window::OnResized` 时强制同步设置尺寸为 `(width, height)`。
- **C42-RND-1（独立绘制阶段）**：`Window::PaintFrame` 必须在初始客户区 Clip 下先执行 `m_rootWidget->Paint`，后执行 `m_overlayRoot->Paint`。
- **C42-RND-2（分层顺序保证）**：绘制严格遵循 `Normal → Modal → Tooltip`，层内按添加序。
- **C42-HIT-1（统一命中同源）**：`FindTargetWidget`、`IsClientInteractiveAt`、`OnQueryCursor` 统一走 `HitTestWindow`，浮层优先。
- **C42-DIS-1（Light Dismiss 触发）**：按下时落点不在顶层浮层子树且非豁免控件，执行对应穿透/拦截策略并关闭。
- **C42-ESC-1（ESC 响应）**：按 ESC 时优先关闭顶层 `closeOnEsc` 浮层。
- **C42-FOC-1（焦点恢复）**：浮层关闭后安全恢复先前焦点。
- **C42-ANC-1（锚点跟随）**：帧首布局结算后根据锚点绝对坐标与 Flip-then-Clamp 计算浮层位置。

---

## §5 影响面评估与规模预算

### 5.1 影响文件与修改点清单（共 6 个产品文件 + 2 个测试基础设施文件）

1. **`ECDI/include/ECDI/Window/OverlayTypes.h`**（新增，约 50 行，公共头 100 → 101）
2. **`ECDI/include/ECDI/Window/Window.h`**：
   - 声明 `m_overlayRoot`（位于 `m_platformWindow` 之前）；
   - 新增 API：`GetOverlayRoot()`、`ShowOverlay`、`HideOverlay`、`HasActiveOverlay`；
   - 增加私有 `HitTestWindow(int x, int y)` 声明。
3. **`ECDI/src/Window/Window.cpp`**：
   - 构造期创建 `m_overlayRoot`（Panel 语义）并设置 `SetWindow(this)`；
   - `PaintFrame`：双根帧首结算 + 锚点跟随重定位 + 双阶段 Paint；
   - `OnResized`：`m_overlayRoot` 尺寸同步（含判空守卫）；
   - `IsWidgetInTree`：改双根判定；
   - `FocusNext`：模态焦点收窄与浮层焦点合并；
   - `IsClientInteractiveAt` / `OnQueryCursor`：收敛至 `HitTestWindow`。
4. **`ECDI/include/ECDI/Widget/HoverTracker.h` / `src/Widget/HoverTracker.cpp`**：
   - 支持多根/浮层根判定，防止 Leave 丢失。
5. **`ECDI/src/Application/Application.cpp`**：
   - `FindTargetWidget` 对接窗口级命中；
   - 9 处 `BuildDispatchChain` 调用点传入 `target->GetRootNode()`；
   - 鼠标按下入口处集成 Light Dismiss 判定。
6. **`ECDI/src/Tests/RunAllTests.h` / `RunAllTests.cpp`**：
   - 手动注册新增测试套件 `RegisterOverlayTests()`。

### 5.2 规模预算表

| 维度 | 预算值 | 判定标准 |
|---|---|---|
| **公共头文件** | **100 → 101** | 新建 `OverlayTypes.h`（需同步更新 README 锚点与 doc_lint 读数） |
| **公共 API 增量** | **+4** | `GetOverlayRoot` / `ShowOverlay` / `HideOverlay` / `HasActiveOverlay` |
| **既有用例** | **556 / 556 保持 100% 全绿** | 存量零破坏 |
| **新增用例预算** | **+14 ~ +18 条** | 包含 3 条必须的负对照（§5.3） |
| **CMakeLists.txt** | **0 改动** | GLOB 自动包含 |

### 5.3 新增测试规划（含 3 条负对照）

- **T42-1（负对照）**：无浮层时主树 `Paint` 命令流与旧版本逐位完全一致（证明零浮层路径零副作用）。
- **T42-2（负对照）**：普通子树内挂在 `ScrollView` 的控件超出视口**确实被裁切**（证明既有裁剪栈未被全局破坏）。
- **T42-3（负对照）**：浮层未命中时，点击落点**确实正确回落命中主内容控件**（证明非命中 Panel 根未抢占输入）。
- **T42-4**：浮层处于 `ScrollView` 内部的锚点触发，浮层绘制**完全逃逸出视口裁剪**。
- **T42-5**：全窗模态浮层打开，主内容区 `VerticalLayout` **尺寸零挤压、零塌陷**。
- **T42-6**：Z-Order 分层验证（Tooltip 恒在 Modal 之上，Modal 恒在 Normal 之上）。
- **T42-7**：Light Dismiss 穿透策略验证（PassThrough 触发底层按钮 vs Consume 拦截）。
- **T42-8**：触发按钮豁免验证（点击 entryButton 不会发生先关后开的闪烁）。
- **T42-9**：ESC 键关闭顶层浮层。
- **T42-10**：嵌套浮层级联关闭与 LIFO 顺序。
- **T42-11**：焦点进入浮层、模态 Focus Trap 环路、关闭后先前焦点精准恢复。
- **T42-12**：锚点跟随位置计算与 Flip-then-Clamp 边界防溢出。
- **T42-13**：锚点脱树后浮层自动安全关闭。
- **T42-14**：浮层内控件 Hover 离开后正常收到 `OnMouseLeave`，状态不粘连。

---

## §6 闸门裁决落盘记录

根据 2026-10-30 用户裁决，三道闸门及附加项已全部拍板定案：

- [x] **闸门 1：API 暴露与管理形态 —— 选项 A（已拍板）**
  `Window` 同时提供 `GetOverlayRoot()` 原语与 `ShowOverlay` / `HideOverlay` 便捷生命周期管理；所有权移交式（`std::unique_ptr<Widget>`），返回 `bool`，支持 `onClosed` 回调。
- [x] **闸门 2：多浮层层叠分级系统 —— 选项 A（已拍板）**
  固定 `Normal` / `Modal` / `Tooltip` 三级枚举；Tooltip 层默认非命中。
- [x] **闸门 3：公共头文件预算 —— 选项 A（已拍板）**
  新建 `ECDI/Window/OverlayTypes.h`，公共头 100 → 101，同步更新文档规模锚点。
- [x] **附加裁定：锚点跟随（Anchor Tracking）—— 本期做（已拍板）**
  纳入 D42-9 规范，帧首自动重定位，支持 Flip-then-Clamp 与脱树守卫。

---

> 需求确认稿 v1.1 编写完成，全部审核意见吸收完毕，各项决策已全部冻结，可直接推进进入 Preliminary Design（初步设计）。
