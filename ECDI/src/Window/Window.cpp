#include "ECDI/Window/Window.h"

#include "Platform/Win32/Win32PlatformWindow.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/Logger.h"          // Phase 32：Degraded 不可请求 / Show 后配置期 Warning
#include "ECDI/Core/Point.h"
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/Render/PaintContext.h"

#include <algorithm>   // Phase 32：std::clamp（tint → 清屏色的 ToByte 语义）
#include <cstdint>     // Phase 32：衬底清屏色 AABBGGRR
#include <string>
#include <memory>
#include <system_error>
#include <vector>

namespace ECDI{

// ★★ 审计 M3 / M4（仅订正注释——不改行为）：本类的判空风格**故意不统一**，
//   理由是两个成员的存在性保证**不同**：
//     · `m_platformWindow`：**初始化列表里必建**（见 `Window.h` 成员声明与构造器）
//       ⇒ 对象一旦构造完成，它恒非空 ⇒ 转发型方法（`SetChromeMode` /
//         `SetCaptionHeight` / `SetResizeInset` / `SetWindowLayer` / `SetBounds` /
//         `GetPlatformWindow`）**无需判空**；少数方法（`Invalidate` /
//         `GetDpiScale` / `SetFileDropEnabled`）仍判空是**防御性冗余**
//         （保留不动——不是缺陷，删掉反而增加风险）。
//     · `m_rootWidget`：构造期**确实可能为 null**（它在 Window 构造体里创建，
//       而 `WM_PAINT` 等回调可在构造完成前到达——条 108）⇒ `PaintFrame`
//       对它的判空是**必需**的。
//   ⇒ 审计 M3（两类风格并存）与 M4（`PaintFrame` 判了 root 不判 platform）
//     都**不是缺陷**，而是上述两成员存在性差异的正确反映。
//     ★ 「统一风格」在此处是**反向操作**：给 platform 转发补判空是死代码，
//       去掉 root 判空会引入 UB。故只订正注释、说明口径。

namespace {

// 5.4.4：DFS 收集可聚焦控件（树前序：父 → 子；匿名 namespace——Window 内部辅助，非公开能力）
// ★ 审计 H7（#65）：与 `Widget::HitTest` 的语义对齐——
//   **不可见的节点 ⇒ 整棵子树跳过**（`HitTest:117-121` 正是如此：入口即 return nullptr，
//   子节点根本没机会被访问）；**不可用（disabled）的节点 ⇒ 自身不收，但子树仍可聚焦**
//   （与 `HitTest:124-128` 一致——那里也只 return 自身，不阻止递归）。
//   修复前只判 `CanFocus()` ⇒ 一个 `SetVisible(false)` 的 TextBox 仍能被 Tab 聚焦并接受键入
//   （用户看不见内容在变）；一个 `SetEnabled(false)` 的控件仍响应键盘。
// ⚠️ 「不可见跳整棵子树」是**刻意与 HitTest 对齐**的选择：焦点语义与命中语义必须同源，
//   否则会出现「Tab 能到、鼠标点不到」的幽灵焦点（比两个通道都不过滤更糟）。
void CollectFocusables(Widget* node, std::vector<Widget*>& out){

	if (!node->IsVisible()){

		return;   // 不可见 ⇒ 整棵子树不可聚焦（同 HitTest 的入口门控）

	}

	if (node->IsEnabled() && node->CanFocus()){

		out.push_back(node);

	}

	for (size_t i = 0; i < node->GetChildCount(); ++i){

		CollectFocusables(node->GetChildAt(i), out);

	}

}

}

Window::Window(Application& app,const std::string& title, int width, int height,
               RenderServices services)
	: m_application(app)
	, m_renderBackend(std::move(services.renderer))
	, m_textMeasurer(std::move(services.measurer))
	, m_renderer(*m_renderBackend)   // 决策 34：Renderer 持 RenderingBackend&——⚠️ m_renderBackend 声明在 m_renderer 前
	, m_platformWindow(std::make_unique<Win32PlatformWindow>(*this, title, width, height))
	, m_animationManager(*m_platformWindow){   // 9.6：能力接缝——manager 只持 PlatformWindow&（声明顺序保证 *m_platformWindow 先构造）

	// 9.6：elapsed 锚点重置钩子——timer 启动时刻起算首次 tick 的 elapsed（防 epoch 累积跳变）
	m_animationManager.SetOnTimerStarted([this]{

		m_lastAnimationTick = std::chrono::steady_clock::now();

	});

	// 7.1.4：句柄注入经 PlatformRenderContext（取代 7.1.1 过渡 SetHwnd(GetHandle())——
	// Window 不再接触 HWND；识别发生在平台实现内部 static_cast）
	m_renderBackend->Initialize(m_platformWindow->GetRenderContext());

	// ★ Phase 26：与后端**同源同序**（详设契约 C-3）——测量器亦取窗口 DPI（D-8 闭合）。
	m_textMeasurer->Initialize(m_platformWindow->GetRenderContext());

	// 创建 RootWidget（Widget 树的根节点，代表窗口客户区）
	m_rootWidget = std::make_unique<Widget>();

	// 5.4.1：根与 Window 建立关联（Widget::Invalidate/HasFocus 上溯到根后走它）
	m_rootWidget->SetWindow(this);

	// 9.5 R4：Hover 状态机注入树根（契约 C 验证锚点——m_rootWidget 就绪后设置）
	m_hoverTracker.SetTreeRoot(m_rootWidget.get());

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

	m_shown = true;   // Phase 32：Show() 调用事实（配置期闸门判据——「API 调用事实」非「系统当前可见」）

}

void Window::Hide() {

	// Phase 14 R12：Show 的对称——隐藏 ≠ 销毁（不触发 WindowDestroyedEvent，窗口仍在册）
	if (m_platformWindow != nullptr) {

		m_platformWindow->Hide();

	}

}

void Window::SetFileDropEnabled(bool enabled) {

	// Phase 14 R8：转发平台能力（默认关闭——零回归底线）
	if (m_platformWindow != nullptr) {

		m_platformWindow->SetFileDropEnabled(enabled);

	}

}

Widget& Window::GetRootWidget() noexcept {

	return *m_rootWidget;

}

// ★ Phase 35：Application 访问器（实现在此——头文件只前置声明 Application，避免 Header 环）
Application& Window::GetApplication() const noexcept {

	return m_application;

}

void Window::PaintFrame()
{
	// ★ Phase 20（崩溃修复）：**构造期防御**。本函数可经 `WM_PAINT → OnPaint` 进入，
	// 而 `m_rootWidget` 要到 `Window` 构造体里才创建 ⇒ 在它之前访问就是 UB
	// （原实现**连判空都没有**——同类回调入口都应至少判空）。
	if (m_rootWidget == nullptr){

		return;

	}

	// 决策 10/13/33：完整编排，严格配对
	m_commands.clear();                              // 决策 4：复用缓冲
	// Phase 27：初始裁剪 = client 矩形（每帧经 GetClientSize 取——resize 天然跟随；
	// 只进构建侧交集栈、不发命令——C27-5；D4 恒开，无运行期开关——D27-C）
	const Size clientSize = m_platformWindow->GetClientSize();
	PaintContext ctx(m_commands, *m_textMeasurer,
	                 Rect{ 0.0f, 0.0f, static_cast<float>(clientSize.width), static_cast<float>(clientSize.height) });   // 7.1.4：测量独立指针（GDITextMeasurer）
	m_rootWidget->Paint(ctx, 0, 0);                  // 决策 6：根从 (0,0)，offset 累加
	SyncBackdropSurface();                            // Phase 32（D32-H）：衬底实际态 → 后端（变化才下发）
	m_renderer.BeginFrame(m_backgroundColor);         // 决策 13：转发（Phase 18：携带本帧背景色）
	m_renderer.Execute(m_commands, GetDpiScale());   // Phase 20.1：每帧取窗口 DPI 缩放（不缓存 ⇒ 跨屏下一帧自动跟随）
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

	// 7.1.4：独立测量器（GDITextMeasurer——拆类后不再兼后端）——返回抽象接口不暴露具体实现
	return *m_textMeasurer;

}

PlatformWindow& Window::GetPlatformWindow() noexcept{

	// 8.5.1：平台能力入口（剪贴板/Timer 等）——薄返回抽象接口，实现是 Win32PlatformWindow
	return *m_platformWindow;

}

float Window::GetDpiScale() const noexcept{

	// Phase 20 R7/G4：薄转发到平台层（D-SEAM-1 第 ② 步——**应用层唯一入口**；
	// 契约与事实来源都在平台层，与 Invalidate / GetWindowState 同转发风格）。
	if (!m_platformWindow){

		return 1.0f;   // 平台窗口未就绪：恒等退化（与 G5 一致——不返回未定义值）

	}

	return m_platformWindow->GetDpiScale();

}

// ── Phase 12 WindowChrome（配置期四件套 + 运行期三方法）──────────────
// 全部为纯转发——契约判据（m_shown / m_chromeConfigured）在平台层，
// Window 不重复实现（单一真相源；与既有 Invalidate 转发同风格）。

void Window::SetChromeMode(ChromeMode mode){

	m_platformWindow->SetChromeMode(mode);

}

// ── Phase 32：窗口衬底（配置期）────────────────────────────────────────
// ★ 与 chrome 四件套「判据在平台层、Window 纯转发」不同：请求态与 tint 是**决策层状态**
//   （Phase 18 底色同构——单一真相源在 Window）⇒ Degraded 忽略与 Show 闸门必须在
//   Window 侧先于存储生效，否则平台已忽略而 Window 侧请求态已被改写 ⇒ 两层失同步。

void Window::SetWindowBackdrop(BackdropMode mode, const Color& tint){

	if (mode == BackdropMode::Degraded){

		// D32-M：Degraded 只读——完全忽略且不改变既有请求态
		Logger::Log(LogLevel::Warning,
			L"WindowBackdrop: Degraded is not a requestable mode - ignored");

		return;

	}

	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowBackdrop: SetWindowBackdrop ignored after Show() - config-time only");

		return;

	}

	m_backdropRequested = mode;
	m_backdropTint = tint;

	// 平台默认 no-op（批二 override 才真正应用——批一仅状态与转发通路）
	m_platformWindow->SetWindowBackdrop(mode, tint);

}

WindowBackdropState Window::GetWindowBackdropState() const noexcept {

	WindowBackdropState state;
	state.requested = m_backdropRequested;

	if (m_platformWindow != nullptr){

		const WindowBackdropState platform = m_platformWindow->GetWindowBackdropState();
		state.actual = platform.actual;
		state.applied = platform.applied;

	}

	return state;

}

// ── Phase 32（D32-H/R）：衬底实际态 → 渲染后端（帧首同步，**变化才下发**）──────
// 平台在 Show()/重应用后产出 actual；本函数把它翻译为后端的清屏语义：
//   Acrylic   = tint RGB + A=0（C32-12：材质透出 + tint 观感由缓冲承载）；
//   Degraded  = 预乘白 0.8 = 0xCCCCCCCC（R-c 固定形态，P4/P5 实证）；
//   None/未应用 = 不下发（后端保持既有清屏路径——逐位同 Phase 18，A6①）。
// 轮询式（每帧一次虚调用）而非事件回调：无新接缝、重应用天然跟随（详设 §4 △2 注）。

namespace {   // Window.cpp 局部：tint → 缓冲清屏色（**0x00RRGGBB**，A=0——C32-12）

std::uint32_t ComposeTintClearRGB(const Color& tint)
{
	const auto toByte = [](float v)
	{
		const float clamped = std::clamp(v, 0.0f, 1.0f);
		return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
	};
	// ★ E-B2-4（批四实测勘误）：缓冲字节序 BGRA（byte0=B）+ 小端 DWORD ⇒
	// **byte0 = 值的 bits 0-7 = B**、byte2 = bits 16-23 = R ⇒ 布局 = **0x00RRGGBB**。
	// 首版按「AABBGGRR」组装成 (b<<16)|(g<<8)|r ⇒ R/B 互换（E2E 抓屏定位：
	// SBS clear=002B2E38 而期望 00382E2B）。A 通道恒 0（材质区透出）。
	return (toByte(tint.r) << 16) | (toByte(tint.g) << 8) | toByte(tint.b);
}

}   // namespace

void Window::SyncBackdropSurface()
{

	BackdropMode mode = BackdropMode::None;
	std::uint32_t clear = 0;

	if (m_platformWindow != nullptr){

		const WindowBackdropState state = m_platformWindow->GetWindowBackdropState();

		if (state.applied && state.actual == BackdropMode::Acrylic){

			mode = BackdropMode::Acrylic;
			clear = ComposeTintClearRGB(m_backdropTint);      // tint RGB 承载观感（C32-12；0x00RRGGBB——E-B2-4）

		} else if (state.applied && state.actual == BackdropMode::Degraded){

			mode = BackdropMode::Degraded;
			clear = 0xCCCCCCCCu;   // 预乘白 0.8（R-c；tint 形参不参与降级——固定白）

		}

	}

	if (!m_backdropSurfaceSynced || mode != m_backdropModePushed || clear != m_backdropClearPushed){

		m_renderer.SetBackdropSurface(mode, clear);
		m_backdropModePushed = mode;
		m_backdropClearPushed = clear;
		m_backdropSurfaceSynced = true;

	}

}

void Window::SetCaptionHeight(int height){

	m_platformWindow->SetCaptionHeight(height);

}

void Window::SetResizeInset(int inset){

	m_platformWindow->SetResizeInset(inset);

}

void Window::SetWindowLayer(WindowLayer layer){

	m_platformWindow->SetWindowLayer(layer);

}

void Window::SetMinimumSize(int width, int height){

	m_platformWindow->SetMinimumSize(width, height);

}

void Window::SetBackgroundColor(const Color& color){

	// Phase 18：改自身成员（**不是**转发平台层——与 SetCaptionHeight 一类的形制不同）
	m_backgroundColor = color;

	// 职责契约（O1）：改了可见状态 → **自身**负责请求重绘（同 TextBox.cpp:189 的既定写法）
	Invalidate();

}

// ── Phase 31：窗口几何（运行期——纯转发，契约与判据在平台层）──────────

void Window::SetBounds(const Rect& bounds){

	// 与 SetWindowLayer / Minimize 同款纯转发（判据 = 非法输入校验 + 运行期门控，均在平台层）
	// ★ m_platformWindow 在构造初始化列表必建 ⇒ 不需要判空（同 SetChromeMode :216）
	m_platformWindow->SetBounds(bounds);

}

Rect Window::GetBounds() const{

	// ★ 判空（与 GetDpiScale :200 同款——const 只读访问器的防御风格）
	if (!m_platformWindow){

		return Rect{};

	}

	return m_platformWindow->GetBounds();

}

void Window::Minimize(){

	m_platformWindow->Minimize();

}

void Window::Maximize(){

	m_platformWindow->Maximize();

}

void Window::Restore(){

	m_platformWindow->Restore();

}

// ── Phase 13：窗口状态查询（R3）──────────────────────────────────

WindowState Window::GetWindowState() const noexcept{

	return m_platformWindow->GetWindowState();

}

// ── Phase 13：关闭请求入口（R4——语义层/可拦截；与系统 X 同路径）────

void Window::RequestClose(){

	WindowCloseRequestedEvent event(this);

	// 同步派发（先例：Application::Create 手动派发 WindowCreatedEvent）
	m_application.OnEvent(event);

}

// ── Phase 13：客户区可交互命中（R2——Host 契约实现）──────────────

bool Window::IsClientInteractiveAt(int x, int y) const noexcept{

	// 防御：RootWidget 尚未就绪时早退（与 OnResized 的既有守卫同款）。
	// 可达性：WM_NCHITTEST 需要窗口可见才有鼠标命中，此时 RootWidget 必已构造（构造期不产生鼠标命中）。
	if (!m_rootWidget){

		return false;

	}

	Widget* hit = m_rootWidget->HitTest(x, y);

	// D9 判据：命中「消费鼠标输入的控件」才算可交互——命中纯显示控件（如标题 Label）仍走 HTCAPTION
	return hit != nullptr && hit->ConsumesMouseInput();

}

// ── 动画系统（9.6）──────────────────────────────────────────────

void Window::OnAnimationTick(){

	// d3：steady_clock 真实 elapsed——WM_TIMER 无时间戳，固定步进会累积漂移；
	// 锚点由 timer 启动钩子重置（首次 tick 的 elapsed 从 timer 启动时刻起算）
	const auto now = std::chrono::steady_clock::now();

	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_lastAnimationTick);

	m_lastAnimationTick = now;

	// d9：Tick(elapsed) 参数化——Window 纯转发，零动画逻辑
	m_animationManager.Tick(elapsed);

}

AnimationManager& Window::GetAnimationManager() noexcept{

	return m_animationManager;

}

void Window::SetCaptureWidget(Widget* widget){

	// 5.4.2：隐式捕获——Down 命中设置、Up 后释放；非拥有指针（生命周期随 Widget 树）
	// ★★ 审计 #60 / C3：**同一次调用的两半**——框架登记 + 真实系统捕获。
	//   修复前本函数**只赋值**（「隐式捕获」纯框架记账，OS 层从未被告知）⇒ 拖拽/滚条
	//   越出客户区收不到 `WM_MOUSEMOVE` ⇒ 不跟手（**无自愈路径**，C3 的核心后果）。
	//   ⇒ 现在两半同步：`m_captureWidget`（框架路由判据）+ `PlatformWindow::SetMouseCapture`
	//     （OS 路由判据）。**必须有捕获目标才向 OS 申请捕获**——无目标却抓系统捕获会让
	//     本窗口吞掉全部鼠标消息而框架无人可派发（比不捕获更糟）。
	// ⚠️ 幂等：同目标重复设置时**不重复调** OS API（避免 `::SetCapture` 重入与无谓系统调用）。
	const bool wasCaptured = (m_captureWidget != nullptr);
	const bool willCapture = (widget != nullptr);

	m_captureWidget = widget;

	if (willCapture == wasCaptured){

		return;   // 状态未跨「无捕获 ⇄ 有捕获」边界 ⇒ OS 侧无需变更

	}

	if (m_platformWindow != nullptr){

		m_platformWindow->SetMouseCapture(willCapture);

	}

}

Widget* Window::GetCaptureWidget() const noexcept{

	return m_captureWidget;

}

void Window::OnCaptureLost(){

	// ★ 审计 #60 / C3：捕获被系统/第三方夺走时的框架侧清理（平台层经 Host 契约上报）。
	// ⚠️ **只清框架登记，不再回调平台**——此时捕获已不在本窗口，任何 `::ReleaseCapture`
	//   都会误伤对方（详见 `PlatformWindowHost::OnCaptureLost` 契约段）。
	// ⚠️ **不补发 Up**：与 #59 的摘树注销同族语义——「异常失效」不等于「正常抬起」，
	//   补发会让控件以为用户松了手（可能触发点击/提交等业务后果）。
	//   若控件自身有按下态需要复位，由它在下一次 Down 时自愈（既有 F1 语义：Down 永远重走 HitTest）。
	m_captureWidget = nullptr;

}

void Window::UpdateHoverState(Widget* newTarget){

	// 9.5 R4 方案 A：委托纯逻辑单元（HoverTracker——不依赖 Window 平台细节）
	m_hoverTracker.Update(newTarget);

}

// ── ★ 审计 #59 / H6：非拥有 Widget 指针的失效清理 ──────────────────────────

namespace{

/// @brief `widget` 是否落在 `subtreeRoot` 子树内（**含子树根自身**）
/// @details 沿父链上溯到 `subtreeRoot` 即命中。⚠️ 必须在摘树**清除父指针之前**调用——
///          链一断就无法判定包含关系。
/// @note 匿名 ns 内部辅助（非 Window 成员）：不读 Window 任何状态，纯粹是「子树包含」判据。
bool IsWithinSubtree(const Widget* widget, const Widget* subtreeRoot) noexcept{

	for (const Widget* current = widget; current != nullptr; current = current->GetParent()){

		if (current == subtreeRoot){

			return true;

		}

	}

	return false;

}

}

void Window::ForgetSubtree(const Widget* subtreeRoot) noexcept{

	if (subtreeRoot == nullptr){

		return;

	}

	// ① 焦点登记：落在被摘子树内 ⇒ 置空。
	//    ★ **不补发** `OnFocusLost`——脱树是异常失效（头文件契约段已写明理由）
	if (m_focusedWidget != nullptr && IsWithinSubtree(m_focusedWidget, subtreeRoot)){

		m_focusedWidget = nullptr;

	}

	// ② 捕获登记：同上（**不补发** Up——置空后 Up 派发自然退化为"按命中测试找目标"，
	//    语义合理且无回调副作用）
	if (m_captureWidget != nullptr && IsWithinSubtree(m_captureWidget, subtreeRoot)){

		m_captureWidget = nullptr;

	}

	// ③ hover 登记：委托 HoverTracker（契约 C 同款语义——不补发 Leave）
	m_hoverTracker.ForgetSubtree(subtreeRoot);

}

bool Window::IsWidgetInTree(const Widget* widget) const noexcept{

	if (widget == nullptr || m_rootWidget == nullptr){

		return false;

	}

	// 沿父链上溯：到达 RootWidget 即「仍在树内」。
	// ⚠️ 本方法**不防悬垂指针**（上溯即 UAF）——只判「已脱树但**仍存活**」，
	//    悬垂由 `ForgetSubtree` 在摘树时消除（头文件已写明这条边界）。
	for (const Widget* current = widget; current != nullptr; current = current->GetParent()){

		if (current == m_rootWidget.get()){

			return true;

		}

	}

	return false;

}

void Window::SetFocusedWidget(Widget* widget){

	// 5.4.3：同控件短路——避免重复设置触发 Lost+Gained 空转
	if (m_focusedWidget == widget){

		return;

	}

	// ★ 审计 #59 / §7.3：**不得把「属于本窗口」建立在断言上**。
	//   原实现只有 `FRAMEWORK_ASSERT(current == &GetRootWidget())`——Release 下断言为空
	//   ⇒ 一个**已脱树**的 widget 会被静默接受为焦点，随即在下一个按键上被解引用
	//   （正是 H6 的 UAF 路径①的 Release 侧入口）。
	//   修法用**保留运行时分支**的既有纪律（同 `Application.cpp:176-178`）：
	//   不属于本树 ⇒ 视为「清除焦点」（与传 nullptr 等价），并保留断言供 Debug 暴露缺陷。
	if (widget != nullptr && !IsWidgetInTree(widget)){

		FRAMEWORK_ASSERT(false);   // Debug：暴露"给本窗口设了外部控件"这一框架级误用

		widget = nullptr;          // Release：收敛为清除焦点，不解引用可疑指针
	}

	// 5.4.3：通知旧焦点失去（数据变更前）
	if (m_focusedWidget){

		m_focusedWidget->OnFocusLost();

	}

	m_focusedWidget = widget;

	if (widget != nullptr){

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

		// ★ 审计 H7（#65）：键盘派发前查 IsEnabled——鼠标通道的命中测试**已**过滤禁用
		//   （`Widget::HitTest:124-128`），键盘通道原先不查 ⇒ 一个 `SetEnabled(false)` 的
		//   控件仍响应键盘（用户可见的错误行为：按钮看着是灰的、按回车却触发）。
		//   ⚠️ 只查 IsEnabled、不查 IsVisible：能成为焦点说明收集时可见（CollectFocusables
		//      已过滤），运行期被隐藏是「焦点悬空」问题（属 §7.1 失效机制线，不在本条）。
		if (m_focusedWidget->IsEnabled()){

			// ★★ Phase 35：键盘是**单收件人**派发（不冒泡）——但要防「回调里删掉自己」：
			//   这里先取一份**局部副本**，回调里 `RequestRemove()` 摘树/清焦点都只改成员，
			//   不会把局部副本变成悬垂（对象已进 Application 的延迟队列，派发结束才析构）。
			//   ⚠️ 为什么不经 Application 的钉住区：窗口的焦点是控件回调的**唯一**入口，
			//      而 `m_focusedWidget->OnKeyDown` 之后这里**不再访问** `m_focusedWidget`
			//      ——局部副本已足够，无需把 Application 拖进 Window 的派发路径。
			Widget* target = m_focusedWidget;

			target->OnKeyDown(event);

		}

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
		// 7.1.3：GetCaretClientGeometry（CaretGeometry——改名 + 参数升级）
		UpdateTextInputCaret(textBox->GetCaretClientGeometry());

	}

}

void Window::NotifyIMECompositionUpdate(const std::string& compositionText){

	// 8.5.1：转发焦点 TextBox——更新组合状态（模型 B：覆盖 m_text 临时区间）
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){

		textBox->UpdateComposition(compositionText);

	}

}

void Window::NotifyIMECompositionCommit(const std::string& resultText){

	// 8.5.1：转发焦点 TextBox——组合区间转正式文本（C7：GCS_RESULTSTR 唯一来源）
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){

		textBox->CommitComposition(resultText);

	}

}

void Window::UpdateTextInputCaret(const CaretGeometry& geometry){

	// 7.1.1 薄转发：平台实现（SetCaretPos + ImmSetCompositionWindow 双通道）在 Win32PlatformWindow
	if (m_platformWindow){

		m_platformWindow->UpdateTextInputCaret(geometry);

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

	// 窗口大小变化时，同步 RootWidget 尺寸 + 重排布局链 + 请求重绘（9.7 触发链）
	if (m_rootWidget){

		m_rootWidget->SetSize(width, height);
		m_rootWidget->Arrange();   // 9.7：渐进语义——有 Layout 的节点重排 / 无 Layout 的保持手摆
		Invalidate();              // 9.7：重排后请求重绘（Window 层负责；Arrange 纯布局不 Invalidate）

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

void Window::OnEvent(const Event& event){

	// 事件转发（框架内，7.1.5 注释转正）：Application 是事件最终入口（EventRouter 基类）——
	// 平台事件 → Host::OnEvent → Window → Application 分发链（HitTest/Bubbling/焦点）
	m_application.OnEvent(event);

}

void Window::OnIMEComposition(){

	// 7.1.2 方案 B：平台层状态同步区上报 → 转发既有框架逻辑（候选窗定位）
	NotifyIMEComposition();

}

void Window::OnIMECompositionUpdate(const std::string& compositionText){

	// 8.5.1：Host 契约——平台层 GCS_COMPSTR 上报 → 转发焦点控件（组合串更新）
	NotifyIMECompositionUpdate(compositionText);

}

void Window::OnIMECompositionCommit(const std::string& resultText){

	// 8.5.1：Host 契约——平台层 GCS_RESULTSTR 上报 → 转发焦点控件（组合提交）
	NotifyIMECompositionCommit(resultText);

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
