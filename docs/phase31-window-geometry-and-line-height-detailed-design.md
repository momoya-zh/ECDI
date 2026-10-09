# Phase 31 · 窗口几何与行高口径 —— 详细设计（v1.0 · 实施规格）

> 来源：初设 `phase31-window-geometry-and-line-height-preliminary-design.md` **v1.1**（评审吸收 B-2：窗口几何 **PASS → 详设**；行高 **公共 hhea 口径**由需求评审拍板）
> 状态：**v1.0**（2026-10-08，待评审）
> 定位：**实施规格**——把初设的 D31-A..F 落成**逐文件、逐行**的改动与精确测试装置。
> ★ 需求稿锁定的范围不变：**不做虚拟化 / 脏区 / 后端执行端 / OnPaint 纯绘制重构 / 富文本 / shaping**。

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
			// ★ otmSize 含尾随字符串区（族名/全名/样式名，实测 256–342 B）
			//   栈上固定缓冲 + 超长回退堆分配（防栈溢出——极端长字体名）
			BYTE stackBuf[1024];
			BYTE* buf = stackBuf;
			std::unique_ptr<BYTE[]> heapBuf;
			if (otmSize > sizeof(stackBuf))
			{
				heapBuf.reset(new BYTE[otmSize]);
				buf = heapBuf.get();
			}

			OUTLINETEXTMETRICW* otm = reinterpret_cast<OUTLINETEXTMETRICW*>(buf);
			if (GetOutlineTextMetricsW(measureDC, otmSize, otm))
			{
				// ★ otmMacLineGap 是 UINT，但 hhea.lineGap 原始类型是 INT16
				//   （可为负——极罕见）⇒ 防御性重解释（> 0x7FFF ⇒ 负值）
				const int gap = (otm->otmMacLineGap > 0x7FFF)
					? static_cast<int>(otm->otmMacLineGap) - 65536
					: static_cast<int>(otm->otmMacLineGap);

				const int box = otm->otmMacAscent - otm->otmMacDescent + gap;
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
| **3** | **`otmMacLineGap` 防御性 INT16 重解释** | TrueType 规范 `hhea.lineGap` 是 **INT16**（可为负）；Windows 存入 UINT 字段——极端字体下高位为 1 ⇒ 不处理会爆加 |
| **4** | **`box <= 0` ⇒ 回退 `font.size`** | 退化字体防御（初设 C31-3）；★ 与 FT 侧 `face == nullptr ⇒ font.size` **对称** |
| **5** | **栈缓冲 1024 B + 超长回退堆** | OTM 结构含尾随字符串区（实测 256–342 B）；1024 B 覆盖绝大多数；**不无脑堆分配**（此路径可能每帧走） |
| **6** | **不缓存**（沿 Phase 26 v1.1「LineHeight 不加缓存」先例） | 最小面；★ 性能影响见 §8 O6 |

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

	const int dpi = GetDpiForWindow(m_hwnd);

	// ★ 一次 SetWindowPos 同时提交位置 + 尺寸（C31-8）
	//   SWP_NOZORDER：z 序归 WindowLayer 管（Phase 16），本方法不碰
	//   SWP_NOACTIVATE：折叠/级联不应抢焦点（DesktopNest 场景——M1 K2/K4）
	//   ★ 不带 SWP_NOSIZE / SWP_NOMOVE（本方法就是要改它们）
	SetWindowPos(m_hwnd, nullptr,
	             DipToPixels(static_cast<int>(bounds.x), dpi),
	             DipToPixels(static_cast<int>(bounds.y), dpi),
	             DipToPixels(static_cast<int>(bounds.width), dpi),
	             DipToPixels(static_cast<int>(bounds.height), dpi),
	             SWP_NOZORDER | SWP_NOACTIVATE);
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

	// ★ 如实反映系统当前值（物理像素 → DIP）
	return Rect{
		static_cast<float>(PixelsToDip(rc.left, dpi)),
		static_cast<float>(PixelsToDip(rc.top, dpi)),
		static_cast<float>(PixelsToDip(rc.right - rc.left, dpi)),
		static_cast<float>(PixelsToDip(rc.bottom - rc.top, dpi))
	};
}
```

| # | 实现细节 | 理由 |
|---|---|---|
| **1** | **`SWP_NOZORDER`** | z 序归 `WindowLayer`（Phase 16 D11）——本方法**不碰** z 序（否则会打断 Desktop 驻留维护） |
| **2** | **`SWP_NOACTIVATE`** | M1 折叠/级联不应抢焦点；★ 与 `ApplyStartupSize`（`:191-193`）和 `WM_DPICHANGED`（`:649-653`）同款 |
| **3** | **`m_shown` 门控** | C31-6（运行期 API——`Show()` 前 Warning + 忽略）；★ 与 `Minimize/Maximize/Restore` 同组 |
| **4** | **`GetWindowRect`（非 `GetClientRect`）** | 语义 = **总尺寸 + 屏幕坐标**（C31 语义 1/2）；★ 与 `:438`（`WM_NCHITTEST` 内）同 API |
| **5** | **`Rect` 的 float 成员在平台边界截断为 int** | `DpiConversion.h` 是 **int 口径**（C31-5）；★ **粒度 = 1 DIP**（与 `Widget::SetPosition(int,int)` / `Create(int,int)` 一致）；文档明写 |

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

## §2 基线（B1–B12——逐项实测行号）

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

---

## §3 逐文件改动（△1–△12）

| △ | 文件 | 改动 | 性质 |
|---|---|---|---|
| **△1** | `GDITextMeasurer.cpp` | `LineHeight` 改写：`tmHeight` → **`otmMac*` 行框**（§1 D31-A 代码）+ `GetOutlineTextMetricsW` 调用 + 栈/堆缓冲 + INT16 防御 + `box ≤ 0` 回退 | **行为变更**（核心） |
| **△2** | `GDITextMeasurer.h` | `LineHeight` 的 `@details` 更新（口径 = hhea 行框；回退条件含「无 hhea 表」） | 契约 |
| **△3** | `TextMeasurer.h:119-121` | `LineHeight` 的 `@details` 改写（D31-A 统一定义 + 两侧实现路径 + 容差 ≤1 px + 回退 + 三概念分离） | **公共契约** |
| **△4** | `FreeTypeTextMeasurer` | ★ **零改动**（`metrics.height` 本就是 hhea 口径） | — |
| **△5** | `FontEngine` | ★ **零改动**（`metrics.height` / `Ascent` 不动） | — |
| **△6** | `PlatformWindow.h` | `#include "ECDI/Core/Rect.h"` + `virtual void SetBounds(const Rect&) = 0;` + `[[nodiscard]] virtual Rect GetBounds() const = 0;` | **公共 API +2** |
| **△7** | `Win32PlatformWindow.h` | `void SetBounds(const Rect&) override;` + `Rect GetBounds() const override;` | 实现声明 |
| **△8** | `Win32PlatformWindow.cpp` | §1 D31-C 代码（`SetWindowPos` + `GetWindowRect` + `m_shown` 门控 + DpiConversion 换算） | 实现 |
| **△9** | `Window.h` | `#include "ECDI/Core/Rect.h"` + `void SetBounds(const Rect&);` + `[[nodiscard]] Rect GetBounds() const;` | **公共 API +2** |
| **△10** | `Window.cpp` | 2 个薄转发（放 `Minimize` 组之后——同运行期分组）+ `m_platformWindow` 判空（与 `GetDpiScale` :200 同款） | 转发 |
| **△11** | `AnimationTests.cpp` | `TestPlatformWindow` + 2 个空 override（`SetBounds` / `GetBounds`） | **测试替身同步** |
| **△12** | `ProgressBarTests.cpp` | 同 △11 | **测试替身同步** |

★ **△4/△5 零改动**（B-2 的直接收益——FT 侧不动）；**△11/△12** = 条 33（加纯虚必查实现者清单：3 个全改）。

---

## §4 契约（C31-1..C31-8）

| # | 契约 |
|---|---|
| **C31-1** | `LineHeight` 语义 = **行推进量**，口径 = **`(ascender − descender + lineGap) × fontSizePx / unitsPerEm`**（由 ECDI 定义） |
| **C31-2** | 两链一致性 = **同一个数**，允许差异仅**舍入级 ≤1 px**（@96dpi，不随字号增长） |
| **C31-3** | 回退：GDI `OTM 失败 / box ≤ 0` · FT `face == nullptr` ⇒ **统一 `font.size`** |
| **C31-4** | `SetBounds` = **请求**；`GetBounds` = **系统当前事实**；二者可不等且不补偿 |
| **C31-5** | 公共 API **恒 DIP**；换算只经 `DpiConversion.h`；几何（int）与测量（float）两条链路不得合并 |
| **C31-6** | `SetBounds`/`GetBounds` = **运行期 API**（`Show()` 前 Warning + 忽略/空 `Rect`） |
| **C31-7** | 三概念分离：**行推进量**（`LineHeight`）· **字形垂直定位**（后端内部——`pos.y` 恒为字符单元顶边）· **光标高度**（= 行框高）。★ `LineHeight` 是 ECDI 的排版行框参数，**不是**后端所有字体度量的统一替代品 |
| **C31-8** | `SetBounds` = **一次平台调用提交位置 + 尺寸**（消除框架主动制造的中间状态）；★ **不承诺**不触发中间消息（`WM_SIZE` 同步派发是预期行为——Phase 20 实测） |

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

---

## §6 用例正文（T31-1..T31-8；356 → 364）

### 批一：窗口几何（T31-4 / T31-5 / T31-7）

**装置**：真实窗口（沿 `TextMeasurerTests.cpp` 的 `MeasurerWindow` 模式——`CreateWindowExW` + `Initialize`）

| # | 场景 | 断言核心 |
|---|---|---|
| **T31-4** | 几何往返（基础）：`SetBounds({100, 200, 400, 300})` → `GetBounds()` | 各分量差 **≤1 DIP**（D31-F：不要求逐位）；★ @96dpi 精确、@120dpi 容差 |
| **T31-5** | ★ 退化输入四类 | ① **负坐标**：`SetBounds({-50, -50, 200, 100})` → 读回 x/y **可为负**（系统允许）且不崩溃；② **跨 DPI**：窗口移到 DPI 不同的屏 → 读回用**新屏 DPI** 解释；③ **尺寸大于屏幕**：`SetBounds({0, 0, 9999, 9999})` → 不崩溃、读回**反映系统实际值**；④ **系统实际调整**（最大化后 SetBounds）→ 读回反映**最大化态**而非请求值。★ **四类均须区分「API 输入 / 系统物理矩形 / DIP 读回」** |
| **T31-7** | 生命周期（C31-6） | `Show()` 前 `SetBounds` ⇒ **无副作用**（GetWindowRect 不变）+ **不崩溃**；`Show()` 前 `GetBounds` ⇒ **空 `Rect{}`**；`Show()` 后正常 |

### 批二：行高语义（T31-1 / T31-2 / T31-3 / T31-6）

**装置**：真实窗口 + `GDITextMeasurer` + `FreeTypeTextMeasurer`（同一 `FontSource`）

| # | 场景 | 断言核心 |
|---|---|---|
| **T31-1** | ★ 两链行高一致性 | 对 **SimSun / Arial / Consolas / Segoe UI** 4 字体 × **3 字号**（12/14/20 px）：**GDI 与 FT 的 `LineHeight` 之差 ≤ 1 px**；★ **覆盖多字号**（残差不随字号增长——B-2 的核心判据） |
| **T31-2** | ★ 行高的下游布局行为 | ① 单行 Label 垂直居中位置 = `(H − lineHeight) / 2`；② 多行 wrap：`totalHeight == lineHeight × 行数`、逐行 y = `baseY + row × lineHeight`；③ TextBox 光标高 == `GetLineHeight()`；④ TextBox 滚动范围 == `行数 × lineHeight − 控件高` |
| **T31-3** | 回退规则 | ① GDI：**模拟 OTM 失败**（不可直接注入 ⇒ 改用**非 TrueType 字体**如 System → `GetOutlineTextMetricsW` 失败 ⇒ 得 `font.size`）；② FT：`m_engine == nullptr` ⇒ 得 `font.size`（既有行为） |
| **T31-6** | ★ 字体同源性锚 | GDI 侧 `GetTextFaceW` 的**族名**与 FT 侧 `FontSource::ResolveFile(family)` 的**文件名**交叉比对；★ **只断言到「族名相同」层级**（评审 P3：**名称同 ≠ 文件同**）+ 声明局限 |

### 批三：跨后端基线（T31-8）

| # | 场景 | 断言核心 |
|---|---|---|
| **T31-8** | ★ 跨后端基线一致性 | ① **固定字体文件**（SimSun / `simsun.ttc`）、**固定字形**（`H` — 无下伸；对照 `g` — 有下伸 / `测` — CJK）、**固定字号**（14 px）与 DPI（96）；② **固定 `pos`**（如 `{100, 100}`）；③ **指标 = 非背景像素的最小 Y 与最大 Y**（字形包围盒纵向两端——**非基线**，基线不可从像素直读）；④ **容差 ±1 px**（光栅化/hinting 差异的显式放宽）；⑤ **断言 = GDI 与 GL 的包围盒顶端偏移与底端偏移各自 ≤ ±1 px** |

★ **A→T 可追溯闭合**（条 119②）：需求 A1 → **T31-1 + T31-6**；A2 → **T31-2**；A3 → **T31-4/5/7**；A4 → **9 消费点核对**；A5 → **库外探针**（`probe_common_box.exe` 复跑——不入本表）。

---

## §7 影响面

| 维度 | 值 |
|---|---|
| 公共头 | **95 → 95**（复用 `Core/Rect.h`——不加新头） |
| 公共 API | **+2**（`Window::SetBounds` / `GetBounds`）· `PlatformWindow` **+2 纯虚** · `TextMeasurer` **签名零变化**（仅 `@details`） |
| 实现者同步 | ★ **`PlatformWindow` 3 个全改**（`Win32PlatformWindow` + 2 个 `TestPlatformWindow`）；`TextMeasurer` **7 个中仅 `GDITextMeasurer` 改实现**（其余 6 个零改动） |
| 生产消费点 | `LineHeight` **9 处**——★ **GDI 行高实际改变**（常规字号 ±2 px）⇒ 逐项核对（A4） |
| 用例 | **+8**（T31-1..T31-8）⇒ **356 → 364** |
| CMake | **0**（无新文件——T31 分布在既有测试文件） |
| 风险 | **中**——① **GDI 默认路径行为变更**（常规字号 ±2 px，多行步进/光标高/滚动范围/垂直居中全受影响）；② 3 个实现者同步；★ **回归面已核实**：既有测试**无一处对 `GDITextMeasurer::LineHeight` 硬编码数值**（`WidgetTests.cpp:108/139` 用 `LineHeight()` 现算，自洽跟随；其余为替身常量） |

---

## §8 批次

| 批 | 内容 | 新增用例 | 累计 | 验收 |
|---|---|---|---|---|
| **批一** | **② 窗口几何**（△6–△10 + △11/△12 替身同步） | T31-4/5/7（**3**） | **359** | 全链编译 + 存量全绿；★ **M1 阻塞解除** |
| **批二** | **① 行高口径**（△1–△3） | T31-1/2/3/6（**4**） | **363** | ★ **363 全绿** + **9 消费点逐项核对**（GDI 行高**会变**——须确认位移可解释） |
| **批三** | T31-8 跨后端基线 + harness 复跑 + 文档收口 | T31-8（**1**） | **364** | 探针读数 + 用户 MSVC |

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

---

## §10 修订记录

- **v1.0**（2026-10-08）初稿。**输入**：初设 **v1.1**（B-2 拍板 + P1–P8 吸收）+ **§1 详设必答逐题钉死**（D31-A GDI 实现代码含 6 条细节 + D31-B 判据 + D31-C 几何实现含 5 条细节 + D31-D 原子边界 + D31-E 三概念 + D31-F DPI 边界）+ **§2 基线 B1–B12 带行号** + **§3 逐文件 △1–△12**（★ △4/△5 零改动 = B-2 收益；△11/△12 = 条 33 实现者同步）+ **§4 契约 C31-1..C31-8**（★ C31-7 三概念分离 + C31-8 原子边界）+ **§5 盯防 10 条（可机检）** + **§6 用例 T31-1..T31-8**（精确装置：T31-5 四类退化输入 + T31-6 族名级局限 + T31-8 五要素）+ **§7 影响面**（95→95 / API +2 / 用例 356→364 / CMake 0 / 风险 中）· §8 三批（② 先行）· §9 O1–O7 · ★ **§2.5 仪器错误如实记录**。
