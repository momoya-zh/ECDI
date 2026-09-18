#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

class ScrollBar;
class ScrollContent;
class MouseWheelEvent;

/// @brief 滚动容器（Phase 15 R1——视口 + 内容偏移 + 内容范围 + 滚轮）
/// @details **继承 `Widget` 而非 `Panel`**（D4）：`Panel::ContainsPoint` 恒 false（输入完全透传），
/// 而 ScrollView **必须自身可命中**——否则空白区滚轮无效（`Application::OnMouseWheel` 在
/// `target == nullptr` 时直接 return）。两者语义相反，复用会制造概念绑架。
///
/// **职责边界（职责四层——禁止本类变万能容器）**：
/// 本类**只做** viewport + offset + extent + 滚动交互：
/// - **不接管** `Arrange`（偏移不参与布局——D1 选视口偏移的核心理由）；
/// - **不提供**样式（背景/圆角/边框属 `Panel`；需要装饰时用一个外层 `Panel` 包住本控件）；
/// - **不推导** 内容尺寸以外的语义（不做 ListBox/虚拟化/框选——见需求稿 §4）。
///
/// **树结构（内容坐标空间与视口空间分离——详设 §3.3）**：
/// ```
/// ScrollView（**不 override 接缝** ⇒ 偏移恒 0 ⇒ 三个直接子都不受影响）
/// ├── ScrollContent（内容坐标空间的根——**唯一 override 偏移**的节点；AddChild 加在它下面）
/// ├── 垂直 ScrollBar（固定在视口——**不受偏移**）
/// └── 水平 ScrollBar（固定在视口——**不受偏移**）
/// ```
/// ⚠️ **判据**：*装饰与交互子节点必须待在偏移层之外*——否则滚动条会随内容一起滚
/// （详设 §3.1.1 有反例：视觉偏差 −offset、命中偏差 +offset）。
///
/// 使用（最小）：
/// ```cpp
/// auto sv = std::make_unique<ECDI::ScrollView>();
/// sv->SetSize(400, 300);
/// sv->SetScrollStep(28);                       // 一滚 28px（框架默认 32——D5 不绑 Demo）
/// sv->GetContentView().AddChild(std::move(row1));
/// sv->GetContentView().AddChild(std::move(row2));
/// sv->UpdateContentExtent();                   // D6：显式更新（不自动挂钩）
/// parent->AddChild(std::move(sv));
/// ```
class ScrollView : public Widget{

public:

	/// @brief 默认滚轮步长（像素；`offset -= delta / 120 × step`）
	/// @details 32 = 与 `Button` 默认高同量级（框架内既有"行高量级"）；
	/// **不绑具体 Demo**——`ModelProbe` 显式 `SetScrollStep(28)`（D5）。
	static constexpr int kDefaultScrollStep = 32;

	ScrollView();

	~ScrollView() override;

	// ── 内容 ────────────────────────────────────────────────

	/// @brief 内容视图（内容子控件加在此节点下；**非拥有**——树内节点）
	/// @details 实际类型为内部类 `ScrollContent`（内容坐标空间的根）；公开面只暴露 `Widget&`
	/// （调用方只需 `AddChild`）⇒ 内部头不必进 Public 头计数。
	/// 其尺寸 = 内容 extent（`SetContentExtent` 同步），故子控件按**内容坐标系**布局。
	Widget& GetContentView() noexcept;
	const Widget& GetContentView() const noexcept;

	/// @brief 从内容视图的直接子控件重新推导 extent（D6——显式动作，**不自动挂钩**）
	/// @details 定义（需求稿 D6 钉死）：extent = 内容坐标系下二维包围盒右下角，负向钳 0——
	/// `w = max(0, max(x + child.GetWidth()))`，`h = max(0, max(y + child.GetHeight()))`。
	/// **不是** `Σ(子控件尺寸)`（间隔会漏算：y=0/h=20 与 y=30/h=20 ⇒ 真实下界 50，Σ 只得 40）。
	/// 副作用链见 `SetContentExtent`。
	void UpdateContentExtent();

	/// @brief 显式设置内容范围（不走子控件推导——内容由外部管理的场景）
	/// @details **原子副作用链（顺序冻结——详设 §2.6）**：
	/// `ApplyLayout（条几何+可见性 ⇒ 定 viewport）→ ClampOffset（用新 viewport 重算 maxOffset）
	///  → SyncBars（范围+可见性）→ Invalidate`。
	/// ⚠️ **不得调换顺序**：`ApplyLayout` 决定条可见性 ⇒ 决定 viewport ⇒ 决定 maxOffset；
	/// 先 clamp 会用**旧 viewport** 的界限（典型失效：内容变短 + 横条消失 ⇒ offset 仍越界）。
	void SetContentExtent(int width, int height);

	/// @brief 内容范围（只读）
	[[nodiscard]] Size GetContentExtent() const noexcept;

	// ── 偏移 ────────────────────────────────────────────────

	/// @brief 设置内容偏移（内部 clamp 到 `[0, maxOffset]`——D11 单一真相源）
	void SetContentOffset(int x, int y);

	/// @brief 滚动偏移 X（**公共查询**——注意与虚接缝 `GetContentOffsetX` **不同名**）
	/// @details ⚠️ **命名不可改回 `GetContentOffsetX`**：那是 `Widget` 的**虚接缝名**；
	/// 若在此声明同名非虚成员会触发 **name hiding**——`Widget* p = &sv; p->GetContentOffsetX()`
	/// 走基类（返回 0），而 `sv.GetContentOffsetX()` 走派生（返回真值）⇒ **一个查询两份语义**。
	/// 沿用 `TextBox::GetScrollOffsetY()` 的既有命名先例（类型 `int`——D1 定为整数像素）。
	[[nodiscard]] int GetScrollOffsetX() const noexcept;

	/// @brief 滚动偏移 Y（公共查询——同 `GetScrollOffsetX`）
	[[nodiscard]] int GetScrollOffsetY() const noexcept;

	/// @brief 最大化偏移（`max(0, contentExtent - viewportExtent)`——与手搓版同式）
	[[nodiscard]] int GetMaxOffsetX() const noexcept;
	[[nodiscard]] int GetMaxOffsetY() const noexcept;

	// ⚠️ **本类不 override `GetContentOffsetX/Y()`**——接缝由内部节点 `ScrollContent` 实现；
	//    若在此 override，滚动条作为直接子也会被偏移（详设 §3.1.1）。

	// ── 配置 ────────────────────────────────────────────────

	/// @brief 滚轮步长（像素；`offset -= delta / 120.0f × step`——与 `TextBox` 换算基准一致）
	/// @pre step >= 0（负值无合理语义：debug 断言 + 忽略）
	void SetScrollStep(int step) noexcept;
	[[nodiscard]] int GetScrollStep() const noexcept;

	/// @brief 是否显示滚动条（默认 true；false = 纯滚动形态）
	void SetScrollBarVisible(bool visible);
	[[nodiscard]] bool IsScrollBarVisible() const noexcept;

	// ── 滚动条访问（R2——非拥有，树内节点）──────────────────

	[[nodiscard]] ScrollBar* GetVerticalScrollBar() noexcept;
	[[nodiscard]] ScrollBar* GetHorizontalScrollBar() noexcept;

	// ── 几何 ────────────────────────────────────────────────

	/// @brief 尺寸变化 → 原子重算（顺序同 `SetContentExtent`：`ApplyLayout → ClampOffset → SyncBars`）
	/// @details **`public`**：与 `TextBox` / `CollapsiblePanel` / `CaptionBar` 三个先例一致
	/// （详设 §2.5 v1.2 修正——草案曾列 `protected`，结果本头自己的用法示例 `sv->SetSize(...)` 编译不过）。
	/// 布局路径经 `Widget*` 调用同样虚分派到本 override。
	void SetSize(int w, int h) override;

	// ── 接缝 ────────────────────────────────────────────────

	/// @brief 命中约束在视口内（D2 / R3）
	/// @details 语义 = 「**命中点**须落在本控件矩形内，否则不递归子树」（详设 §3.2）。
	[[nodiscard]] bool ClipsChildren() const noexcept override { return true; }

protected:

	/// @brief 滚轮滚动（`Application::OnMouseWheel` 已按 target→parent 冒泡，本类只滚自己）
	void OnMouseWheel(const MouseWheelEvent& event) override;

private:

	// ── 内部：布局与同步（**顺序冻结**：ApplyLayout → ClampOffset → SyncBars）──

	/// @brief 条几何 + 内容节点几何（**不读 offset**——故可安全前置）
	void ApplyLayout();

	/// @brief 把现有 offset 夹回 `[0, maxOffset]`（不变式 C7）
	void ClampOffset();

	/// @brief 条的范围 + 可见性 + 滑块位置同步
	void SyncBars();

	// ── 内部：双轴 viewport（两轮判定——详设 §3.3）────────────

	/// @brief 是否需要垂直条（两轮判定；`m_barsVisible == false` 时恒 false）
	[[nodiscard]] bool NeedsVerticalBar() const noexcept;

	/// @brief 是否需要水平条（两轮判定）
	[[nodiscard]] bool NeedsHorizontalBar() const noexcept;

	/// @brief 可用宽（**扣除已显示的垂直条**）
	[[nodiscard]] int ViewportWidth() const noexcept;

	/// @brief 可用高（**扣除已显示的水平条**）
	[[nodiscard]] int ViewportHeight() const noexcept;

	/// @brief 条厚度（读样式——参与 viewport 计算）
	[[nodiscard]] int BarThickness() const noexcept;

	ScrollContent* m_content = nullptr;   ///< 内容坐标空间的根（非拥有——子节点）
	ScrollBar* m_vBar = nullptr;          ///< 垂直条（非拥有——子节点；父先于子析构）
	ScrollBar* m_hBar = nullptr;          ///< 水平条（非拥有）

	int m_offsetX = 0;                    ///< 内容偏移 X（**权威状态**——D11）
	int m_offsetY = 0;                    ///< 内容偏移 Y
	int m_contentW = 0;                   ///< 内容范围宽（D6）
	int m_contentH = 0;                   ///< 内容范围高
	int m_step = kDefaultScrollStep;      ///< 滚轮步长（构造注入框架默认值——D5）
	bool m_barsVisible = true;            ///< 滚动条开关

};

}
