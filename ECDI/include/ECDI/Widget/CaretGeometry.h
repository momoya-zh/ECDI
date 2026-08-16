#pragma once

#include "ECDI/Core/Rect.h"

namespace ECDI{

/// @brief 文本插入点几何（7.1.3 输入层抽象——"光标不是点，是矩形区域"）
/// @details 语义放大（GPT）：光标位置是通用能力——单行/多行/富文本/代码编辑器都需要，
/// 不只 IME。由 TextBox 输出（唯一生产者），经 Window 转发给平台层消费。
/// 领域落点：Widget/ 目录（非 Core——迁 Core 条件 = 3~4 个独立子系统使用）。
/// 扩展预留（注释记录，非现在实现）：baseline——部分输入法候选框按基线定位。
struct CaretGeometry{
	Rect rect;	///< 插入点矩形（客户区坐标：x/y = 光标顶部 + width/height = 光标尺寸）

	/// @brief 光标**逻辑可见性**（存在 ≠ 可见——GPT 三轮语义精化）
	/// @note 注意（防后续维护者误解）：
	/// 此标志控制逻辑光标状态，**不是平台可见性**——
	/// Win32 系统 caret 永远隐藏（仅作 TSF/IMM 定位锚点），
	/// 用户看到的光标竖线由控件 OnPaint 自画。
	/// visible=false → 平台层 HideCaret（失焦/只读/代码补全弹窗统一处理）；
	/// **切勿在 true 时 ShowCaret**（系统 caret + 自画光标 = 双光标 Bug，8.5 闪烁尤其危险）。
	bool visible = true;
};

}
