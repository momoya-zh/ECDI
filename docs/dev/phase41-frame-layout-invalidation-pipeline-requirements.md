# Phase 41 · 帧级布局 / 重绘失效结算管线（#79）· 需求稿

> 状态：**v1.2**（2026-10-29 **两项核心裁决已拍板并落下**：D41-1 = **ⓑ**、D41-11 = **方案 A**；配套 D41-2 ⓐ / D41-3 ⓑ+ⓐ / D41-3.2 / D41-4 门控全部定案。v1.1 收 Gemini 4 Argon 审核：3 P0 + 4 P1 + 2 P2 全部逐条对码核实成立）。上游 = `roadmap-deferred.md` v2.63 的 **#79**（顺位 ②）。
> ★ **本 Phase 是契约推翻项**——它要求改写 `Widget.h` 两条已写下的契约（`:147-149` / `:201-203`）与 `ScrollView.cpp:337` 的一条注释。按项目纪律，**契约裁决必须先经用户拍板**（§7 闸门 1），不得在实施期顺手改。
> ★ 全部行号已对 HEAD `a375824` 亲核；用例基线 **546 / 546 全绿**（2026-10-29 MinGW 实跑）。

**红线（三条，任一破了本 Phase 即失败）**

1. **布局结果逐位不变**：`Arrange` 的几何输出与「显式调用」时完全相同——包括不可见子停泊坐标、stretch 余数分配、末位可见截断。帧首结算只是**换了个驱动者**，不是换算法。
2. **无窗口路径零副作用 + 存量测试改动面**：构造期 / 无头测试 / `GetWindow()==nullptr` 时，setter 的置脏行为必须静默退化。既有 **546** 条中 **545 条零改动**，★ **恰 1 条必须随契约同步翻转**——`Widget.SetSizeStillNoRepaint`（`VisibilityRepaintTests.cpp:191-213`，`:286` 注册），它就是旧契约的**反向守卫**（`:188-190` 注释：「若将来有人『顺手统一』，本条会红」）。★ **v1.1 订正**：v1.0 写「546 条不改一行即全绿」是**错的**（当时只 grep 了「未 Arrange/不 Arrange/手摆」，漏了这条以 `RegionDirty` 形式表达的守卫）。翻转必须**显式登记**，不得悄悄改。
3. **不引入每帧无界重算**：结算必须**有界**——要么按脏触发，要么有可论证的代价上界。不得让「业务侧漏调」消失的同时换来「每帧烧满 CPU」。

---

## §1 输入与问题陈述

### 1.1 账本原文（#79，roadmap v2.63）

> **帧级布局 / 重绘失效结算管线缺失**——`SetText` / `SetSize` / `SetVisible` 各自为政、无一上报布局失效；`Layout` 无失效通知；`PaintFrame` 帧首无布局结算 ⇒ 业务侧必须手写调用链（漏一环即错位或不刷新），而为防漏调又普遍防御性重复全量重排。

账本把它定性为「**架构缺口**（★ 附契约推翻项）」，并明确「必须走三件套 + 用户拍板，不得顺手改」。

### 1.2 为什么这条与 ECDI 的定位正面冲突

框架名叫 **Everyone Can Do It**。而现状要求使用者**记住框架内部的调用顺序**：

```cpp
// examples/ModelProbe/ModelProbe.cpp:735-743 —— 业务侧真实代码
void ModelProbePage::RefreshStatText(const std::string& message){
	// 9.8 状态消息统一出口（详设 §6 顺序硬要求）：① SetText → ② AutoSize（宽度随内容——
	// 后调用者赢覆盖构造期 180 定宽）→ ③ statRow Arrange（statLabel 变宽后 searchBox/allBtn/noneBtn 随动）→ ④ Invalidate
	m_statLabel->SetText(message);
	m_statLabel->AutoSize();
	m_statRow->Arrange();
	Invalidate();
}
```

注释自己写着「**顺序硬要求**」——四步顺序错了就错位或不刷新，而**编译器不会提醒、运行期不会报错**。这不是使用者的问题，是框架把内部时序暴露成了公共契约。

### 1.3 三类具体失效（都不是理论）

| # | 失效 | 机理（已亲核） | 现状补救 |
|---|---|---|---|
| **①** | 改可见性 ⇒ 兄弟控件**错位** | `HorizontalLayout.cpp:67-72` / `VerticalLayout.cpp:67-72` 对**不可见子跳过槽位并停泊到自身 bbox 负区**（C-VIS-2/4）⇒ 可见性变化**必然**改变后续子控件坐标。但 `Widget::SetVisible`（`Widget.cpp:427-446`）只 `Invalidate()`，**不通知父布局** | 业务侧手写 `Arrange` |
| **②** | 改尺寸 ⇒ 兄弟控件**错位** | `Widget::SetSize`（`Widget.cpp:420-425`）只写 `m_geometry`，不置脏、不通知 | 业务侧手写 `Arrange` |
| **③** | 改文本 ⇒ 视觉**不刷新** | `TextWidget::SetText`（`TextWidget.cpp:37-51`）只 `MarkTextChanged()`（`TextWidget.h:101`：`++m_textRevision`，仅失效 **preferred 指纹**），**不 `Invalidate`** | 业务侧手写 `Invalidate` |

条 ③ 是审计条 65 记录的「`SetText` 陷阱」同族——非鼠标事件驱动的 UI 更新漏了 `Invalidate`，现象极像「事件根本没到达」，会把排查引向错误方向。

### 1.3.1 ★ 第四条：现网活体 bug（`examples/ModelProbe` 的搜索过滤）

`ModelProbe.cpp:326-333` 的搜索框回调：

```cpp
searchBox->SetOnTextChanged([this](const std::string& keyword){
    ...
    for (size_t i = 0; i < m_models.size() && i < m_rows.size(); ++i){
        ...
        m_rows[i].panel->SetVisible(kw.empty() || id.find(kw) != std::string::npos);
    }
});
```

**只调 `SetVisible`，没有 `Arrange`**。而 `m_rows` 的容器由 `ListLayout` 管槽位（`ModelProbe.cpp:390`：`list->GetContentView().SetLayout(std::make_unique<ListLayout>(kRowHeight))`）⇒ `ListLayout.cpp:24` 对不可见子停泊到 bbox 负区、**后续可见行不再前移** ⇒ 过滤后**可见行停在旧 `y`，中间留出大段空白**（且 `UpdateContentExtent` 也未调，滚动范围同时失真）。

★ **这条的价值**：它不是「理论上的漏调」，而是**仓内真实存在的一处**。它同时印证两件事：① 契约缺口会造成真实可见的错位；② 连本仓作者都会漏——因为 `phase25` 文档 B2 曾误称「每帧 Arrange」（§2 S13②），按那个描述写代码，`SetVisible` 之后确实应该自动重排。**文档错误直接生产了代码 bug**。Phase 41 落地后此处将自动修复（不需要改 `ModelProbe.cpp`）。

### 1.4 驱动点散落：三处 + 四处手写

| 位置 | 内容 | 问题 |
|---|---|---|
| `Window::OnResized:830-841` | `SetSize → Arrange → Invalidate` | ★ **全库唯一完整的「手写三件套」原型**——正确顺序就写在这里，但它是私有的、业务侧看不见 |
| `Application::OnWindowResized:287-293` | `root.Arrange()` | ★ **与上一条同一帧重复**：`WM_SIZE` 先调 `m_host.OnResized()`（`Win32PlatformWindow.cpp:938`），随后 fall-through 派发 `WindowResizedEvent`（`:967`）⇒ 同一帧 Arrange **两次**（幂等所以无害，但暴露「没人知道该谁负责」） |
| `Window::OnResized:836` | （即第一条） | — |
| `examples/MinimalApp/main.cpp:21` | Show 前手写 `Arrange()` | `Window::Show()`（`Window.cpp:129-139`）**自身不 Arrange** ⇒ 业务必须自己补。★ **v1.1 补充**：平台 `Win32PlatformWindow::Show()` 在 `ShowWindow` 之后调 **`UpdateWindow(m_hwnd)`**（`:464`）⇒ **同步**派发首帧 `WM_PAINT` ⇒ `PaintFrame` 立刻跑。所以帧首结算一旦落地，`Show()` 返回前就已完成首次布局 ⇒ 这两处手写 `Arrange()` 变成**冗余安全调用**（可留可删） |
| `examples/ModelProbe/main.cpp:330` | 手写 `Arrange()` | — |
| `examples/ModelProbe/ModelProbe.cpp:740` | 四步链中间一步 | 见 §1.2 |
| `examples/ModelProbe/ModelProbe.cpp:801` | `GetContentView().Arrange()` | `ScrollView` 明写「不接管 Arrange」（`ScrollView.h:19`） |

账本描述的「为防漏调又普遍防御性重复全量重排」在此得到印证：**同一帧 Arrange 两次**已是既存事实。

### 1.5 ★ 成本模型已变：`Invalidate` 的代价项**已经付过了**

这是本稿最需要先讲清的一条，因为它是 `Widget.h:201-203` 反对理由的事实基础：

> 几何 setter 由 `Layout` 逐帧批量调用（**加了会每帧重绘**）

**实测事实**（`VisibilityRepaintTests.cpp` 文件头注释已记账，本轮复核）：

- `Win32PlatformWindow.cpp:923-926`：`case WM_PAINT: m_host.OnPaint(); return 0;` —— **没有 `BeginPaint` / `EndPaint`**（决策 39：绘制不走翻译器）
- `Win32PlatformWindow.cpp:564-572`：`Invalidate()` = `InvalidateRect(hwnd, nullptr, FALSE)`（全客户区）
- ⇒ **update region 从不被 validate ⇒ region 恒脏 ⇒ 每轮消息泵必产一帧**（该文件注释记录了探针实测：「泵 N 条消息恒定产出 ~N 帧，与是否 Invalidate 无关」）

**结论**：在当前平台层实现下，「每帧重绘」**已经在发生**。因此：

- `SetSize` 里加 `Invalidate()` 的**边际代价 ≈ 0**（只是多一次 `InvalidateRect`，而 region 本来就是脏的）；
- 真正的成本项是 **「每帧 O(N) 布局计算」**——这才是必须防的东西，也是本 Phase 红线③ 的靶子。

⇒ 本 Phase 的取舍不是「要不要加 `Invalidate`」，而是「**要不要每帧 Arrange、如果不是靠什么判断**」。

### 1.6 同族 setter 三态不一（现行自相矛盾）

同族 setter 行为分五类；★ **四个** `SetSize` override（不是三个——见下方 v1.1 订正）方向**相反**：

| setter | 位置 | 同值守卫 | `Invalidate` | 布局通知 |
|---|---|---|---|---|
| `SetPosition` | `Widget.h:119`（**非虚 inline**） | ✗ | ✗ | ✗ |
| `Widget::SetSize` | `Widget.cpp:420-425`（虚） | ✗ | ✗ | ✗ |
| `TextBox::SetSize` | `TextBox.cpp:261` | ✗ | ★ **有**（末尾，为 caret 可见 + 滚动条） | ✗ |
| `ScrollView::SetSize` | `ScrollView.cpp:328-339` | ✗ | ★ **明确拒绝**（`:337`「SetSize 由布局路径调用，重绘由调用方负责（既有契约）」） | ✗ |
| `CollapsiblePanel::SetSize` | `CollapsiblePanel.cpp:155`（收起态早退） | ✗ | ✗ | ✗ |
| ★ `CaptionBar::SetSize` | `CaptionBar.cpp:78`（`Widget::SetSize` + `RelayoutChildren()` 对 4 个子控件 `SetPosition`/`SetSize`） | ✗ | ✗ | ✗ |
| `Widget::SetVisible` | `Widget.cpp:427-446` | ★ **有**（M9 修复，正确性要求） | ★ **有** | ✗ |
| `TextWidget::SetText` | `TextWidget.cpp:37-51` | ✗ | ✗ | ✗ |
| ★ `TextBox::SetText` | `TextBox.cpp:124-145`（两个重载） | ✗ | ★ **有**（末尾 `Invalidate()`，且**重复**调了一次 `MarkTextChanged()`——基类已调） | ✗ |

**这条表的用处**：说明「同族 setter 该不该 `Invalidate`」在**仓内已有互相矛盾的答案**，而且**两族都矛盾**：

- **`SetSize` 族**：4 个 override 里 **1 个 `Invalidate`（`TextBox`）、1 个明确拒绝（`ScrollView:337` 注释）、2 个不管（`CollapsiblePanel` / `CaptionBar`）**；
- **`SetText` 族**：`TextBox::SetText` 有 `Invalidate`、`TextWidget::SetText`（`Label`/`Button`/`StateWidget` 的继承体）**没有**——同一次语义变更，子类比基类多做了一件事。

业务侧无法预测，只能逐个试。这不是谁的错，是**没有一条统一契约**。★ **v1.1 订正**：v1.0 只列了 3 个 `SetSize` override（漏 `CaptionBar`）、只列了 `TextWidget::SetText`（漏 `TextBox` 的 override）——两处都是**按「我记得的」写、而非按「全库清点」写**，已按 `grep -rn "SetSize(int w, int h)"` 与 `grep "^void TextBox::SetText"` 的实测结果补齐。

---

## §2 现状勘察（S1–S13，2026-10-29 亲核 HEAD `a375824`）

| # | 勘察项 | 事实（行号已亲核） |
|---|---|---|
| **S1** | `SetPosition` 形态 | `Widget.h:119`：`void SetPosition(int x, int y) { m_geometry.x = ...; }` —— **非虚、内联、纯赋值**。★ 若要求它置脏，必须**移出头文件**并重新评估调用频次（`Layout` 内每 child 一次）。 |
| **S2** | `Widget::SetSize` 形态 | `Widget.cpp:420-425`：虚函数，两行赋值，无守卫、无 `Invalidate`、无置脏。★ 无同值守卫 ⇒ 若在此置脏，`Layout` 每帧对每个 child 调 1–2 次都算「变化」。 |
| **S3** | ★ `TextBox::SetSize` 已内建 `Invalidate` | `TextBox.cpp:261` 起：`Widget::SetSize` → `EnsureCaretVisible` → `LayoutScrollBar` → `SyncScrollBar` → **`Invalidate()`**。⇒ 「几何 setter 不得 Invalidate」的契约**已被该类合法违反**。 |
| **S4** | ★ `ScrollView::SetSize` 明确拒绝 | `ScrollView.cpp:328-339`，`:337` 注释「不 Invalidate：SetSize 由布局路径调用，重绘由调用方负责（既有契约）」；但同函数内 `ApplyLayout → ClampOffset → SyncBars` 三段副作用**都会间接改几何**（滚动条摆位）。 |
| **S5** | `CollapsiblePanel::SetSize` | `CollapsiblePanel.cpp:155`：收起态早退（重定 `m_expandedRect` + `ApplyGeometry(0)`）；展开态 `Panel::SetSize` + `m_content->SetSize`。**全程无 `Invalidate`、无置脏**。`ApplyGeometry:283` 同样不 `Invalidate`（动画期间的刷新靠 `AnimationManager` 聚合，见 S12）。★ **v1.1 补齐第 4 个 override**：`CaptionBar::SetSize`（`CaptionBar.cpp:78`）= `Widget::SetSize` + `RelayoutChildren()`（对 4 个内部子控件 `SetPosition`/`SetSize`），同样无 `Invalidate`、无置脏。⇒ `SetSize` override **共 4 个**（`grep -rn "SetSize(int w, int h)"` 实测）。 |
| **S6** | `SetVisible` 是唯一的「正确形态」参照 | `Widget.cpp:427-446`：同值守卫（`:434-436`，正确性要求——`TextBox::SyncScrollBar` 每帧调用）+ `Invalidate()`（`:444`）。★ 但仍**不通知布局** ⇒ §1.3 条①。 |
| **S7** | `SetText` 只失效指纹 | `TextWidget.cpp:37-51` 两个重载都只 `MarkTextChanged()`；后者（`TextWidget.h:101`）= `++m_textRevision`，仅喂 preferred 指纹（`:563`）与 wrap 指纹（`:432`）。**不 `Invalidate`、不置布局脏**——这是 `Widget.h:203` 说的「有意契约」。 |
| **S8** | `Layout` 抽象零失效通道 | `Layout.h:13` 仅 `virtual void Arrange(Widget& parent) = 0;`。`Layout.cpp` 是空 namespace。**既无 `Invalidate()`、也无测量/脏查询接口**。 |
| **S9** | 四个 Layout 的几何写入模式 | `HorizontalLayout.cpp:72/89/92/95`（停泊 + `SetSize` + `SetPosition`）· `VerticalLayout.cpp:72/93/96/99`（同构）· `GridLayout.cpp:29/35`（**只 `SetPosition`**，D9）· `ListLayout.cpp:24/28`（**只 `SetPosition`**，D9）。⇒ 若 `SetSize`/`SetPosition` 一律置脏，**布局自己算一次就会把整棵子树标脏**（S10 的降级问题由此而来）。 |
| **S10** | `ArrangeInternal` 自上而下 | `Widget.cpp:398-412`：先 `m_layout->Arrange(*this)`，再逐 child 递归。`Arrange()`（`:414-418`）是它的薄壳。**无重入保护、无脏判断、无版本号**。 |
| **S11** | 帧管线无结算 | `Window::PaintFrame`（`Window.cpp:176-199`）：`m_commands.clear()` → 建 `PaintContext` → `m_rootWidget->Paint(ctx,0,0)` → `SyncBackdropSurface` → `BeginFrame/Execute/EndFrame`。**帧首无任何布局动作**。`Window::Show`（`:129-139`）自身也不 Arrange（但平台 `Show` 的 `UpdateWindow` 会同步触发首帧 `WM_PAINT`，见 §1.4 该行注）。 |
| **S12** | 动画只聚合重绘、不聚合布局 | `AnimationManager.h:77` 契约④「有任何 onValue 实际执行 → 聚合一次 `Invalidate`」；`AnimationManager.cpp:248` `m_platformWindow.Invalidate()`。⇒ 动画期间「画对了新尺寸、画错了兄弟位置」（#80 的机理，本 Phase **非目标**）。 |
| **S13** | ★ 文档陈述订正（两处） | ① roadmap #79 机理⑥ 写「全库唯一布局驱动点 = `WM_SIZE` 的 `Application::OnWindowResized`」——**实为三处**（另含 `Window::OnResized:836`、`examples` 四处手写），且前者与 `Application::OnWindowResized` 是**同一帧两次**（§1.4）。② `phase25-list-container-preliminary-design.md:51` 的 B2 写「**Arrange 触发链 = 每帧**：`Window::OnPaint` → `m_rootWidget->Arrange()`」——**经 `git log -S` 查证：`PaintFrame` 里从未有过 `Arrange`**（`m_rootWidget->Arrange()` 全历史只出现在一个 commit `81037ed`，位置是 `OnResized`）。该陈述不准确，本稿不继承。⇒ 回写账本与 phase25 稿的订正留待详设（§7 闸门 5）。 |

### 2.1 测试面现状（决定影响面的真实下界）

| 项 | 事实 |
|---|---|
| 基线 | **546 / 546 全绿**（本机 MinGW 实跑，2026-10-29） |
| `LayoutTests.cpp` | **1457 行 / 33 条用例 / 65 处显式 `.Arrange()`** |
| **幂等性已断言** | `LayoutTests.cpp:50` · `:126` · `:488` · `:943` · `:1069` · `:1260`，注释统一为「幂等（**每帧 Arrange 的自然结果**，B2）」⇒ 「重复 Arrange 结果不变」是**既有已测属性** |
| **「未 Arrange 则几何保持」零断言** | 全仓 `grep -iE "未 ?Arrange\|不 ?Arrange\|保持手\|手动布局\|不自动"` 在测试中 **0 命中** ⇒ 就「Arrange 驱动」这一面而言**没有守卫阻力**。★ **但 v1.1 重大订正**：本稿 v1.0 由此推出「契约推翻没有守卫阻力」，**这个推论是错的**——旧契约还有一条**以 `RegionDirty` 形式表达**的反向守卫，见下一行 |
| ★★ **旧契约的反向守卫（恰 1 条，必须翻转）** | `Widget.SetSizeStillNoRepaint`（`VisibilityRepaintTests.cpp:191-213`，`:286` 注册）：真窗口 + `Button` 挂 `RootWidget` 下，`ClearRegion()` 后 `button->SetSize(120,40)` 断言 `!RegionDirty()`（`:207`），再 `SetPosition(30,30)` 断言 `!RegionDirty()`（`:211`）。`:188-190` 注释自述「本条钉住范围边界……**若将来有人「顺手统一」，本条会红 —— 那就是范围蔓延的信号**」⇒ 它**就是为了拦住本 Phase 而写的** |
| 观测量具（已存在，可复用） | `invalidateCount`（`AnimationTests.cpp:31` / `ProgressBarTests.cpp:38` / `RingProgressBarTests.cpp:46`）· `probe.RegionDirty()`（`VisibilityRepaintTests.cpp:107` 实现 / `:237` **量具校准用例**证明 `GetUpdateRect` 能观测到框架发出的 Invalidate） |
| `SetLayout` 调用 | 测试 **57 处** · `examples` **13 处** |
| `AutoSize` 调用 | 业务侧 **仅 1 处**（`ModelProbe.cpp:739`）· 测试 17 处 |

★ **关键边界**：无窗口（`GetWindow()==nullptr`）时没有任何「帧首」⇒ 结算**天然不触发** ⇒ 65 处显式 `Arrange` 的测试模式**完全保留**。这是红线②「**545 条守恒**」那一半的结构性原因；另 1 条（`SetSizeStillNoRepaint`）按 D41-11 显式翻转。

---

## §3 范围与非目标

### 3.1 范围

| # | 内容 | 依赖 |
|---|---|---|
| **R1** | **契约裁决与改写**（§7 闸门 1 拍板后执行）：`Widget.h:147-149`、`Widget.h:201-203`、`ScrollView.cpp:337` 三处，改写为与新机制一致的表述。★ **并含 1 条存量用例的显式翻转**：`VisibilityRepaintTests.cpp:191-213` 的 `Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`（D41-11 方案 A 的配套动作，红线② 已登记） | D41-1 / D41-11 |
| **R2** | **布局失效通道**：`Widget` 基类上建立「脏」的表达与传播（+ 逐 setter 的**置脏门控**，D41-4）；`Layout` 抽象**不改**（D41-2 ⓐ 下零改动） | D41-1/D41-2 |
| **R3** | **帧首结算**：`PaintFrame` 在 `m_rootWidget->Paint()` **之前**完成一次布局结算；结算必须**有界**（红线③）；含 D41-3 的抑制 + 结算后清脏、D41-3.2 的根/子树作用域 | D41-1/D41-3 |
| **R4** | **同族 setter 方向统一**：★ **四个** `SetSize` override（`TextBox:261` / `ScrollView:328` / `CollapsiblePanel:155` / **`CaptionBar:78`**）纳入同一契约；`SetVisible` 补布局通知；★ **并收敛 `TextBox::SetSize` 末尾的冗余 `Invalidate`** 与 `TextBox::SetText` 的**重复 `MarkTextChanged`**（`TextBox.cpp:130/:138`——基类 `TextWidget::SetText` 已调） | D41-4 / D41-11 |
| **R5** | **驱动点收敛**：`Window::OnResized` 与 `Application::OnWindowResized` 的同帧重复 Arrange 归并（保留一个权威驱动点） | D41-7 |
| ★ **R6** | **几何 setter 同值守卫**（v1.1 新增）：`SetSize` / `SetPosition` / `SetStretch` 加同值守卫，与 `Widget::SetVisible`（`Widget.cpp:434-436`）同构。★ **这是正确性要求而非优化**（D41-3 末段的论证） | D41-3 / D41-11 |

### 3.2 非目标（明确排除，逐条给理由）

| # | 非目标 | 理由 |
|---|---|---|
| **N1** | 动画驱动的连续几何（**#80**） | 动画是「每帧都改」⇒ 脏恒真；需要额外回答「60fps 下脏子树重排的代价与合并策略」。账本已把它单列为 #80 并写明依赖本 Phase。本 Phase 只提供**通道**，不解决「每帧都脏」的优化。★★ **v1.1 必须预警的隐藏雷区**（Gemini P1-4，已亲核）：`CollapsiblePanel::SetSize` 的**收起态**分支（`CollapsiblePanel.cpp:157-172`）会把传入的 `(w,h)` 写进 `m_expandedRect` 作为「展开基准」；而 `VerticalLayout:95-97` 的 `m_fillCrossAxis` 分支会调 `child->SetSize(cross, child->GetHeight())`，收起态 `GetHeight()==0` ⇒ **`m_expandedRect` 的高度被抹成 0**，此后永远展不开。当前**无实例**（`examples` 里 `CollapsiblePanel` 都在手摆容器中，`DesktopNest::BoxRoot` 无 Layout）⇒ 546 条不会爆，但 Phase 41 落地后「把 `CollapsiblePanel` 塞进 `VerticalLayout`」会**立刻踩雷**。⇒ 必须在新契约注释或闸门里写明：**在 #80 修好这个基准覆写冲突前，`CollapsiblePanel` 不得放入 `fillCrossAxis=true` 的 Layout 容器** |
| **N2** | 局部重绘 / 脏矩形 | 现为全客户区 `InvalidateRect(hwnd, nullptr, FALSE)`。改成脏矩形是**渲染层**课题（且 §1.5 已证明当前每帧本来就全量重绘），与布局失效正交。 |
| **N3** | 增量 / 脏子树布局 | 本 Phase 的结算粒度是**整树**（`Arrange()` 既有语义）。脏子树重排需要 `Layout` 提供「我的输入变了吗」的判据——那是第二个真实消费者出现后的事（YAGNI）。 |
| **N4** | 浮层根（**#81**） | 等第二消费者。 |
| **N5** | `Home`/`End` 视觉行首尾（**#82**） | 纯键盘语义，与之正交。 |
| **N6** | `ScrollView::UpdateContentExtent` 的自动挂钩 | `ScrollView.h:41` / `:66` 明写 D6「**显式更新（不自动挂钩）**」，且要求「按序摆放」时用 `ListLayout`/`GridLayout` 组合。**extent 是内容语义**（extent ≠ 几何），不属于「几何失效」范畴。★ 若 D41-4 认定 `AddChild` 需置脏，则 `ListLayout` 场景下 extent 仍需业务显式更新——**这条边界要写进新契约的注释**，否则使用者会以为自动了。 |
| **N7** | 虚拟化 / 控件回收 | 不在范围（Phase 25 已排除）。 |

---

## §4 决策点 D41-1..11（★ **D41-1 / D41-2 / D41-3 / D41-3.2 / D41-4 / D41-7 / D41-11 已定案**；D41-5 / D41-6 / D41-9 / D41-10 仍为倾向，待初设闸门）

### D41-1 ★ 结算形态（**本 Phase 的核心裁决**）

| 候选 | 形态 | 优点 | 代价 |
|---|---|---|---|
| **ⓐ** | **帧首无条件 `root.Arrange()`** | 契约改动最小（只需删掉「不 Arrange」）；幂等**已被 6 处断言守住**；零脏标志、零传播、零「永远脏」风险 | 每帧 O(N) 几何计算。`ListLayout` 20k 行时每帧 20k 次 `SetPosition`（§2 S9）⇒ **触碰红线③** |
| ★★ **ⓑ（定案）** | **setter 置脏 + 帧首按需结算** | 空闲帧 O(1)（一次 bool 判断）；有界 | 需回答 D41-2（传播形态）与 D41-3（结算期间置脏 = 永远脏）|
| **ⓒ** | **保守：保留显式契约，只新增 `InvalidateLayout()` 供业务选调** | 零契约推翻、零风险 | **不解决问题**——业务侧漏调依然错位；只是把「手写 Arrange」改名为「手写 InvalidateLayout」。★ 本稿**不推荐**：它让 §1.2 的四步链变短一行，但没消除「必须记住顺序」 |

★★ **定案（用户 2026-10-29 拍板）：ⓑ**。

理由：ⓐ 在 20k 内容下触碰红线③；ⓒ 名存实亡。但 ⓑ **必须**同时把四件答干净，否则会退化成「永远脏 ⇒ 等价于 ⓐ，却多了复杂度」：

| 配套 | 内容 | 定案 |
|---|---|---|
| **D41-2** | 脏标志存放与传播 | **ⓐ 根单标志 + 沿父链上溯**（见下） |
| **D41-3** | 结算期间置脏的处置 | **ⓑ 抑制 + ⓐ 结算后清**（见下） |
| **D41-3.2** | 清脏的作用域 | 根清 / 子树不清（见下） |
| **D41-4** | 置脏门控 | 有 Layout 读才置脏（见下） |

★ 这四件是**一组**，缺任何一个都会让 ⓑ 变成负资产。这是收审核后最重要的认知变化：v1.0 只意识到 D41-3 一件。

### D41-2 脏标志的存放与传播

| 候选 | 形态 | 代价 |
|---|---|---|
| ★★ **ⓐ（定案）** | **根单标志** + setter 沿父链上溯置位（同 `Invalidate()` 既有模式 `Widget.cpp:585-593`） | 每次 setter O(depth)。`Layout` 内每 child 1–3 次写 ⇒ O(N·depth)。**与 Arrange 同阶**，可接受 |
| ⓑ | **每节点 bool** + 帧首查询时遍历整树求 or | 帧首 O(N) 遍历，但只在有脏时才有收益 |
| ⓒ | **根持计数/代数**（`uint64 m_layoutEpoch`），setter 上溯 ++ | 同 ⓐ，多一个「已经结算到第几代」的比较，避免 clear 时序问题 |

★★ **定案：ⓐ 根单标志 + 上溯**。理由：最简、与既有 `Invalidate()` 的上溯模式同构（同一个 `GetWindow()` 父链遍历，`Widget.cpp:551-563`）；O(N·depth) 与 `Arrange` 本身同阶，不引入新的复杂度量级。

★ **ⓒ 作为详设的后备**：若详设发现「结算期抑制」有边角（见 D41-3），可升级到代数比较——它在结构上天然免疫 clear 时序问题。**本稿不预先引入**（YAGNI：先按最简形态落，有问题再换，两者改动面都是 `Widget` 内部 2–3 个函数）。

★★ **ⓐ 必须配 D41-6 的「子树脏向新根合并」**：根单标志的固有盲区是「离线搭好的子树，脏写在它自己的临时根上」。`AddChild` 必须把子树的脏合并到 `this`，否则挂入窗口后首帧不排（D41-6 的实现要求②）。

### D41-3 ★ 结算期间置脏（ⓑ 的致命细节）

`Arrange` 内部**必然**调用 `SetSize`/`SetPosition`（§2 S9）。若它们无条件置脏：

1. `root.Arrange()` → `Layout` 对 child `SetSize` → **置脏**
2. 结算结束 → 脏标志仍为真
3. 下一帧 → 又 `Arrange()` → 又置脏 → **永远脏 ⇒ 每帧重排 ⇒ 退化成 ⓐ 却更复杂**

**必须回答**（三选一，待详设定）：

| 候选 | 机制 | 风险 |
|---|---|---|
| **ⓐ** | **结算后 clear**（`Arrange()` **递归完成之后**才 `m_layoutDirty = false`） | 结算期间产生的脏被**覆盖掉**。因 `Arrange` 幂等（§2.1 的 6 处断言），丢弃**结果不变** ⇒ 正确。★ 但必须区分**根**与**子树**的 `Arrange()`（见 D41-3.2） |
| **ⓑ** | **结算期间抑制**（`m_inArrange` 标志，setter 见之跳过置脏） | 语义最明确、零「误吞」争议；代价是引入一个状态标志。★ 若 `Layout` 内部真的改了「需要再算一次」的输入会被误吞——但已实核：四个 Layout **只写子几何**，不写 `stretch`/可见性/子集合，故**不存在**这种输入 |
| ⓒ | **代数比较**（D41-2 ⓒ）：结算前记 `epoch0`，结算后若 `epoch != epoch0` 说明「结算期间有新变化」，**再算一次**（最多 N 次，实际 2 次收敛） | 最严谨，但「最多算几次」需要上界论证 |

★★ **v1.1 订正（原 v1.0 的 ⓐ 写反了，且是实质错误）**：v1.0 写「**结算前置** clear（先 `m_layoutDirty = false` 再 Arrange）」，并称「结算期间新产生的脏被丢弃」——**这条推理不成立**。推演如下：

```
若前置 clear：                   若结算后 clear：
  dirty = false                    dirty = true（进来时）
  Arrange() 内部 SetSize(...)       Arrange() 内部 SetSize(...)
    → 置脏（dirty = true）            → 置脏（已 true，无变化）
  返回时 dirty == true ✗            返回时 dirty = false ✓
```

**前置 clear 会被结算期间的写入立刻覆盖**，返回时仍为 `true` ⇒ 下一帧再排 ⇒ **永远脏、每帧重排**（退化成 D41-1 ⓐ，击穿红线③ 与 A41-6）。原稿把因果方向写反了，本条是 v1.1 的**第一处 P0 级修正**。

★★ **定案（用户 2026-10-29 拍板）：ⓑ + ⓐ 组合**（结算期抑制 **且** 结算后清根脏）。理由：

1. **ⓑ 单独不够**——它保证结算期不产生脏，但「进来时的那个脏」仍需要被消费掉，否则下一帧继续脏；
2. **ⓐ 单独也不够**——它虽能覆盖结算期的脏，但如果将来某个 `Layout` 真的在 `Arrange` 里改了输入（新增布局器时可能发生），结算后 clear 会**静默吞掉**该变化；
3. 两者组合 = 「结算期不产生新脏（ⓑ）」+「结算后消费掉进来的脏（ⓐ）」⇒ 语义闭合，且对未来的 `Layout` 更保守。

★★ **v1.2 新增关键细化：抑制的范围是「置布局脏 + `Invalidate`」两者**（由 D41-11 定案 A 带来）

D41-11 定案 A ⇒ 几何 setter 会 `Invalidate()`。而 `Layout::Arrange` 内部对每个 child 调 `SetSize`/`SetPosition`（§2 S9）⇒ 若结算期不抑制 `Invalidate`，一次 `Arrange` 会产生 **N 次 `InvalidateRect`**（region 已脏所以无实际代价，但语义上是 N 次无意义请求）。

⇒ **`m_inArrange` 期间：既不置布局脏、也不 `Invalidate()`**。

★ **为何这是安全的（不是漏刷）**：结算发生在 `PaintFrame` 的 `m_rootWidget->Paint()` **之前**（R3），结算改的几何在**同一帧**就会被画出来 ⇒ 不需要额外请求重绘。唯一例外是「结算期间业务回调改了几何」——但那种改法本身违反结算期语义，详设用 `FRAMEWORK_ASSERT` 暴露（不进 release 分支，因为结算期写几何是布局自己的行为、无法从语法上区分）。

★ **配套必要条件（v1.1 新增）**：`SetSize` / `SetPosition` / `SetStretch` **必须加同值守卫**，与 `Widget::SetVisible`（`Widget.cpp:434-436`）同构。否则：① `Layout` 每帧对每个 child `SetSize`（即使值相同）都会产生「伪变化」；② 复合控件内部写同值几何（`TextBox::LayoutScrollBar` :719、`ScrollView::ApplyLayout` :272-288）也会无谓置脏 ⇒ 下一帧白跑一次全树 `Arrange`，让 **A41-6 变红**。同值守卫在此**不是优化、是正确性要求**（与 M9 修复的理由完全同构）。

### D41-3.2 ★ 清脏的作用域：根 vs 子树（v1.1 新增，P0 级）

D41-3 的「结算后 clear」必须回答一个**作用域**问题：`Arrange()` 是**公开**的（`Widget.h:329`），业务侧会调 `m_statRow->Arrange()`（`ModelProbe.cpp:740`）这种**子树**局部的 `Arrange`。若子树 `Arrange` 也去清**根**的脏标志，就会误吞兄弟分支尚未结算的脏。

**不变量（必须写进详设）**：

| 调用 | 清脏行为 |
|---|---|
| `Arrange()` 于**根**（`m_parent == nullptr`） | ★ **必须清根脏** + 清 `m_inArrange` |
| `Arrange()` 于**非根**（子树局部） | **不清**根脏（兄弟分支可能仍脏）；但**必须**正确复位 `m_inArrange` |

★ **为何这条是 P0**：`Window::OnResized`（`Window.cpp:830-841`）是「`SetSize`（置脏）→ `Arrange()`（同步，根）→ `Invalidate()`」，紧接着 `WM_PAINT` 进 `PaintFrame`。若根的 `Arrange()` **不清脏**，`PaintFrame` 会看到 `dirty == true` 再排第 **2** 次 ⇒ **A41-8「一次 `WM_SIZE` 只 Arrange 一次」永远不成立**。⇒ 「根 `Arrange` 完成即清根脏」是 A41-8 的**唯一**成立条件。

★ 同时，`m_inArrange` 用**计数**而非 bool（同 `m_dispatchRefCount` 的先例，`Widget.h:405-408`）：子树 `Arrange` 可能嵌套在根 `Arrange` 内部（`ArrangeInternal` 递归时不重入 `Arrange`，但业务回调里可能嵌套调用）⇒ 计数可避免内层提前复位。**待详设确认嵌套场景是否真实存在**；若不存在则 bool 足够。

### D41-4 哪些 setter 置布局脏（精确清单）

**判据**：是否改变「**某个** `Layout` 的输入」。据此逐个裁决，并给出门控条件。

★★ **v1.1 核心新增：置脏门控**（Gemini P1-1，已亲核成立）

`Widget::ArrangeInternal`（`Widget.cpp:398-412`）的语义是：`m_layout->Arrange(*this)` **只读 `*this` 的尺寸 + `*this` 的 `m_children` 的几何/可见性/stretch**，然后递归下钻各子的 `m_layout`。⇒ **一次几何写入只有在「某个 Layout 会读它」时才是布局输入**：

| 写入 | 谁会读它 | 门控条件 |
|---|---|---|
| 我的**尺寸** | ① 我的 `m_layout`（我的子要重排）② 父的 `m_layout`（我在父的槽位里） | `m_layout != nullptr \|\| (m_parent && m_parent->m_layout != nullptr)` |
| 我的**位置** | 父的 `m_layout`（父给我定位） | `m_parent && m_parent->m_layout != nullptr` |
| 我的**可见性** | 父的 `m_layout`（不可见 ⇒ 跳过槽位 + 停泊） | 同上 |
| 我的**stretch** | 父的 `m_layout`（分配主轴） | 同上 |
| 我**增删子** | 我的 `m_layout` | `m_layout != nullptr` |

★ **为何这条门控是必要的（不是优化）**：`TextBox::OnPaint` 在**绘制期**调 `SyncScrollBar()`（`TextBox.cpp:1410`），后者调 `m_vBar->SetVisible(need)`（`:755`）。若 `SetVisible` 无条件上溯置脏，则每次滚动条出现/消失都会在**刚刚结算完的那一帧里**把根重新弄脏 ⇒ 下一帧白跑一次全树 `Arrange`。而 `m_vBar` 的父是 `TextBox`，**`TextBox` 没有 Layout**（已实核：`TextBox` / `ScrollView` / `CaptionBar` / `CollapsiblePanel` 四个复合控件**均无 `SetLayout` 调用**，`m_layout` 恒 `nullptr`）⇒ 门控直接免疫这一类伪置脏，且**语义上完全正确**（没有 Layout 读它，就没有布局失效可谈）。

★ **门控的代价（如实记账）**：多一次指针比较（`m_parent && m_parent->m_layout`），可忽略。但它把「置脏」从「无条件」变成「有条件的」，详设必须给出一张**逐 setter 的门控表**，否则实施时极易漏判。

| setter | 是否置布局脏 | 门控条件 | 依据 |
|---|---|---|---|
| `SetSize` | ★ **是** | `m_layout \|\| (m_parent && m_parent->m_layout)` | 改几何；且是 `Layout` 的输出（结算期处置见 D41-3） |
| `SetPosition` | ★ **是** | `m_parent && m_parent->m_layout` | 同上（`GridLayout`/`ListLayout` 只调它，S9） |
| `SetVisible` | ★ **是**（本 Phase 核心缺口） | `m_parent && m_parent->m_layout` | `Layout` 对不可见子**跳过槽位 + 停泊**（§2 S9 的 `HorizontalLayout:67-72` / `VerticalLayout:67-72`）⇒ 可见性**是**布局输入（§1.3 条①） |
| `SetStretch` | ★ **是** | `m_parent && m_parent->m_layout` | `stretch` 由父 Layout 读（`Widget.cpp:448-462`；`GetStretch()>0` 才是参与条件） |
| `AddChild` / `RemoveChild` | ★ **是**（v1.1 定为「是」，理由见下） | `m_layout != nullptr` | 子节点集合只影响**我的** Layout。★ 见 D41-6 的 P1 级补充：**子树脏必须向新根传染** |
| `SetLayout` | ★ **是** | 无条件 | 换布局器 ⇒ 我的子树必须重算 |
| `SetText` | **否**（严格判据下，只 `Invalidate`） | — | 文本内容**不直接**改变几何；只有经 `AutoSize()`（D41-9）才间接改。★ 但 `Invalidate`（重绘）**应加**——这是条 65 的陷阱 |
| `SetStyle` / `ApplyTheme` | **否** | — | 字体变化会改 preferred size，但仍需 `AutoSize` 才落到几何；视觉变化已由既有 `Invalidate` 覆盖 |
| ★ `SetWordWrap` | **否** | — | v1.1 补：`TextWidget::SetWordWrap`（`TextWidget.cpp:104-119`）改断行缓存 + preferred，**已内建 `Invalidate`**，与 `SetText`/`SetStyle` 同类——需经 `AutoSize` 才改几何 |

★ **这张表是 §7 闸门 2 的必答项**。特别注意 **`SetPosition` 是 `Widget.h:119` 的非虚 inline**——若纳入置脏 + 门控，它必须**移出头文件**（S1），这是一个 API 形态变化（签名不变、但 `Widget.h` 改动会触发近全量重编），影响面需在初设核算。

### D41-5 `SetText` 是否置布局脏

- **严格判据（倾向）**：不置。文本不改变几何，只有 `AutoSize` 才改。⇒ `SetText` 只 `Invalidate`（解决条 65），不标布局脏。
- **宽松判据**：置。理由是「业务期望文本变长后容器跟着变」——但那需要 `AutoSize`，不是 `Arrange`。若 `SetText` 只标脏而不 `AutoSize`，`Arrange` 会用**旧几何**重排一次，**结果不变**（纯浪费）。

**倾向：严格**（只 `Invalidate`，不置布局脏）。★ 这条同时意味着 `Widget.h:203`「`SetText` 是有意契约」**仍然成立**——它只是需要补 `Invalidate`，不需要挂布局。契约改写因此比账本预期的**小**。

### D41-6 `AddChild` / `RemoveChild` 是否置脏

**结论（v1.1 定为「是」，且不是可选项）**。

★★ **为何在 D41-2 ⓐ（根单标志）下它是必要条件**（Gemini P1-3，已亲核）：

业务侧的真实建树路径是「**先离线搭子树、再挂进窗口**」：

```cpp
auto subtree = std::make_unique<Panel>();      // 离线：无窗口、无根
subtree->AddChild(std::make_unique<Button>());  // 此时置脏只落在 subtree 这个临时根上
window.GetRootWidget().AddChild(std::move(subtree));   // ★ 挂入窗口
```

- `Widget::SetWindow` 是 **private**（`Widget.h:376`，`friend class Window` 在 `:378`），**全库唯一调用点 = `Window` 构造函数第 113 行**（对刚 new 出来的空 `m_rootWidget`）⇒ **`SetWindow` 不是业务侧路径**，不能指望它来「补脏」。
  ⇒ ★ **v1.0 的 D41-8 曾写「需在详设确认 `SetWindow` 路径不会漏清」，该表述不准确**（把 private 的内部方法当成了业务入口），v1.1 已改。
- 因此：若 `AddChild` **不**把**子树的脏合并到 `this`**，那么子树在离线期积累的脏会**留在临时根上**、随 `std::move` 挂入后**不再有人看**（根单标志只看根）⇒ **挂入后的首帧不会 Arrange** ⇒ 几何停在初始值。

**⇒ 实现要求（两条，都要）**：

1. `AddChild` / `RemoveChild` **自身置脏**（门控 `m_layout != nullptr`，见 D41-4）；
2. `AddChild` **必须把 `child->HasDirtyLayoutInSubtree()` 或子树的脏向上合并**（沿新父链上溯置脏）——否则离线搭树场景失效。

★ 代价：`HasDirtyLayoutInSubtree()` 若做成整树遍历是 O(subtree)。但 `AddChild` 不是热路径（构造期一次性），**可接受**；详设也可改用「每节点 bool + 上溯合并」的无遍历形态（`child->m_layoutDirty` 本身为真就直接置 `this` 脏，递归一层即可——若子树内部脏但子树根不脏则不传播，**这需要详设定：是否要求子树根始终反映子树脏**）。

**其余风险点**：`AddChild` 在构造期被大量调用（`examples` 34 处）此时无窗口 ⇒ 静默退化（安全）；`RemoveChild` 可能发生在**事件派发期**（Phase 35 的 `RequestRemove` 路径）——置脏本身是纯内存写、**不触发任何重排**（重排只在帧首）⇒ 安全。

### D41-7 resize 时序契约

现状 `Window::OnResized:830-841` 是**同步** `SetSize → Arrange → Invalidate`，且随后 `Application::OnWindowResized` **再 Arrange 一次**（§1.4）。

| 候选 | 形态 | 风险 |
|---|---|---|
| ⓐ | **保留同步 Arrange**，只删 `Application::OnWindowResized` 的重复 | 零时序变化；但「同步」与「帧首结算」并存两个驱动点 |
| ⓑ | resize 只置脏，几何更新延到帧首 | ★ **时序契约破坏**：`WindowResizedEvent` 的消费者（含 `Application::OnWindowResized`）在派发时若读子控件几何，会读到**旧值**。UI 布局类框架通常保证「resize 事件到达时布局已更新」（`Win32PlatformWindow.cpp:940-941` 的注释明写「消费者收到事件时 RootWidget 已是新尺寸」） |

★★ **定案：ⓐ**（保留同步，仅去重）。理由：`ⓑ` 会改写一条**已在注释里承诺**的时序契约（`:940-941`），代价远超收益；且 resize 本来就不是高频路径。★ 本条实为 **D41-3.2 的必然推论**——既然「根 `Arrange` 完成即清根脏」，同步 `Arrange` 后 `PaintFrame` 就是 no-op ⇒ A41-8 的「1 次」成立。若不保留同步 Arrange（选 ⓑ），则 `OnResized` 只置脏，`WindowResizedEvent` 消费者会读到旧几何 ⇒ 与 `:940-941` 的承诺冲突。

### D41-8 无窗口行为

**必须**：`GetWindow()==nullptr` 时置脏为**纯内存写、不触发任何结算**（不 Arrange、不 Invalidate——后者既有 `Invalidate` 已是 no-op，`Widget.cpp:585-593`）。⇒ 既有用例**545 条零改动**（红线②），另 1 条按 P0-2 显式翻转。

★ **无窗口时脏标志会累积为真**——这是**正确行为**（几何本就该算）。但「随后挂进窗口」的真实路径**不是 `SetWindow`**（private、唯一调用点 = `Window` 构造，见 D41-6），而是 `AddChild` ⇒ **脏必须由 `AddChild` 向新根合并**（D41-6 的实现要求 ②）。★ v1.0 此处误把 `SetWindow` 当业务入口，v1.1 订正。

### D41-9 `AutoSize` 是否置脏

`Widget::AutoSize`（`Widget.cpp:470-485`）只调 `SetSize`，与 `Widget.h:147-149` 的契约一致。

- 若 D41-4 判 `SetSize` 置脏 ⇒ `AutoSize` **自动**继承了置脏（无需额外改动）⇒ **契约文字可以不变**（它仍然「只调 SetSize」）。
- ★ 这意味着 `Widget.h:147-149` 的「不 Arrange、不 Invalidate、不挂钩 SetText」**后半句仍需改写**：内部虽只调 `SetSize`，但 `SetSize` 现在会置脏 ⇒ 「不 Arrange」的说法在**语义上**仍然成立（它不立刻重排），只是「失效被延迟结算」需要写清。

**倾向**：`AutoSize` 本身零改动，只在契约注释里补一句「其 `SetSize` 会置布局脏 ⇒ 下一次帧首结算重排父容器」。

### D41-10 重绘与布局是否合并为一个 API

- 现状：`Invalidate()`（重绘）已存在且被广泛调用；布局失效是新通道。
- **倾向：保持两个独立 API**。理由：二者语义不同（§1.5 已证重绘边际代价 ≈ 0，而布局是有代价的 O(N)）；合并会让「只想刷新视觉」的调用点被迫触发重排。★ 但可以提供**便捷组合**（如 `InvalidateLayout()` 内部也 `Invalidate()`），因为「布局变了视觉必然要重画」。★ **注意**：这个「内部也 `Invalidate()`」正是 D41-11 的两难所在，须一并拍板。

### D41-11 ★★ 几何 setter 是否连带 `Invalidate()`（v1.1 新增，P0 级两难）

**这是本 Phase 唯一一处「两个方案都有代价、必须由用户裁决」的地方**，起因是仓内存在一条**专门拦住它**的反向守卫（§2.1）：

```cpp
// VisibilityRepaintTests.cpp:191-213  注册名 "Widget.SetSizeStillNoRepaint"
probe.ClearRegion();
button->SetSize(120, 40);
EXPECT_TRUE(!probe.RegionDirty());     // ★ 几何 setter 不请求重绘（既有契约保持）
probe.ClearRegion();
button->SetPosition(30, 30);
EXPECT_TRUE(!probe.RegionDirty());
```

该用例 `:188-190` 自述：「这条钉住范围边界：M9 只补『可见性』这一条，**没有**顺手把 `Invalidate` 推广到 `SetSize`/`SetPosition`（它们由 `Layout` 逐帧批量调用 ⇒ 加了会每帧重绘）。⇒ 若将来有人『顺手统一』，**本条会红** —— 那就是范围蔓延的信号。」

| 方案 | 几何 setter 行为 | 后果 | 代价 |
|---|---|---|---|
| ★★ **A（定案）** | 值真变化（同值守卫通过）+ 不在结算期 ⇒ **置布局脏 + `Invalidate()`** | 「几何变了就一定会被画出来」成为**契约**，不再依赖平台层的偶然副作用 | ★ **`Widget.SetSizeStillNoRepaint` 必红 ⇒ 必须显式翻转**为 `Widget.SetSizeRequestsRepaint`（红线② 已登记）；另需收敛 `TextBox::SetSize` 末尾的冗余 `Invalidate` |
| **B** | 只置布局脏、**不** `Invalidate()` | 该用例保持绿 | ★★ **把正确性押在平台实现的巧合上**：今天「几何变了会被画出来」纯粹因为 `WM_PAINT` 无 `BeginPaint` ⇒ region 恒脏 ⇒ 每帧都被重绘（§1.5）。一旦将来做脏区合帧（#21），`SetSize` 后将**静默不刷新**——正是审计条 65 / M9 的同一个坑，只是潜伏更深 |

★★ **定案（用户 2026-10-29 拍板）：方案 A**。三条理由：

1. **项目已有明文纪律**「不把契约建立在断言/巧合上」——`Widget.cpp:50-55` 的 `AddChild` 注释：「只写断言 = Release 下契约完全失守……本项目已在两处确立『不把契约建立在断言上』」；方案 B 等价于「把正确性建立在『平台层碰巧每帧都重绘』上」，同一条纪律的反面。
2. §1.5 已证：**当前每帧本来就全量重绘** ⇒ 方案 A 的边际代价 ≈ 0（`InvalidateRect` 幂等，region 已脏）。所谓「加了会每帧重绘」的代价项**已经付过了**。
3. 方案 B 会让 R1 推翻 `ScrollView.cpp:337`（「不 Invalidate」）**失去依据**——因为整个 R1 的立论就是「这条注释错了」，若最终行为仍是「不 Invalidate」，那注释其实是对的。

★★★ **v1.2 由此推出的核心架构澄清：两条通道的触发判据不同**

方案 A 与门控（D41-4）合起来，迫使我们必须把「重绘失效」与「布局失效」的判据**显式分开**——这是 v1.1 及之前都没有讲清的一点：

| 通道 | 触发判据 | 例子 |
|---|---|---|
| **重绘失效**（`Invalidate()`） | **值真的变了**（同值守卫通过） | `m_vBar->SetVisible(true)` ⇒ 滚动条要画出来 ⇒ 必须重绘 |
| **布局失效**（置布局脏） | 值真的变了 **且** 某个 `Layout` 会读它（D41-4 门控） | 同上：`m_vBar` 的父（`TextBox`）**无 Layout** ⇒ **不置布局脏** |

★ **这个区分彻底解决了 P1-1**：`TextBox::OnPaint` 里 `m_vBar->SetVisible(need)` 会 `Invalidate()`（视觉确实变了），但**不会**置布局脏（`TextBox` 无 `Layout`）⇒ 下一帧只重绘、**不重排**。而「下一帧重绘」本来就是滚动条出现所必需的 ⇒ 完全自洽，无需任何特判。

⇒ **详设必须在契约注释里把这两条判据分开写**，不能再用「不 Arrange、不 Invalidate」这种把两件事并列的表述（那正是 `Widget.h:148` 原文的病根，见 §1.6）。

★ **翻转的正当性**：那条用例不是「回归守卫」，而是「**范围边界的意图声明**」（注释自称）。本 Phase **正是**要改变那个意图 ⇒ 翻转它是**契约变更的配套动作**，不是掩盖回归。但必须：① 改名为 `Widget.SetSizeRequestsRepaint`（名字要表达新契约）；② 注释写明「Phase 41 契约变更，原 `StillNoRepaint` 见 v1.0 需求稿 §2.1」；③ **断言拆成两条**——`RegionDirty() == true`（重绘通道）+ **布局脏仍为假**（门控：`Button` 与 `RootWidget` 都无 `Layout`）⇒ 这一条同时成为 D41-4 门控的判据；④ 在 §6 影响面与 §8 修订记录中**双处登记**。

---

## §5 验收 A41-1..10

| # | 判据 | 期望 | 手段 |
|---|---|---|---|
| **A41-1** | **存量测试改动面受控**：**545 条零改动全绿** + **恰 1 条显式翻转**（`Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`，见 D41-11 方案 A） | 545/545 + 1 条按新契约断言 | 四构建链（红线②）；★ v1.0 写「546 条一行不改」已订正 |
| **A41-2** | **布局结果逐位不变**：同一棵树的 `Arrange` 输出与改前完全相同 | 逐位 | 复用 `LayoutTests` 33 条几何断言（它们本就是逐位断言）；红线① |
| **A41-3** | **条① 修复**：`SetVisible(false)` 后**无需**手写 `Arrange`，兄弟控件位置即正确 | 新用例 | 新用例（`VerticalLayout` 三子中间一个隐藏 ⇒ 第三个子 `y` 前移） |
| **A41-4** | **条② 修复**：`SetSize` 后兄弟控件经帧首结算自动重排 | 新用例 | 需真窗口（帧首由 `PaintFrame` 驱动）；无窗口下不触发 ⇒ **必须真窗口**或提供 `FlushLayout()` 可测接缝（待详设定） |
| **A41-5** | **条③ 修复**：`SetText` 后 region 变脏（重绘请求已发出） | region 脏 | `GetUpdateRect`（复用 `VisibilityRepaintTests` 量具 + 校准用例） |
| **A41-6** | **有界性（红线③）**：空闲帧**不重排**（ⓑ 形态下） | 0 次 `Layout::Arrange` | 计数型 Layout 替身（`Arrange` 被调 ++）；或 `Arrange` 埋计数器 |
| **A41-7** | **无窗口零副作用** | 构造期 setter 不崩、不重排 | 既有 545 条已覆盖；补一条显式断言 |
| **A41-8** | **驱动点去重**：一次 `WM_SIZE` 只 Arrange 一次 | 1 次 | 计数型 Layout 替身 + 真窗口 resize；★ 依赖 D41-3.2「根 Arrange 完成即清根脏」 |
| **A41-9** | **绘制期伪置脏免疫**（v1.1 新增，P1-1）：`TextBox` 的滚动条因内容变化首次出现/消失（`OnPaint:1410` → `SyncScrollBar:755` → `m_vBar->SetVisible`）后，**下一帧不多排一次** | 后续 2 帧 `Layout::Arrange` 计数 == 0 | 计数 Layout + 真窗口（填入足以触发纵向滚动条的文本 ⇒ 等待出现 ⇒ 观测后续空闲帧）；这是门控（D41-4）的直接判据 |
| **A41-10** | **离线搭树后挂入即排**（v1.1 新增，P1-3）：无窗口下搭好子树（`AddChild` 若干）→ `window.GetRootWidget().AddChild(subtree)` → 首帧后子控件几何正确 | 几何逐位 | 真窗口 + `Show()`（`UpdateWindow` 同步触发首帧）；这是「子树脏向新根合并」（D41-6 要求②）的直接判据 |

★ **A41-4 / A41-6 / A41-8 都需要「能观测到 Arrange 发生了几次」**——现有测试设施里**没有**这个量具（已实核：`grep "class .*: public Layout"` 在 `src/Tests/` **零命中**；`arrangeCalls|ArrangeCount` 全仓**零命中**）。这是本 Phase 的**新增测试基础设施**，须在初设明确。

★ **推荐的量具形态 = 装饰器**（而非重写一个假 Layout）：

```cpp
// 包住真 Layout：转发 → 结果逐位不变（A41-2 与 A41-6 同一装置可测）
class CountingLayout : public Layout{
public:
	explicit CountingLayout(std::unique_ptr<Layout> inner) : m_inner(std::move(inner)){}
	void Arrange(Widget& parent) override{ ++m_calls; m_inner->Arrange(parent); }
	[[nodiscard]] std::size_t Calls() const noexcept{ return m_calls; }
private:
	std::unique_ptr<Layout> m_inner;
	std::size_t m_calls = 0;
};
```

★ 理由：**重写一个假 Layout（只 `SetPosition` 不 `SetSize`）会同时改变布局结果**，让 A41-2「逐位不变」这条红线失去判据；装饰器则**只加计数、不改行为**。这是 Phase 40 里「量具必须与被测行为同构」教训（I-3）的同族应用。

---

## §6 影响面

| 项 | 预估 |
|---|---|
| 源码 | `Widget.h`（契约注释 + 置脏声明 + **同值守卫** + `SetPosition` 可能移出 inline）· `Widget.cpp`（`SetSize`/`SetVisible`/`SetStretch`/`AddChild`/`RemoveChild` 置脏 + 门控；`Arrange` 结算语义 + **根清脏**）· `Layout.h`（若走 D41-2 ⓐ 则**零改动**）· `Window.cpp`（`PaintFrame` 帧首结算 + `OnResized` 去重）· `Application.cpp`（`OnWindowResized` 去重）· `ScrollView.cpp`（`:337` 注释 + `SetSize` 方向统一）· `TextBox.cpp`（方向统一 + **收敛冗余 `Invalidate`/`MarkTextChanged`**）· `CollapsiblePanel.cpp`（方向统一）· ★ **`src/Window/CaptionBar.cpp`**（v1.1 补：第 4 个 `SetSize` override）· `TextWidget.cpp`（`SetText` 补 `Invalidate`）· `src/Tests/*`（`CountingLayout` 装饰器 + 8 条用例 + **1 条翻转**） |
| 公共头 | **100 → 100**（无新增头；`Widget.h` 是既有公共头）。★ 若 `SetPosition` 移出 inline，`Widget.h` 的 ABI 不变但**每个 TU 都要重编**（一次全量） |
| public API | **+1 或 +2**（预计 `InvalidateLayout()`；若 `SetPosition` 移出 inline 则签名不变、非新增）。**待 §7 拍板后定** |
| CMake | **0**（无新增源文件） |
| 用例 | **546 → ~554**（★ **545 守恒 + 1 条翻转**（`SetSizeStillNoRepaint`→`SetSizeRequestsRepaint`）+ **8 条新增**：A41-3/4/5/6/7/8/9/10 + `CountingLayout` 量具校准 1 条） |
| docs | **204 → 205**（本稿）；初设 / 详设各 +1 → **207** |
| 编译批次 | ★ **一次近全量**（`Widget.h` 几乎被所有 TU 包含）⇒ 与 Phase 40 同样「一次编 35 min 量级」 |

---

## §7 初设闸门（移交初设的必答项）

1. ✅ **契约裁决（已拍板 2026-10-29）**：**D41-1 = ⓑ**；配套 **D41-2 = ⓐ（根单标志 + 上溯）**、**D41-3 = ⓑ 抑制 + ⓐ 结算后清（抑制范围含 `Invalidate`）**、**D41-3.2 = 根清/子树不清**、**D41-4 = 门控**。⇒ 剩余工作 = **三处契约文字**（`Widget.h:147-149` / `:201-203` / `ScrollView.cpp:337`）的**新表述逐字给出**，不能再留「由调用方负责」的模糊语义；★ 且必须按 D41-11 的「两条通道判据分开写」。
2. ✅ **D41-11（已拍板 2026-10-29）= 方案 A**：几何 setter 值真变化 + 不在结算期 ⇒ 置布局脏（受门控）+ `Invalidate()`。配套 = 翻转 `Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`（断言拆两条：`RegionDirty()` 为真 + **布局脏仍为假**）。
3. ★ **置脏精确清单 + 门控**（D41-4 表）——逐 setter 定「是/否」与**门控条件**，特别是 `SetPosition`（需移出 inline？）、`AddChild`/`RemoveChild`（含**子树脏向新根合并**的形态）、`SetLayout`。
4. **`SetText` 的双通道裁决**（D41-5）：只 `Invalidate` 还是也置布局脏。★ 这条决定 `Widget.h:203` 的改写幅度；并一并收敛 `TextBox::SetText` 的重复 `MarkTextChanged`（R4）。
5. **可测性**：A41-4/A41-6/A41-8/A41-9 需要「Arrange 调用计数」量具——给出替身形态与接缝（本稿推荐**装饰器** `CountingLayout`，§5 已给草案）。
6. ★★ **D41-3.2 的清脏作用域**：明确「根 `Arrange` 完成即清根脏、子树 `Arrange` 不清」；给出 `m_inArrange` 用 bool 还是计数（需先确认嵌套 `Arrange` 是否真实存在）。
7. ★ **P1-4 雷区处置**：`CollapsiblePanel` 的 `m_expandedRect` 被 `fillCrossAxis` 覆写的问题——本 Phase **只登记预警**（写入契约注释），**不在本 Phase 修**（属 #80 的基准语义，改了就是另一个行为变更）。若你认为必须同期修，需要单独立项。
8. **文档订正**：roadmap #79 机理⑥（三处驱动点 + 同帧重复）与 `phase25` B2（「每帧 Arrange」不实）两处回写——**属于账本/旧稿订正，需授权**。
9. **与 #80 的接缝**：本 Phase 只交付「通道」。必须明确写出「动画期间脏恒真」时的**行为**（每帧整树重排），以及这是**已知且接受**的代价（#80 再优化）——否则详设会被迫在本 Phase 解决 #80。★ 并纳入第 7 条的 `CollapsiblePanel` 预警。
10. **`ScrollView` 的 extent 边界**（N6）：新契约下 `UpdateContentExtent` 仍显式 ⇒ 必须在 `ScrollView.h` 注释里写清「几何自动、extent 手动」，否则使用者会误解。

---

## §8 修订记录

- **v1.0（2026-10-29）** 立项 + 需求稿落盘。§1 问题陈述（三类失效 + 驱动点散落 + ★ **成本模型已变**）；§2 勘察 S1–S13（全部行号对 HEAD `a375824` 亲核）+ §2.1 测试面（**546 全绿** / 65 处显式 Arrange / 幂等已断言 / 「未 Arrange 保持」零断言）；§3 范围 R1–R5 + 非目标 N1–N7（含 `ScrollView` extent 边界）；§4 决策点 D41-1..10（**D41-1 三候选含不推荐的 ⓒ**、D41-3 结算期置脏三候选）；§5 验收 A41-1..8（★ 三条需新量具）；§6 影响面；§7 七条初设闸门（**闸门 1 = 用户拍板**）。
- ★ **本稿订正两处既有文档陈述**（§2 S13）：roadmap #79 机理⑥「唯一驱动点」实为三处且含同帧重复；`phase25` B2「每帧 Arrange」经 `git log -S` 查证**不成立**。订正写入待授权（§7 闸门 5）。
- ★ **本稿的一处利好发现**（§2.1）：全仓**没有**任何断言守护「未 Arrange 则几何保持」⇒ 就「Arrange 驱动」这一面而言阻力小。但这**不等于**没有语义风险（D41-7 的时序契约：`Win32PlatformWindow.cpp:940-941` 承诺 resize 事件到达时布局已更新）。★★ **v1.1 订正**：v1.0 由此推出「契约推翻没有守卫阻力」是**错的**——旧契约还有一条以 `RegionDirty` 表达的**反向守卫** `Widget.SetSizeStillNoRepaint`（§2.1 下一行），v1.0 因 grep 词表不含 `RegionDirty` / `StillNoRepaint` 而漏检。

### v1.1（2026-10-29，收 Gemini 4 Argon 审核）

★ **审核提出的 9 条（3 P0 + 4 P1 + 2 P2）我逐条对 HEAD `a375824` 亲核，全部成立**，无一条被驳回。其中三条是 v1.0 的**实质缺陷**：

| # | 缺陷 | 性质 | 落改 |
|---|---|---|---|
| **P0-1** | D41-3 的 ⓐ「**结算前置** clear」**因果写反** —— 前置 clear 会被结算期间的 `SetSize/SetPosition` 立刻覆盖回去，返回时 `dirty==true` ⇒ 永远脏 ⇒ 击穿红线③ 与 A41-6 | ★ **我的逻辑错误**（附反例推演） | D41-3 的 ⓐ 改「结算**后** clear」；倾向改「ⓑ 抑制 + ⓐ 结算后清」组合；并新增「几何 setter 必须加同值守卫」为**必要条件** |
| **P0-2** | 红线②「546 条一行不改」与 `VisibilityRepaintTests.cpp:191-213` 的 `Widget.SetSizeStillNoRepaint` **直接冲突** | ★ **判断被推翻**（我的 grep 词表漏了 `RegionDirty`/`StillNoRepaint`）| 红线② 改「545 守恒 + **恰 1 条显式翻转**」；§2.1 补「反向守卫」行；新增 **D41-11**（方案 A/B 两难，**必须用户拍板**）；A41-1 同步改 |
| **P0-3** | 保留 `OnResized` 同步 Arrange 时，若「清脏」只写在 `PaintFrame`，则 `PaintFrame` 会再排一次 ⇒ A41-8 永远 = 2 | ★ **我的机制漏洞**（v1.0 只写了「倾向 ⓐ」，没给清脏落点） | 新增 **D41-3.2**：根 `Arrange` 完成即清根脏、子树 `Arrange` 不清（A41-8 的唯一成立条件）|
| **P1-1** | `TextBox::OnPaint:1410` → `SyncScrollBar:755` → `m_vBar->SetVisible(need)` ⇒ **绘制期置脏** ⇒ 下一帧白排一次 | ★ 我未发现 | D41-4 新增 **「置脏门控」**（`m_parent && m_parent->m_layout` 才置脏）——已亲核四个复合控件**均无 `SetLayout`**，门控成立且语义正确；新增 A41-9 |
| **P1-2** | 漏计 `CaptionBar::SetSize`（第 **4** 个 override，`CaptionBar.cpp:78`）与 `TextBox::SetText` override（`TextBox.cpp:124-145`，**已有 `Invalidate`** 且**重复** `MarkTextChanged`） | ★ **清点漏项**（我按记忆写「三个」） | §1.6 表 / §2 S5 / R4 / §6 四处补齐；★ 后者反而**加强**了「同族矛盾」的立论 |
| **P1-3** | `SetWindow` 是 **private**、唯一调用点 = `Window` 构造 ⇒ 离线搭树后挂入靠 `AddChild` ⇒ **子树脏需向新根传染**，否则挂入后首帧不排 | ★ 我误把 private 方法当业务入口 | D41-6 重写（含两条实现要求）；D41-8 订正；新增 A41-10 |
| **P1-4** | `CollapsiblePanel` 收起态 `SetSize` 会把 `m_expandedRect` 覆写；`VerticalLayout:95-97` 的 `fillCrossAxis` 传 `(cross, 0)` ⇒ **展开基准被抹成 0** | ★ 我未发现的雷区 | N1 加预警；§7 闸门 7（**本 Phase 只登记不修**，属 #80） |
| **P2-1** | `Win32PlatformWindow::Show()` 有 `UpdateWindow`（`:464`）⇒ **同步**派发首帧 ⇒ 落地后 `Show()` 返回前已 Arrange | ★ 表述需订正 | §1.4 该行 + S11 补注 |
| **P2-2** | D41-4 可补 `SetWordWrap`（`TextWidget.cpp:104-119`，已有 `Invalidate`） | 边界补全 | D41-4 表加行 |

★ **另外采纳了 Gemini 提供的一条现网证据**（我已亲核成立）：`ModelProbe.cpp:326-333` 的搜索过滤只调 `SetVisible`、不调 `Arrange`，而容器是 `ListLayout` ⇒ 过滤后行留空白。这是契约缺口的**活体实例**，写入 **§1.3.1**。★ 它同时是 S13② 的因果后果——**`phase25` B2 的错误描述直接生产了这处 bug**。

★ **同时自我记录一处方法论教训**：v1.0 的「零守卫阻力」与「三个 override」两处错，根因同一个——**用「我记得的」代替「全库清点」**。前者 grep 词表不含 `RegionDirty`，后者没做 `grep -rn "SetSize(int w, int h)"`。这与 Phase 40 I-2（机检词表要覆盖注释）同族：**判据的词表本身就是判据的一部分**。

### v1.2（2026-10-29，两项核心裁决拍板）

★★ **用户拍板：D41-1 = ⓑ（setter 置脏 + 帧首按需结算）、D41-11 = 方案 A（几何 setter 连带 `Invalidate`）**。两者连同四项配套全部转为定案：

| 决策 | 定案 |
|---|---|
| **D41-1** | **ⓑ** —— setter 置脏、`PaintFrame` 帧首按需结算 |
| **D41-2** | **ⓐ 根单标志 + 沿父链上溯**（同 `Invalidate()` 既有模式）；ⓒ 代数比较留作详设后备 |
| **D41-3** | **ⓑ 抑制 + ⓐ 结算后清根脏**；★ **抑制范围含 `Invalidate`**（v1.2 新增细化） |
| **D41-3.2** | **根 `Arrange` 完成即清根脏；子树 `Arrange` 不清** |
| **D41-4** | **门控**：`SetSize` 需 `m_layout \|\| parent->m_layout`；`SetPosition`/`SetVisible`/`SetStretch` 需 `parent->m_layout`；`AddChild`/`RemoveChild`/`SetLayout` 需 `m_layout` |
| **D41-11** | **方案 A**：值真变化 + 不在结算期 ⇒ 置布局脏（受门控）+ `Invalidate()`；配套翻转 1 条存量用例 |

★★★ **v1.2 最重要的新推论：两条通道的触发判据不同**（由 ⓑ + A 组合逼出，v1.1 及之前都没讲清）：

- **重绘失效**（`Invalidate()`）的判据 = **值真的变了**（同值守卫通过）；
- **布局失效**（置布局脏）的判据 = 值变了 **且** 某个 `Layout` 会读它（D41-4 门控）。

★ 这个区分**彻底解决了 P1-1**：`TextBox::OnPaint:1410` → `SyncScrollBar:755` → `m_vBar->SetVisible(need)` 会 `Invalidate()`（滚动条视觉确实变了），但**不置布局脏**（`TextBox` 无 `Layout`）⇒ 下一帧只重绘、不重排。而那一帧重绘本就是滚动条出现所必需 ⇒ 自洽，无需特判。

⇒ 契约注释必须**把两条判据分开写**，不能再用「不 Arrange、不 Invalidate」这种并列表述——那正是 `Widget.h:148` 原文的病根（§1.6）。

★ **连带影响清单**（详设据此展开）：
1. `TextBox::SetSize`（`TextBox.cpp:261`）末尾的 `Invalidate()` 变**冗余**（基类已做）⇒ 收敛；
2. `ScrollView.cpp:337` 注释「不 Invalidate：SetSize 由布局路径调用」**必须改写**（方案 A 下它不成立，但「结算期抑制」是它的正确版本）；
3. `Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`，断言拆两条：`RegionDirty() == true`（重绘通道）+ **布局脏仍为假**（门控通道）⇒ 后者同时成为 D41-4 的判据；
4. `Widget.h:147-149` / `:201-203` 的新表述必须体现「两条判据分开」。
