#pragma once

#include <string>
#include <vector>

namespace ECDI{

/// @brief 字体源（Phase 26）：family（UTF-8）→ 字体文件路径（UTF-8）
/// @details ★ 平台无关的**内部件抽象**——`FontEngine` 持它，因而**不直接知道**
///          「系统字体目录在哪」。这是 Phase 26 详设 §3-③（外部评审 §6）的落点：
///          **字体发现 / 文件定位属平台或字体源层**，不进 `FontEngine` 的自身抽象。
///
///          ★ **为什么只有一个实现的接口**（答辩 YAGNI）：抽象的理由**不是「第二个实现」**，
///          而是**分层**——`src/Render/` **不得依赖** `src/Platform/Win32/`。
///          与 `PlatformRenderContext`（空基类 + `Win32RenderContext`）**同族**。
/// @brief 候选文件（Phase 36 批二 △8）——`ResolveFileCandidates` 的产出
/// @details ★ **directFile = true ⇒ FontEngine 豁免族名过滤**：显式文件名（`consola.ttf`）
///          的 face nameID1（'Consolas'）≠ 请求串，若不过滤豁免会被误杀回退 SimSun
///          （P0-① 的救回面）。注册表候选 = false（档位/斜体过滤照常）。
struct FontSourceCandidate{
	std::string filePath;    ///< `FT_New_Face` 可用全路径（编码契约同 `ResolveFile`）
	int faceIndexHint = 0;   ///< 复合键匹配项位置（0 起）——**仅扫描顺序提示**，加载时验证
	bool directFile = false; ///< 文件名直查候选（family 概念不适用 ⇒ 豁免族名过滤）
};


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

	/// @brief family → **候选文件列表**（Phase 36 批二 △8；三层规则第 1 层）
	/// @details ★ **带默认实现的新虚函数**（实现者零改动——沿 Phase 32 S7 / #60 先例）：
	///          默认 = 「`ResolveFile(family)` 非空 ⇒ 单候选」，并把该候选标为 **directFile**
	///          （legacy 语义 = 信任该文件 ⇒ FontEngine 豁免族名过滤）⇒ 非 Win32 源与
	///          测试替身（CountingFontSource 经此委托计数）行为与既有完全一致。
	/// @param family UTF-8 族名（非空；空 family 走 `ResolveFile` 默认路径，不经此接口）
	/// @return 候选列表（可能为空 ⇒ 调用方回退默认 face + 告警）；**档位不在候选层判定**
	///         （face 自身 `OS/2.usWeightClass` 提供——样式词映射表废除，详设 v1.1 P2-⑫）
	virtual std::vector<FontSourceCandidate> ResolveFileCandidates(const std::string& family) const
	{
		std::vector<FontSourceCandidate> out;
		const std::string path = ResolveFile(family);
		if (!path.empty())
		{
			out.push_back({path, 0, true});
		}
		return out;
	}
};

}
