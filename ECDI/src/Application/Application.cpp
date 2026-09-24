#include "ECDI/Application/Application.h"

#include "Platform/Win32/Win32PlatformApplication.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/EventSystem/Window/WindowResizedEvent.h"
#include "ECDI/EventSystem/Window/WindowDestroyEvent.h"
#include "ECDI/EventSystem/Window/WindowCreatedEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Window/DropFilesEvent.h"
#include "ECDI/EventSystem/Application/TrayEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/Logger.h"

#include <algorithm>

namespace ECDI{

Application::Application()
	: m_platformApplication(std::make_unique<Win32PlatformApplication>()){

	// ★ Phase 20（△22）：**进程 DPI 感知在此声明**——早于任何窗口创建（窗口只在 Create 内建）
	//   ⇒ 框架「公共 API 语义恒为 DIP」的既有契约自此真正生效（Phase 13 立 · Phase 20 兑现）。
	//   ★ 走**应用级接缝**（PlatformApplication）而非就地直调 Win32 API——
	//     核心不变量「每个 Win32 API 唯一归属」。
	//   ★ 失败容忍（契约 C6）：平台层只记日志；框架始终读真实 DPI、不假定本声明成功。
	m_platformApplication->DeclareDpiAwareness();

	// 7.1.5：延迟清理逻辑注册给平台循环（时机平台控制——每条消息后 PerformDeferredCleanup）
	m_platformApplication->SetDeferredCleanup([this]{ ProcessDeferredDestroy(); });

	// Phase 14 D3：托盘事件上行通道（O-1 契约 A——构造期注册，早于一切上行可能；
	// sink 内只调 OnEvent——经既有 EventRouter 分派到 OnTrayEvent 虚方法）
	m_platformApplication->SetTrayEventSink([this](const TrayEvent& event){
		OnEvent(event);
	});

}

Application::~Application(){

	// Phase 14 O-1：先断托盘事件上行，再让 m_platformApplication 析构——
	// 防止宿主窗口销毁过程中的 Shell 回调访问已析构的 Application（谁注册谁清理）
	m_platformApplication->SetTrayEventSink(nullptr);

}

int Application::Run() {

	// 7.1.5：消息循环下沉平台层（GetMessageW/TranslateMessage/DispatchMessageW 唯一归属
	// Win32PlatformApplication——Application 零 Win32）
	return m_platformApplication->Run();

}

Window& Application::Create(const std::string& title, int width, int height,
                            RenderServices services) {
	// ⚠️ B1-t：必须写作 unique_ptr(new Window(...))，**不可**用 std::make_unique<Window>(...)。
	//    原因：构造器为 private（B1），而 make_unique 的函数体**不是 Application 的成员**——
	//    friend 授权只在"访问发生处"生效，make_unique 内部会因无权访问而编译失败。
	//    此处 new 表达式就在本成员函数体内，friend 生效；所有权**立即**交给 unique_ptr（非裸指针）。
	m_windows.emplace_back(std::unique_ptr<Window>(new Window(*this, title, width, height,
	                                                        std::move(services))));
	Window& window = *m_windows.back();

	// 手动派发 WindowCreatedEvent（不是 Win32 消息翻译的产物，是框架层语义事件）
	WindowCreatedEvent event(&window);

	OnEvent(event);


	return window;
}

void Application::Exit(){
	if (!m_running)
		return;
	m_running = false;
	m_platformApplication->RequestExit();   // 7.1.5：退出请求下沉（PostQuitMessage 唯一归属平台层）
}

void Application::ProcessDeferredDestroy() {
	// 清空延迟销毁列表（unique_ptr 析构，Window 资源释放）
	//
	// ⚠️ 契约（window-ownership.md §4.3——本前提此前从未表达）：**Window 对象的回收时机依赖消息循环**。
	// 本函数由平台消息循环在**每条消息后**调用（Win32PlatformApplication::PerformDeferredCleanup
	// → SetDeferredCleanup 注入的回调，见构造体）；若消费者不调用 Application::Run()（如测试），
	// 清理永不发生，对象将延后到 ~Application 的成员析构。—— 消费者**不得**假设
	// 「窗口关闭后 Window 对象立即析构」。
	// 注：以上是**当前实现路径**，非「设计保证的固定顺序」（~Application 残留窗口隐患另案记账）。
	m_deferredDestroy.clear();
}

void Application::OnWindowCreated(
	const WindowCreatedEvent& event)
{
	// 默认空实现，子类可 override 执行初始化逻辑
}

void Application::OnWindowDestroyed(
	const WindowDestroyedEvent& event){
	// 将已销毁的 Window 从活跃列表移到延迟销毁列表
	auto it = std::find_if(
		m_windows.begin(),
		m_windows.end(),
		[&](const auto& window)
		{
			return window.get() ==
				event.GetWindow();
		});

	// 容器成员关系是**内部不变量**（B1/B2 之后，框架外已无法产生未登记窗口）。
	// Debug 构建以断言暴露框架自身的缺陷；Release 构建保留下面的运行时分支，
	// 避免不变量一旦被违反时「无效迭代器解引用」进一步演化为未定义行为。
	FRAMEWORK_ASSERT(it != m_windows.end());

	if (it == m_windows.end()){
		return;
	}

	m_deferredDestroy.emplace_back(
		std::move(*it)
	);


	m_windows.erase(it);


	// 所有窗口都关闭了，退出消息循环（Phase 14 R13：常驻应用可关闭隐式退出——默认 true = 零行为变更）
	if (m_windows.empty() && m_quitOnLastWindowClosed){
		Exit();
	}
}

void Application::OnWindowResized(
	const WindowResizedEvent& event)
{
	event.GetWindow()
		->GetRootWidget()
		.Arrange();
}

void Application::OnWindowCloseRequested(
	const WindowCloseRequestedEvent& event
)
{
	// 用户请求关闭 → 销毁底层 HWND
	event.GetWindow()->Release();
}

void Application::OnTimer(const TimerEvent& event){

	// 9.6：动画统一 tick 路由（d2——保留 timerId 分支，先于焦点派发；
	// TimerEvent 自带来源窗口 GetWindow()——结构性免查找，不经焦点链）
	if (event.GetTimerId() == AnimationManager::kAnimationTickTimer){

		if (event.GetWindow() != nullptr){

			event.GetWindow()->OnAnimationTick();

		}

		return;

	}

	// 8.5.1：定时器触发 → 派发给焦点控件（与 OnCharInput 同路径——非坐标事件，无 HitTest）
	Widget* target = FindFocusedWidget(*event.GetWindow());

	if (target == nullptr){

		return;

	}

	target->OnTimer(event);

}

Widget* Application::FindTargetWidget(
	Window& window,
	int x,
	int y)const noexcept {

	return window.GetRootWidget().HitTest(x, y);

}

Widget* Application::FindFocusedWidget(Window& window)const noexcept {

	return window.GetFocusedWidget();

}

// ── 鼠标事件处理（HitTest → Target Dispatch → Bubbling）────────────

void Application::OnMouseMove(const MouseMoveEvent& event){

	Window& window = *event.GetWindow();

	// 9.5 R4 契约 A：Capture 存在 → hover 状态机完全冻结（MouseMove 直派捕获者）
	Widget* captureTarget = window.GetCaptureWidget();

	if (captureTarget != nullptr){

		Widget* current = captureTarget;

		while (current != nullptr){

			current->OnMouseMove(event);

			current = current->GetParent();

		}

		return;   // 冻结：不进入 hover 状态机
	}

	// 1. HitTest：通过坐标找到目标 Widget
	Widget* newTarget = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());

	// 9.5 R4：更新 Hover 状态机（唯一入口）
	// 不改变既有 MouseMove 语义：现有代码在 target==nullptr 时直接 return（不派发）
	// R4 保持这一行为——仅追加 hover 状态机更新，不引入新的 MouseMove 接收者
	window.UpdateHoverState(newTarget);

	// 2. 无目标则忽略事件（不派发——与既有语义一致）
	if (newTarget == nullptr){

		return;

	}

	// 3. Bubbling：沿 Parent 链逐级调用 OnMouseMove
	Widget* current = newTarget;

	while (current != nullptr){

		current->OnMouseMove(event);

		current = current->GetParent();

	}

}

void Application::OnMouseButtonDown(const MouseButtonDownEvent& event){

	Window& window = *event.GetWindow();

	Widget* target = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {

		// 防御：RootWidget 覆盖整个客户区，正常不会到这
		return;

	}

	// 5.4.3：点击焦点语义——可聚焦区域设焦点、不可聚焦（RootWidget/Panel 空白）清焦点
	// （RootWidget 全覆盖下 target 恒非空，"空白"需按 CanFocus 判定；Phase 5 回顾 R1：删重复设置）
	window.SetFocusedWidget(target->CanFocus() ? target : nullptr);

	// 5.4.2：隐式捕获——命中即捕获（后续 Move/Up 直接派发给它，跳过 HitTest）
	window.SetCaptureWidget(target);

	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseButtonDown(event);

		current = current->GetParent();

	}

}

void Application::OnMouseButtonUp(const MouseButtonUpEvent& event){

	Window& window = *event.GetWindow();

	Widget* target = window.GetCaptureWidget();

	if (target == nullptr) {

		target = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());

	}

	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseButtonUp(event);

		current = current->GetParent();

	}

	// 5.4.2：先派发再释放（GPT 修正——控件 Up 里 GetCaptureWidget 仍能拿自身状态）
	window.SetCaptureWidget(nullptr);

}

void Application::OnMouseWheel(const MouseWheelEvent& event){

	Widget* target = FindTargetWidget(*event.GetWindow(), event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {

		return;

	}

	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseWheel(event);

		current = current->GetParent();

	}

}

void Application::OnCharInput(const CharInputEvent&event){

	Widget* target = FindFocusedWidget(*event.GetWindow());

	if (target == nullptr) {

		return;

	}

	target->OnCharInput(event);

}

void Application::OnKeyDown(const KeyDownEvent& event) {

	// 5.4.4：键盘入口统一走 Window（Tab 拦截 + 焦点控件派发）——焦点属于 Window
	event.GetWindow()->HandleKeyDown(event);

}

void Application::OnKeyUp(const KeyUpEvent& event) {

	Widget* target = FindFocusedWidget(*event.GetWindow());

	if (target == nullptr) {

		return;
	
	}

	target->OnKeyUp(event);

}

// ── Phase 14：托盘透传 + 拖入派发 ────────────────────────────────

void Application::SetTrayIcon(const TrayIconOptions& options){
	m_platformApplication->SetTrayIcon(options);
}

void Application::RemoveTrayIcon(){
	m_platformApplication->RemoveTrayIcon();
}

int Application::ShowTrayMenu(const TrayMenu& menu){
	return m_platformApplication->ShowTrayMenu(menu);
}

void Application::SetQuitOnLastWindowClosed(bool enabled){
	m_quitOnLastWindowClosed = enabled;
}

// ── Phase 14 R11：拖入派发（HitTest → Dispatch → Bubbling——与鼠标同族）──
void Application::OnDropFiles(const DropFilesEvent& event){

	Window& window = *event.GetWindow();

	// 1. HitTest（落点为客户区坐标——DragQueryPoint 既有语义，与鼠标同系）
	Widget* target = FindTargetWidget(window, event.GetX(), event.GetY());

	// 2. 无目标则忽略（与 OnMouseMove 同款语义）
	if (target == nullptr){

		return;

	}

	// 3. Bubbling：沿 Parent 链逐级调用（与 OnMouseMove 同款——不查 IsHandled）
	Widget* current = target;

	while (current != nullptr){

		current->OnDropFiles(event);

		current = current->GetParent();

	}

}

}
