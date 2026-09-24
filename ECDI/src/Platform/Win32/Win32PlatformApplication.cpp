#include "Platform/Win32/Win32PlatformApplication.h"

#include "Platform/Win32/Win32WindowClass.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"

#include <Windows.h>
#include <shellapi.h>                  // Phase 14：Shell_NotifyIconW / TrackPopupMenu
#include <windowsx.h>                  // Phase 14：GET_X_LPARAM / GET_Y_LPARAM（v4 锚点提取）

#ifdef DrawText
#undef DrawText   // Win32 宏防护（条 10）
#endif

namespace ECDI{

namespace{   // 匿名 namespace：托盘内部常量

constexpr UINT kTrayIconId = 1;              ///< 托盘图标 ID（单图标——多图标属非目标 O-8）
constexpr UINT kTrayCallbackMessage = WM_APP + 1;   ///< 托盘回调消息（NIF_MESSAGE）

}

Win32PlatformApplication::Win32PlatformApplication() = default;

// ── 7.1.5：消息循环（既有，不动——GetMessageW(nullptr) 自动覆盖宿主窗口，K9）──

int Win32PlatformApplication::Run(){

	// 标准 Win32 消息循环：GetMessage 返回 0 时退出（收到 WM_QUIT）
	MSG message{};

	while (GetMessageW(&message, nullptr, 0, 0)){

		TranslateMessage(&message);

		DispatchMessageW(&message);

		// 每条消息后的延迟清理时机（资源生命周期管理——框架经 SetDeferredCleanup 注册）
		PerformDeferredCleanup();

	}

	return static_cast<int>(message.wParam);

}

void Win32PlatformApplication::RequestExit(){

	PostQuitMessage(0);

}

// ── Phase 20（△22）：进程 DPI 感知声明（★ 应用级接缝——不在 Application 直调 Win32）──

void Win32PlatformApplication::DeclareDpiAwareness(){

	// 支持基线 = **Windows 10 1703+**（详设 §5.3 已冻结）⇒ 直接静态链接，不做
	// LoadLibrary / GetProcAddress 动态加载（YAGNI——失败容忍只覆盖"运行期"失败）。
	// ★ Per-Monitor V2：窗口跨屏时系统自动发 WM_DPICHANGED 并附建议矩形
	//   （Win32PlatformWindow 的 △14 处理块已就位）。
	// ⚠️ 必须在**任何窗口创建之前**调用——之后调用会因 ERROR_ACCESS_DENIED 失败（属常态）。
	if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)){

		return;   // 成功：默认路径，不记日志（不污染输出）

	}

	// ★ 失败容忍（契约 C6）：**只记日志**——不抛异常、不断言、**不主动降级尝试**。
	// ★ 诊断要点：把**实际感知级别**一并写出——框架「读取而非假定」，排查者须能看到真实状态。
	//   失败 + 本就 unaware ⇒ GetDpiForWindow 恒 96 ⇒ 换算恒等 ⇒ **退化为现状**
	//   （= G5 护栏），**不是"半降级"**——详设 §5.2。
	const DPI_AWARENESS awareness =
		GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());

	const wchar_t* level = L"PER_MONITOR_AWARE_V2";

	switch (awareness){

	case DPI_AWARENESS_UNAWARE:           level = L"UNAWARE"; break;

	case DPI_AWARENESS_SYSTEM_AWARE:      level = L"SYSTEM_AWARE"; break;

	case DPI_AWARENESS_PER_MONITOR_AWARE: level = L"PER_MONITOR_AWARE"; break;

	default: break;   // PER_MONITOR_AWARE_V2（含未识别值——按最高级处理）

	}

	Logger::Log(LogLevel::Warning,
		std::wstring(L"DPI awareness declaration failed; actual level = ") + level
		+ L" (framework reads the real DPI, so behaviour stays self-consistent)");

}

// ── Phase 14：托盘宿主（D2——懒创建；首次 SetTrayIcon 时）────────────────

void Win32PlatformApplication::EnsureTrayHost(){

	if (m_trayHostHwnd != nullptr) {

		return;

	}

	// ① 独立窗口类（与主窗口类分离——UnregisterClassW 各自独立，K10）
	m_trayHostClass = std::make_unique<WindowClass>("ECDI TrayHost", &TrayHostProc);

	// ② 隐藏顶层窗口（F1：不能是 message-only——否则收不到 TaskbarCreated 广播；
	//    F2：必须顶层才能收到。WS_POPUP 且不调用 ShowWindow ⇒ 创建后不可见）
	m_trayHostHwnd = CreateWindowExW(
		0,
		m_trayHostClass->GetClassName(),
		L"",
		WS_POPUP,
		0, 0, 0, 0,
		nullptr,
		nullptr,
		m_trayHostClass->GetInstance(),
		this   // CREATESTRUCT 传递 this——TrayHostProc 中 GWLP_USERDATA 绑定
	);

	if (m_trayHostHwnd == nullptr){

		Logger::Log(LogLevel::Error, L"Tray: CreateWindowExW failed for host window");

		m_trayHostClass.reset();   // 失败回滚窗口类（下次 SetTrayIcon 重试）

		return;   // 不抛异常（§6.7 失败语义——托盘失败不应杀应用）

	}

	// ③ shell 广播消息注册（explorer 重建通知——D9 自愈的触发源；返 0 ⇒ 自愈禁用）
	m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

	if (m_taskbarCreatedMsg == 0){

		Logger::Log(LogLevel::Warning, L"Tray: RegisterWindowMessageW(TaskbarCreated) failed");

	}

}

LRESULT CALLBACK Win32PlatformApplication::TrayHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){

	Win32PlatformApplication* self = nullptr;

	if (msg == WM_NCCREATE){

		auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);

		self = static_cast<Win32PlatformApplication*>(cs->lpCreateParams);

		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

	} else {

		self = reinterpret_cast<Win32PlatformApplication*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	}

	if (self == nullptr){

		return DefWindowProcW(hwnd, msg, wParam, lParam);

	}

	return self->HandleTrayHostMessage(hwnd, msg, wParam, lParam);

}

LRESULT Win32PlatformApplication::HandleTrayHostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){

	// ── TaskbarCreated 自愈（F2/D9——**只按 desired state 恢复**）────────
	if (m_taskbarCreatedMsg != 0 && msg == m_taskbarCreatedMsg){

		if (m_trayDesired && m_trayIcon != nullptr){

			NOTIFYICONDATAW nid = BuildIconData(m_trayIcon, m_trayTooltip);

			if (m_notifyShell(NIM_ADD, &nid)){

				nid.uVersion = NOTIFYICON_VERSION_4;

				m_notifyShell(NIM_SETVERSION, &nid);

				m_trayRegistered = true;

				Logger::Log(LogLevel::Info, L"Tray: re-registered after TaskbarCreated");

			} else {

				Logger::Log(LogLevel::Warning, L"Tray: re-register after TaskbarCreated failed");

			}

		} else {

			// desired=false（应用已主动移除）⇒ **绝不重加**（D9 铁律）——只同步内部状态
			m_trayRegistered = false;

		}

		return 0;

	}

	// ── 托盘回调（v4 翻译 → TrayEvent 上行）────────────────────────
	if (msg == kTrayCallbackMessage){

		HandleTrayCallback(wParam, lParam);

		return 0;

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

// ── Phase 14：托盘状态机（§3.1 十一态转移表的代码化）────────────────────

void Win32PlatformApplication::SetTrayIcon(const TrayIconOptions& options){

	m_trayDesired = true;   // 意图先行（表 #1–#5 ⇒ desired=true，无论成败）

	// ① 宿主就位（失败 ⇒ 不崩溃——§6.7；registered 不动，等重试/自愈）
	EnsureTrayHost();

	if (m_trayHostHwnd == nullptr){

		Logger::Log(LogLevel::Error, L"Tray: SetTrayIcon failed - host unavailable");

		return;

	}

	// ② 先加载新图标（「成功才提交」——失败时旧资源完整有效）
	int resourceId = options.iconResourceId;

	if (resourceId <= 0){

		Logger::Log(LogLevel::Warning, L"Tray: invalid icon resource id - using default");

		resourceId = TrayIconOptions::kDefaultIconResourceId;

	}

	HICON newIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
		MAKEINTRESOURCEW(resourceId), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

	bool newNeedsDestroy = false;

	if (newIcon == nullptr){

		Logger::Log(LogLevel::Warning, L"Tray: LoadImageW failed - fallback to system default icon");

		newIcon = LoadIconW(nullptr, IDI_APPLICATION);   // 共享句柄——不销毁

	} else {

		newNeedsDestroy = true;

	}

	const std::wstring newTooltip = UTF8ToWide(options.tooltip);

	// ③ Shell 操作：ADD/MODIFY 判据 = m_trayRegistered（**Shell 实际**——非 desired）
	const UINT action = m_trayRegistered ? NIM_MODIFY : NIM_ADD;

	NOTIFYICONDATAW nid = BuildIconData(newIcon, newTooltip);

	if (m_notifyShell(action, &nid)){

		// ④ 成功 ⇒ 先销旧 owned 句柄再提交新值（旧句柄在 Shell 成功前被引用——不可提前销毁）
		if (m_trayIcon != nullptr && m_trayIconNeedsDestroy){

			DestroyIcon(m_trayIcon);

		}

		m_trayIcon = newIcon;

		m_trayIconNeedsDestroy = newNeedsDestroy;

		m_trayTooltip = newTooltip;

		m_trayRegistered = true;

		// 版本设置（NOTIFYICON_VERSION_4——D5）：必须在 NIM_ADD 之后单独设
		if (action == NIM_ADD){

			nid.uVersion = NOTIFYICON_VERSION_4;

			if (!m_notifyShell(NIM_SETVERSION, &nid)){

				Logger::Log(LogLevel::Warning, L"Tray: NIM_SETVERSION failed (degrade to legacy semantics)");

			}

		}

	} else {

		// ⑤ 失败 ⇒ 销毁刚加载的新句柄（owned 才销），旧值全部保持（§3.1 #2/#5）
		if (newIcon != nullptr && newNeedsDestroy){

			DestroyIcon(newIcon);

		}

		Logger::Log(LogLevel::Warning,
			(action == NIM_ADD) ? L"Tray: NIM_ADD failed"
			                    : L"Tray: NIM_MODIFY failed (kept old state)");

	}

}

void Win32PlatformApplication::RemoveTrayIcon(){

	m_trayDesired = false;   // 意图先行（表 #6–#8 ⇒ desired=false）

	if (!m_trayRegistered){

		return;   // 幂等（D11 表 #8）——不调 Shell、不记错误

	}

	NOTIFYICONDATAW nid = BuildIconData(m_trayIcon, m_trayTooltip);

	const BOOL ok = m_notifyShell(NIM_DELETE, &nid);

	m_trayRegistered = false;   // 向「已移除」收敛（表 #6/#7——无论成败）

	if (ok){

		Logger::Log(LogLevel::Info, L"Tray: icon removed");

		// DELETE 成功 ⇒ shell 无引用 ⇒ 立即销毁 owned 句柄
		if (m_trayIcon != nullptr && m_trayIconNeedsDestroy){

			DestroyIcon(m_trayIcon);

		}

		m_trayIcon = nullptr;

		m_trayIconNeedsDestroy = false;

	} else {

		// DELETE 失败 ⇒ 保守保留句柄（shell 可能仍引用），析构步骤 2.2 兜底销毁
		Logger::Log(LogLevel::Warning, L"Tray: NIM_DELETE failed (converge to removed)");

	}

}

int Win32PlatformApplication::ShowTrayMenu(const TrayMenu& menu){

	// 未设图标（宿主未建）或空菜单 ⇒ 0（与「未选中」天然兼容——§6.7）
	if (m_trayHostHwnd == nullptr || menu.items.empty()){

		return 0;

	}

	HMENU hMenu = CreatePopupMenu();

	if (hMenu == nullptr){

		Logger::Log(LogLevel::Warning, L"Tray: CreatePopupMenu failed");

		return 0;   // 与「未选中」语义兼容（§6.7）

	}

	// 一级 + 纯文本 + ID（D6 范围边界——图标/勾选/子菜单不做）
	for (const auto& item : menu.items){

		if (item.id <= 0) {

			continue;   // 非法 ID 忽略（保证 0 能安全表示「未选中」）

		}

		AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(item.id),
			UTF8ToWide(item.text).c_str());

	}

	// 锚点兜底（修正五）：`ShowTrayMenu` 是公共 API，调用方可在**任何时刻**调它
	//（不一定是响应 TrayEvent）。若此刻尚无任何锚点（缓存仍是 0——从未收过事件），
	// 取当前光标位置，否则菜单会弹在屏幕左上角 (0,0)。
	int anchorX = m_lastAnchorX;
	int anchorY = m_lastAnchorY;

	if (anchorX == 0 && anchorY == 0){

		POINT cursor{};

		if (GetCursorPos(&cursor)){
			anchorX = cursor.x;
			anchorY = cursor.y;
		}

	}

	// ★ TrackPopupMenu 前置要求（MSDN）：必须先 SetForegroundWindow，
	//   否则菜单不会随点击外部而消失（经典 Win32 陷阱）
	SetForegroundWindow(m_trayHostHwnd);

	const int cmd = static_cast<int>(TrackPopupMenu(
		hMenu,
		TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,   // 结果不走 WM_COMMAND（D6/D10）
		anchorX,
		anchorY,
		0,
		m_trayHostHwnd,
		nullptr));

	// 菜单关闭后补发 WM_NULL（MSDN 配套要求——让系统完成菜单状态清理）
	PostMessageW(m_trayHostHwnd, WM_NULL, 0, 0);

	DestroyMenu(hMenu);

	return cmd;   // 0 = 未选中 / 取消

}

// ── Phase 14：回调翻译（v4 语义 → TrayEvent——O-5 已核实）────────────────

void Win32PlatformApplication::HandleTrayCallback(WPARAM wParam, LPARAM lParam){

	// v4 语义（O-5 已核实——MSDN「NOTIFYICONDATAW」原文）：
	//   LOWORD(lParam) = 通知事件；HIWORD(lParam) = 图标 ID（16 位）；
	//   GET_X/Y_LPARAM(wParam) = 锚点（对 NIN_SELECT / NIN_KEYSELECT / 鼠标消息有效；
	//   键盘生成时 = 目标图标左上角）。⚠️ 其余消息（含 WM_CONTEXTMENU）wParam undefined。
	const UINT notifyEvent = LOWORD(lParam);

	TrayEventType type{};

	switch (notifyEvent){

		// ★ Phase 14 A3 修正一/三（2026-09-17 实测）：v4 下**左键单击走鼠标消息 WM_LBUTTONUP**。
		// 修正三：NIN_SELECT 与 WM_LBUTTONUP **同源重复**——实测一次左键单击会**同时**送来这两条
		// 通知（都翻译成 Select ⇒ 单击被上报两次；界面观测为"点一下就 #2，再点一下 #4"）。
		// 故只认鼠标消息（它带锚点坐标，且与 DBLCLK/CONTEXTMENU 同族），NIN_SELECT 不再单独翻译
		//（落到 default 静默忽略）。键盘路径由 NIN_KEYSELECT 独立覆盖，不受影响。
		case WM_LBUTTONUP:     type = TrayEventType::Select;      break;

		case NIN_KEYSELECT:    type = TrayEventType::KeySelect;   break;

		// ★ Phase 14 A3 修正二/四（2026-09-17 实测）：双击由**系统 DBLCLK**上报。
		// 修正一曾「以 UP 序列自合成双击 + 忽略系统 DBLCLK」；修正二改为直接采用 DBLCLK（权威来源、
		// 无需自合成）。实测完整序列 = `UP` + `DBLCLK` + `UP`——**系统确实会报 DBLCLK**，而尾部
		// 那个 UP 属双击序列本身，由修正四（下方吞除）处理。
		case WM_LBUTTONDBLCLK: type = TrayEventType::DoubleClick; break;

		case WM_CONTEXTMENU:   type = TrayEventType::ContextMenu; break;

		default: return;   // 其它通知（NIN_POPUPOPEN / balloon 系——非目标）静默忽略

	}

	// ── 双击序列尾部的 UP 吞除（Phase 14 A3 修正四——2026-09-17 实测）────────────────
	// 实测双击事件序列 = `UP` + `DBLCLK` + `UP`，且 **DBLCLK 与紧随的 UP 间隔恒为 0 ms**
	//（6 组样本全部同毫秒）——尾部那个 UP 属于双击序列本身（第二次按下的抬起），不是新一次单击。
	// 不吞掉的话：双击会报 **3 个事件**、末位多出一个 `Select`（实测 `#2 Select / #3 DoubleClick / #4 Select`）。
	// 判据 = 「DBLCLK 之后 GetDoubleClickTime() 内的第一个 UP」：两次**独立**单击之间不会出现 DBLCLK，
	// 故不会误吞；若某宿主不发 DBLCLK 后的 UP，标志会在时限后自动失效（不粘连下一次单击）。
	const ULONGLONG nowTick = GetTickCount64();

	if (notifyEvent == WM_LBUTTONUP
		&& m_swallowNextUpTick != 0
		&& (nowTick - m_swallowNextUpTick) <= GetDoubleClickTime()){

		m_swallowNextUpTick = 0;   // 已消费（一次性）
		return;

	}

	int x = 0;

	int y = 0;

	if (type == TrayEventType::ContextMenu){

		// v4 下 WM_CONTEXTMENU 无坐标（O-5）⇒ GetCursorPos 兜底
		//（右键上下文菜单时光标必在图标处——Win32 标准做法）
		POINT cursor{};

		if (GetCursorPos(&cursor)){

			x = cursor.x;

			y = cursor.y;

		} else {

			x = m_lastAnchorX;   // 最近有效锚点兜底（§6.7）

			y = m_lastAnchorY;

		}

	} else {

		x = GET_X_LPARAM(wParam);

		y = GET_Y_LPARAM(wParam);

	}

	// ★ 锚点缓存**两个分支统一更新**（修正五，2026-09-17 实测）。
	// `ShowTrayMenu` 靠 `m_lastAnchorX/Y` 定位菜单；原先只有 else 分支更新 ⇒
	// **首次直接右键**时缓存仍是 0（从未收过鼠标事件）⇒ 菜单弹在屏幕**左上角 (0,0)**，
	// 而"先点一次左键"后缓存被刷新，表现就变成"点了左键才正常"（实测症状）。
	// 两个分支此刻都已得到**有效坐标**（ContextMenu = GetCursorPos 兜底 / 其余 = v4 锚点），故统一提交。
	m_lastAnchorX = x;

	m_lastAnchorY = y;

	if (type == TrayEventType::DoubleClick){

		m_swallowNextUpTick = nowTick;   // 武装吞除：双击的第二个 UP 紧随其后（实测间隔 0 ms）

	}

	EmitTrayEvent(TrayEvent(type, x, y));   // ← 上行（D3 的 sink）

}

// ── Phase 14：测试注入/观测（仅内部头可见——条 51：seam 不出实现层）────────

UINT Win32PlatformApplication::GetTrayCallbackMsgForTests() const noexcept{

	return kTrayCallbackMessage;   // 回调消息号是本 .cpp 私有常量——测试注入需知道它

}

// ── Phase 14：辅助 ─────────────────────────────────────────────────────

NOTIFYICONDATAW Win32PlatformApplication::BuildIconData(HICON icon, const std::wstring& tooltip){

	NOTIFYICONDATAW nid{};

	nid.cbSize = sizeof(NOTIFYICONDATAW);

	nid.hWnd = m_trayHostHwnd;             // ← 宿主窗口（非 message-only——F1）

	nid.uID = kTrayIconId;

	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

	nid.uCallbackMessage = kTrayCallbackMessage;

	nid.hIcon = icon;

	if (!tooltip.empty()){

		wcsncpy_s(nid.szTip, tooltip.c_str(), _TRUNCATE);   // szTip[128]

	}

	return nid;

}

BOOL Win32PlatformApplication::NotifyShellAdapter(UINT action, NOTIFYICONDATAW* nid){

	return Shell_NotifyIconW(action, nid);

}

void Win32PlatformApplication::DragFinishAdapter(HDROP hDrop){

	DragFinish(hDrop);

}

// ── Phase 14：析构（四步不变量：NIM_DELETE → DestroyIcon → DestroyWindow → UnregisterClassW）──

Win32PlatformApplication::~Win32PlatformApplication(){

	// 2.1 幽灵图标防线（R1 硬要求）：registered 才删——无论 desired；
	//     失败仅 Warning——不阻塞析构（§6.7，剩余风险 = 幽灵图标，记账）
	if (m_trayRegistered && m_trayHostHwnd != nullptr){

		NOTIFYICONDATAW nid = BuildIconData(m_trayIcon, m_trayTooltip);

		m_notifyShell(NIM_DELETE, &nid);

		m_trayRegistered = false;

	}

	// 2.2 图标句柄（owned 才销毁——LoadIconW 共享句柄除外）
	if (m_trayIcon != nullptr && m_trayIconNeedsDestroy){

		DestroyIcon(m_trayIcon);

		m_trayIcon = nullptr;

	}

	// 2.3 先销窗口（K10：UnregisterClassW 要求该类无存活窗口）
	if (m_trayHostHwnd != nullptr){

		DestroyWindow(m_trayHostHwnd);

		m_trayHostHwnd = nullptr;

	}

	// 2.4 m_trayHostClass（unique_ptr）成员析构自动 UnregisterClassW——此时窗口已销，安全

}

}
