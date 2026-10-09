# DesktopNest M1 详细设计

> **状态**：v1.1（2026-10-09，外部评审吸收——有条件通过 ⇒ 必须项闭合；待复核）
> **输入**：需求稿 `desktopnest-m1-requirements.md` **v1.1**（评审 PASS）· 初设 `desktopnest-m1-preliminary-design.md` **v1.2**（外部评审吸收版——必须项 P1–P5/P7/P8 + 建议项 P6/P9 全闭合）
> **定位**：详设 = 初设契约的**落地形式**（条 42（详设/实施规格必须按最小修改面写））——初设已钉的机制（判据落点 / 四件分离 / 单向数据流）此处不重开，只给实现形态；本稿新增物 = 逐文件分解（△1..△12）+ 头全文 + 实现级契约（C-M1-11..C-M1-21）+ 测试装置（§7）。
> **框架事实基准**：初设 §1 的 F1–F8（带行号）继续有效；本稿新增实测引用另行标注行号。

## §1 范围映射

### 1.1 K1–K7 → 详设落点

| 需求 | 落点 |
|---|---|
| K1 常驻观感 | △4 BoxRoot 圆角背景 + △2 度量（220×320）+ △5 配置期四件套 + △6 初始摆放（贴顶边一排） |
| K2 二态折叠 | △4 折叠按钮 + CollapsiblePanel 消费 + △5 SetBounds（Phase 31）+ C-M1-18 时序 |
| K3 成员列表浮层 | △3 MemberListPopup（两分区）+ △4 BadgeButton 徽标 + C-M1-12 判据 |
| K4 收编/拆出/切换 | △1 BoxModel（纯数据操作）+ △6 协调器（窗口生命周期编排）+ C-M1-13 通知语义 |
| K5 拖动 | △5 配置期四件套——**零应用拖动代码**（HTCAPTION 系统路线，需求 K5 v1.1 修正） |
| K6 假数据装配 | △1 AddMergedBox + △6 BuildFixture（A/B/C 冻结初始态）+ △4 内容区假行 |
| K7 工程 | △7–△12（目录 CMake / 根 CMake / 框架测试集成 ×4） |

### 1.2 模型读法钉死（详设细化——需求稿 §2.2 两层结构的 M1 实现形态）

需求稿把 `Member` 与 `Box` 分为两层。M1 假数据阶段，**收编的框即成员**——「成员」没有独立于「框」的字段（id/title 与 Box 全同）⇒ **MemberId 与 BoxId 是同一 ID 空间**（`using BoxId = int`），「成员表」=「框表」在合并框内的投影（`Box::members` 存被收编框的 id）。理由：① K4 收编/拆出的实体是**框**（窗口消失/重建），不是无窗的纯数据行；② 需求稿承诺 M2 增量是「Member **加**字段而非推翻」——届时 Box 记录加可选文件身份字段即可，本读法不构成阻碍。

### 1.3 通知语义钉死（详设细化——初设 ModelChange 三种 ⇒ 五种）

初设列 `ModelChange{Collect, Detach, SetActive}`。详设发现**同一变更有两个接收方、需要不同反应**，按「什么操作」分种无法区分——改按「**对接收方意味着什么**」分种：

| 种 | 接收方 | 反应 |
|---|---|---|
| `Collected` | 收编方（target） | `Rebuild()`（徽标/标题/内容） |
| `Absorbed` | 被收编方（source） | `m_onAbsorbed()` → 协调器关原生窗口（C-M1-10②） |
| `MemberDetached` | 源框 | `Rebuild()` |
| `Released` | 被拆出的成员 | **无本地操作**——旧树随旧窗销毁（延迟到泵清理点，C-M1-20；C-M1-16⑧），协调器负责重建窗口 |
| `Activated` | 切换方 | `Rebuild()` |

★ 反证（为何三分种不够）：若按操作分种，`Detach` 同时通知源框（应重建）与被拆成员（应无操作）——两者都收到「Detach」，视图无法区分 ⇒ 要么被拆成员误 `Rebuild()`（**悬空访问已销毁的树**），要么源框漏重建。五种即无歧义，且 handler 无需本地状态（无 last-state 跟踪）。

### 1.4 A1–A5 → C → T 认领表（继承初设 §6；覆盖边界细化）

| 验收 | 契约 | 用例 | 覆盖边界 |
|---|---|---|---|
| A1 观感主回路 | C-M1-1/2/5/7/9 | 人工（K6 冻结路径） | 人工——自动用例不重复 |
| A2 模型一致性 | C-M1-6/8/13/20 | T-M1-1/2/3 + T-M1-4⑤⑥ | **流程级自动**（协调器可链入测试——§2.6；T-M1-2④ 真调 `Detach()`）；窗口销毁 = `BoxWindow::IsOpen()` 可观测；回收时序 = C-M1-20 框架契约（测试态延后到 `~Application`——有界） |
| A3 浮层纪律 | C-M1-12/19 | T-M1-4 | ①–④ 无头树 + ⑤ 真实派发 + ⑥ 浮层重开 |
| A4 拖动语义 | C-M1-2/5 | T-M1-6 | **命中路由自动**；拖动顺滑/拖出边缘不失 = 人工（系统行为）——两者分开记录（评审 §4.3） |
| A5 既有全绿 | — | 框架套件 | 基线 366（Phase 31 收口值）+ 新增 6 用例 |

## §2 头全文草案

### 2.1 BoxModel.h（纯数据层——零 ECDI 依赖，T-M1-1..3 无头先测）

```cpp
#pragma once

#include "ECDI/Core/Rect.h"   // placement（收编时保存的对方窗口位置）

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ECDI::DesktopNest {

/// 框 ID——★ 与成员同一 ID 空间（详设 §1.2）：收编的框即成员，
/// 「成员表」是「框表」在合并框内的投影（需求稿 §2.2 两层结构的 M1 读法）。
/// ★ id 即 m_boxes 下标：Box 不销毁（协调器持有至退出——C-M1-10①），
/// id 自 0 单调分配 ⇒ GetBox(box) == m_boxes[box]。
using BoxId = int;

/// 框状态：TopLevel = 独立窗口；Merged = 被收编（作为他人的成员）。
enum class BoxState { TopLevel, Merged };

/// 变更通知种类（详设 §1.3——按「对接收方意味着什么」分种，
/// 非按「什么操作」分种：同一变更的两个接收方反应不同）。
enum class ModelChange {
	Collected,        ///< 收编方：我吸收了一个成员
	Absorbed,         ///< 被收编方：我成为他人成员
	MemberDetached,   ///< 源框：我少了一个成员
	Released,         ///< 被拆出成员：我恢复独立（旧树随旧窗销毁——延迟到泵清理点，C-M1-20；无本地操作）
	Activated         ///< 切换当前成员
};

/// 框记录（纯数据——P3 单向数据流的唯一事实源）。
struct Box {
	BoxId id = -1;
	std::string title;
	BoxState state = BoxState::TopLevel;
	std::vector<BoxId> members;   ///< 收编的框（= 成员）ID；空 = 独立框
	BoxId active = -1;            ///< 当前成员（仅 Merged 有意义）
	Rect placement{};             ///< 收编时保存的对方窗口位置（空 Rect = 无记忆）
};

/// 框模型（纯数据层——ModelProbeTests 先例：demo 逻辑类可无窗构造）。
/// @details 变更 = 纯数据操作（C-M1-6：几何一律作数据参数传入，Model 不读窗口）；
/// 变更后经订阅通知（C-M1-8：注册/注销成对 + 快照派发 + handler 只关自己窗口）。
class BoxModel {
public:
	using ChangeCallback = std::function<void(BoxId /*box*/, ModelChange /*change*/)>;
	/// 订阅句柄（C-M1-11：std::function 不可比较 ⇒ 注销按句柄；单调递增、不复用）。
	using Subscription = std::size_t;

	/// 新增独立框（标题即显示标题）；返回分配的 ID（自 0 单调分配）。
	BoxId AddBox(std::string title);

	/// 新建合并框（K6 冻结初始态用——成员记录一并创建；placement 无记忆
	/// ⇒ 成员日后拆出走级联偏移，§9-6 拍板）。
	BoxId AddMergedBox(std::string title, std::vector<std::string> memberTitles);

	/// 收编：source 作为成员并入 target。
	/// @param sourcePlacement 调用方在调用前读 source 窗口位置（Phase 31
	///        GetBounds——「记 placement 要读」）；Model 只存数据（C-M1-6）。
	/// @pre source 为 TopLevel 且 != target（调用方门控——弹层「加入成员」只列独立框）。
	void Collect(BoxId target, BoxId source, const Rect& sourcePlacement);

	/// 拆出「当前成员」：active 从 members 移除并恢复 TopLevel（placement 记录保留）。
	/// @pre CanDetach(box)（members.size() > 1——弹层拆出项的可用性门控）。
	void Detach(BoxId box);

	/// 切换当前成员（active 指向换人；框尺寸不变——内容重建由通知驱动）。
	void SetActive(BoxId box, BoxId member);

	/// @name 查询
	/// @{
	const Box& GetBox(BoxId box) const;
	/// 「加入成员…」列表 = 全部独立框（含 owner 自身——由弹层排除自身）。
	std::vector<BoxId> GetTopLevelBoxes() const;
	bool CanDetach(BoxId box) const;   ///< members.size() > 1（拆出项可用性判据）
	/// @}

	/// @name 订阅（C-M1-8：注册/注销成对；派发 = 快照——注销不影响本轮）
	/// @{
	Subscription AddHandler(ChangeCallback callback);
	void RemoveHandler(Subscription subscription);
	/// @}

private:
	void Notify(BoxId box, ModelChange change);   ///< 快照派发（C-M1-8②）

	std::vector<Box> m_boxes;
	std::vector<std::pair<Subscription, ChangeCallback>> m_handlers;
	Subscription m_nextSubscription = 0;
	BoxId m_nextId = 0;
};

}   // namespace ECDI::DesktopNest
```

### 2.2 VisualStyle.h（观感常量单一真相源——header-only）

> 为何独立成件（ModelProbe::Palette 先例——`ModelProbe.h:29-43` 的理由原样适用）：
> 颜色/度量被 4 个 TU 共享（BoxView / MemberListPopup / BoxWindow / DesktopNest），
> 放 BoxView.h 会造 BoxView↔MemberListPopup 循环包含，放 BoxModel.h 会破「Model 零
> ECDI 依赖」。★ 初设 §2 的六件清单据此上修为八件（§9 修订记录登记偏离）。

```cpp
#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Rect.h"

namespace ECDI::DesktopNest {

/// 调色板——自绘层（框背景/标题条分隔线/浮层/徽标）的颜色单一真相源。
/// @details 按钮保持框架默认 ButtonStyle（M1 消费现有主题能力，不自绘皮肤——
/// YAGNI；见 §8 D-Q1）。
struct Palette {
	static constexpr Color BoxBackground() noexcept { return Color::FromRGBA8(43, 47, 56, 255); }     ///< 框背景 #2b2f38
	static constexpr Color TitleSeparator() noexcept { return Color::FromRGBA8(16, 18, 22, 255); }    ///< 标题条分隔线 #101216
	static constexpr Color TextForeground() noexcept { return Color::FromRGBA8(232, 232, 232, 255); }   ///< 标题文本 #e8e8e8
	static constexpr Color SecondaryText() noexcept { return Color::FromRGBA8(154, 160, 170, 255); }  ///< 内容假行 #9aa0aa
	static constexpr Color PopupBackground() noexcept { return Color::FromRGBA8(51, 55, 63, 255); }   ///< 浮层背景 #33373f
	static constexpr Color BadgeBackground() noexcept { return Color::FromRGBA8(199, 84, 80, 255); }  ///< 徽标底 #c75450
	static constexpr Color BadgeText() noexcept { return Color::FromRGBA8(255, 255, 255, 255); }      ///< 徽标字 #ffffff
};

/// 布局度量（DIP——公共 API 语义恒为 DIP，Phase 20 口径）。
struct Metrics {
	static constexpr int kWindowWidth = 220;       ///< 框宽（需求 K1：roadmap §8.1 示例值）
	static constexpr int kWindowHeight = 320;      ///< 框高（展开态）
	static constexpr int kCaptionHeight = 32;      ///< 标题条高 = CaptionBar::kDefaultHeight（CaptionBar.h:37）
	static constexpr int kEntryButtonWidth = 96;   ///< 入口按钮宽
	static constexpr int kCollapseButtonSize = 32; ///< 折叠按钮（正方形）
	static constexpr int kBarPad = 8;              ///< 标题条内边距
	static constexpr int kBoxRadius = 12;          ///< 框背景圆角
	static constexpr int kPopupWidth = 180;        ///< 浮层宽
	static constexpr int kPopupRowHeight = 24;     ///< 浮层行高
	static constexpr int kPopupRowPad = 4;         ///< 浮层行侧留白
	static constexpr int kPopupRadius = 8;         ///< 浮层圆角
	static constexpr int kBadgeDiameter = 18;      ///< 徽标圆直径
	static constexpr int kCascadeStep = 28;        ///< 级联偏移步长（§9-6：一律级联）
	static constexpr int kFakeContentRows = 5;     ///< 内容区假数据行数
};

/// 级联偏移（§9-6 简化拍板：不判重叠、一律级联——源框当前位置 + 步长）。
/// @details 纯函数——可无头测试（T-M1-2②）。
inline Rect CascadedPlacement(const Rect& sourceBounds) {
	return Rect{
		sourceBounds.x + Metrics::kCascadeStep,
		sourceBounds.y + Metrics::kCascadeStep,
		sourceBounds.width,
		sourceBounds.height};
}

}   // namespace ECDI::DesktopNest
```

### 2.3 MemberListPopup.h（两分区浮层——裸 Widget 容器）

```cpp
#pragma once

#include "BoxModel.h"   // BoxId（同一 ID 空间——详设 §1.2）
#include "ECDI/Widget/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace ECDI {
class BoxModel;

namespace DesktopNest {

/// 成员列表浮层（K3——裸 Widget 容器，G-5 红线：禁用 Panel——
/// Panel::ContainsPoint 恒 false（Panel.h:16/45）⇒ 空白点击穿透）。
/// @details 两分区（需求 K3）：① 成员单选行（勾选即切换）；
/// ② 成员管理（拆出当前 + 加入其他独立框）。
/// 行 = Button（ConsumesMouseInput() == true——Button.h:33：命中即 HTCLIENT，
/// 按钮可点；浮层不在 caption 区，与标题条拖动语义无交集）。
/// ★ 重建纪律（C-M1-19）：全量重建（Rebuild）只在 Open 时；浮层可见期间的
/// 模型变更只可能是 Activated（成员数不变）⇒ Refresh 原位更新文本、绝不销毁
/// 行——派发链安全论证见 §4 C-M1-19。
class MemberListPopup : public Widget {
public:
	using SelectionCallback = std::function<void(BoxId /*member*/)>;
	using DetachCallback = std::function<void()>;
	using CollectCallback = std::function<void(BoxId /*source*/)>;

	/// 全量重建两分区（每次 Open 前——S3 幂等：先拆旧行再建新行）。
	void Rebuild(const BoxModel& model, BoxId box);

	/// 原位刷新（仅浮层可见时——成员数不变，只更新文本/可用性）。
	void Refresh(const BoxModel& model, BoxId box);

	/// @name 回调（由 BoxView 注册——K4 纪律：纯 Model 操作入口）
	/// @{
	void SetOnMemberSelected(SelectionCallback callback) { m_onSelected = std::move(callback); }
	void SetOnDetachRequested(DetachCallback callback) { m_onDetach = std::move(callback); }
	void SetOnCollectRequested(CollectCallback callback) { m_onCollect = std::move(callback); }
	/// @}

	void OnPaint(PaintContext& ctx, int x, int y) override;   // 圆角背景（自绘）

private:
	/// 拆旧行（RemoveChild 返 unique_ptr——离开作用域即销毁，Widget.h:65）。
	void ClearRows();
	Widget* MakeRow(const std::string& text, bool enabled);   // 行工厂（Button）

	SelectionCallback m_onSelected;
	DetachCallback m_onDetach;
	CollectCallback m_onCollect;

	std::vector<Widget*> m_memberRows;   ///< 分区① 行（非拥有——子树拥有）
	Widget* m_detachRow = nullptr;        ///< 分区② 拆出行（非拥有）
	std::vector<Widget*> m_joinRows;      ///< 分区② 加入行（非拥有）
};

}   // namespace DesktopNest
}   // namespace ECDI
```

### 2.4 BoxView.h（BoxRoot 判据容器 / BadgeButton 徽标按钮 / BoxView 视图）

```cpp
#pragma once

#include "BoxModel.h"
#include "VisualStyle.h"

#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Widget.h"

#include <functional>
#include <memory>

namespace ECDI {
class BoxWindow;
class MemberListPopup;

namespace DesktopNest {

/// 框根容器（BoxView 装配的根——点外关闭判据的落点，P1）。
/// @details OnPaint 发 DrawRoundedRect 铺满客户区（框背景，K1）+ 标题条
/// 分隔线；OnMouseButtonDown = 点外关闭判据（C-M1-12 三判据）。
/// 坐标约定：BoxRoot 满铺客户区 ⇒ 事件客户区坐标 == BoxRoot 局部坐标
/// （初设 §1.1.2 伪码的同款前提）。
class BoxRoot : public Widget {
public:
	void SetPopup(MemberListPopup* popup) noexcept { m_popup = popup; }
	void SetEntryButton(Widget* entryButton) noexcept { m_entryButton = entryButton; }

	void OnPaint(PaintContext& ctx, int x, int y) override;
	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;

private:
	MemberListPopup* m_popup = nullptr;      ///< 非拥有——BoxRoot 子树（末位 AddChild）
	Widget* m_entryButton = nullptr;          ///< 非拥有——标题条入口按钮（判据豁免）
};

/// 徽标按钮（标题条入口——成员数徽标，K3 视觉标识）。
/// @details 按钮本体 = 框架默认 ButtonStyle（Button::OnPaint）；徽标 =
/// 自绘右上圆 + 数字（DrawRoundedRect + DrawText——PaintContext.h:38/47）。
class BadgeButton : public Button {
public:
	using Button::Button;   // 文本构造沿用（Button.h:26-28）
	void SetBadgeCount(int count) noexcept { m_badgeCount = count; }
	int GetBadgeCount() const noexcept { return m_badgeCount; }   // 测试观测点（T-M1-1④）

protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:
	int m_badgeCount = 0;
};

/// 框视图（P2 四件分离之 View——装配 + 重建，不持 Model 数据）。
class BoxView {
public:
	/// 协调器动作（窗口生命周期类操作归 main 的注册表——View 不建/关窗）。
	enum class Action { Detach, Collect };
	using ActionCallback = std::function<void(Action /*action*/, BoxId /*source*/)>;
	using AbsorbedCallback = std::function<void()>;
	using PopupOpeningCallback = std::function<void()>;

	BoxView(BoxModel& model, BoxId box);
	~BoxView();   // C-M1-8③：析构注销订阅（RemoveHandler(m_subscription)）

	/// 装配（S3 幂等：全量重建——宿主根可为新根，C-M1-16⑧ 的重建入口）。
	void Assemble(Widget& hostRoot);
	/// 解装（协调器关窗后调用——非拥有指针清空；Assemble 的前置不变量）。
	void Disassemble() noexcept;

	/// 几何请求通道（C-M1-14：可空——nullptr = 无头测试态，几何请求跳过）。
	void SetBoxWindow(BoxWindow* window) noexcept { m_window = window; }

	/// @name 回调（由协调器注册）
	/// @{
	void SetOnAction(ActionCallback callback) { m_onAction = std::move(callback); }
	void SetOnAbsorbed(AbsorbedCallback callback) { m_onAbsorbed = std::move(callback); }
	void SetOnPopupOpening(PopupOpeningCallback callback) { m_onPopupOpening = std::move(callback); }
	/// @}

	/// 订阅 handler（Model → UI 单向数据流的接收端——C-M1-13）。
	void OnModelChanged(BoxId box, ModelChange change);

	/// 浮层开/关/切换（P6 切换契约——入口按钮点击回调的权威入口）。
	void OpenPopup();
	void ClosePopup();
	void TogglePopup();

	/// 折叠切换（K2——唯一二态开关；几何请求经 C-M1-14 通道）。
	void ToggleCollapse();

	/// 浮层内动作（MemberListPopup 回调——纯 Model 操作，K4 纪律）。
	void SelectMember(BoxId member);
	void RequestDetach();
	void RequestCollect(BoxId source);

	/// @name 测试观测面
	/// @{
	bool IsPopupVisible() const noexcept;
	int GetBadgeCount() const noexcept;
	/// @}

private:
	void Rebuild();   // S3 幂等重建：标题（C-M1-17）+ 徽标 + 内容 + 浮层（C-M1-19 R2）

	BoxModel& m_model;
	BoxId m_box;
	BoxWindow* m_window = nullptr;            // C-M1-14（几何通道——可空）
	Widget* m_root = nullptr;                  // BoxRoot（非拥有——宿主树拥有）
	Widget* m_titleBar = nullptr;              // 标题条容器（非拥有）
	BadgeButton* m_entryButton = nullptr;      // 入口按钮（非拥有）
	Widget* m_titleLabel = nullptr;            // 标题 Label（非拥有）
	Button* m_collapseButton = nullptr;        // 折叠按钮（非拥有）
	CollapsiblePanel* m_contentPanel = nullptr;   // 内容面板（非拥有）
	MemberListPopup* m_popup = nullptr;        // 浮层（非拥有——BoxRoot 子树，常驻隐藏态）
	BoxModel::Subscription m_subscription = 0;   // 订阅句柄（析构注销）
	ActionCallback m_onAction;
	AbsorbedCallback m_onAbsorbed;
	PopupOpeningCallback m_onPopupOpening;
};

}   // namespace DesktopNest
}   // namespace ECDI
```

### 2.5 BoxWindow.h（窗口生命周期包装——C-M1-10/16 的落地）

```cpp
#pragma once

#include "VisualStyle.h"

#include "ECDI/Application/Application.h"   // Create（唯一构造入口——Window.h:42）
#include "ECDI/Core/Rect.h"
#include "ECDI/Window/Window.h"

namespace ECDI {
class BoxView;

namespace DesktopNest {

/// 框窗口包装（C-M1-10 BoxWindow 生命周期契约的落地）。
/// @details 持有 Window*（Application 容器拥有——Window.h:40 所有权契约，
/// 调用者不得 delete）。Create 可重复调用（C-M1-10③/16）：每次 = 释放旧
/// HWND + 建新窗 + 全量重建视图。Close 只关原生窗口（Release——HWND 销毁、
/// 幂等，Window.h:77），不销毁 BoxView——视图由协调器注册表持有至退出
/// （C-M1-10①）。
class BoxWindow {
public:
	BoxWindow() = default;

	/// 创建/重建原生窗口。
	/// @param placement 初始边界（Phase 31——请求语义，读回以 GetBounds 为准）。
	/// @pre 配置期四件套（SetChromeMode/SetCaptionHeight/SetResizeInset/
	///        SetWindowLayer）均在 Show 前调用（F2–F4——Show 后记 Warning 并忽略）。
	void Create(Application& application, const Rect& placement,
	            BoxView& view, std::string title);

	/// 关闭原生窗口（Release——幂等）。
	/// @details ★ 关闭后**置 m_window = nullptr**（C-M1-20）：Window C++ 对象的销毁是
	/// **延迟的**（WM_DESTROY → Application 移入 m_deferredDestroy → 消息循环清理点
	/// 才 ~Window——Application.cpp:127-153 / :109-118）⇒ 包装对象不得保留该指针，
	/// 否则 GetBounds/SetBounds 转发即悬空解引用。
	void Close();

	/// 显示（Create 后；A1 人工回路入口）。
	void Show();

	/// 运行期几何（Phase 31——请求语义）。
	/// @note Close 后 `m_window == nullptr` ⇒ GetBounds 返回空 Rect（**不得**当有效位置用——
	///       级联 fallback 的前提见 △6 Detach）。
	void SetBounds(const Rect& bounds);
	[[nodiscard]] Rect GetBounds() const;

	/// @name 观测面
	/// @{
	bool IsOpen() const noexcept { return m_open; }   // 窗口存活（A2 可观测）
	Window* GetWindow() const noexcept { return m_window; }   // 测试/协调器缝（Close 后为 nullptr）
	/// @}

private:
	Window* m_window = nullptr;   ///< 非拥有——Application 容器（Window.h:40）；Close 后置空（C-M1-20）
	bool m_open = false;
};

}   // namespace DesktopNest
}   // namespace ECDI
```

### 2.6 DesktopNest.h（协调器——可链入测试的应用类）

> 为何从 main.cpp 提出（ModelProbeTests 先例的原样应用）：ModelProbe 的
> 全部可测逻辑在 `ModelProbePage`（demo 逻辑类，链入 TEST_SOURCES），
> `main.cpp` 只做壳。本稿把窗口生命周期编排（收编/拆出/级联）提为
> `DesktopNestApp` ⇒ A2 **流程级**判据可自动执行（初设的测试策略只覆盖
> 原语级）；main.cpp 退化为薄壳（wWinMain → Run()，§2.7）。

```cpp
#pragma once

#include "BoxModel.h"
#include "BoxView.h"
#include "BoxWindow.h"
#include "VisualStyle.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Rect.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ECDI::DesktopNest {

/// 框登记项——一个框的窗口 + 视图（协调器持有至退出，C-M1-10①）。
struct BoxEntry {
	std::unique_ptr<BoxWindow> window;
	std::unique_ptr<BoxView> view;
};

/// DesktopNest 应用（协调器——窗口生命周期的编排点）。
/// @details ★ 流程纪律（K4 v1.1 单向数据流的应用形态）：
/// ① 收编/拆出 = 「读几何（Phase 31）→ Model 变更 → 通知驱动视图/窗口」——
/// 禁止「点击后直接删/建 Widget、顺便改 members」（需求 K4 v1.1 纪律）；
/// ② 收编/拆出动作**先关发起浮层**再执行 Model 操作（C-M1-19 R3）。
class DesktopNestApp {
public:
	/// K6 冻结初始态：A（独立）· B（独立）· C（合并框 = [C1, C2]）。
	/// @param show 生产 = true；测试 = false（隐藏窗——装置不扰动桌面）。
	struct Fixture { BoxId a; BoxId b; BoxId c; };
	Fixture BuildFixture(bool show = true);

	/// 生产入口：BuildFixture(true) → 消息循环（阻塞至退出）。
	int Run();

	/// @name 流程（A2 自动判据的驱动面——与弹层动作回调同一入口）
	/// @{
	void Collect(BoxId target, BoxId source);   ///< 收编（读 placement → Model）
	void Detach(BoxId box);                     ///< 拆出当前成员（级联/恢复 → Model → 重建窗）
	/// @}

	/// @name 测试观测面
	/// @{
	BoxModel& GetModel() noexcept { return m_model; }
	BoxEntry& GetEntry(BoxId box) { return m_boxes.at(box); }
	Application& GetApplication() noexcept { return m_app; }
	/// @}

private:
	BoxId MakeBox(const std::string& title, const Rect& placement, bool show);
	BoxId MakeMergedBox(const std::string& title,
	                    const std::vector<std::string>& members,
	                    const Rect& placement, bool show);
	void CloseBoxEntry(BoxId box);         ///< 关原生窗口 + 视图解装（C-M1-10②/16）
	void CloseOtherPopups(BoxId keepOpen); ///< 单浮层纪律（开新关旧）
	void OnBoxAction(BoxId owner, BoxView::Action action, BoxId source);

	Application m_app;                       ///< 进程唯一（Application.h:43 语义）
	BoxModel m_model;                        ///< 唯一事实源（P3）
	std::map<BoxId, BoxEntry> m_boxes;     ///< id → 登记（持有至退出）
};

}   // namespace ECDI::DesktopNest
```

### 2.7 main.cpp（薄壳——全部装配在 DesktopNestApp）

```cpp
#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

// Windows.h 宏防护（ModelProbe/main.cpp:3-7 先例——DrawText 等宏不污染 ECDI 头声明）
#ifdef DrawText
#undef DrawText
#endif

#include "DesktopNest.h"

/// @brief 入口（薄壳——装配与逻辑全部在 DesktopNestApp::BuildFixture，
/// 与逻辑分离以便链入测试——详设 §2.6）。
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
	ECDI::DesktopNest::DesktopNestApp app;
	return app.Run();
}
```

## §3 实现分解（△1..△12）

> 全部为**新增文件**（应用侧）或**追加行**（构建/登记）——无既有文件修改面
> （条 42：每个 △ 的改动面即其全文/追加行，其余行保持现状）。

### △1 BoxModel.h/.cpp（新——纯数据层）

- `.h` 全文 = §2.1。
- `.cpp` 要点：
  - `AddBox`：`m_boxes.push_back(Box{m_nextId, std::move(title), BoxState::TopLevel, {}, -1, {}})` → 返回 `m_nextId++`。
  - `AddMergedBox`：先建合并框记录（`members` = 成员 id 列表、`active` = 首成员），再逐个建成员记录（`state = Merged`、`placement` 空 = 无记忆）。
  - `Collect`：`target.members.push_back(source)` · `source.state = Merged` · `source.placement = sourcePlacement` → `Notify(target, Collected)` + `Notify(source, Absorbed)`。
  - `Detach`：`member = box.active` → 从 `box.members` 移除 → `member.state = TopLevel`（`placement` 记录保留）→ `box.active = box.members.front()`（保有效指向）→ `Notify(box, MemberDetached)` + `Notify(member, Released)`。
  - `SetActive`：`box.active = member` → `Notify(box, Activated)`。
  - `Notify`（C-M1-8② 快照派发）：`auto snapshot = m_handlers;`（值拷贝）→ 遍历副本回调——注销只改活列表、不影响本轮。
  - `RemoveHandler`：按句柄 erase-remove（句柄单调递增不复用——C-M1-11）。

### △2 VisualStyle.h（新——header-only）

- 全文 = §2.2。零实现文件。

### △3 MemberListPopup.h/.cpp（新——两分区浮层）

- `.h` 全文 = §2.3。
- `.cpp` 要点：
  - `OnPaint`：`ctx.DrawRoundedRect(Rect{x, y, w, h}, Metrics::kPopupRadius, Palette::PopupBackground())`（坐标约定同 Button::OnPaint——Button.cpp:244-248 先例）。
  - **布局表（DIP，相对浮层；n = 成员数，m = 可加入独立框数）**：

    | 行 | 矩形 |
    |---|---|
    | 成员行 i | `(4, 4 + i×24, 172, 24)` |
    | 分隔线 | y = `8 + n×24`（DrawLine 1px，TitleSeparator） |
    | 拆出行 | `(4, 12 + n×24, 172, 24)` |
    | 「加入成员」头 | `(4, 36 + n×24, 172, 20)`——Label，非交互 |
    | 加入行 j | `(4, 56 + n×24 + j×24, 172, 24)` |

    浮层高 = `60 + n×24 + m×24`。
  - `Rebuild`：`ClearRows()` → 按布局表建行（成员行文本 = `[X] 标题`（active）/ `[ ] 标题`；拆出行 = `拆出「<active 标题>」`，`SetEnabled(CanDetach)`；加入行 = `加入 <框标题>`，排除 owner 自身）→ 逐行 `SetOnClick` 绑回调（捕获 **id 值**，不捕获控件指针——C-M1-19 安全论证的一环）。
  - `Refresh`（仅 `IsVisible()` 时被调——C-M1-19 R2）：原位 `SetText` 成员行勾选前缀 + 拆出行标题/可用性；**不创建、不销毁任何控件**。

### △4 BoxView.h/.cpp（新——判据容器 / 徽标按钮 / 视图）

- `.h` 全文 = §2.4。
- `.cpp` 要点：
  - `BoxRoot::OnPaint`：① 圆角背景铺满（`DrawRoundedRect(Rect{x, y, w, h}, kBoxRadius, BoxBackground)`）② 标题条分隔线（`DrawLine` at `y + kCaptionHeight`，1px，TitleSeparator）。
  - `BoxRoot::OnMouseButtonDown`（C-M1-12 判据——实现形态）：

    ```cpp
    void BoxRoot::OnMouseButtonDown(const MouseButtonDownEvent& event){
    	if (m_popup == nullptr || !m_popup->IsVisible())
    		return;                                    // ① 浮层未开——无判据
    	Widget* hit = HitTest(event.GetMouseX(), event.GetMouseY());
    	if (hit == nullptr)
    		return;                                    // ② 防御（RootWidget 全覆盖）
    	if (m_popup->Contains(hit))
    		return;                                    // ③ 浮层内（含空白）——不穿透、不关闭
    	if (hit == m_entryButton)
    		return;                                    // ④ 入口按钮豁免（P6——开/关由其点击回调权威切换）
    	m_popup->SetVisible(false);                    // ⑤ 点外（含标题条其他按钮/内容区）→ 关闭
    	Invalidate();
    }
    ```

    ★ ③ 用既有 `Widget::Contains`（Widget.h:280——递归后代检测，AddChild 防环同源）替代初设伪码的手写 `GetParent()` 上溯——语义等价（hit ∈ 浮层子树 ⇔ Contains），既有 API 优先。
  - `BadgeButton::OnPaint`：先 `Button::OnPaint(ctx, x, y)`（本体）→ `m_badgeCount > 0` 时右上徽标：`DrawRoundedRect(Rect{x + w − d − 2, y + 2, d, d}, d/2, BadgeBackground)` + `DrawText`（`std::to_string(m_badgeCount)`，居中偏移经 `ctx.MeasureText(Font(), text)` 算——PaintContext.h:70「对齐偏移由控件算好」）。
  - `BoxView::Assemble`（装配序列——CollapsiblePanel 初始化约定 CollapsiblePanel.h:23 同款）：
    1. 解装旧树（`m_root` 非空 ⇒ 从旧宿主 `RemoveChild`，unique_ptr 离作用域销毁）；
    2. `hostRoot.AddChild(std::make_unique<BoxRoot>())` → `SetPosition(0,0)` · `SetSize(宿主宽, 宿主高)`；
    3. 标题条容器（裸 Widget，`(0, 0, w, kCaptionHeight)`）→ 入口 `BadgeButton`（`(kBarPad, 0, kEntryButtonWidth, kCaptionHeight)`，文本「成员」）→ 标题 `Label`（`(kBarPad + kEntryButtonWidth + kBarPad, 0, w − …, kCaptionHeight)`，SetTextColor(TextForeground)）→ 折叠 `Button`（`(w − kCollapseButtonSize, 0, kCollapseButtonSize, kCollapseButtonSize)`，文本见下）；
    4. 内容面板：`CollapsiblePanel` → `SetExpandDirection(ExpandDirection::Down)` → `SetPosition(0, kCaptionHeight)` → `SetSize(w, kWindowHeight − kCaptionHeight)` → **`SetExpanded(true)`**（框架默认收起——CollapsiblePanel.h:96；M1 框默认展开）→ 内容 = `GetContent()` 容器 + kFakeContentRows 假行 Label（`(kBarPad, i×24, w − 2×kBarPad, 24)`，文本「<active 标题> · 假数据行 N」，SetTextColor(SecondaryText)）；
    5. 浮层（**末位 AddChild**——z 序最顶，F5）：`MemberListPopup` → `SetPosition(0, kCaptionHeight)` · `SetSize(kPopupWidth, 初始高)` · `SetVisible(false)`（常驻隐藏态）；
    6. 接线：弹层三回调 → `SelectMember`/`RequestDetach`/`RequestCollect`；入口按钮 `SetOnClick` → `TogglePopup`（P6）；折叠按钮 `SetOnClick` → `ToggleCollapse`。
  - `ToggleCollapse`（C-M1-18 时序——几何瞬时先行）：

    ```cpp
    void BoxView::ToggleCollapse(){
    	const bool expanded = !m_contentPanel->IsExpanded();
    	if (m_window){                                   // C-M1-14：无头态跳过几何
    		Rect bounds = m_window->GetBounds();
    		bounds.height = expanded ? Metrics::kWindowHeight : Metrics::kCaptionHeight;
    		m_window->SetBounds(bounds);                 // 窗口几何瞬时
    	}
    	m_contentPanel->SetExpanded(expanded);           // 内容子树随后（内置 200ms 过渡）
    	m_collapseButton->SetText(expanded ? "-" : "+"); // 二态开关文案
    	if (m_root) m_root->Invalidate();
    }
    ```
  - `Rebuild`（S3 幂等）：① 标题 Label `SetText`（派生标题 C-M1-17）② 入口按钮 `SetBadgeCount(members.size())` ③ 折叠按钮文案按 `IsExpanded()` ④ 内容区：清 `GetContent()` 子节点 → 重建 kFakeContentRows 假行 ⑤ **浮层（C-M1-19 R2）：`if (m_popup->IsVisible()) m_popup->Refresh(model, m_box)`——不可见 ⇒ 跳过（行留待下次 Open 全量重建）** ⑥ `m_root->Invalidate()`。
    ★ `SetText` 不请求重绘（架构边界条约定）——每次 `SetText` 后 `Invalidate()`。
  - `OnModelChanged`（C-M1-13）：

    ```cpp
    void BoxView::OnModelChanged(BoxId box, ModelChange change){
    	if (box != m_box)
    		return;                                      // 订阅是广播——只认自己
    	switch (change){
    	case ModelChange::Absorbed:  m_onAbsorbed();  break;   // 协调器关原生窗口
    	case ModelChange::Released:  break;                  // 树随旧窗已销毁——协调器负责重建
    	default:                     Rebuild();        break;   // Collected/MemberDetached/Activated
    	}
    }
    ```
  - `OpenPopup`：`m_onPopupOpening()`（协调器关其他浮层——单浮层纪律）→ `m_popup->Rebuild(model, m_box)` → `SetVisible(true)` → `Invalidate()`。
  - `RequestDetach` / `RequestCollect`（C-M1-19 R3 先关浮层）：`ClosePopup()` → `m_onAction(...)`。
  - `Disassemble`：`m_root = m_titleBar = m_entryButton = m_titleLabel = m_collapseButton = m_contentPanel = m_popup = nullptr`（订阅**不**注销——注销只在析构，C-M1-8③）。

### △5 BoxWindow.h/.cpp（新——生命周期契约落地）

- `.h` 全文 = §2.5。
- `.cpp` 要点（`Create` 十步——C-M1-16 重建清单的实现形态）：

  ```cpp
  void BoxWindow::Create(Application& application, const Rect& placement,
                         BoxView& view, std::string title){
  	if (m_window)
  		m_window->Release();                    // ① 幂等——重复 Create 先释放旧 HWND
  	m_window = &application.Create(std::move(title),   // ② 唯一构造入口（Window.h:42）
  	                               Metrics::kWindowWidth, Metrics::kWindowHeight);
  	m_window->SetChromeMode(ChromeMode::Borderless);   // ③ F2——Show 前
  	m_window->SetCaptionHeight(Metrics::kCaptionHeight); // ④ F3——标题条行为区（K5 前提）
  	m_window->SetResizeInset(0);                        // ⑤ K5：不可缩放（Phase 12 既有契约）
  	m_window->SetWindowLayer(WindowLayer::Desktop);     // ⑥ F4——K1 常驻（Win+D 后仍可见）
  	view.SetBoxWindow(this);                            // ⑦ C-M1-14 几何通道注入
  	view.Assemble(m_window->GetRootWidget());           // ⑧ C-M1-16⑧ S3 全量重建
  	m_window->SetBounds(placement);                     // ⑨ Phase 31——请求语义
  	m_open = true;                                      // ⑩ 存活标记（A2 可观测）
  }
  ```

  `Close`（C-M1-20）：`if (m_window) { m_window->Release(); m_window = nullptr; m_open = false; }`
  ——Release 幂等（重复 Close 安全）；**置空是必须的**（Window 对象延迟销毁 ⇒ 不置空即悬空指针）。
  `SetBounds`/`GetBounds`：转发 `m_window`（`m_window == nullptr` ⇒ 空 Rect——Close 后与「平台窗口未就绪」同语义，Window.h:168）。

### △6 DesktopNest.h/.cpp（新——协调器）

- `.h` 全文 = §2.6。
- `.cpp` 要点：
  - `BuildFixture`：`MakeBox("独立框 A", {16, 16, 220, 320})` · `MakeBox("独立框 B", {252, 16, …})` · `MakeMergedBox("合并框 C", {"C1", "C2"}, {488, 16, …})`（贴顶边一排——需求 §5 风险继承 R-3 的「框倾向贴边」摆放）。
  - `MakeBox`/`MakeMergedBox`：建 `BoxEntry` → 视图注册三回调（`SetOnAction` → `OnBoxAction`；`SetOnAbsorbed` → `CloseBoxEntry`；`SetOnPopupOpening` → `CloseOtherPopups`）→ `window->Create(m_app, placement, *view, title)` → `if (show) window->Show()`。
  - `Collect`（流程纪律①）：

    ```cpp
    void DesktopNestApp::Collect(BoxId target, BoxId source){
    	const Rect placement = m_boxes[source].window->GetBounds();  // 记 placement 要读
    	m_model.Collect(target, source, placement);                  // 通知驱动余下一切
    }
    ```
  - `Detach`：落点 = `recorded.width > 0 ? recorded : CascadedPlacement(sourceBounds)`（恢复优先、无记忆级联——§9-6）→ `m_model.Detach(box)` → 被拆成员**同一 BoxWindow 包装对象**重建：`entry.window->Create(m_app, placement, *entry.view, title)` + `Show()`。
    ★ **fallback 前提显式化**（评审 §3.2）：级联基准 = **源框自己**的当前窗口边界（`m_boxes[box].window->GetBounds()`）——源框在拆出动作期间必然打开（动作由源框浮层发起），故前提恒成立；契约仍写明「**只在 `IsOpen()` 且读回 `width > 0` 时使用当前边界**，否则落默认落点（源框位置 + 步长，不读已关窗口）」——防日后重建流程改动时误依赖已关闭窗口的边界。
  - `CloseBoxEntry`：`entry.window->Close()` + `entry.view->Disassemble()`（C-M1-10② + C-M1-16 解装不变量）。
    ★ **顺序不可交换**（C-M1-20）：Close 后旧树仍存活到泵清理点 ⇒ `Disassemble()` 必须紧接着断掉视图的非拥有指针，否则清理点前任何视图访问都是悬空。
  - `CloseOtherPopups`：遍历 `m_boxes`，`id != keepOpen` ⇒ `entry.view->ClosePopup()`。

### △7 examples/DesktopNest/CMakeLists.txt（新——沿 ModelProbe 模板）

```cmake
# DesktopNest 应用（examples/——链接框架静态库 ECDI；目标名 desktopnest——K7）

add_executable(desktopnest WIN32
    main.cpp
    BoxModel.cpp
    BoxView.cpp
    BoxWindow.cpp
    MemberListPopup.cpp
    DesktopNest.cpp
)

target_include_directories(desktopnest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"            # 同目录头
)

target_link_libraries(desktopnest PRIVATE ECDI)

target_compile_definitions(desktopnest PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(desktopnest PRIVATE /utf-8)   # 源码含中文注释/字面量
endif()
if(MINGW)
    target_compile_options(desktopnest PRIVATE -municode)
    target_link_options(desktopnest PRIVATE -municode -static)   # 单 exe 分发
endif()
```

### △8 根 CMakeLists.txt（+1 行）

- `add_subdirectory(examples/DesktopNest)`（需求 K7——ModelProbe 先例）。

### △9 ECDI/CMakeLists.txt（+6 行——O5 拍板 (a) 的完整形态）

- TEST_SOURCES 追加 **5 个实现文件**（BoxModel / MemberListPopup / BoxView / BoxWindow / DesktopNest——main.cpp 不入：wWinMain 与测试入口冲突，test_main.cpp:5 先例）；
- `target_include_directories(ecdi_tests …)` 追加 `"${CMAKE_CURRENT_SOURCE_DIR}/examples/DesktopNest"`。
- ★ 影响面如实修正：初设 v1.2 C-M1-1 估「框架 CMake +1~2 行」按 ModelProbe **单文件**先例估——实测 5 源 ⇒ **+6 行**（公共 API 0 / 框架实现 0 不变——见 §5）。

### △10/△11 ECDI/src/Tests/RunAllTests.h/.cpp（各 +1 行）

- `.h`：`void RegisterDesktopNestTests();   ///< Phase M1：DesktopNest（T-M1-1..6）`（ModelProbeTests 声明先例——RunAllTests.h:25）。
- `.cpp`：`RegisterDesktopNestTests();`（调用与定义成对——条 127（注册↔定义成对必须集合做差））。

### △12 ECDI/src/Tests/DesktopNestTests.cpp（新——6 用例）

- 全文按 §7.2 逐用例判据实现；装置按 §7.1 三层；注册按 §7.3 口径。

## §4 契约（实现级——C-M1-11..C-M1-21；初设 C-M1-1..C-M1-10 为前提，不再重述）

| # | 契约 |
|---|---|
| C-M1-11 | **订阅句柄**：`AddHandler(cb) → Subscription`（单调递增、不复用）；`RemoveHandler(Subscription)`。理由：`std::function` 不可比较 ⇒ 注销无法按值匹配；句柄是「具有明确生命周期的订阅对象」的最小形态（非事件总线——P1 方案 (a) 不变）。 |
| C-M1-12 | **点外关闭判据**（BoxRoot::OnMouseButtonDown，△4 给出实现形态）：① 浮层未开 ⇒ return；② hit == nullptr ⇒ return；③ `m_popup->Contains(hit)`（Widget.h:280）⇒ return（浮层内含空白——不穿透、不关闭）；④ `hit == m_entryButton` ⇒ return（**入口按钮豁免**——P6 的开/关由其点击回调权威切换；若不豁免，Down 阶段判据先关浮层、Up 阶段回调见「已关」又打开 ⇒ 切换失效）；⑤ 其余 ⇒ 关闭。 |
| C-M1-13 | **通知五种**（§1.3）：按「对接收方意味着什么」分种（Collected / Absorbed / MemberDetached / Released / Activated）；handler 无本地状态。 |
| C-M1-14 | **几何通道可空**：`BoxView::SetBoxWindow(BoxWindow*)`——`nullptr` = 无头测试态，几何请求（折叠 SetBounds）跳过；生产由 `BoxWindow::Create` 注入（△5 步⑦）。细化初设 C-M1-9 的「持 BoxWindow&」——& 形式使无头测试无法构造 BoxView（ModelProbeTests 无窗构造先例）。 |
| C-M1-15 | **框架测试集成线**（O5 (a) 完整形态）：ecdi_tests = TEST_SOURCES 追加 5 源 + include 目录 1 行（**+6 行**，非初设估的 +1~2——ModelProbe 是单文件先例）；RunAllTests.h/.cpp 各 +1 行登记；公共 API 0 / 框架实现文件 0 不变。 |
| C-M1-16 | **Create 重建清单**（BoxWindow::Create 十步——△5）：① 释放旧 HWND（幂等；**同时置 `m_window = nullptr`**——见 C-M1-20(b)）② Application::Create ③–⑥ 配置期四件套 ⑦ 注入几何通道 ⑧ `BoxView::Assemble` 在新根上全量重建（S3）⑨ SetBounds(placement) ⑩ 存活标记。**重建 = 控件树 + 几何，不重建 BoxView 对象与订阅**（订阅构造期一次注册、析构期注销——C-M1-8③）。**解装不变量**：协调器 `CloseBoxEntry` = `Close()` + `view->Disassemble()`（**顺序不可交换**——C-M1-20(c)）——Assemble 只在「已解装或首次」时被调（悬空树防护）。附：拆出重建的框 = **展开态**（折叠态是纯视图态、不随 Model 持久——M1 不保留，留 D-O4）。 |
| C-M1-17 | **派生标题**：显示标题 = `members.empty() ? box.title : GetMember(active).title`（K3①「标题+内容整体换成该成员」）；`Rebuild` 时 `SetText` + `Invalidate()`。 |
| C-M1-18 | **折叠时序**：窗口几何瞬时（SetBounds 请求语义）**先行**，内容子树随后（`CollapsiblePanel::SetExpanded` 内置 200ms 过渡，kToggleDurationMs = 200——CollapsiblePanel.h:92）。已知观感折中：折叠时内容在剩余高度内被窗口边界裁切（父 Clip 语义——CollapsiblePanel.h:54）⇒ 实际观感 = 内容瞬隐 + 窗口瞬切；展开 = 窗口瞬切 + 内容 200ms 过渡。需求 K2「动画可关」——观感里程碑，平滑度非验收项；窗口跟随动画的平滑收缩 = M2+（需动画 tick 驱动 SetBounds——框架无此钩子，D-O2）。 |
| C-M1-19 | **派发安全三规则**（本稿新发现——初设层不可见）：框架冒泡派发在「调用后才读父」（`Application.cpp:294-302` Down / `318-326` Up：`current->OnMouseButtonDown(event); current = current->GetParent();`）⇒ **点击回调若销毁被点控件或其任意祖先 = 悬空读（UB）**。三规则共同保证回调执行期间「被点行 → 浮层 → BoxRoot」全链存活：<br>**R1** 浮层行全量重建只在 `OpenPopup` 时（`Rebuild`）；<br>**R2** 浮层可见期间的模型变更**只可能是 `Activated`**（单线程交互 + R3 ⇒ 成员数不变）⇒ `Refresh` 原位更新文本、**不销毁任何控件**；<br>**R3** 收编/拆出动作回调**先 `ClosePopup()` 再执行 Model 操作**——通知到达时浮层已隐藏 ⇒ `Rebuild` 跳过浮层（△4 Rebuild 步⑤）⇒ 被点行不销毁。 |
| C-M1-20 | **窗口回收时序与包装指针纪律**（v1.1 新增——吸收外部评审 §2/§3.2；实测链条带行号）：① `BoxWindow::Close()` → `Window::Release()`（`Window.cpp:147-157`）**只销毁 HWND**（`m_platformWindow->Release()`——`Win32PlatformWindow.cpp:373`）；② HWND 销毁 → 同步 `WM_DESTROY` → `Application::OnWindowDestroyed`（`Application.cpp:127-153`）把 Window 从 `m_windows` **移入 `m_deferredDestroy`**（`Application.h:166-168`，unique_ptr 所有权转移）；③ `ProcessDeferredDestroy`（`Application.cpp:109-118`）由平台消息循环**在每条消息处理后**调用（`Application.cpp:41` 注入回调）⇒ `m_deferredDestroy.clear()` ⇒ `~Window` ⇒ **根控件树随 Window 对象一并销毁**。<br>⇒ 纪律：**(a) 旧树不泄漏**——生产态（跑 `Run()`）在 Close 后一条消息内销毁；测试态（不泵消息）延后到 `~Application`，**有界**（框架契约原文 `window-ownership.md` §4.3；`Application.cpp:112-116` 自注「消费者**不得**假设窗口关闭后 Window 对象立即析构」）；**(b) `Close()` 必须置 `m_window = nullptr`**——销毁是延迟的，保留指针即悬空（△5）；**(c) `CloseBoxEntry` 的 `Close` → `Disassemble` 顺序不可交换**——清理点前旧树仍存活，视图非拥有指针必须先断；**(d) C-M1-19 前提不破**——旧树销毁发生在泵清理点（消息处理**之后**），不在点击回调的派发栈内；**(e) `GetBounds()` 空 Rect 不得当有效位置**（评审 §3.2）——级联 fallback 前提 = 源窗口 `IsOpen()` 且读回 `width > 0`（△6 Detach）。 |
| C-M1-21 | **订阅者寿命**（v1.1 新增——吸收外部评审 §3.1）：快照派发（C-M1-8②）只保证「回调列表修改不使本轮遍历失效」，**不**保证回调所引用对象的寿命 ⇒ 订阅者（`BoxView`）必须活到可能引用它的快照回调全部执行完毕。M1 满足方式 = `DesktopNestApp` 持有全部 `BoxView` 至退出（C-M1-10①）——**运行中销毁 View = 必须重新设计这条保证**（M2+ 若引入动态框增删则先解此题）。 |

## §5 影响面

| 维度 | 值 |
|---|---|
| 应用侧新目录 `examples/DesktopNest/` | **8 件**（初设 6 件 + VisualStyle.h + DesktopNest.h/.cpp；main.cpp 由「装配点」降为薄壳——偏离登记见 §9）：BoxModel.h/.cpp · VisualStyle.h · MemberListPopup.h/.cpp · BoxView.h/.cpp · BoxWindow.h/.cpp · DesktopNest.h/.cpp · main.cpp · CMakeLists.txt |
| 根 CMakeLists.txt | +1 行（`add_subdirectory`） |
| 框架 CMake | **+6 行**（TEST_SOURCES 追加 5 源 + include 目录 1 行——O5 (a)；初设 v1.2 估 +1~2 按单文件先例，本稿按实测修正） |
| 框架测试登记 | RunAllTests.h / .cpp 各 +1 行 |
| 框架公共头 / 公共 API / 框架实现文件 | **0 / 0 / 0**（不变——C-M1-1 口径维持） |
| 测试 | 新文件 `src/Tests/DesktopNestTests.cpp`——**6 注册用例**（T-M1-1..6；需求稿「~4–6 条」上界内；条 99（计数口径）：T 编号数 = 注册条目数 = 6，一注册内多断言——v1.1 的流程级判据[T-M1-2④⑤、T-M1-4⑥]并入既有注册，不新增条目） |
| 文档 | 本稿 + roadmap v1.21 回写 + docs/README.md 新行 |

## §6 待定项

| # | 待定项 |
|---|---|
| D-O1 | 末位拆出后 `state` 不自动归 TopLevel（保持 Merged + 徽标 1——如实记录收编历史）；是否自动归位留 M2/M3 数据安全阶段随真实模型再议。 |
| D-O2 | 折叠动画与窗口几何的同步（当前瞬切折中——C-M1-18）；平滑收缩需动画 tick 驱动 SetBounds，框架无此钩子 ⇒ M2+。 |
| D-O3 | 非 100% 缩放下放置几何：Phase 31 GetBounds/SetBounds 为 DIP 口径（理论一致），未实测 ⇒ 留 M4 位置持久化阶段。 |
| D-O4 | 内容区滚动：M1 假数据 5 行 × 24 = 120 DIP < 内容区 288 DIP——不触发；内容超出时引入 ScrollView（K4 已声明滚动可后置）。折叠态持久化同批再议。 |
| D-O5 | 弹出期间拖动框（浮层随窗口移动）：未验证；M1 假数据阶段拖动通常发生在浮层关闭态——若评审计入自动覆盖，装置补充（拖动中泵 + 断言）。 |
| D-O6 | 系统关闭路径（`WM_CLOSE` / Alt+F4 → `WindowCloseRequested` → 框架默认 `Release()`，`Application.cpp:170-176`）未接：该路径会绕过 `BoxWindow::Close()`（不置空 `m_window`）⇒ 包装指针悬空。M1 不阻塞——框不设焦点（K1 常驻观感），Alt+F4 不可达；M2 随真实关闭语义（含 `~Application` 残留窗口隐患，`Application.cpp:117` 自注）一并处理。 |

## §7 测试方向

### 7.1 装置三层

| 层 | 装置 | 先例 |
|---|---|---|
| D1 无头数据层 | `BoxModel` 直接构造 + `ChangeCallback` 计数（订阅句柄自持） | ModelProbeTests.cpp:4（demo 头直接 include） |
| D2 无头树层 | 裸 `Widget` 测试根（`SetSize(220, 320)`）+ `BoxView::Assemble` + 合成事件 `MouseButtonDownEvent(nullptr, x, y, MouseButton::Left)`（事件公开可构造——MouseButtonDownEvent.h:16；`OnMouseButtonDown` 是公有虚函数——Widget.h:160，直接调用） | WidgetTests.cpp:346 / CheckBoxTests.cpp:98 |
| D3 真实窗口层 | `DesktopNestApp::BuildFixture(false)`（隐藏窗——装置不扰动桌面）+ `GetHwndForTests()`（Win32PlatformWindow.h:102——ApplicationDispatchTests 先例）+ `SendMessage` 合成 WM_LBUTTONDOWN/UP | ApplicationDispatchTests.cpp:310 |

★ D3 坐标单位契约（条 109（测试装置的单位契约））：平台入站把客户区**物理像素**折成 DIP（WindowMessageHandler.cpp:153-157）⇒ 装置发消息前先按 `GetDpiScale()` 把 DIP 换算为物理像素（`static_cast<int>(dip * scale + 0.5f)`）——`dpi == 96` 时恒等 ⇒ 零回归可证。

### 7.2 逐用例判据（T-M1-1..6）

| 用例 | 装置 | 判据 |
|---|---|---|
| T-M1-1 `DesktopNest.ModelCollectNotifiesBoth` | D1 + D2 | ① `A.members == [B]` · `B.state == Merged` · `B.placement == 传入值` ② A/B handler 各恰好 1 次、C 0 次（P9 通知次数判据） ③ **派发期注销实证**（初设装置注记的落地）：A 的 handler 内注销 C 的订阅 ⇒ C 本轮仍收到（快照语义）、后续 `SetActive` 不再通知 C ④ D2 无头树：`Assemble` 后入口按钮 `GetBadgeCount() == members.size()`（徽标重建） |
| T-M1-2 `DesktopNest.ModelDetachRestoresMembership` | D1 + D3 | ① `CanDetach([C1,C2]) == true`；`Detach` 后 `members == [C2]` · `C1.state == TopLevel` · `C1.placement` 保留 · `CanDetach == false`（members.size() == 1） ② 级联纯函数：`CascadedPlacement({100,100,220,320}) == {128,128,220,320}` ③ D3：`BoxWindow::Create(placement)` → `GetBounds() == placement`（Phase 31 请求语义——读回为准） ④ **流程级**（v1.1 吸收评审 §4.1——真调协调器 `DesktopNestApp::Detach()`，非原语拼装）：被拆成员 `state == TopLevel` · `entry.window->IsOpen() == true` · `GetBounds() == 请求落点`（有记忆 ⇒ 恢复；无记忆 ⇒ 级联） · 源框 `members`/`active` 正确（active 仍指向有效成员） ⑤ **收编↔拆出循环两轮**（v1.1 吸收评审 §2 建议）：`Collect → Detach → Collect → Detach`，每轮断言 ④ 同款 + **逐次通知计数恒 == 1**（无重复订阅/重复控件——「遗留订阅」的可观测替代；旧树释放本身由框架契约 C-M1-20 保证、不可移植断言，故以契约引用代之） |
| T-M1-3 `DesktopNest.ModelSetActiveNotifiesOnlyOwner` | D1 + D2 | ① `SetActive(C, C2)` ⇒ C 的 handler 恰好 1 次、C1/C2 的 handler 0 次（仅属主通知） ② 内容重建：D2 树 `GetContent()` 首行文本含「C2」（切换后内容整体换人——K3①） ③ 派生标题：标题 Label 文本 == 「C2」（C-M1-17） |
| T-M1-4 `DesktopNest.PopupDiscipline` | D2 + D3 | ① 浮层内空白 `HitTest` == 浮层自身（不穿透——A3） ② 浮层外（内容区行）`HitTest` 返回外部控件且 `!m_popup->Contains(hit)` ③ 浮层可见时 Down @（标题条另一按钮 \| 内容区行）⇒ 浮层隐藏（判据⑤——点外含标题条按钮与内容区） ④ 浮层不可见时同坐标 Down ⇒ 无操作（判据①） ⑤ D3 真实派发（SendMessage · DIP→物理换算）：入口按钮点击 ×2 = 开→关（P6 切换）；成员行点击 = 切换且**行不被销毁**（派发安全——C-M1-19 R2）；「加入」行点击 = 收编全路径（模型状态正确 + 被收编框 `IsOpen() == false` + 发起浮层已关——R3） ⑥ **浮层重开生命周期**（v1.1 吸收评审 §4.2——验证 Refresh/Rebuild 分工）：开 → 切成员（原位刷新）→ 关 → **再开** ⇒ 成员勾选、拆出项可用性、加入列表**全部与当前 Model 一致**（重开走 `Rebuild` 全量重建，而非沿用可见期的原位状态） |
| T-M1-5 `DesktopNest.FoldGeometry` | D3 + D2 | ① D3 真实窗：`ToggleCollapse` → `GetBounds().height == kCaptionHeight`；再 `Toggle` → `== kWindowHeight`（C-M1-18 几何瞬时） ② D2 无头：CollapsiblePanel 无 Window 降级瞬时切换（CollapsiblePanel.h:53）⇒ `IsExpanded() == false && GetContent()->IsVisible() == false`；再 `Toggle` 恢复（无需泵驱动——动画被降级跳过） |
| T-M1-6 `DesktopNest.DragRoute` | D3 | **本用例证明的是「命中路由」**（评审 §4.3——真实拖动的顺滑/边缘行为仍属 A1 人工，报告须分开记录）：`IsClientInteractiveAt(150, 16) == false`（标题文本上——⇒ caption 区返回 HTCAPTION，可拖动——「拖标题文字可移动窗口」，Widget.h:207）· `IsClientInteractiveAt(56, 16) == true`（入口按钮）· `IsClientInteractiveAt(204, 16) == true`（折叠按钮）（⇒ HTCLIENT——按钮可点、不被拖动吃掉——A4） |

### 7.3 装置注记

- **测试坐标 = 常量推导**（条 47（文档里写下的测试探针坐标必须在脚本里复算断言））：入口按钮中心 `(kBarPad + kEntryButtonWidth/2, 16) = (56, 16)`；折叠按钮中心 `(kWindowWidth − kCollapseButtonSize/2, 16) = (204, 16)`；标题文本探针 `(150, 16)`（Label 区域内）；成员行 i 中心 `(kPopupRowPad + 86, kCaptionHeight + 4 + i×24 + 12)`；加入行 j 中心 `(kPopupRowPad + 86, kCaptionHeight + 56 + n×24 + j×24 + 12)`——测试内用 `Metrics` 常量算并 `assert`，不写字面量。
- **注册↔定义做差**（条 127）：`&(TestDesktopNest\w+)\);` 与 `^void TestDesktopNest\w+\(` 两集合做差必须为空。
- **清理**：每个 D3 用例结束前 `Close` 全部窗（Release 幂等）；Application 析构兜底（ApplicationDispatchTests 先例）。
- **回收时序（C-M1-20）**：D3 用例不跑 `Application::Run()` ⇒ Window 对象与旧树延后到 `~Application` 销毁——**有界、非泄漏**；用例**不得**假设「Close 后 Window 对象立即析构」（框架契约原文 `Application.cpp:112-116`）。用例内只断言 `IsOpen()`/`GetBounds()` 等包装层观测点。
- **断言启用**：Debug 构建断言层经 CMakeLists 持久修复对 MinGW 亦启用（条 35/50——2026-09-18 起四链 10/10）；报绿时写明构建配置。

## §8 开放决策点

| # | 决策点 | 倾向 |
|---|---|---|
| D-Q1 | 按钮皮肤：框架默认 ButtonStyle vs 深色自绘 | M1 用默认（消费现有主题能力——YAGNI）；深色自绘留观感打磨阶段 |
| D-Q2 | 观感值：浮层宽 180 / 行高 24 / 圆角 8 / 级联步长 28 / 徽标径 18 | 按 §2.2 度量表——评审可驳（纯常量，VisualStyle.h 单点改） |
| D-Q3 | 徽标形态：右上圆 + 数字 | 初设 §3.2 倾向——本稿按此实现 |
| D-Q4 | 入口按钮文案「成员」、折叠文案「-」/「+」 | ASCII 安全（默认字体字形覆盖）——评审可驳 |

## §9 修订记录

- **v1.0**（2026-10-09）初稿。**输入** = 初设 v1.2（外部评审吸收版）。**与初设的偏离/细化登记**（条 30①：原措辞不抹除、以引用形式落进修订条目）：
  ① **文件清单 6 件 → 8 件**：新增 `VisualStyle.h`（颜色/度量单一真相源被 4 个 TU 共享——ModelProbe::Palette 先例；放 BoxView.h 会造循环包含，放 BoxModel.h 会破「Model 零 ECDI 依赖」）与 `DesktopNest.h/.cpp`（协调器逻辑提出 main.cpp——ModelProbeTests 先例：demo 逻辑类链入 TEST_SOURCES ⇒ A2 流程级判据可自动执行）；main.cpp 由「装配点」降为「薄壳」。
  ② **ModelChange 3 种 → 5 种**（§1.3 反证：同一变更的两个接收方反应不同，三分种 ⇒ 被拆成员误 Rebuild 悬空访问）。
  ③ **订阅句柄**（C-M1-11）：`std::function` 不可比较 ⇒ 初设 C-M1-8 的「注册/注销成对」落地形态为句柄注销。
  ④ **框架 CMake +1~2 行 → +6 行**（C-M1-15）：初设按 ModelProbe 单文件先例估——实测 5 个实现文件。
  ⑤ **判据新增入口按钮豁免**（C-M1-12 ④）：初设 §1.1.2 判据在 P6 场景下会「Down 先关、Up 回调再开」⇒ 切换失效；豁免后开/关由点击回调权威切换。
  ⑥ **`Widget::Contains` 替代手写 `GetParent()` 上溯**（Widget.h:280——既有递归后代检测 API，语义等价）。
  ⑦ **派发安全三规则**（C-M1-19）：详设级新发现——派发循环「调用后才读父」（Application.cpp:294-302/318-326）⇒ 点击回调销毁被点控件即 UB；R1/R2/R3 共同保证全链存活。
  ⑧ **折叠时序钉死**（C-M1-18）+ **拆出重建 = 展开态**（折叠态不随 Model 持久——D-O4）。
  ⑨ **测试装置三层**（§7.1）+ D3 坐标单位契约（条 109——平台入站折 DIP，装置发物理像素）。

- **v1.1**（2026-10-09）**外部评审吸收**（评审结论：**有条件通过**——「先确认旧控件树的生命周期，再进入实施」）。★ 评审认可项：五种面向接收者的通知语义 / 句柄注销 + 快照派发 / C-M1-19 派发安全 / 协调器独立使流程级自动测试可行。**必须项与建议项处置**：
  ① **旧控件树生命周期 ⇒ 新增 C-M1-20**（评审 §2）：实测链条 = `Close()` → `Window::Release()`（`Window.cpp:147-157`，只销毁 HWND）→ 同步 `WM_DESTROY` → `Application::OnWindowDestroyed` 移入 `m_deferredDestroy`（`Application.cpp:127-153`）→ 消息循环**每条消息后** `ProcessDeferredDestroy`（`Application.cpp:109-118`，`:41` 注入）⇒ `~Window` ⇒ 根树一并销毁。**结论：不泄漏**（生产态一条消息内回收；测试态延后到 `~Application`，有界——框架契约原文 `window-ownership.md` §4.3）。**由此揪出并修掉本稿一处真缺陷**：销毁是延迟的 ⇒ `BoxWindow::Close()` 若不置空 `m_window` 即**悬空指针**（`GetBounds()` 转发即 UB）——改为 `Release() + m_window = nullptr + m_open = false`；`CloseBoxEntry` 的 `Close → Disassemble` 顺序标为不可交换（清理点前旧树仍存活，视图非拥有指针必须先断）。
  ② **`GetBounds()` 空值语义**（评审 §3.2）⇒ 并入 C-M1-20(e) + △6 Detach：级联 fallback 前提 = 源窗口 `IsOpen()` 且读回 `width > 0`；M1 中动作恒由源框浮层发起 ⇒ 前提恒成立，契约写明防日后退化。
  ③ **订阅者寿命**（评审 §3.1）⇒ **新增 C-M1-21**：快照派发不保证回调引用对象寿命 ⇒ 订阅者须活到本轮派发结束（M1 = 协调器持有全部 View 至退出；运行中销毁 View 须重新设计——M2+）。
  ④ **T-M1-2 补流程级 + 循环**（评审 §4.1/§2 建议）⇒ 增判据 ④（真调 `DesktopNestApp::Detach()`：状态/`IsOpen()`/落点读回/源框 members·active）与 ⑤（`Collect↔Detach` 两轮，通知计数恒 1 = 无重复订阅；旧树释放本身以 C-M1-20 契约引用代替不可移植断言）。
  ⑤ **T-M1-4 补浮层重开**（评审 §4.2）⇒ 增判据 ⑥（开→切→关→**再开** ⇒ 全部与当前 Model 一致——验证 `Refresh`/`Rebuild` 分工）。
  ⑥ **T-M1-6 表述收窄**（评审 §4.3）⇒ 判据明写「本用例证明**命中路由**；真实拖动顺滑/边缘行为属 A1 人工，报告分开记录」。
  ⑦ **新增 D-O6**：系统关闭路径（`WM_CLOSE`/Alt+F4 → `WindowCloseRequested` → 框架默认 `Release()`，`Application.cpp:170-176`）绕过 `BoxWindow::Close()` ⇒ 指针悬空；M1 不阻塞（框不设焦点，Alt+F4 不可达），M2 随真实关闭语义一并处理。
  ⑧ 契约区间 C-M1-11..**C-M1-21**；§7.3 增「回收时序」装置注记；**清理 v1.0 文末游离 `}`**（落盘残留）。
