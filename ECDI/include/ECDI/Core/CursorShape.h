#pragma once

#include <cstdint>

namespace ECDI{

/// @brief 鼠标光标形态（★ #72-②：纯值类型、零依赖——独立公共头，`TextAlignment.h` 同构）
/// @details 消费链（自下而上）：`Widget::GetCursor()`（悬停控件自述）→
///          `PlatformWindowHost::OnQueryCursor`（窗口以 HitTest 代答，平台层不认识 Widget）
///          → Win32 `WM_SETCURSOR`（映射系统共享 HCURSOR + `::SetCursor`）。
/// @note ⚠️ 零值 = `Arrow` 即「无自述」的缺省（全部控件默认）；新形态**只追加末尾不重排**
///          （序列化稳定性与 `TextAlignment` 同约定）。
enum class CursorShape : std::uint8_t{
	Arrow = 0,   ///< 标准箭头（缺省——Widget 基类默认）
	IBeam  = 1,  ///< I 形文本光标（TextBox 等文本编辑区悬停）

	// ── #87（2026-11-05，来源 = harness 账本 REQ-19-A）：悬停控件可自述的其余形态 ────
	//    ★ 只追加末尾、不重排（本头 :11 的约定）⇒ 既有断言与零值语义零影响。
	//    ★ **不收 `Wait` / `AppStarting`**：那是**模态忙闲态**（整窗级），不是「悬停在
	//      这个控件上」的自述；混进来会让「每条 `WM_SETCURSOR` 都按命中控件重查」的语义
	//      失真——忙闲态属应用级 API，是另一条设计题。
	//    ★ `SizeWE` / `SizeNS` 是**同一族的两个方向**（分割条横竖各一），要补就一起补：
	//      只补一半，下一个加水平分割条的调用方仍然只能裸调 `::SetCursor`。
	SizeWE   = 2,   ///< 左右拉伸（垂直分割条悬停）
	SizeNS   = 3,   ///< 上下拉伸（水平分割条悬停）
	SizeNWSE = 4,   ///< 左上-右下拉伸（角部）
	SizeNESW = 5,   ///< 右上-左下拉伸（角部）
	SizeAll  = 6,   ///< 四向移动（可拖动面板/浮层）
	Hand     = 7,   ///< 手形（可点链接 / 可点路径）
	Cross    = 8,   ///< 十字（精确取点）
};

}
