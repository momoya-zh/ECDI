# Phase 9.6 Panel 容器语义变更：默认透明 + 输入透传 + CollapsiblePanel 默认收起 —— 详细设计

| 项目 | 内容 |
|---|---|
| 版本 | v1.1 |
| 日期 | 2026-08-30 |
| 状态 | **已实施（2026-08-30，GPT 评审全项授权 + 用户确认）——待用户编译验证** |
| 关联文档 | phase9-theme-system-detailed-design.md v1.6 / phase9.6-collapsiblepanel-detailed-design.md v1.1 |

**修订记录**

| 版本 | 变更 |
|---|---|
| v1.0 | 初版 |
| v1.1 | GPT 评审吸收：§3.3 增收起态 SetSize 轴语义**硬契约**（非动画轴保持 / 动画轴为 0）；初始化约定明确为「文档约定、不做运行时 assert」；§6.2 测试补四向轴断言 |
| 实施 | 2026-08-30 用户授权，按 §8 顺序落地全部 9 项文件；待用户编译验证（AI 仅静态自检） |

---

## 1. 需求背景

demo 美化准备中确认三项默认行为变更需求：

1. Panel 默认背景应为 RGBA(0,0,0,0)——镶板本质是「布局容器」，默认应隐形；
2. Panel 默认应跳过鼠标按下——鼠标事件只由子控件接收（上次 panel1 重叠挡住 textBox 的命中问题即源于此）；
3. CollapsiblePanel 默认应收起——demo 场景初始隐藏内容，点击 header 展开。

## 2. 需求确认

### 2.1 范围

- **R-A** Panel 默认背景色 = `Color::FromRGBA8(0, 0, 0, 0)`（经 DefaultTheme 生效，非硬编码）。
- **R-B** Panel 输入透传：Panel 自身**永不**参与命中测试（`ContainsPoint` 恒 false），子控件命中路径完全不受影响。
  - 决策记录：初案为「命中与背景 alpha 联动（a>0 才命中）」，用户否决——镶板语义即纯容器，自身不需要鼠标事件；**最终定案：永不命中、无任何开关**（YAGNI，需求出现再加）。
- **R-C** CollapsiblePanel 默认收起（`m_expanded = false`），并补齐收起态 `SetSize` 语义（见 §3.3——否则首次展开时 `AxisTarget()==0` 展不开）。

### 2.2 非目标

- demo 的 main.cpp 改版（后续单独授权）；
- 收起态下 `SetPosition` 同步展开基准（记为已知限制，§10）；
- Panel 命中开关 API（`SetHitTestVisible` 之类）；
- 内容容器独立于面板尺寸的布局（容器继续跟随面板尺寸，收起态为 0 高/宽）。

## 3. 详细设计

### 3.1 R-A：Panel 默认背景透明

**DefaultTheme.cpp** `GetPanelStyle()`：

```cpp
PanelStyle DefaultTheme::GetPanelStyle() const{
	PanelStyle s;
	s.background.value = Color::FromRGBA8(0, 0, 0, 0);   // 默认透明（2026-08-30：镶板 = 隐形布局容器）
	return s;
}
```

**Panel.cpp** `OnPaint` 加 alpha 短路（纯性能优化，避免发出全透明 GDI 合成命令）：

```cpp
void Panel::OnPaint(PaintContext& ctx, int x, int y){
	// 决策 6：最终坐标 x/y + GetWidth/GetHeight
	// 默认透明（a==0）时跳过 DrawRect——PushClip/PopClip 仍由 Widget::Paint 基类管线发出
	if (m_style.background.value.a > 0.0f){
		ctx.DrawRect(Rect{ ... }, m_style.background.value);
	}
}
```

行为推论：默认构造的 Panel 绘制命令流 = `PushClip → PopClip`（size 2，无 DrawRect）；`SetStyle` 设色后恢复 `PushClip → DrawRect → PopClip`（size 3）。

### 3.2 R-B：Panel 输入透传（镶板 = 纯容器）

`Widget::ContainsPoint` 是 `virtual`（Widget.h:241），且 `Widget::HitTest` 为**子优先递归**（先逆序遍历 children，子全部未命中才测自身）——因此只需一处 override：

```cpp
// Panel.h（public 区）
/// @brief Panel 永远自身不命中（镶板 = 纯容器语义，2026-08-30 定案）
/// @details HitTest 子优先递归——鼠标事件由子控件接收，Panel 自身矩形不拦截任何输入。
/// 无开关（YAGNI）。
bool ContainsPoint(int x, int y) const noexcept override;

// Panel.cpp
bool Panel::ContainsPoint(int x, int y) const noexcept{
	return false;
}
```

要点：

- 子控件命中不受影响：HitTest 先递归 children，命中即返回，永远走不到 Panel 自身分支；
- CollapsiblePanel 继承此行为：其自身不命中，但 `m_content` 子树照常命中，外部 header Button（兄弟/外部自组）不受影响；
- 与 D7/ApplyTheme 无交互：`ContainsPoint` 不读样式，恒 false，不受 SetStyle/ApplyTheme 影响。

### 3.3 R-C：CollapsiblePanel 默认收起

**问题**：直接把 `m_expanded` 初值改 false 不够——构造后 `m_expandedRect` 为空 `{}`，首次展开时 `AxisTarget()==0`，展不开；且收起态下外部 `SetSize(300,400)` 会把面板直接设成 300×400 全尺寸，破坏「收起 = 沿锚定边收缩到 0」语义。

**改动 1 —— 状态初值**（CollapsiblePanel.h/.cpp）：

```cpp
bool m_expanded = false;   ///< 展开状态（默认收起——2026-08-30 变更）
```

构造函数：`SetContentVisible(true)` → `SetContentVisible(false)`。

**改动 2 —— SetSize 收起态语义**：外部 `SetSize` 在收起态下含义 = **定义展开基准尺寸**，实际呈现保持收缩：

```cpp
void CollapsiblePanel::SetSize(int w, int h){

	if (!m_expanded){
		// 收起态：SetSize 定义「展开基准」，实际呈现保持收缩（锚定边固定）
		m_expandedRect = Rect{(float)GetX(), (float)GetY(), (float)w, (float)h};
		ApplyGeometry(0.0f);
		return;
	}

	Panel::SetSize(w, h);
	if (m_content) m_content->SetSize(w, h);

}
```

**硬契约（收起态 SetSize 轴语义，GPT 评审 v1.1 补充）**：收起态 `SetSize` 只定义展开基准，呈现几何按轴拆分——**非动画轴保持 SetSize 给出值，动画轴呈现 0**：

| 方向 | 非动画轴（= SetSize 给出值） | 动画轴（呈现 0） |
|---|---|---|
| Down / Up | 宽度 = w | 高度 = 0 |
| Left / Right | 高度 = h | 宽度 = 0 |

即 Down 方向 `SetSize(300, 400)` 呈现 `(w=300, h=0)`，**不得**呈现 `(300, 400)`。该契约由测试断言固化（§6.2——现有 DownCollapse 的 `w==300` / RightCollapse 的 `h==400` 断言即其载体，新增 DefaultCollapsedPresentation 补全对称断言）。

**改动 3 —— ApplyGeometry 防递归**：内部 `SetSize(...)` 调用改为直调 `Panel::SetSize(...) + m_content->SetSize(...)`：

```cpp
// 原：SetSize(w, s);          // 虚分派进 override → 收起态会用 (w,0) 污染 m_expandedRect
// 改：Panel::SetSize(w, s);  m_content->SetSize(w, s);   // 绕开 override，行为等价（override 本就是这两个调用）
```

四处方向分支同改。此改动同时是改动 2 能安全调用 `ApplyGeometry(0.0f)` 的前提。

**API 时序约定（语义增强）**：

- `SetExpandDirection` 由「建议在首次折叠前设置」升级为**须在首次 SetSize 前设置**——方向决定收缩呈现的作用轴（Down/Up = 高度轴，Left/Right = 宽度轴），收起态 SetSize 按当前方向计算收缩呈现；
- 收起态下 **先 `SetPosition` 后 `SetSize`**——SetSize 用当前 `GetX()/GetY()` 记基准位置；先 SetSize 后 SetPosition 会导致基准位置陈旧、展开时跳回（§10 已知限制）。
- 上述两条均为**初始化约定**（文档 + 头文件注释表达），**不引入运行时 assert / 强制校验机制**——「用户按约定初始化即可」（GPT 评审 v1.1 确认，YAGNI）。

### 3.4 典型序列推演（默认收起，无窗口降级 = 测试路径）

```
构造            m_expanded=false, content invisible, m_expandedRect={0,0,0,0}, 面板 0×0
SetPosition(100,200)   面板 (100,200,0,0)
SetSize(300,400)       收起态分支：基准 = (100,200,300,400)
                       ApplyGeometry(0)：Down → SetPosition(100,200) + Panel::SetSize(300,0)
                       面板呈现 (100,200,300,0)，内容容器 (300,0)
SetExpanded(true)      m_expanded=true；SetContentVisible(true)
                       ApplyGeometry(400)：SetPosition(100,200) + SetSize(300,400)
                       面板 (100,200,300,400) ✓ 基准恢复
Toggle()               SetExpanded(false)：m_expanded==true → 记基准 (100,200,300,400)（未变）
                       ApplyGeometry(0) → 收缩 (100,200,300,0)，动画结束隐藏内容
```

## 4. 行为变更矩阵

| 场景 | 变更前 | 变更后 |
|---|---|---|
| `Panel` 默认绘制 | 灰底 DrawRect（命令流 size 3） | 透明，无 DrawRect（size 2） |
| `Panel` 默认命中 | 自身矩形可命中（挡鼠标） | 永不命中（透传子控件） |
| `Panel` SetStyle 设色后绘制 | DrawRect 自定义色 | 同左（不变） |
| `Panel` SetStyle 设色后命中 | 可命中 | **仍不命中**（语义变化点） |
| `CollapsiblePanel` 初始状态 | 展开（SetSize 后即全尺寸） | 收起（SetSize 定义基准，呈现收缩到 0） |
| `CollapsiblePanel` 收起态 SetSize | 设置面板实际尺寸（破坏收起语义） | 定义展开基准 + 保持收缩呈现 |
| `CollapsiblePanel` Toggle 首次调用 | 折叠 | **展开** |

**兼容性提示**：这是破坏性默认值变更。既有代码若依赖 Panel 灰底或 Panel 自身拦截鼠标，须改为显式 `SetStyle` 设色 / 显式换用 `Widget`。当前仓库内 main.cpp 的 panel1 已显式设色，无隐性依赖。

## 5. 影响面（文件清单）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/Theme/DefaultTheme.cpp` | GetPanelStyle 默认背景 → FromRGBA8(0,0,0,0) |
| 2 | `include/ECDI/Widget/Panel.h` | 声明 ContainsPoint override + 类注释补输入透传语义 |
| 3 | `src/Widget/Panel.cpp` | ContainsPoint 实现（恒 false）+ OnPaint alpha 短路 |
| 4 | `include/ECDI/Widget/CollapsiblePanel.h` | m_expanded=false + IsExpanded/构造注释 + SetExpandDirection 时序注释 |
| 5 | `src/Widget/CollapsiblePanel.cpp` | 构造 SetContentVisible(false) + SetSize 收起态分支 + ApplyGeometry 直调防递归 |
| 6 | `src/Tests/WidgetTests.cpp` | TestPanelPaint / TestPanelSetStyle 更新 + 新增透传用例（§6） |
| 7 | `src/Tests/CollapsiblePanelTests.cpp` | 7 条更新 + 新增默认收起用例（§6） |
| 8 | `docs/phase9-theme-system-detailed-design.md` | v1.6 → v1.7（默认透明 + 输入透传） |
| 9 | `docs/phase9.6-collapsiblepanel-detailed-design.md` | v1.1 → v1.2（默认收起 + SetSize 收起语义） |

不改动：main.cpp（demo 后续单独授权）、vcxproj（无新增文件）、RunAllTests（无新增注册函数——新用例挂入现有 Register* 函数）。

## 6. 测试计划

### 6.1 WidgetTests.cpp

| 测试 | 改动 |
|---|---|
| `Widget.PanelPaint` | 默认透明 → 断言命令流 size==2（PushClip+PopClip、无 DrawRect）；DrawRect 内容断言移至 SetStyle 设色场景 |
| `Widget.PanelSetStyle` | 「覆盖前灰底 r==0.5」断言改为「覆盖前无 DrawRect（透明短路）」；覆盖后断言不变 |
| **新增** `Widget.PanelInputPassThrough` | ① Panel 自身区域 HitTest 返回 nullptr；② Panel 的子控件区域 HitTest 返回子指针（透传验证） |

### 6.2 CollapsiblePanelTests.cpp（13 条逐条）

| 测试 | 处置 | 说明 |
|---|---|---|
| DefaultExpanded | **改** | `IsExpanded()` 断言 true → false；改名/注释体现默认收起 |
| DownCollapse | 免改 | 新语义下 SetSize 即呈现收起，SetExpanded(false) 幂等；全部断言仍通过 |
| UpCollapse | **改** | `SetExpandDirection(Up)` 须移到 `SetSize` **前**（方向决定收缩呈现轴；原顺序下 SetExpanded(false) 幂等 no-op，y 停在 200 ≠ 600） |
| RightCollapse | **改** | 同上（方向前置；h 断言依赖 Right 收缩呈现） |
| LeftCollapse | **改** | 同上（x 断言依赖 Left 收缩呈现） |
| ExpandRestoresRect | 免改 | 方向已先于折叠设置；展开回基准推演通过 |
| ExpandAfterResize | 免改 | 首次 SetSize 即定义基准 A；后续流程不受影响 |
| Idempotent | 免改 | 幂等语义不变 |
| ToggleFlipsState | **改** | 语义反转：首次 Toggle = 展开（true, h=400），再 Toggle = 折叠（false, h=0） |
| ContentContainer | **改** | 容器尺寸断言（500×600）须在展开态查——收起态容器跟随面板为 500×0 |
| CollapseHitTestPanelSelf | 免改（注释更新） | 断言仍通过；命中失效理由更新为「Panel 永不命中」 |
| ContentFollowsGeometry | **改** | 前半段几何断言移到 SetExpanded(true) 之后（收起态 content 高度为 0） |
| RepeatedCollapseMemory | 免改 | 基准记忆推演通过（展开→折叠跃迁记录完整几何） |

**新增** `CollapsiblePanel.DefaultCollapsedPresentation`：构造 → SetPosition → SetSize → 断言 IsExpanded()==false、呈现收缩（Down 下 **h==0 且 w==300 非动画轴保持**）、SetExpanded(true) 后恢复基准全尺寸、内容可见；另加 Right 方向对称断言（SetExpandDirection(Right) → SetSize(300,400) → **h==400 保持、w==0**）——硬契约 §3.3 的测试载体。

### 6.3 验证方式

AI 仅静态自检（含 BOM `ef bb bf` 校验）；编译与测试由用户在 VS / CLion+CMake（MSVC 工具链）执行。

## 7. 已知限制与记账

1. 收起态下外部 `SetPosition` 不同步展开基准 → 展开时位置跳回基准——v0.1「动画中外部 SetPosition 不支持」限制的延伸，记账不处理；
2. 动画中外部 SetPosition/SetSize 不支持（既有限制不变）；
3. HitTest 不校验父级裁剪（既有架构债，不变）；
4. Panel 永不命中为类固定语义，无开关——若未来出现「有底色面板需拦截拖拽」场景，届时再议（YAGNI）。

## 8. 实施顺序（授权后）

1. DefaultTheme.cpp + Panel.h/.cpp（R-A + R-B）
2. WidgetTests.cpp 更新 + 新增
3. CollapsiblePanel.h/.cpp（R-C）
4. CollapsiblePanelTests.cpp 更新 + 新增
5. 头文件注释同步 + 两份主详设修订（v1.7 / v1.2）+ 本文件状态更新
6. 静态自检 + BOM checkpoint → 交用户编译验证

## 9. 待确认决策点

1. R-B「永不命中、无开关」定案确认（§2.1 决策记录）；
2. 行为变更矩阵中两条破坏性变更（Panel 设色后仍不命中 / Toggle 首次语义反转）是否接受；
3. §5 文件清单（9 项）是否全部授权。
