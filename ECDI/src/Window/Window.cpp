#include "ECDI/Window/Window.h"

#include "ECDI/Platform/Win32/Win32PlatformWindow.h"
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

#include <string>
#include <memory>
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
	, m_renderer(m_backend){   // 决策 35：m_backend 先构造（默认构造），m_renderer 绑定引用

	// 7.1.1：工厂创建平台窗口——CreateWindowExW 在 Win32PlatformWindow 构造内完成
	auto platform = std::make_unique<Win32PlatformWindow>(
		*this, app, windowClass, title, width, height);

	// ⚠️ 过渡（7.1.2 拆派发 / 7.1.4 PlatformRenderContext 替换）：后端注入 HWND——
	// GDIBackend 是 4.7 稳定代码（skill 23 不碰内部），SetHwnd 接口保持，仅调用点经平台层中转
	m_backend.SetHwnd(platform->GetHandle());

	m_platformWindow = std::move(platform);

	// 创建 RootWidget（Widget 树的根节点，代表窗口客户区）
	m_rootWidget = std::make_unique<Widget>();

	// 5.4.1：根与 Window 建立关联（Widget::Invalidate/HasFocus 上溯到根后走它）
	m_rootWidget->SetWindow(this);

	FRAMEWORK_ASSERT(m_rootWidget != nullptr);

	// 初始客户区尺寸（平台窗口构造后查询——GetClientSize 返回框架层 Size；
	// 不用 OnResized 回调：构造时 m_rootWidget 尚未创建，顺序问题）
	// SetSize 接收 int（客户区像素本就是整数，float → int 显式转换消除 C4244）
	const Size clientSize = m_platformWindow->GetClientSize();

	m_rootWidget->SetSize(static_cast<int>(clientSize.width), static_cast<int>(clientSize.height));

}

void Window::Show() {

	if (m_platformWindow != nullptr) {

		m_platformWindow->Show();

	}

}

Widget& Window::GetRootWidget() noexcept {

	return *m_rootWidget;

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

	if (m_platformWindow==nullptr) {

		return true;

	}

	return m_platformWindow->Release();

}

Window::~Window()noexcept {

	Release();

}

Widget* Window::GetFocusedWidget() const noexcept{

	return m_focusedWidget;

}

void Window::Invalidate(){

	// 5.4.1：整个客户区无效 → WM_PAINT → PaintFrame（平台实现双缓冲自动重绘）
	if (m_platformWindow){

		m_platformWindow->Invalidate();

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
	// dynamic_cast<TextBox*> 升级为 dynamic_cast<EditableTextWidget*>。
	// （7.1.1：平台调用已下沉 Win32PlatformWindow——本方法只剩框架逻辑，无 Win32 类型）
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){

		// v1.0.3：IME 组合时同步插入点（双通道——系统 caret + ImmSetCompositionWindow 都在平台层）
		UpdateTextInputCaret(textBox->GetCaretClientPosition());   // 客户区坐标（TextBox 零平台依赖）

	}

}

void Window::UpdateTextInputCaret(const Point& clientPos){

	// 7.1.1 薄转发：平台实现（SetCaretPos + ImmSetCompositionWindow 双通道）在 Win32PlatformWindow
	if (m_platformWindow){

		m_platformWindow->UpdateTextInputCaret(clientPos);

	}

}

void Window::DestroyTextInputCaret(){

	// 7.1.1 薄转发
	if (m_platformWindow){

		m_platformWindow->DestroyTextInputCaret();

	}

}

// ── PlatformWindowHost 实现（7.1.1：平台事件 → 框架响应）──

void Window::OnPaint(){

	PaintFrame();

}

void Window::OnResized(int width, int height){

	// 窗口大小变化时，同步 RootWidget 尺寸到新的客户区大小
	if (m_rootWidget){

		m_rootWidget->SetSize(width, height);

	}

}

void Window::OnExitSizeMove(){

	// 5.6 v1.0.3 修复（2026-08-15 验证）：窗口移动/缩放结束后刷新文本输入插入点。
	// 根因：TSF 输入法组合中已显示候选窗时**缓存位置**，不重新查询 GetCaretPos——
	// 仅 SetCaretPos 更新系统 caret 不够，候选窗停在旧屏幕位置（实测"飘屏幕中部"）。
	// 修复：销毁+重建系统 caret，强制 TSF 缓存失效重新查询（下一键 WM_IME_COMPOSITION 亦触发刷新）。
	// 焦点非 TextBox 时 NotifyIMEComposition 内部 fail-safe 跳过（caret 销毁无害）。
	DestroyTextInputCaret();
	NotifyIMEComposition();   // 重建（UpdateTextInputCaret 懒创建 + SetCaretPos + Imm 刷新）

}

Window* Window::GetWindow() const noexcept{

	return const_cast<Window*>(this);

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
