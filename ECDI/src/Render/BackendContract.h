#pragma once

#include "ECDI/Core/Image.h"

#include <cmath>
#include <cstddef>

namespace ECDI{

/// @brief **后端契约**（★ 审计 #62 / §7.2 定线的落点——GDI 与 GL 共用的可测断言/规则）
/// @details
/// ★★ **为什么需要这个件**（`framework-implementation-audit.md` §7.2）：GL 与 GDI 是
///   **两套独立实现**，各有各的守卫与降级——审计逐条核对后发现模式是「**GDI 有守卫/降级，
///   GL 没有**」，共 8 条（H2/H3/H4 + M13/M16/M17 + L10/L14）指向同一结论：
///   **两端的契约没有被同一份东西钉住**。⇒ 定线 = 把「能共用的一律共用」。
/// ★★ **形态选择（本件的存在理由）**：审计建议「抽成**共享的可测试断言**」。实测后收窄为
///   **共享代码优先、共享断言其次**——因为：
///   ① **共享代码强于断言**：两条断言各自写一遍，仍可能各自漂移；**同一份代码**不可能漂移
///      （谓词只有一份实现，两端调用同一符号）。⇒ 凡「判据本身是纯函数」的，抽成 `inline`
///      函数放本件，两端调用（H3 的图像可用性守卫即此形态）。
///   ② ★★ **自动化的 GL↔GDI 差分测试在本项目结构性不可行**——全库**无任何测试创建真实
///      WGL context**（`GLBackendTests.cpp` 只走初始化失败路径；Phase 26 详设亦把成功路径
///      列为**人工目视项 T26-9**）。⇒ 「同一命令序列两端产生等价结果」这类断言**写不出来**，
///      写成替身只会得到**假绿**。故本件只承载**平台无关**的规则，其正确性由**纯函数真值表**
///      用例覆盖（不需要 GL 环境 ⇒ 真正可跑）。
///   ③ 余下**无法共用**的（如取整口径在两端的历史差异）⇒ 在本件**写明规范规则**并把
///      偏差记账，而不是假装已统一（见 `QuantizeEdge` 的注释段）。
///
/// ★ 分层：`src/Render/` 内部头（**非公共 API**——`include/ECDI` 零改动）。
///   只依赖 `Core/Image.h` 与 `<cmath>` ⇒ 零平台依赖，两端都可用。
namespace BackendContract{

/// @brief 图像是否**可安全绘制**（★ 审计 H3：越界读的统一守卫）
/// @param image 待校验图像
/// @return true = 尺寸/行距/缓冲长度自洽，可逐行按 `row * stride` 读取
/// @details ★★ **这是 H3 的修复本体**：`image.pixels` 的读取方式是
///          `pixels.data() + j * stride` 再取 `row[i*4..i*4+3]` ⇒ **三个几何量必须自洽**：
///          ① `width > 0 && height > 0`；② `stride >= width * 4`（每行至少装得下本行 4 字节/像素）；
///          ③ `pixels.size() >= stride * height`（缓冲至少装得下 height 行）。
///          任一不成立，逐行读取即**越界读**（UB）。
///          ★ **审计原文**：修复前 **GDI 有这套守卫、GL 没有** ⇒ 同一 `DrawImage` 命令
///          「GDI 安全 no-op / GL 越界读」——**后端决定内存安全**。这是 §7.2 最严重的一条
///          （唯一的内存安全问题），故抽成**唯一实现**、两端同调。
///          ★ **语义 = 「不可绘制」而非「断言非法」**：`PaintContext::DrawImage` 是**公开 API**，
///          应用自造 `Image` 完全合法（可从外部解码器来）⇒ 非法几何参数的正确反应是
///          **安全跳过**（与 GDI 侧原 `return` 语义逐位一致；不抛、不崩、不写日志刷屏）。
/// @note **契约来源 = `Core/Image.h:12-15`** 的字段说明（`stride >= width*4`、
///       `pixels.size() >= stride*height`）——本函数是该**文字契约的代码化**，
///       两端从此不可能对它有第二套理解。
inline bool ImageIsDrawable(const Image& image) noexcept
{
	// ① 空图像（Image.h:12：width == 0 || height == 0 视为空图像，不产生绘制）
	if (image.width <= 0 || image.height <= 0)
	{
		return false;
	}

	// ② 行距至少容得下本行（Image.h:13）
	if (image.stride < image.width * 4)
	{
		return false;
	}

	// ③ 缓冲至少容得下 height 行（Image.h:14）
	//    ⚠️ 先升 size_t 再乘：int × int 在极端尺寸下会**溢出**（那本身也是越界的成因之一）
	const std::size_t needed =
		static_cast<std::size_t>(image.stride) * static_cast<std::size_t>(image.height);

	return image.pixels.size() >= needed;
}

/// @brief **矩形边的规范取整**（★ 审计 M17：口径定线的代码化）
/// @param edge 边坐标（可为负——水平滚动 / 越界绘制都是负坐标）
/// @return 该边所属的**像素索引**（`floor`）
/// @details ★★ **规范规则（M17 定线结论）**：
///          **边取 `floor`；尺寸由量化后的两条边相减导出，绝不独立取整。**
///          理由：矩形 `[x, x+w)` 覆盖的整数像素列是 `[floor(x), floor(x+w))`——
///          这是**区间语义**的正确量化；而 `width' = lround(w)` 与
///          `left' + width' != floor(x + w)` 会**自相矛盾**（同一条右边有两个值）。
///          ⇒ 本函数只回答「边落在哪个像素」，`floor` 对**正负号一致**（域是整数格点）。
///          ★ **为什么不用 `static_cast<int>`（向零截断）**：`static_cast<int>(-1.5f) == -1`
///          把 -1.5 归到像素 `[-1, 0)`——而它 5/6 的覆盖在 `[-2, -1)` ⇒ **符号相关的不一致**。
///          ★★ **已知偏差（如实记账，不假装已统一）**：`GDIBackend` 的多处边转换仍用
///          `static_cast<LONG>`（**向零截断**）⇒ 与本节规则**仅在「负的分数边」上相差 1px**
///          （正数、以及负整数不受影响）。**本批不改 GDI 的这四处**——那会改变**每一条
///          负坐标绘制**的像素输出（`ScaleRect` 在非 100% DPI 下天然产生分数边），
///          属需要独立目视回归的改动，不塞进缺陷批（沿「不把无关改动伪装成顺带修复」纪律）。
///          偏差已登记入 `roadmap-deferred.md` #62 行余量。
inline int QuantizeEdge(float edge) noexcept
{
	return static_cast<int>(std::floor(edge));
}

/// @brief 顶点吸附（GL 侧用：float 顶点须落在整数像素格上才能与 GDI 的整数 `RECT` 对齐）
/// @details ★ 等价于 `static_cast<float>(QuantizeEdge(v))`——**单一真相源**：
///          GL 的 `Snap` 从「自己写一个 `std::floor`」改为调本函数 ⇒
///          两端从此共用同一条边规则（M17 定线在 GL 侧的落地）。
/// @note 为什么 GL 需要它：`TextOutW` 只收整数坐标，字形恒落在整数网格；GL 若用分数顶点，
///       hinted 位图会在分数相位下重采样（实测「文字污染」）——详见 `GLRenderer::Snap` 原文。
inline float SnapVertex(float v) noexcept
{
	return static_cast<float>(QuantizeEdge(v));
}

}   // namespace BackendContract

}
