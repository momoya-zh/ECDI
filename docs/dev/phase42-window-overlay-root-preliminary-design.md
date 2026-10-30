# Phase 42 · 初步设计 —— Window 级浮层根（#81 Overlay Root + Light Dismiss）

> 状态：**v1.1 已吸收 DeepSeek 初设审核意见**（2026-10-30）。上游 = 需求稿 **v1.1**（三大闸门 A/A/A 及锚点跟随本期做已由用户裁定拍板）。
> ★ **审核吸收（4 P0 + 8 P1 + 8 P2 全部成立并落定）**：
> 1. **P0-1 断言保真重写**：推翻恒等式修法，给 `BuildDispatchChain` 增加第二个根形参 `secondaryRoot`，断言改为双根集合判定，9 处调用点精准传参；
> 2. **P0-2 动词落点补全**：ESC 拦截落地 `Window::HandleKeyDown`、打开落焦与关闭回焦落地 `ShowOverlay` / `HideOverlay`；
> 3. **P0-3 Tooltip 纯视觉保证**：`HitTestWindow` 明确排除 `m_tooltipLayer`，Tooltip 绝不拦截鼠标、不参与点外判定；
> 4. **P0-4 判定总闸修复**：`HasAnyOverlay()` 代替单层 `HasActiveOverlay()`，彻底避免 Modal 浮层漏拦截；
> 5. **P1 算术与 API 对齐**：更正分批用例算术链（556→559→563→566→572）、API 预算锁死为需求规定的 **+4**（`HideOverlay(nullptr)` 兼任关闭顶层，取消冗余 `CloseTopOverlay` 公开方法）、补齐 `noexcept` 与 private 符号声明。
> ★ 全部行号已对 HEAD `c863575` 亲核；基线用例 **556 / 556**，公共头 **100**。

**红线（继承需求，本稿的每条设计都要对它负责）**

1. **主内容树（`m_rootWidget`）既有布局与渲染 O(1) 零退化**：零浮层存在时，`PaintFrame`、`HitTest`、布局结算与事件派发仅增加 O(1) 指针判空，零逐帧动态分配，零可见帧时变化；主树原有几何与渲染行为 100% 逐位不变。
2. **裁剪隔离与零逃逸**：浮层绝对**不继承**触发它的锚点控件（或祖先容器如 `ScrollView`）的 `PushClip` 裁剪栈；浮层内部绘制与交互完全逃逸出祖先边界，但仍严格受限于宿主窗口客户区（Window Client Bounds）。
3. **零业务代码的 Light Dismiss（点外关闭）与焦点恢复**：点击浮层外部时自动关闭浮层、按 ESC 自动关闭顶层浮层；关闭后焦点自动安全恢复至打开前的控件，业务侧零手写上溯递归。

---

## §1 核心机制与架构总装（需求必答逐项收口）

### 1.1 双根拓扑结构与分层挂载冻结（D42-1 · 吸收 P0-1 / P2-1）

`Window` 内部持有两棵并列的顶级 Widget 树，几何尺寸恒定覆盖全客户区 `(0, 0, width, height)`：

```
Window 成员拓扑
  ├── m_rootWidget (Widget 语义) ─────► 主内容树根（承载应用常规布局，由 GetRootWidget() 访问）
  └── m_overlayRoot (Panel 语义) ────► 浮层根（纯容器，ContainsPoint 恒 false，背景默认透明）
        ├── [0] m_normalLayer (Panel) ──► 普通浮层容器（下拉菜单、气泡、ContextMenu）
        ├── [1] m_modalLayer  (Panel) ──► 模态浮层容器（全屏半透明遮罩、Modal Dialog、Sheet）
        └── [2] m_tooltipLayer(Panel) ──► 提示浮层容器（Tooltip，纯视觉显示，HitTestWindow 显式跳过）
```

#### 1.1.1 容器基类类型冻结为 `Panel`

`Widget::HitTest`（[`Widget.cpp:337-382`](file:///D:/Project/ECDI/ECDI/src/Widget/Widget.cpp#L337-L382)）算法为：子节点逆序递归全未命中时，调用自身 `ContainsPoint(x, y)`。
- 若 `m_overlayRoot` 直接使用 `Widget`（其默认 `ContainsPoint` 判定矩形区域），在无浮层或点击浮层空白区时，`m_overlayRoot` 将永远命中自身，导致全窗鼠标事件被全部吞噬；
- 因此，`m_overlayRoot` 以及三级分层子容器**类型严格冻结为 `Panel`**（[`Panel.h:47-50`](file:///D:/Project/ECDI/ECDI/include/ECDI/Widget/Panel.h#L47-L50) 已明确 `ContainsPoint` 恒返回 `false`）。
- **模态独占阻断归属澄清**：`m_overlayRoot` 自身不阻断空白处。模态阻断必须由业务或 `ShowOverlay` 自动铺设的全客户区遮罩控件（Mask）承担。

#### 1.1.2 三级 Layer 挂载顺序硬性冻结（吸收 P2-1）

- 绘制正序（`m_children` 从 0 到 2）：`Normal (0) → Modal (1) → Tooltip (2)`，确保视觉上 Tooltip 恒在最顶，Modal 恒盖在 Normal 之上；
- 命中逆序（`m_children` 逆序遍历）：`Tooltip (2) → Modal (1) → Normal (0)`。结合 §1.3.1 的 Tooltip 跳过机制，命中优先进入 Modal，再进入 Normal；
- **挂载纪律**：在 `Window` 构造函数体内，一次性按 `[m_normalLayer, m_modalLayer, m_tooltipLayer]` 顺序执行三次 `AddChild`。**此后三个 layer 容器在树中位置永不调换、永不增删**。

---

### 1.2 成员声明顺序与 Phase 20 崩溃防御（D42-6 · 吸收 P0-5）

在 [`ECDI/include/ECDI/Window/Window.h:410-435`](file:///D:/Project/ECDI/ECDI/include/ECDI/Window/Window.h#L410-L435) 中，成员声明顺序具有致命的生命周期约束：

```cpp
// ── 错误顺序（必崩）：m_overlayRoot 声明在 m_platformWindow 之后 ──
// std::unique_ptr<Widget> m_rootWidget;
// std::unique_ptr<PlatformWindow> m_platformWindow;
// std::unique_ptr<Widget> m_overlayRoot; // ← 崩溃：OnResized 调用时此指针尚未构造！

// ── 正确顺序（冻结）：紧随 m_rootWidget，严禁置于 m_platformWindow 之后 ──
std::unique_ptr<Widget> m_rootWidget;     ///< 主内容根节点
std::unique_ptr<Widget> m_overlayRoot;    ///< 浮层根节点（★ 必须早于 m_platformWindow）
std::unique_ptr<PlatformWindow> m_platformWindow; ///< 平台窗口封装（构造期派发 WM_SIZE）
```

**机理与先例**：
- Windows 平台在 `CreateWindowExW` 期间会**同步**派发 `WM_SIZE`，直达 `Window::OnResized`（[`Window.cpp:835-853`](file:///D:/Project/ECDI/ECDI/src/Window/Window.cpp#L835-L853)）；
- 若 `m_overlayRoot` 声明在后，回调触发时其内存仍为未初始化状态，访问直接触发 **0xC000041D 启动崩溃**（Phase 20 真实排查血泪记录）；
- 同时，`Window::OnResized` 内部访问 `m_overlayRoot` 必须添加判空保护与同步 Arrange（吸收 P2-4）：
  ```cpp
  if (m_overlayRoot != nullptr) {
      m_overlayRoot->SetSize(width, height);
      m_overlayRoot->Arrange(); // 同步承诺：resize 后浮层几何立即可读
  }
  ```

---

### 1.3 窗口级命中同源与 Tooltip 纯视觉呈现（D42-8 · 吸收 P0-3 / P0-4）

框架内存在三处必须进行命中测试（HitTest）的窗口级核心入口。此前各自为政或硬编码 `m_rootWidget`，本 Phase 统一收敛至私有助手 `HitTestWindow(int x, int y)`。

#### 1.3.1 `HitTestWindow` 实现形态与 Tooltip 排除（吸收 P0-3）

`Tooltip` 属于纯视觉反馈层，其内部控件绝不能拦截鼠标点击或 Hover，也不能成为 Light Dismiss 的内部判定目标（否则点击 Tooltip 会抢走底层按钮点击并打穿模态）。

```cpp
Widget* Window::HitTestWindow(int x, int y) const noexcept {
    // 1. 优先探测浮层树（显式跳过 Tooltip 层，仅检测 Modal 与 Normal）
    if (m_overlayRoot != nullptr) {
        // [1] 先探测模态层
        if (m_modalLayer != nullptr && m_modalLayer->GetChildCount() > 0) {
            Widget* hit = m_modalLayer->HitTest(x, y);
            if (hit != nullptr) return hit;
        }
        // [2] 次探测普通浮层层
        if (m_normalLayer != nullptr && m_normalLayer->GetChildCount() > 0) {
            Widget* hit = m_normalLayer->HitTest(x, y);
            if (hit != nullptr) return hit;
        }
        // ★ m_tooltipLayer 故意不在此调用 HitTest：Tooltip 恒定穿透，绝不截留输入
    }

    // 2. 回落探测主内容树
    if (m_rootWidget != nullptr) {
        return m_rootWidget->HitTest(x, y);
    }
    return nullptr;
}
```

#### 1.3.2 三大消费点收敛改造（严格保持 `const noexcept` 签名 · 吸收 P1-3）

1. **`Application::FindTargetWidget`**（[`Application.cpp:333-340`](file:///D:/Project/ECDI/ECDI/src/Application/Application.cpp#L333-L340)）：
   ```cpp
   Widget* Application::FindTargetWidget(Window& window, int x, int y) const noexcept {
       return window.HitTestWindow(x, y);
   }
   ```
2. **`Window::IsClientInteractiveAt`**（[`Window.cpp:486-500`](file:///D:/Project/ECDI/ECDI/src/Window/Window.cpp#L486-L500)）：
   ```cpp
   bool Window::IsClientInteractiveAt(int x, int y) const noexcept {
       Widget* hit = HitTestWindow(x, y);
       return (hit != nullptr && hit->ConsumesMouseInput());
   }
   ```
3. **`Window::OnQueryCursor`**（[`Window.cpp:574-588`](file:///D:/Project/ECDI/ECDI/src/Window/Window.cpp#L574-L588)）：
   ```cpp
   CursorShape Window::OnQueryCursor(int x, int y) const noexcept {
       Widget* hit = HitTestWindow(x, y);
       return (hit != nullptr) ? hit->GetCursor() : CursorShape::Arrow;
   }
   ```

---

### 1.4 五处「单根假设」清算实施明细（D42-8 · 吸收 P0-2 / P2-7）

| 编号 | 现有单根代码位置 | 现有硬编码逻辑 | 本 Phase 统一清算升级实现 | 消除的硬缺陷 |
|---|---|---|---|---|
| **S-ROOT-1** | `Window.cpp:661-684`<br>`IsWidgetInTree` | `if (current == m_rootWidget.get()) return true;`<br>（且开头 `m_rootWidget == nullptr` 即返回 false） | 开头改为双根皆空才返回 false（P2-7）；<br>上溯判定匹配任一根：<br>`if (current == m_rootWidget.get() \|\| current == m_overlayRoot.get()) return true;` | 浮层内的输入框在 `SetFocusedWidget` 时不再报断言，Release 下不再被静默清空焦点 |
| **S-ROOT-2** | `Window.cpp:902-935`<br>`FocusNext` | `CollectFocusables(m_rootWidget.get(), focusables);` | 若有活动模态浮层，仅收集该模态子树（Focus Trap）；否则先收集 `m_rootWidget` 再收集 `m_overlayRoot` | 模态弹窗 Tab 键不跳出主界面，非模态浮层支持 Tab 键导航 |
| **S-ROOT-3** | `HoverTracker.cpp:32-58`<br>`IsInTree` | `if (current == m_treeRoot) return true;` | `HoverTracker` 增加 `SetSecondaryTreeRoot(m_overlayRoot.get())`，上溯同时匹配两个根 | 鼠标移出浮层内的按钮时，正常派发 `OnMouseLeave`，按钮高亮不再常亮粘连 |
| **S-ROOT-4** | `Window.cpp:486-500`<br>`IsClientInteractiveAt` | `Widget* hit = m_rootWidget->HitTest(x, y);` | 改为调用 `HitTestWindow(x, y)` | 浮层区域不再响应窗口拖拽（不会把下拉框误当成标题栏） |
| **S-ROOT-5** | `Window.cpp:574-588`<br>`OnQueryCursor` | `Widget* hit = m_rootWidget->HitTest(x, y);` | 改为调用 `HitTestWindow(x, y)` | 浮层内各类光标（I-Beam、Hand）正常根据控件自述呈现 |

---

### 1.5 派发链断言保真与 9 处调用点精准传参（D42-7 · 吸收 P0-1 / P1-4 / P1-6）

#### 1.5.1 `BuildDispatchChain` 断言保真重写

在 [`ECDI/src/Application/Application.cpp:199-233`](file:///D:/Project/ECDI/ECDI/src/Application/Application.cpp#L199-L233) 中，原实现形参增加 `const Widget* secondaryRoot = nullptr`：

```cpp
std::vector<Widget*> Application::BuildDispatchChain(Widget* target, const Widget* root, const Widget* secondaryRoot) {
    std::vector<Widget*> chain;
    if (target == nullptr) return chain;

    chain.push_back(target);
    for (Widget* node = target->GetParent(); node != nullptr; node = node->GetParent()) {
        chain.push_back(node);
    }

    // ★ 集合断言（判别力不丢：能准确抓出传入其他窗口控件的非法调用）
    FRAMEWORK_ASSERT(root == nullptr || chain.back() == root ||
                     (secondaryRoot != nullptr && chain.back() == secondaryRoot));
    return chain;
}
```

#### 1.5.2 9 处功能调用点精准替换清单（吸收 P1-6）

| 调用点行号 | 原变量名与上下文 | 精准替换为（双根传入） |
|---|---|---|
| `:378` | `captureTarget` | `BuildDispatchChain(captureTarget, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:405` | `newTarget` | `BuildDispatchChain(newTarget, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:437` | `target` | `BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:478` | `target` | `BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:505` | `target` | `BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:531` | `target` | `BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:571` | `keyboardTarget`（窗口来自 `event.GetWindow()`） | `BuildDispatchChain(keyboardTarget, &event.GetWindow()->GetRootWidget(), &event.GetWindow()->GetOverlayRoot())` |
| `:600` | `target` | `BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot())` |
| `:650` | `target` | `BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot())` |

同时将 `:360` 的注释行更新为 `// chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());`，确保 I42-4 机检 100% 成立。

---

### 1.6 渲染编排与 Phase 41 结算管线接缝（C42-RND-1 / D42-6 / P2-3）

在 [`ECDI/src/Window/Window.cpp:176-204`](file:///D:/Project/ECDI/ECDI/src/Window/Window.cpp#L176-L204) `Window::PaintFrame()` 中执行：

```cpp
void Window::PaintFrame() {
    if (m_rootWidget == nullptr) return;

    // 步骤 1：帧首双根布局结算（Phase 41 管线接缝）
    m_rootWidget->SettleLayoutIfDirty();
    if (m_overlayRoot != nullptr) {
        m_overlayRoot->SettleLayoutIfDirty();
    }

    // 步骤 2：锚点跟随自动重定位（带同值守卫防每帧置脏重排 · 吸收 P2-3）
    UpdateOverlayPositions();

    // 步骤 3：复用命令流缓冲
    m_commands.clear();
    const Size clientSize = m_platformWindow->GetClientSize();
    const Rect initialClip{ 0.0f, 0.0f, static_cast<float>(clientSize.width), static_cast<float>(clientSize.height) };

    // 步骤 4：主内容树绘制（Layer 0）
    PaintContext ctx(m_commands, *m_textMeasurer, initialClip);
    m_rootWidget->Paint(ctx, 0, 0);

    // 步骤 5：浮层树绘制（Layer 1 —— 初始全客户区裁切栈，完全隔离祖先 PushClip）
    if (m_overlayRoot != nullptr && HasVisibleOverlays()) {
        m_overlayRoot->Paint(ctx, 0, 0);
    }

    // 步骤 6：衬底与帧呈现
    SyncBackdropSurface();
    m_renderer.BeginFrame(m_backgroundColor);
    m_renderer.Execute(m_commands, GetDpiScale());
    m_renderer.EndFrame();
}
```

---

### 1.7 交互闭环：Light Dismiss、ESC、落焦与回焦（D42-4 / D42-5 · 吸收 P0-2 / P0-4）

#### 1.7.1 浮层记录项（定义于 `Window.h` private 区 · 吸收 P1-8）

```cpp
private:
    struct OverlayEntry {
        Widget* widget = nullptr;                       ///< 浮层根节点（非拥有，由对应 layer Panel 拥有）
        OverlayOptions options;                         ///< 配置选项
        Widget* ownerOverlay = nullptr;                 ///< 逻辑父浮层（支持嵌套）
        Widget* previousFocus = nullptr;                ///< 打开前持焦控件（用于关闭时恢复）
    };
    std::vector<OverlayEntry> m_overlayEntries;
```

#### 1.7.2 Light Dismiss 点击外部拦截（吸收 P0-4 总闸修复）

在 `Application::OnMouseButtonDown`（[`Application.cpp:415-445`](file:///D:/Project/ECDI/ECDI/src/Application/Application.cpp#L415-L445)）最前部执行：

```cpp
// 1. 检查当前窗口是否有任意活动浮层（调用 HasAnyOverlay，避免被单层默认参数屏蔽 · P0-4）
if (window.HasAnyOverlay()) {
    Widget* hit = window.HitTestWindow(event.GetMouseX(), event.GetMouseY());
    OverlayEntry* topOverlay = window.GetTopActiveOverlay();

    if (topOverlay != nullptr && topOverlay->options.lightDismiss != LightDismissPolicy::None) {
        bool isInsideOverlay = (hit != nullptr && window.IsWithinSubtree(hit, topOverlay->widget));
        bool isExempt = (hit != nullptr && topOverlay->options.exemptWidget != nullptr &&
                         (hit == topOverlay->options.exemptWidget ||
                          window.IsWithinSubtree(hit, topOverlay->options.exemptWidget)));

        if (!isInsideOverlay && !isExempt) {
            const LightDismissPolicy policy = topOverlay->options.lightDismiss;
            window.HideOverlay(nullptr); // 关闭当前顶层浮层

            if (policy == LightDismissPolicy::Consume) {
                return; // 拦截消费，不向下穿透
            }
        }
    }
}
```

#### 1.7.3 ESC 键拦截与落焦闭环（吸收 P0-2）

在 [`ECDI/src/Window/Window.cpp:729-746`](file:///D:/Project/ECDI/ECDI/src/Window/Window.cpp#L729-L746) `Window::HandleKeyDown` 中：

```cpp
void Window::HandleKeyDown(const KeyDownEvent& event) {
    // 1. ★ ESC 键拦截顶层浮层（同 Tab 拦截同构，插在 Tab 之前 · 吸收 P0-2）
    if (event.GetKeyCode() == KeyCode::Escape && HasAnyOverlay()) {
        OverlayEntry* top = GetTopActiveOverlay();
        if (top != nullptr && top->options.closeOnEsc) {
            HideOverlay(nullptr); // 关闭顶层浮层
            return; // 框架拦截，不派发给焦点控件
        }
    }

    // 2. Tab 键焦点导航（既有框架逻辑）
    if (event.GetKeyCode() == KeyCode::Tab) {
        FocusNext(event.IsShiftDown() ? -1 : 1);
        return;
    }

    // 3. 常规键盘派发给焦点控件
    Widget* target = m_application.FindFocusedWidget(*this);
    if (target != nullptr) {
        ...
```

#### 1.7.4 打开落焦与关闭回焦实现（吸收 P0-2 / P2-2 重入守卫）

1. **`ShowOverlay` 打开落焦**：
   ```cpp
   // 记录打开前的持有焦点指针
   entry.previousFocus = m_focusedWidget;
   // 寻找浮层内部第一个可聚焦控件
   std::vector<Widget*> focusables;
   CollectFocusables(rawWidget, focusables);
   if (!focusables.empty()) {
       SetFocusedWidget(focusables.front());
   }
   ```
2. **`HideOverlay` 关闭回焦与重入守卫（P2-2）**：
   ```cpp
   if (m_isClosingOverlay) return false; // 重入守卫
   m_isClosingOverlay = true;

   Widget* prevFocus = entry.previousFocus;
   auto onClosedCb = entry.options.onClosed;

   // 从 Layer Panel 中摘除并延迟销毁
   targetLayer->RemoveChild(entry.widget);
   m_overlayEntries.erase(it);

   // 安全回焦：前焦点仍在树上且启用，则恢复焦点
   if (prevFocus != nullptr && IsWidgetInTree(prevFocus) && prevFocus->IsEnabled()) {
       SetFocusedWidget(prevFocus);
   } else if (m_focusedWidget != nullptr && !IsWidgetInTree(m_focusedWidget)) {
       SetFocusedWidget(nullptr);
   }

   m_isClosingOverlay = false;
   if (onClosedCb) onClosedCb();
   Invalidate();
   ```

---

### 1.8 锚点跟随与自动重定位算法（D42-9 · 吸收 P1-1 / P2-3）

在 `Window::UpdateOverlayPositions()` 中执行：

```cpp
void Window::UpdateOverlayPositions() {
    if (m_overlayEntries.empty() || m_platformWindow == nullptr) return;
    const Size clientSize = m_platformWindow->GetClientSize();

    for (auto& entry : m_overlayEntries) {
        if (entry.options.anchorWidget == nullptr) continue;

        // 脱树守卫：锚点已脱树，自动关闭该浮层
        if (!IsWidgetInTree(entry.options.anchorWidget)) {
            HideOverlay(entry.widget);
            continue;
        }

        const Point apt = entry.options.anchorWidget->GetAbsolutePosition();
        const int aw = entry.options.anchorWidget->GetWidth();
        const int ah = entry.options.anchorWidget->GetHeight();
        const int ow = entry.widget->GetWidth();
        const int oh = entry.widget->GetHeight();

        int targetX = static_cast<int>(apt.x);
        int targetY = static_cast<int>(apt.y) + ah; // 默认 Below

        // Flip-then-Clamp 算法
        if (entry.options.placement == AnchorPlacement::Below) {
            if (targetY + oh > clientSize.height && static_cast<int>(apt.y) - oh >= 0) {
                targetY = static_cast<int>(apt.y) - oh; // 翻转为 Above
            }
        }
        targetX = std::clamp(targetX, 0, std::max(0, clientSize.width - ow));
        targetY = std::clamp(targetY, 0, std::max(0, clientSize.height - oh));

        // 同值守卫（吸收 P2-3，防止每帧无条件 SetPosition 造成频繁重排）
        if (entry.widget->GetX() != targetX || entry.widget->GetY() != targetY) {
            entry.widget->SetPosition(targetX, targetY);
        }
    }
}
```

---

## §2 公共 API 与头文件规范

### 2.1 新建公共头文件 `ECDI/include/ECDI/Window/OverlayTypes.h`（公共头 100 → 101）

```cpp
#pragma once

#include <functional>
#include "ECDI/Widget/Widget.h"

namespace ECDI {

/// @brief 浮层所属层级（层间顺序严格满足 Tooltip > Modal > Normal > Content）
enum class OverlayLayer {
    Normal = 0,  ///< 普通交互浮层（下拉列表、气泡弹窗、右键菜单）
    Modal = 1,   ///< 模态浮层（全屏半透明遮罩、居中对话框、设置抽屉）
    Tooltip = 2  ///< 提示浮层（纯视觉呈现，恒定置顶，默认不参与命中）
};

/// @brief 点击浮层外部时的关闭与事件分派策略
enum class LightDismissPolicy {
    PassThrough = 0, ///< 关闭浮层，且点击事件继续向下派发给底层命中控件（默认）
    Consume = 1,     ///< 关闭浮层，并拦截吞噬该点击事件，不向底层派发
    None = 2         ///< 禁用点外关闭（强模态对话框）
};

/// @brief 浮层相对锚点控件的展开方向
enum class AnchorPlacement {
    Below = 0, ///< 锚点正下方（默认）
    Above = 1, ///< 锚点正上方
    Left = 2,  ///< 锚点正左侧
    Right = 3  ///< 锚点正右侧
};

/// @brief 浮层显示配置选项
struct OverlayOptions {
    OverlayLayer layer = OverlayLayer::Normal;
    LightDismissPolicy lightDismiss = LightDismissPolicy::PassThrough;
    bool closeOnEsc = true;                   ///< 是否支持按 ESC 键关闭
    const Widget* exemptWidget = nullptr;     ///< 豁免控件（通常为触发浮层的 EntryButton）
    const Widget* anchorWidget = nullptr;     ///< 跟随的锚点控件（开启锚点跟随定位）
    AnchorPlacement placement = AnchorPlacement::Below;
    std::function<void()> onClosed = nullptr; ///< 浮层完全关闭脱树后的回调
};

} // namespace ECDI
```

### 2.2 `ECDI/include/ECDI/Window/Window.h` API 增量（严格 +4 公共方法 · 吸收 P1-2）

```cpp
public:
    /// @brief 获取浮层根容器（底阶原语，通常用于自定拓扑）
    Widget& GetOverlayRoot() noexcept;

    /// @brief 弹出浮层（移交所有权）
    /// @param overlayWidget 待弹出的浮层 Widget（所有权移交 Window）
    /// @param options 浮层配置选项
    /// @return true = 成功弹出；false = 参数非法或状态冲突
    bool ShowOverlay(std::unique_ptr<Widget> overlayWidget, const OverlayOptions& options = {});

    /// @brief 关闭指定浮层（若传入 nullptr 则关闭当前活动顶层浮层）
    /// @param overlayWidget 待关闭的浮层原始指针（nullptr = 关闭顶层）
    bool HideOverlay(Widget* overlayWidget = nullptr);

    /// @brief 是否存在活动浮层（指定层）
    bool HasActiveOverlay(OverlayLayer layer) const noexcept;

    /// @brief 是否存在任意活动浮层（全局总闸）
    bool HasAnyOverlay() const noexcept;

private:
    // 吸收 P1-7，内部符号声明齐全
    bool HasVisibleOverlays() const noexcept;
    void UpdateOverlayPositions();
    bool IsWithinSubtree(const Widget* node, const Widget* subtreeRoot) const noexcept;
    OverlayEntry* GetTopActiveOverlay() noexcept;
    Widget* HitTestWindow(int x, int y) const noexcept;

    Panel* m_normalLayer = nullptr;
    Panel* m_modalLayer = nullptr;
    Panel* m_tooltipLayer = nullptr;
    bool m_isClosingOverlay = false;

    // friend class Application; 已存在（Window.h:349，吸收 P2-8 备注）
```

### 2.3 `ECDI/include/ECDI/Widget/HoverTracker.h` 增量

```cpp
public:
    /// @brief 设置副树根（浮层根，支持双根树内校验）
    void SetSecondaryTreeRoot(Widget* secondaryRoot) noexcept { m_secondaryRoot = secondaryRoot; }

private:
    Widget* m_secondaryRoot = nullptr; ///< 副树根（浮层根，非拥有指针）
```

---

## §3 不变量与机检判据（I42）

| 判据编号 | 规则描述 | 检验命令 / 方法 | 期望值 |
|---|---|---|---|
| **I42-1** | 零浮层时，主内容命令流逐位完全一致 | 运行负对照测试 `T42-1` | **PASS 0 diff** |
| **I42-2** | `m_overlayRoot` 必须声明在 `m_platformWindow` 之前 | `grep -n "m_overlayRoot\|m_platformWindow" ECDI/include/ECDI/Window/Window.h` | `m_overlayRoot` 行号 < `m_platformWindow` 行号 |
| **I42-3** | 三处窗口命中测试严格同源 | `grep -n "HitTestWindow" ECDI/src/` | **恰 4 行（1 定义 + 3 调用：Window.cpp 2 处 + Application.cpp 1 处）**（吸收 P1-4） |
| **I42-4** | `Application.cpp` 零硬编码单根 `GetRootWidget()` 派发链 | `grep -n "BuildDispatchChain.*GetRootWidget()" ECDI/src/Application/Application.cpp` | **0 命中**（吸收 P1-5 注释修正） |
| **I42-5** | `m_overlayRoot` 构造类型继承 `Panel` | `grep -n "m_overlayRoot.*std::make_unique<Panel>" ECDI/src/Window/Window.cpp` | **1 命中** |
| **I42-6** | 公共头文件数精确增长 1 个 | `find ECDI/include -name "*.h" \| wc -l` | **100 → 101** |

---

## §4 测试体系与 16 条用例详细设计

测试实现落入 `ECDI/src/Tests/OverlayTests.cpp`，并在 `RunAllTests.cpp` 中显式注册 `RegisterOverlayTests()`。

```
用例矩阵结构
  ├── 硬核负对照组 (T42-1 .. T42-3) ──► 证明旧路径完全不被破坏，无全局静默降级
  ├── 裁剪与布局隔离组 (T42-4 .. T42-6) ► 证明 PushClip 逃逸、主布局零挤压、Z-Order 严密
  ├── Light Dismiss 与交互组 (T42-7 .. T42-10) ► 点外穿透/拦截、按钮豁免、ESC、LIFO 级联
  └── 焦点与锚点跟随组 (T42-11 .. T42-16) ► Focus Trap、焦点恢复、Flip-then-Clamp、脱树守卫、Tooltip穿透、Phase 41
```

### 4.1 详细用例清单

1. **`T42-1 (Overlay.Negative_ZeroOverlayBitIdentical)` [负对照]**：
   - 装置：标准窗口下挂复杂布局与文本。
   - 检验：在未打开任何 Overlay 的情况下，`PaintFrame` 录制的 `RenderCommand` 序列与改前逐字节完全一致。
2. **`T42-2 (Overlay.Negative_ScrollViewClipIntact)` [负对照]**：
   - 装置：在 `ScrollView` 内部普通子树挂载超出视口的普通按钮。
   - 检验：其绘制依然产生被视口截断的 `PushClip`（证明既有裁剪栈未被全局关掉）。
3. **`T42-3 (Overlay.Negative_FallbackToContentRoot)` [负对照]**：
   - 装置：仅打开一个局部尺寸的浮层。
   - 检验：点击浮层外空白区时，`HitTestWindow` 准确回落并命中主内容区对应的按钮（证明 `Panel` 根未劫持非命中点）。
4. **`T42-4 (Overlay.EscapeScrollViewClip)`**：
   - 装置：将触发锚点置于 `ScrollView` 内，弹出浮层，浮层尺寸超出 `ScrollView` 视口。
   - 检验：浮层绘制阶段的当前 Clip 等于整个 ClientRect，无任何视口裁切，完整绘制。
5. **`T42-5 (Overlay.MainLayoutNeverSquashed)`**：
   - 装置：主内容根使用 `VerticalLayout(0, true)`，弹出一个覆盖全屏的高尺寸模态遮罩与 Sheet。
   - 检验：主内容区各兄弟控件高度与位置严格不变，`remaining` 算式零影响，主区零塌陷。
6. **`T42-6 (Overlay.ZOrderHierarchy)`**：
   - 装置：依次弹出 Normal 菜单、Modal 遮罩、Tooltip 提示。
   - 检验：命中与绘制严格满足 `Tooltip > Modal > Normal`；Tooltip 即使先加也位于最顶。
7. **`T42-7 (Overlay.LightDismissPolicies)`**：
   - 装置：分别测试 `PassThrough` 与 `Consume`。
   - 检验：`PassThrough` 下点击外部按钮，浮层关闭且按钮触发 Click；`Consume` 下仅浮层关闭，按钮不触发。
8. **`T42-8 (Overlay.ExemptWidgetToggle)`**：
   - 装置：配置 `options.exemptWidget = entryBtn`。
   - 检验：点击 entryBtn 时不触发点外关闭，由按钮自身的 OnClick 权威执行 Toggle。
9. **`T42-9 (Overlay.CloseOnEscKey)`**：
   - 装置：浮层开启，派发 `KeyDown(KeyCode::Escape)`。
   - 检验：顶层浮层关闭，事件被拦截，主窗口未收到 Escape。
10. **`T42-10 (Overlay.NestedLifoAndCascadeClose)`**：
    - 装置：浮层 A 内部弹出浮层 B（A 为 B 的 owner）。
    - 检验：点 B 外部关 B、A 仍保留；点 A 外部关 A 时，B 被级联关闭，两者 `onClosed` 均被触发。
11. **`T42-11 (Overlay.FocusTrapAndRestore)`**：
    - 装置：主窗口输入框 A 持焦，弹出模态浮层（内含输入框 B 与 C）。
    - 检验：焦点自动落到 B；按 Tab 键在 B 和 C 之间循环切换（不跳到 A）；浮层关闭后焦点自动恢复给 A。
12. **`T42-12 (Overlay.AnchorFlipThenClamp)`**：
    - 装置：锚点位于窗口最底部，配置 `Placement::Below`。
    - 检验：下方空间不足，浮层自动 Flip 翻转到锚点上方展开；若两侧均超，自动 Clamp 停泊于边缘。
13. **`T42-13 (Overlay.AnchorDetachAutoClose)`**：
    - 装置：跟随锚点的浮层处于活动态，将锚点从其父控件 `RemoveChild` 摘树。
    - 检验：下一帧布局结算时，浮层自动安全关闭并脱树，无悬浮幽灵。
14. **`T42-14 (Overlay.HoverTrackerNoStuck)`**：
    - 装置：鼠标移动进入浮层内按钮，随后移出浮层到主窗口背景。
    - 检验：浮层内按钮正常收到 `OnMouseLeave`，高亮状态复位，无状态粘连。
15. **`T42-15 (Overlay.TooltipNonInteractive)`**：
    - 装置：弹出 Tooltip 覆盖在普通按钮上方。
    - 检验：鼠标点击穿透 Tooltip 直接触发下方按钮，Tooltip 不参与点外判定。
16. **`T42-16 (Overlay.Phase41SettleIntegration)`**：
    - 装置：在浮层内部子控件动态调用 `SetSize`。
    - 检验：`m_overlayRoot->m_layoutDirty` 置脏，在下一次 `PaintFrame` 帧首结算自动完成重排。

---

## §5 分批实施计划（精确算术核对 · 吸收 P1-1）

| 批次 | 改动性质 | 涉及文件 | 验收标准与预期用例数 |
|---|---|---|---|
| **批 0** | **测试基建与公共类型**（产品码零行为改动） | 新增 `ECDI/include/ECDI/Window/OverlayTypes.h`<br>新增 `src/Tests/OverlayTests.cpp`<br>`RunAllTests.h/.cpp`（注册） | 编译通过，公共头 **100 → 101**，基线 **556 → 559**（落地 3 条负对照 T42-1..3） |
| **批 1** | **双根基础拓扑与五处单根清算** | `Window.h`, `Window.cpp`<br>`HoverTracker.h`, `HoverTracker.cpp`<br>`Application.cpp`（9 处链根修正） | 命中同源生效、双根不炸断言，**559 → 563**（加测 T42-4/5/14/15，+4） |
| **批 2** | **浮层生命周期、分层编排与结算管线** | `Window.h`, `Window.cpp`<br>（`ShowOverlay`, `HideOverlay`, `PaintFrame`） | 浮层正常弹出关闭、分层渲染正常、Phase 41 接缝闭合，**563 → 566**（加测 T42-6/10/16，+3） |
| **批 3** | **Light Dismiss、Focus Trap 与锚点跟随** | `Window.cpp`, `Application.cpp` | 全功能闭合、测试全绿，**566 → 572**（加测 T42-7/8/9/11/12/13，+6，累计新增 16 条） |

---

## §6 影响面与风险评估

1. **头文件规模守恒约束**：
   - 公共头由 100 增至 101（新增 `OverlayTypes.h`），在收口时需同步更新 `README.md`、`docs/dev/README.md` 规模锚点与 `tools/doc_lint.py`。
2. **Phase 20 崩溃防御**：
   - 依赖机检判据 `I42-2` 确保 `m_overlayRoot` 绝不发生声明顺序漂移。
3. **断言有效性防御**：
   - 依赖机检判据 `I42-4` 确保 `Application.cpp` 中 9 处调用点全部使用双根集合判定，绝不退化为永真断言。
