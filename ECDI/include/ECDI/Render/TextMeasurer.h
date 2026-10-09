#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/UTF8.h"   // ★ Phase 29 批一（△1）：默认 FitText 体的码点↔字节换算

#include <algorithm>   // ★ Phase 29（△1）：默认 FitText 体的 (std::max)
#include <cstddef>
#include <limits>      // ★ Phase 29（△1）：默认 FitText 体的 numeric_limits（码点数上界探测）
#include <string>

namespace ECDI {

class PlatformRenderContext;   // 前置声明（Initialize 参数 const&——零 include 依赖，Phase 26）

	/// @brief 断行适配结果（★ Phase 29 批一 △1：D29-Ⅲ **精确契约**冻结）
	/// @details
	/// 语义在**码点**口径（不是字节、不是 wchar）：
	///   - `fitCp` = **从 `startCp` 起实际可消费的码点数**（**相对数**——非绝对结束位置）。
	///     例：text = "A B C D E"（5 个码点）、`startCp` = 2、`fitCp` = 2 ⇒ 消费 "C D"。
	///   - `width` = **已消费段**的测量宽（**DIP**——与 `MeasureText` 同单位口径）。
	///   - ★ **`fitCp` 永不落在 UTF-8 字节序列中间**（契约 C29-1）——换算即保证。
	///   - ★ **surrogate pair 不可拆分**（契约 C29-2）——UTF-16 侧实现须退到完整代理对边界。
	struct TextFit
	{
		std::size_t fitCp = 0;   ///< 从 startCp 起可消费的**相对**码点数
		float width = 0.0f;      ///< 已消费段测量宽（DIP）
	};

	/// @brief 文本测量能力接口（独立于 RenderingBackend，路线 X 定案）
	/// @details
	/// 只知道 Font + text → 尺寸/行高；不接触 Widget/PaintContext/RenderCommand；
	/// 与 RenderingBackend **无继承关系**（正交能力接口，测试类可同时实现二者——纯测试便利）。
	class TextMeasurer {
	public:
		virtual ~TextMeasurer() = default;

		/// @brief 平台上下文注入（Phase 26：与 RenderingBackend::Initialize **结构对称**）
		/// @details ⚠️ **结构对称 ≠ 职责相同**：本方法**可选**（默认空实现即合法——无需平台
		///          上下文的实现者保持零改动）；`RenderingBackend::Initialize` 是**必须**的
		///          （10 个纯虚的实现依赖平台句柄）。**不要**因两处调用相邻就推断生命周期语义等价。
		///          GDITextMeasurer / FreeTypeTextMeasurer 覆盖它：取窗口 DPI ⇒ 测量基准 = 窗口 DPI
		///          （闭合审计 D-8：测量链与渲染链同基准）。
		virtual void Initialize(const PlatformRenderContext& context) {}

		/// @brief 测量文本尺寸（控件对齐偏移计算依赖此，D5 职责确认）
		/// @return 文本尺寸——★ **单位恒为 DIP**（Phase 20 Q2；与 `Font::size` 同源）。
		///         实现内部按「基准 DPI 换算进、同一 DPI 折回」的自洽方式产出
		///         （详见 Phase 20 初步设计 §2.2），故返回值对基准 DPI 近似不敏感。
		virtual Size MeasureText(const Font& font, const std::string& text) = 0;

		/// @brief 断行适配：**从 `startCp` 起最多能放进 `maxWidth` 的码点数**（★ Phase 29 △1）
		/// @details
		/// ★ **职责边界（契约 C29-1 / 盯防①）**——本方法**不理解**空格 / CJK / 连字符 / 禁则：
		///        「**断在哪**」属布局引擎（`TextWidget` 的换行路径），本方法只答
		///        「**从 startCp 最多放多少**」。禁设语义即防 API 失控。
		/// @param font   字体
		/// @param text   ★ **全串**（`startCp` 是全串的**绝对**码点索引；实现自行切窗口）
		/// @param startCp 起始码点索引（绝对；**>= 码点总数 ⇒ 返回 {0, 0}**）
		/// @param maxWidth 可用宽（DIP，**< 0 视作 0** ⇒ 至少一个码点也放不下时返回 `{0, 0}`）
		/// @return `TextFit`：`fitCp` = **相对**可消费码点数；`width` = 消费段宽（DIP）
		/// @note ★ **默认体 = 兼容 fallback**（D29-Ⅶ）——码点二分 + `MeasureText(切片)`，
		///       **仅**保证既有自定义 `TextMeasurer` / 测试派生类**零改动**即可工作，
		///       **非** GDI / FT 生产性能路径（substring 构造成本 O(len)×O(log)，已记录在案）。
		///       GDI 生产 = `GDITextMeasurer::FitText` 原生覆写；FT 生产 =
		///       `FreeTypeTextMeasurer::FitText` → `FontEngine::FitText`（advance memo 累积）。
		///       **调用侧不得依赖默认体的性能**（盯防⑦）。
		virtual TextFit FitText(const Font& font, const std::string& text,
		                        std::size_t startCp, float maxWidth)
		{
			// ★ 默认体（内联于公共头 ⇒ 自包含：只依赖自身 `MeasureText` + `Core/UTF8.h` 换算）。
			//   码点总数 = `ByteOffsetToCodepointIndex(text, text.size())`（走完整串 ⇒ 计数）。
			//   ★ **实施勘误**（批一）：此处原写 `CodepointIndexToByteOffset(text, SIZE_MAX)`——
			//   那是「码点索引 → **字节偏移**」且超界时**钳制到 `text.size()`** ⇒ 返回的其实是
			//   **字节数**，在 UTF-8 多字节串上把码点数放大 2~3 倍（探针实测 `cp=9 bytes=9` 起
			//   每字符 +2 字节），导致 `fitCp` 越界。**换算方向必须成对使用**。
			const std::size_t totalCp = ByteOffsetToCodepointIndex(text, text.size());
			if (startCp >= totalCp)
			{
				return TextFit{ 0, 0.0f };   // 窗口在串尾之后（含空串）
			}

			// ★ 「至少能放一个」是二分的前置不变式：若连**首码点**都放不下 ⇒ 返回 0
			//   （否则二分区间无解）。单码点宽 = 该码点**单独**测一次——O(1) 次 MeasureText。
			const std::size_t firstEnd = CodepointIndexToByteOffset(text, startCp + 1);
			const std::string firstCpText = text.substr(
				CodepointIndexToByteOffset(text, startCp), firstEnd - CodepointIndexToByteOffset(text, startCp));
			if (MeasureText(font, firstCpText).width > (std::max)(0.0f, maxWidth))
			{
				return TextFit{ 0, 0.0f };
			}

			// ★ 码点二分：找**最大**可消费前缀（谓词「前缀宽 <= maxWidth」单调——GDI/FT 口径
			//   均按 advance 累加，浮点误差不改变单调性到足以翻转比较的量级）。
			std::size_t lo = 1;                                  // 已知可消费的码点数
			std::size_t hi = totalCp - startCp;                   // 候选上界（**开区间**：上界本身待验）
			while (lo < hi)
			{
				const std::size_t mid = lo + (hi - lo + 1) / 2;   // ★ 上取整 ⇒ 无死循环（lo 必推进）
				const std::size_t byteStart = CodepointIndexToByteOffset(text, startCp);
				const std::size_t byteEnd = CodepointIndexToByteOffset(text, startCp + mid);
				if (MeasureText(font, text.substr(byteStart, byteEnd - byteStart)).width
				    <= (std::max)(0.0f, maxWidth))
				{
					lo = mid;
				}
				else
				{
					hi = mid - 1;
				}
			}

			const std::size_t byteStart = CodepointIndexToByteOffset(text, startCp);
			const std::size_t byteEnd = CodepointIndexToByteOffset(text, startCp + lo);
			const Size consumed = MeasureText(font, text.substr(byteStart, byteEnd - byteStart));
			return TextFit{ lo, consumed.width };
		}

		/// @brief 字体行高 —— ★ **单位恒为 DIP**
		/// @details ★★ **语义（Phase 31 选定）**：本方法返回「**该字体在该字号下，ECDI
		///          逐行排版所用的行框高度（行推进量）**」——即相邻两行**基线**的距离，
		///          也是「逐行 y 步进」与「**光标高度**」的唯一依据
		///          （★ 光标高 = 行框高——**不是**「行框内另有偏移」）。
		///          ★★ **计算口径（Phase 31 定义——两侧各自独立算出同一个数）**：
		///          ```
		///          行高 = (ascender − descender + lineGap) × fontSizePx / unitsPerEm
		///          ```
		///          · **GDI 实现** → `GetOutlineTextMetricsW` 的
		///            `otmMacAscent − otmMacDescent + otmMacLineGap`
		///            （★ 实测即 hhea 的缩放值——12/12 字体误差 0.00 px，见 phase31 初设 §2.6）
		///          · **FT 实现** → `face->size->metrics.height`（★ 实测本就是该口径）
		///          ⇒ ★ **两后端应给出同一个数**；允许的差异仅为**舍入**：**≤1 px**
		///             （@96dpi；两侧整数/26.6 定点的舍入方向之差，**不随字号增长**）。
		///             ——**该容差是契约的一部分，不是测试的私设**。
		/// @note ★ **基线不是本方法的职责**（`pos.y` = 字符单元顶边；GL 侧自行下移一个
		///       `FontEngine::Ascent`——`GLRenderer.cpp:215`）——两者**有关联但不是同一个概念**：
		///       **行推进量 / 字形垂直定位 / 光标高度**是三个概念（详见 phase31 详设 D31-E）。
		///       ★ 本方法是 **ECDI 的排版行框参数**，**不是**「后端所有字体度量的统一替代品」。
		/// @note 回退（三处统一为 `font.size`）：缺 face / 字体无 `hhea` 表 /
		///       `unitsPerEm == 0` / 查询失败 / 计算出的行框非正。
		virtual float LineHeight(const Font& font) = 0;
	};

}
