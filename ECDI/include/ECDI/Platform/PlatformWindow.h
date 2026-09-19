#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Widget/CaretGeometry.h"
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/WindowLayer.h"
#include "ECDI/Window/WindowState.h"

#include <string>

namespace ECDI{

class PlatformWindowHost;   // 前置声明（构造注入 Host&）
class PlatformRenderContext;   // 前置声明（GetRenderContext 返回 const&——零 include 依赖，7.1.4）

/// @brief 平台窗口抽象（7.1）：平台负责"窗口存在"，框架负责"窗口里面发生什么"
/// @details Window 组合此接口——Window 不接触 HWND/创建细节；
/// 生命周期 + 平台能力（重绘请求/客户区查询/文本输入插入点）下沉。
/// 唯一实现：Win32PlatformWindow（X11/Wayland 只留接口，YAGNI）。
/// 零 Win32 类型——接口全部用框架层类型（Size/CaretGeometry），平台细节封装在实现内。
///
/// ★ 平台能力扩展惯例（Phase 12 R9 定稿——后续阶段加能力的模板，D-SEAM-1）：
///   ① PlatformWindow 加一个能力 virtual（本文件——接口即契约，零消息号）；
///   ② Window 加公共方法透传（Window.h——应用层唯一入口）；
///   ③ 平台消息在具体实现的消息循环内消化（如 Win32PlatformWindow::HandleMessage
///      状态同步区——不进 WindowMessageHandler 翻译器，除非它产生 Framework Event）。
/// 三步都不新建接缝类/消息注册机制——「惯例而非抽象」（R9 裁决）。
/// 先例：Phase 12 的 SetChromeMode/SetWindowLayer/Minimize/Maximize/Restore。
class PlatformWindow{
public:
	virtual ~PlatformWindow() = default;

	/// @brief 显示窗口
	virtual void Show() = 0;

	/// @brief 销毁底层窗口句柄（幂等——重复调用返回 true 不报错）
	virtual bool Release() noexcept = 0;

	/// @brief 请求重绘整个客户区（异步可合并——契约语义，非平台细节）
	virtual void Invalidate() = 0;

	/// @brief 客户区尺寸（框架层 Size，非 Win32 RECT——类型封装在实现内）
	virtual Size GetClientSize() const = 0;

	/// @brief 平台渲染上下文（7.1.4：后端经此拿平台句柄——"参数识别"→"平台返回"，
	/// Window 层零识别；识别发生在平台实现内部 static_cast）
	virtual const PlatformRenderContext& GetRenderContext() const = 0;

	/// @brief 更新文本输入插入点（5.6 双通道：系统 caret + ImmSetCompositionWindow；
	/// 7.1.3 参数升级 CaretGeometry——插入点矩形 + 逻辑可见性）
	/// @param geometry 插入点几何（框架层 CaretGeometry，非 Win32 类型；坐标系语义封装在实现内）
	virtual void UpdateTextInputCaret(const CaretGeometry& geometry) = 0;

	/// @brief 销毁文本输入插入点（幂等）
	virtual void DestroyTextInputCaret() = 0;

	/// @brief 从系统剪贴板读取文本（8.5.1；平台能力——UTF-8，转换封装在实现内）
	/// @return 空字符串 = 剪贴板无文本数据（或非文本格式）
	virtual std::string GetClipboardText() const = 0;

	/// @brief 写入文本到系统剪贴板（8.5.1；UTF-8）
	/// @param text 待写入文本（空串 = 清空剪贴板——调用方负责避免误清）
	virtual void SetClipboardText(const std::string& text) = 0;

	/// @brief 启动周期定时器（8.5.1；通用平台能力——ID 语义由调用方定义，平台不知道业务）
	/// @param timerId    定时器标识（调用方自定义；重复 Start 同 id = 重置周期）
	/// @param intervalMs 触发间隔（毫秒）
	virtual void StartTimer(int timerId, unsigned int intervalMs) = 0;

	/// @brief 停止定时器（幂等——未启动/已停止返回无动作）
	virtual void StopTimer(int timerId) = 0;

	// ── Phase 12：WindowChrome / 窗口层级（配置期——Window 构造后 / Show() 前）──

	/// @brief 设置窗口 chrome 形态（R1）
	/// @param mode 目标形态
	/// @details **仅配置期生效**（初设决策 D1，见 §9.1）——
	/// Show() 之后调用记 Warning 日志并忽略（不抛异常）。
	/// ⚠️ **一次确定**（详设 D-CHROME-1）：配置期内重复调用（无论同值异值）一律
	/// Warning + 忽略——避免「Borderless → Normal」这类会与 frame 状态失同步的切换。
	virtual void SetChromeMode(ChromeMode mode) = 0;

	/// @brief 设置自定义标题栏高度（R3；逻辑坐标 DIP）
	/// @param height 标题栏高度（逻辑坐标；<= 0 视为 0——允许应用完全放弃 HTCAPTION 拖动区）
	virtual void SetCaptionHeight(int height) = 0;

	/// @brief 设置缩放热区宽度（R3；逻辑坐标 DIP）
	/// @param inset 四边/四角的命中测试宽度（逻辑坐标；<= 0 视为 0——完全禁用边缘缩放）
	virtual void SetResizeInset(int inset) = 0;

	/// @brief 设置窗口层级档位（R10）
	/// @param layer 目标档位
	/// @details **Bottom** 档持续维护普通窗口层底部位置；**Desktop** 档持续维护
	/// 「紧贴桌面窗口正上方」（Win+D 后仍可见）——两者均由 Win32 实现经
	/// `WM_WINDOWPOSCHANGING` 维护。切回 Normal 时停止维护（不主动改变当前 z 序——
	/// 交系统自然演化）。
	virtual void SetWindowLayer(WindowLayer layer) = 0;

	// ── Phase 12：窗口状态（运行期——**Show() 之后**才有效）──────────
	// @pre 运行期契约（初设 v1.3）：Show() 之前调用记 Warning 并忽略；
	//      与配置期四件套**对称**（配置期 = Show 前有效 / 运行期 = Show 后有效）。

	/// @brief 最小化窗口（R7）——薄封装 ShowWindow(SW_MINIMIZE)
	/// @pre Show() 之前调用记 Warning 并忽略（运行期 API——见初设 §9.2）
	virtual void Minimize() = 0;

	/// @brief 最大化窗口（R7）——薄封装 ShowWindow(SW_MAXIMIZE)
	/// @pre 同 Minimize
	virtual void Maximize() = 0;

	/// @brief 还原窗口（R7）——薄封装 ShowWindow(SW_RESTORE)
	/// @details 最小化态 → 还原到原尺寸；最大化态 → 还原到最大化前尺寸（系统语义）。
	/// @pre 同 Minimize
	virtual void Restore() = 0;

	// ── Phase 13：窗口状态查询（R3——**事实来源在本层**）────────────────

	/// @brief 查询当前窗口状态（Phase 13 R3）
	/// @details 与 `WindowStateChangedEvent` 互补：事件回答「变成了什么」，本查询回答「现在是什么」。
	/// 事实的唯一来源在平台层（`WM_SIZE` 时由 `IsIconic` / `IsZoomed` 判定并缓存）——**纯虚**。
	virtual WindowState GetWindowState() const noexcept = 0;

	// ── Phase 14：窗口显示控制 / 文件拖入（运行期——Show() 之后有效）────

	/// @brief 隐藏窗口（R12——Show 的对称；**不销毁 HWND**）
	/// @details 与 Release() 的语义边界：Hide = 资源存活仅不可见（可再 Show）；
	/// Release = 销毁 HWND（不可逆）。**不改 m_shown**（运行期标记不因隐藏回退）。
	/// @pre Show() 之前调用记 Warning 并忽略（与 Minimize 同组）
	virtual void Hide() = 0;

	/// @brief 启用 / 停用本窗口的文件拖入（R8；默认关闭）
	/// @details 平台实现为 DragAcceptFiles 薄封装；幂等、可重复调用。
	/// @pre Show() 之前调用记 Warning 并忽略（O-3 拍板：运行期分组——架构一致性选择）
	virtual void SetFileDropEnabled(bool enabled) = 0;
};

}
