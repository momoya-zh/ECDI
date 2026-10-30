#pragma once

// ══════════════════════════════════════════════════════════════════════
// ★ Phase 41：真窗口测试装置 `WindowProbe`
// ══════════════════════════════════════════════════════════════════════
// 来源 = `VisibilityRepaintTests.cpp` 的 anonymous-namespace `M9Probe`（原 `:63-114`）。
// ★ 抽出的依据 = 「第二个真实消费者出现再抽象」：`LayoutInvalidationTests.cpp` 的帧级用例
//   （T41-1/2/4/6/7/8）需要**同一套**「真 Application + 真 Window + FindWindowW + 泵消息 +
//   ValidateRect/GetUpdateRect」。搬运**行为零变化**（5 条 M9 用例判据不动）。
// ★ 两条关键实测事实（沿 M9 原注释，此处只留结论——完整论证仍在本文件的使用方头部注释里）：
//   ① `FindWindowW(nullptr, L"<title>")` 能按标题取到本窗口 HWND——region 判据的前提；
//   ② `WM_PAINT` 无 `BeginPaint/EndPaint` ⇒ **帧计数不可用作判据**，用 region 脏位；
//   ③ Phase 41 新增：`PumpMessages` 只处理**当下队列里**的消息，队列空即返回
//      ⇒ 「空泵」**不是**「空闲帧」——想造帧必须 `PumpOneFrame()`（或先 `window->Invalidate()`）。

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本头含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Window/Window.h"
#include "Render/RecordingBackend.h"

#include <memory>
#include <utility>

namespace ECDI::Test{

/// @brief 手动消息泵（处理至多 maxCount 条消息后返回；队列空则立即返回）
inline void PumpMessages(int maxCount) {

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {

		TranslateMessage(&msg);
		DispatchMessageW(&msg);

	}

}

/// @brief 真 Application + 真 Window 装置（注入 RecordingBackend 供正向对照观测）
/// @details 关键实测事实（探针取证，如实记账）：
///   ① `FindWindowW(nullptr, L"<title>")` 能按标题取到本窗口 HWND —— region 判据的前提；
///   ② `WM_PAINT` 泛滥（无 BeginPaint/EndPaint）⇒ **帧计数不可用作判据**，改用 region 脏位。
struct WindowProbe {

	Application app;

	Window* window = nullptr;   ///< 非拥有（所有权归 Application）

	HWND hwnd = nullptr;        ///< 平台窗口句柄（FindWindowW 取——region 判据的观测面）

	explicit WindowProbe(const char* title) {

		auto recorderOwned = std::make_unique<RecordingBackend>();
		RenderServices services{
			std::move(recorderOwned),
			std::make_unique<RecordingBackend>()   // 测量占位（本组不断言测量）
		};

		window = &app.Create(title, 400, 300, std::move(services));

	}

	WindowProbe(const WindowProbe&) = delete;
	WindowProbe& operator=(const WindowProbe&) = delete;

	~WindowProbe() { if (window) window->Release(); }

	/// @brief 显示并泵若干帧（让窗口进入稳态——region 由 Show 的第一帧弄脏）
	void ShowAndSettle() {

		window->Show();
		PumpMessages(64);

	}

	/// @brief 清空 update region（测试侧家务——把「是否变脏」变成可区分二值）
	/// @note 必须显示后调用（未显示的窗口 Region 操作无意义）
	void ClearRegion() {

		::ValidateRect(hwnd, nullptr);

	}

	/// @brief update region 是否变脏（非零 = 有绘制请求待处理）
	/// @pre ClearRegion() 之后调用才有判别力
	bool RegionDirty() const {

		RECT rc{};
		return ::GetUpdateRect(hwnd, &rc, FALSE) != 0;

	}

	/// @brief **强制出 1 帧**：请求重绘 → 泵消息
	/// @details ★ 不能改用「空泵」代替：`PumpMessages` 在队列空时立即返回 ⇒ 一帧都不会跑。
	///          「空闲帧」的判据对象是「**有帧**但不重排」⇒ 必须先造出帧（T41-4 依赖这一点）。
	void PumpOneFrame() {

		window->Invalidate();
		PumpMessages(16);

	}

};

}   // namespace ECDI::Test
