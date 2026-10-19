#pragma once

#include "ECDI/Core/Image.h"

#include <algorithm>
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
///          ★★ **历史偏差（Phase 34 已消除）**：本段曾登记「GDIBackend 多处边转换仍用
///          `static_cast<LONG>`，仅在负分数边相差 1px」。实测该描述有两处不准：
///          ① 边级截断为 **22 处**（survey 复算：边级 trunc 22 · 全部 trunc 40）；
///          ② 另有 **16 处边级 std::lround** 未被提及。在职责确认 §9.2 记录的实测 DPI
///          档位与输入样本中，lround 与 floor 的分歧率为 **43.2%–49.8%**（该区间仅为本次
///          测量结果，不代表所有非整数 DPI 或任意输入分布；统计口径见该节：DIP 整数 0..80
///          × 五个 scale，含负坐标偏移，共 12960 样本/档）。其相对截断组（仅负坐标分歧）
///          高出一个数量级。⇒ Phase 34：边级统一 QuantizeEdge，尺寸由边相减，端点（D6）与
///          线宽/半径（N4）显式保留。
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


// ══════════════════════════════════════════════════════════════════════════
// Phase 33：圆弧原语（`DrawArc`）的两端共享契约
// ══════════════════════════════════════════════════════════════════════════
// ★★ **为什么弧几何也放这里**（同 §7.2 的定线理由，条 97 找同族先例）：
//   弧要被 **GDI（覆盖度网格 → PatchSurface）** 与 **GL（覆盖度网格 → 图集槽位）**
//   两端消费。若「点到弧段的距离」两端各写一遍，就会出现**两套几何语义**——
//   角度的正负、端点的归属、退化的钳制任何一处不一致，两端的弧就长得不一样，
//   而这**不会让任何编译失败**。⇒ 按本件既有形态：**共享代码优先、共享断言其次**。

/// @brief 圆弧参数的上界：`radius`/`width`（★ §3.3 N1）
/// @details 用途 = **在 `lround` 与 bbox 取整之前**把「极大有限值」挡住。
///          取 1e6 的理由：任何真实窗口的弧都远小于它，而它足以让后续所有中间量
///          （`pad`、`floor(cx ± pad)`、`lround(width)`）留在 `int` / `float` 的安全域内。
inline constexpr float kMaxArcExtent = 1.0e6f;

/// @brief 圆弧圆心坐标的上界（★ §3.3 N1）——同上，防 bbox 的 `floor` 结果溢出 `int`
inline constexpr float kMaxArcCoord = 1.0e6f;

/// @brief 单张补丁/网格的像素上界（★ §3.3 N1）
/// @details 沿 Phase 24 O2 的既有阈值（`GDIBackend.cpp:1460-1466` 的 2M 像素 one-shot
///          告警）——**同一个数字只用一处定义**，故在此集中。
inline constexpr long long kMaxArcPixels = 2'097'152LL;

/// @brief 弧的规范化参数（★ Phase 33：C33-4「退化与钳制表」的唯一实现）
/// @details `drawable == false` ⇒ 调用方**必须**直接跳过（不得再进几何判定）。
struct ArcParams
{
	float cx = 0.0f, cy = 0.0f;
	float radius = 0.0f;
	float startDeg = 0.0f;
	float sweepDeg = 0.0f;      ///< 已钳制到 [-360, 360]
	float halfWidth = 0.0f;     ///< 已钳制：hw = min(effW, 2*radius) / 2
	bool drawable = false;
	bool oversized = false;     ///< ★ v1.1：仅当因「极大有限值」被拒时为 true（§3.3 N2）
};

/// @brief 规范化（无效判定 + 数值安全带 + 扫角钳制 + 半宽钳制）—— C33-4 全表
/// @details 判定顺序（★ **顺序即契约**，逐条对应 C33-4）：
///   ① 任一浮点入参 `!isfinite` ⇒ 不可绘制（**先于一切算术**——防 NaN 传播进 fmod/lround）
///   ② `radius <= 0` ⇒ 不可绘制（**不取绝对值**——不静默改变输入语义）
///   ③ `width <= 0` ⇒ 不可绘制（沿 DrawLine 家族）
///   ④ `sweepDeg == 0` ⇒ 不可绘制
///   ⑤ `sweepDeg = clamp(sweepDeg, -360, 360)`（超一周**钳制**，不是多圈）
///   ⑥ ★★ **数值安全带**（见 §3.3）：`radius`/`width` 超 `kMaxArcExtent`、或
///      `|cx|`/`|cy|` 超 `kMaxArcCoord` ⇒ 不可绘制**且** `oversized = true`
///      （**必须先于 ⑦ 的 `lround`**——`lround` 对超范围浮点是「未指定值 + 可能置
///       `FE_INVALID`」，且 bbox 的 `floor` 结果要装进 `int`）
///   ⑦ `effW = max(1, lround(width))`；`hw = min(effW * 0.5f, radius)`
///      （★ **内半径非正 ⇒ 几何退化为扇形**——C33-4 的已知语义，不是「宽度 0 = 实心」的后门）
///   ⑧ `startDeg` **不归约**（保留原值——归约在 `ArcSegmentDistanceSq` 内部按 `mod 360` 做；
///      理由：**记录件**要用原值（C33-9）；★ **缓存键**则用 `Mod360` 后的值）
/// @note ① 与 ⑥ 的分工：① 管 NaN/±Inf（**算术之前**），⑥ 管**极大有限值**（**取整之前**）
///       ——两者**不可互相替代**。
inline ArcParams NormalizeArc(float cx, float cy, float radius,
                              float startDeg, float sweepDeg, float width) noexcept
{
	ArcParams p;
	p.cx = cx;
	p.cy = cy;
	p.radius = radius;
	p.startDeg = startDeg;
	p.sweepDeg = sweepDeg;

	// ① 非有限值：先于一切算术（否则 fmod / lround / floor 全部进入未定义或未指定行为）
	if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius)
		|| !std::isfinite(startDeg) || !std::isfinite(sweepDeg) || !std::isfinite(width))
	{
		return p;   // drawable == false
	}

	// ② ③ ④ 拒绝（不钳制）：这三档没有「合理的钳制目标」，静默改值会让调用方失去反馈
	if (radius <= 0.0f || width <= 0.0f || sweepDeg == 0.0f)
	{
		return p;
	}

	// ⑤ 超一周钳制（沿全库钳制家族：无观感意义且会引入重复覆盖）
	p.sweepDeg = (std::min)((std::max)(sweepDeg, -360.0f), 360.0f);

	// ⑥ 数值安全带（须先于 ⑦ 的 lround）
	if (radius > kMaxArcExtent || width > kMaxArcExtent
		|| std::fabs(cx) > kMaxArcCoord || std::fabs(cy) > kMaxArcCoord)
	{
		p.oversized = true;
		return p;   // drawable == false，但 reasons 可辨（后端据此记一次 Warning）
	}

	// ⑦ 线宽取整与半宽钳制（同 legacy 口径 max(1, lround)；内半径非正 ⇒ 扇形）
	const int effW = (std::max)(1, static_cast<int>(std::lround(width)));
	p.halfWidth = (std::min)(static_cast<float>(effW) * 0.5f, radius);

	// ⑧ startDeg 原样保留（见上）
	p.drawable = true;
	return p;
}

/// @brief 点 `(x, y)` 到**有限弧段**的距离平方（★ C33-1 + C33-2 的唯一实现）
/// @param p `NormalizeArc` 的产物（调用方须先确认 `p.drawable`）
/// @return 距离的平方（与 `DistSqToSegment` 同口径——开方留给调用方做比较时省掉）
/// @details 逐行规格（详设 §2 △1b，**15 行**）：
///   ① `radial <= 1e-6f`（圆心本身）⇒ **直接返回 `radius²`**：极角无定义（`atan2(0,0)`
///      未定义），而几何上「圆心到弧段的距离」**恒等于 `radius`**（弧段上任一点距圆心都是
///      `radius`）⇒ 圆心归属由**距离比较**自动决定（`radius <= hw` 时命中），全程不调 `atan2`。
///   ② 极角用 **`atan2f(dy, dx)`（y 不取负）**：屏幕坐标 y 向下时，它给出的**就是**
///      「3 点钟 = 0、顺时针为正」的角（C33-2 的定义）。
///      ★★ 这与「弧上点的重建式 `pq = center + radius*(cos, sin)`」**必须同号**——两处各反
///      一处，弧会沿水平直径**镜像**（形状/面积/接缝全不变 ⇒ 只有方位真值表能钉住它）。
///   ③ **越界吸附**：`delta > |sweep|`（点落在扫角之外）时，取**圆周角差更小**的那个端点。
///      ★ 为什么不是 `t = clamp(delta/|sweep|, 0, 1)`：那样在「点位于**起点之前**」时
///      `delta` 会绕到 ~355°、被钳到 `1` ⇒ 返回的是到**终点**的距离，**起点侧圆帽整片丢失**
///      （实测误判占带内子采样 8.40% ~ 25.02%）。
///      ★ 等价性：`min(圆周角差)` 与 `min(到两端点的欧氏距离)` 在圆上**恒等价**（二者只差
///      一个单调的 `cos`），故这里是**语义正确**而非近似。
///   ④ 无除法：`|sweep| == 0` 时 `sw == 0`，两支都退回 `s = 0`（不产生 0/0）。
/// @note `|sweep| = 360°` 无需特判：`delta ∈ [0,360)` 而 `sw = 360` ⇒ 越界分支**恒不进入**
///       ⇒ `q` 即 `p` 的径向投影，判据自然成为「到整圆的距离」`|radial - radius|`（C33-3②）。
inline float ArcSegmentDistanceSq(const ArcParams& p, float x, float y) noexcept
{
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kRadToDeg = 180.0f / kPi;

	const float dx = x - p.cx;
	const float dy = y - p.cy;
	const float radial = static_cast<float>(std::sqrt(double(dx) * double(dx)
		+ double(dy) * double(dy)));

	// ① 圆心本身：见上（几何上恒为 radius）
	if (radial <= 1e-6f)
	{
		return p.radius * p.radius;
	}

	// ② 极角（度）：y 不取负 ⇒ 顺时针为正
	const float a = static_cast<float>(std::atan2(dy, dx) * double(kRadToDeg));

	// 沿**扫描方向**度量的有向角差，归约到 [0, 360)（正负用同一规则 ⇒ 起点恒给 delta = 0）
	float delta = (p.sweepDeg >= 0.0f) ? (a - p.startDeg) : (p.startDeg - a);
	delta = std::fmod(delta, 360.0f);
	if (delta < 0.0f)
	{
		delta += 360.0f;
	}

	// ③ 弧内取径向投影；弧外吸附「圆周角差更小」的端点
	const float sw = std::fabs(p.sweepDeg);
	const float s = (delta <= sw)
		? delta
		: (((360.0f - delta) < (delta - sw)) ? 0.0f : sw);

	// ④ 重建弧上点（与 ② 同号——见上）
	// ★ `ang` 归约到 [0, 360)：`cos/sin` 本就以 360 为周期，故**几何语义零变化**；而
	//   归约让「同一弧段的等价写法」逐位相同——否则 (270,+90) 与 (0,-90) 在共享端点处
	//   分别算出 `ang = 360` 与 `ang = 0`，`cos` 的浮点舍入差会让 T33-3 的别名断言
	//   在边界采样点上偶发失败（那会把「等价类」变成「近似等价」）。
	float ang = static_cast<float>(
		(double(p.startDeg) + double((p.sweepDeg >= 0.0f) ? s : -s)) * (double(kPi) / 180.0));
	ang = std::fmod(ang, 2.0f * kPi);
	if (ang < 0.0f)
	{
		ang += 2.0f * kPi;
	}
	const float qx = p.cx + p.radius * std::cos(ang);
	const float qy = p.cy + p.radius * std::sin(ang);
	const float ddx = x - qx;
	const float ddy = y - qy;
	return ddx * ddx + ddy * ddy;
}


}   // namespace BackendContract

}
