#include "ECDI/Window/Window.h"

#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/WindowClass.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/Render/PaintContext.h"

#include <Windows.h>
#include <imm.h>

#include <string>
#include <system_error>
#include <vector>

namespace ECDI{

namespace {

// 5.4.4：DFS 收集可聚焦控件（树前序：父 → 子；匿名 namespace——Window 内部辅助，非公开能力）
void CollectFocusables(Widget* node, std::vector<Widget*>& out){

	if (node->CanFocus()){

		out.push_back(node);

	}

	for (size_t i = 0; i < node->GetChildCount(); ++i){

		CollectFocusables(node->GetChildAt(i), out);

	}

}

}

Window::Window(Application* app,const WindowClass &windowClass,const std::string& title, int width, int height)
	: m_application(app)
	, m_messageHandler(m_application)
	, m_renderer(m_backend){   // 决策 35：m_backend 先构造（默认构造），m_renderer 绑定引用

	// 公共 API 为 UTF-8（std::string），在平台边界转换到 UTF-16（字符串边界划分）
	const std::wstring wideTitle = UTF8ToWide(title);

	// 创建 Win32 窗口（WS_OVERLAPPEDWINDOW = 标题栏 + 边框 + 最小化/最大化/关闭按钮）
	m_handle = CreateWindowExW(
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

	if (m_handle == nullptr){

		throw std::system_error(
			std::error_code(static_cast<int>(GetLastError()), std::system_category()),
			"CreateWindowExW failed");

	}

	// 决策 35：hwnd 就绪后注入渲染后端（BeginFrame 才真正使用）
	m_backend.SetHwnd(m_handle);

	// 创建 RootWidget（Widget 树的根节点，代表窗口客户区）
	m_rootWidget = std::make_unique<Widget>();

	// 5.4.1：根与 Window 建立关联（Widget::Invalidate/HasFocus 上溯到根后走它）
	m_rootWidget->SetWindow(this);

	FRAMEWORK_ASSERT(m_rootWidget != nullptr);

	// 同步 RootWidget 尺寸到窗口客户区（不含边框和标题栏）
	RECT rc{};

	if (GetClientRect(m_handle, &rc)){

		m_rootWidget->SetSize(rc.right - rc.left, rc.bottom - rc.top);

	}
}

void Window::Show() {

	if (m_handle != nullptr) {

		ShowWindow(m_handle, SW_SHOW);

		UpdateWindow(m_handle);

	}

}

Widget& Window::GetRootWidget() noexcept {

	return *m_rootWidget;

}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	Window* window = nullptr;

	// WM_NCCREATE：窗口创建最早的消息，通过 CREATESTRUCT 绑定 HWND ↔ Window
	if (msg == WM_NCCREATE) {

		CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);

		window = static_cast<Window*>(create->lpCreateParams);

		window->m_handle = hwnd;

		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

	}

	else {

		// 后续消息：从 GWLP_USERDATA 取回 Window 指针
		window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	}

	if (window) {

		return window->HandleMessage(hwnd,msg, wParam, lParam);

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

LRESULT Window::HandleMessage(HWND hwnd,UINT msg, WPARAM wParam, LPARAM lParam) {

	// ── 内部状态同步（在 Event 翻译前完成，保证 Handler 看到最新状态）──
	switch (msg){

	case WM_PAINT:
		// 决策 39：绘制不走翻译器（不是 Event），Window 编排整帧
		PaintFrame();
		return 0;

	case WM_DESTROY:
		m_handle = nullptr;
		break;

	case WM_SIZE:
		// 窗口大小变化时，同步 RootWidget 尺寸到新的客户区大小
		if (m_rootWidget){

			m_rootWidget->SetSize(LOWORD(lParam), HIWORD(lParam));

		}

		break;

	case WM_EXITSIZEMOVE:
		// 5.6 v1.0.3 修复（2026-08-15 验证）：窗口移动/缩放结束后刷新文本输入插入点。
		// 根因：TSF 输入法组合中已显示候选窗时**缓存位置**，不重新查询 GetCaretPos——
		// 仅 SetCaretPos 更新系统 caret 不够，候选窗停在旧屏幕位置（实测"飘屏幕中部"）。
		// 修复：销毁+重建系统 caret，强制 TSF 缓存失效重新查询（下一键 WM_IME_COMPOSITION 亦触发刷新）。
		// 焦点非 TextBox 时 NotifyIMEComposition 内部 fail-safe 跳过（caret 销毁无害）。
		DestroyTextInputCaret();
		NotifyIMEComposition();   // 重建（UpdateTextInputCaret 懒创建 + SetCaretPos + Imm 刷新）
		return 0;

	}

	// 将 Win32 消息翻译为 Framework Event 并派发
	auto result = m_messageHandler.Handle(this, hwnd, msg, wParam, lParam);

	if (result) {

		return *result;

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

void Window::PaintFrame()
{
	// 决策 10/13/33：完整编排，严格配对
	m_commands.clear();                              // 决策 4：复用缓冲
	PaintContext ctx(m_commands, m_backend);         // 路线 X：m_backend 兼 TextMeasurer（测量帧无关）
	m_rootWidget->Paint(ctx, 0, 0);                  // 决策 6：根从 (0,0)，offset 累加
	m_renderer.BeginFrame();                         // 决策 13：转发
	m_renderer.Execute(m_commands);
	m_renderer.EndFrame();
}

bool Window::Release() noexcept {

	if (m_handle==nullptr) {

		return true;

	}

	return DestroyWindow(m_handle) != FALSE;

}

Window::~Window()noexcept {

	Release();

}

Widget* Window::GetFocusedWidget() const noexcept{

	return m_focusedWidget;

}

void Window::Invalidate(){

	// 5.4.1：整个客户区无效 → WM_PAINT → PaintFrame（GDIBackend 双缓冲自动重绘）
	if (m_handle){

		InvalidateRect(m_handle, nullptr, FALSE);

	}

}

TextMeasurer& Window::GetTextMeasurer() noexcept{

	// 5.5 T1：GDIBackend 兼 TextMeasurer（5.1 双接口）——返回抽象接口不暴露具体后端
	return m_backend;

}

void Window::SetCaptureWidget(Widget* widget){

	// 5.4.2：隐式捕获——Down 命中设置、Up 后释放；非拥有指针（生命周期随 Widget 树）
	m_captureWidget = widget;

}

Widget* Window::GetCaptureWidget() const noexcept{

	return m_captureWidget;

}

void Window::SetFocusedWidget(Widget* widget){

	// 5.4.3：同控件短路——避免重复设置触发 Lost+Gained 空转
	if (m_focusedWidget == widget){

		return;

	}

	// 5.4.3：通知旧焦点失去（数据变更前）
	if (m_focusedWidget){

		m_focusedWidget->OnFocusLost();

	}

	m_focusedWidget = widget;

	if (widget != nullptr){

		// 沿 Parent 链回溯到树根，验证 widget 属于当前窗口的 Widget 树
		Widget* current = widget;

		while (current->GetParent()){

			current = current->GetParent();

		}

		// 树根必须是当前窗口的 RootWidget
		FRAMEWORK_ASSERT(current == &GetRootWidget());

		// 5.4.3：通知新焦点获得
		widget->OnFocusGained();

	}

	// 5.4.3：焦点变化重绘（设新/清空都重绘）
	Invalidate();

}

void Window::HandleKeyDown(const KeyDownEvent& event){

	// 5.4.4 + 5.5.2：Tab 框架拦截（焦点导航是 Window 职责）；Shift+Tab 反向（5.4 债务落地）
	if (event.GetKeyCode() == KeyCode::Tab){

		FocusNext(event.IsShiftDown() ? -1 : 1);

		return;

	}

	if (m_focusedWidget){

		m_focusedWidget->OnKeyDown(event);

	}

}

void Window::NotifyIMEComposition(){

	// MVP 技术债（显式记录）：Window 临时识别具体控件 TextBox（框架内首例"Window 认识具体控件"）。
	// 可接受理由：fail-safe——非 TextBox 焦点时跳过更新，IME 交系统默认行为；
	// 而 Widget 基类虚函数方案会把候选窗钉死在 (0,0)（fail-wrong）。
	// 演进路径：第二个可编辑控件出现时 → 抽象 EditableTextWidget，
	// dynamic_cast<TextBox*> 升级为 dynamic_cast<EditableTextWidget*>；
	// 同时随 Phase 7 PlatformWindow 下沉 Imm 调用。
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){

		// v1.0.3：IME 组合时同步插入点（双通道——系统 caret + ImmSetCompositionWindow 都在 UpdateTextInputCaret 内）
		UpdateTextInputCaret(textBox->GetCaretClientPosition());   // 客户区坐标（TextBox 零平台依赖）

	}

}

void Window::UpdateTextInputCaret(const Point& clientPos){

	// ① 系统 caret（TSF 输入法主路径——Win11 微软拼音查询 GetCaretPos 定位候选窗，最小实验已验证）
	// 懒创建：首次调用（TextBox 获焦）创建；后续只 SetCaretPos
	if (!m_caretCreated){

		CreateCaret(m_handle, nullptr, 2, 20);   // 2x20 竖线 caret（隐藏不显示，光标竖线由控件自画）

		m_caretCreated = true;

	}

	SetCaretPos(static_cast<int>(clientPos.x), static_cast<int>(clientPos.y));   // 客户区坐标（caret 语义=左上角）

	HideCaret(m_handle);   // 隐藏：系统 caret 仅作位置信标，视觉零变化

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
	if (HIMC imc = ImmGetContext(m_handle)){

		COMPOSITIONFORM cf{};

		cf.dwStyle = CFS_POINT;         // 候选窗左上角对准 ptCurrentPos（客户区坐标——实测语义）

		cf.ptCurrentPos = pt;

		ImmSetCompositionWindow(imc, &cf);

		ImmReleaseContext(m_handle, imc);

	}

}

void Window::DestroyTextInputCaret(){

	if (m_caretCreated){

		DestroyCaret();

		m_caretCreated = false;

	}

}

void Window::FocusNext(int direction){

	// 5.4.4：树前序收集 CanFocus 控件 → 当前焦点 index + direction → 循环取模
	std::vector<Widget*> focusables;

	CollectFocusables(m_rootWidget.get(), focusables);

	if (focusables.empty()){

		return;

	}

	int index = -1;

	for (size_t i = 0; i < focusables.size(); ++i){

		if (focusables[i] == m_focusedWidget){

			index = static_cast<int>(i);

			break;

		}

	}

	const int size = static_cast<int>(focusables.size());

	const int next = (index < 0) ? 0 : (index + direction + size) % size;

	SetFocusedWidget(focusables[next]);

}

}
