#include "ECDI/Platform/Win32/Win32PlatformWindow.h"

#include "ECDI/Window/WindowClass.h"
#include "ECDI/Core/String.h"

#include <Windows.h>
#include <imm.h>

#include <string>
#include <system_error>

namespace ECDI{

Win32PlatformWindow::Win32PlatformWindow(PlatformWindowHost& host, Application* app,
		const WindowClass& windowClass, const std::string& title, int width, int height)
	: m_host(host)
	, m_application(app)
	, m_messageHandler(app){

	// 公共 API 为 UTF-8（std::string），在平台边界转换到 UTF-16（字符串边界划分）
	const std::wstring wideTitle = UTF8ToWide(title);

	// 创建 Win32 窗口（WS_OVERLAPPEDWINDOW = 标题栏 + 边框 + 最小化/最大化/关闭按钮）
	m_hwnd = CreateWindowExW(
		0,
		windowClass.GetClassName(),
		wideTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		width,
		height,
		nullptr,
		nullptr,
		windowClass.GetInstance(),
		this  // 通过 CREATESTRUCT 传递 this 指针，用于 WindowProc 中 GWLP_USERDATA 绑定

	);

	if (m_hwnd == nullptr){

		throw std::system_error(
			std::error_code(static_cast<int>(GetLastError()), std::system_category()),
			"CreateWindowExW failed");

	}

}

Win32PlatformWindow::~Win32PlatformWindow(){

	Release();

}

void Win32PlatformWindow::Show() {

	if (m_hwnd != nullptr) {

		ShowWindow(m_hwnd, SW_SHOW);

		UpdateWindow(m_hwnd);

	}

}

bool Win32PlatformWindow::Release() noexcept {

	if (m_hwnd==nullptr) {

		return true;

	}

	return DestroyWindow(m_hwnd) != FALSE;

}

void Win32PlatformWindow::Invalidate(){

	if (m_hwnd){

		InvalidateRect(m_hwnd, nullptr, FALSE);

	}

}

Size Win32PlatformWindow::GetClientSize() const{

	RECT rc{};

	if (GetClientRect(m_hwnd, &rc)){

		// 直接返回 float（Size 成员为 float——转 int 会触发 C2397 narrowing）
		return Size{
			static_cast<float>(rc.right - rc.left),
			static_cast<float>(rc.bottom - rc.top)
		};

	}

	return Size{};

}

LRESULT CALLBACK Win32PlatformWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	Win32PlatformWindow* self = nullptr;

	// WM_NCCREATE：窗口创建最早的消息，通过 CREATESTRUCT 绑定 HWND ↔ Win32PlatformWindow
	if (msg == WM_NCCREATE) {

		CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);

		self = static_cast<Win32PlatformWindow*>(create->lpCreateParams);

		self->m_hwnd = hwnd;

		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

	}

	else {

		// 后续消息：从 GWLP_USERDATA 取回实例指针
		self = reinterpret_cast<Win32PlatformWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	}

	if (self) {

		return self->HandleMessage(hwnd, msg, wParam, lParam);

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

LRESULT Win32PlatformWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	// ── 内部状态同步（在 Event 翻译前完成，经 Host 回调保证框架层看到最新状态）──
	switch (msg){

	case WM_PAINT:
		// 决策 39：绘制不走翻译器（不是 Event），经 Host 回调编排整帧
		m_host.OnPaint();
		return 0;

	case WM_DESTROY:
		m_hwnd = nullptr;   // 句柄失效（Window 端无需动作——YAGNI，无 OnDestroyed 回调）
		break;

	case WM_SIZE:
		// 窗口大小变化 → Host 回调同步 RootWidget 尺寸；随后 fall-through 翻译器（WindowResizedEvent）
		m_host.OnResized(LOWORD(lParam), HIWORD(lParam));
		break;

	case WM_EXITSIZEMOVE:
		// 5.6 v1.0.3 修复（2026-08-15 验证）：窗口移动/缩放结束后刷新文本输入插入点。
		// 根因：TSF 输入法组合中已显示候选窗时**缓存位置**，不重新查询 GetCaretPos——
		// 仅 SetCaretPos 更新系统 caret 不够，候选窗停在旧屏幕位置（实测"飘屏幕中部"）。
		// 修复：销毁+重建系统 caret，强制 TSF 缓存失效重新查询。
		m_host.OnExitSizeMove();   // → Window 销毁+重建 caret（dynamic_cast<TextBox*> 在框架层）
		return 0;

	}

	// 将 Win32 消息翻译为 Framework Event 并派发（翻译器保持现状：直派 Application；
	// 结构 7.1.2 再拆 Translate/Dispatch——GPT 边界守则 F1）
	auto result = m_messageHandler.Handle(m_host.GetWindow(), hwnd, msg, wParam, lParam);

	if (result) {

		return *result;

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

HWND Win32PlatformWindow::GetHandle() const noexcept{

	return m_hwnd;

}

void Win32PlatformWindow::UpdateTextInputCaret(const Point& clientPos){

	// ① 系统 caret（TSF 输入法主路径——Win11 微软拼音查询 GetCaretPos 定位候选窗，最小实验已验证）
	// 懒创建：首次调用（TextBox 获焦）创建；后续只 SetCaretPos
	if (!m_caretCreated){

		CreateCaret(m_hwnd, nullptr, 2, 20);   // 2x20 竖线 caret（隐藏不显示，光标竖线由控件自画）

		m_caretCreated = true;

	}

	SetCaretPos(static_cast<int>(clientPos.x), static_cast<int>(clientPos.y));   // 客户区坐标（caret 语义=左上角）

	HideCaret(m_hwnd);   // 隐藏：系统 caret 仅作位置信标，视觉零变化

	// ② ImmSetCompositionWindow（IMM 保底通道——GPT 双保险：兼容性最广）
	// ⚠️ 关键修正（2026-08-15 用户洞察）：实测微软拼音（TSF）把 ptCurrentPos 当**客户区坐标**解释！
	// 证据：最小实验 SetCaretPos(300,200) 时候选框出现在窗口内 (300,200)（客户区）而非屏幕 (300,200)。
	// 若按 IMM 文档"屏幕坐标"传 ClientToScreen 后的值，候选框落在窗口内"屏幕坐标值"处（远离光标），
	// 窗口移动时还叠加窗口偏移（"像素过多"）。故**不再 ClientToScreen，直接传客户区坐标**。
	POINT pt{

		static_cast<LONG>(clientPos.x),

		static_cast<LONG>(clientPos.y)

	};

	// Imm 三件套：取上下文 → 设置组合窗口 → 释放（HIMC 判空：失败静默，交默认行为）
	if (HIMC imc = ImmGetContext(m_hwnd)){

		COMPOSITIONFORM cf{};

		cf.dwStyle = CFS_POINT;         // 候选窗左上角对准 ptCurrentPos（客户区坐标——实测语义）

		cf.ptCurrentPos = pt;

		ImmSetCompositionWindow(imc, &cf);

		ImmReleaseContext(m_hwnd, imc);

	}

}

void Win32PlatformWindow::DestroyTextInputCaret(){

	if (m_caretCreated){

		DestroyCaret();

		m_caretCreated = false;

	}

}

}
