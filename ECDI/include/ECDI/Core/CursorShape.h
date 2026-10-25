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
};

}
