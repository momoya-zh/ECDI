#include "ECDI/Platform/Win32/Win32PlatformWindow.h"

#include "ECDI/Platform/Win32/Win32WindowClass.h"
#include "ECDI/Core/String.h"

#include <Windows.h>
#include <imm.h>

#include <cstring>
#include <string>
#include <system_error>

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
		m_hwnd = nullptr;   // 句柄失效（框架层无需动作——YAGNI，无 OnDestroyed 回调）
		break;

	case WM_SIZE:
		// 窗口大小变化 → Host 回调同步 RootWidget 尺寸；随后 fall-through 翻译器（WindowResizedEvent）
		m_host.OnResized(LOWORD(lParam), HIWORD(lParam));
		break;

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

}
