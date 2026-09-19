#include "Platform/Win32/Win32PlatformWindow.h"

#include "Platform/Win32/Win32WindowClass.h"
#include "ECDI/Core/Logger.h"          // Phase 12：chrome/层级/状态契约的 Warning 日志
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"   // Phase 12 R7：WM_SIZE 状态事件
#include "ECDI/EventSystem/Window/DropFilesEvent.h"            // Phase 14 R9：拖入事件

#include <Windows.h>
#include <dwmapi.h>                    // Phase 12 R6：DwmExtendFrameIntoClientArea / DwmSetWindowAttribute
#include <imm.h>
#include <shellapi.h>                  // Phase 14：DragAcceptFiles / DragQueryFileW / DragQueryPoint / DragFinish
#include <windowsx.h>                  // Phase 12 R3：GET_X_LPARAM / GET_Y_LPARAM（NCHITTEST 屏幕坐标提取）

#ifdef DrawText
#undef DrawText   // Win32 宏防护（dwmapi.h / windowsx.h 展开链也可能带入 Windows.h）
#endif

#include <cstring>
#include <cwchar>                      // Phase 16：IsDesktopClassWindow 的 wcscmp（D-6——显式列全，不依赖 <Windows.h> 的展开链）
#include <string>
#include <system_error>
#include <unordered_map>               // Phase 16：hook → 实例 反查表（仅 .cpp——不进任何头）
#include <vector>

// ── Phase 12 D-DWM-1（实现期修正）：Win11 圆角常量**无需兜底** ──────────
// 实测取证：MinGW-w64 的 <dwmapi.h> 把 DWMWA_WINDOW_CORNER_PREFERENCE 定义为
//   DWMWINDOWATTRIBUTE 枚举**成员**（= 33），DWM_WINDOW_CORNER_PREFERENCE 与
//   DWMWCP_* 亦为枚举定义——**它们都不是宏**，故 `#ifndef <宏>` 守卫恒真、必与既有
//   定义冲突（详设 D-DWM-1 原「#ifndef 兜底」的前提不成立）。MSVC SDK 同样为枚举。
// ⇒ 直接用头里的定义，零兜底代码。

namespace ECDI{

namespace{   // 匿名 namespace：Win32PlatformWindow 内部辅助（不暴露）

/// @brief 剪贴板打开守卫（8.5.1 C10：OpenClipboard/CloseClipboard 资源配对——局部 RAII）
/// @details 仅封装资源配对，不做任何业务逻辑（YAGNI——非 ClipboardManager）；
/// 失败（其他应用占用）时 IsOpen() 为 false，调用方安全跳过。
class ClipboardGuard{
public:
	explicit ClipboardGuard(HWND hwnd): m_opened(OpenClipboard(hwnd) != FALSE){}
	~ClipboardGuard(){ if (m_opened) CloseClipboard(); }
	ClipboardGuard(const ClipboardGuard&) = delete;
	ClipboardGuard& operator=(const ClipboardGuard&) = delete;
	bool IsOpen() const noexcept{ return m_opened; }
private:
	bool m_opened;
};

/// @brief hook → 实例 映射（Phase 16 D5：每窗口一个钩子 ⇒ 回调必须能反查 owner）
/// @details `SetWinEventHook` 的回调签名固定且**无 user-data 参数**，而框架须支持多窗口
/// ⇒ 以**回调首参（hook 自身）**为键反查实例。**替代方案（进程级窗口列表 + 广播）已否决**：
/// 那要求回调遍历所有窗口并各自判断「是否该动」，把「谁的维护」变成全局语义。
/// ⚠️ **线程安全**：`WINEVENT_OUTOFCONTEXT` 的回调在**注册钩子的那个线程**的消息循环中执行
/// ⇒ 与 Install / Uninstall 天然同线程 ⇒ **无需加锁**（契约 C8）。
/// ⚠️ 函数局部 `static` 的析构发生在进程退出——此时任何仍存活的窗口自身即属泄漏，无实际风险。
std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*>& HookOwners(){

	static std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*> owners;

	return owners;

}

/// @brief Phase 16 A5 修复：桌面跟随的**延后一拍**消息（仅本 TU 使用——不进公共头）
/// @details 前台事件到达时 z 序尚未 settle ⇒ 当场判 `IsDirectlyAboveDesktop()` 会误读
/// 「已在位」而跳过重插（A5 实测）。改为投递到自身消息队列，
/// 下一轮消息循环再执行，判据即读到稳定态。
/// ⚠️ 取 `WM_APP + 2`——`WM_APP + 1` 已被 Win32PlatformApplication.cpp 的托盘回调
/// `kTrayCallbackMessage` 占用（虽属不同窗口，仍避开以免同号两义）。
constexpr UINT kDesktopFollowMsg = WM_APP + 2;

/// @brief Phase 16 A5 修复：桌面跟随的**重试定时器**（TimerId 保留段，登记见
/// `include/ECDI/Animation/AnimationManager.h` 的登记表与详设 §9.6 §7）
/// @details 桌面跟随必须等到**外壳真的抬起桌面之后**才有效——而那个时刻晚于本窗口的下一轮
/// 消息循环（A5 实测：一拍时 `GetWindow(Progman, GW_HWNDPREV)` 仍是 `IME`，说明桌面尚未抬升）。
/// ⇒ 用短延时定时器**固定多拍重插**：外壳动作的落点不可预知（本机实测第 2 拍即命中），
/// 固定次数给出确定性覆盖——最后一拍必然落在外壳抬桌面之后。
constexpr UINT_PTR kDesktopFollowTimerId = 3;   // 保留段：TextBox=1 / Animation=2 / Desktop=3
constexpr UINT kDesktopFollowRetryMs = 16;      // 与动画 tick 同量级（约一个 UI 帧）
constexpr int  kDesktopFollowMaxSteps = 4;      // 额外拍数上限（4×16ms ≈ 64ms；本机第 2 拍命中，留 2× 余量）

/// @brief 桌面类窗口判定（钩子过滤与 z 序判据共用同一份类名集合）
/// @details `Progman` = 桌面本体；`WorkerW` = 壁纸 / `SHELLDLL_DefView` 宿主（部分 Windows
/// 版本「显示桌面」抬升的是它而非 `Progman`）⇒ 两者都算「桌面层被抬升」。
/// ⚠️ 类名缓冲 32 宽字符足够（两名称最长 7），`GetClassNameW` 返回值 0 视为不匹配。
bool IsDesktopClassWindow(HWND hwnd){

	wchar_t buf[32]{};

	if (GetClassNameW(hwnd, buf, 32) == 0){ return false; }

	// 序数比较（`<cwchar>` 已在 include 段显式补入——D-6 / 详设 §2.4.1）。
	// ⚠️ 不用 lstrcmpW：那是 locale 敏感的 CompareString 语义，类名比较要的是序数。
	return wcscmp(buf, L"Progman") == 0 || wcscmp(buf, L"WorkerW") == 0;

}

}

Win32PlatformWindow::Win32PlatformWindow(PlatformWindowHost& host,
		const std::string& title, int width, int height)
	: m_host(host)
	, m_messageHandler(m_host){

	// 公共 API 为 UTF-8（std::string），在平台边界转换到 UTF-16（字符串边界划分）
	const std::wstring wideTitle = UTF8ToWide(title);

	// 7.1.5：窗口类注册下沉——窗口系统资源归窗口类自身（WindowClass::Instance()）
	const WindowClass& windowClass = WindowClass::Instance();

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

	// 7.1.4：hwnd 就绪后绑定渲染上下文（后端经 GetRenderContext() 拿句柄）
	m_renderContext.SetHandle(m_hwnd);

}

Win32PlatformWindow::~Win32PlatformWindow(){

	Release();

}

void Win32PlatformWindow::Show() {

	if (m_hwnd != nullptr) {

		m_shown = true;   // 配置期 → 运行期分界线（与 Window::Show() 一一对应）

		ShowWindow(m_hwnd, SW_SHOW);

		UpdateWindow(m_hwnd);

	}

}

void Win32PlatformWindow::Hide() {

	// Phase 14 R12：与 Show() 对称——但 m_shown 是「配置期/运行期」标记，不因隐藏回退
	if (m_hwnd != nullptr) {

		ShowWindow(m_hwnd, SW_HIDE);

	}

}

void Win32PlatformWindow::SetFileDropEnabled(bool enabled) {

	// Phase 14 R8：运行期契约（与 Minimize 同组——O-3 拍板）
	if (!m_shown) {

		Logger::Log(LogLevel::Warning,
			L"FileDrop: SetFileDropEnabled ignored before Show() - runtime API");

		return;

	}

	if (m_hwnd != nullptr) {

		DragAcceptFiles(m_hwnd, enabled ? TRUE : FALSE);   // 幂等（shellapi）

	}

}

void Win32PlatformWindow::DragFinishAdapter(HDROP hDrop) {

	DragFinish(hDrop);

}

void Win32PlatformWindow::HandleDropFiles(HDROP hDrop) {

	// ① 路径列表（UTF-8——边界转换在本层，R9）
	const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

	std::vector<std::string> paths;

	paths.reserve(count);

	for (UINT i = 0; i < count; ++i) {

		const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);   // 不含终止符

		std::wstring buf(len + 1, L'\0');

		DragQueryFileW(hDrop, i, buf.data(), static_cast<UINT>(buf.size()));

		buf.resize(len);

		paths.push_back(WideToUTF8(buf));

	}

	// ② 落点（客户区坐标——DragQueryPoint 既有语义，与鼠标事件同系）
	POINT pt{};

	DragQueryPoint(hDrop, &pt);

	// ③ ★ DragFinish 先于事件抛出（R10）——经测试缝（v1.1 拍板：函数指针）
	m_dragFinish(hDrop);

	// ④ 抛 Framework Event（O-6：Window* 经既有 GetWindow——PlatformWindowHost.h:32-34）
	DropFilesEvent event(m_host.GetWindow(), std::move(paths), pt.x, pt.y);

	m_host.OnEvent(event);   // → Window::OnEvent → Application::OnEvent → HitTest → Bubbling

}

bool Win32PlatformWindow::Release() noexcept {

	// ★ Phase 16（契约 C12）：**脱钩必须在 hwnd 判空之前** —— 旧实现
	//   `if (m_hwnd==nullptr) return true;` 会让「hwnd 已空、钩子仍在」的路径
	//   **跳过全部清理** ⇒ 回调打到已析构对象（UB）。**钩子与 hwnd 是相互独立的资源**，
	//   不可用同一个判空条件代表两者（与 Phase B 的 Window 所有权教训同族）。
	SyncDesktopHookOff();

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

	// ── Phase 14 R9：文件拖入（窗口级——DragAcceptFiles 启用后才收得到）──
	case WM_DROPFILES:

		HandleDropFiles(reinterpret_cast<HDROP>(wParam));

		return 0;

	// ── Phase 12 R2：无边框客户区（必须先于其它 NC 相关处理）────────────
	case WM_NCCALCSIZE:

		// 仅 Borderless 且 wParam == TRUE（客户区矩形需重算）时拦截。
		// wParam == FALSE 时 lParam 是 RECT 而非 NCCALCSIZE_PARAMS——不能解释（必须走 DefWindowProc）。
		if (m_chromeMode == ChromeMode::Borderless && wParam == TRUE){

			// 非最大化：客户区 = 整窗 → 返回 0 且不改 rect（系统按 rect 直接采用）
			if (!IsZoomed(hwnd)){

				return 0;

			}

			// 最大化：系统会把窗口外扩一圈（边框 + 阴影），客户区若原样采用会盖住任务栏。
			// 按 rcWork 校正（R4——见 AdjustMaximizedClientRect）
			AdjustMaximizedClientRect(hwnd,
				reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]);

			return 0;

		}

		break;   // Normal 或 wParam == FALSE → DefWindowProc

	// ── Phase 12 R3：命中测试（自定义九宫格 + IsZoomed 门控）──────────
	case WM_NCHITTEST: {

		// Normal 模式不干预（系统标题栏/边框行为完全不变——零回归底线）
		if (m_chromeMode != ChromeMode::Borderless){

			break;

		}

		// 屏幕坐标 → 窗口坐标（GET_X_LPARAM 带符号提取——多显示器负坐标安全）
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		RECT rcWin{};

		GetWindowRect(hwnd, &rcWin);

		const int x = pt.x - rcWin.left;

		const int y = pt.y - rcWin.top;

		// DIP → 物理像素（D-DPI-1：换算点唯一在此——公共 API 语义恒为 DIP）
		const int inset = DipToPixels(m_resizeInset, hwnd);

		const int caption = DipToPixels(m_captionHeight, hwnd);

		const int w = rcWin.right - rcWin.left;

		const int h = rcWin.bottom - rcWin.top;

		// 最大化态：系统不会进入 resize 循环，返回 HTLEFT/HTTOP 等会让人误以为可拖宽——
		// 故四边四角 resize 判据整体跳过（D-HIT-1）。
		// ⚠️ caption 判据不走这条门——最大化时仍允许从顶部往下拖还原（系统行为）。
		const bool resizable = !IsZoomed(hwnd);

		// 四角优先（角命中优先级高于边——否则角落会被边的判定吃掉）
		const bool left = resizable && x < inset;

		const bool right = resizable && x >= w - inset;

		const bool top = resizable && y < inset;

		const bool bottom = resizable && y >= h - inset;

		if (top && left)     return HTTOPLEFT;

		if (top && right)    return HTTOPRIGHT;

		if (bottom && left)  return HTBOTTOMLEFT;

		if (bottom && right) return HTBOTTOMRIGHT;

		if (left)   return HTLEFT;

		if (right)  return HTRIGHT;

		if (top)    return HTTOP;

		if (bottom) return HTBOTTOM;

		// 标题栏区 → HTCAPTION（拖动移动 / 双击最大化 / Aero Snap 系统免费获得）
		// ⚠️ 判定顺序：先 resize 边（上一条），后 caption——保证窗口最上缘是缩放手感
		if (y < caption && caption > 0){

			// ★ Phase 13 R2：caption 区内**先问 Host**——命中「消费鼠标输入的控件」（如 CaptionBar 按钮）
			//   则让本次命中走客户区（break → DefWindowProc → HTCLIENT，与「非 caption 区」同一出口），
			//   鼠标事件正常派发给控件；否则维持 HTCAPTION（拖拽 / 双击最大化 / 系统菜单）。
			//   委托范围**仅限本分支**（非全窗口每点）——命中测试高频调用，成本与既有判定同级。
			if (m_host.IsClientInteractiveAt(x, y)){

				break;

			}

			return HTCAPTION;

		}

		break;   // 客户区 → DefWindowProc（返回 HTCLIENT，交框架派发鼠标事件）

	}

	// ── Phase 12 R8：激活切换防闪烁（Borderless）+ 最小化态放行 ─────────
	case WM_NCACTIVATE: {

		if (m_chromeMode != ChromeMode::Borderless){

			break;   // Normal：系统标题栏行为完全不变

		}

		// 最小化态按 Win32 语义放行 DefWindowProc——minimized 窗口对该消息有特殊
		// 处理路径，lParam = -1 抑制重绘的模式不覆盖该状态。
		if (IsIconic(hwnd)){

			break;

		}

		// 防闪烁标准做法：以 lParam = -1 调 DefWindowProcW 表示「不重绘非客户区」，
		// 但保留其返回值的语义（激活状态变更的内部处理照常执行）。
		// ⚠️ 返回值必须原样透传——不能返回固定 TRUE（会破坏系统对激活链的维护）。
		return DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

	}

	// ── Phase 12 R10 / Phase 16 D8：持续维护层级档位对应的 z 序位置 ─────────
	case WM_WINDOWPOSCHANGING: {

		// ⚠️ 必须是「持续维护」而非一次性 SetWindowPos（其他程序会把我们顶下来）；
		// ⚠️ 只改 hwndInsertAfter，不碰 x/y/cx/cy/flags（否则会干扰最大化/还原几何）；
		// ⚠️ 契约边界：不承诺阻止第三方 SetWindowPos 造成的瞬时 z 序变化（Windows z 序是动态的）
		// ★ Phase 16（D8）：判据由「!= Normal ⇒ 一律 HWND_BOTTOM」改为**按档位分流**——
		//   Normal 不修改 / Bottom 保持既有语义（逐位等价）/ Desktop 贴桌面窗口正上方。
		auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);

		if ((wp->flags & SWP_NOZORDER) == 0){

			const HWND target = TargetInsertAfter();

			// ★ K8 / C2：target == nullptr 表示**本次不修改**（Normal 档 / 桌面句柄无效 /
			//   桌面已处 z 序最顶）——**绝不可**把它交给 SetWindowPos（那会落到
			//   HWND_BOTTOM，把 Desktop 档意外降级成 Bottom）。保持原值是唯一安全选择。
			if (target != nullptr){

				wp->hwndInsertAfter = target;

			}

		}

		break;   // 走 DefWindowProc（几何变更仍由系统处理）

	}

	// ── Phase 16 A5：桌面跟随（**延后一拍 + 有界重试**——见 OnForegroundChanged）──
	case kDesktopFollowMsg:

		FollowDesktopStep(hwnd);

		return 0;

	// ── Phase 16 A5：桌面跟随**重试定时器**（只认自己的 id；其余 id 走翻译器，既有行为不变）──
	case WM_TIMER:

		if (wParam == kDesktopFollowTimerId){

			FollowDesktopStep(hwnd);

			return 0;

		}

		break;   // 其它定时器（动画 tick / 光标闪烁）→ 翻译器 → TimerEvent（零回归）

	case WM_PAINT:
		// 决策 39：绘制不走翻译器（不是 Event），经 Host 回调编排整帧
		m_host.OnPaint();
		return 0;

	case WM_DESTROY:
		m_hwnd = nullptr;   // 句柄失效（框架层无需动作——YAGNI，无 OnDestroyed 回调）
		break;

	case WM_SIZE: {
		// 窗口大小变化 → Host 回调同步 RootWidget 尺寸；随后 fall-through 翻译器（WindowResizedEvent）
		m_host.OnResized(LOWORD(lParam), HIWORD(lParam));

		// Phase 12 R7：窗口状态变化 → 事件（尺寸同步已在 OnResized 完成——顺序契约：
		// 消费者收到事件时 RootWidget 已是新尺寸）。
		// ⚠️ 判定来源是 IsIconic/IsZoomed（系统真实状态），不是我们调了哪个 API——
		// 鼠标拖拽最大化 / Win+↑ / Aero Snap / 双击标题栏等非 API 路径同样产生事件。
		WindowState state = WindowState::restored;

		if (IsIconic(hwnd)){

			state = WindowState::minimized;

		}
		else if (IsZoomed(hwnd)){

			state = WindowState::maximized;

		}

		// 去重：仅状态真正变化时派发（WM_SIZE 在拖拽缩放时高频到达——
		// 不去重会以同一状态淹没消费者；连续两次 Maximize 只产生 1 个事件）
		if (state != m_lastWindowState){

			m_lastWindowState = state;

			m_host.OnEvent(WindowStateChangedEvent(m_host.GetWindow(), state));

		}

		break;   // 既有行为：继续走翻译器（WindowResizedEvent——零回归）
	}

	case WM_EXITSIZEMOVE:
		// 7.1.1 职责：窗口移动/缩放结束 → 通知 Host（框架层决定如何响应）。
		// 背景（5.6 实测）：TSF 输入法组合中已显示候选窗时缓存位置，不重新查询系统 caret——
		// 框架层 OnExitSizeMove 会销毁+重建 caret 强制 TSF 缓存失效（候选窗归位）。
		// 平台层只报告"移动结束了"，不碰 caret/IME 细节。
		m_host.OnExitSizeMove();
		return 0;

	case WM_IME_STARTCOMPOSITION:
		// 8.5.1 内嵌模式：阻止系统创建/显示默认组合窗（return 0 不调 DefWindowProc）——
		// 组合串由 TextBox 自绘（模型 B：m_text 含组合串 + 下划线），系统组合窗若显示会叠加重复。
		// 5.6 时代"IME 消息必须走 DefWindowProc"论证基于无内嵌需求；内嵌后 START 阻止组合窗
		// 是 Windows 标准模式（Notepad 等文本编辑器同款——组合数据仍经 ImmGetCompositionString 可读，
		// 不破坏 IME 状态机）；候选窗定位不受影响（走 ② 通道/系统 caret）。
		// 风险（实测验证）：若输入法组合 UI 整体依赖组合窗（TSF 独立 UI），候选窗可能受影响——
		// 回退 = 恢复 break 走 DefWindowProc。
		m_host.OnIMEComposition();   // → Window::NotifyIMEComposition（候选窗定位）
		return 0;   // ⚠️ 内嵌模式关键：阻止默认组合窗显示

	case WM_IME_COMPOSITION:
		// 7.1.2 方案 B（GPT 三轮）：IME 属输入法子系统（TSF/IMM/候选窗/系统 caret——
		// 独立状态机，非事件系统成员），平台层状态同步区上报，不再经翻译器
		// （翻译器职责纯粹化：Translate → Event → Host）
		m_host.OnIMEComposition();   // → Window::NotifyIMEComposition（候选窗定位）
		// 8.5.1：组合串内容上报（C7 契约——GCS_COMPSTR=Update，GCS_RESULTSTR=Commit）
		if (lParam & GCS_COMPSTR){
			// ① 组合串更新（正在组合的内容——临时编辑，不触发正式编辑语义）
			if (HIMC imc = ImmGetContext(m_hwnd)){
				// ⚠️ ImmGetCompositionStringW 返回 LONG（字节数）非 DWORD——负值 = 失败/无数据
				const LONG len = ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
				if (len > 0){
					std::wstring composition(static_cast<size_t>(len) / sizeof(wchar_t), L'\0');
					ImmGetCompositionStringW(imc, GCS_COMPSTR,
						composition.data(), static_cast<DWORD>(len));
					m_host.OnIMECompositionUpdate(WideToUTF8(composition));
				}
				else{
					m_host.OnIMECompositionUpdate({});   // 组合串清空（组合仍在——非 Commit）
				}
				ImmReleaseContext(m_hwnd, imc);
			}
		}
		if (lParam & GCS_RESULTSTR){
			// ② 组合提交（最终结果——Commit 的唯一可靠来源，C7）
			if (HIMC imc = ImmGetContext(m_hwnd)){
				const LONG len = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
				if (len > 0){
					std::wstring result(static_cast<size_t>(len) / sizeof(wchar_t), L'\0');
					ImmGetCompositionStringW(imc, GCS_RESULTSTR,
						result.data(), static_cast<DWORD>(len));
					m_host.OnIMECompositionCommit(WideToUTF8(result));
					// 8.5.1 双写修复：结果已经 GCS_RESULTSTR 提交给框架（TextBox 已写入 m_text），
					// 但系统随后仍会发结果 WM_CHAR 序列（DefWindowProc 通道）——
					// 记下待吞计数（UTF-16 码元数 = WM_CHAR 消息数），HandleMessage 前置拦截。
					m_imeResultPendingChars = static_cast<int>(result.size());
				}
				else{
					m_host.OnIMECompositionCommit({});   // 空结果 Commit（合法——C12）
				}
				ImmReleaseContext(m_hwnd, imc);
			}
		}
		break;   // 走翻译器（无 WM_IME case）→ nullopt → DefWindowProcW（IME 内部状态机必需）

	case WM_IME_ENDCOMPOSITION:
		// 8.5.1：组合结束（含取消/ESC）——无 GCS_RESULTSTR 时占位拼音会残留 m_text。
		// 用 Commit("")（空结果提交）统一收尾：正常路径（已 Commit）→ no-op 安全；
		// 取消路径（未 Commit）→ 擦除占位 + 清组合标记（TextBox::CommitComposition 语义闭合）。
		m_host.OnIMECompositionCommit({});
		break;   // 继续 DefWindowProcW（IME 内部状态机必需）

	}

	// 8.5.1 双写修复：IME 结果 WM_CHAR 拦截（GCS_RESULTSTR 已提交框架——系统随后重复发送
	// 结果字符 WM_CHAR，若放行会经 CharInputEvent → InsertCodepoint 二次插入，产生"你好你好"）
	if (msg == WM_CHAR && m_imeResultPendingChars > 0){

		--m_imeResultPendingChars;

		return 0;   // 吞掉（不翻译——结果已由 CommitComposition 写入）

	}

	// 将 Win32 消息翻译为 Framework Event 并派发（翻译器纯翻译 + 经 Host 派发——7.1.2；
	// 结构 Translate→Event→Host 已定稿，翻译逻辑本体零改动）
	auto result = m_messageHandler.Handle(m_host.GetWindow(), hwnd, msg, wParam, lParam);

	if (result) {

		return *result;

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

const PlatformRenderContext& Win32PlatformWindow::GetRenderContext() const{

	// 7.1.4：返回渲染上下文（构造时已 SetHandle——后端 Initialize 经 static_cast 取 HWND）
	return m_renderContext;

}

void Win32PlatformWindow::UpdateTextInputCaret(const CaretGeometry& geometry){

	// 7.1.3：visible 判断在**平台表现层**（GPT：Window 不知 CreateCaret/HideCaret 细节）
	// visible=false → HideCaret（**存在 ≠ 可见**——caret 仍存在但不显示；
	// 区别于"销毁"（DestroyTextInputCaret）——失焦销毁 vs 存在隐藏是两种语义）
	if (!geometry.visible){

		HideCaret(m_hwnd);

		return;

	}

	// ① 系统 caret（TSF 输入法主路径——Win11 微软拼音查询 GetCaretPos 定位候选窗，最小实验已验证）
	// 懒创建：首次调用（TextBox 获焦）创建；后续只 SetCaretPos
	// 7.1.3：尺寸来自 rect（消灭硬编码 2x20——定位/绘制/输入同源，TextBox 输出完整几何）
	if (!m_caretCreated){

		CreateCaret(m_hwnd, nullptr,
			static_cast<int>(geometry.rect.width),
			static_cast<int>(geometry.rect.height));

		m_caretCreated = true;

	}

	SetCaretPos(static_cast<int>(geometry.rect.x), static_cast<int>(geometry.rect.y));   // 客户区坐标（caret 语义=左上角）

	// ⚠️ 保持 5.6 行为：始终 HideCaret（**不自画双光标**——系统 caret 仅作 TSF 位置信标，
	// 光标竖线由控件 OnPaint 自画）。visible=true 不做 ShowCaret（GPT 三轮认同——分歧消解）。
	HideCaret(m_hwnd);

	// ② IMM 通道（GPT 双保险——兼容性最广；8.5.1 拆分：组合窗口隐藏 + 候选窗口跟随）
	// ⚠️ 坐标系修正（2026-08-15 用户洞察）：实测微软拼音（TSF）把 ptCurrentPos 当**客户区坐标**解释！
	// 证据：最小实验 SetCaretPos(300,200) 时候选框出现在窗口内 (300,200)（客户区）而非屏幕 (300,200)。
	// 若按 IMM 文档"屏幕坐标"传 ClientToScreen 后的值，候选框落在窗口内"屏幕坐标值"处（远离光标），
	// 窗口移动时还叠加窗口偏移（"像素过多"）。故**不再 ClientToScreen，直接传客户区坐标**。
	POINT pt{

		static_cast<LONG>(geometry.rect.x),

		static_cast<LONG>(geometry.rect.y)

	};

	if (HIMC imc = ImmGetContext(m_hwnd)){

		// ②a 组合窗口（ImmSetCompositionWindow）：**CFS_POINT 钉光标**——组合窗显示已由
		// WM_IME_STARTCOMPOSITION return 0 阻止（内嵌模式），此处位置设置仅作候选窗锚点参考
		// （微软拼音把 ptCurrentPos 当客户区坐标解释——5.6 v1.0.4 用户洞察；候选窗参考此位置）。
		// ⚠️ 不能用 CFS_RECT（实测微软拼音忽略，组合串回原生层）；不能移出屏幕（候选窗跟着飘走）。
		COMPOSITIONFORM cfComposition{};

		cfComposition.dwStyle = CFS_POINT;

		cfComposition.ptCurrentPos = pt;

		ImmSetCompositionWindow(imc, &cfComposition);

		// ②b 候选窗口（ImmSetCandidateWindow）：**跟随光标**——IMM 老输入法候选窗保底；
		// TSF（Win11 微软拼音）候选窗走 ②a 组合窗锚点 + 系统 caret（①），本通道兼容 IMM 输入法。
		CANDIDATEFORM cfCandidate{};

		cfCandidate.dwStyle = CFS_POINT;

		cfCandidate.ptCurrentPos = pt;

		ImmSetCandidateWindow(imc, &cfCandidate);

		ImmReleaseContext(m_hwnd, imc);

	}

}

void Win32PlatformWindow::DestroyTextInputCaret(){

	if (m_caretCreated){

		DestroyCaret();

		m_caretCreated = false;

	}

}

std::string Win32PlatformWindow::GetClipboardText() const{

	// 8.5.1 C10：RAII 守卫——任何路径自动 CloseClipboard；失败（占用）→ 空串（下个机会重试）
	ClipboardGuard guard(m_hwnd);

	if (!guard.IsOpen())

		return {};

	HANDLE hData = GetClipboardData(CF_UNICODETEXT);

	if (hData == nullptr)

		return {};

	const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(hData));

	if (wide == nullptr)

		return {};

	// 公共 API UTF-8——Win32 边界 WideToUTF8 转换（字符串边界划分，skill 11）
	const std::string utf8 = WideToUTF8(wide);

	GlobalUnlock(hData);

	return utf8;

}

void Win32PlatformWindow::SetClipboardText(const std::string& text){

	ClipboardGuard guard(m_hwnd);

	if (!guard.IsOpen())

		return;

	EmptyClipboard();   // 标准流程：先清空再设置

	const std::wstring wide = UTF8ToWide(text);

	// 含终止符（剪贴板以 null 结尾的 wchar 串）
	const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);

	HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE, bytes);

	if (hData == nullptr)

		return;

	void* dest = GlobalLock(hData);

	if (dest == nullptr){

		GlobalFree(hData);   // 锁定失败 → 释放（未被剪贴板接管）

		return;

	}

	memcpy(dest, wide.c_str(), bytes);

	GlobalUnlock(hData);

	// ⚠️ C10 契约：SetClipboardData 失败必须释放 hData（成功才由剪贴板接管）
	if (SetClipboardData(CF_UNICODETEXT, hData) == nullptr)

		GlobalFree(hData);

}

void Win32PlatformWindow::StartTimer(int timerId, unsigned int intervalMs){

	// 8.5.1：通用定时器（C2——平台不知道 timerId 的业务语义；Win32 定时器 ID 即 wParam）
	if (m_hwnd)

		SetTimer(m_hwnd, timerId, intervalMs, nullptr);

}

void Win32PlatformWindow::StopTimer(int timerId){

	// 幂等：KillTimer 对不存在/已停止的定时器返回 FALSE（无害）
	if (m_hwnd)

		KillTimer(m_hwnd, timerId);

}

// ══════════════════════════════════════════════════════════════════
// Phase 12 WindowChrome / 层级 / 状态
// ══════════════════════════════════════════════════════════════════

void Win32PlatformWindow::SetChromeMode(ChromeMode mode){

	// 配置期契约（与其它 chrome 配置同判据）。
	// 判据 m_shown：在 Show() 内置位——表达「框架 API 是否调用过 Show()」，
	// 而非「系统当前是否可见」（Show()+Hide() 后 IsWindowVisible 为假，但契约上已进运行期）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetChromeMode ignored after Show() - mode is config-time only");

		return;

	}

	// ★ D-CHROME-1：ChromeMode 一次确定——配置期内重复调用（无论同值异值）一律忽略。
	// 根因：Borderless→Normal 不派发 SWP_FRAMECHANGED ⇒ frame 与状态失同步，
	// 且「已应用」标记残留会把第三次 Borderless 调用挡在幂等分支外。
	// 采用「一次确定」而非「配置期动态重配置」：后者需要双向 frame 重算状态机——
	// 无消费者（YAGNI），且与「窗口形态由创建期一次决定」的配置期语义更一致。
	if (m_chromeConfigured){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetChromeMode ignored - chrome mode is decided once (config-time)");

		return;

	}

	m_chromeConfigured = true;

	m_chromeMode = mode;

	if (mode != ChromeMode::Borderless){

		return;   // Normal：无需任何处理（默认样式即 Normal——不走 SWP_FRAMECHANGED）

	}

	// HWND 生命周期前提：本框架窗口构造即建 HWND（构造体 CreateWindowExW，失败抛异常），
	// 故 Window 构造完成后 m_hwnd 恒非空。此处仍做防御，与 SetWindowLayer 对齐。
	if (m_hwnd == nullptr){

		return;

	}

	// Borderless 应用流程：
	// ① 样式本身不变（保留 WS_OVERLAPPEDWINDOW——技术路线核心）
	// ② 通知系统重算非客户区：SWP_FRAMECHANGED（必须在窗口显示前派发——否则闪一次边框）
	SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	// ③ DWM 增强（阴影/圆角——失败容忍）
	ApplyDwmEnhancements(m_hwnd);

}

void Win32PlatformWindow::SetCaptionHeight(int height){

	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetCaptionHeight ignored after Show() - chrome config is config-time only");

		return;

	}

	// 边界契约：「<= 0 视为 0」——允许应用完全放弃 HTCAPTION 拖动区
	m_captionHeight = height < 0 ? 0 : height;

}

void Win32PlatformWindow::SetResizeInset(int inset){

	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetResizeInset ignored after Show() - chrome config is config-time only");

		return;

	}

	m_resizeInset = inset < 0 ? 0 : inset;

}

void Win32PlatformWindow::SetWindowLayer(WindowLayer layer){

	// 配置期契约：与 chrome 三件套同一生命周期。
	// API 签名不因此锁死——未来若出现运行期切层需求，只需松开此判据（零签名变更）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetWindowLayer ignored after Show() - layer is config-time only");

		return;

	}

	if (m_windowLayer == layer){

		return;   // 幂等

	}

	m_windowLayer = layer;   // 语义状态恒记录用户请求（D-DESK-1：不随实现路径降级）

	// ── Phase 16 D1(A′) / D3：Desktop 档移除 WS_MINIMIZEBOX ────────────────
	// 真因（需求稿 §8 P0 实测）：「显示桌面」只最小化**可最小化**窗口 ⇒ 差异是**一位**，
	// 不是整个窗口形态 ⇒ 保留 WS_OVERLAPPEDWINDOW 的其余系统红利（Alt+Space /
	// Aero Snap / 最大化动画），仅让 Desktop 档放弃「可最小化」这一条。
	ApplyDesktopStyle(IsDesktopLayer());

	// ── Phase 16 D11：钩子生命周期（Set ⇒ 装 / 离档 ⇒ 卸；Hide / Show **不参与**）──
	SyncDesktopHook();

	if (IsDesktopLayer()){

		// K2 修正：原文案写 "spike pending"（该 spike 已于 2026-09-15 结项）——
		// 本阶段即为落地，不再降级执行。
		Logger::Log(LogLevel::Info,
			L"WindowChrome: WindowLayer::Desktop enabled - window is wedged above the desktop window");

	}

	if (m_hwnd == nullptr){

		return;   // 无窗口：仅记录状态（Show 后由 WM_WINDOWPOSCHANGING 自然生效）

	}

	// 切到 Bottom/Desktop：立即派发一次（后续由 WM_WINDOWPOSCHANGING 持续维护）；
	// 切回 Normal：target == nullptr ⇒ **不主动改变当前 z 序**（交系统自然演化——
	// 避免「突然跳到最前」的反直觉效果）。
	const HWND target = TargetInsertAfter();

	if (target != nullptr){

		SetWindowPos(m_hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}

// ══════════════════════════════════════════════════════════════════════════
// Phase 16：桌面驻留层（R1–R4）
// ══════════════════════════════════════════════════════════════════════════

HWND Win32PlatformWindow::ResolveTarget(WindowLayer layer, HWND desktop){

	// ── Normal 档：不参与 z 序维护 ⇒ 返回「跳过」哨兵 ──
	if (layer == WindowLayer::Normal){

		return nullptr;

	}

	// ── Bottom 档：既有语义（Phase 12 已实现）——逐位不变 ──
	if (layer == WindowLayer::Bottom){

		return HWND_BOTTOM;

	}

	// ── Desktop 档：紧贴桌面窗口正上方 ──
	if (desktop == nullptr || !IsWindow(desktop)){

		// ★ K8 / C2：桌面窗口不可用（explorer 重建窗口期 / 非交互式会话）⇒ **跳过本次修改**。
		//   绝不可 fallback 到 HWND_BOTTOM —— 那会把 Desktop 档意外降级成 Bottom，
		//   是「比不重插更糟」的位置（desktop_spike.cpp:592-594 已显式防御同一陷阱）。
		return nullptr;

	}

	// 桌面窗口的上一位 =「紧贴着它的那个位置」。
	// ⚠️ 返回 nullptr（桌面已在 z 序最顶）同样是**跳过** —— 此时「紧贴其上」不可能，
	//    而任何替代位置（HWND_BOTTOM / HWND_TOP）都违反契约。
	return GetWindow(desktop, GW_HWNDPREV);

}

HWND Win32PlatformWindow::TargetInsertAfter() const{

	// ⚠️ **只让 Desktop 档去查桌面句柄**——Normal / Bottom 不看它。
	//    这让 WM_WINDOWPOSCHANGING 在 **Bottom 档下不产生任何 GetShellWindow() 调用**
	//    （零新增成本：Bottom 是既有档位，不应因本阶段变慢 —— 契约 §3.4）。
	const HWND desktop = IsDesktopLayer() ? FindDesktopWindow() : nullptr;

	return ResolveTarget(m_windowLayer, desktop);

}

HWND Win32PlatformWindow::FindDesktopWindow(){

	// D7：实测 `GetShellWindow()` ≡ `FindWindowW(L"Progman")`（四组独立运行一致）⇒ 取
	// **官方 API**：语义直述（「shell 的桌面窗口」）、无按类名枚举的成本。
	// ⚠️ 返回值仍须 `IsWindow` 校验——explorer 重建窗口期可能短暂失效（K8）；
	//    调用方（ResolveTarget / IsDirectlyAboveDesktop）一律按「不可用 ⇒ 跳过」处理。
	// ★ D6：**不缓存** ⇒ 不需要「缓存 + 有效性检测 + 重定位」状态机——explorer 重建
	//   `Progman` 之后，下一次查询**天然**拿到新句柄。代价实测 0.8 次/秒（可忽略）。
	return GetShellWindow();

}

bool Win32PlatformWindow::IsDirectlyAboveDesktop() const{

	// ★ 契约 C11（三态）：true = 已在位 / false = 未在位 / **true = 不可判定**。
	//   不可判时返回 true 的含义是「**禁止无依据的 z-order 操作**」，
	//   **不是**「z 序已满足 C1」。
	if (m_hwnd == nullptr || !IsWindow(m_hwnd)){ return true; }   // 不可判 ⇒ 抑制动作

	const HWND desktop = FindDesktopWindow();

	if (desktop == nullptr || !IsWindow(desktop)){ return false; }   // 桌面不可用 ⇒ 未在位

	return GetWindow(desktop, GW_HWNDPREV) == m_hwnd;

}

void Win32PlatformWindow::ReinsertAboveDesktop(){

	if (!IsDesktopLayer() || m_hwnd == nullptr || !IsWindow(m_hwnd)){

		return;

	}

	// ★ 需求稿 §8.1②（本阶段**正式设计输入**）：先判在位，已在位则什么都不做。
	//   路线 E 的原始实现是**无条件重插**——实测那是 Win+D 瞬间「闪烁一下」的来源之一
	//   （用户目视确认）。在位检查把抖动降到最小，且零额外成本。
	// ⚠️ Phase 16 A5：本判据在**外壳抬桌面之前**也为真（实测）⇒ 它只表示「当前 z 序无需
	//   调整」，**不代表「已跟随完成」**（外壳随后才抬桌面；见 FollowDesktopStep）。
	if (IsDirectlyAboveDesktop()){

		return;

	}

	const HWND target = TargetInsertAfter();

	if (target != nullptr){

		SetWindowPos(m_hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}

void Win32PlatformWindow::FollowDesktopStep(HWND hwnd){

	// **每一拍都无条件重插，并再排下一拍**，直到用满 kDesktopFollowMaxSteps。
	// ⚠️ 为什么不能用「插上就停」：重插成功只证明「插了」，**不证明「插在外壳抬桌面之后」**——
	//   A5 实测：第一拍常在外壳动作**之前**就成功（目标为 `IME` / `Windows.UI.Core.CoreWindow`），
	//   随后外壳才把桌面抬到我们上面 ⇒ 被盖住；只有目标变成 `WinUIDesktopWin32WindowClass`
	//   的那一拍才真正落在外壳动作之后（本机为第 2 拍）。
	// ⚠️ 也不能用 `IsDirectlyAboveDesktop()` 当停止条件：外壳抬桌面**之前**它同样为真。
	ReinsertAboveDesktop();

	++m_desktopFollowRetries;

	if (m_desktopFollowRetries <= kDesktopFollowMaxSteps){

		SetTimer(hwnd, kDesktopFollowTimerId, kDesktopFollowRetryMs, nullptr);

		return;

	}

	// 用满即停（每轮 Win+D 都会从 0 重新计数——见 OnForegroundChanged）。
	m_desktopFollowRetries = 0;

	KillTimer(hwnd, kDesktopFollowTimerId);

	// 收尾仍未在位 ⇒ 拍数上限不够（正常不应出现；kDesktopFollowMaxSteps 需加大）。
	if (!IsDirectlyAboveDesktop()){

		Logger::Log(LogLevel::Warning, L"DesktopLayer: follow ended but NOT above desktop (raise max steps)");

	}

}

void Win32PlatformWindow::ApplyDesktopStyle(bool desktop){

	if (m_hwnd == nullptr){

		return;   // 无窗口无从改样式（构造期已创建 HWND，此处仅防御）

	}

	const LONG_PTR current = GetWindowLongPtrW(m_hwnd, GWL_STYLE);

	const LONG_PTR wanted = desktop
		? (current & ~static_cast<LONG_PTR>(WS_MINIMIZEBOX))
		: (current |  static_cast<LONG_PTR>(WS_MINIMIZEBOX));

	if (wanted == current){

		return;   // 幂等：已是目标值则不动手（避免多余的 WM_STYLECHANGING/CHANGED 往返）

	}

	// ⚠️ **不需要 SWP_FRAMECHANGED**（O1 已实测关闭，初设 §6.1）：只改样式位，
	//    非客户区几何逐位不变（v5a 三点量具 A = B = C = 0 × 0）。
	//    Borderless 档的 SetChromeMode 已在配置期派发过一次，那次重算发生在本调用
	//    **之前**，且不因改这一位失效 ⇒ 既不必要、也不应重复派发。
	SetWindowLongPtrW(m_hwnd, GWL_STYLE, wanted);

}

void Win32PlatformWindow::SyncDesktopHook(){

	const bool want = (IsDesktopLayer() && m_hwnd != nullptr);

	if (want && m_desktopHook == nullptr){

		// hInstance 可传 nullptr（D5 / K9）：WINEVENT_OUTOFCONTEXT 的回调在**本进程内**
		// 由系统在注册线程的消息循环中调用，不要求回调位于 DLL ⇒ 无需模块句柄。
		m_desktopHook = SetWinEventHook(
			EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
			nullptr, &Win32PlatformWindow::DesktopForegroundProc,
			0, 0, WINEVENT_OUTOFCONTEXT);

		if (m_desktopHook != nullptr){

			HookOwners()[m_desktopHook] = this;

			if (m_hookObserver != nullptr){ m_hookObserver(true); }

		}

	}
	else if (!want && m_desktopHook != nullptr){

		SyncDesktopHookOff();

	}

}

void Win32PlatformWindow::SyncDesktopHookOff(){

	if (m_desktopHook == nullptr){ return; }   // 幂等（Release 与析构会各调一次）

	// ★ 注销顺序（契约 C6 / C12）：**先从映射表移除，再 UnhookWinEvent** ——
	//   反序会让「回调已取出 owner、而对象正在析构」成为可能（UAF）。
	//   移除后即使回调窗口期内仍被触发，`HookOwners().find()` 也会返回 end() ⇒ 安全返回。
	HookOwners().erase(m_desktopHook);

	UnhookWinEvent(m_desktopHook);

	m_desktopHook = nullptr;

	if (m_hookObserver != nullptr){ m_hookObserver(false); }

}

void Win32PlatformWindow::OnForegroundChanged(HWND foreground){

	// 只有「桌面层被抬升」才需要跟随（Win+D / Show Desktop 的机制即此——spike §7.1）。
	if (!IsDesktopClassWindow(foreground)){

		return;

	}

	// ★ Phase 16 A5 修复：**延后一拍 + 固定 N 拍无条件重插**。
	//   为什么一拍不够：前台事件到达时 z 序尚未 settle，**且外壳抬桌面发生在其后**
	//   （A5 实测：一拍时目标仍是 `IME` / `Windows.UI.Core.CoreWindow`，说明桌面尚未抬升）
	//   ⇒ 单拍会插在「外壳动作之前」而落空。而「插上就停」同样不行——第一拍**可能真的插上**，
	//   只是插在外壳动作之前。⇒ 改为**固定 N 拍无条件重插**（见 FollowDesktopStep），
	//   最后一拍必然落在外壳抬桌面之后。
	m_desktopFollowRetries = 0;
	if (m_hwnd != nullptr){ PostMessageW(m_hwnd, kDesktopFollowMsg, 0, 0); }

}

void CALLBACK Win32PlatformWindow::DesktopForegroundProc(HWINEVENTHOOK hook, DWORD event,
	HWND hwnd, LONG idObject, LONG /*idChild*/, DWORD /*thread*/, DWORD /*time*/){

	// 三重过滤：事件类型 / 窗口句柄 / 对象粒度（只关心窗口级前台变更）
	if (event != EVENT_SYSTEM_FOREGROUND || hwnd == nullptr || idObject != OBJID_WINDOW){

		return;

	}

	const auto it = HookOwners().find(hook);

	if (it == HookOwners().end()){

		// 已卸除（回调与 UnhookWinEvent 之间的窗口期——见 SyncDesktopHookOff 的注销顺序）
		return;

	}

	it->second->OnForegroundChanged(hwnd);

}

void Win32PlatformWindow::Minimize(){

	// 运行期契约：与配置期 API 对称，Show() 前拒绝。
	// ⚠️ 不能靠「系统会忽略」成立契约——ShowWindow 本身就是显示状态操作。
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: Minimize ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);

}

void Win32PlatformWindow::Maximize(){

	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: Maximize ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_MAXIMIZE);

}

void Win32PlatformWindow::Restore(){

	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: Restore ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_RESTORE);

}

// ── Phase 13：窗口状态查询（R3）──────────────────────────────────

WindowState Win32PlatformWindow::GetWindowState() const noexcept{

	// 事实来源：WM_SIZE 时由 IsIconic / IsZoomed 判定并写入（Phase 12 既有去重锚）——
	// 本方法只读取缓存，不重新查询系统（避免与事件流判定不一致）。
	return m_lastWindowState;

}

void Win32PlatformWindow::AdjustMaximizedClientRect(HWND hwnd, RECT& rcClient){

	// R4 验收基准：客户区不覆盖任务栏、不残留系统边框空白。
	// ⚠️ 不能用 rcMonitor（含任务栏区，客户区会盖住任务栏）；必须用 rcWork。
	MONITORINFO mi{};

	mi.cbSize = sizeof(MONITORINFO);

	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)){

		return;   // 查询失败：保持系统原值（fail-safe——宁可留系统空白也不盖任务栏）

	}

	// 客户区 = 显示器工作区（屏幕坐标）。
	// 最大化时系统已把 rcWindow 撑到 rcWork 之外（按边框量外扩）；此处直接把客户区取为
	// rcWork，即得「可见范围 == 工作区」。无需补偿，也就不会出现方向性错误（D-COMP-1）。
	rcClient = mi.rcWork;

	// ⚠️ D-COMP-1：刻意不引入任何基于 (rcWindow - rcMonitor) 差值的补偿——最大化时该
	// 差值为负，「正负代入」的对称补偿会把客户区推出 rcWork。若 T4 在某 Windows 版本
	// 失败，另起 R 立项修订（补偿只能「正内缩」：left += ix / right -= ix，ix >= 0）。

}

int Win32PlatformWindow::DipToPixels(int dip, HWND hwnd){

	// D-DPI-1：DPI 来源 = GetDeviceCaps(LOGPIXELSX)（窗口 DC——非鼠标所在显示器）。
	// 理由：caption/inset 描述窗口自身 UI 的几何语义，跟随窗口比跟随鼠标更一致。
	// 公式：整数 half-up —— px = (dip * dpi + 48) / 96（dpi = 96 时恒等于 dip，无误差）。
	// 当前项目未声明 PerMonitorV2 → 系统 DPI 虚拟化下 dpi 恒 96 → 现状 1:1。
	// 未来启用 Per-Monitor V2 时仅替换 DPI 来源（GetDeviceCaps → GetDpiForWindow），
	// 不改变调用位置、参数语义与换算公式。
	// 溢出防护：dip 是公共 API 直收的 int，理论可传 INT_MAX——中间量升 long long。
	HDC hdc = GetDC(hwnd);

	if (hdc == nullptr){

		return dip;   // DC 获取失败：1:1 降级（与「DPI 未落地」现状一致——fail-safe）

	}

	const int dpi = GetDeviceCaps(hdc, LOGPIXELSX);

	ReleaseDC(hwnd, hdc);

	const long long scaled = static_cast<long long>(dip) * dpi;

	return static_cast<int>((scaled + 48) / 96);

}

void Win32PlatformWindow::ApplyDwmEnhancements(HWND hwnd){

	// R6 失败容忍契约：DWM 不可用/属性不被支持 → 仅日志 Warning，绝不中断。
	// 本函数整体属「Win32 实现细节」——数值参数不是公共 API 语义。
	// （老系统优雅降级：视觉层次缺失，功能完整）

	// ① 保留系统阴影/层次：minimal frame extension（MARGINS{1,1,1,1}——起步值；
	//    全 0 失去系统阴影，过大则玻璃延伸进客户区。真机标定观察项：
	//    [a] 阴影是否保留 [b] 客户区顶部是否出现玻璃条 [c] Win10 / Win11 各一遍）
	MARGINS margins{ 1, 1, 1, 1 };

	const HRESULT hrExtend = DwmExtendFrameIntoClientArea(hwnd, &margins);

	if (FAILED(hrExtend)){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: DwmExtendFrameIntoClientArea failed - DWM enhancements skipped");

	}

	// ② Win11 圆角：DWMWA_WINDOW_CORNER_PREFERENCE（build 22000+）
	//    ⚠️ Win10 及更早返回 E_INVALIDARG——被失败容忍吸收（预期路径，非异常）
	DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;

	const HRESULT hrCorner = DwmSetWindowAttribute(hwnd,
		DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

	if (FAILED(hrCorner)){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: corner preference unsupported - rounded corners skipped");

	}

}

}
