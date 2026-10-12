#pragma once

#include "ECDI/Animation/AnimationToken.h"
#include "ECDI/Widget/Panel.h"

namespace ECDI{

/// @brief 展开方向（内容向对侧展开；header 锚定在反侧）
enum class ExpandDirection{
	Down,    ///< 内容向下展开（header 在上；默认）
	Up,      ///< 内容向上展开（header 在下）
	Right,   ///< 内容向右展开（header 在左）
	Left,    ///< 内容向左展开（header 在右）
};

/// @brief 可折叠面板（9.6 S2 demo 产品化升格）
/// @details
/// 继承 Panel；四向折叠：沿锚定边 resize 收缩到 0 + 内容容器 SetVisible(false)
/// （visible 机制双跳过 Paint/HitTest——Widget 基类已有，零新增）。
///
/// 默认收起（2026-08-30 v1.2 变更，phase9.6-panel-container-semantics v1.1）：
/// 构造即收起；收起态 SetSize = 定义展开基准（非动画轴保持给出值、动画轴呈现 0），
/// 实际呈现保持收缩。初始化顺序约定：SetExpandDirection → SetPosition → SetSize（文档约定、无运行时 assert）。
///
/// 边界原则（详细设计 v1.1 §2 硬契约）：
/// - header 完全外部自组——本控件不持有/管理/隐藏 header（锚定边由 SetExpandDirection 语义表达）
/// - 折叠 = resize 收缩（非 translate 滑出）；动画只驱动尺寸轴值 s，位置是 s 的纯函数（单 token）
/// - m_expandedRect = 最后一次「展开态→折叠」跃迁的完整几何（非永久原始几何）
/// - 动画中外部 SetPosition/SetSize 不是支持场景（v0.1 已知限制）
class CollapsiblePanel: public Panel{
public:

	CollapsiblePanel();

	~CollapsiblePanel() override = default;

	// ── 方向 ────────────────────────────────────────

	/// @brief 设置展开方向（默认 Down）
	/// @details **初始化约定（2026-08-30 v1.2 升格）**：须在首次 SetSize 前设置——方向决定收缩呈现的
	/// 作用轴（Down/Up = 高度轴、Left/Right = 宽度轴），收起态 SetSize 按当前方向计算收缩呈现。
	/// 文档约定、不做运行时 assert。立即生效于下一次动画；动画中切换 = 替换式重启
	/// （from = 新方向轴当前呈现值，不做跨轴换算——v0.1 已知限制）。
	/// ★ 审计 M7 修复：方向改变 ⇒ **锚定边含义随之改变**（Up 锚底边 / Left 锚右边 /
	///   其余锚左上角）。若展开基准已建立，本方法会**按新方向重取锚定边并重呈收起几何**——
	///   否则「先 SetSize（旧方向）后 SetExpandDirection（新方向）」这一序列（在初始化约定
	///   之外、但 API 允许）会让收起呈现仍是旧方向的形状，而新方向的锚点反推基于错误的轴。
	void SetExpandDirection(ExpandDirection dir) noexcept;

	/// @brief 当前展开方向
	[[nodiscard]] ExpandDirection GetExpandDirection() const noexcept{ return m_direction; }

	// ── 折叠状态 ────────────────────────────────────

	/// @brief 设置展开状态（false = 折叠：沿锚定边收缩到 0，动画结束隐藏内容）
	/// @details 幂等（同状态重复调用无副作用）；无 Window（未挂树/测试）时降级为瞬时切换
	/// （无动画——测试可测性，动画时序由 AnimationTests 独立覆盖）。
	/// 折叠动画期间内容保持可见（父 Clip 天然裁切），onFinished 才 SetContentVisible(false)；
	/// 展开动画开始前 SetContentVisible(true)（避免空面板长高、内容突现）。
	void SetExpanded(bool expanded);

	/// @brief 展开/折叠切换（等价 SetExpanded(!IsExpanded())；默认收起下首次调用 = 展开）
	void Toggle();

	/// @brief 是否展开（默认 false——初始收起，2026-08-30 v1.2 变更）
	[[nodiscard]] bool IsExpanded() const noexcept{ return m_expanded; }

	// ── 内容容器 ────────────────────────────────────

	/// @brief 获取内容容器（裸 Widget——不画背景；用户内容统一 AddChild 进此容器）
	/// @details 容器铺满面板（经 SetSize override 同步）；内容坐标相对容器，不受折叠影响。
	/// 内容布局（Layout 或手动摆位）由用户自理。
	[[nodiscard]] Widget* GetContent() noexcept{ return m_content; }

	/// @brief 面板 + 内容容器尺寸同步（详细设计冻结点 4——先面板后容器；初始/动画/外部改尺寸三场景统一）
	/// @details 收起态语义（2026-08-30 v1.2）：SetSize = 定义展开基准（m_expandedRect），
	/// 实际呈现保持收缩——硬契约：非动画轴保持给出值（Down/Up→宽=w；Left/Right→高=h），
	/// 动画轴呈现 0。位置取当前 GetX()/GetY()，故收起态须先 SetPosition 后 SetSize。
	/// @note public（基类 Widget::SetSize 即 public virtual，override 保持 public——用户须能设定面板尺寸）
	void SetSize(int w, int h) override;

private:

	/// @brief 动画回调：按方向推导位置 + 尺寸（单动画值 s 驱动；详细设计 §2.1 公式）
	/// @details ★ 审计 M7 修复：位置是 `s` 的**纯函数**（由缓存的锚定边 + s 整值推导），
	///          **不再**读「上一次自己写过的位置」再叠加增量——那是逐帧无界漂移的根因。
	void ApplyGeometry(float s);

	/// @brief 从**当前几何**重取锚定边（审计 M7 修复的锚点来源之一）
	/// @details 语义 = 「锚定边此刻在哪」。锚定边在 `s` 变化时**恒不动**（正是"锚定"的定义）
	///          ⇒ 只要在**进入跃迁前**取一次，其后 `ApplyGeometry` 全程用缓存值推导位置即可，
	///          既不做增量累加，也不受自身先前写入的影响。
	///          ★ 保留 v1.3 的原始意图：取的是**当前**几何（而非 `m_expandedRect` 的陈旧位置），
	///          故 Layout 容器排位后锚定边依然正确固定。
	void CacheAnchorFromCurrent() noexcept;

	/// @brief 由**展开基准**（`m_expandedRect`）推导锚定边
	/// @details 专用于「首次 SetSize」：那时当前呈现几何尚未建立（`GetWidth/GetHeight` 多为 0），
	///          而展开基准刚由调用方给出 ⇒ 只有基准能给出正确的锚定边
	///          （Up 的底边 = `basis.y + basis.height`）。
	void CacheAnchorFromBasis() noexcept;

	/// @brief 内容容器可见性统一切换（折叠完成隐藏 / 展开开始前显示）
	void SetContentVisible(bool visible) noexcept;

	/// @brief 当前方向动画轴的呈现值（Down/Up = 高度；Left/Right = 宽度）
	float CurrentAxisValue() const noexcept;

	/// @brief 展开目标轴值（Down/Up = m_expandedRect.height；Left/Right = m_expandedRect.width）
	float AxisTarget() const noexcept;

	static constexpr int kToggleDurationMs = 200;	///< 尺寸过渡时长（内部常量，不开放配置——YAGNI）

	ExpandDirection m_direction = ExpandDirection::Down;	///< 展开方向

	bool m_expanded = false;			///< 展开状态（默认收起——2026-08-30 v1.2 变更；触发点翻转——动画不产生状态）

	Rect m_expandedRect{};				///< 展开基准几何（默认收起下由首次 SetSize 定义；此后 = 最后一次展开态→折叠跃迁记忆）

	// ── 审计 M7 修复：锚定边缓存（位置推导的唯一基准）──────────────────────────
	// ★ 为什么必须有它：位置 = f(s) 是**纯函数**，但「锚定边在哪」只能从**外部**给定一次——
	//   若每帧从 GetX()/GetY() 现取（原实现），读到的就是**上一次自己写过的位置**，
	//   逐帧累加 ⇒ 无界漂移（Up/Left 位置随 s 变；Down/Right 位置不变故未暴露）。
	// ★ 三个不变式：① `m_anchorValid == false` ⇒ 尚未建立（首次 SetSize 时建立）；
	//   ② 有效期内锚定边**恒定**——`ApplyGeometry` 只读不写；
	//   ③ 只在「进入跃迁前」与「外部改基准尺寸」两处重建（见 `.cpp` 的调用点标注）。
	bool m_anchorValid = false;			///< 锚定边是否已建立

	int m_anchorX = 0;					///< 锚定边 X（Down/Up：左边固定；Left：右边固定）
	int m_anchorY = 0;					///< 锚定边 Y（Down/Right：上边固定；Up：底边固定）

	Widget* m_content = nullptr;		///< 内容容器（裸 Widget；树拥有所有权，非拥有指针）

	AnimationToken m_sizeToken;			///< 尺寸动画令牌（RAII；析构自动标脏——回调不打在死对象上）

};

}
