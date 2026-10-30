#include "ECDI/Application/Application.h"

#include "Platform/Win32/Win32PlatformApplication.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Platform/PlatformWindow.h"   // Phase 22：调用 ApplyStartupSize（Window.h:24 仅前置声明）
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
#include <utility>

namespace ECDI{

// ★ Phase 35：跨类访问桥（定义处才拥有 friend 权限——clang-cl/GCC/MSVC 三链一致）
//   定义体放 .cpp ⇒ 头文件无需包含 Widget 的完整定义（`unique_ptr<Widget>` 参数类型在
//   `Application.h` 里已有前置声明 + 显式析构约定）。
bool ApplicationAccess::IsDispatching(const Application& application) noexcept{

	return application.IsDispatching();

}

void ApplicationAccess::DeferDestroy(Application& application, std::unique_ptr<Widget> widget){

	application.DeferDestroy(std::move(widget));

}

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

	// ★★ Phase 32/#57 修复（deferred-destroy 挂账的兑现——ProcessDeferredDestroy 注
	// 「~Application 残留窗口隐患另案记账」）：**析构期容器改写 UB**。机制（MinGW 实测
	// 0xC0000374，栈 = ~Application → ~Window → DestroyWindow → WM_DESTROY →
	// OnWindowDestroyed → RtlFreeHeap）：
	// ① 成员析构序 = 声明序的**逆序** ⇒ m_deferredDestroy（声明在 m_windows 后）先亡，
	//    其堆块已释放；② m_windows 析构逐元素 ~Window → DestroyWindow → **同步
	//    WM_DESTROY** → OnWindowDestroyed → `m_deferredDestroy.emplace_back(...)` 写入
	//    **已析构 vector 的悬垂块**（0xC0000374 的直接来源）；③ `m_windows.erase(it)`
	//    又发生在 m_windows 自身元素析构循环中 ⇒ erase 收缩 finish 到循环指针之后，
	//    libstdc++ 的 `first != last` 永不再相等 ⇒ 循环越过数组尾销毁垃圾 unique_ptr
	//    （第二重 UB）。触发条件 = Application **析构时仍有存活窗口**（M1 的
	//    DesktopNestApp 按 C-M1-20 由基类容器持有窗口 ⇒ M1 全套测试必现；生产 App
	//    正常退出时最后窗口已关 ⇒ m_windows 空 ⇒ 不触发——#57 只在测试链暴露的原因）。
	// 修复 = 析构体**第一件事**置位（此刻所有成员仍存活），随后**显式排水**两个容器
	// （WM_DESTROY 照常同步派发，但 OnWindowDestroyed 见旗标直返——容器不再被改写）。
	m_shuttingDown = true;

	m_deferredDestroy.clear();   // 延迟列表：HWND 已销毁，~Window→Release 幂等（C32/#57 前已验证）

	m_windows.clear();           // 残留窗口：逐个 ~Window → DestroyWindow → WM_DESTROY
	                             // （OnWindowDestroyed 旗标直返——不再 erase/emplace）

	// ★ Phase 35：延迟移除的控件收尾——**必须在 m_windows 之后**（队列里的控件已脱树，
	// 本可先析构；但排在窗口之后可保证「控件析构函数里访问所属 Window」的既有惯用法不失效）。
	// 正常路径下队列此时已空（每次派发结束都会排水），这里只兜底「应用层在 ~Application 前
	// 留下了未排空的请求」——不做延迟，直接析构。
	m_deferredWidgets.clear();

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

	// ★★ Phase 22：把构造期记录的 DIP 目标尺寸**按当前 DPI 落实**。
	// 为什么在这里：此刻 `Window` **已完全构造**（成员就绪 ⇒ 回调安全，避开 Phase 20 的构造期 UB），
	//   且**本函数尚未返回** ⇒ 「公共 API 语义恒为 DIP」在 `Create` 返回时即成立。
	// 为什么在事件**之前**：`WindowCreatedEvent` 的监听者若读窗口几何，应当看到**已成立**的值。
	// 幂等：`Show()` 会再调一次（跨屏复核），届时为 no-op（契约 C3 / C6）。
	window.GetPlatformWindow().ApplyStartupSize();

	// 手动派发 WindowCreatedEvent（不是 Win32 消息翻译的产物，是框架层语义事件）
	WindowCreatedEvent event(&window);

	OnEvent(event);


	return window;
}

bool Application::PostToUi(std::function<void()> work){

	// 7.1.5 / Phase 23：本函数**只做转发**——队列、唤醒、关闭态全部在平台实现内部。
	//   ★ std::move：形参已是按值副本，若此处传左值会**二次拷贝** std::function（可能再分配）。
	//   ★ 不 catch：std::bad_alloc 属资源耗尽，不属于"提交被拒绝"（V6 定案）。
	return m_platformApplication->PostToUi(std::move(work));

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

	// ★ Phase 35：控件级延迟移除的兜底排水。正常路径下每次派发结束（最外层）已排空，
	// 这里是「应用层在非派发上下文里留下了请求队列」的唯一兜底（当前 API 面产生不了这种状态，
	// 防御性保留——幂等，代价是一次空 vector 检查）。
	DrainDeferredRemoval();
}

// ── ★ Phase 35：事件派发语义（消费 + 存活保护 + 延迟移除）──────────────────

void Application::DeferDestroy(std::unique_ptr<Widget> widget){

	if (!widget){

		return;

	}

	// ⚠️ 不复用 m_deferredDestroy（窗口队列）：语义不同（控件 vs 窗口）、生命周期锚点不同
	//    （派发结束 vs 消息循环轮次）。共用会让「派发结束冲掉窗口队列」这种耦合凭空出现。
	m_deferredWidgets.push_back(std::move(widget));

}

void Application::DrainDeferredRemoval(){

	// 逆序析构：后入队者先亡。典型入队序 = 子 → 父（子回调里先删自己、再删父），
	// 逆序 ⇒ 父后亡 —— 与「子树自上而下析构」的自然顺序一致。
	// 循环到空：析构函数里若再走 Widget::RequestRemove（此时已不在派发中 ⇒ 走立即分支），
	// 不会写入本队列；但若某控件析构函数经 PostToUi/直接派发再次入队，也一并排空。
	while (!m_deferredWidgets.empty()){

		m_deferredWidgets.pop_back();   // unique_ptr 析构 ⇒ ~Widget（此处是唯一析构点）

	}

}

std::vector<Widget*> Application::BuildDispatchChain(Widget* target, Widget* root, Widget* secondaryRoot){

	// ★ 冻结祖先链：target → ... → root（root 为 nullptr 时上溯到真正的树根）。
	//   为什么在**派发开始时**一次性算好（而不是在循环里 `current->GetParent()`）：
	//   ① 回调里摘树会断父链 —— 上溯会**提前终止**（父控件收不到它本应收到的 Up），
	//      而「已消费 ⇒ 停止冒泡」只是**一个**停止条件，不该被「树结构变了」这种副作用冒充；
	//   ② 回调里对被摘节点的任何访问（`GetParent()` 本身）都可能是 UAF。
	//   冻结后的链 = 「本次派发**还该向谁派发**」的集合：之后挂上的新祖先**本次收不到**事件
	//   （可测试、可解释：一次点击的接收者集合在点击**开始时**就已确定）。
	//   ⚠️ 链上的节点由 DispatchWithChain 的 Prepare/Release 钉住存活（RequestRemove 只摘树）。
	std::vector<Widget*> chain;

	if (target == nullptr){

		return chain;   // 空链：调用方据此跳过派发（与既有 `target == nullptr` 早返语义一致）

	}

	chain.push_back(target);

	// ★ 注意根边界：`window.GetRootWidget()` **本身也在链上**（= 链尾）——点击空白区时
	//   RootWidget 就是唯一接收者（既有行为；Phase 5 回顾 R1：不可聚焦区域清焦点）。
	//   故上溯终止条件是 `node == nullptr`；`root` 只用于 Debug 断言。
	for (Widget* node = target->GetParent(); node != nullptr; node = node->GetParent()){

		chain.push_back(node);

	}

	// Debug 校验：链尾理应就是 window 的 RootWidget。仅断言不修链——异常形态下
	//   「按父链走完」仍是安全选择（宁可多派发一级，也不丢事件）。
	FRAMEWORK_ASSERT(root == nullptr || chain.back() == root || chain.back() == secondaryRoot);

	return chain;

}

void Application::OnWindowCreated(
	const WindowCreatedEvent& event)
{
	// 默认空实现，子类可 override 执行初始化逻辑
}

void Application::OnWindowDestroyed(
	const WindowDestroyedEvent& event){

	// ★ Phase 32/#57：析构期直返——~Application 已置位 m_shuttingDown 并显式排水，
	// 此后每个 ~Window 的同步 WM_DESTROY 都会到达这里；若继续执行，
	// `m_deferredDestroy.emplace_back`（已析构 vector 的悬垂块）与 `m_windows.erase`
	// （析构循环中）都是 UB（#57 的精确机制，详见 ~Application 处注释）。
	if (m_shuttingDown)
	{
		return;
	}

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
	// ★ 审计 M1：与上面的动画分支**对称判空**——`TimerEvent::GetWindow()` 可为 null
	//（TrayEvent 的 GetWindow() 恒为 nullptr；今天「托盘事件不走 OnTimer」只是巧合，
	//  不是结构性保证）。动画分支（:216）已判空，本分支原先直接解引用。
	if (event.GetWindow() == nullptr){

		return;

	}

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

	return window.HitTestWindow(x, y);

}

Widget* Application::FindFocusedWidget(Window& window)const noexcept {

	// ★ 审计 H7（#65）：键盘通道的 IsEnabled 门控**收敛在这一处**——四个调用点
	//   （OnTimer / OnCharInput / OnKeyUp / FindFocusedWidget 自身）全部是「派发给
	//   焦点控件」的同族路径，没有调用点想要「禁用控件的焦点」⇒ 判据放助手而非散在三处。
	//   鼠标通道的对应判据在 `Widget::HitTest:124-128`（不可用 ⇒ 不参与命中）。
	//   ⚠️ 只查 IsEnabled、不查 IsVisible：能成为焦点说明收集时可见
	//      （`CollectFocusables` 已过滤不可见整棵子树）；运行期被隐藏是「焦点悬空」，
	//      属 §7.1 统一失效机制线（#59），不在本条范围。
	Widget* focused = window.GetFocusedWidget();

	return (focused != nullptr && focused->IsEnabled()) ? focused : nullptr;

}

// ── 鼠标事件处理（HitTest → Target Dispatch → Bubbling）────────────
//
// ★★ Phase 35 起，本区块**所有**冒泡派发都走同一套骨架（与「兄弟路径参数化」同一方法论）：
//      chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
//      DispatchWithChain(chain, [&]{ BubbleAlongChain(chain, event, [&](Widget& w){ w.OnXxx(event); }); });
//   为什么必须统一：契约要的是「Down/Up/Move/Wheel/Drop **行为一致**」——
//   三个循环各自实现 ⇒ 迟早出现「只拦 Up 不拦 Down」的漂移（本需求稿第 2 条明确禁止）。
//   ★ 骨架同时提供：① 祖先链**冻结**（摘树不断链）；② 派发期间**钉住**（RequestRemove 只摘树）；
//   ③ 已移除节点**跳过**；④ 已消费**停止冒泡**；⑤ 最外层派发结束统一**排水**（延迟析构）。

void Application::OnMouseMove(const MouseMoveEvent& event){

	Window& window = *event.GetWindow();

	// 9.5 R4 契约 A：Capture 存在 → hover 状态机完全冻结（MouseMove 直派捕获者）
	Widget* captureTarget = window.GetCaptureWidget();

	if (captureTarget != nullptr){

		// ★ 捕获通道与命通道骨架完全相同（只是目标来源不同）——
		//   捕获者可能在派发中被移除：链照样冻结，已移除则跳过其自身回调。
		std::vector<Widget*> chain = BuildDispatchChain(captureTarget, &window.GetRootWidget(), &window.GetOverlayRoot());

		DispatchWithChain(chain, [&]{

			BubbleAlongChain(chain, event, [&](Widget& w){ w.OnMouseMove(event); });

		});

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

	// 3. Bubbling：沿**冻结链**逐级调用 OnMouseMove（消费 / 已移除 两种停止条件见头文件）
	std::vector<Widget*> chain = BuildDispatchChain(newTarget, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		BubbleAlongChain(chain, event, [&](Widget& w){ w.OnMouseMove(event); });

	});

}

void Application::OnMouseButtonDown(const MouseButtonDownEvent& event){

	Window& window = *event.GetWindow();

	// ★ Phase 42：Light Dismiss 点外关闭前置拦截
	if (window.HasActiveOverlay()){
		Widget* hit = window.HitTestWindow(event.GetMouseX(), event.GetMouseY());
		auto* topOverlay = window.GetTopActiveOverlay();

		if (topOverlay != nullptr && topOverlay->options.lightDismiss != LightDismissPolicy::None){
			const bool isInside = (hit != nullptr && window.IsWithinSubtree(hit, topOverlay->widget));
			const bool isExempt = (hit != nullptr && topOverlay->options.exemptWidget != nullptr &&
			                       (hit == topOverlay->options.exemptWidget ||
			                        window.IsWithinSubtree(hit, topOverlay->options.exemptWidget)));

			if (!isInside && !isExempt){
				const LightDismissPolicy policy = topOverlay->options.lightDismiss;
				window.HideOverlay(nullptr); // 关闭顶层活动浮层

				if (policy == LightDismissPolicy::Consume){
					return; // 拦截消费，不向底层继续传递
				}
			}
		}
	}

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

	// ★ Phase 35：捕获/焦点已在**派发之外**建立（不进入钉住区——它们只改 Window 状态，
	//   不访问控件树）；下面才是控件回调区。
	std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		BubbleAlongChain(chain, event, [&](Widget& w){ w.OnMouseButtonDown(event); });

	});

}

void Application::OnMouseButtonUp(const MouseButtonUpEvent& event){

	Window& window = *event.GetWindow();

	Widget* target = window.GetCaptureWidget();

	if (target != nullptr && target->GetWindow() != &window){

		// ★ Phase 35 防御：捕获指针属于**另一个**窗口（理论上不该发生——SetCaptureWidget 只在
		//   Down 里按本窗口 HitTest 结果设置，摘树也会清；但一旦发生，跨窗口上溯会派发到
		//   无关的控件树上）。降级为按坐标重新命中，而不是信任陈旧指针。
		target = nullptr;

	}

	if (target == nullptr) {

		target = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());

	}

	if (target == nullptr){

		return;   // 防御：RootWidget 覆盖整个客户区，正常不会到这

	}

	// 5.4.2：先派发再释放（GPT 修正——控件 Up 里 GetCaptureWidget 仍能拿自身状态）
	//   ★ 放在 DispatchWithChain **之后**（而非 bubble 循环之后、排水之前）：
	//     排水析构发生在派发结束的同一时机，capture 指针若在排水期被读就是悬垂。
	//     摘树路径已由 `Window::ForgetSubtree` 清空 capture——这里只处理「没摘树、单纯结束」的常态。
	std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		BubbleAlongChain(chain, event, [&](Widget& w){ w.OnMouseButtonUp(event); });

	});

	window.SetCaptureWidget(nullptr);   // 先派发再释放；摘树路径已由 ForgetSubtree 清空（幂等）

}

void Application::OnMouseWheel(const MouseWheelEvent& event){

	Window& window = *event.GetWindow();

	Widget* target = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {

		return;

	}

	// ⚠️ **滚轮冒泡的边界**（条 135 同族）：子控件不 SetHandled 时事件继续
	//   落到父控件滚内容 = **期望行为**；`BubbleAlongChain` 只让「已显式消费」的子控件
	//   阻止冒泡，不改变默认的冒泡语义。
	std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		BubbleAlongChain(chain, event, [&](Widget& w){ w.OnMouseWheel(event); });

	});

}

void Application::OnCharInput(const CharInputEvent&event){

	Window& window = *event.GetWindow();

	Widget* target = FindFocusedWidget(window);

	if (target == nullptr) {

		return;

	}

	// ★ Phase 35：单收件人派发也走同一骨架——**不是为了冒泡**（CharInput 不冒泡），
	//   而是为了「回调里 RequestRemove()」同样安全：目标被钉住 ⇒ 摘树后延迟析构。
	//   链 = target → 祖先（BubbleAlongChain 只取**第一个未移除**节点 = target 本身；
	//   若 target 在本次派发的更早一步里已被移除，则向它的祖先降级派发——见下方 BubbleAlongChain 语义）。
	std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		// CharInput 的契约是「只给焦点控件」⇒ 只取链首；链首已移除时直接不发（不降级到祖先）。
		Widget* head = chain.front();

		if (!head->IsRemovedDuringDispatch()){

			head->OnCharInput(event);

		}

	});

}

void Application::OnKeyDown(const KeyDownEvent& event) {

	// 5.4.4：键盘入口统一走 Window（Tab 拦截 + 焦点控件派发）——焦点属于 Window
	// ★ Phase 35：Window::HandleKeyDown 内含焦点控件的派发（Tab 导航 + OnKeyDown），
	//   它自己**不开钉住区**；若按键回调里 RequestRemove 焦点控件，摘树即安全
	//   （ForgetSubtree 清焦点、对象进延迟队列）。见 Window.cpp 的 HandleKeyDown 注释。
	//   ⚠️ **必须**在本入口开钉住区：`RequestRemove` 只在 IsDispatching()（最外层派发计数 >0）
	//      时才延迟；若只派发 Up/Char/Drop 而不派发 Down，计数在 Up 之后已归零，
	//      孤立的 `HandleKeyDown`（测试/自定义消息泵直接调用）就会把焦点控件**立即析构**
	//      ⇒ 回调体内后续语句踩已析构对象（实测 0xC0000005）。
	//      此处的「链」= 焦点控件 → 根（窗口没有焦点时为空链，无副作用）。
	Widget* keyboardTarget = FindFocusedWidget(*event.GetWindow());

	if (keyboardTarget == nullptr) {

		event.GetWindow()->HandleKeyDown(event);   // Tab 导航等窗口级处理照常

		return;

	}

	// 键盘是**单收件人**语义（不冒泡）：链只用于钉住存活与「同一 Application::OnKeyDown 在
	// 回调内被重入派发」时的移除标记，HandleKeyDown 自身只摸窗口的焦点成员。
	std::vector<Widget*> chain = BuildDispatchChain(keyboardTarget, &event.GetWindow()->GetRootWidget(), &event.GetWindow()->GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		if (chain.front()->IsRemovedDuringDispatch()) {

			return;   // 外层已移除该焦点控件 ⇒ 不再派发（与 BubbleAlongChain 条件 3 一致）

		}

		event.GetWindow()->HandleKeyDown(event);

	});

}

void Application::OnKeyUp(const KeyUpEvent& event) {

	Window& window = *event.GetWindow();

	Widget* target = FindFocusedWidget(window);

	if (target == nullptr) {

		return;
	
	}

	// ★ Phase 35：与 CharInput 同款——单收件人 + 钉住（回调里可安全 RequestRemove）
	std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		Widget* head = chain.front();

		if (!head->IsRemovedDuringDispatch()){

			head->OnKeyUp(event);

		}

	});

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

	// 3. Bubbling：沿**冻结链**逐级调用（与 OnMouseMove 同款——消费即停、已移除即跳过）
	std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());

	DispatchWithChain(chain, [&]{

		BubbleAlongChain(chain, event, [&](Widget& w){ w.OnDropFiles(event); });

	});

}

}
