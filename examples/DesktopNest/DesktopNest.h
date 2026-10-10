#pragma once

#include "BoxModel.h"
#include "BoxView.h"
#include "BoxWindow.h"
#include "VisualStyle.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"   // OnWindowCloseRequested 形参（C-M1-22）

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ECDI::DesktopNest {

/// @brief 框登记项——一个框的窗口 + 视图（协调器持有至退出，C-M1-10①）。
struct BoxEntry {
	std::unique_ptr<BoxWindow> window;
	std::unique_ptr<BoxView> view;
};

/// @brief DesktopNest 应用（协调器——窗口生命周期的编排点）。
/// @details ★ 流程纪律（K4 v1.1 单向数据流的应用形态）：
///          ① 收编/拆出 = 「读几何（Phase 31）→ Model 变更 → 通知驱动视图/窗口」——
///          禁止「点击后直接删/建 Widget、顺便改 members」；
///          ② 收编/拆出动作**先关发起浮层**再执行 Model 操作（C-M1-19 R3）。
/// ★ **继承 Application**（C-M1-22(a)）：关闭请求的唯一拦截点是
///   `Application::OnWindowCloseRequested`（virtual），而仅「持有」Application 成员
///   无法 override。窗口仍由基类容器持有（Create 登记、框架延迟回收——C-M1-20）——
///   继承不改变所有权。
class DesktopNestApp : public Application {
public:
	/// @brief K6 冻结初始态：A（独立）· B（独立）· C（合并框 = [C1, C2]）
	struct Fixture { BoxId a; BoxId b; BoxId c; };

	/// @brief 建冻结初始态（三框）+ 装配
	/// @param show 生产 = true；测试 = false（隐藏窗——装置不扰动桌面）
	Fixture BuildFixture(bool show = true);

	/// @brief 生产入口：BuildFixture(true) → 消息循环（阻塞至退出）
	int Run();

	// ── 流程（A2 自动判据的驱动面——与弹层动作回调同一入口）────

	/// @brief 收编（读 placement → Model）；已关框不参与（C-M1-22(f)）
	void Collect(BoxId target, BoxId source);

	/// @brief 拆出当前成员（恢复/级联 → Model → 重建窗）；已关框不参与（C-M1-22(f)）
	void Detach(BoxId box);

	/// @brief 关闭请求拦截（C-M1-22——`WM_CLOSE`/Alt+F4 与 `Window::RequestClose()`
	///        同路径、同步派发）：先同步包装器与视图（`Close` 置空指针 + `Disassemble`），
	///        **再委托基类**实际销毁（顺序不可颠倒）。
	void OnWindowCloseRequested(const WindowCloseRequestedEvent& event) override;

	// ── 测试观测面 ──────────────────────────────────

	[[nodiscard]] BoxModel& GetModel() noexcept { return m_model; }

	[[nodiscard]] BoxEntry& GetEntry(BoxId box) { return m_boxes.at(box); }

	/// @brief 框是否已关闭（登记存在但窗口已关——C-M1-22(e)）
	[[nodiscard]] bool IsBoxClosed(BoxId box) const;

private:
	BoxId MakeBox(const std::string& title, const Rect& placement, bool show);

	BoxId MakeMergedBox(const std::string& title,
	                    const std::vector<std::string>& members,
	                    const Rect& placement, bool show);

	/// @brief 关原生窗口 + 视图解装（C-M1-10②/16——顺序不可交换，C-M1-20(c)）
	void CloseBoxEntry(BoxId box);

	/// @brief 单浮层纪律（开新关旧）
	void CloseOtherPopups(BoxId keepOpen);

	void OnBoxAction(BoxId owner, BoxView::Action action, BoxId source);

	/// @brief 关闭请求反查（C-M1-22；未命中返回 -1）
	[[nodiscard]] BoxId FindBoxByWindow(const Window* window) const;

	BoxModel m_model;                      ///< 唯一事实源（P3）
	std::map<BoxId, BoxEntry> m_boxes;     ///< id → 登记（持有至退出）
	// ★ 无 Application 成员（C-M1-22）：基类即 Application 实例——Create/Run 直接可用。
};

}   // namespace ECDI::DesktopNest
