#pragma once
namespace ECDI
{

/// @brief Framework 事件类型枚举
/// @details
/// 每种事件类型对应一个 Event 子类，用于 EventDispatcher 的类型分派。
enum class EventType{

	None = 0,
	
	// ── 窗口事件 ────────────────────────────────────
	WindowCreated,			///< 窗口创建完成
	WindowDestroyed,		///< 窗口销毁
	WindowCloseRequested,	///< 用户请求关闭窗口（点击关闭按钮 / Alt+F4）
	WindowResized,			///< 窗口大小变化

	// ── 鼠标事件 ────────────────────────────────────
	MouseMove,				///< 鼠标移动
	MouseButtonDown,		///< 鼠标按键按下
	MouseButtonUp,			///< 鼠标按键释放
	MouseWheel,				///< 鼠标滚轮

	// ── 键盘事件 ────────────────────────────────────
	KeyDown,				///< 键盘按键按下
	KeyUp,					///< 键盘按键释放
	CharInput				///< 字符输入（由 WM_CHAR 翻译，一个 wchar_t = 一个事件）
};

}
