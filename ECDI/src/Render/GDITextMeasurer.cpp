#include "Render/GDITextMeasurer.h"

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Core/UTF8.h"   // ★ Phase 29 △2：CodepointIndexToByteOffset（startCp → 字节偏移）
#include "Platform/Win32/Win32RenderContext.h"   // ★ Phase 26：Initialize 取 HWND（同 GDIBackend 先例）

#include <algorithm>
#include <cmath>
#include <limits>   // ★ Phase 29 △2：numeric_limits（码点数上界探测）
#include <memory>   // ★ Phase 31 △1：unique_ptr（OTM 超长时的对齐堆缓冲 RAII）
#include <new>      // ★ Phase 31 △1：std::align_val_t / operator new(size, align)（对齐分配）
#include <string>

namespace ECDI{

GDITextMeasurer::~GDITextMeasurer()
{
	// ★ 诊断（Phase 26 D26-5 基准的「计数打印」）：退出时一行汇总——供**人工性能对照**读数
	//   （GDI warm 阶段的靶子 = 测量缓存未命中是否随滚动线性增长）。★ `Info` 级：无调试器
	//   接收 `OutputDebugStringW` 时几乎零成本。
	Logger::Log(LogLevel::Info,
	            L"GDITextMeasurer: measureCacheMiss=" + std::to_wstring(m_measureCacheMissCount)
	            + L" measureCacheSize=" + std::to_wstring(m_measureCache.size())
	            + L" fontCacheSize=" + std::to_wstring(m_fontCache.size()));

	// D1：字体缓存统一清理（GDI 对象 10,000 上限纪律）
	for (auto& entry : m_fontCache)
	{
		DeleteObject(entry.second);
	}
	m_fontCache.clear();
}

void GDITextMeasurer::Initialize(const PlatformRenderContext& context)
{
	// ★ Phase 26（D-8 闭合）：与 GDIBackend::Initialize 同法——取窗口 HWND。
	//   测量基准 DPI 由此变为**窗口 DPI**（此前 = 屏幕 DC 的 LOGPIXELSX）。
	m_hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();
}

HFONT GDITextMeasurer::GetOrCreateFont(const Font& font, int dpi)
{
	// ★ Phase 20（△21）：与 GDIBackend 同逻辑，但**基准 DPI 不同**——本类"纯测量零 hwnd"，
	//   基准 = **调用点传入的测量 DC 的 `LOGPIXELSX`**（屏幕 DC）。
	//   测量链路"自己进、自己出"用同一基准 ⇒ 返回值对基准 DPI **近似不敏感**（初设 §2.2.4）；
	//   与渲染侧（窗口 DC 基准）的差异是**已记的近似**（详设 O1）。
	// D1：缓存键 = size + family + **dpi**（键隔离——与 GDIBackend 同理）
	const int effectiveDpi = (dpi > 0) ? dpi : 96;

	const auto key = std::make_tuple(font.size, font.family, effectiveDpi);
	auto it = m_fontCache.find(key);
	if (it != m_fontCache.end())
	{
		return it->second;
	}

	// D3：CreateFontIndirectW + LOGFONTW（零初始化）
	LOGFONTW lf{};
	// ★ Phase 20：DIP → 物理像素（float 口径）——使本地 HFONT 与渲染侧同尺度
	lf.lfHeight = -static_cast<LONG>(std::lround(font.size * effectiveDpi / 96.0));
	lf.lfCharSet = DEFAULT_CHARSET;                             // 关键：中文/Unicode 正常
	lf.lfWeight = FW_NORMAL;

	if (!font.family.empty())
	{
		// D3 约束 5：family UTF-8 → UTF-16，封闭在平台层
		const std::wstring wideFamily = UTF8ToWide(font.family);
		// D3 约束 7：LF_FACESIZE 长度限制 + 结尾 L'\0'（(std::min) 括号抑制 Windows 的 min 宏）
		const size_t length = (std::min)(wideFamily.size(), static_cast<size_t>(LF_FACESIZE - 1));
		wideFamily.copy(lf.lfFaceName, length);
		lf.lfFaceName[length] = L'\0';
	}

	HFONT hfont = CreateFontIndirectW(&lf);
	if (!hfont)
	{
		return nullptr;   // 决策 30：创建失败跳过（不缓存失败）
	}

	m_fontCache.emplace(key, hfont);
	return hfont;
}

Size GDITextMeasurer::MeasureText(const Font& font, const std::string& text)
{
	// ★ Phase 26（△9a）：**测量基准 DPI = 窗口 DPI**（与 GDIBackend 同基准——D-8 闭合）。
	//   ★ dpi 不再来自测量 DC ⇒ **命中缓存时可完全跳过 GetDC**。
	int dpi = GetDpiForWindow(m_hwnd);
	if (dpi <= 0)
	{
		dpi = 96;   // fail-safe（无窗口 / 失败——与 DpiConversion 一致）
	}

	// ★ Phase 26（△9b）：查测量结果缓存（★ 在 GetDC **之前**）。
	//   键 = (text, size, family, **dpi**)——★ 含 DPI（跨屏自动失效）。
	const auto cacheKey = std::make_tuple(text, font.size, font.family, dpi);
	const auto cacheIt = m_measureCache.find(cacheKey);
	if (cacheIt != m_measureCache.end())
	{
		return cacheIt->second;   // ★ 命中 ⇒ 零 GetDC / 零 SelectObject / 零 GetTextExtentPoint32W
	}
	++m_measureCacheMissCount;

	// D2：帧无关测量——GetDC(NULL) 临时屏幕 DC（仅测量，不承担绘制职责）
	Size result{};

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return result;
	}

	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		// D2 补充 3：SelectObject 后恢复原字体（GDI 纪律）
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		const std::wstring wideText = UTF8ToWide(text);
		SIZE extent{};
		if (!wideText.empty() && GetTextExtentPoint32W(measureDC, wideText.c_str(),
		                                               static_cast<int>(wideText.size()), &extent))
		{
			// ★ Phase 20：物理像素 → **DIP**（`TextMeasurer` 的接口语义）。
			//   保留 **float**——测量链路口径（详设 §3.5：**不得**与 `PixelsToDip` 的整数口径合并）。
			const float toDip = 96.0f / static_cast<float>((dpi > 0) ? dpi : 96);

			result.width = static_cast<float>(extent.cx) * toDip;
			result.height = static_cast<float>(extent.cy) * toDip;
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);

	// ★ Phase 26（△9b）：写缓存（★ 空文本也是合法测量结果——一并缓存）
	if (m_measureCache.size() >= kMaxMeasureCache)
	{
		m_measureCache.clear();   // ★ 上限 ⇒ 清空（O(1)，防无界增长）
	}
	m_measureCache.emplace(cacheKey, result);
	return result;
}

TextFit GDITextMeasurer::FitText(const Font& font, const std::string& text,
                                std::size_t startCp, float maxWidth)
{
	// ★ Phase 29 △2/D29-Ⅳ：**原生**实现——一次 `GetTextExtentExPointW` 同时拿宽度与 wchar fit，
	//   再按三步换算折回码点口径。★ 不走 `TextMeasurer.h` 的默认二分体（那是兼容 fallback）。
	const std::size_t totalCp = ByteOffsetToCodepointIndex(text, text.size());
	if (startCp >= totalCp)
	{
		return TextFit{ 0, 0.0f };   // 窗口在串尾之后（含空串）
	}

	// ★ 与 MeasureText 同基准：`GetDpiForWindow`（★ 不读 m_measureCache——fit 的键含 startCp，
	//   另建缓存条目会与测量缓存语义混淆；缓存化留给批二/三按需再加）。
	int dpi = GetDpiForWindow(m_hwnd);
	if (dpi <= 0)
	{
		dpi = 96;   // fail-safe（与 MeasureText 同口径）
	}

	// ★ 三步换算（UTF-8 字节 → 码点偏移 → UTF-16 wchar 窗口）：
	//   ① startCp 码点 → 字节偏移（切片起点）
	//   ② 切片 → `UTF8ToWide`（**唯一一次**转换——本方法内不重复转换，C29-9 记为已知债务 O4）
	const std::size_t byteStart = CodepointIndexToByteOffset(text, startCp);
	const std::wstring wide = UTF8ToWide(text.substr(byteStart));
	if (wide.empty())
	{
		return TextFit{ 0, 0.0f };
	}

	// ★ 物理像素口径的宽度上限（DIP → px）。★ **必须截断（floor）而非 ceil**（批三 E8 勘误）：
	//   ceil 会把像素上限顶到 DIP 限宽之上（limit=26.25 DIP ⇒ ceil=27px ⇒ 放行 27px 前缀，
	//   折回 DIP = 27 > 26.25 = **违反「width 恒不超 maxWidth」契约**）；截断后
	//   `px ≤ floor(limit·dpi/96) ⟺ px·96/dpi ≤ limit`（px 为整数）⇒ GDI 接受集与
	//   DIP 口径在**任意 DPI 精确相等**，且宁少一码点不超宽（与代理对回退同保守方向）。
	//   ★ 该缺陷由 T29-FIT-2 的 DIP oracle 在缩放 100%（dpi=96）下暴露——违规区是否被
	//   采样踩中取决于 DPI，故此前 125% 缩放时未触发（测试无错，DPI 变化揭了它）。
	const float limit = (std::max)(0.0f, maxWidth);
	const LONG maxPx = static_cast<LONG>(limit * static_cast<float>(dpi) / 96.0);

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return TextFit{ totalCp - startCp, 0.0f };   // ★ 无 DC ⇒ 放行全串（同 FontEngine 无 face 口径）
	}

	TextFit result{ totalCp - startCp, 0.0f };   // 兜底 = 放行全串
	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		SIZE extent{};
		INT fitW = 0;
		// ★ `GetTextExtentExPointW` 的 maxExtent **接受 LONG 像素**；fitW = 放得下的 wchar 数。
		//   ★ **不设 `DT_` 之类旗标**（本函数无旗标参数）；`lpdz` 传 nullptr ⇒ 不做逐字符定位。
		if (GetTextExtentExPointW(measureDC, wide.c_str(), static_cast<int>(wide.size()),
		                          maxPx, &fitW, nullptr, &extent))
		{
			// ── D29-Ⅳ 三步换算：wchar fit → 码点数 ──
			//  ① ★ 末位是**高代理**（0xD800–0xDBFF）⇒ 停在代理对中间 ⇒ −−fitW
			//    （代理对不可拆分——C29-2；宁少一个码点，不可产出半个代理对）。
			INT fit = fitW;
			if (fit > 0)
			{
				const wchar_t last = wide[static_cast<std::size_t>(fit) - 1];
				if (last >= 0xD800 && last <= 0xDBFF)
				{
					--fit;
				}
			}
			//  ② 线性计数 [0, fit) 内的**低代理**（0xDC00–0xDFFF）——每代理对 2 wchar = 1 码点
			//     ⇒ cpFit = fitW − 低代理数。
			std::size_t lowSurrogates = 0;
			for (INT k = 0; k < fit; ++k)
			{
				const wchar_t c = wide[static_cast<std::size_t>(k)];
				if (c >= 0xDC00 && c <= 0xDFFF)
				{
					++lowSurrogates;
				}
			}
			const std::size_t cpFit = static_cast<std::size_t>(fit) - lowSurrogates;

			//  ③ 码点口径 + **消费段宽**。★ 宽度用 `extent`（= 整个串的 extent，非 fit 前缀）——
			//     不可用：extent 是全串宽。⇒ 宽度取 **消费前缀**的 `GetTextExtentPoint32W`
			//     （仅在 fit < 全长时；否则 extent 本身即答案——省一次原生调用）。
			float width = 0.0f;
			if (fit >= static_cast<INT>(wide.size()))
			{
				width = static_cast<float>(extent.cx) * (96.0f / static_cast<float>(dpi));
			}
			else
			{
				SIZE prefixExtent{};
				if (GetTextExtentPoint32W(measureDC, wide.c_str(), fit, &prefixExtent))
				{
					width = static_cast<float>(prefixExtent.cx) * (96.0f / static_cast<float>(dpi));
				}
			}
			result = TextFit{ cpFit, width };
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);

	// ★ 防御：cpFit 不得越过串尾（fitW 上界为 wide.size() ⇒ 天然成立，此处仅防 fitW 异常）
	if (result.fitCp > totalCp - startCp)
	{
		result.fitCp = totalCp - startCp;
	}
	return result;
}

float GDITextMeasurer::LineHeight(const Font& font)
{
	// ★★ Phase 31（D31-A）：行高口径 = **hhea 行框**（asc − desc + lineGap）
	//   经 `GetOutlineTextMetricsW` 的 `otmMac*` 取（= hhea 缩放值——12/12 字体误差 0.00 px）
	//   ★ **不再用 `tmHeight`**：那是字体映射器的产物（初设 §2.2 证其不可由 FT 复现——
	//     差值有正有负、随字号放大到 +9 px，且无单一候选式可普遍复现）
	float height = font.size;   // 兜底：字号（**已是 DIP**——与返回值单位一致；契约 C31-3）

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return height;
	}

	// ★ Phase 26（△9a）：与 MeasureText 同一基准——**窗口 DPI**（D-8 闭合）
	int dpi = GetDpiForWindow(m_hwnd);
	if (dpi <= 0)
	{
		dpi = 96;   // fail-safe
	}

	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		// ★ Δ14：经**注入缝**调用（生产恒为真实 API——见头文件契约）
		const UINT otmSize = m_outlineTextMetrics(measureDC, 0, nullptr);

		if (otmSize > 0)
		{
			// ★ 对齐保证（详设评审 P3）：`BYTE[]` **不保证**满足 `OUTLINETEXTMETRICW`
			//   的对齐要求 ⇒ 栈路径 `alignas`、堆路径按对齐分配（**不得**依赖栈帧偶然对齐）
			//   ★ otmSize 含尾随字符串区（族名/全名/样式名——实测 256–342 B）
			alignas(OUTLINETEXTMETRICW) BYTE stackBuf[1024];
			BYTE* buf = stackBuf;
			std::unique_ptr<BYTE[], void(*)(BYTE*)> heapBuf(
				nullptr, [](BYTE* p){ ::operator delete(p); });

			if (otmSize > sizeof(stackBuf))
			{
				// ★ 超长（极端字体名）⇒ 堆分配，**仍保对齐**
				heapBuf.reset(static_cast<BYTE*>(
					::operator new(otmSize, std::align_val_t(alignof(OUTLINETEXTMETRICW)))));
				buf = heapBuf.get();
			}

			if (m_outlineTextMetrics(measureDC, otmSize,
			                         reinterpret_cast<LPOUTLINETEXTMETRICW>(buf)))
			{
				const OUTLINETEXTMETRICW* otm =
					reinterpret_cast<const OUTLINETEXTMETRICW*>(buf);

				// ★★ 字段读法（契约 C31-10——**受控实测结论**，勿改）：
				//   `otmMacAscent` / `otmMacDescent` 声明为 **int**（wingdi.h:2565/2566）
				//   `otmMacLineGap` 声明为 **UINT**，但 GDI 实际存的是 **int32 补码**
				//   ⇒ **按 int 直读即正确**（负数自动还原）。
				//   实据（本机受控合成——patch 字体副本的 hhea.lineGap，px=20/upem=2048）：
				//     patch −200  ⇒ GDI 0xFFFFFFFE ⇒ (int) −2（期望 −1.95）✓
				//     patch −1000 ⇒ GDI 0xFFFFFFF6 ⇒ (int) −10（期望 −9.77）✓
				//   ★ **不得**做「> 0x7FFF ⇒ 减 65536」式重解释——那在 −200 时会给出 −65538（错）
				//   ★ 符号约定：ascent > 0 · descent < 0 · lineGap 通常 ≥ 0 可负
				//     ⇒ 行框高 = asc − desc + gap（desc 为负 ⇒ 实际是加其绝对值）
				const int macAsc  = otm->otmMacAscent;
				const int macDesc = otm->otmMacDescent;
				const int macGap  = static_cast<int>(otm->otmMacLineGap);

				const int box = macAsc - macDesc + macGap;

				if (box > 0)   // ★ 退化字体防御（零/负行框 ⇒ 回退 font.size——契约 C31-3）
				{
					// ★ Phase 20：物理像素 → DIP（保留 float——测量链路口径）
					height = static_cast<float>(box)
						* (96.0f / static_cast<float>((dpi > 0) ? dpi : 96));
				}
			}
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return height;
}

}
