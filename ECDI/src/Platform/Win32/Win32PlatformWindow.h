#pragma once

#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/WindowLayer.h"
#include "ECDI/Window/WindowState.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Platform/Win32/WindowMessageHandler.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护：DrawText → DrawTextW 会污染公共头的方法名（与 GDIBackend.h 同款）
#endif

#include <string>

namespace ECDI{

/// @brief Win32 平台窗口实现（7.1.1 唯一实现）
/// @details 承载全部 Win32：窗口生命周期 / WindowProc / 消息处理 / 翻译器 / IME 平台调用。
/// 平台代码不再出现在框架类（Window）中——Window.h 零 Win32（V2 验收）。
/// 持 Host& 回调框架层（Platform 不认识框架具体类，只认识契约）。
/// 7.1.5：窗口类注册下沉（WindowClass::Instance()——窗口系统资源归窗口类自身）。
class Win32PlatformWindow final : public PlatformWindow{
public:

	/// @param host     Host 回调（框架契约）
	/// @param title    窗口标题（UTF-8，内部转 UTF-16）
	/// @param width    窗口总宽度（含边框和标题栏）
	/// @param height   窗口总高度
	/// @throws std::system_error CreateWindowExW 失败
	Win32PlatformWindow(PlatformWindowHost& host,
	                    const std::string& title, int width, int height);

	~Win32PlatformWindow() override;   ///< Release（幂等）

	void Show() override;
	bool Release() noexcept override;
	void Invalidate() override;
	Size GetClientSize() const override;
	const PlatformRenderContext& GetRenderContext() const override;   ///< 7.1.4：后端经此拿平台句柄
	void UpdateTextInputCaret(const CaretGeometry& geometry) override;
	void DestroyTextInputCaret() override;
	std::string GetClipboardText() const override;   ///< 8.5.1：剪贴板 capability（CF_UNICODETEXT + 转换封装）
	void SetClipboardText(const std::string& text) override;
	void StartTimer(int timerId, unsigned int intervalMs) override;   ///< 8.5.1：通用定时器（SetTimer）
	void StopTimer(int timerId) override;   ///< 8.5.1：StopTimer（KillTimer，幂等）

	// ── Phase 12：WindowChrome / 窗口层级（配置期——Window 构造后 / Show() 前）──
	void SetChromeMode(ChromeMode mode) override;
	void SetCaptionHeight(int height) override;
	void SetResizeInset(int inset) override;
	void SetWindowLayer(WindowLayer layer) override;

	// ── Phase 12：窗口状态（运行期——Show() 之后才有效）──────────
	void Minimize() override;
	void Maximize() override;
	void Restore() override;

	// ── Phase 13：窗口状态查询（R3——返回 WM_SIZE 缓存的事实）──────────

	WindowState GetWindowState() const noexcept override;

	// ── Phase 14：窗口显示控制 / 文件拖入 ──────────────────────────

	void Hide() override;
	void SetFileDropEnabled(bool enabled) override;

	/// @brief DragFinish 测试缝类型（v1.1 拍板：函数指针——不出实现层、保 final）
	/// @details 声明必须位于首个使用点之前——GCC 对成员函数形参不做延迟名字查找
	/// （放在 private 区会令 MinGW 报 "'DragFinishFn' has not been declared"）
	using DragFinishFn = void (*)(HDROP hDrop);

	// ── 测试注入/观测（仅内部头——不进 Public API；条 51：seam 不出实现层）──

	HWND GetHwndForTests() const noexcept{ return m_hwnd; }

	void SetDragFinishForTests(DragFinishFn fn){ m_dragFinish = fn; }

	// ── Phase 16：桌面驻留层（D9 观测缝 + O3 纯函数）──────────────────────

	/// @brief 桌面钩子安装/卸除的观测缝（Phase 16 D9——**函数指针**形态，复刻 `DragFinishFn` 先例）
	/// @details `SetWinEventHook` 依赖**真实桌面环境**，自动化只能断言「装 / 卸被调用了」，
	/// 故以观测缝计数，不去 mock 系统 API（条 51：seam 不出实现层、保 `final`）。
	/// ⚠️ 声明必须位于其首个使用点（`SetDesktopHookObserverForTests` 的**形参类型**）之前——
	/// 与 `DragFinishFn` 同一教训：GCC 对成员函数形参不做延迟名字查找
	/// （若放在 private 区，MinGW 会报 has not been declared）。
	using HookObserverFn = void (*)(bool installed);

	/// @param fn 观测回调（`true` = 已装 / `false` = 已卸）；`nullptr` = 取消观测
	void SetDesktopHookObserverForTests(HookObserverFn fn){ m_hookObserver = fn; }

	/// @brief 把「档位 + 桌面句柄」映射为目标 z 序位置（**纯函数**——O3 定为 C 的落点）
	/// @param layer   当前档位
	/// @param desktop 桌面窗口句柄（仅 `Desktop` 档需要；其余档位忽略）
	/// @return `HWND_BOTTOM` = Bottom 档 ·「桌面窗口的上一位」= Desktop 档 ·
	///         **`nullptr` = 本次不修改**（Normal 档 / 桌面句柄无效 / 桌面已处 z 序最顶）
	/// @details ⚠️ **`nullptr` 是「跳过」哨兵，不是「插到最底」**——调用方必须显式判空，
	/// 绝不可把它直接交给 `SetWindowPos`（那会落到 `HWND_BOTTOM`，把 `Desktop` 档
	/// **意外降级**成 `Bottom`——K8 / 契约 C2 的核心）。
	/// @note **public static 的唯一目的是可被自动化测试直接覆盖**（C2 是本阶段最危险的路径）。
	/// 测试已 include 本内部头（`DropFilesTests.cpp:13` 先例）⇒ 零新文件、零测试缝；
	/// 且它不读实例状态 ⇒ 无副作用、可纯逻辑断言（T16-7）。
	static HWND ResolveTarget(WindowLayer layer, HWND desktop);

	/// @brief 静态窗口过程（应用层注册 WindowClass 用；GWLP_USERDATA 绑定本实例）
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:

	/// @brief 实例级消息处理（WindowProc 路由到这里）
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief WM_DROPFILES 处理（Phase 14——解析 HDROP → DragFinish → 抛事件，R9/R10/R11）
	void HandleDropFiles(HDROP hDrop);

	/// @brief DragFinish 测试缝实现（类型定义见上方 public 区——此处只留适配器与成员）
	static void DragFinishAdapter(HDROP hDrop);
	DragFinishFn m_dragFinish = &DragFinishAdapter;

	/// @brief 最大化客户区校正（R4——rcWork 唯一基准，D-COMP-1：不引入补偿）
	void AdjustMaximizedClientRect(HWND hwnd, RECT& rcClient);

	/// @brief DIP → 物理像素（D-DPI-1：窗口 DPI——非鼠标所在显示器）
	static int DipToPixels(int dip, HWND hwnd);

	/// @brief DWM 增强（R6——系统阴影 + Win11 圆角；失败容忍，仅日志不中断）
	void ApplyDwmEnhancements(HWND hwnd);

	// ── Phase 16：桌面驻留层（R1–R4）────────────────────────────────────

	/// @brief 目标 z 序位置（D8——按档位分流的**唯一**入口）
	/// @details 只做两件事：**判断是否要查桌面句柄**（仅 Desktop 档）+ 交给 `ResolveTarget`。
	/// 语义与返回值同 `ResolveTarget`（`nullptr` = 本次不修改）。
	HWND TargetInsertAfter() const;

	/// @brief 定位桌面窗口（D6 / D7——**不缓存**，每次即时重查）
	/// @return 桌面窗口句柄；`nullptr` = 不可用（explorer 重建窗口期 / 非交互式会话）
	static HWND FindDesktopWindow();

	/// @brief 本窗口是否已「紧贴桌面窗口正上方」（D8 判据——需求稿 §8.1② 的前置检查）
	/// @details ⚠️ **三态**（契约 C11）：`true` = 已在位 / `false` = 未在位 /
	/// **`true` = 不可判定**——不可判时返回 `true` 的语义是「**禁止无依据的 z-order 操作**」，
	/// **不是**「z 序已满足 C1」。名称保留（改成 `…OrCannotDetermine` 只会让调用点更难读）。
	bool IsDirectlyAboveDesktop() const;

	/// @brief 把窗口重新插到桌面窗口正上方（内部**先判在位**，已在位则什么都不做）
	/// @details Phase 16 A5：本判据在**外壳抬桌面之前**也为真（实测）⇒ 「已在位」可能是
	/// **过期判断**，故**不得**把它当作「已跟随完成」的依据（详见 `FollowDesktopStep`）。
	void ReinsertAboveDesktop();

	/// @brief Phase 16 A5：桌面跟随的**一步**（重插；并再排下一拍，直到用满 kDesktopFollowMaxSteps）
	/// @details 由 `kDesktopFollowMsg`（延后一拍）与 `kDesktopFollowTimerId`（短延时）共用。
	/// **每一拍都无条件重插、无条件再排下一拍**——不能用「插上就停」：重插成功只证明
	/// 「插了」，**不证明「插在外壳抬桌面之后」**（A5 实测：第一拍常在外壳动作前成功，
	/// 随后被盖住）。也不能用 `IsDirectlyAboveDesktop()` 当停止条件：外壳抬桌面**之前**
	/// 它同样为真。
	void FollowDesktopStep(HWND hwnd);

	/// @brief 应用 / 撤销 Desktop 档所需的样式位（D1 A′ / D3）
	/// @param desktop `true` = 移除 `WS_MINIMIZEBOX`；`false` = 补回
	/// @details **幂等**（目标值与现值相同则不动手——避免多余的 `WM_STYLECHANGING/CHANGED`
	/// 往返）且**可逆**（配置期内允许 `Desktop → Normal` 转移）。
	/// ⚠️ **不需要 `SWP_FRAMECHANGED`**（O1 已实测关闭，初设 §6.1）：`WS_MINIMIZEBOX`
	/// 不参与非客户区几何计算 ⇒ 改这一位没有需要系统重算的对象。
	void ApplyDesktopStyle(bool desktop);

	/// @brief 同步钩子与当前档位（D11 四态表的**唯一**执行点）
	/// @details 幂等：Desktop 档且未装 ⇒ 装；非 Desktop 档且已装 ⇒ 卸；其余不动。
	void SyncDesktopHook();

	/// @brief 强制卸除钩子（`Release()` / 析构路径专用——**只减不增**，不读档位）
	void SyncDesktopHookOff();

	/// @brief 前台事件回调的转发落点（在**注册线程**上执行——契约 C8）
	/// @param foreground 成为前台的窗口（可能不是本窗口）
	void OnForegroundChanged(HWND foreground);

	/// @brief 静态窗口过程式回调（`SetWinEventHook` **无 user-data 参数** ⇒ 经 hook 反查实例）
	/// @details 签名与 `WINEVENTPROC` 一致。**既有先例**：`&Win32PlatformWindow::WindowProc`
	/// 以同款「`static … CALLBACK` 成员 → Win32 回调指针」形态传进 `WNDCLASSW.lpfnWndProc`
	/// （`Win32WindowClass.cpp:28`）——但 `WINEVENTPROC` 是**另一个**函数指针类型，
	/// 四工具链仍须实际编译确认（O5）。
	static void CALLBACK DesktopForegroundProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
		LONG idObject, LONG idChild, DWORD thread, DWORD time);

	/// @brief 当前是否处于 Desktop 档（判据集中——避免各处重复比较枚举）
	bool IsDesktopLayer() const noexcept{ return m_windowLayer == WindowLayer::Desktop; }

	PlatformWindowHost& m_host;	///< Host 回调（非拥有——Window 实现）
	WindowMessageHandler m_messageHandler;	///< 翻译器（7.1.2：构造传 m_host——不再认识应用层）
	HWND m_hwnd = nullptr;	///< 窗口句柄（原 Window::m_handle）
	bool m_caretCreated = false;	///< 系统 caret 是否已创建（5.6 v1.0.3 懒创建标记）
	Win32RenderContext m_renderContext;	///< 渲染上下文（7.1.4：构造体 CreateWindowExW 成功后 SetHandle）

	// ── Phase 12：chrome / 层级 / 生命周期状态 ─────────────────────

	ChromeMode m_chromeMode = ChromeMode::Normal;	///< chrome 形态（R1——配置期生效，Show 后拒绝切换）
	int m_captionHeight = 32;	///< 标题栏命中高度（R3；逻辑坐标 DIP——默认 32）
	int m_resizeInset = 8;	///< 缩放热区宽度（R3；逻辑坐标 DIP——默认 8）
	WindowLayer m_windowLayer = WindowLayer::Normal;	///< 层级档位（R10——语义状态，不随实现路径降级）

	bool m_shown = false;	///< 是否已调用过 Show()（配置期/运行期判据——「API 调用事实」，非「系统当前可见」）
	bool m_chromeConfigured = false;	///< ChromeMode 是否已配置（D-CHROME-1：一次确定——重复调用一律 Warning + 忽略）
	WindowState m_lastWindowState = WindowState::restored;	///< 状态事件去重锚（零值 = 窗口初始态）

	int m_imeResultPendingChars = 0;	///< 待吞掉的 IME 结果 WM_CHAR 数（8.5.1：GCS_RESULTSTR 已提交框架——
	///< 系统随后仍发结果 WM_CHAR（DefWindowProc 通道），吞掉防双写；UTF-16 码元计数 = WM_CHAR 消息数）

	// ── Phase 16：桌面驻留层状态 ────────────────────────────────────────

	HWINEVENTHOOK m_desktopHook = nullptr;	///< 桌面驻留维护钩子（D11——**仅 Desktop 档持有**，其余档恒 `nullptr`）
	HookObserverFn m_hookObserver = nullptr;	///< 钩子观测缝（测试用——生产恒 `nullptr`，见 public 区的 setter）
	int m_desktopFollowRetries = 0;	///< Phase 16 A5：桌面跟随**步数计数**（每轮 Win+D 由 OnForegroundChanged 清零；上限 kDesktopFollowMaxSteps）

};

}
