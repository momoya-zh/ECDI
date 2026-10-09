# Phase 31 · 窗口几何与行高口径 —— 详细设计（v1.1 · 实施规格）

> 来源：初设 `phase31-window-geometry-and-line-height-preliminary-design.md` **v1.1**（评审吸收 B-2：窗口几何 **PASS → 详设**；行高 **公共 hhea 口径**由需求评审拍板）
> 状态：**v1.2**（2026-10-08）——**批一（窗口几何）已实施**：△6–△13 落地，**三链 360/360 全绿**（MinGW / Clang / ClangCL），**M1 阻塞已解除**；批二/批三待实施（见 §9 实施回填）
> 定位：**实施规格**——把初设的 D31-A..F 落成**逐文件、逐行**的改动与精确测试装置。
> ★ 需求稿锁定的范围不变：**不做虚拟化 / 脏区 / 后端执行端 / OnPaint 纯绘制重构 / 富文本 / shaping**。
> ★★ **v1.1 三项实施前必办（评审给定，已处理）**：① **P1 浮点精度**——`SetBounds` 不再先截断到 int DIP，改走新增的 **float 重载**（△13）；② **P2 `otmMacLineGap` 语义**——经**受控合成实验**证其存 **int32 补码**（原稿「减 65536」在负值下**算错**）⇒ 改直读（C31-10）；③ **P3 缓冲区对齐**——`alignas` 保证。

---

## §1 详设必答（初设 D31-A..F 逐题钉死——本稿核心）

### D31-A ★★ 行高口径的**精确实现**（B-2 落地）

**统一定义（两侧各自独立算出同一个数）**：

```
LineHeight(DIP) = (hhea.ascender − hhea.descender + hhea.lineGap) × fontSizePx / unitsPerEm
```

| 链 | 实现 | 依据（实测） |
|---|---|---|
| **GDI** | `GetOutlineTextMetricsW` → `otmMacAscent − otmMacDescent + (int)otmMacLineGap`，× `96.0f / dpi` | ★ H1 实测 **12/12 字体、误差 0.00 px**——`otmMac*` 就是 hhea 的缩放值（初设 §2.6） |
| **FT** | `face->size->metrics.height`，× `96.0f / dpi` | ★ **零改动**——实测它**就是**该口径（与 hhea 推算的 BOXG 差 ≤ 0.5 px） |
| **RecordingBackend** | `return 14.0f;` | ★ **测试替身，零改动**（`RecordingBackend.h:113`） |
| **FakeTextMeasurer ×2 / CountingMeasurer / FixedWidthMeasurer** | `return 16.0f;` | ★ **测试替身，零改动** |

**GDI 侧的实现细节（`GDITextMeasurer::LineHeight` 改写）**：

```cpp
float GDITextMeasurer::LineHeight(const Font& font)
{
	float height = font.size;   // ★ 回退值（C31-3——两侧一致）

	HDC measureDC = GetDC(nullptr);
	if (!measureDC) return height;

	int dpi = GetDpiForWindow(m_hwnd);
	if (dpi <= 0) dpi = 96;

	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		// ★ Phase 31（D31-A）：行高口径 = hhea 行框（asc − desc + lineGap）
		//   经 GetOutlineTextMetricsW 的 otmMac* 取（= hhea 缩放值，初设 §2.6 H1 实测 12/12）
		//   ★ 不再用 tmHeight（那是字体映射器的产物——初设 §2.2 证其不可由 FT 复现）
		const UINT otmSize = GetOutlineTextMetricsW(measureDC, 0, nullptr);
		if (otmSize > 0)
		{
			// ★★ v1.1（评审 P3）：**对齐保证**——BYTE 数组不保证满足 OUTLINETEXTMETRICW
			//   的对齐要求 ⇒ 必须 alignas（不得依赖栈帧偶然对齐）
			//   ★ 栈上固定缓冲 + 超长回退堆分配（otmSize 含尾随字符串区，实测 256–342 B）
			alignas(OUTLINETEXTMETRICW) BYTE stackBuf[1024];
			BYTE* buf = stackBuf;
			std::unique_ptr<BYTE[]> heapBuf;
			if (otmSize > sizeof(stackBuf))
			{
				// ★ 堆路径同样保对齐：按 OUTLINETEXTMETRICW 对齐的分配
				//   （new BYTE[] 只保证 max_align_t；用 operator new + align_val_t 显式指定）
				heapBuf.reset(static_cast<BYTE*>(
					::operator new(otmSize, std::align_val_t(alignof(OUTLINETEXTMETRICW)))));
				buf = heapBuf.get();
			}

			if (GetOutlineTextMetricsW(measureDC, otmSize,
			                           reinterpret_cast<LPOUTLINETEXTMETRICW>(buf)))
			{
				const OUTLINETEXTMETRICW* otm =
					reinterpret_cast<const OUTLINETEXTMETRICW*>(buf);

				// ★★ v1.1（评审 P2——**受控实测后修正**）：
				//   `otmMacLineGap` 声明为 **UINT**，但 GDI 存的是 **int32 补码**
				//   ⇒ 直接按 **int** 读即为正确值（**不需要减 65536**）。
				//   实证据（本机受控合成——patch 字体副本的 hhea.lineGap，px=20/upem=2048）：
				//     patch −200  ⇒ GDI 0xFFFFFFFE ⇒ (int) = −2（期望 −1.95）✓
				//     patch −1000 ⇒ GDI 0xFFFFFFF6 ⇒ (int) = −10（期望 −9.77）✓
				//     patch −1    ⇒ GDI 0x00000000 ⇒ (int) = 0
				//   ★ 原稿「> 0x7FFF ⇒ 减 65536」在此**算出 −65538 / −65546（错）**
				//   ★ 前景：本机 385 个 face 中负 lineGap = **0** ⇒ 该分支靠**合成字体**取证
				const int macAsc  = otm->otmMacAscent;      // ★ 字段本身即 int（wingdi.h:2565）
				const int macDesc = otm->otmMacDescent;     // ★ 字段本身即 int
				const int macGap  = static_cast<int>(otm->otmMacLineGap);   // ★ 补码 ⇒ int 直读

				// ★ 符号约定（写进注释，避免后人只看到「防御性转换」不知前提）：
				//   ascent  > 0（基线以上高度）
				//   descent < 0（基线以下，**负值**——与 FreeType `metrics.descender` 同号）
				//   lineGap 通常 ≥ 0，可负（== hhea.lineGap 的符号语义）
				//   ⇒ 行框高 = asc − desc + gap（desc 为负 ⇒ 实际是加绝对值）
				const int box = macAsc - macDesc + macGap;
				if (box > 0)   // ★ 退化字体防御（零/负行框 ⇒ 回退）
					height = static_cast<float>(box) * (96.0f / static_cast<float>(dpi));
			}
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return height;
}
```

| # | 实现细节 | 理由 |
|---|---|---|
| **1** | **不再读 `tmHeight`** | §2.2 证其不可由 FT 复现（差值非常数、候选式不稳定） |
| **2** | **`otmMac*` 而非 `otmAscent/otmDescent`** | otmAscent/otmDescent 是**逻辑坐标**口径（含 DPI 缩放的另一种方式），otmMac* **直接对应 hhea**（H1 12/12 逐位） |
| **3** | ★★ **`otmMacLineGap` 按 `int` 直读**（v1.1 修订——评审 P2 受控实测后修正） | `otmMacLineGap` 声明为 `UINT`，但 GDI 存的是 **int32 补码** ⇒ **直接 `static_cast<int>` 即正确**。★ 原稿「> 0x7FFF ⇒ 减 65536」在真实负值下**算错**（实测 −200 ⇒ 该式得 −65538，正确为 −2） |
| **3b** | ★ **符号约定写进注释**（v1.1 新增——评审 P2） | ascent **> 0** · descent **< 0** · lineGap 通常 ≥ 0 可负 ⇒ **行框 = asc − desc + gap**（desc 为负 ⇒ 实际是加绝对值）。★ 不只留一个「防御性转换」让后人猜前提 |
| **4** | **`box <= 0` ⇒ 回退 `font.size`** | 退化字体防御（初设 C31-3）；★ 与 FT 侧 `face == nullptr ⇒ font.size` **对称** |
| **5** | ★★ **缓冲区对齐保证**（v1.1 修订——评审 P3） | `BYTE` 数组**不保证**满足 `OUTLINETEXTMETRICW` 的对齐 ⇒ 栈路径 `alignas(...)`、堆路径 `operator new(size, align_val_t(...))`。**不得依赖栈帧偶然对齐** |
| **5b** | **栈缓冲 1024 B + 超长回退堆** | OTM 结构含尾随字符串区（实测 256–342 B）；1024 B 覆盖绝大多数；**不无脑堆分配**（此路径可能每帧走） |
| **6** | **不缓存**（沿 Phase 26 v1.1「LineHeight 不加缓存」先例） | 最小面；★ 性能影响见 §8 O6 |
| **7** | ★ **失败接缝**（v1.1 新增——评审 P5.3） | 见 **△14**：内部函数指针缝（复刻 `SetDragFinishForTests` / `SetDesktopHookObserverForTests` 先例）⇒ T31-3 可**稳定注入 OTM 失败**，不依赖某系统字体恰好失败 |

### D31-B 一致性判据的**可测形式**

| 项 | 值 |
|---|---|
| **判据** | 同一**字体文件、字重、字号、DPI** 下，`GDITextMeasurer::LineHeight` 与 `FreeTypeTextMeasurer::LineHeight` 之差 **≤ 1 px**（@96dpi，按 DPI 线性缩放） |
| **实测依据** | 12 字体 × 8 字号（12..64 px）：GDI 推算值与 FT 现值之差**全表 ≤ 1.00 px**、**不随字号增长**（初设 §2.6） |
| **残差来源** | GDI `otmMac*` 为**整数**（Windows 逐字段 round），FT 为 **26.6 定点**（/64 取浮点）⇒ 差 = **两侧舍入方向之差**，上界 = 分量数（3）× 半像素 |
| **不要求** | ★ **逐位相等**（做不到——两侧的舍入路径不同，且 GDI 无 26.6 精度） |

### D31-C 窗口几何 API 的**精确语义**（B-2 窗口几何落地）

**语义四条**（初设 D31-C/D31-D 的落地形式——**全部写进 `@details`**）：

1. **屏幕坐标**（非客户区坐标）——`x/y` 是窗口左上角在**虚拟屏幕**上的位置，**可为负**（多屏布局）
2. **总尺寸**（含非客户区）——`width/height` = **含边框 + 标题栏**的整窗尺寸，**与 `Application::Create(width, height)` 同口径**（`Application.h:65-66`）
3. **恒 DIP**——`SetBounds` 的输入与 `GetBounds` 的输出**均为 DIP**；换算**只在平台边界**（`DpiConversion.h`）
4. **请求 vs 事实**——`SetBounds` = **请求**（系统可调整）；`GetBounds` = **系统当前事实**（如实读回）；**二者可不等且框架不补偿**

**实现（`Win32PlatformWindow`）**：

```cpp
void Win32PlatformWindow::SetBounds(const Rect& bounds)
{
	// C31-6：运行期 API——Show() 前 Warning + 忽略（同 Minimize 组）
	if (!m_shown)
	{
		Logger::Warning(L"SetBounds before Show(): ignored");
		return;
	}

	// ★★ v1.1（评审 P5.1）：非法输入**明确拒绝**（不静默猜测调用者意图）
	//   ① 非有限值（NaN / ±Inf）② 尺寸非正 ③ 超出 int 转换范围
	const bool finite = std::isfinite(bounds.x) && std::isfinite(bounds.y)
	                 && std::isfinite(bounds.width) && std::isfinite(bounds.height);
	if (!finite || bounds.width <= 0.0f || bounds.height <= 0.0f)
	{
		Logger::Warning(L"SetBounds: non-finite or non-positive bounds ignored");
		return;
	}
	constexpr float kMaxDip = 16777216.0f;   // 2^24：远超任何真实屏幕，防 int 转换溢出
	if (std::fabs(bounds.x) > kMaxDip || std::fabs(bounds.y) > kMaxDip
	    || bounds.width > kMaxDip || bounds.height > kMaxDip)
	{
		Logger::Warning(L"SetBounds: out-of-range bounds ignored");
		return;
	}

	const int dpi = GetDpiForWindow(m_hwnd);

	// ★★ v1.1（评审 P1）：**保留 float 精度直到最后一次取整**
	//   不得先 `static_cast<int>(bounds.x)` —— 那会在 DIP 端额外丢一次小数
	//   （如 100.8 DIP 会先变成 100 DIP，144 DPI 下物理坐标随之偏 1 px）
	//   ⇒ 走 DpiConversion 的 **float 重载**（同一舍入规则，见 △x）
	SetWindowPos(m_hwnd, nullptr,
	             DipToPixels(bounds.x, dpi),        // float 重载（保留精度）
	             DipToPixels(bounds.y, dpi),
	             DipToPixels(bounds.width, dpi),
	             DipToPixels(bounds.height, dpi),
	             SWP_NOZORDER | SWP_NOACTIVATE);   // ★ C31-8：一次提交位置 + 尺寸
}

Rect Win32PlatformWindow::GetBounds() const
{
	if (!m_shown)
	{
		Logger::Warning(L"GetBounds before Show(): empty rect");
		return Rect{};   // C31-6：同组运行期契约（空值语义）
	}

	RECT rc{};
	if (!GetWindowRect(m_hwnd, &rc))
		return Rect{};

	const int dpi = GetDpiForWindow(m_hwnd);

	// ★ 如实反映系统当前值（物理像素 → DIP，int 口径——此方向天然是整数像素）
	return Rect{
		static_cast<float>(PixelsToDip(rc.left, dpi)),
		static_cast<float>(PixelsToDip(rc.top, dpi)),
		static_cast<float>(PixelsToDip(rc.right - rc.left, dpi)),
		static_cast<float>(PixelsToDip(rc.bottom - rc.top, dpi))
	};
}
```

**★ 配套：`DpiConversion.h` 新增 float 重载（△13——评审 P1 的落点）**

```cpp
/// @brief DIP → 物理像素（★ float 输入重载——Phase 31）
/// @param dip DIP 值（**保留小数**——公共几何 API 的 `Rect` 是 float）
/// @details ★ 为什么需要它：整数重载会迫使调用方**先截断再换算**，在 DIP 端
///          多丢一次精度（144 DPI 下 100.8 DIP 与 100 DIP 差 1 px）。
///          本重载**保留 float 至最后一次取整** ⇒ 与整数重载**同一舍入规则**
///          （C10 half-away-from-zero）、**同一真相源**（不引入第二套换算）。
///          实现 = 把 DIP 放大 65536 倍取整（1/65536 DIP 分辨率）后复用
///          `RoundHalfAwayFromZero`——**舍入仍只出现在那一个函数里**。
/// @note ★ 与文本测量链路的区别不变（本件仍是几何/坐标口径，不服务测量）
inline int DipToPixels(float dip, int dpi)
{
	if (dpi <= 0) dpi = 96;

	constexpr long long kSub = 65536;   // 1/65536 DIP 分辨率
	const long long sub = std::llround(static_cast<double>(dip) * static_cast<double>(kSub));

	return RoundHalfAwayFromZero(sub * dpi, 96LL * kSub);
}
```

| # | 实现细节 | 理由 |
|---|---|---|
| **1** | **`SWP_NOZORDER`** | z 序归 `WindowLayer`（Phase 16 D11）——本方法**不碰** z 序（否则会打断 Desktop 驻留维护） |
| **2** | **`SWP_NOACTIVATE`** | M1 折叠/级联不应抢焦点；★ 与 `ApplyStartupSize`（`:191-193`）和 `WM_DPICHANGED`（`:649-653`）同款 |
| **3** | **`m_shown` 门控** | C31-6（运行期 API——`Show()` 前 Warning + 忽略）；★ 与 `Minimize/Maximize/Restore` 同组 |
| **4** | **`GetWindowRect`（非 `GetClientRect`）** | 语义 = **总尺寸 + 屏幕坐标**（C31 语义 1/2）；★ 与 `:438`（`WM_NCHITTEST` 内）同 API |
| **5** | ★★ **float 精度保留至最后一次取整**（v1.1 修订——评审 P1） | 不得先截断到 int DIP；走 `DpiConversion` 的 **float 重载**（同舍入规则、同真相源） |
| **6** | ★★ **非法输入明确拒绝**（v1.1 新增——评审 P5.1） | 非有限值 / 尺寸 ≤ 0 / 超 ±2²⁴ DIP ⇒ **Warning + 忽略**（**不静默规范化**——比猜测调用者意图更合适） |
| **7** | **读回方向走整数重载** | 物理像素天然为整数 ⇒ `PixelsToDip(int)` 足够；★ 读回值量化为 **1 DIP** |

### D31-D ★ **「原子」的精确边界**（评审 P5 收紧——写进 `@details`）

> **一次 `SetBounds` 调用通过一次平台级边界设置操作同时提交位置和尺寸，避免框架通过两次独立 API 调用主动制造中间状态。**
> ★ **不承诺**：系统在设置过程中**不派发**尺寸/位置消息；布局在消息处理中**不更新**（`SetWindowPos` 可**同步派发 `WM_SIZE`** → `OnResized` → `Arrange`——Phase 20 崩溃修复的实测）。
> ⇒ **测试**（T31-4）**不断言**「调用期间无消息」——只断言**调用后**的最终状态。

### D31-E 三概念分离的**契约落点**（评审 P2）

| 概念 | 归属 | 契约 |
|---|---|---|
| **行推进量** | `LineHeight()` | ★ **本方法**——逐行 y 步进 + 光标高 + 单行居中 |
| **字形垂直定位** | 后端内部 | GDI = `TextOutW` 的 `TA_LEFT\|TA_TOP`（`pos.y` = 字符单元顶边）；GL = `pos.y + FontEngine::Ascent`（`GLRenderer.cpp:215`）——**两侧机制不同但输出语义相同** |
| **光标高度** | `TextBox::GetLineHeight()` | ★ **= `LineHeight()`**（`TextBox.cpp:431-436`——「等于行框高」，**不是**「行框内另有偏移」） |

⇒ **C31-7 写明三者关系**；**`LineHeight` 是 ECDI 的排版行框参数，不是「后端所有字体度量的统一替代品」**。

### D31-F DPI 与坐标转换的**验证边界**（评审 P6）

| 项 | 契约 |
|---|---|
| **`GetBounds` 未显示时** | 空 `Rect{}` + Warning（**与同组运行期 API 一致**——`Minimize` 等） |
| **跨 DPI** | 读回**用当前窗口 DPI**（`GetDpiForWindow`）——窗口移到另一屏后**以新屏 DPI 解释** |
| **往返精度** | **不要求逐位相等**——DIP→物理→DIP 含舍入（`DpiConversion.h` C9：`dpi > 96` 时物理网格更密 ⇒ 多对一 ⇒ 信息已丢失）；T31-4 断言**容差 ≤1 DIP** |
| **系统调整 vs 转换误差** | ★ T31-5 **必须区分**：负坐标/超屏时**系统主动调整**（读回 ≠ 输入是**预期行为**，非框架 bug）vs **同 DPI 无超屏时读回偏差 > 1 DIP**（**那才是框架转换错误**） |

---

## §2 基线（B1–B17——逐项实测行号）

| # | 事实 | 位置 |
|---|---|---|
| B1 | GDI `LineHeight` 现读 `tmHeight` | `GDITextMeasurer.cpp:287` |
| B2 | FT `LineHeight` 现读 `metrics.height` | `FontEngine.cpp:401` |
| B3 | FT 侧转发（`m_engine ? … : font.size`） | `FreeTypeTextMeasurer.cpp:54-56` |
| B4 | RecordingBackend 行高 = 14.0f（替身） | `RecordingBackend.h:113` |
| B5 | TextBox 光标高 = `GetLineHeight()` = `LineHeight()` | `TextBox.cpp:431-436` |
| B6 | GDI 测量路径脚手架（GetDC + GetOrCreateFont + SelectObject） | `GDITextMeasurer.cpp:265-281` |
| B7 | GDI 回退 = `font.size` | `GDITextMeasurer.cpp:263` |
| B8 | FT 回退 = `font.size` | `FontEngine.cpp:396` |
| B9 | `GetFontData` **恒 `GDI_ERROR`**（本机实测） | 初设 §2.3 |
| B10 | `GetOutlineTextMetricsW` **可用**（256–342 B） | 初设 §2.3 |
| B11 | OTM `otmMac*` = hhea 缩放值（12/12 字体 0.00 px） | 初设 §2.6 H1 |
| B12 | `WidgetTests.cpp:108/139` 用 `backend.LineHeight()` 现算期望（自洽） | `WidgetTests.cpp:108/139` |
| ★ **B13** | **`otmMacLineGap` 是 `UINT` 字段**（`int otmMacAscent` / `int otmMacDescent` / `UINT otmMacLineGap`） | `wingdi.h:2565/2567`（MinGW-w64） |
| ★ **B14** | **本机 385 个 face 中负 `lineGap` = 0**（289 零 / 96 正）⇒ 负值情形**无现成样本** | 探针 `probe_linegap_scan.cpp` |
| ★★ **B15** | **受控合成负值实测**：GDI 存 **int32 补码**（−200 ⇒ `0xFFFFFFFE`；−1000 ⇒ `0xFFFFFFF6`）⇒ **`(int)` 直读即正确** | 探针 `probe_neglinegap.cpp`（patch 字体副本 + `AddFontResourceExW` FR_PRIVATE） |
| ★ **B16** | **`get`/`set` 的唯一 DIP↔px 真相源是 `DpiConversion.h`**——当前**只有 int 重载** | `DpiConversion.h:55/71` |
| ★ **B17** | **内部测试缝先例**：函数指针形态（不进公共 API、保 `final`） | `Win32PlatformWindow.h:92/105`（`SetDragFinishForTests` / `SetDesktopHookObserverForTests`） |

---

## §3 逐文件改动（△1–△14）

| △ | 文件 | 改动 | 性质 |
|---|---|---|---|
| **△1** | `GDITextMeasurer.cpp` | `LineHeight` 改写：`tmHeight` → **`otmMac*` 行框**（§1 D31-A 代码）+ `GetOutlineTextMetricsW` 调用 + **`alignas` 缓冲** + **`otmMacLineGap` 按 int 直读**（v1.1 修正）+ `box ≤ 0` 回退 + 失败接缝挂钩 | **行为变更**（核心） |
| **△2** | `GDITextMeasurer.h` | `LineHeight` 的 `@details` 更新（口径 = hhea 行框；回退条件含「无 hhea 表」）+ ★ **失败接缝声明**（v1.1——见 △14） | 契约 + 内部缝 |
| **△3** | `TextMeasurer.h:119-121` | `LineHeight` 的 `@details` 改写（D31-A 统一定义 + 两侧实现路径 + 容差 ≤1 px + 回退 + 三概念分离） | **公共契约** |
| **△4** | `FreeTypeTextMeasurer` | ★ **零改动**（`metrics.height` 本就是 hhea 口径） | — |
| **△5** | `FontEngine` | ★ **零改动**（`metrics.height` / `Ascent` 不动） | — |
| **△6** | `PlatformWindow.h` | `#include "ECDI/Core/Rect.h"` + `virtual void SetBounds(const Rect&) = 0;` + `[[nodiscard]] virtual Rect GetBounds() const = 0;` | **公共 API +2** |
| **△7** | `Win32PlatformWindow.h` | `void SetBounds(const Rect&) override;` + `Rect GetBounds() const override;` | 实现声明 |
| **△8** | `Win32PlatformWindow.cpp` | §1 D31-C 代码（**非法输入拒绝** + `SetWindowPos` + `GetWindowRect` + `m_shown` 门控 + **float 重载换算**） | 实现 |
| **△9** | `Window.h` | `#include "ECDI/Core/Rect.h"` + `void SetBounds(const Rect&);` + `[[nodiscard]] Rect GetBounds() const;` | **公共 API +2** |
| **△10** | `Window.cpp` | 2 个薄转发（放 `Minimize` 组之后——同运行期分组）+ `m_platformWindow` 判空（与 `GetDpiScale` :200 同款） | 转发 |
| **△11** | `AnimationTests.cpp` | `TestPlatformWindow` + 2 个 override（`SetBounds` / `GetBounds`——**记录式**以便 Window 级转发断言） | **测试替身同步** |
| **△12** | `ProgressBarTests.cpp` | 同 △11 | **测试替身同步** |
| ★ **△13** | `src/Platform/Win32/DpiConversion.h` | ★ **新增 float 重载 `DipToPixels(float, int)`**（v1.1——评审 P1 落点；保留 float 至最后一次取整，**复用同一 `RoundHalfAwayFromZero`**——舍入仍只出现在一处） | **内部件 +1 函数** |
| ★ **△14** | `GDITextMeasurer.h/.cpp` | ★ **OTM 查询失败注入缝**（v1.1——评审 P5.3）：内部函数指针（默认指向真 `GetOutlineTextMetricsW`），仅供测试注入失败 ⇒ T31-3 可**稳定**走回退分支 | **内部测试缝**（不进公共 API） |

★ **△4/△5 零改动**（B-2 的直接收益——FT 侧不动）；**△11/△12** = 条 33（加纯虚必查实现者清单：3 个全改）。
★ **△13/△14 是 v1.1 新增**——分别对应评审 **P1**（浮点精度）与 **P5.3**（可注入失败）；两者都**不扩公共 API**（△13 在**内部头**、△14 是**内部缝**，沿 B17 先例）。

---

## §4 契约（C31-1..C31-10）

| # | 契约 |
|---|---|
| **C31-1** | `LineHeight` 语义 = **行推进量**，口径 = **`(ascender − descender + lineGap) × fontSizePx / unitsPerEm`**（由 ECDI 定义） |
| **C31-2** | 两链一致性 = **同一个数**，允许差异仅**舍入级 ≤1 px**（@96dpi，不随字号增长） |
| **C31-3** | 回退：GDI `OTM 失败 / box ≤ 0` · FT `face == nullptr` ⇒ **统一 `font.size`** |
| **C31-4** | `SetBounds` = **请求**；`GetBounds` = **系统当前事实**；二者可不等且不补偿 |
| **C31-5** | 公共 API **恒 DIP**；换算只经 `DpiConversion.h`；几何（int）与测量（float）两条链路不得合并。★ **v1.1 补**：几何链路的 **DIP→px 方向**保留 **float 精度至最后一次取整**（不先截断到 int DIP） |
| **C31-6** | `SetBounds`/`GetBounds` = **运行期 API**（`Show()` 前 Warning + 忽略/空 `Rect`） |
| **C31-7** | 三概念分离：**行推进量**（`LineHeight`）· **字形垂直定位**（后端内部——`pos.y` 恒为字符单元顶边）· **光标高度**（= 行框高）。★ `LineHeight` 是 ECDI 的排版行框参数，**不是**后端所有字体度量的统一替代品 |
| **C31-8** | `SetBounds` = **一次平台调用提交位置 + 尺寸**（消除框架主动制造的中间状态）；★ **不承诺**不触发中间消息（`WM_SIZE` 同步派发是预期行为——Phase 20 实测） |
| ★ **C31-9** | **非法输入明确拒绝**（v1.1 新增——评审 P5.1）：**非有限值**（NaN / ±Inf）· **尺寸 ≤ 0** · **超 ±2²⁴ DIP** ⇒ `SetBounds` **Warning + 忽略**（**不静默规范化**，不猜测调用者意图）。★ 坐标可为负（合法——多屏）；**仅尺寸必须为正** |
| ★ **C31-10** | **`otmMacLineGap` 的读法**（v1.1 新增——评审 P2）：GDI 在该字段存 **int32 补码** ⇒ **按 `int` 直读**；★ **不得**做「> 0x7FFF ⇒ 减 65536」式重解释（受控实测证明其在真实负值下算错） |

---

## §5 盯防（可机检）

| # | 项 | 判据 |
|---|---|---|
| ① | GDI `LineHeight` 不再用 `tmHeight` | `grep -c "tmHeight" GDITextMeasurer.cpp` 的 `LineHeight` 函数体 **= 0** |
| ② | GDI `LineHeight` 使用 `otmMac*` | `grep -c "otmMacAscent" GDITextMeasurer.cpp` **≥ 1** |
| ③ | `GetFontData` **不得出现**在产品代码 | `grep -rc "GetFontData" ECDI/src/` **= 0** |
| ④ | FT 侧零改动 | `git diff --stat -- ECDI/src/Render/FontEngine.cpp ECDI/src/Render/FreeTypeTextMeasurer.cpp` **= 空** |
| ⑤ | `TextMeasurer` 签名零变化 | `git diff -- TextMeasurer.h` **仅 @details 行变更**（`virtual float LineHeight` 行不变） |
| ⑥ | `PlatformWindow` 纯虚 +2 | `grep -c "SetBounds\|GetBounds" PlatformWindow.h` **≥ 2**；3 个实现者全有 override |
| ⑦ | `SetBounds` 带 `SWP_NOZORDER` | `grep "SWP_NOZORDER" Win32PlatformWindow.cpp`（`SetBounds` 函数体内）**≥ 1** |
| ⑧ | `SetBounds` 不带 `SWP_NOSIZE` / `SWP_NOMOVE` | 函数体内 **= 0** |
| ⑨ | `SetBounds` 带 `m_shown` 门控 | 函数体内 `if (!m_shown)` **≥ 1** |
| ⑩ | `Window::SetBounds` 带 `m_platformWindow` 判空 | `Window.cpp` 函数体内 `if (!m_platformWindow)` **≥ 1**（与 `GetDpiScale` :200 同款） |
| ★ **⑪** | **`otmMacLineGap` 按 int 直读**（v1.1——评审 P2） | `grep -c "otmMacLineGap" GDITextMeasurer.cpp` **≥ 1** **且**同文件 `grep -c "65536" GDITextMeasurer.cpp` **= 0**（禁止减 65536 式重解释） |
| ★ **⑫** | **缓冲区有对齐保证**（v1.1——评审 P3） | `grep -c "alignas(OUTLINETEXTMETRICW)" GDITextMeasurer.cpp` **≥ 1** |
| ★ **⑬** | **非法输入被拒绝**（v1.1——评审 P5.1） | `SetBounds` 函数体内 `std::isfinite` **≥ 1** 与 `<= 0.0f` **≥ 1** |
| ★ **⑭** | **float 重载存在且被用**（v1.1——评审 P1） | `DpiConversion.h` 有 `DipToPixels(float` **≥ 1**；`SetBounds` 函数体内**无** `static_cast<int>(bounds` **= 0** |
| ★ **⑮** | **失败接缝存在**（v1.1——评审 P5.3） | `GDITextMeasurer.h` 有 `...ForTests` 形参 **≥ 1**；★ 且**不进公共头**（`include/ECDI/` 零命中） |

---

## §6 用例正文（T31-1..T31-8 + T31-4b；356 → 365）

### 批一：窗口几何（T31-4 / T31-5 / T31-7）

**装置**：真实窗口（沿 `TextMeasurerTests.cpp` 的 `MeasurerWindow` 模式——`CreateWindowExW` + `Initialize`）

| # | 场景 | 断言核心 |
|---|---|---|
| **T31-4** | 几何往返（基础）：整数 DIP `{100, 200, 400, 300}` → `GetBounds()` | 各分量差 **≤1 DIP**（D31-F：不要求逐位）；★ @96dpi 精确、@120dpi 容差 |
| ★ **T31-4b** | **非整数 DIP 往返**（v1.1 新增——评审 P5.2） | `SetBounds({100.8, 200.5, 400.25, 300.75})` ⇒ **验证 float 精度保留**：在 **≥120 dpi** 环境下，读回值与「按 float 换算的期望」一致（**若先截断到 int DIP，144 dpi 下会偏 1 px**——该用例即为此差异设计）。★ 至少一个**非 96 DPI** 环境 |
| **T31-5** | ★ 退化输入（v1.1 扩充——评审 P5.1/P6） | ① **负坐标**：`{-50, -50, 200, 100}` → 读回 x/y 可为负、不崩溃；② **跨 DPI**：移到 DPI 不同的屏 → 读回用**新屏 DPI** 解释；③ **尺寸大于屏幕**：`{0, 0, 9999, 9999}` → 不崩溃、读回**反映系统实际值**；④ **系统实际调整**（最大化后 SetBounds）→ 读回反映**最大化态**；★⑤ **新增非法输入**：`width = 0` / 负值 / `NaN` / `Inf` / 超 ±2²⁴ ⇒ **Warning + 无副作用**（C31-9）。★ 全部须区分「**API 输入 / 系统物理矩形 / DIP 读回**」三者 |
| **T31-7** | 生命周期（C31-6） | `Show()` 前 `SetBounds` ⇒ **无副作用**（GetWindowRect 不变）+ **不崩溃**；`Show()` 前 `GetBounds` ⇒ **空 `Rect{}`**；`Show()` 后正常 |

### 批二：行高语义（T31-1 / T31-2 / T31-3 / T31-6）

**装置**：真实窗口 + `GDITextMeasurer` + `FreeTypeTextMeasurer`（同一 `FontSource`）

| # | 场景 | 断言核心 |
|---|---|---|
| **T31-1** | ★ 两链行高一致性 | 对 **SimSun / Arial / Consolas / Segoe UI** 4 字体 × **3 字号**（12/14/20 px）：**GDI 与 FT 的 `LineHeight` 之差 ≤ 1 px**；★ **覆盖多字号**（残差不随字号增长——B-2 的核心判据）。★★ **v1.1 口径统一（评审 4.2）**：本用例结论**限定为「同字体族、同字号、同 DPI」下的一致性**——**不声称已验证同文件**（同源层级见 T31-6） |
| **T31-2** | ★ 行高的下游布局行为 | ① 单行 Label 垂直居中位置 = `(H − lineHeight) / 2`；② 多行 wrap：`totalHeight == lineHeight × 行数`、逐行 y = `baseY + row × lineHeight`；③ TextBox 光标高 == `GetLineHeight()`；④ TextBox 滚动范围 == `行数 × lineHeight − 控件高` |
| **T31-3** | 回退规则 | ★★ **v1.1 修订（评审 P5.3）**：① GDI：**经 △14 注入缝**稳定模拟 OTM 查询失败（**不再**依赖「某系统字体恰好失败」——那会随系统版本/字体替代策略漂移）⇒ 断言得 `font.size`；★ **另补一条真实路径**：注入缝不启用时对**真实非 TrueType 字体**（如 `System`）取值 ⇒ 记录其在**本机**的实际行为（**不当作稳定判据**）；② FT：`m_engine == nullptr` ⇒ 得 `font.size`（既有行为） |
| **T31-6** | ★ 字体同源性锚（**层级如实声明**） | ★★ **v1.1 明确（评审 4.2/P3）**：`GetTextFaceW` **只返回族名** ⇒ 本用例**只证明到「族名相同」层级**，**不证明文件相同**。判据 = ① GDI `GetTextFaceW` 族名与用例预期族名一致；② FT `FontSource::ResolveFile` 返回的文件名与用例预期一致；③ ★ **附强证据**：`otmEMSquare`（GDI）**等于** FT `units_per_EM`（设计单位数——同值大幅提高「同一字体」的可能性，但**仍非文件级证明**）。★ **文档必须写明该局限**（不得写成「已验证同一文件」） |

### 批三：跨后端基线（T31-8）

| # | 场景 | 断言核心 |
|---|---|---|
| **T31-8** | ★ 跨后端**字形栅格包围盒**对齐 | ① **固定字体文件**（SimSun / `simsun.ttc`）、**固定字形**（`H` — 无下伸；对照 `g` — 有下伸 / `测` — CJK）、**固定字号**（14 px）与 DPI（96）；② **固定 `pos`**（如 `{100, 100}`）；③ **指标 = 非背景像素的最小 Y 与最大 Y**（字形栅格包围盒纵向两端——**非基线**，基线不可从像素直读）；④ **容差 ±1 px**（光栅化/hinting 差异的显式放宽）；⑤ **断言 = GDI 与 GL 的包围盒顶端偏移与底端偏移各自 ≤ ±1 px**。★★ **命名纪律（评审 §6）**：本用例**只能称「字形栅格包围盒对齐」**——**不得**在后续总结中扩大解释为「已证明基线完全一致」 |

★ **A→T 可追溯闭合**（条 119②）：需求 A1 → **T31-1 + T31-6**；A2 → **T31-2**；A3 → **T31-4/5/7**；A4 → **9 消费点核对**；A5 → **库外探针**（`probe_common_box.exe` 复跑——不入本表）。

---

## §7 影响面

| 维度 | 值 |
|---|---|
| 公共头 | **95 → 95**（复用 `Core/Rect.h`——不加新头） |
| 公共 API | **+2**（`Window::SetBounds` / `GetBounds`）· `PlatformWindow` **+2 纯虚** · `TextMeasurer` **签名零变化**（仅 `@details`） |
| 实现者同步 | ★ **`PlatformWindow` 3 个全改**（`Win32PlatformWindow` + 2 个 `TestPlatformWindow`）；`TextMeasurer` **7 个中仅 `GDITextMeasurer` 改实现**（其余 6 个零改动） |
| 生产消费点 | `LineHeight` **9 处**——★ **GDI 行高实际改变**（常规字号 ±2 px）⇒ 逐项核对（A4） |
| 用例 | ★ **+9**（T31-1..T31-8 **+ T31-4b**）⇒ **356 → 365**（★ v1.1：评审 P5.2 要求补非整数 DIP 用例） |
| CMake | **0**（无新文件——T31 分布在既有测试文件） |
| 风险 | **中**——① **GDI 默认路径行为变更**（常规字号 ±2 px，多行步进/光标高/滚动范围/垂直居中全受影响）；② 3 个实现者同步；★ **回归面已核实**：既有测试**无一处对 `GDITextMeasurer::LineHeight` 硬编码数值**（`WidgetTests.cpp:108/139` 用 `LineHeight()` 现算，自洽跟随；其余为替身常量）；③ ★ v1.1 新增 **△13/△14**（float 重载 + 失败注入缝）——均为**内部件**，公共面零增量 |

---

## §8 批次

| 批 | 内容 | 新增用例 | 累计 | 验收 |
|---|---|---|---|---|
| **批一** | **② 窗口几何**（△6–△10 + △11/△12 替身同步 + ★ **△13 float 重载**） | T31-4/4b/5/7（**4**） | **360** | 全链编译 + 存量全绿；★ **M1 阻塞解除** |
| **批二** | **① 行高口径**（△1–△3 + ★ **△14 失败注入缝**） | T31-1/2/3/6（**4**） | **364** | ★ **364 全绿** + **9 消费点逐项核对**（GDI 行高**会变**——须确认位移可解释） |
| **批三** | T31-8 字形栅格包围盒对齐 + harness 复跑 + 文档收口 | T31-8（**1**） | **365** | 探针读数 + 用户 MSVC |

★ **顺序**：② 先行（M1 卡在它上面）；① 独立批次（回归面最大，须可独立定位）。

---

## §9 开放项

| # | 项 |
|---|---|
| **O1** | ~~容差收紧~~ → ★ **关闭**（B-2 下已为舍入级 ≤1 px） |
| **O2** | `WindowBounds` 独立类型（**回退项**——若评审认为坐标系必须类型可见则启用，公共头 95 → 96） |
| **O3** | `Ascent` 升格进 `TextMeasurer`（当前无消费者——YAGNI） |
| **O4** | `WM_DPICHANGED` 与 `SetBounds` 交互细化（当前：系统建议矩形优先，框架不补偿） |
| **O5** | harness `REQ-05` 复跑（**固定版本指纹 + 字体文件 + DPI 配置**） |
| **O6** | `LineHeight` 缓存评估（OTM 调用比 `GetTextMetricsW` 多一次 + 可能堆分配——若基准显示开销显著则加缓存，沿 Phase 26 测量缓存先例） |
| **O7** | **回退开关评估**（条 60：翻转默认形态须评估是否留回退）——行高从 `tmHeight` → `otmMac*` 是**默认路径行为变更**；若用户报告观感不可接受，是否提供编译期/构造期回退？（**倾向不提供**——YAGNI + 这是缺陷修正而非「新能力」） |
| ★ **O8** | **负 `lineGap` 的实机覆盖**（v1.1 新增——评审 P2/4.1）：受控合成已证 GDI 存 int32 补码（B15），但**本机 385 个 face 无负值真样本** ⇒ 该分支**未被真实字体覆盖**。若将来遇到负 `lineGap` 字体，应按 B15 的读法复核（**当前实现已按实测结论写**） |
| ★ **O9** | **`DpiConversion.h` 的 float 重载命名/位置**（v1.1 新增——评审 P1）：本稿取 `DipToPixels(float, int)` 重载（与既有 int 版同址）。★ 备选 = 独立函数名（如 `DipToPixelsF`）——**倾向重载**（调用点零心智负担；重载解析按实参类型，无歧义） |

---

## §10 修订记录

- **v1.0**（2026-10-08）初稿。**输入**：初设 **v1.1**（B-2 拍板 + P1–P8 吸收）+ **§1 详设必答逐题钉死**（D31-A GDI 实现代码含 6 条细节 + D31-B 判据 + D31-C 几何实现含 5 条细节 + D31-D 原子边界 + D31-E 三概念 + D31-F DPI 边界）+ **§2 基线 B1–B12 带行号** + **§3 逐文件 △1–△12**（★ △4/△5 零改动 = B-2 收益；△11/△12 = 条 33 实现者同步）+ **§4 契约 C31-1..C31-8**（★ C31-7 三概念分离 + C31-8 原子边界）+ **§5 盯防 10 条（可机检）** + **§6 用例 T31-1..T31-8**（精确装置：T31-5 四类退化输入 + T31-6 族名级局限 + T31-8 五要素）+ **§7 影响面**（95→95 / API +2 / 用例 356→364 / CMake 0 / 风险 中）· §8 三批（② 先行）· §9 O1–O7 · ★ **§2.5 仪器错误如实记录**。
- **v1.1**（2026-10-08）**详设评审吸收（P1–P5 + §4/§6）**。**评审总判**：「**详设接近 PASS，但暂不建议直接进入实现**——窗口几何设计方向通过（补输入校验与浮点转换后可实施）；**GDI 行高实施前必须确认**（`otmMacLineGap` 负值依据 + 缓冲区对齐）；测试与文档少量修订即可闭合」。
  **★ 三项实施前必办（评审给定）**：
  ① ★★ **P1 浮点精度丢失（已修正）**——原稿 `DipToPixels(static_cast<int>(bounds.x), dpi)` **在 DIP 端先截断一次**（100.8 DIP → 100 DIP ⇒ 144 dpi 下物理坐标偏 1 px）⇒ 新增 **△13**：`DpiConversion.h` 增 **float 重载**（放大 65536 倍取整后复用同一 `RoundHalfAwayFromZero`——**舍入仍只在一处**），`SetBounds` 改用之；契约 **C31-5 补**「DIP→px 方向保留 float 至最后一次取整」；**新增 T31-4b**（非整数 DIP + ≥120 dpi）。
  ② ★★ **P2 `otmMacLineGap` 语义（已受控实测并修正）**——原稿「> 0x7FFF ⇒ 减 65536」**是假设而非事实**（评审：不能因原始表是 INT16 就断言 Win32 返回值编码）⇒ **本会话做了受控合成实验**：本机 385 face **负 lineGap = 0**（无现成样本）⇒ patch 一份 `arial.ttf` 副本的 `hhea.lineGap` 为 −1/−200/−1000 + 改名 + `AddFontResourceExW(FR_PRIVATE)` 私有装载，实测 **GDI 存 int32 补码**（−200 ⇒ `0xFFFFFFFE`、−1000 ⇒ `0xFFFFFFF6`）⇒ **`(int)` 直读即正确；原稿逻辑在 −200 时算出 −65538（错）**。⇒ 实现改为直读，**新增 C31-10** 与盯防 **⑪**（禁 65536 式重解释），**新增 O8**（该分支无真实字体覆盖，如实登记）。
  ③ ★ **P3 缓冲区对齐（已修正）**——`BYTE[]` **不保证** `OUTLINETEXTMETRICW` 对齐 ⇒ 栈路径 **`alignas(OUTLINETEXTMETRICW)`**、堆路径 **`operator new(size, align_val_t(...))`**；**新增盯防 ⑫**。
  **其余吸收**：④ **P5.1** 非法输入明确拒绝（非有限 / 尺寸 ≤ 0 / 超 ±2²⁴ DIP ⇒ Warning + 忽略，**不静默规范化**）——**新增 C31-9** 与盯防 **⑬**、T31-5 第 ⑤ 类；⑤ **P5.2** T31-4b（见上）；⑥ **P5.3** T31-3 改为**稳定注入失败**——**新增 △14**（内部函数指针缝，复刻 `SetDragFinishForTests` 先例；**不进公共 API**）与盯防 **⑮**；⑦ **评审 4.2/P3** **T31-1 与 T31-6 口径统一**——T31-1 结论**限定为「同字体族」**、T31-6 补 `otmEMSquare == FT units_per_EM` 作**强证据但非文件级证明**，并明写「**名称同 ≠ 文件同**」不得含糊；⑧ **评审 §6** T31-8 **命名纪律**——只称「**字形栅格包围盒对齐**」，**不得**扩大解释为「已证明基线一致」；⑨ **§2 基线扩 B13–B17**（`wingdi.h` 字段类型 / 负 `lineGap` 扫描 / 受控实测 / DpiConversion 现状 / 内部缝先例）；⑩ 用例 **+8 → +9**（加 T31-4b）⇒ **356 → 365**，**§8 批次累计数同步**（批一 360 / 批二 364 / 批三 365）；⑪ **§9 新增 O8（负 lineGap 实机覆盖）与 O9（float 重载的命名/位置）**。**未改动**：窗口几何的 API 形态（`SetBounds`/`GetBounds`）· `SWP_NOZORDER`/`SWP_NOACTIVATE` 的选择 · D31-A 的 **hhea 口径**（评审支持该方向）· FT 侧零改动 · D31-D 原子边界 · D31-E 三概念分离。
- **v1.2**（2026-10-08）**批一实施回填（见 §11）**。状态行更新为「批一已实施 · 三链 360/360 全绿」。

---

## §11 实施回填（批一——窗口几何）

> ★ 本节按条 6「实现回写三件套」在**实施后**补写：**实测数据 + 与详设的偏离 + 遗留项**。

### 11.1 落地清单（△6–△13；★ 与 §3 逐条对照）

| △ | 文件 | 落地 |
|---|---|---|
| △6 | `PlatformWindow.h` | ✅ `#include "ECDI/Core/Rect.h"` + `SetBounds` / `GetBounds` 两纯虚（含契约注释） |
| △7 | `Win32PlatformWindow.h` | ✅ 两 override 声明（`ApplyStartupSize` 之后——与 §3 一致） |
| △8 | `Win32PlatformWindow.cpp` | ✅ 非法输入校验 + `DipToPixels`（**float 重载**）+ `SetWindowPos(SWP_NOZORDER \| SWP_NOACTIVATE)` + `GetWindowRect` → `PixelsToDip` |
| △9 | `Window.h` | ✅ `#include "ECDI/Core/Rect.h"` + 两公共方法（放在 Phase 12 运行期组之前，独立小节） |
| △10 | `Window.cpp` | ✅ 两转发（`SetBounds` 直转；`GetBounds` 带 `m_platformWindow` 判空——与 `GetDpiScale` :200 同款） |
| △11 | `AnimationTests.cpp` | ✅ `TestPlatformWindow` **记录式**替身（`lastBounds` + `setBoundsCount`） |
| △12 | `ProgressBarTests.cpp` | ✅ 同 △11 |
| △13 | `DpiConversion.h` | ✅ `DipToPixels(float, int)` 重载（1/65536 DIP 子像素 → 复用 `RoundHalfAwayFromZero`） |
| △14 | `GDITextMeasurer` | ⏳ **批二**（行高口径一并落地） |

### 11.2 ★ 实施期偏离（如实记录）

| # | 详设原稿 | 实施落地 | 原因 |
|---|---|---|---|
| **1** | 日志写 `Logger::Warning(L"...")` | ✅ 改为 **`Logger::Log(LogLevel::Warning, L"...")`** | ★ 详设原稿的 API 名**不存在**——`Logger` 只有 `Log(LogLevel, msg)`（`Logger.h:28`）。**编译期即暴露**（条 62 的价值） |
| **2** | `#include` 未列 `<cmath>` | ✅ 补 `#include <cmath>` | `std::isfinite` / `std::fabs` 需要 |
| **3** | （未预见） | ✅ `Invalidate()` **未调用** | ★ `SetBounds` 改几何 ⇒ 系统**同步派发 `WM_SIZE`** ⇒ `OnResized` → `Arrange` → 既有的重绘链**自动覆盖**；**无需**框架自加 `Invalidate()`（与 Phase 22 `ApplyStartupSize` 同款——那是既有路径） |
| **4** | 详设 §1 D31-C 代码未含 `m_hwnd == nullptr` 守卫 | ✅ `GetBounds` 加该守卫 | 防御性：平台窗口未就绪 ⇒ 空 `Rect`（与基类契约「未就绪 ⇒ 空值」一致） |

### 11.3 ★★ 验收读数（三链全绿）

| 链 | 编译 | 测试 |
|---|---|---|
| **MinGW**（`cmake-build-debug-mingw`） | ✅ 全目标（ECDI + ecdi_tests + 3 示例） | ✅ **360 / 360** |
| **Clang**（`cmake-build-debug-clang`） | ✅ 全目标 | ✅ **360 / 360** |
| **ClangCL**（`cmake-build-debug-clangcl`） | ✅ 全目标 | ✅ **360 / 360** |

★ **用例数 356 → 360**（+4 = T31-4 / T31-4b / T31-5 / T31-7）——**与详设 §8 批一投影（360）逐位一致**。
★ **断言层已启用**（条 35/50）：`CMAKE_BUILD_TYPE=Debug` + `build.ninja` 含 `-D_DEBUG`（MinGW 链为 CMake 显式补入——见根 `CMakeLists.txt:25-27`）。

### 11.4 ★ 非空虚验证（独立探针——条 99「判据不来自被测实现」）

探针 `.workbuddy/spike/p31-geometry/probe_batch1_verify.cpp`（读数 `batch1_verify.txt`）**独立复算**并证明新用例的断言**非空虚**：

| 检验 | 读数 | 意义 |
|---|---|---|
| **G5 恒等（@96dpi）** | 整型 vs float 重载在 `-500..500` **全等** | ★ float 重载**不破坏**「dpi==96 恒等」零回归红线 |
| **P1 精度差真实存在** | `100.8 DIP @144dpi` ⇒ float 路径 **151 px** vs 截断路径 **150 px** | ★ 证明评审 P1 指出的「多丢一次」**是真实的 1 px**（T31-4b 即为此而设） |
| **C10 负数对称** | `±100.8 @144dpi` ⇒ **−151 / 151** | 远离零舍入在 float 重载中**保持** |
| **Show 前生效** | 读回 `(300,250) 350x220` | ★ D31-G 前提成立（`Create` 后即可用） |
| **非整数 DIP** | 读回 `(101,201) 400x301` | 400.25 → 400、300.75 → 301（**合理舍入**，非截断） |
| **非法输入无副作用** | `400x301 → 400x301`（逐位未变） | ★ C31-9 生效 |
| **负坐标** | 读回 `(-50,-50) 200x100` | 系统接受负坐标、框架如实读回 |

★ **探针本身未发现仪器错误**（对照初设 §2.5 的两处——本轮读数与生产代码路径一致）。

### 11.5 遗留项

| # | 项 | 去向 |
|---|---|---|
| ★ **L1** | **`SetBounds` 的 DPI 跨屏场景未实测**（T31-5 第 ② 类「跨 DPI」需要**双屏/双 DPI 环境**） | 本机单屏 ⇒ **如实登记为未覆盖**（同 Phase 26 T26-10 的诚实标注先例）；O4 跟踪 |
| **L2** | △14（失败注入缝）与批二同时落地 | **批二** |
| **L3** | 批二的 **9 消费点核对**（GDI 行高会变） | **批二** |

### 11.6 状态

- **批一 = ✅ 完成**（△6–△13 + T31-4/4b/5/7；三链 360/360）⇒ ★★ **DesktopNest M1 的窗口几何阻塞已解除**
- **批二**（行高口径 △1–△3 + △14 + T31-1/2/3/6）**待实施**
- **批三**（T31-8 + harness 复跑 + 收口）**待实施**
