#pragma once

#include "VisualStyle.h"

#include "ECDI/Application/Application.h"   // Create（唯一构造入口——Window.h:42）
#include "ECDI/Core/Rect.h"
#include "ECDI/Window/Window.h"

#include <string>   // std::string（Create 形参）——头自包含（C-M1-23）

namespace ECDI::DesktopNest {

// ★ 前置声明须在本命名空间内（实施勘误 2026-10-09——同 BoxView.h 注记）
class BoxView;

/// @brief 框窗口包装（C-M1-10 BoxWindow 生命周期契约的落地）。
/// @details 持有 Window*（Application 容器拥有——Window.h:40 所有权契约，
///          调用者不得 delete）。Create 可重复调用（C-M1-10③/16）：每次 = 释放旧
///          HWND + 建新窗 + 全量重建视图。Close 只关原生窗口（Release——HWND 销毁、
///          幂等，Window.h:77），不销毁 BoxView——视图由协调器注册表持有至退出（C-M1-10①）。
class BoxWindow {
public:
	BoxWindow() = default;

	/// @brief 创建/重建原生窗口。
	/// @param placement 初始边界（Phase 31——请求语义，读回以 GetBounds 为准）。
	/// @pre 配置期四件套（SetChromeMode/SetCaptionHeight/SetResizeInset/SetWindowLayer）
	///      均在 Show 前调用（F2–F4——Show 后记 Warning 并忽略）。
	void Create(Application& application, const Rect& placement,
	            BoxView& view, std::string title);

	/// @brief 关闭原生窗口（Release——幂等）。
	/// @details ★ 关闭后**置 m_window = nullptr**（C-M1-20(b)）：Window C++ 对象的销毁是
	///          **延迟的**（WM_DESTROY → Application 移入 m_deferredDestroy → 消息循环
	///          清理点才 ~Window）⇒ 包装对象不得保留该指针，否则 GetBounds/SetBounds
	///          转发即悬空解引用。
	void Close();

	/// @brief 显示（Create 后；A1 人工回路入口）。
	void Show();

	/// @brief 运行期几何（Phase 31——请求语义）。
	/// @note Close 后 m_window == nullptr ⇒ GetBounds 返回空 Rect（**不得**当有效位置用——
	///       级联 fallback 的前提见 C-M1-22(f)）。
	void SetBounds(const Rect& bounds);

	[[nodiscard]] Rect GetBounds() const;

	// ── 观测面 ──────────────────────────────────────

	/// @brief 窗口存活（A2 可观测；C-M1-22(c)：关闭后不得报告存活）
	[[nodiscard]] bool IsOpen() const noexcept { return m_open; }

	/// @brief 底层窗口指针（测试/协调器缝；Close 后为 nullptr）
	[[nodiscard]] Window* GetWindow() const noexcept { return m_window; }

private:
	Window* m_window = nullptr;   ///< 非拥有——Application 容器（Window.h:40）；Close 后置空（C-M1-20(b)）
	bool m_open = false;          ///< 存活标记（末位设置——C-M1-16⑩）
};

}   // namespace ECDI::DesktopNest
