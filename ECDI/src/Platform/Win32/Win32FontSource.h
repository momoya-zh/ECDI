#pragma once

#include "Render/FontSource.h"   // ★ Platform → Render 单向依赖（Render 不得反向依赖 Platform）

#include <functional>
#include <string>
#include <vector>

namespace ECDI{

/// @brief Win32 字体源（Phase 26）：解析 family → 系统字体文件路径
/// @details
/// ★★ **两条解析路径（v2.0 / REQ-09——补上族名维度）**，按序尝试：
///   1. **文件名直查**（`consola.ttf` 这类写法，Phase 26 起既有行为）——**保留且优先**，
///      保证既有调用点与 T26-6 ③ 的期望**逐位不变**（条 95⑤）；
///   2. **族名反查**（`Consolas` / `SimSun` / `Microsoft YaHei` / `Cascadia Mono`）——
///      经注册表 `HKLM\...\Fonts` 的「族名 → 文件名」表解析。
///   空 family ⇒ 内置默认（**含 CJK 的系统字体**，覆盖拉丁 + 中文——详设 L2）。
///
/// ★★ **为什么必须两条都留**（`REQ-09` 的实测结论，见 phase31 详设 §13.6 ④）：
///   两条后端链对 `Font::family` 的**语义不同**——GDI 链把同一个串送
///   `LOGFONTW.lfFaceName`（**族名**语义），FreeType 链只经本类定位文件。
///   此前只认文件名 ⇒ 写族名时 **GDI 命中而 GL 静默回退默认 face**；
///   若改成只认族名则**反方向重演**（写文件名时 GDI 回退）。
///   ⇒ **只有两条都接受**，同一个 `Font::family` 才能让两条链落到同一张 face。
///
/// ★ **仍然不做**（如实登记的局限，详设 §9 L1/L2 + 本条订正）：
///   - **样式变体区分**：`Consolas Bold` 这类请求会落到**常规体**（注册表里 Bold 是另一键，
///     本实现按「样式后缀」排除，宁取常规体也不误取粗体——见 `.cpp` 的匹配规则）；
///   - **中文族名**（`宋体` / `微软雅黑`）：注册表无单名键（只有 `SimSun & NSimSun` 这类
///     **英文复合键**），要支持需读 OpenType `name` 表别名 ⇒ **超出本件范围**，
///     实测消费者均写英文名（harness `Consolas` / ModelProbe `consola.ttf` / 本仓库测试英文名）；
///   - 多字体回退（N6）。
class Win32FontSource : public FontSource{
public:
	std::string ResolveFile(const std::string& family) const override;

	/// @brief family → 候选文件列表（Phase 36 批二 △9；详设 §2.2）
	/// @details **双源**：① 文件名直查（镜像 legacy 路径①，候选序首位，`directFile=true`）
	///          ② 注册表逐项（键名剥后缀 → ` & ` 拆项 → 每项独立判「== family / family+␣ 前缀」）。
	///          ★ 档位**不在此判定**（face 自身 `OS/2.usWeightClass`——样式词映射表废除）；
	///          注册表命中项经存在性检查（镜像 legacy :150-157）。
	std::vector<FontSourceCandidate> ResolveFileCandidates(const std::string& family) const override;

	/// @brief 默认字体文件路径（空 family 时用）
	/// @details 按**优先级列表**取第一个存在的（抗系统差异）——
	///          `msyh.ttc`（微软雅黑，含 CJK）→ `simsun.ttc`（宋体）→ `segoeui.ttf` → `arial.ttf`。
	/// @return **ANSI** 路径（同 `ResolveFile` 的编码契约）；**都不存在 ⇒ 空串**
	static std::string DefaultFontFile();

private:
	/// @brief 族名 → 文件名（**注册表反查**；`REQ-09` 的落点）
	/// @param familyU8 UTF-8 族名（**非空**——空 family 由 `DefaultFontFile` 处理）
	/// @return ANSI 文件名（**仅文件名**，不含目录；调用方拼字体目录）；未命中 ⇒ 空串
	/// @details ★ **匹配规则（宁缺毋滥，禁子串匹配）**——本机实测的反例：
	///          `宋体` 若允许子串匹配会命中 **`华文宋体` → STSONG.TTF**（**另一个字体**）；
	///          `SimSun` 的注册表键是复合形式 **`SimSun & NSimSun`**。
	///          故只接受三种强度：① `base == family`；② `base` 以 `"<family> "` 开头
	///          （收集 Bold/Italic 等**样式变体**，但不是无界包含）；③ `base` 以
	///          `"<family> & "` 开头（**复合族名的首项**，如 `SimSun & NSimSun`）。
	///          ★ **样式变体排序**：常规体优先（无 Bold/Italic/Light/Semibold/Black 等后缀），
	///          避免「请求族名却拿到粗体」（本件不做样式区分——见头文件局限声明）。
	///          ★ **不做** `find()` 式包含匹配、不做大小写归一以外的模糊匹配。
	/// @brief 注册表字体键枚举（Phase 36 批二 △10 提取）——新旧两路共用同一份循环
	/// @param onEntry 每条回调（键名 UTF-8、文件名 UTF-8；空文件名/非 REG_SZ 已过滤）
	static void ForEachFontRegistryKey(const std::function<void(const std::string&, const std::string&)>& onEntry);
	static std::string FamilyToFileName(const std::string& familyU8);
};

}
