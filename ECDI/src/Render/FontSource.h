#pragma once

#include <string>

namespace ECDI{

/// @brief 字体源（Phase 26）：family（UTF-8）→ 字体文件路径（UTF-8）
/// @details ★ 平台无关的**内部件抽象**——`FontEngine` 持它，因而**不直接知道**
///          「系统字体目录在哪」。这是 Phase 26 详设 §3-③（外部评审 §6）的落点：
///          **字体发现 / 文件定位属平台或字体源层**，不进 `FontEngine` 的自身抽象。
///
///          ★ **为什么只有一个实现的接口**（答辩 YAGNI）：抽象的理由**不是「第二个实现」**，
///          而是**分层**——`src/Render/` **不得依赖** `src/Platform/Win32/`。
///          与 `PlatformRenderContext`（空基类 + `Win32RenderContext`）**同族**。
class FontSource{
public:
	virtual ~FontSource() = default;

	/// @brief family → 字体文件路径
	/// @param family UTF-8 字体族名；★ **空串 = 系统默认**
	/// @return 字体文件路径——★ **该平台上 `FT_New_Face` 可直接使用的编码**
	///         （Win32 = **ANSI / 当前代码页**：FreeType 的 Windows 系统接口
	///          `builds/windows/ftsystem.c` 对收到的 `char*` 执行 `MultiByteToWideChar(CP_ACP,…)`）；
	///         ★ **找不到 / 该编码表达不了 ⇒ 空串**
	/// @details ★ 返回空串时由调用方决定「回退默认 face **并写告警日志**」——
	///          **不得静默加载另一个字体让用户以为指定成功**（详设 △2 / 外部评审 §11）。
	virtual std::string ResolveFile(const std::string& family) const = 0;
};

}
