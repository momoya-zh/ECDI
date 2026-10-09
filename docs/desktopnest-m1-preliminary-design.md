# DesktopNest M1 · 初步设计（v1.1）

> 来源：需求稿 `desktopnest-m1-requirements.md` **v1.1 ✅ 评审通过（PASS → Preliminary Design）**
> 状态：**v1.1**（2026-10-08，自审缺口补完——A 类矛盾修正 + B 类机制补节 + C 类接口钉死 + D 类 A→C→T 闭合；待评审）
> 定位：**初步设计**——把需求稿 **§7 初设闸门**的三件必钉项（P1 capture 事件流 / P2 Box·View·Model 边界 / P3 Model→UI 同步）钉死，落成组件划分与接口契约。
> ★★ **框架侧改动 = 0**（需求稿 §2.1 v1.1 修正后的口径）：M1 **消费** Phase 31 已交付的运行期窗口几何；**本初设不新增任何框架 API**（评审「不要再为 M1 做框架设计」）。
> ★ **依赖前置已就绪**：Phase 31 **三批全部实施完成**（`SetBounds` / `GetBounds` 可用 · 三链 366/366）⇒ M1 的窗口几何阻塞**已解除**。

---

## §1 初设必答（P1–P3 逐题钉死——本稿核心）

### 1.1 ★★★ P1：浮层的 capture 与「点外关闭」事件流

**先给框架事实**（全部带行号实测，`docs` 之外的源码为准）：

| # | 事实 | 位置 | 对设计的意义 |
|---|---|---|---|
| **F1** | `Application::OnMouseButtonDown` **每次都重新 `HitTest`**（`FindTargetWidget`），然后 `SetCaptureWidget(target)` + **沿 `GetParent()` 链冒泡** | `Application.cpp:278` / `:292` / `:294-302` | ★ **Down 永远走 HitTest，不读既有 capture** ⇒ 「点外面」**天然会命中外部控件**——不需要「在 Down 里判断点在哪」 |
| **F2** | `OnMouseMove` / `OnMouseButtonUp` **优先用 `GetCaptureWidget()`**；capture 非空时**只派发给捕获者及其祖先**（不 HitTest） | `Application.cpp:228-244` / `:310-326` | ★ 捕获一旦建立，Move/Up 直达捕获者 ⇒ **拖出窗口不失**（M1 K4 需要） |
| **F3** | `Up` 后**无条件** `SetCaptureWidget(nullptr)`（先派发再释放） | `Application.cpp:328-329` | ★ capture 生命周期 = **一次 Down..Up** ⇒ 不会被长期占用 |
| **F4** | **框架零「窗口失活」事件**——`WM_ACTIVATE` / `WM_KILLFOCUS` / `WM_SETFOCUS` 在 Win32 平台层**零处理** | 实测 grep（`Platform/Win32/` 全目录无命中） | ★ **不能**靠「窗口失活」关闭浮层；关闭只能由**鼠标路径**驱动（正是 P1 要设计的） |
| **F5** | `Widget::HitTest`：不可见/禁用 ⇒ `nullptr`；**逆序**遍历子节点（后加者在上）；子全未命中才判自身 `ContainsPoint` | `Widget.cpp:114-159` | ★ 「最后一个 `AddChild` 的浮层」**命中优先级最高**（z 序天然） |
| **F6** | `Panel::ContainsPoint()` **恒 false**（自身永不参与命中）；**但递归不受它门控**（子控件照常命中） | `Widget.cpp:98-112` + `Panel.h:45` | ★★ **浮层容器禁用 `Panel`**（需求稿已记红线）——否则**点浮层空白会穿透到下层** |
| **F7** | 裸 `Widget::OnPaint` 是**空实现**（默认不绘制任何东西） | `Widget.cpp:288` | ★ 裸 `Widget` 可作**透明容器**；而其 `ContainsPoint` 走**矩形判定**（`:98-110`）⇒ **可命中** |
| **F8** | `Window::IsClientInteractiveAt` 判据 = 命中且 `hit->ConsumesMouseInput()` | `Window.cpp:310-325` | ★ 决定标题条上**哪些点走客户区**（按钮要做成 `ConsumesMouseInput`，否则点不动） |

**★★ 结论：P1 的答案是「popup 不捕获，且判据不能放在 popup 自己身上」**——理由见下（★ 本稿在核实中**推翻了自己的一版方案**，如实记录在 §1.1.2）。

#### 1.1.1 事件流（修正版）

```
① 用户点「左侧按钮」（标题条上，ConsumesMouseInput=true）
   ⇒ OnMouseButtonDown 走 HitTest 命中该 Button → SetCaptureWidget(button)（F1）
   ⇒ Button::OnClick 回调（应用侧）→ MemberListPopup::Open()

② Open() 做三件事（★ 全部应用侧，零框架改动）：
   a. 把 popup 加进**框的根容器**（AddChild —— 最后一个 = 命中优先 + 最后绘制，F5）
   b. popup->SetVisible(true)
   c. popup->Invalidate()          ← ★ 条 65：改数据必须显式请求重绘
   ★ **不调用 SetCaptureWidget**（此时 capture 仍是那个 Button；Up 到达后框架自动清空 F3）

③ 用户点「浮层内」的部件（成员行 / 管理项）
   ⇒ Down 走 HitTest 命中该部件（F1）→ 其 OnClick → 经回调上报 BoxView → 转译为 Model 调用
   ⇒ 该事件同时**沿祖先链冒泡到框的根容器**（F1 的 while 循环）⇒ 根容器判「hit 在本子树内」⇒ **不关闭**

④ 用户点「浮层空白区」
   ⇒ HitTest 命中 popup 容器自身（★ 必须用**裸 Widget**——F6/F7：裸 Widget 的 `ContainsPoint`
      是矩形判定 ⇒ 命中 popup 自身，**不穿透**到下层）
   ⇒ 冒泡到根容器 ⇒ 判「hit 在 popup 子树内」⇒ **不关闭**（空白区不穿透、也不误关）

⑤ 用户点「浮层外」（框内其他位置）★ 关键路径
   ⇒ HitTest 命中的是**外部控件**（F1）
   ⇒ ★★ **关键：该事件的冒泡链 = 外部控件 → … → 框的根容器** ⇒ **根容器必然收到**（F1）
   ⇒ 根容器判「hit **不在** popup 子树内」⇒ **Close()** ✅
```

★★ **为什么判据必须落在「框的根容器」而不是 popup 自己**（F1 的直接推论）：
`Application::OnMouseButtonDown` 的派发 = **`target` 沿 `GetParent()` 上溯**（`Application.cpp:294-302`）
⇒ **只有 `target` 及其祖先收到事件**。点浮层外时 `target` 是外部控件，**popup 不在其祖先链上** ⇒ **popup 自己永远收不到这次 Down** ⇒ **把「点外关闭」写在 popup 的 `OnMouseButtonDown` 里是死代码**。
⇒ ★ **而「框的根容器」是全框尺寸的祖先节点** ⇒ **任何框内点击都必然经过它** ⇒ 它是唯一可靠的判据点。

#### 1.1.2 ★ 实现口径（唯一）

```cpp
// BoxView 的根容器（app 侧：BoxRoot : public Widget，铺满框客户区）
// ★ 它是**所有框内控件的公共祖先** ⇒ 任何框内 Down 都会冒泡到它（F1）
void BoxRoot::OnMouseButtonDown(const MouseButtonDownEvent& event){
    if (m_popup == nullptr || !m_popup->IsVisible()) return;   // 未开浮层 ⇒ 零开销

    // ① 找「本次真正命中的控件」——与 Application::FindTargetWidget 同源（root 空间 = 客户区绝对，条 68）
    Widget* root = this;
    while (root->GetParent() != nullptr) root = root->GetParent();
    Widget* hit = root->HitTest(event.GetMouseX(), event.GetMouseY());

    // ② 判 hit 是否在 popup 子树内——★ 沿 GetParent() 上溯
    //    （★ Widget **没有** IsAncestorOf 之类的现成 API——实测；上溯是唯一手法）
    bool inside = false;
    for (Widget* w = hit; w != nullptr; w = w->GetParent()){
        if (w == m_popup){ inside = true; break; }
    }

    // ③ 点外 ⇒ 关闭（★ 只改数据 + 显式重绘——条 65）
    if (!inside){
        m_popup->SetVisible(false);
        m_popup->Invalidate();
    }
}
```

★ **四条依据**：
1. **同源**：与 `Application::FindTargetWidget` 用**同一个 `root->HitTest`**（`Application.cpp:212`）⇒ 不存在第二套坐标/命中规则（条 68）；
2. **判据点必然被调用**：根容器是所有框内控件的祖先（F1 的冒泡链必经）；
3. **无父/无窗口时 `root == this`** ⇒ 无头测试**可验证**（条 135 的同一理由）；
4. ★ **`BoxRoot` 的落点**：定义于 `BoxView.h/.cpp`（§2 文件清单）——它是 `BoxView` 装配的根节点，挂于窗口 `GetRootWidget()` 之下（`Window.h:71`），铺满客户区；`m_popup` 也挂在它下面。

★★ **评审点名的风险如何被规避**：评审担心「Popup 自己 capture ⇒ 所有 MouseDown 被它吃掉 ⇒ 点外面关不掉」。本设计**不建立 popup 捕获**，且**判据不放在 popup 上** ⇒ 该风险**结构性不存在**（不是「注意避免」，是「没有那条路径」）。

#### 1.1.3 ★ 两条边界（如实登记，不掩盖）

| # | 边界 | 说明 | 去向 |
|---|---|---|---|
| **B1** | **标题条区域的点击不会关闭浮层** | 标题条走 `HTCAPTION`（F8：空白处 `IsClientInteractiveAt` 为 false）⇒ 该 Down **不进 Widget 派发链** ⇒ 根容器收不到 ⇒ 浮层不关（★ 且此时用户意图是「拖框」，不关是**合理**的） | 记为**设计选择**（非缺陷） |
| **B2** | **点其他框 / 桌面不会关闭浮层** | 每个框是**独立顶层窗口**；点别的窗口 ⇒ 消息进那个窗口 ⇒ 本框无从得知。★ 框架**零窗口失活事件**（F4）⇒ 无跨窗关闭的现成手段 | ★ **M1 不做**（需框架级能力）；记为 **O6** |

★ **另一条可选路径（不采用，记为备选）**：`backdrop`（弹浮层时铺一层全框透明裸 `Widget` 作遮罩，点它即关闭）。
- **可行性**：★ 确实可行——裸 `Widget::ConsumesMouseInput()` 默认 **false**（`Widget.h:213`）⇒ 遮罩**不会**破坏标题条的 `HTCAPTION` 拖动；内容区点击命中遮罩 ⇒ 关闭。
- **不采用的理由**：① 需多一个**全框尺寸**的节点，且其尺寸须**随框变化同步**（M1 折叠会改窗口高 ⇒ 多一处同步点，条 136 的「摆位依赖」教训）；② 根容器方案**零额外节点**（根容器本就要铺满客户区）。
- ⇒ **本设计取「根容器判据」**——**更少机制**（YAGNI，条 22）。

### 1.2 ★★★ P2：Box / View / Model 的职责边界

**采用评审倾向的第三种思路**（`BoxWindow` / `BoxView` / `model` 三层分离），落地为**四个件**：

| 件 | 层 | 职责（一句话） | ★ **不做什么** |
|---|---|---|---|
| **`BoxModel`** | **纯数据**（`desktopnest::` 命名空间，**零 ECDI 依赖**） | 持有全部框的 `Box` 记录（`id` / `title` / `state` / `members` / `active`），提供**纯函数式**查询与变更（`Collect` / `Detach` / `SetActive` / `Toggle`），并在每次变更后**发通知** | ★ **不持有 Widget / Window / 任何 ECDI 类型**；**不做布局、不碰 UI** |
| **`BoxWindow`** | **窗口生命周期** | 持有一个顶层 `Window&`（经 `Application::Create`）+ 其**根子树**（`BoxRoot` + `BoxView`——装配接线见 **C-M1-9**）；负责创建 / 销毁 / 用 Phase 31 的 `SetBounds`/`GetBounds` 落实几何 | ★ **不解释鼠标手势**、**不持有成员语义**（那是 View/Model） |
| **`BoxView`** | **视图组装** | 组装框内控件树（**根容器 `BoxRoot`** + 标题条三部件 + 内容区 + 浮层）；把用户操作**翻译成 Model 调用**；按 Model 状态**重建/同步**自身；**「点外关闭」判据的宿主**（§1.1.2——落在 `BoxRoot::OnMouseButtonDown`） | ★ **不直接改 `members`**（必须经 Model）；★ **不自行决定几何**（窗口几何归 `BoxWindow`）；★ **不碰 `Window` 类型**（几何请求经 `BoxWindow&`——C-M1-6/9） |
| **`MemberListPopup`** | **视图（浮层的独立件）** | 浮层本体（裸 `Widget` 子类）：两分区渲染 + 选择意图回调 + **弹出几何**（§3.4） | ★ **不碰 Model**——它只**发出选择意图**（回调），由 `BoxView` 转译成 Model 调用；★ **不承担「点外关闭」判据**——判据在 `BoxRoot`（F1 冒泡链决定 popup 收不到点外事件，§1.1.2） |

★ **与评审原话的对照**：评审要求「不要让一个 `BoxWidget` 同时承担**窗口生命周期 / 模型 / 视图**」⇒ 本设计**恰好三个件各担一件**（`BoxWindow` / `BoxView` / `BoxModel`），浮层再独立一件。
★ **依赖方向（单向，无环）**：`BoxModel`（最底层，零依赖）← `BoxView` ← `BoxWindow`（最上层，持 Window）。★ **`BoxModel` 不 include 任何 `ECDI/` 头**——这是「纯数据」的可机检判据（盯防 ①）。

### 1.3 ★★★ P3：Model → UI 的同步机制（单向数据流）

**唯一口径：`Model 变更 → 通知 → View 重建/同步`**（评审原话「不要变成『点击收编 → 直接删 Widget → 然后顺便修改 members』」）。

```
用户操作（如点「收编 B」）
      ↓
① BoxView 的意图转译：model.Collect(a, b)          ← ★ 只改数据
      ↓
② BoxModel 内部：members 变更 → 自增 revision → **多播通知**（★ **形态钉死 = 多播 `std::function` 回调列表**——沿框架 Phase 7.5 先例[`Button::SetOnClick`]；签名 `void(BoxId, ModelChange)`，Model 对**每个受影响的 Box 各发一次**：收编/拆出 = 2 次[A 与 B]、切换 = 1 次；`BoxView` 在**构造期**注册自己的 handler）
      ↓
③ 各 BoxView 的 OnModelChanged() 被调用             ← ★ 唯一的 UI 更新入口
      ↓
④ View 按**当前 Model 状态**重建/同步自身：
     · 被收编者（B）：BoxWindow->Close()（销毁窗口）  ← ★ 由 View 执行「窗口生命周期」的请求
     · 收编者（A）：标题徽标 / 成员列表 / 内容 按新 state 重建
      ↓
⑤ 收尾：Invalidate()（★ 条 65——数据变了必须显式请求重绘）
```

**★ 三条纪律（写进实现契约）**：

| # | 纪律 | 反例（禁止） |
|---|---|---|
| **S1** | **UI 更新只发生在 `OnModelChanged` 内**（唯一入口） | ❌ 点击回调里直接删 Widget + 顺手改 `members` |
| **S2** | **Model 变更前不触碰任何 Widget / Window** | ❌ 先 `Close()` 再改 `members`（中途 Model 与 UI 不一致，且失败无法回滚） |
| **S3** | **`OnModelChanged` 必须幂等**（按当前状态重建，不依赖「上一次是什么」） | ❌ 「删掉刚加的那个 Widget」式增量补丁 |

★ **S3 的意义**：让「Model 是唯一真相源」在实现上可验证——**重建的结果只取决于 Model 快照**；A2 用例即按此断言（改 Model → 调 handler → 查 UI 与 Model 一致）。

---

## §2 组件划分与文件清单（`examples/DesktopNest/`）

| 文件 | 内容 | 预估规模 |
|---|---|---|
| **`BoxModel.h/.cpp`** | `Box` / `MemberId` / `BoxModel`（纯数据 + 通知） | ~150 行 |
| **`BoxWindow.h/.cpp`** | 顶层窗口封装：`Application::Create` 建窗 + **装配接线**（建 `BoxRoot` 挂 `GetRootWidget()`、建 `BoxView` 装配控件树）+ `SetBounds` / `GetBounds` 封装 + `Close()` | ~110 行 |
| **`BoxView.h/.cpp`** | 框视图：**根容器 `BoxRoot`**（点外关闭判据宿主）+ 标题条三部件（含徽标按钮）+ 内容区 + 浮层挂载 + Model 通知响应 | ~280 行 |
| **`MemberListPopup.h/.cpp`** | 浮层（裸 `Widget` 子类）：两分区渲染 + 选择回调 + 弹出几何（§3.4） | ~150 行 |
| **`main.cpp`** | 装配（§3.7 伪代码：建 `BoxModel` → 3 个 `BoxWindow` → `Show`） | ~60 行 |
| **`CMakeLists.txt`** | 沿 `examples/ModelProbe` 模板 | ~25 行 |

★ **注意**：需求稿 §2.2 原写「4 文件」——本初设按 P2 的**四件分离**扩展为**6 文件**（含 CMake）。★ **如实记偏离**：这是 P2 三件必钉项的**直接后果**（评审要求「不要让一个 BoxWidget 同时承担三层」⇒ 拆件必然增文件）。
★ **根 CMake 影响面**：+1 行 `add_subdirectory(examples/DesktopNest)`（框架 CMake 仍 0）。

---

## §3 关键机制设计

### 3.1 框的整体形态与常驻（K1）

**创建 + 配置期四件套**（均 `Show()` 之前——Phase 12 配置期契约）：

```cpp
Window& win = app.Create(boxTitle, 220, 320);                // K1 默认尺寸 220×320（DIP——`Create` 的 DIP 契约，Phase 22）
win.SetChromeMode(ChromeMode::Borderless);                   // 无边框（Phase 12）
win.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);      // = 32 DIP：行为区覆盖标题条（§3.2 的 HTCAPTION 前提）
win.SetResizeInset(0);                                       // 不可缩放（§9-5 拍板——消费 Phase 12 既有契约，无框架改动）
win.SetWindowLayer(WindowLayer::Desktop);                    // K1 常驻：被应用窗口覆盖、Win+D 后仍可见（Phase 16 已验证，直接消费）
```

- **圆角背景**：`BoxRoot::OnPaint` 发 `DrawRoundedRectCommand`（Phase 8 命令现成）铺满客户区——「看得见的圆角框」= 本命令 + `Borderless`，无新机制。
- **尺寸不随成员切换改变**（K1）：切换只换内容区控件（§3.5），窗口几何不动（`SetBounds` 仅在折叠 / 拆出 / 级联时调用——§3.3 / §3.5）。
- **多开**（K6）：每框一个顶层 `Window`——Desktop 档多窗口结构上支持（Phase 16 `DesktopLayerTests` T16-4 双窗口用例在案），无特殊机制。

### 3.2 标题条三部件与 `HTCAPTION` 的分工（K5）

| 部件 | 类 | `ConsumesMouseInput` | `WM_NCHITTEST` 结果 |
|---|---|---|---|
| 左侧按钮（成员列表入口，含徽标） | **`BadgeButton`**（`Button` 子类） | **true**（`Button.h:33`） | 客户区 ⇒ 按钮收到点击 |
| 中部标题 | `Label` | false（默认） | **`HTCAPTION`** ⇒ 拖动整框（★ 系统路线，K5） |
| 右侧按钮（二态开关） | `Button` | **true** | 客户区 ⇒ 按钮收到点击 |
| 标题条空白 | 容器自身 | — | **`HTCAPTION`** ⇒ 拖动 |

★ **依据**：`Window::IsClientInteractiveAt`（`Window.cpp:310-325`）——「命中**消费鼠标输入的控件**才算可交互，命中纯显示控件仍走 `HTCAPTION`」。⇒ **本设计无需任何框架改动**即可同时得到「按钮可点」+「空白可拖」（A4 的判据正落在此）。
★ **前置**：`SetCaptionHeight(CaptionBar::kDefaultHeight)` 使**行为区**覆盖标题条（否则 `HTCAPTION` 分支不生效——Phase 13 D7「框架不联动」）。

★ **成员数徽标**（K3「左侧按钮加成员数徽标」）：左侧按钮 = `BadgeButton : public Button`——`OnPaint` 先调 `Button::OnPaint`（既有绘制不动），再在按钮右上角叠加**圆角底 + 数字**（`DrawRoundedRectCommand` + `DrawTextCommand`，命令现成）；徽标值 = `members.size()`（M1 假数据下仅合并框 C 显示 `2`），经 `OnModelChanged` 重建（S3 幂等）。★ **不自创绘制设施**——只消费 Phase 5.1 / Phase 8 命令。

### 3.3 折叠 / 展开（K2）

**消费 `CollapsiblePanel` 既有语义**（`SetExpanded` / `Toggle` / `SetExpandDirection(Down)`）：

- 折叠 = `CollapsiblePanel` 沿**锚定边收缩到 0** + 动画结束时 `SetContentVisible(false)`（既有四路径同步，Phase 30 T30-7 已钉住）；
- ★ **但「仅标题条」还要求窗口本身变矮** ⇒ **`BoxWindow` 用 Phase 31 的 `SetBounds`**：折叠时 `height = 标题条高`、展开时恢复原高（★ **保留 `x/y` 不变**——K2 要求「只改几何高度」）。
- ★ **顺序**：先 `SetBounds`（窗口变矮）→ 再 `collapsible->SetExpanded(false)`（内容收缩）——或反之？**本设计定：先 `SetBounds`**（窗口先到位，避免内容先收缩而窗口未变的中间态可见）。★ 详见 §5 开盘项 **O1**（动画期间窗口高度是否需要逐帧跟随——**M1 取「不跟随」**，见 §5）。

### 3.4 成员列表浮层（K3）

- **容器**：`MemberListPopup : public Widget`（★ **不用 `Panel`**——F6 红线）；
- **两分区**：① 「显示哪个成员」= 成员行（`Button` 或 `StateWidget` 系）② 「成员管理」= 「拆出」「加入成员…」；
- **z 序**：`AddChild` **最后一个**（F5：逆序命中 ⇒ 最上层）；
- **点外关闭**：§1.1 的判据；
- ★ **弹出几何（钉死）**：锚点 = 标题条左侧按钮——`x` = 按钮左边缘、`y` = 标题条底边（= `captionHeight` = **32 DIP**）；**宽 = 框客户区宽**、**高 = 内容区剩余高度**（框高 − 标题条高）⇒ 浮层 = 覆盖内容区的全高浮层。理由三条：① K3「浮层不改变框尺寸」⇒ 只能占框内已有空间；② O2「不出框」⇒ 高 = 内容区高；③ 入口在标题条 ⇒ 从标题条底边展开是唯一自然锚点。★ 像素级微调（留边 / 圆角）留详设，**几何口径初设钉死**。
- **不改变框尺寸**：浮层是框内的**浮层**（绝对定位在框内，允许溢出框外？⇒ ★ **M1 限制：浮层位于框内且不出框**——理由：出框需要「子节点不裁剪」的额外语义（`ClipsChildren` 是 `ScrollView` 等在用），M1 不做。**记为 O2**）。

### 3.5 收编 / 拆出 / 切换（K4）

**纯 Model 操作 + 通知重建**（§1.3）：

| 操作 | Model | View（在 `OnModelChanged` 中） |
|---|---|---|
| **收编** `A.Collect(B)` | `A.members += B`；`B` 记录 `placement`（收编前 `GetBounds`）后标记为非顶层 | `B.BoxWindow->Close()`；`A` 重建徽标/列表 |
| **拆出** `A.Detach(B)` | `A.members -= B`；`B` 恢复为顶层 | `B.BoxWindow = Create()` + `SetBounds(恢复的 placement 或级联偏移)` |
| **切换** `A.SetActive(X)` | `A.active = X` | `A` 重建标题 + 内容区 |

★ **级联偏移**（K4 冻结「一律级联、不判重叠」）：`newPlacement = 原 placement + (kCascadeStep, kCascadeStep)`（无记忆时 = 收编者右下角 + 步进）。

★ **切换成员的内容滚动**：M1 **不做**——需求稿 K4 允许后置（「滚动行为可后置到内容超出时才验」）；M1 假列表不超出一屏 ⇒ 记为 **O7**（内容超出时的滚动留 M2+，`ScrollView` 能力现成，届时直接消费）。

### 3.6 拖动（K5）

**零应用代码**——`HTCAPTION` 系统路线（§3.2）；★ A4 的可测判据 = 「标题条空白区 `IsClientInteractiveAt` 返回 false」（走 `HTCAPTION`）+「按钮返回 true」。

### 3.7 装配流程（`main.cpp` ~60 行——C-M1-9 的落地形态）

```cpp
int main(){
    Application app;

    BoxModel model;                                    // 纯数据（零 ECDI 依赖——C-M1-4）
    // K6 冻结初始态：3 个框——A（独立）· B（独立）· C（合并框 [C1, C2]，active = C1）
    BoxId a = model.AddBox("A");
    BoxId b = model.AddBox("B");
    BoxId c = model.AddBox("C", { c1, c2 });           // members = [C1, C2]

    // 每框一件：BoxWindow 持窗口生命周期，BoxView 装配控件树（§1.2 P2 四件分离）
    std::vector<std::unique_ptr<BoxWindow>> boxes;
    for (BoxId id : { a, b, c }){
        auto box = std::make_unique<BoxWindow>(app, model, id);   // Create() 内：建窗 → BoxRoot 挂 GetRootWidget() → BoxView 装配
        box->Show();
        boxes.push_back(std::move(box));
    }
    return app.Run();
}
```

★ **所有权链**（无环——C-M1-4）：`main` 拥有 `BoxModel`（最底层）与 `BoxWindow` 列表；`BoxWindow` 拥有 `BoxView` 并持 `Window&`（`Application::Create` 返回，窗口生命周期由 `Application` 容器管理——框架既有契约）；`BoxView` 持 `BoxWindow&`（**几何请求唯一通道**——C-M1-6/9）与 `BoxModel&`（数据）；`BoxModel` **零 ECDI 依赖**。`BoxView` 构造期向 `BoxModel` 注册自己的变更 handler（C-M1-8）——`main.cpp` **不注册**任何 Model 回调。

---

## §4 契约（C-M1-1..C-M1-9）

| # | 契约 |
|---|---|
| **C-M1-1** | **框架侧改动 = 0**（公共 API 0 / 框架 CMake 0）；M1 只消费 Phase 31 + 既有能力 |
| **C-M1-2** | ★★ **浮层不建立捕获**；**「点外关闭」判据落在「框的根容器」**（不是 popup 自己——F1 冒泡链决定 popup 收不到「点外」事件，见 §1.1.2） |
| **C-M1-3** | **浮层容器必须是裸 `Widget`**（禁 `Panel`——`ContainsPoint` 恒 false ⇒ 穿透） |
| **C-M1-4** | **依赖单向**：`BoxModel` 零 ECDI 依赖 ← `BoxView` ← `BoxWindow`；**无环** |
| **C-M1-5** | **UI 更新唯一入口 = `OnModelChanged`**（S1）；**Model 变更前不碰 Widget**（S2）；**重建幂等**（S3） |
| **C-M1-6** | **窗口几何只经 `BoxWindow`**（`SetBounds` / `GetBounds`）；`BoxView` 不直接碰 `Window` |
| **C-M1-7** | **折叠先 `SetBounds` 后 `SetExpanded`**；折叠保留 `x/y` 只改 `height`（K2） |
| **C-M1-8** | **Model 通知 = 多播 `std::function` 回调**（沿 Phase 7.5 先例）；签名 `void(BoxId, ModelChange)`，每个受影响 Box 各发一次；`BoxView` 构造期注册，`main.cpp` 不注册 |
| **C-M1-9** | **装配接线**：`BoxWindow::Create()` 内 = `Application::Create` → `BoxRoot` 挂 `GetRootWidget()` → `BoxView` 装配；`BoxView` 持 `BoxWindow&`（几何请求唯一通道），不碰 `Window` 类型 |

---

## §5 开放项（O1–O7）

| # | 项 | 倾向 |
|---|---|---|
| **O1** | 折叠动画期间**窗口高度是否逐帧跟随** | ★ **M1 不跟随**（一次 `SetBounds` 到位 + 内容动画）——逐帧跟随需在 `AnimationManager` 回调里逐帧 `SetBounds`（每帧一次 `SetWindowPos`），M1 观感验收不要求；★ 记为 M2+ 观感优化 |
| **O2** | 浮层是否允许**溢出框外** | ★ **M1 不允许**（框内绝对定位）——溢出需「子节点不裁剪」语义，M1 不做（YAGNI） |
| **O3** | 「加入成员…」的候选列表**排序规则** | 按 Model 记录序（插入序）——M1 假数据规模下不值得排序 |
| **O4** | `CaptionBar` 复用 vs 自绘标题条（需求稿 §2.1 原留项） | ★ **倾向自绘**——`CaptionBar` 的三按钮（最小化/最大化/关闭）与 M1 需要的三部件（成员列表/标题/折叠）**语义不符**；且 `CaptionBar` 内部固定布局，无法插入「徽标」⇒ **自组装标题条**（容器 + 三子件） |
| ★ **O5** | ★★ **M1 用例进 `ecdi_tests` 需改框架 `CMakeLists.txt`**（追加应用源到 `TEST_SOURCES`——沿 `ModelProbeTests` 先例 `CMakeLists.txt:157`）⇒ **与 C-M1-1「框架 CMake 0」冲突** | 两选项见 **§8 第 4 问**；★ **倾向 (a)**（沿先例、测试同批跑），并把 **C-M1-1 如实修正**为「框架公共 API 0 / 框架 CMake **+1 行**」 |
| ★★ **O6** | **点「其他框 / 桌面」不关闭浮层**（§1.1.3 B2）：每框是独立顶层窗口，且框架**零窗口失活事件**（F4）⇒ **无跨窗关闭手段** | ★ **M1 接受**（记为已知边界——不影响 A1/A3 验收：A3 只要求「点外部关闭」，M1 口径 = **同框内**点外部）；★ 若将来要求跨窗关闭 ⇒ 需框架级「失活事件」（**新 Phase**，M1 不做） |
| **O7** | 切换成员时的**内容滚动** | ★ **M1 不做**（需求稿 K4 允许后置——M1 假列表不超出一屏）；内容超出时的滚动留 M2+（`ScrollView` 能力现成，届时直接消费） |

---

## §6 验收映射（A1–A5 → 设计落点）

| 需求验收 | 设计落点 | 可测性 |
|---|---|---|
| **A1 观感主回路（人工）** | §3.1–3.6 全部机制 | ★ **人工**（用户执行 K6 冻结路径） |
| **A2 模型一致性（自动）** | §1.3 单向数据流 + S1–S3 | ★ **可自动**（改 Model → 调 handler → 断言 UI/Model 一致） |
| **A3 浮层纪律（自动）** | §1.1 + C-M1-2/3 | ★ **可自动**（HitTest 命中 popup 自身不穿透；浮层期间框尺寸逐位不变） |
| **A4 拖动语义（自动+人工）** | §3.2 + §3.6 | ★ **可自动**（`IsClientInteractiveAt` 在按钮/空白处的返回值） |
| **A5 既有全绿** | — | ★ **基线 366**（Phase 31 落地后）——M1 新增用例按实际数计 |

★ **A→C→T 闭合表**（条 119②——需求验收 A → 设计契约 C → 测试用例 T 逐项认领；**6 条** = 需求稿「~4–6 条」的上界）：

| 用例 | 验收 | 契约 | 装置与判据 |
|---|---|---|---|
| **T-M1-1** | **A2** | C-M1-5 / C-M1-8 | **收编** `B→A`：断言 `A.members` 含 `B`、`A` 徽标重建为 `2`、`B` 收到销毁请求（`BoxWindow::Close` 被调） |
| **T-M1-2** | **A2** | C-M1-5 / C-M1-6 / C-M1-8 | **拆出** `B`：`A.members` 移除 + `B` 窗口重建；`SetBounds` 恢复 placement（有记忆）/ 级联偏移 `原值 + (kCascadeStep, kCascadeStep)`（无记忆）逐项断言 |
| **T-M1-3** | **A2** | C-M1-5 / C-M1-8 | **切换成员**：`active` 变更 + 标题 / 内容区重建断言 |
| **T-M1-4** | **A3** | C-M1-2 / C-M1-3 | 点浮层空白 ⇒ `HitTest` 命中 popup 自身（**不穿透**）；点框内外部 ⇒ 根容器判据触发关闭（`IsVisible == false`）；浮层期间 `GetBounds` **逐位不变** |
| **T-M1-5** | **A3** | C-M1-5（S3） | 重建幂等：连续两次触发 `OnModelChanged` ⇒ UI 状态逐位一致 |
| **T-M1-6** | **A4** | C-M1-1 | `IsClientInteractiveAt`：标题条空白 ⇒ `false`（走 `HTCAPTION`）/ 标题条按钮 ⇒ `true` |

★ **装置**：沿 `ModelProbeTests` 先例（demo 实现链入 `ecdi_tests`——O5 待拍板）；`BoxModel` 零 ECDI 依赖 ⇒ **T-M1-1..3 可在纯数据层先验（无头）**；T-M1-4..6 需窗口装置（`TestWindow` 先例 / `RecordingBackend`）。

---

## §7 影响面

| 维度 | 值 |
|---|---|
| 框架公共头 / API | **0 / 0** |
| 框架 CMake | **0**；根 CMake **+1 行** |
| 应用侧文件 | **6**（§2；★ 需求稿原写 4——按 P2 四件分离如实上修） |
| 测试 | 新增 M1 用例 **6 条**（**T-M1-1..6**——§6 A→C→T 闭合表：A2×3 / A3×2 / A4×1）——★ **需框架 CMake 的 tests 链接应用实现**（沿 `ModelProbeTests` 先例：`CMakeLists.txt:157` 把 `ModelProbe.cpp` 追加进 `TEST_SOURCES`）⇒ ★ **框架 CMake 可能 +1~2 行**（★ 与 C-M1-1「框架 CMake 0」**冲突**，见 O5） |
| 风险 | **低～中**——零文件操作；最大不确定点已由 §1 三题钉死 |

---

## §8 待评审确认

1. **P1 的方案**（§1.1）：★ **「不建立 popup 捕获」+「判据落在框的根容器」** 是否认可——它是评审点名风险的**结构性规避**（§1.1.2 给出了 F1 冒泡链的推论：判据放 popup 上是**死代码**）；
2. **P2 四件分离**（`BoxModel` / `BoxWindow` / `BoxView` / `MemberListPopup`）是否认可；
3. **O4 倾向自绘标题条**（不复用 `CaptionBar`）是否认可；
4. ★ **新增 O5**（§7 暴露的冲突）：M1 用例要进 `ecdi_tests` 就需**改框架 `CMakeLists.txt`**（追加应用源）——这与 C-M1-1「框架 CMake 0」冲突。**两个选项**：**(a)** 沿用 ModelProbe 先例，框架 CMake +1 行（**如实修正** C-M1-1 为「框架公共 API 0 / 框架 CMake +1 行」）；**(b)** M1 用例不进 `ecdi_tests`，改为应用侧独立测试目标（**框架 CMake 仍 0**，但测试不在主套件里跑）。★ **本稿倾向 (a)**——沿既有先例、且 M1 的自动验收（A2/A3/A4）应当与框架其余测试**同批跑**；
5. ★★ **O6（跨窗关闭）**：§1.1.3 B2 登记的边界——**点其他框/桌面不关闭浮层**（框架零失活事件）。★ **M1 是否接受该边界**？（接受 ⇒ 不影响 A3 验收口径；不接受 ⇒ 需**新立框架 Phase** 加失活事件，M1 顺延）。

---

## §9 修订记录

- **v1.0**（2026-10-08）初稿。**输入**：需求稿 **v1.1**（评审 PASS + §7 三件初设闸门）+ Phase 31 **三批实施完成**（窗口几何就绪）。
  **§1 三题钉死**：
  - **P1**——八条框架事实 **F1–F8 带行号**（Down 恒 HitTest / capture 只影响 Move-Up / Up 后自动释放 / **框架零失活事件** / HitTest 逆序 / `Panel` 恒 false / 裸 `Widget` 可命中 / `IsClientInteractiveAt` 判据）⇒ ★★ **本稿在核实中推翻了自己的一版方案**：初版把「点外关闭」判据放在 **popup 自己的 `OnMouseButtonDown`**，核实 `Application.cpp:294-302` 的**冒泡链**后发现——**点浮层外时 popup 不在 target 的祖先链上 ⇒ 它根本收不到那次 Down ⇒ 那段判据是死代码**；★ 且初版误用了**不存在的 API** `IsAncestorOf`（实测 `Widget` 无此方法）。⇒ **修正为**：**判据落在「框的根容器」**（所有框内控件的公共祖先 ⇒ 任何框内 Down 必经，F1）；「hit 是否在 popup 子树内」用 **`GetParent()` 上溯**判定。★ 两条边界如实登记（**B1** 标题条点击不关——走 `HTCAPTION` 不进派发链，且符合拖动意图；**B2** 点其他框/桌面不关——框架零失活事件 ⇒ **O6**）。
  - **P2**——**四件分离**（`BoxModel` 零 ECDI 依赖 / `BoxWindow` 窗口生命周期 / `BoxView` 视图组装 / `MemberListPopup` 独立）；依赖**单向无环**。
  - **P3**——**单向数据流**（`Model 变更 → 通知 → OnModelChanged 重建`）+ **S1/S2/S3 三条纪律**（唯一入口 / 变更前不碰 UI / 重建幂等）。
  **§2 组件划分**（6 文件——★ 需求稿「4 文件」按 P2 如实上修）· **§3 机制设计**（标题条三部件 × `HTCAPTION` 分工 · 折叠消费 `CollapsiblePanel` + Phase 31 `SetBounds` · 浮层 z 序与禁 `Panel` · 收编/拆出/切换的 Model↔View 对照 · 拖动零应用代码）· **§4 契约 C-M1-1..C-M1-7** · **§5 开放项 O1–O6**（★ O4 倾向自绘标题条）· **§6 验收映射 A1–A5** · **§7 影响面**（★ 暴露 **O5**：框架 CMake 是否 +1 行——**与 C-M1-1 冲突，待拍板**）· §8 待评审确认**五问**。
- **v1.1**（2026-10-08）**自审缺口补完**（对照需求稿 K1–K7 / A1–A5 逐项核对 + 条 119② A→C→T 闭合检查；用户拍板「A/B/C/D 全收」）。**实体改动**：
  ① **A 类（内部矛盾修正）**：§1.2 `MemberListPopup` 职责**删除「点外关闭」判据**（与 §1.1.2「判据落在根容器」的结论矛盾——旧方案残留），改列「两分区渲染 + 选择回调 + 弹出几何」；`BoxView` 职责**增列判据宿主**；**`BoxRoot` 落点钉死**（定义于 `BoxView.h/.cpp`，挂 `GetRootWidget()` 之下——`Window.h:71`；§1.1.2 依据新增第 4 条）；
  ② **B 类（机制补节）**：**新增 §3.1「框的整体形态与常驻（K1）」**（配置期四件套代码 + 圆角背景 = `DrawRoundedRectCommand` + 默认尺寸 220×320 不随切换变 + 多开注记）——原 §3.1–3.5 顺延为 **§3.2–§3.6**（内部引用同步：§3.6 拖动 → §3.2、§6 A1 → §3.1–3.6、§6 A4 → §3.2 + §3.6）；**§3.2 增「成员数徽标」机制**（`BadgeButton : public Button`——`OnPaint` 调基类后叠加 `DrawRoundedRectCommand` + `DrawTextCommand`；值 = `members.size()` 经 `OnModelChanged` 重建）；**§3.4 钉死浮层弹出几何**（锚点 = 左侧按钮：`x` = 按钮左缘 / `y` = 标题条底边 32 DIP；宽 = 客户区宽 / 高 = 内容区剩余高——理由三条）；**§3.5 登记切换滚动去向**（M1 不做 ⇒ **O7**）；**新增 §3.7 装配流程伪代码**（`main.cpp` ~60 行形态 + 所有权链）；
  ③ **C 类（接口钉死）**：**通知形态 = 多播 `std::function` 回调**（沿 Phase 7.5 先例；签名 `void(BoxId, ModelChange)`，每个受影响 Box 各发一次——收编/拆出 2 次、切换 1 次）⇒ **C-M1-8**；**装配接线钉死**（`BoxWindow::Create()` 内 = `Application::Create` → `BoxRoot` 挂 `GetRootWidget()` → `BoxView` 装配；`BoxView` 持 `BoxWindow&` 为几何请求唯一通道、不碰 `Window` 类型）⇒ **C-M1-9**；
  ④ **D 类（验收闭合）**：§6 **新增 A→C→T 闭合表**——**T-M1-1..6**（A2×3 / A3×2 / A4×1；`BoxModel` 零依赖 ⇒ T-M1-1..3 纯数据层无头先验，T-M1-4..6 窗口装置）；§7 测试数「~4–6 条」收敛为 **6 条**；
  ⑤ **契约表扩为 C-M1-1..C-M1-9**、**开放项扩为 O1–O7**。
  **未改动**：§1.1 P1 全部结论（F1–F8 / B1 / B2 / 根容器判据）· §1.2 四件分离形态 · §1.3 S1–S3 纪律 · §8 待评审五问。
