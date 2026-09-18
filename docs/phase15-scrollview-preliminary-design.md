# Phase 15 滚动容器（ScrollView + 滚动条）初步设计

> 状态：v1.0（2026-09-18）｜初步设计——🚧 待评审
> 前序：需求确认 v1.1 ✅（2026-09-18 二轮外部评审通过，「可进初设」）
> 上游：[phase15-scrollview-requirements.md](phase15-scrollview-requirements.md)（**§1.4 设计不变量为本稿的强约束**）
> 相关：phase9.5-r1-clip-detailed-design.md（Clip 能力）/ phase9.6-panel-container-semantics-detailed-design.md（Panel 语义）/ phase13-captionbar-preliminary-design.md（`ConsumesMouseInput` 接缝先例）/ roadmap-deferred.md §7.5

---

## 1. 范围映射（需求 → 设计域）

| 需求 | 设计域 | 本稿落点 |
|---|---|---|
| **R1** ScrollView 内核 | `Widget` 坐标接缝（3 处消费）+ `ScrollView` 类 | §2.1 / §2.2 / §3.1 / §3.3 |
| **R2** 滚动条 | `ScrollBar` 控件 + `ScrollBarStyle` + `Theme` 接入 | §2.3 / §2.4 / §2.5 / §2.6 / §3.4 |
| **R3** 命中可见性约束 | `Widget::ClipsChildren()` + `HitTest` 判定序 | §2.1 / §3.2 |
| **R4** ModelProbe 接入 | `ModelListPanel` → `ScrollView` 迁移映射 | §3.7 |
| **R5** 测试承载 | `ecdi_tests` 新用例 | §7 |

**贯穿约束（需求稿 §1.4）**：① 布局位置永不随滚动变化 ② 自身几何/`PushClip` 用**视口**矩形、绝不叠自身偏移 ③ `Paint` / `HitTest` / `GetAbsolutePosition` **三处同变换** ④ 职责四层（Widget=坐标+裁剪接缝 / Layout=排布不参与偏移 / ScrollView=viewport+offset+extent / ScrollBar=展示+输入）。

---

## 2. 头全文草案

### 2.1 `ECDI/include/ECDI/Widget/Widget.h`（增量：D1 + D2 两个接缝）

```cpp
	// ── 内容偏移接缝（Phase 15 D1-A：视口偏移——内容坐标 → 视口坐标的变换）──
	// 语义：本控件「子内容」相对视口的位移（正 = 内容被向左/向上推出）。
	// ⚠️ 不影响自身：自身 m_geometry 与 Paint 的 PushClip 一律用**视口矩形**，绝不叠加本偏移
	//    （否则容器会随内容一起移出视口——需求稿 §1.4(2)）。
	// ⚠️ 三处必须同变换：Paint（子偏移）/ HitTest（子局部坐标）/ GetAbsolutePosition（父链累加）
	//    ——需求稿 §1.4(3) 的**充要条件**；漏一处即产生「看得见点不到 / 点得到看不见 / Popup·IME 错位」。
	// 默认 0 ⇒ 非容器控件零回归（既有行为逐位不变）。

	/// @brief 内容偏移 X（默认 0——仅滚动容器 override 返回非 0）
	[[nodiscard]] virtual int GetContentOffsetX() const noexcept { return 0; }

	/// @brief 内容偏移 Y（默认 0）
	[[nodiscard]] virtual int GetContentOffsetY() const noexcept { return 0; }

	/// @brief 是否把子节点的命中约束在本控件矩形内（Phase 15 D2——默认 false 零回归）
	/// @details 语义：**命中点**须落在本控件矩形内，否则**不递归其子树**——不是「限制子控件几何」，
	/// 也不是「逐层传递裁剪域」。检查发生在**递归子节点之前**；逐层检查自动等价于
	/// 「所有祖先裁剪域的交集」（与 Paint 的嵌套 PushClip 同构）。
	/// ⚠️ **不得用 ContainsPoint 实现本门控**：`Panel::ContainsPoint` 恒 `false`（`Panel.h:45`），
	///    拿它当门会**拦死 Panel 自己的整棵子树**。须用独立矩形判定 `[0,GetWidth()) × [0,GetHeight())`。
	/// 仅需裁剪命中的容器 override 返回 true（Phase 15 = `ScrollView`）。
	[[nodiscard]] virtual bool ClipsChildren() const noexcept { return false; }
```

> **两处均为非纯虚**（带默认实现）⇒ **零破坏**：无需任何既有派生类同步（skill 条 33 的「实现者清单」问题不成立）。
> **同时修正** `Widget.h:222` 的过期注释：`GetAbsolutePosition` 的用途已从「TextBox 光标 / ScrollBar / Popup / Tooltip **未来用**」改为**现状陈述**（TextBox 已在用 3 处，见 §5）。

### 2.2 新建 `ECDI/include/ECDI/Widget/ScrollView.h`（Public 头 **89 → 90**）

```cpp
#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

class ScrollBar;
class MouseWheelEvent;

/// @brief 滚动容器（Phase 15 R1——视口 + 内容偏移 + 内容范围 + 滚轮）
/// @details **继承 `Widget` 而非 `Panel`**（D4）：`Panel::ContainsPoint` 恒 false（输入完全透传），
/// 而 ScrollView **必须自身可命中**——否则空白区滚轮无效（`Application::OnMouseWheel` 在
/// `target == nullptr` 时直接 return）。两者语义相反，复用会制造概念绑架。
///
/// **职责边界（§1.4(4) 职责四层——禁止本类变万能容器）**：
/// 本类**只做** viewport + offset + extent + 滚动交互：
/// - **不接管** `Arrange`（偏移不参与布局——D1 选 A 的核心理由）；
/// - **不提供**样式（背景/圆角/边框属 `Panel`；需要装饰时用一个外层 `Panel` 包住本控件）；
/// - **不推导** 内容尺寸以外的语义（不做 ListBox/虚拟化/框选——见需求稿 §4）。
///
/// 树结构（内容与装饰分离）：
/// ```
/// ScrollView（视口）
/// ├── content 容器（尺寸 = 内容 extent；子控件加在它下面）   ← GetContentView()->AddChild(...)
/// ├── 垂直 ScrollBar（靠右；条自身不参与 extent 推导）
/// └── 水平 ScrollBar（靠底；v1 默认创建但仅在需要时显示——见 §3.4）
/// ```
///
/// 使用（最小）：
/// ```cpp
/// auto sv = std::make_unique<ECDI::ScrollView>();
/// sv->SetSize(400, 300);
/// sv->SetScrollStep(28);                       // 一滚 28px（框架默认值见 O5）
/// sv->GetContentView()->AddChild(std::move(row1));
/// sv->GetContentView()->AddChild(std::move(row2));
/// sv->UpdateContentExtent();                   // D6：显式更新（不自动挂钩）
/// parent->AddChild(std::move(sv));
/// ```
class ScrollView : public Widget{

public:

	ScrollView();

	~ScrollView() override;

	// ── 内容 ────────────────────────────────────────

	/// @brief 内容容器（子控件加在此节点下；**非拥有**——树内节点）
	/// @details 其尺寸 = 内容 extent（`SetContentExtent` 同步），故子控件按**内容坐标系**布局。
	Widget& GetContentView() noexcept;
	const Widget& GetContentView() const noexcept;

	/// @brief 从内容容器的直接子控件重新推导 extent（D6——显式动作，**不自动挂钩**）
	/// @details 定义（需求稿 D6 钉死）：extent = 内容坐标系下二维包围盒右下角，负向钳 0——
	/// `w = max(0, max(x + child.GetWidth()))`，`h = max(0, max(y + child.GetHeight()))`。
	/// **不是** `Σ(子控件尺寸)`（间隔会漏算）。
	/// 副作用（原子完成，防「内容变了条还是旧长度」）：extent → maxOffset → **clamp 现有 offset**
	/// → 同步 `ScrollBar::SetRange` → `Invalidate`。
	void UpdateContentExtent();

	/// @brief 显式设置内容范围（不走子控件推导——内容由外部管理的场景）
	void SetContentExtent(int width, int height);

	/// @brief 内容范围（只读）
	[[nodiscard]] Size GetContentExtent() const noexcept;

	// ── 偏移 ────────────────────────────────────────

	/// @brief 设置内容偏移（内部 clamp 到 `[0, maxOffset]`——D11 单一真相源）
	void SetContentOffset(int x, int y);

	/// @brief 内容偏移 X（接缝实现——供 Paint/HitTest/GetAbsolutePosition 消费）
	[[nodiscard]] int GetContentOffsetX() const noexcept override;

	/// @brief 内容偏移 Y（接缝实现）
	[[nodiscard]] int GetContentOffsetY() const noexcept override;

	/// @brief 最大化偏移（`max(0, contentExtent - viewportExtent)`——与 K11 手搓版同式）
	[[nodiscard]] int GetMaxOffsetX() const noexcept;
	[[nodiscard]] int GetMaxOffsetY() const noexcept;

	// ── 配置 ────────────────────────────────────────

	/// @brief 滚轮步长（像素；`offset -= delta / 120.0f * step`——与 `TextBox` 换算基准一致）
	/// @pre step >= 0
	void SetScrollStep(int step) noexcept;
	[[nodiscard]] int GetScrollStep() const noexcept;

	/// @brief 是否创建/显示滚动条（默认 true；false = 纯滚动形态）
	void SetScrollBarVisible(bool visible);
	[[nodiscard]] bool IsScrollBarVisible() const noexcept;

	// ── 滚动条访问（R2——非拥有，树内节点）──────────

	[[nodiscard]] ScrollBar* GetVerticalScrollBar() noexcept;
	[[nodiscard]] ScrollBar* GetHorizontalScrollBar() noexcept;

	// ── 接缝 ────────────────────────────────────────

	/// @brief 命中约束在视口内（D2 / R3）
	[[nodiscard]] bool ClipsChildren() const noexcept override { return true; }

protected:

	/// @brief 尺寸变化 → 重算 maxOffset + clamp 偏移 + 同步滚动条版式
	void SetSize(int w, int h) override;

	/// @brief 滚轮滚动（`Task`：`Application::OnMouseWheel` 已按 target→parent 冒泡，本类只滚自己）
	void OnMouseWheel(const MouseWheelEvent& event) override;

private:

	Widget* m_content = nullptr;		///< 内容容器（非拥有——子节点）
	ScrollBar* m_vBar = nullptr;		///< 垂直条（非拥有——子节点；父先于子析构）
	ScrollBar* m_hBar = nullptr;		///< 水平条（非拥有）

	int m_offsetX = 0;					///< 内容偏移 X（权威状态——D11）
	int m_offsetY = 0;					///< 内容偏移 Y
	int m_contentW = 0;					///< 内容范围宽（D6）
	int m_contentH = 0;					///< 内容范围高
	int m_step = 0;						///< 滚轮步长（构造注入框架默认值——见 O5）
	bool m_barsVisible = true;			///< 滚动条开关

};

}
```

### 2.3 新建 `ECDI/include/ECDI/Widget/ScrollBar.h`（Public 头 **90 → 91**）

```cpp
#pragma once

#include "ECDI/Theme/ScrollBarStyle.h"
#include "ECDI/Widget/Widget.h"

#include <functional>

namespace ECDI{

class Theme;

/// @brief 滚动条（Phase 15 R2——轨道 + 滑块，自绘 + 主题化）
/// @details 纯**展示 + 输入**层（D11）：**不持有**偏移权威，偏移由 `ScrollView` 裁决；
/// 本类只做「把 (contentExtent, viewportExtent, offset) 三元组画出来」+「把用户操作翻译成新 offset」。
/// 与 `ProgressBar` 同构（继承 `Widget` + 构造注入 Style + `ApplyTheme`/`SetStyle`）。
///
/// 能力消费：只需 `DrawRect`（轨道/滑块）+ `DrawRoundedRect`（圆角）——**全部是 Phase 8 既有能力**，
/// 零新增 `RenderCommand`（需求稿 §5：Backend 完全不知道「这里是滚动条」）。
class ScrollBar : public Widget{

public:

	enum class Orientation : unsigned char{
		Vertical = 0,		///< 垂直（轨道高 = 可用高；滑块沿 Y 移动）
		Horizontal = 1		///< 水平（轨道宽 = 可用宽；滑块沿 X 移动）
	};

	/// @brief 构造
	/// @param orientation 朝向（构造确定、无 setter——YAGNI）
	explicit ScrollBar(Orientation orientation = Orientation::Vertical);

	~ScrollBar() override;

	// ── 范围模型（三元组——D11：由 ScrollView 单向驱动）────

	/// @brief 设置范围（内容长度 / 视口长度——本类据此推导滑块长度与位置）
	void SetRange(int contentExtent, int viewportExtent);

	/// @brief 设置偏移（外部驱动；内部 clamp 到 `[0, MaxOffset()]`）
	void SetOffset(int offset);
	[[nodiscard]] int GetOffset() const noexcept;

	/// @brief 最大偏移（`max(0, contentExtent - viewportExtent)`）
	[[nodiscard]] int GetMaxOffset() const noexcept;

	/// @brief 朝向（只读）
	[[nodiscard]] Orientation GetOrientation() const noexcept;

	// ── 通知（D11：变化时直接通知容器——`std::function` 有 `CaptionButton` 先例）──

	/// @brief 偏移变化回调（用户拖拽/点击触发；`ScrollView` 构造时设置）
	void SetOnOffsetChanged(std::function<void(int)> callback);

	// ── Phase 9：主题与样式（D7 契约）────────────────

	void ApplyTheme(const Theme& theme);
	void SetStyle(ScrollBarStyleOverride override);

	// ── 接缝 ────────────────────────────────────────

	/// @brief **消费鼠标输入**（Phase 13 D9 语义——本类是交互控件，不是容器）
	/// @details `true` ⇒ 命中本条的鼠标不会让 `--borderless` 窗口被视为「可拖标题栏」。
	/// 与 `ScrollView`（保持 `false`，见 D4）形成对照：**条**是交互控件、**空白**是容器。
	[[nodiscard]] bool ConsumesMouseInput() const noexcept override { return true; }

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;
	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;
	void OnMouseButtonUp(const MouseButtonUpEvent& event) override;
	void OnMouseMove(const MouseMoveEvent& event) override;

	ScrollBarStyle m_style;			///< 样式（protected——测试派生类可访问，同 ProgressBar 先例）

private:

	/// @brief 滑块矩形（相对本控件原点；`Local` 后缀标明坐标系）
	[[nodiscard]] Rect ThumbRectLocal() const noexcept;

	/// @brief 轨道起点/长度（主轴，相对本控件原点）
	[[nodiscard]] int TrackStart() const noexcept;
	[[nodiscard]] int TrackLength() const noexcept;

	/// @brief 拖拽起点 → 新偏移（反推，见 §3.4）
	[[nodiscard]] int OffsetFromThumbStart(int thumbStart) const noexcept;

	Orientation m_orientation;			///< 朝向（构造确定）
	int m_contentExtent = 0;			///< 内容长度（主轴）
	int m_viewportExtent = 0;			///< 视口长度（主轴）
	int m_offset = 0;					///< 当前偏移（外部驱动）
	int m_dragGrabOffset = 0;			///< 拖拽时「鼠标落在滑块内的相对位置」（防跳动）
	bool m_dragging = false;			///< 拖拽中（Capture 由 Application 隐式接管）

	std::function<void(int)> m_onOffsetChanged;		///< 变化通知（可空）

};

}
```

### 2.4 新建 `ECDI/include/ECDI/Theme/ScrollBarStyle.h`（Public 头 **91 → 92**）

```cpp
#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Theme/StyleField.h"

#include <optional>

namespace ECDI{

/// @brief ScrollBar 专属样式（Phase 15——轨道/滑块三态色 + 圆角 + 厚度）
/// @details 与 `ProgressBarStyle` 同构（`StyleField` 携带 override 标志位——D7 契约）。
struct ScrollBarStyle{
	StyleField<Color> trackColor;			///< 轨道底色
	StyleField<Color> thumbColor;	 		///< 滑块常态色
	StyleField<Color> thumbHoverColor;		///< 滑块 hover 色
	StyleField<Color> thumbPressedColor;	///< 滑块按下/drag 色
	StyleField<float> cornerRadius;			///< 滑块圆角（0 = 直角）
	StyleField<int>   thickness;			///< 条厚度（占用视口的宽度/高度——**参与 viewportExtent 计算**，见 §3.4）
};

/// @brief ScrollBar 样式运行时覆盖（D7——Set() 标记 overridden，后续 ApplyTheme 不覆盖）
struct ScrollBarStyleOverride{
	std::optional<Color> trackColor;
	std::optional<Color> thumbColor;
	std::optional<Color> thumbHoverColor;
	std::optional<Color> thumbPressedColor;
	std::optional<float> cornerRadius;
	std::optional<int>   thickness;
};

}
```

> ⚠️ **`thickness` 放在 Style 里**（而非 `ScrollBar` 的成员常量）：它不是视觉细节，而是**参与 viewport 计算**的几何量（§3.4），必须与主题同源。这带来一个后果：**主题切换可能改变 maxOffset** ⇒ `ScrollView` 需在滚动条样式变化后重算（§6 待定项）。

### 2.5 `ECDI/include/ECDI/Theme/Theme.h`（增量：+1 纯虚）

```cpp
#include "ECDI/Theme/ScrollBarStyle.h"      // ← 新增

	virtual ScrollBarStyle GetScrollBarStyle() const = 0;   ///< Phase 15 ScrollBar 专属样式  // ← 新增
```

### 2.6 `ECDI/include/ECDI/Theme/DefaultTheme.h` / `src/Theme/DefaultTheme.cpp`（增量）

```cpp
// DefaultTheme.h
	ScrollBarStyle GetScrollBarStyle() const override;      ///< Phase 15   ← 新增

// DefaultTheme.cpp
ScrollBarStyle DefaultTheme::GetScrollBarStyle() const{
	// v0.1 默认视觉：中性灰轨道 + 稍亮滑块（三态）；厚度 12（细粒度滚动条观感）
	...
}
```

> **影响面极小（已 grep 实证）**：`Theme` 的**唯一实现者是 `DefaultTheme`**（`DefaultTheme.h:10` / `.cpp:82`），
> 全库**无 FakeTheme / 测试替身**（测试只调 `GetDefaultTheme()` 拿具体类型）⇒ 加纯虚**只改这一处**。
> ⚠️ 对照 skill 条 33：本次**不必**清点替身，因为清点结果为空——但**清点动作本身**已在 §5 完成。

### 2.7 构建登记（**零改动**——已核实）

`CMakeLists.txt` 用 `file(GLOB_RECURSE FRAMEWORK_SOURCES CONFIGURE_DEPENDS "ECDI/src/*.cpp")`
⇒ **新增 `.cpp` 自动入库，无需改构建脚本**（`CONFIGURE_DEPENDS` 会触发重新 configure）。
`ecdi_public_header_test`（每 Public 头一 TU）同理自动覆盖新增的 3 个头。

---

## 3. 实现分解

### 3.1 坐标变换三处（D1-A 核心——**必须完全展开，不能只写「减 offset」**）

**约定**：设控件在**父局部坐标**下的几何为 `g`，其视觉位置（相对父的视口）为 `p`，父的内容偏移为 `O`。
**不变式**：`p = g − O`（内容坐标 → 视口坐标的平移）。

```cpp
// ① Paint（Widget.cpp）——偏移沿子树**累加**
int x = offsetX + static_cast<int>(m_geometry.x);       // 自身视觉位置（不含自身偏移）
int y = offsetY + static_cast<int>(m_geometry.y);

ctx.PushClip(Rect{x, y, m_geometry.width, m_geometry.height});   // 视口矩形（§1.4(2)：不叠自身偏移）
OnPaint(ctx, x, y);

// ★ 子控件的视觉原点 = 自身视觉位置 − 自身内容偏移
const int childOriginX = x - GetContentOffsetX();
const int childOriginY = y - GetContentOffsetY();
for (auto& child : m_children)
    child->Paint(ctx, childOriginX, childOriginY);

ctx.PopClip();
```

```cpp
// ② HitTest（Widget.cpp）——与 Paint **反向**（子局部 = 父局部 − 子几何 + 父偏移）
//    ★ 裁剪门控必须在递归子节点**之前**（§3.2）
if (ClipsChildren() &&
    (x < 0 || y < 0 || x >= GetWidth() || y >= GetHeight()))
    return nullptr;

for (auto it = m_children.rbegin(); it != m_children.rend(); ++it){
    Widget* child = it->get();
    const int localX = x - static_cast<int>(child->m_geometry.x) + GetContentOffsetX();
    const int localY = y - static_cast<int>(child->m_geometry.y) + GetContentOffsetY();
    if (Widget* target = child->HitTest(localX, localY))
        return target;
}
if (ContainsPoint(x, y)) return this;
return nullptr;
```

```cpp
// ③ GetAbsolutePosition（Widget.cpp）——父链累加时**逐层减去父的内容偏移**
Point pos{ static_cast<float>(GetX()), static_cast<float>(GetY()) };
const Widget* parent = m_parent;
while (parent){
    pos.x += static_cast<float>(parent->GetX() - parent->GetContentOffsetX());
    pos.y += static_cast<float>(parent->GetY() - parent->GetContentOffsetY());
    parent = parent->GetParent();
}
```

**三处同构性检验（§1.4(3) 的充要条件）**：

| 处 | 变换式 | 默认偏移 0 时 |
|---|---|---|
| `Paint` | `子视觉原点 = 自身视觉 − 自身偏移` | 退化为现状 `offsetX + geometry.x` ✓ |
| `HitTest` | `子局部 = 父局部 − 子几何 + 父偏移` | 退化为现状 `x - child.geometry.x` ✓ |
| `GetAbsolutePosition` | `+= 父几何 − 父偏移` | 退化为现状 `+= parent->GetX()` ✓ |

⇒ **三者默认路径逐位等价于现状** = 零回归的**结构保证**（不依赖用例覆盖）。

**数值自检（多层嵌套——符号/累加验证）**：
设 `Root`（客户区原点 0）→ `ScrollView`（几何 y=100，局部）→ `content`（几何 y=0）→ `row3`（几何 y=84）。
ScrollView 的 `offsetY = 50`，content 与 row3 的偏移均为 0。

| 量 | 计算 | 值 |
|---|---|---|
| row3 在 content 局部 | `g = 84` | 84 |
| content 在 ScrollView 局部 | `p = g − O_scroll = 0 − 50` | **−50** |
| row3 在 ScrollView 局部 | `p = g − O_content = 84 − 0` | 84 |
| row3 **视觉**（客户区） | `Root 0 + ScrollView(100) − 50 + content(0) + 84` | **134** ✓（= 100 + 84 − 50） |
| 反查：点 `y = 134` 能否命中 row3 | `ScrollView` 局部 `= 134 − 100 = 34`；门控 `34 ∈ [0, 视口高)` ✓；`row3` 局部 `= 34 − 0 + 0 − 84 = −50`… | 见下 |

⚠️ **上表最后一行为什么要小心**：`HitTest` 收到的是 **ScrollView 局部 34**，而 `row3` 的局部应得 `0`
（点恰在 row3 顶边）。差异来自 `34 − 0 + 0 = 34` 而 `row3.geometry.y = 84` ⇒ `34 − 84 = −50` **不命中**。

**根因**：`row3` 是 **content 的子**，content 的偏移为 0，所以点必须先经 content 的偏移换算——
而 **content 自身不做偏移**，它的子 `row3` 的视觉位置是 `content 视觉 + 84`。
content 视觉 = `100 − 50 = 50`（客户区）⇒ row3 视觉 = `50 + 84 = 134` ✓（与上表一致）。

**但 `HitTest` 从 ScrollView 收到 34（局部）**，ScrollView 把它减去**自己的偏移**后传给 content：
`34 + 50 = 84` —— 即 content 局部 84 ✓ 正是 row3 的几何起点 ✓ **命中**。

⇒ **正确式**（`localX = x − child.geometry + 自身偏移`，注意是 **加**偏移）：

```
ScrollView.HitTest(34)  →  content 局部 = 34 − 0 + 50 = 84
content.HitTest(84)     →  row3  局部 = 84 − 84 + 0 = 0  ∈ [0, h) ✓ 命中 row3
```

**结论（本节的实质产出）**：`Paint` 用**减**（传给子的原点左移）、`HitTest` 用**加**（收到的局部坐标先加回偏移），
两者方向相反但同源。**这是最容易写错符号的地方**——详设须为每一处给出正负号，并用上表这样的数值实例回归。

### 3.2 `ClipsChildren()` 判定序（D2——**必须在递归之前**）

```
HitTest(x, y)
  ├─ !IsVisible() → nullptr          （既有）
  ├─ !IsEnabled() → nullptr          （既有）
  ├─ ★ ClipsChildren() && 点不在 [0,w)×[0,h) → nullptr   （新增——**不递归子树**）
  ├─ 逆序遍历子节点（子局部 = x − child.geometry + 自身偏移）
  └─ ContainsPoint(x,y) → this / nullptr
```

三条纪律：
1. **检查点在递归之前**——放后面等于没约束（子节点已命中）；
2. **判据是独立矩形判定**，不是 `ContainsPoint`（`Panel` 恒 false 会拦死自己的子树）；
3. **逐层检查即充分**——无需逐层传递裁剪域；但**偏移必须沿子树传递**（坐标变换，与 §3.1 同源）。

### 3.3 `ScrollView` 结构（含内容容器决策）

**内容容器**（§8 O1 的倾向方案）：`ScrollView` 的树为
`[content, 垂直条, 水平条]`，**内容子控件挂在 `content` 下**而非直接挂 `ScrollView` 下。

| 方案 | 形态 | 取舍 |
|---|---|---|
| X 直接子 | 内容与两条同为 `ScrollView` 的 children | `UpdateContentExtent` 须**排除滚动条**（特判）；`Arrange` 若设在 ScrollView 上会排到滚动条 |
| **Y 内容容器（倾向）** | 内容挂 `content` 子节点 | 内容/装饰天然分离；`Layout` 挂在 `content` 上（ScrollView 自身不参与布局——符合 §1.4(4)）；`extent` 推导无特判。代价 = 多一层 Widget 树 |

**`content` 容器的尺寸 = 内容 extent**（不是视口尺寸）⇒ 其子控件按**内容坐标系**正常布局。
⚠️ 这会让 `content` 的 `PushClip`（自身边界）是**内容矩形**而非视口——**不构成问题**：
`ScrollView` 已 `PushClip` 视口，嵌套交集 = 视口 ∩ 内容矩形 = 视口 ✓（K2 的既有语义）。

**`SetSize` override 的副作用**（视口变化时的原子序列）：
`基类 SetSize` → 重算 `maxOffset`（视口变了）→ `clamp` 偏移 → 重排 `content`/两条的几何 → 同步 `ScrollBar::SetRange`。

**`OnMouseWheel`**：`offset -= delta / 120.0f * step`（与 `TextBox.cpp:452` 同式）→ `clamp` → 同步条 → `Invalidate`。

### 3.4 `ScrollBar` 版式与拖拽（范围模型）

**范围三元组**（D11 单一真相源在 `ScrollView`）：

```
contentExtent  = 内容长度（主轴，由 ScrollView 派生自 extent）
viewportExtent = 视口长度（主轴，**扣除已显示的条**——见 §3.5）
offset         = 当前偏移（0 ≤ offset ≤ maxOffset）

maxOffset   = max(0, contentExtent - viewportExtent)
trackLen    = 本条可用长度
thumbLen    = max(kMinThumb, trackLen * viewportExtent / contentExtent)     // 比例映射
thumbStart  = trackLen == thumbLen ? 0
                                   : (trackLen - thumbLen) * offset / maxOffset
拖拽反推     offset = (thumbStart - trackStart) * maxOffset / (trackLen - thumbLen)
```

⚠️ **除零保护**：`maxOffset == 0`（内容 ≤ 视口）⇒ 滑块占满、无需显示本条（见 §3.5 判据）；
`contentExtent == 0` ⇒ `thumbLen = trackLen`（占满）。

**交互三件套**（`OnMouseButtonDown`）：
- 命中**滑块内** ⇒ `m_dragging = true` + 记录 `m_dragGrabOffset`（鼠标相对滑块起点——防拖拽瞬间跳动）；
- 命中**轨道空白** ⇒ 翻页（`offset ± viewportExtent`，clamp）+ `Invalidate`。

**拖拽**（`OnMouseMove`，仅 `m_dragging`）：`thumbStart = 鼠标主轴坐标 − m_dragGrabOffset` → `OffsetFromThumbStart` → `m_onOffsetChanged(newOffset)`。
**释放**（`OnMouseButtonUp`）：`m_dragging = false`。**Capture 由 `Application` 隐式接管**（既有语义——Down 捕获 / Up 释放）⇒ 拖出条外仍跟手 ✓。

### 3.5 双轴 viewport 定义（GPT 关注点 5——**本稿最危险的一处**）

**必须钉死**：`viewportExtent` 是"含条"还是"不含条"。定错 ⇒ **滚到底部时最后一行被水平条遮住**。

**v1 定案（倾向）**：

1. **条占位（reserve），不 overlay**——确定、可预测（overlay 属体验增强，记账）；
2. **「是否需要某条」的判据用未扣除的视口**：`contentExtent > GetHeight()/GetWidth()`（**单轮判定**，不做两轮收敛）；
3. **供 `maxOffset` / 滑块用的 `viewportExtent` = 扣除已显示条后的长度**：
   `viewportH = GetHeight() - (hBar 显示 ? 垂直条厚度 : 0)`；
4. **双轴交叉区不做缩角**（已记账）：水平条轨道宽 = 视口宽 − 垂直条厚。

⚠️ **已知误差（如实记账）**：单轮判定在**边界情形**会偏一位——内容宽度恰好在 `视口宽` 与 `视口宽 − 条厚` 之间时，
理论上需要水平条但单轮判据认为不需要（因判据用的是未扣除宽度）。**v1 接受**（记账），
若实测暴露可见缺陷，再引入"两轮判定"（第二轮用扣除后宽度复核）——属详设/实施期的收敛项。

### 3.6 `extent` 更新时机（GPT 关注点 6）

**唯一入口 + 原子副作用链**：

```
UpdateContentExtent()
  ├─ 推导 extent（＝ max(0, max(x+w)) / max(0, max(y+h))，遍历 content 的直接子控件）
  ├─ SetContentExtent（写 m_contentW / m_contentH）
  ├─ 重算 maxOffset
  ├─ ★ clamp 现有 offset（防「内容变短后 offset 悬空」——GPT 指出的隐性状态问题）
  ├─ 同步 ScrollBar::SetRange(contentExtent, viewportExtent)
  ├─ 重排 content 与两条的几何
  └─ Invalidate
```

**调用时机**：① 调用方显式调用（内容变化后）；② `SetSize`（视口变化时内部调）；③ **不自动挂钩**（与 9.8 的"不挂钩"原则一致）。
**已知缺口（记账）**：调用方**忘记**调 `UpdateContentExtent` 时，滚动条仍显示旧长度——v1 不做自动侦测（无 `Layout` 完成回调可用）。

### 3.7 R4：`ModelListPanel` → `ScrollView` 迁移映射

| 原（`ModelProbe.cpp:50-85`） | 新 |
|---|---|
| `class ModelListPanel : public Panel`（35 行） | **删除**，改用 `ScrollView` |
| `SetRows(std::vector<Widget*>)` | `sv->GetContentView()->AddChild(std::move(row))`（逐行） |
| `SetRowHeight(28)` + `OnMouseWheel` 按行滚 | `sv->SetScrollStep(28)`（**显式设置**——框架默认值不绑 Demo，D5 概念分离） |
| `ApplyLayout()`（手动 `SetPosition(0, i*rowH − offset)`） | **删除**（偏移由 ScrollView 处理——这正是要收回的代码） |
| `m_offset` + `clamp` | `ScrollView` 内部（`SetContentOffset` 自带 clamp） |
| 无滚动条 | 默认创建（`ScrollBarStyle` 主题化） |
| `PanelStyle`（背景/圆角/边框——继承自 `Panel`） | ⚠️ **`ScrollView` 无样式能力** ⇒ 需**外层 `Panel`** 包住（装饰分离，§8 O6） |

**验收判据**：滚轮仍**一滚一行**（28px）、行高与视觉不变、背景/圆角/边框保持、**总代码行数净减少**（35 行手搓逻辑消失）。

---

## 4. 契约

### 4.1 坐标契约（三处同变换）

| 契约 | 条文 |
|---|---|
| 布局位置不变性 | `GetX/GetY/GetGeometry` **永不**受偏移影响；`Arrange` 结果与滚动态无关 |
| 自身不受自身偏移 | `ScrollView` 的 `m_geometry` 与 `Paint` 的 `PushClip` 用**视口**矩形 |
| 视觉位置 | `GetAbsolutePosition` 返回**含**沿途偏移的客户区坐标 |
| 同变换 | `Paint`（减）/ `HitTest`（加）/ `GetAbsolutePosition`（减）三处同源；默认偏移 0 时逐位退化为现状 |

### 4.2 偏移与范围契约

| 契约 | 条文 |
|---|---|
| 偏移权威 | 只有 `ScrollView` 持有并可钳制；`ScrollBar::SetOffset` 亦走 clamp（但对权威只读） |
| clamp 不变式 | 任意时刻 `0 ≤ offset ≤ maxOffset`；`maxOffset` 变化时**立即**重新 clamp |
| extent 定义 | 二维包围盒右下角，负向钳 0；非 `Σ` |
| 更新时机 | 显式 `UpdateContentExtent()`；**不自动挂钩**（缺口记账） |

### 4.3 `ClipsChildren()` 契约

| 契约 | 条文 |
|---|---|
| 判据 | 「**命中点**在容器矩形内」——不是「限制子控件几何」、不是「传递裁剪域」 |
| 时机 | **递归子节点之前** |
| 实现约束 | **不得**用 `ContainsPoint`（`Panel` 恒 false 会拦死子树） |
| 默认 | `false` ⇒ 既有控件树命中语义**零变化** |
| 与 NCHITTEST 的耦合 | `Window::IsClientInteractiveAt` 亦走 `HitTest` ⇒ 本契约生效后，**越界内容不再让 caption 区失去拖拽**（D4 连带收益） |

### 4.4 失败模式与安全

| 场景 | 行为 |
|---|---|
| `contentExtent == 0` | `maxOffset = 0`；滑块占满；滚动无效（非错误） |
| `offset` 超出新范围 | 立即 clamp（不抛异常——与框架既有风格一致） |
| `SetScrollStep(负值)` | `FRAMEWORK_ASSERT(step >= 0)` + 忽略 |
| `m_onOffsetChanged` 为空 | 静默跳过（`ScrollBar` 可独立使用——D9） |
| `GetContentView()` 早于构造完成 | 不可能（构造函数内创建 content） |

---

## 5. 影响面（全库 grep 实证）

| 项 | 实测 | 影响 |
|---|---|---|
| **`Theme` 实现者** | **1 处**：`DefaultTheme`（`DefaultTheme.h:10` / `.cpp:82`）。**无 FakeTheme / 无测试替身** | 加 `GetScrollBarStyle()` 纯虚 ⇒ 只改 1 处 ✓ |
| **`ContainsPoint` override 者** | **1 处**：`Panel.h:45`（恒 false） | D2 **不碰** `ContainsPoint` ⇒ 零影响 ✓ |
| **`ConsumesMouseInput` override 者** | **4 处**：`Button.h:33` · `StateWidget.h:26` · `TextBox.h:34` · `CaptionButton.h:30` | `ScrollBar` 成为 **第 5 个**（+1）；`ScrollView` 保持默认 false ✓ |
| **`HitTest` 生产调用点** | **2 处**：`Application.cpp:185`（`FindTargetWidget`）/ `Window.cpp:257`（`IsClientInteractiveAt`） | 改一处实现 ⇒ 两处自动受益 ✓ |
| **`HitTest` 测试直调** | **4 处**：`WidgetTests.cpp:282/285` · `CollapsiblePanelTests.cpp:45/199` | 均为 `Panel` 派生（`ClipsChildren()==false`）⇒ 行为不变；**须列入回归清单** |
| **`OnMouseWheel` 实现者** | **5 处**（K18：基类虚 / `TextBox` / `ModelListPanel` / `EventRouter` / `Application`） | 本阶段**不改签名**（D7 已降级）✓ |
| **`GetAbsolutePosition` 消费者** | **5 处**（K10，全为视觉坐标） | 认偏移后**全部受益**（3 处换算链纠正）✓ |
| **`GetDefaultTheme()` 调用** | 24 处（生产 11 / 测试 5 / 声明注释 8） | 全部是整体 `ApplyTheme` ⇒ 新增 Style 方法**零影响** ✓ |
| **构建脚本** | `GLOB_RECURSE ... CONFIGURE_DEPENDS` | 新增 `.cpp` **自动入库** ⇒ `CMakeLists.txt` **零改动** ✓ |
| **Public 头** | 89 → **92**（+`Widget/ScrollView.h` +`Widget/ScrollBar.h` +`Theme/ScrollBarStyle.h`） | README 锚点实施后同步 |

**替换/新增文件清单（预计 8 文件）**：3 新头 + 3 新 `.cpp` + `Widget.h/cpp` 改 + `Theme.h` 改 + `DefaultTheme.h/cpp` 改 + `examples/ModelProbe/ModelProbe.cpp` 改。

---

## 6. 待定项（详设解决）

| # | 待定项 | 需在详设给出 |
|---|---|---|
| T1 | **`ScrollBar` 三态色的具体色值** | `DefaultTheme::GetScrollBarStyle()` 的常数值（参照 `ModelProbe` 暗色配色或中性灰） |
| T2 | **`kMinThumb` / 默认 `step` / 默认 `thickness` 的取值** | 常量表 + 依据（默认 step 不得绑 Demo——D5） |
| T3 | **`ScrollView` 的 `content` 节点类** | 复用 `Panel`（含样式）还是裸 `Widget`？（倾向裸 `Widget`——样式归外层 Panel，O6） |
| T4 | **`ScrollBar` 的 `ContainsPoint`** | 默认矩形是否够（轨道整条可点 ⇒ 够）；hover 命中是否需区分滑块/轨道 |
| T5 | **两条的几何重排规则** | 垂直条 `x = 视口宽 − 厚度`；水平条 `y = 视口高 − 厚度`；`SetScrollBarVisible(false)` 时如何回收空间 |
| T6 | **测试替身/探针** | `RecordingBackend` 命令断言如何验"内容被偏移"（命令坐标 vs 期望） |
| T7 | **`IsScrollBarVisible` 的语义** | "创建与否" vs "显示与否"（倾向后者：条始终存在、按需 `visible`） |

---

## 7. 测试方向

| 组 | 用例方向（详设给编号） |
|---|---|
| **坐标变换** | 默认偏移 0 ⇒ `Paint`/`HitTest`/`GetAbsolutePosition` 与现状逐位一致（**零回归锚**）；单层偏移下三处一致性；**多层嵌套**（ScrollView→content→row）的符号/累加 |
| **命中约束** | 点在该容器内 + 子越界 ⇒ 不命中越界子；点在容器外 ⇒ 整棵子树不命中（含深层）；**`Panel` 不受影响**（`ClipsChildren()==false` 回归） |
| **偏移与 clamp** | 滚轮步长换算（`delta=±120/±30/±240`）；到边界不再滚；`maxOffset` 变小后 offset 立即 clamp（§3.6 缺口用例） |
| **extent** | `max(y+h)` 而非 `Σ`（间隔场景 → 50 not 40）；负向钳 0；空内容 = 0 |
| **ScrollBar 版式** | 滑块长度/位置的比例；`maxOffset==0` 占满；拖拽反推；轨道点击翻页；拖拽中 Capture 跟手 |
| **主题** | `ApplyTheme` 注入 + `SetStyle` override 后 `ApplyTheme` 不覆盖（D7 契约，同 `ProgressBarTests` 先例） |
| **端到端** | ModelProbe 迁移后的行为等价（R4：一滚一行 / 行高 / 视觉） |

---

## 8. 开放决策点（含初设新发现）

### O1 内容与装饰的树结构：直接子（X）vs 内容容器（Y）
**倾向 Y**（§3.3）。理由：extent 推导无特判、`Layout` 挂 content 更自然、符合职责四层。**代价** = 多一层节点。
**待拍板**：是否接受多一层（影响 R4 迁移写法与 `GetContentView()` API）。

### O2 内容容器的尺寸语义
**倾向**：`content` 尺寸 = **内容 extent**（子控件按内容坐标系布局）。
**备选**：`content` 尺寸 = 视口尺寸（则子控件需自行按内容坐标布置，但 `PushClip` 语义更直白）。
⚠️ 取决于 O1 的结论——若选 X 则本项消失。

### O3 条占位 vs overlay，以及单轮/两轮判定
**倾向**：占位 + 单轮（§3.5），边界误差**记账**。
**待拍板**：是否接受单轮的边界误差（可能表现为"该出横条却没出"）。

### O4 `ScrollBar` 的通知形态
**倾向**：`std::function<void(int)>`——**已有既先例**（`CaptionButton` 构造收 `std::function` 回调）。
**备选**：`ScrollBar` 直接持 `ScrollView*`（耦合更紧，但少一层间接）。

### O5 框架默认 `step`
**倾向**：框架给**通用默认**（初设定值，与字号/行高无关）；`ModelProbe` 显式 `SetScrollStep(28)`。
**理由**：避免框架默认值被 Demo 视觉尺寸绑死（需求稿 D5 / GPT 修正）。

### O6 `ScrollView` 要不要样式能力？
**倾向**：**不要**——背景/圆角/边框属 `Panel`；需要装饰时外层包 `Panel`。
**理由**：§1.4(4) 职责四层（ScrollView = viewport+offset+extent，不是"面板"）。
⚠️ 与 R4 直接相关（`ModelListPanel` 原本继承 `Panel` 的样式）⇒ 迁移需加一层 Panel。

### O7 `ScrollBarStyle::thickness` 影响 `maxOffset` 的连锁
主题切换 ⇒ 厚度变 ⇒ `viewportExtent` 变 ⇒ `maxOffset` 变 ⇒ 需重算。
**倾向**：`ScrollView` 在 `SetSize` / `UpdateContentExtent` 时读取厚度（不做主题变化订阅——记账）。

### O8 `ScrollView` 是否 override `GetPreferredSize()`
**倾向**：**不 override**（容器不"希望"某尺寸——由使用者 `SetSize`/`SetStretch` 决定）。
**理由**：避免与 9.8 的尺寸协商语义纠缠（内容尺寸 ≠ 控件希望尺寸）。

---

## 9. 评审响应（v1.0）

> 本稿为初稿；外部评审意见待回填。

---

## 10. 修订记录

- v1.0（2026-09-18）初步设计初稿：§1 范围映射（R1–R5 → 设计域）· **§2 头全文草案 7 处**（`Widget.h` 两接缝增量 · 新建 `ScrollView.h` / `ScrollBar.h` / `ScrollBarStyle.h` / `Theme.h` 增量 / `DefaultTheme` 增量 / 构建零改动说明）· §3 实现分解（**坐标变换三处完全展开 + 数值自检** · `ClipsChildren` 判定序 · `ScrollView` 结构与内容容器 · `ScrollBar` 范围模型与拖拽 · **双轴 viewport 定义** · extent 更新时机 · R4 迁移映射）· §4 契约四组 · **§5 影响面 10 项 grep 实证** · §6 待定项 7 项 · §7 测试方向 7 组 · **§8 开放决策点 O1–O8**。落实需求稿 §1.4 四条不变量为强约束；**响应外部评审的 7 个关注点**（§3.1 变换展开 / §3.2 判定序 / §2.3 事件与命中优先（`HitTest` 逆序 ⇒ 条天然优先）· §3.4 范围模型 / §3.5 双轴定义 / §3.6 更新时机 / §2.3 + §4.3 `ConsumesMouseInput` 推演）。
