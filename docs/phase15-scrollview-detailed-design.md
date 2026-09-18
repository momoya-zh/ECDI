# Phase 15 滚动容器（ScrollView + 滚动条）详细设计

> 状态：v1.0（2026-09-18）｜详细设计——🚧 待评审
> 前序：需求确认 ✅ v1.2（2026-09-18）｜初步设计 ✅ v1.2（三轮外部评审通过「可进详设」）
> 上游：[phase15-scrollview-requirements.md](phase15-scrollview-requirements.md)（§1.4 四条不变量）· [phase15-scrollview-preliminary-design.md](phase15-scrollview-preliminary-design.md)（§9.1 评审基线澄清）
> 相关：phase12-windowchrome-detailed-design.md（**逐文件最小 diff 规格**先例）· phase13-captionbar-detailed-design.md（`ConsumesMouseInput` 接缝）

---

## 1. 实施总览

| # | 文件 | 动作 | 规模 | 备注 |
|---|---|---|---|---|
| 1 | `ECDI/include/ECDI/Widget/Widget.h` | 修改 | +3 虚方法 + 1 私有辅助 + 1 注释修正 | §2.1 |
| 2 | `ECDI/src/Widget/Widget.cpp` | 修改 | **4 处最小 diff** | §2.2 |
| 3 | `ECDI/src/Widget/ScrollContent.h` | **新建（内部头）** | ~35 行 | §2.3 |
| 4 | `ECDI/src/Widget/ScrollContent.cpp` | **新建** | ~20 行 | §2.4 |
| 5 | `ECDI/include/ECDI/Widget/ScrollView.h` | **新建（Public）** | ~120 行 | §2.5 |
| 6 | `ECDI/src/Widget/ScrollView.cpp` | **新建** | ~260 行 | §2.6 |
| 7 | `ECDI/include/ECDI/Widget/ScrollBar.h` | **新建（Public）** | ~110 行 | §2.7 |
| 8 | `ECDI/src/Widget/ScrollBar.cpp` | **新建** | ~230 行 | §2.8 |
| 9 | `ECDI/include/ECDI/Theme/ScrollBarStyle.h` | **新建（Public）** | ~40 行 | §2.9 |
| 10 | `ECDI/include/ECDI/Theme/Theme.h` | 修改 | +1 include +1 纯虚 | §2.10 |
| 11 | `ECDI/include/ECDI/Theme/DefaultTheme.h` | 修改 | +1 override 声明 | §2.10 |
| 12 | `ECDI/src/Theme/DefaultTheme.cpp` | 修改 | +1 实现（~14 行） | §2.10 |
| 13 | `examples/ModelProbe/ModelProbe.cpp`(+`.h`) | 修改 | 删 `ModelListPanel`（35 行）→ 接 `ScrollView` | §2.11 |
| 14 | `ECDI/src/Tests/ScrollViewTests.cpp` | **新建** | T15-1..N | §5 |
| — | `CMakeLists.txt` | **零改动** | — | `GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库（§2.12） |

**Public 头**：89 → **92**（+`Widget/ScrollView.h` +`Widget/ScrollBar.h` +`Theme/ScrollBarStyle.h`；`ScrollContent.h` 为 `src/` 内部头**不计入**）。

---

## 2. 逐文件最小 diff 规格

> 纪律（skill 条 42/43）：**只列真正要动的行** + 前后对照；未列出的行**一律保持现状**；顶层/成员级新增必须写明**落点**。

### 2.1 `ECDI/include/ECDI/Widget/Widget.h`

**落点 A**：新增 3 个虚方法——插在 **`AutoSize()` 段之后、`GetX()` 之前**（即 `// ── AutoSize` 段与 `int GetX()` 之间）。

```cpp
	// ── 内容偏移接缝（Phase 15 D1-A：视口偏移——内容坐标 → 视口坐标的变换）────
	// 语义：本控件「子内容」相对视口的位移（正 = 内容被向左/向上推出）。
	// ⚠️ 不影响自身：自身 m_geometry 与 Paint 的 PushClip 一律用**视口矩形**，绝不叠加本偏移。
	// ⚠️ 三处必须同变换：Paint（子偏移）/ HitTest（子局部坐标）/ GetAbsolutePosition（父链累加）。
	// 默认 0 ⇒ 非容器控件零回归（既有行为逐位不变）。

	/// @brief 内容偏移 X（默认 0——仅滚动容器的内容节点 override 返回非 0）
	/// @details **override 归属**：只有「内容坐标空间的根」（`ScrollContent`）才 override；
	/// `ScrollView` 自身**不得** override——否则滚动条作为直接子也被偏移（Phase 15 详设 §3.1）。
	[[nodiscard]] virtual int GetContentOffsetX() const noexcept { return 0; }

	/// @brief 内容偏移 Y（默认 0）
	[[nodiscard]] virtual int GetContentOffsetY() const noexcept { return 0; }

	/// @brief 是否把子节点的命中约束在本控件矩形内（Phase 15 D2——默认 false 零回归）
	/// @details 语义：**命中点**须落在此容器矩形内，否则**不递归其子树**——不是「限制子控件几何」，
	/// 也不是「逐层传递裁剪域」。检查发生在**递归子节点之前**；逐层检查自动等价于
	/// 「所有祖先裁剪域的交集」（与 Paint 的嵌套 PushClip 同构）。
	/// ⚠️ **不得用 ContainsPoint 实现本门控**：`Panel::ContainsPoint` 恒 `false`（`Panel.h:45`），
	/// 拿它当门会**拦死 Panel 自己的整棵子树**。实际判据走**非虚** `ContainsRect`（见 §2.2 ①）。
	[[nodiscard]] virtual bool ClipsChildren() const noexcept { return false; }
```

**落点 B**：`private:` 区新增非虚辅助——插在 `bool Contains(const Widget* widget) const noexcept;` **之后**。

```cpp
	/// @brief 矩形命中判定（**非虚**——供 `ContainsPoint` 默认实现与 `ClipsChildren` 门控共用）
	/// @details 抽出的动机：D2 门控**不能**走 `ContainsPoint`（虚、可被 `Panel` 覆盖为恒 false），
	/// 但两者应共享**同一份**判定表达式（`[0, width) × [0, height)`）——否则门控与命中语义会漂移。
	[[nodiscard]] bool ContainsRect(int x, int y) const noexcept;
```

**落点 C**（注释修正）：`GetAbsolutePosition` 的注释由「TextBox 光标 / ScrollBar / Popup / Tooltip **未来用**」改为现状陈述。

```cpp
	/// @brief 获取绝对坐标（父链累加，**含沿途内容偏移**；TextBox 光标 / ScrollBar 拖拽 / IME 已用）
	/// @details Phase 15：父链累加时逐层**减去父的内容偏移**（§2.2 ④）——返回**视觉位置**。
```

### 2.2 `ECDI/src/Widget/Widget.cpp`（4 处最小 diff）

**① `ContainsPoint` 抽出共享判定**（L98-103）

```cpp
// ── 现状 ──
bool Widget::ContainsPoint(int x, int y) const noexcept{
	// 默认命中区域：矩形 [0, width) × [0, height)
	return x >= 0 &&y >= 0 &&x < m_geometry.width &&y < m_geometry.height;
}

// ── 改为 ──
bool Widget::ContainsPoint(int x, int y) const noexcept{
	return ContainsRect(x, y);      // 表达式抽到非虚辅助——D2 门控共用同一份判据
}

bool Widget::ContainsRect(int x, int y) const noexcept{
	// 默认命中区域：矩形 [0, width) × [0, height)
	// ⚠️ 用 m_geometry.width（float）而非 GetWidth()（int 截断）——保持与既有判定逐位一致
	return x >= 0 &&y >= 0 &&x < m_geometry.width &&y < m_geometry.height;
}
```

> ⚠️ **数值一致性**：既有判定用 `m_geometry.width`（**float**），不是 `GetWidth()`（int 截断）。门控必须沿用同一表达式——**这是初设未覆盖、详设补钉的细节**。

**② `HitTest`——门控 + 子局部坐标加偏移**（L105-141，两处极小的行级改动）

```cpp
	if (!IsEnabled()) {

		return nullptr;
	}

	// ★ Phase 15 D2：裁剪容器门控——**必须在递归子节点之前**（放后面等于没约束）
	// 判据走非虚 ContainsRect（不可用 ContainsPoint：Panel 恒 false 会拦死自身整棵子树）
	if (ClipsChildren() && !ContainsRect(x, y)){

		return nullptr;
	}

	// 逆序遍历子节点（后添加的在上层，优先命中）
	for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
	{
		Widget* child = it->get();

		// 坐标转换：当前局部 → 子局部（★ Phase 15：**加**回自身内容偏移——与 Paint 反向同源）
		const int localX = x - static_cast<int>(child->m_geometry.x) + GetContentOffsetX();
		const int localY = y - static_cast<int>(child->m_geometry.y) + GetContentOffsetY();
```

**③ `Paint`——子原点减偏移**（`Widget::Paint` 内的子循环）

```cpp
	OnPaint(ctx,x,y);

	// ★ Phase 15：子控件的视觉原点 = 自身视觉位置 − 自身内容偏移
	//   默认偏移 0 ⇒ childOriginX/Y 与 x/y 逐位相同（零回归的结构保证）
	const int childOriginX = x - GetContentOffsetX();
	const int childOriginY = y - GetContentOffsetY();

	for(auto& child : m_children){

		child->Paint(ctx,childOriginX,childOriginY);

	}
```

> ⚠️ `ctx.PushClip(...)`（自身边界）**保持不变**——用**视口矩形**，绝不叠自身偏移（§1.4(2)）。

**④ `GetAbsolutePosition`——父链减偏移**

```cpp
	const Widget* parent = m_parent;

	while (parent){

		pos.x += static_cast<float>(parent->GetX() - parent->GetContentOffsetX());
		pos.y += static_cast<float>(parent->GetY() - parent->GetContentOffsetY());

		parent = parent->GetParent();

	}
```

**三处同构性（零回归的结构保证）**

| 处 | 改动 | 默认偏移 0 时 |
|---|---|---|
| `Paint` | 子原点 `− 自身偏移` | `childOrigin == x` ⇒ 逐位同现状 ✓ |
| `HitTest` | 子局部 `+ 自身偏移`；新增门控 | 门控 `ClipsChildren()==false` 短路 ⇒ 逐位同现状 ✓ |
| `GetAbsolutePosition` | `+= 父几何 − 父偏移` | `+= parent->GetX()` ⇒ 逐位同现状 ✓ |

⇒ **三处默认路径不引入任何行为差异**；既有 196 用例须全绿（§6 A1）。

### 2.3 新建 `ECDI/src/Widget/ScrollContent.h`（内部头）

同初设 §2.2 草案，**详设补两点**：

```cpp
class ScrollContent : public Widget{
public:
	explicit ScrollContent(ScrollView& owner);

	/// @brief 内容偏移 X（**接缝实现**——转发所属 ScrollView 的权威偏移）
	[[nodiscard]] int GetContentOffsetX() const noexcept override;
	[[nodiscard]] int GetContentOffsetY() const noexcept override;

private:
	ScrollView& m_owner;	///< 非拥有（树内节点——父先于子析构）
};
```

> **补 1**：**不 override `ClipsChildren()`**（保持默认 `false`）——视口约束由 `ScrollView` 一层负责即够（`HitTest` 纪律 3：逐层检查等价于交集）。
> **补 2**：**不 override `ConsumesMouseInput()`**（默认 `false`）——本节点是内容容器而非交互控件。

### 2.4 新建 `ECDI/src/Widget/ScrollContent.cpp`

```cpp
#include "Widget/ScrollContent.h"          // 内部头（src 内相对 include——同 CaptionButton 先例）

#include "ECDI/Widget/ScrollView.h"

namespace ECDI{

ScrollContent::ScrollContent(ScrollView& owner)
	: m_owner(owner){
	// 无内容：几何由 ScrollView::SetContentExtent 同步（= 内容 extent）
}

int ScrollContent::GetContentOffsetX() const noexcept{

	return m_owner.GetScrollOffsetX();
}

int ScrollContent::GetContentOffsetY() const noexcept{

	return m_owner.GetScrollOffsetY();
}

}
```

### 2.5 新建 `ECDI/include/ECDI/Widget/ScrollView.h`（Public 头 89 → 90）

**沿用初设 §2.2 草案**（已含：不 override 偏移 · `GetScrollOffsetX/Y()` 公共查询 · `ClipsChildren()==true` · `SetSize`/`OnMouseWheel` override）。**详设补充/冻结**：

| 项 | 详设定案 |
|---|---|
| `kDefaultScrollStep` | **32**（与 `Button` 默认高同量级——框架内既有"行高量级"；**不绑 Demo**，`ModelProbe` 显式 `SetScrollStep(28)`） |
| `m_content` 类型 | `ScrollContent*`（非拥有·树内节点） |
| `GetMaxOffsetX/Y` | `max(0, m_contentW − ViewportWidth())` / `…H()`，其中 `ViewportWidth/H` 见 §3.3 |
| 新增私有 | `int ViewportWidth() const noexcept;` / `int ViewportHeight() const noexcept;`（**扣除已显示条**——§3.3） |
| 新增私有 | `void SyncBars() noexcept;`（范围 + 几何 + 可见性一次同步——§3.4） |
| 新增私有 | `void ApplyLayout();`（`ScrollContent` 尺寸 + 两条几何重排） |

### 2.6 新建 `ECDI/src/Widget/ScrollView.cpp`（核心实现）

```cpp
ScrollView::ScrollView(){
	// ① 三个子节点按"内容 → 条"顺序创建并 AddChild（顺序无关 Z——互不重叠）
	//    条最后 AddChild ⇒ HitTest 逆序时条优先命中（自然满足"条优先成为 target"）
	auto content = std::make_unique<ScrollContent>(*this);
	m_content = content.get();
	AddChild(std::move(content));

	auto vbar = std::make_unique<ScrollBar>(ScrollBar::Orientation::Vertical);
	m_vBar = vbar.get();
	AddChild(std::move(vbar));

	auto hbar = std::make_unique<ScrollBar>(ScrollBar::Orientation::Horizontal);
	m_hBar = hbar.get();
	AddChild(std::move(hbar));

	// ② 条 → 容器：偏移变化回调（std::function 先例：CaptionButton）
	m_vBar->SetOnOffsetChanged([this](int o){ SetContentOffset(GetScrollOffsetX(), o); });
	m_hBar->SetOnOffsetChanged([this](int o){ SetContentOffset(o, GetScrollOffsetY()); });

	m_step = kDefaultScrollStep;
}

ScrollView::~ScrollView() = default;   // 三个成员皆非拥有——生命周期归 m_children
```

**`SetSize`**（视口变化 → 原子重算）

```cpp
void ScrollView::SetSize(int w, int h){
	Widget::SetSize(w, h);          // 基类几何（必须显式调——隐藏基类会在 override 后失效）
	ApplyLayout();                  // ① 条几何（厚度/可见性） ② ScrollContent 尺寸
	ClampOffset();                  // ③ maxOffset 变了 ⇒ 立即 clamp（不变式）
	SyncBars();                     // ④ 范围 + 可见性同步
	// 不 Invalidate：SetSize 由布局调用，重绘由调用方/Arrange 路径负责（既有契约）
}
```

**`UpdateContentExtent`**（D6——唯一入口 + 原子链）

```cpp
void ScrollView::UpdateContentExtent(){
	// ① 推导（内容坐标系下二维包围盒右下角，负向钳 0；**不是 Σ**）
	int w = 0, h = 0;
	const size_t n = m_content->GetChildCount();
	for (size_t i = 0; i < n; ++i){
		const Widget* c = m_content->GetChildAt(i);
		w = (std::max)(w, c->GetX() + c->GetWidth());
		h = (std::max)(h, c->GetY() + c->GetHeight());
	}
	SetContentExtent(w, h);         // ② extent → ③ maxOffset → ④ clamp offset → ⑤ sync → ⑥ Invalidate
}
```

**`SetContentExtent`**（原子副作用链——初设 §3.6）

```cpp
void ScrollView::SetContentExtent(int width, int height){
	const int w = (std::max)(0, width);
	const int h = (std::max)(0, height);
	if (w == m_contentW && h == m_contentH){
		return;                     // 同尺寸 no-op（避免无谓重排）
	}
	m_contentW = w;
	m_contentH = h;

	m_content->SetSize(m_contentW, m_contentH);   // 内容坐标空间根节点的尺寸 = 内容 extent
	ClampOffset();                                 // ★ maxOffset 变了 ⇒ **立即** clamp（内容变短的悬空防护）
	ApplyLayout();
	SyncBars();
	Invalidate();
}
```

**`SetContentOffset`（唯一权威 + clamp）**

```cpp
void ScrollView::SetContentOffset(int x, int y){
	const int cx = (std::clamp)(x, 0, GetMaxOffsetX());
	const int cy = (std::clamp)(y, 0, GetMaxOffsetY());
	if (cx == m_offsetX && cy == m_offsetY){
		return;
	}
	m_offsetX = cx;
	m_offsetY = cy;
	SyncBars();                     // 条滑块位置跟随（条自身不 clamp——D11 单一真相源）
	Invalidate();
}
```

**`OnMouseWheel`**（与 `TextBox.cpp:452` 同式）

```cpp
void ScrollView::OnMouseWheel(const MouseWheelEvent& event){
	// 与 TextBox 一致的换算基准（120 = WHEEL_DELTA）；高精度滚轮天然支持分数步长
	m_offsetY -= static_cast<int>(static_cast<float>(event.GetDelta()) / 120.0f
	                              * static_cast<float>(m_step));
	SetContentOffset(m_offsetX, m_offsetY);   // 经唯一入口 ⇒ clamp + sync + Invalidate
}
```

> **v1 横向无滚轮入口**（K15/K16：`WM_MOUSEHWHEEL` 未翻译、`MouseEvent` 无修饰键）——横向只能靠拖动滚动条或 `SetContentOffset`。

**`ApplyLayout` / `SyncBars`**（几何与范围同步——§3.3/§3.4 定义）

### 2.7 新建 `ECDI/include/ECDI/Widget/ScrollBar.h`（Public 头 90 → 91）

**沿用初设 §2.3 草案**（`Orientation` / `SetRange` / `SetOffset` / `SetOnOffsetChanged` / `ApplyTheme` / `SetStyle` / `ConsumesMouseInput()==true`）。**详设定案**：

| 项 | 定案 |
|---|---|
| `kMinThumbLength` | **24**（最小可抓取长度） |
| `kPageClickRatio` | 翻页量 = `viewportExtent`（一屏）—— 不用比例 |
| `m_dragGrabOffset` | 拖拽起点在滑块内的相对偏移（防跳） |
| 未 override `ClipsChildren()` | 保持 false（条的子为空——无子树需约束） |

### 2.8 新建 `ECDI/src/Widget/ScrollBar.cpp`（核心实现——范围模型 §3.4）

```cpp
ScrollBar::ScrollBar(Orientation orientation)
	: m_orientation(orientation){
	ApplyTheme(GetDefaultTheme());   // 构造注入（Widget 构造期虚分派静态 ⇒ 必须在此重调——同 ProgressBar 先例）
}

void ScrollBar::ApplyTheme(const Theme& theme){
	ScrollBarStyle defaults = theme.GetScrollBarStyle();
	m_style.trackColor.Apply(defaults.trackColor.value);
	m_style.thumbColor.Apply(defaults.thumbColor.value);
	m_style.thumbHoverColor.Apply(defaults.thumbHoverColor.value);
	m_style.thumbPressedColor.Apply(defaults.thumbPressedColor.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);
	m_style.thickness.Apply(defaults.thickness.value);
	Invalidate();
}

void ScrollBar::SetRange(int contentExtent, int viewportExtent){
	m_contentExtent  = (std::max)(0, contentExtent);
	m_viewportExtent = (std::max)(0, viewportExtent);
	Invalidate();
}

int ScrollBar::GetMaxOffset() const noexcept{
	return (std::max)(0, m_contentExtent - m_viewportExtent);
}

void ScrollBar::SetOffset(int offset){
	const int o = (std::clamp)(offset, 0, GetMaxOffset());
	if (o == m_offset) return;
	m_offset = o;
	Invalidate();
}
```

**版式（§3.4 数学）**：

```cpp
int ScrollBar::TrackLength() const noexcept{
	return m_orientation == Orientation::Vertical ? GetHeight() : GetWidth();
}

Rect ScrollBar::ThumbRectLocal() const noexcept{
	const int track = TrackLength();
	if (m_contentExtent <= 0 || track <= 0){
		return (m_orientation == Orientation::Vertical)
			? Rect{0, 0, static_cast<float>(GetWidth()), static_cast<float>(track)}
			: Rect{0, 0, static_cast<float>(track), static_cast<float>(GetHeight())};
	}
	// thumbLen = max(kMin, track × viewport / content)——比例映射，钳最小值
	int thumbLen = static_cast<int>(static_cast<long long>(track) * m_viewportExtent / m_contentExtent);
	thumbLen = (std::clamp)(thumbLen, (std::min)(kMinThumbLength, track), track);

	const int maxOff = GetMaxOffset();
	const int start = (maxOff == 0) ? 0
	              : static_cast<int>(static_cast<long long>(track - thumbLen) * m_offset / maxOff);
	return (m_orientation == Orientation::Vertical)
		? Rect{0, static_cast<float>(start), static_cast<float>(GetWidth()), static_cast<float>(thumbLen)}
		: Rect{static_cast<float>(start), 0, static_cast<float>(thumbLen), static_cast<float>(GetHeight())};
}
```

> ⚠️ **`long long` 中间量**：`track × viewport` 可能溢出 int（如 2000 × 2000 = 4e6 尚可，但 50000² 溢出）——用 `long long` 承积再除。**与 Phase 12 详设 `DipToPixels` 的同类教训同源**。

**拖拽反推**：

```cpp
int ScrollBar::OffsetFromThumbStart(int thumbStart) const noexcept{
	const int track = TrackLength();
	const int thumbLen = static_cast<int>(m_orientation == Orientation::Vertical
	                     ? ThumbRectLocal().height : ThumbRectLocal().width);
	const int denom = track - thumbLen;
	if (denom <= 0) return 0;                                   // 除零保护（滑块占满）
	const int maxOff = GetMaxOffset();
	const int s = (std::clamp)(thumbStart, 0, denom);
	return static_cast<int>(static_cast<long long>(s) * maxOff / denom);
}
```

### 2.9 新建 `ECDI/include/ECDI/Theme/ScrollBarStyle.h`（Public 头 91 → 92）

**沿用初设 §2.4 草案**（6 字段：track/thumb/thumbHover/thumbPressed + cornerRadius + thickness）。**详设定默认值**（§3.5）。

### 2.10 `Theme.h` / `DefaultTheme.h` / `DefaultTheme.cpp`

```cpp
// Theme.h —— ① include 追加（在 "ECDI/Theme/ProgressBarStyle.h" 之后）
#include "ECDI/Theme/ScrollBarStyle.h"

// Theme.h —— ② 纯虚追加（在 GetProgressBarStyle 之后）
	virtual ScrollBarStyle GetScrollBarStyle() const = 0;   ///< Phase 15 ScrollBar 专属样式

// DefaultTheme.h —— ③ override 声明追加
	ScrollBarStyle GetScrollBarStyle() const override;      ///< Phase 15

// DefaultTheme.cpp —— ④ 实现追加（在 GetProgressBarStyle 之后、GetDefaultTheme 之前）
ScrollBarStyle DefaultTheme::GetScrollBarStyle() const{
	// Phase 15 默认视觉：中性灰轨道 + 稍亮滑块（三态）
	ScrollBarStyle s;
	s.trackColor.value        = Color::FromRGBA8(240, 240, 245);
	s.thumbColor.value        = Color::FromRGBA8(190, 190, 200);
	s.thumbHoverColor.value   = Color::FromRGBA8(160, 160, 175);
	s.thumbPressedColor.value = Color::FromRGBA8(130, 130, 145);
	s.cornerRadius.value      = 3.0f;
	s.thickness.value         = 12;
	return s;
}
```

> **影响面（已 grep 实证）**：`Theme` 的**唯一实现者是 `DefaultTheme`**（无 FakeTheme/替身）⇒ 加纯虚只改这一处 ✓

### 2.11 `examples/ModelProbe/` 迁移（R4）

| 原 | 新 |
|---|---|
| `class ModelListPanel : public Panel`（35 行，`ModelProbe.cpp:50-85`） | **删除** |
| `list->SetRows(rows)` / `SetRowHeight` | `sv->GetContentView().AddChild(std::move(row))`（逐行） |
| `OnMouseWheel` 按行滚 / `ApplyLayout()` / `m_offset` | **全部删除**（由 `ScrollView` 接管） |
| `SetScrollStep` | `sv->SetScrollStep(28)`（显式——框架默认 32 不绑 Demo） |
| **背景/圆角/边框**（原继承 `Panel`） | ⚠️ `ScrollView` **无样式能力** ⇒ **外层 `Panel`** 包住（`PanelStyleOverride` 原样搬迁，§3.6） |
| `m_list` 成员类型 | `ModelListPanel*` → `ScrollView*` |

**验收判据**：滚轮一滚一行（28px）· 行高/视觉不变 · 圆角边框保持 · **净删 ~35 行手搓逻辑**。

### 2.12 构建（零改动）

`CMakeLists.txt` 用 `file(GLOB_RECURSE FRAMEWORK_SOURCES CONFIGURE_DEPENDS "ECDI/src/*.cpp")`
⇒ 新增 3 个 `.cpp`（`ScrollContent`/`ScrollView`/`ScrollBar`）**自动入库**；
`ecdi_public_header_test`（每 Public 头一 TU）自动覆盖新增 3 个头。**无需改构建脚本**。

> ⚠️ **但 CLion 可能需手动 Reload CMake Project**（`CONFIGURE_DEPENDS` 在部分生成器下不即时生效）——交付时提示用户。

---

## 3. 关键常量与算法冻结

### 3.1 常量表

| 常量 | 值 | 位置 | 依据 |
|---|---|---|---|
| `ScrollView::kDefaultScrollStep` | **32** | `ScrollView.h` public static constexpr | 框架内既有"行高量级"（`Button` 默认高）；**不绑 Demo**（D5） |
| `ScrollBar::kMinThumbLength` | **24** | `ScrollBar.h` private static constexpr | 最小可抓取长度 |
| `ScrollBarStyle.thickness` 默认 | **12** | `DefaultTheme` | 细粒度滚动条观感 |
| （`ModelProbe`）滚动步长 | **28** | `ModelProbe.cpp` 显式 | R4 零回归（等于原 `kRowHeight`） |

### 3.2 坐标变换三处（符号冻结——§2.2 已给最小 diff）

```
Paint                : childOrigin = 自身视觉位置 − 自身内容偏移     （减）
HitTest              : childLocal  = 父局部 − 子几何 + 父内容偏移    （加）
GetAbsolutePosition  : pos += 父几何 − 父内容偏移                   （减）
```

### 3.3 双轴 viewport（**两轮判定**——冻结）

```
nV1 = contentH > viewportH0 ;  nH1 = contentW > viewportW0
tW  = viewportW0 - (nV1 ? T : 0)
tH  = viewportH0 - (nH1 ? T : 0)
nV  = nV1 || (contentH > tH)          // 单调升级
nH  = nH1 || (contentW > tW)
ViewportWidth()  = viewportW0 - (nV ? T : 0)
ViewportHeight() = viewportH0 - (nH ? T : 0)
```

其中 `T = 条厚度`（`ScrollBarStyle::thickness`，由 `ScrollBar` 的样式读取）。
`viewportW0/H0` = `ScrollView` 的 `GetWidth()/GetHeight()`（未扣除）。
**交叉区不做缩角**（记账）：水平条轨道宽 = `ViewportWidth()`（已扣垂直条）。

### 3.4 范围模型与版式（冻结）

```
maxOffset  = max(0, contentExtent - viewportExtent)
thumbLen   = clamp(track × viewportExtent / contentExtent, min(kMin, track), track)
thumbStart = maxOffset == 0 ? 0 : (track - thumbLen) × offset / maxOffset
反推       = denom <= 0 ? 0 : clamp(thumbStart, 0, track - thumbLen) × maxOffset / (track - thumbLen)
```

⚠️ 中间乘积一律走 `long long`（§2.8 注释）。

### 3.5 默认样式值

见 §2.10 ④（中性灰四色 + `cornerRadius 3` + `thickness 12`）。

### 3.6 R4 装饰分离（`ModelProbe`）

```
原：ModelListPanel : Panel（自带 PanelStyle）
新：Panel（PanelStyleOverride 原样）
    └── ScrollView（无样式）
          └── GetContentView()（内部 ScrollContent）
```

---

## 4. 契约表

| # | 契约 | 条文 | 双向 |
|---|---|---|---|
| C1 | **坐标三分** | 布局位置永不随滚动变化；视觉位置 = 布局 − 沿途偏移 | — |
| C2 | **自身不受自身偏移** | `ScrollView` 的 `m_geometry` 与 `PushClip` 用视口矩形 | — |
| C3 | **三处同变换** | `Paint`(减) / `HitTest`(加) / `GetAbsolutePosition`(减) | — |
| C4 | **偏移 override 归属** | 只有 `ScrollContent` override；`ScrollView` 恒 0；`ScrollBar` 在偏移层之外 | — |
| C5 | **`ClipsChildren` 判据** | 命中点在 `[0,w)×[0,h)` 内才递归子树；**非虚 `ContainsRect`**；检查在递归**之前** | 默认 false ⇒ 既有零变化 |
| C6 | **offset 权威** | 仅 `ScrollView` 持有并可 clamp；`ScrollBar::SetOffset` 也 clamp 但对权威只读 | — |
| C7 | **clamp 不变式** | 任意时刻 `0 ≤ offset ≤ maxOffset`；`maxOffset` 变化时**立即**重新 clamp | — |
| C8 | **extent 定义** | 二维包围盒右下角（`max(x+w)` / `max(y+h)`），负向钳 0；非 `Σ` | — |
| C9 | **extent 更新** | 显式 `UpdateContentExtent()` + `SetContentExtent()`；**不自动挂钩** | 缺口记账（§8 L1） |
| C10 | **`SetScrollStep`** | `@pre step >= 0`（`FRAMEWORK_ASSERT` + 忽略负值） | — |
| C11 | **`ScrollBar` 回调可空** | `m_onOffsetChanged` 为空时静默跳过（条可独立使用——D9） | — |
| C12 | **`ConsumesMouseInput`** | `ScrollView` false（容器）/ `ScrollBar` true（交互件）——对照关系 | Phase 13 D9 语义 |
| C13 | **`GetScrollOffsetX/Y` 命名** | 公共查询**不得**与虚接缝 `GetContentOffsetX/Y` 同名（name hiding ⇒ 一个查询两份语义） | — |
| C14 | **生命周期** | 三个成员均**非拥有**（树内节点）；父先于子析构 ⇒ 析构期成员指针必然仍有效 | — |

---

## 5. 测试规格（`ecdi_tests`——预计 T15-1..T15-14）

| # | 用例 | 断言要点 |
|---|---|---|
| **T15-1** | `ScrollView.DefaultOffsetIsZero` | 新构造的 `ScrollView`：`GetScrollOffsetX/Y()==0`；`GetContentView().GetContentOffsetX/Y()==0` |
| **T15-2** | `Widget.DefaultOffsetAndClipFlag` | 裸 `Widget` / `Panel` / `Button`：`GetContentOffsetX/Y()==0`、`ClipsChildren()==false`（**零回归锚**） |
| **T15-3** | `ScrollView.OffsetDoesNotAffectOwnClip` | 偏移非 0 时，`ScrollView` 自身 `OnPaint` 收到 `(x,y)` == 未偏移值（§1.4(2)） |
| **T15-4** | `ScrollView.ContentIsOffset` | `ScrollContent` 的子 → `Paint` 命令坐标 = `自身视觉 − offset`（`RecordingBackend` 断言） |
| **T15-5** | ★ `ScrollBar.NotAffectedByContentOffset` | **偏移层隔离回归锚**：偏移非 0 时，条的命令坐标/HitTest/`GetAbsolutePosition` **均与偏移 0 时相同** |
| **T15-6** | `ScrollView.HitTestWithOffset` | 多层（`ScrollView`→`ScrollContent`→子）点击命中正确（初设 §3.1 数值实例） |
| **T15-7** | ★ `ScrollView.ClipsChildrenRejectsOutside` | 点在该容器外 ⇒ 整棵子树不命中（含深层）；点在容器内 + 子越界 ⇒ 不命中越界子 |
| **T15-8** | `Panel.HitTestRegression` | `Panel`（`ClipsChildren()==false`）`HitTest` 行为**与改动前一致**（既有 `WidgetTests`/`CollapsiblePanelTests` 4 处直调不改判据） |
| **T15-9** | `ScrollView.WheelStepAndClamp` | `delta=±120/±30/±240` 的步长换算；到边界不再滚 |
| **T15-10** | `ScrollView.ContentShrinkClampsOffset` | 内容变短 ⇒ 现有 offset **立即** clamp（§3.6 缺口用例） |
| **T15-11** | `ScrollView.ExtentIsBoundingBox` | `y=0/h=20` + `y=30/h=20` ⇒ extent **50**（非 `Σ`=40）；负向钳 0；空内容 = 0 |
| **T15-12** | `ScrollBar.ThumbGeometry` | 滑块长度/位置比例；`maxOffset==0` 占满；`contentExtent==0` 占满 |
| **T15-13** | `ScrollBar.DragInvertsOffset` | 拖拽反推；`denom<=0` 除零保护；`m_dragGrabOffset` 不跳动 |
| **T15-14** | `ScrollBar.ThemeAndOverride` | `ApplyTheme` 注入 + `SetStyle` override 后 `ApplyTheme` **不覆盖**（D7 契约，同 `ProgressBarTests` 先例） |

**用例数**：196（现状）→ **+14 = 210**（预计；实施后以实测为准）。

---

## 6. 验收

| # | 项 | 判据 |
|---|---|---|
| **A1** | 四工具链构建 + 测试 | MSVC / ClangCL / Clang / MinGW 全绿；用例 196 → **210** |
| **A2** | **断言启用核验** | 按 skill 条 50 取二进制证据（7 条条件串）——`CMakeLists.txt` 已为非 MSVC 模拟链补 `_DEBUG` |
| **A3** | **零回归** | 既有 196 用例**全部保持通过**（尤其 `WidgetTests` / `CollapsiblePanelTests` / `ClipTests` 的 `HitTest` 直调用例） |
| **A4** | **偏移层隔离**（T15-5） | ★ 本条是本阶段特有的结构性验收——防「偏移 override 被搬回 `ScrollView`」 |
| **A5** | `ModelProbe` 手测 | 滚轮一滚一行（28px）· 行高/圆角/边框不变 · 滚动条可拖拽/翻页 · 标题栏可拖窗（`--borderless`） |
| **A6** | `ecdi_public_header_test` | 92 头各自独立可编译（含新增 3 头） |
| **A7** | 交付提示 | CLion 需 Reload CMake Project（§2.12） |

---

## 7. 影响面与回归清单

| 项 | 实测 | 影响 |
|---|---|---|
| `Theme` 实现者 | **1**（`DefaultTheme`，无替身） | 加纯虚改 1 处 ✓ |
| `ContainsPoint` override 者 | **1**（`Panel.h:45`） | D2 **不碰**它（走非虚 `ContainsRect`）✓ |
| `ConsumesMouseInput` override 者 | **4**（`Button`/`StateWidget`/`TextBox`/`CaptionButton`） | `ScrollBar` 成第 5 个 ✓ |
| `HitTest` 生产调用点 | **2**（`Application.cpp:185` / `Window.cpp:257`） | 改一处两处受益 ✓ |
| `HitTest` **测试直调** | **4**（`WidgetTests:282/285` · `CollapsiblePanelTests:45/199`） | ★ **回归清单**——均 `Panel` 派生（`ClipsChildren()==false`）⇒ 判据不变 |
| `OnMouseWheel` 实现者 | **5**（K18） | 本阶段**不改签名**（D7 已降级）✓ |
| `GetAbsolutePosition` 消费者 | **5**（K10，全视觉坐标） | 认偏移后全部受益 ✓ |
| `Widget::ContainsPoint` 抽取 | 新增非虚辅助 | 虚函数签名/语义不变；`Panel` override 继续生效 ✓ |
| 构建脚本 | `GLOB_RECURSE … CONFIGURE_DEPENDS` | **零改动** ✓ |

**另需同步（文档/锚点）**：README 规模锚点（Public 头 89→92 · 用例 196→210）——**实施后**按条 61 枚举全部承载文件（根 README + `docs/README.md`）。

---

## 8. 已知局限与记账

| # | 局限 | 处置 |
|---|---|---|
| **L1** | `UpdateContentExtent` **不自动挂钩**——调用方忘记调 ⇒ 条显示旧长度 | 记账（与 9.8「不挂钩」原则一致；无 `Layout` 完成回调可用） |
| **L2** | 双轴**边界误差**已由两轮判定消除，但**交叉区不缩角**（水平条轨道宽含垂直条占位） | 记账（体验增强） |
| **L3** | 横向**无滚轮入口**（K15/K16：`WM_MOUSEHWHEEL` 未翻译 + `Event` 无修饰键） | 记账（`roadmap-deferred.md` §7.5 条 36） |
| **L4** | `ScrollBarStyle::thickness` 变化 ⇒ `maxOffset` 变 ⇒ 需重算；本阶段仅在 `SetSize`/`SetContentExtent` 时读取 | 记账（不做主题变化订阅——O7） |
| **L5** | 嵌套滚动**停递不做**（内外层同时滚） | 记账（D7 降级——条 35） |
| **L6** | 内容负向坐标（<0）不支持（extent 钳 0） | 记账（条 37） |
| **L7** | `CaptionBar` 窄窗（<138px）按钮越界仍可命中 | **既有缺陷**，本阶段不处理（条 30） |

---

## 9. 修订记录

- v1.0（2026-09-18）详细设计初稿：§1 实施总览（**14 文件**，Public 头 89→92）· §2 **逐文件最小 diff 规格**（`Widget.h` 落点 A/B/C · `Widget.cpp` **4 处最小 diff** · `ScrollContent` 内部头 · `ScrollView`/`ScrollBar` 新建实现 · `ScrollBarStyle` · `Theme`/`DefaultTheme` · R4 迁移 · 构建零改动）· §3 常量与算法冻结（含**详设补钉**：`ContainsPoint` 判定用 `m_geometry.width`（float）而非 `GetWidth()` ⇒ 抽出**非虚 `ContainsRect`** 供门控共用；`long long` 中间量防溢出）· §4 契约表 **C1–C14** · §5 测试规格 **T15-1..14** · §6 验收 **A1–A7** · §7 影响面与回归清单 · §8 已知局限 **L1–L7**。上游：需求 v1.2 ✅ + 初设 v1.2 ✅（三轮评审通过）。
