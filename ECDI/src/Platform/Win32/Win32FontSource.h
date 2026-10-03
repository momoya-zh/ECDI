#pragma once

#include "Render/FontSource.h"   // ★ Platform → Render 单向依赖（Render 不得反向依赖 Platform）

#include <string>

namespace ECDI{

/// @brief Win32 字体源（Phase 26）：在系统字体目录内解析 family
/// @details
/// ★ **本 Phase 的解析 = 「family 视作文件名」直查**（如 `consola.ttf`）；
///   空 family ⇒ 内置默认（**含 CJK 的系统字体**，覆盖拉丁 + 中文——详设 L2）。
///
/// ★ **不做**（如实登记的局限，详设 §9 L1/L2）：
///   - 完整 family↔文件名解析（`EnumFontFamiliesExW`）——**GDI 侧仍按 family 名**，
///     故两侧语义**不完全对等**（N1 不承诺视觉等价）；
///   - 多字体回退（N6）。
///   ★ 重启条件 = 需要真实 family 名匹配时。
class Win32FontSource : public FontSource{
public:
	std::string ResolveFile(const std::string& family) const override;

	/// @brief 默认字体文件路径（空 family 时用）
	/// @details 按**优先级列表**取第一个存在的（抗系统差异）——
	///          `msyh.ttc`（微软雅黑，含 CJK）→ `simsun.ttc`（宋体）→ `segoeui.ttf` → `arial.ttf`。
	/// @return **ANSI** 路径（同 `ResolveFile` 的编码契约）；**都不存在 ⇒ 空串**
	static std::string DefaultFontFile();
};

}
