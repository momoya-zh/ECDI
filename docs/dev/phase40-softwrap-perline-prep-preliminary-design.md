# Phase 40 · 初步设计 —— 折行支每行「整串预备」O(L·N) 的消除（deferred #83）

> 状态：**v1.1**（2026-10-29 收 Qoder 评审：**4 must-fix + 7 should-fix + nits 全部落实**；三项待裁定已定案）
> **需求基线**：`docs/dev/phase40-softwrap-perline-prep-requirements.md` **v1.1**（§7 八项闸门 = 本稿 §1 的八个小节，逐题收口）
> **代码基线**：HEAD `a377026`（`docs(phase39): batch 2 -- requirements v1.2, roadmap v2.61 (#78 done)`）——需求稿落盘后**仅 `docs/` 变动**，`ECDI/src` / `ECDI/include` 与需求稿基线**逐字节相同**，全部行号沿用需求稿且无漂移（`git diff a377026 HEAD -- ECDI/src ECDI/include` 为空）。
> ★ **事实纪律**：本稿每条行号 = 2026-10-29 `grep -n` / `sed` 亲核 HEAD `a377026`；机检命令**都实际跑过**并把读数写进正文（不写「应该为 N」）。
> ★ **数字纪律**：只承诺①复杂度 ②分配次数 ③改前/改后记录制耗时；**不写「提升 X 倍」**（沿 Phase 28 / 38 / 39 先例）。
> ★★ **本稿最重要的一件事**：钉死「偏移表 = 谁的结果」。`DecodeUtf8`（`src/Render/Utf8Decode.h:17`，折行引擎用）与 `DecodeCodepointAt` / `SequenceLength`（`src/Core/UTF8.cpp:9/55`，字节换算用）对**非法 / 截断字节的语义不同**（前者跳过并停止，后者按 1 字节计数）⇒ 表必须定义为 `offsets[k] = CodepointIndexToByteOffset(text, k)`（SequenceLength 步进），才能**逐位替换**现有 `CodepointIndexToByteOffset(text, X)` 调用（含脏字节的既有分歧，§1.2 详述）。

---

## §1 初设必答（需求 §7 八项逐题收口）

### 1.1 问一 = 偏移表结构 + 单趟构建（逐位复刻 `CodepointIndexToByteOffset`）

**表结构**（纯数据、无字符串指针——字符串是临时量，表只存整数偏移）：

```cpp
// TextWidget 基类成员（protected / mutable）
mutable std::vector<std::uint32_t> m_cpByteOffsets;   // offsets[k] = 第 k 个码点的首字节偏移；offsets[count] = 末位
// offsets.size() == count + 1，其中 count == ByteOffsetToCodepointIndex(text, text.size())
```

**构建算法**（一行一码点，`byte += DecodeCodepointAt(...)`；★ `DecodeCodepointAt` 返回的步进量 == `SequenceLength(lead)`，与 `CodepointIndexToByteOffset` 的 `byteOffset += SequenceLength(text[byteOffset])` **同一函数、同一步长**）：

```cpp
namespace{   // TextWidget.cpp 新增匿名辅助（不暴露）
void BuildByteOffsets(const std::string& text, std::vector<std::uint32_t>& offsets){
	offsets.clear();
	std::size_t byte = 0;
	while (byte < text.size()){
		offsets.push_back(static_cast<std::uint32_t>(byte));
		char32_t cp = 0;
		byte += DecodeCodepointAt(text.data() + byte, text.size() - byte, cp);
	}
	offsets.push_back(static_cast<std::uint32_t>(byte));   // == CodepointIndexToByteOffset(text, count)
}
}
```

**等价性证明（表项 == 换算结果，逐位）**

| # | 命题 | 证明 |
|---|---|---|
| **T1** | `offsets[k] == CodepointIndexToByteOffset(text, k)`，对 `k ∈ [0, count]` | 两者推进式同为 `pos += SequenceLength(lead)`（`Core/UTF8.cpp:38-41` vs `byte += DecodeCodepointAt(...)`，而后者返回的正是 `SequenceLength(lead)`——见 §1.2 对齐表）；初值同为 0；`while (byte < text.size())` 与 `while (byteOffset < text.size() && cpIndex < codepointIndex)` 在「未到串尾」时同步推进。**归纳即得**。★ 末段截断处两者同样「越过 `text.size()`」（`"A\xC0"` 第二次推进：1 → 3，`size` = 2）——**这是刻意的**（做钳制反而与既有 `CodepointIndexToByteOffset` 不逐位对应） |
| **T2** | `count == offsets.size() - 1 == ByteOffsetToCodepointIndex(text, text.size())` | 迭代次数 == 推进到串尾的步数；`ByteOffsetToCodepointIndex(text, size)` 的状态机同为「未到串尾则推进一个 `SequenceLength`」⇒ 步数相同（`Core/UTF8.cpp:48-51`） |

★ **为什么表存 `std::uint32_t` 而非 `std::size_t`**：偏移上限 = 字符串字节数（本框架文本 ≤ 输入框长度量级，远 < 4 GiB）；半宽表项把表内存减半、缓存更友好。**截断防护 v1.1 定案（不再推给详设）**：`BuildByteOffsets` 入口加一条 `FRAMEWORK_ASSERT(text.size() <= (std::numeric_limits<std::uint32_t>::max)())`（`ECDI/include/ECDI/Core/ECDIAssert.h:24`；`:41` 在 Release 下展开为 `((void)0)`）——一行、`_DEBUG` 生效、零 release 成本；**不做运行期钳制**（钳制会改变越界行为，反而是语义变化）。需在 `TextWidget.cpp` 补 `#include <limits>`（或复用已有 `<cstdint>` 的 `UINT32_MAX`）。

### 1.2 问一之要害 = 两套解码语义的边界（★ 本节是全稿最容易做错的地方）

代码里存在**两套互不相同的 UTF-8 解码**：

| 解码器 | 位置 | 非法首字节 | 续字节非 `10xxxxxx` | 截断序列 | 用途 |
|---|---|---|---|---|---|
| `DecodeUtf8` | `src/Render/Utf8Decode.h:17` | **跳过**（`++i; continue`，不产码点） | `ok=false; ++i`（跳过） | **`break` 停止** | 折行引擎的 `cps`（`BuildTextLayout:211`）、`FontEngine::FitText:529` |
| `DecodeCodepointAt` / `SequenceLength` | `src/Core/UTF8.cpp:9/55` | 按 **1 字节**计数、产出 `lead` 值 | 不看续字节合法性，按前导**声称长**推进 | 按声称长推进（可能越界） | `CodepointIndexToByteOffset` / `ByteOffsetToCodepointIndex`、`TextBox` 非折行扫描（Phase 39 R1） |

★ **二者对合法 UTF-8 逐位相同**（前导 1/2/3/4 的分支判据在合法字节上等价）；**只对非法 / 截断字节分歧**（Phase 39 §1.3 已把 `C0 4A` / `C0 0A` 这类分歧划为「Phase 26/38 既有差异，不属任何性能 Phase」，记 R39-4）。

**⇒ 铁律：表的索引体系必须选 `SequenceLength`，而不是 `DecodeUtf8`。** 因为：

1. 现状代码里**所有** `CodepointIndexToByteOffset(text, X)` 调用（`OnPaint:1489-1507/1526-1527`、`DrawTextContent:405-406`、`CaretIndexFromLineX:895-910`、`BuildTextLayout` 的 `emitLine:229-230`）用的都是 `SequenceLength` 索引；
2. 把 `CodepointIndexToByteOffset(text, X)` 换成**表项** `offsets[X]`（读取侧经 `ByteOffsetAt(X)`），只有当 `offsets` **按 SequenceLength 构建**时，才对**任意串（含脏字节）逐位等价**；若按 `DecodeUtf8` 构建，则在脏字节上索引错位、行为漂移。

**但折行引擎的 `cps` 仍是 `DecodeUtf8` 语义**（`\n` 判定、禁则、软断点全在 `cps` 上）。因此**两个数组并存**：

| 数组 | 语义 | 消费者 | 是否改动 |
|---|---|---|---|
| `cps`（`DecodeUtf8`） | 跳过非法、截断即停 | 折行行切分（`cps[i]==U'\n'`、`FindLastSoftBreak`、`IsLineEnd/StartForbidden`）、`FontEngine::FitText` 的 advance 查表 | **不动**（现状即如此） |
| `m_cpByteOffsets`（SequenceLength） | 逐字节、脏字节按 1 算 | 全部 `CodepointIndexToByteOffset` 站点 | **新增（本 Phase）** |

**合法文本二者对齐**（`cps.size() == offsets.size()-1`），是本 Phase 的正常路径与全部测试样本；**脏字节二者分歧**，是既有行为，本 Phase **原样保留**（表用 SequenceLength 索引恰好复刻现状），**绝不作等价性判据的正确性来源**（overlong `C0 8A` 三档合法续字节仍同源，见 A40-7 / T40-5）。

★ **因此「同趟产出 cps + byteOffsets」在本 Phase 不做**：一个 `DecodeUtf8` 单趟只能产出一种语义的码点序列；要同时得两种，就是两趟（或一趟里分叉，更易错）。**本稿取两趟**：`DecodeUtf8(text)` 产 `cps`（现状，`:211` 保留）+ `BuildByteOffsets(text, ...)` 产偏移表（新增）。两趟皆 O(N)，总 O(N)，达成目标复杂度。★ 这是对需求 §7-1「一次构建」的**口径订正**：需求原意是「不要 L 次」，本稿落实为「每 rebuild 2 趟 O(N)，非每行 2 趟」——性质上是达成，形态上是两趟独立数组（因两套解码语义不能混）。

### 1.3 问二 = `FitText` 重载落点（纯新增 5 参；4 参一字不动）

**现状**（`TextMeasurer.h:68-119`）4 参默认体已含 Phase 39 R2 的 `byteStart` 外提。**不改它**。新增 5 参重载：

```cpp
// TextMeasurer.h：新增（放在 4 参 FitText 之后、LineHeight 之前）
/// @brief 断行适配（★ Phase 40：表感知重载——不再每行 `CodepointIndexToByteOffset` / 每行 `DecodeUtf8`）
/// @param cps         已解码码点序列（`DecodeUtf8` 语义——与调用方行切分**同源**）
/// @param byteOffsets 码点→字节偏移表（`SequenceLength` 语义——`byteOffsets[k]` = 第 k 个码点首字节）
/// @details 语义与 4 参完全相同（`TextFit{fitCp,width}`），只是**输入预算好**。
///          ★ 默认体 = 通用二分（用 `byteOffsets` 取 `byteStart`/`byteEnd`，仍 `MeasureText` 切片）；
///          GDI / FT 覆写 = 原生（GDI 用 `byteOffsets` + 窗口化宽串；FT 用 `cps` 免二次解码）。
///          ★ 4 参签名与默认体**一字不动**（D40-7：自定义 measurer 零改动契约）。
virtual TextFit FitText(const Font& font, const std::string& text, std::size_t startCp,
                        float maxWidth, const std::vector<char32_t>& cps,
                        const std::vector<std::uint32_t>& byteOffsets);
```

**默认体（表感知通用二分，放在 .h 内联——自包含，只依赖 `MeasureText` + 两个入参）**：

```cpp
TextFit FitText(const Font& font, const std::string& text, std::size_t startCp,
                float maxWidth, const std::vector<char32_t>& cps,
                const std::vector<std::uint32_t>& byteOffsets)
{
	(void)cps;   // ★ v1.1：默认体只需 byteOffsets（保 /W4 无「未使用形参」告警）
	if (byteOffsets.size() < 2) return TextFit{ 0, 0.0f };   // ★ v1.1：空/单元素表 ⇒ 早退（防 size()-1 下溢成 SIZE_MAX）
	const std::size_t totalCp = byteOffsets.size() - 1;      // ★ 与 cps.size() 仅合法文本相等（脏字节以本值为准，见 D40-E）
	if (startCp >= totalCp) return TextFit{ 0, 0.0f };
	const std::size_t byteStart = byteOffsets[startCp];
	const std::size_t firstEnd  = byteOffsets[startCp + 1];
	const std::string firstCpText = text.substr(byteStart, firstEnd - byteStart);
	if (MeasureText(font, firstCpText).width > (std::max)(0.0f, maxWidth)) return TextFit{ 0, 0.0f };
	std::size_t lo = 1, hi = totalCp - startCp;
	while (lo < hi){
		const std::size_t mid = lo + (hi - lo + 1) / 2;
		const std::size_t byteEnd = byteOffsets[startCp + mid];
		if (MeasureText(font, text.substr(byteStart, byteEnd - byteStart)).width <= (std::max)(0.0f, maxWidth))
			lo = mid; else hi = mid - 1;
	}
	const std::size_t byteEnd = byteOffsets[startCp + lo];
	return TextFit{ lo, MeasureText(font, text.substr(byteStart, byteEnd - byteStart)).width };
}
```

★ **`cps` 在默认体里没用**（默认体走 `MeasureText(切片)`，只需 `byteOffsets`）；保留 `cps` 入参是为了**GDI/FT 覆写的统一签名**（FT 必须用 `cps` 查 advance）。★ 默认体与 4 参默认体**输出逐位相同**（把 `CodepointIndexToByteOffset(text, x)` 换成 `byteOffsets[x]`，由 T1 等价；其余逐行同构）——这是 A40-4 的判据基础。

★★ **v1.1 must-fix（M1）：5 参是「并列虚」，不实现也能编译——但计数缝会全哑。** 全仓有**三个只覆写 4 参**的量具（`grep "TextFit FitText("  src/Tests` 恰好 3 处）：`WrapMeasurer`（`WidgetTests.cpp:647`，转发批一默认体并计 `m_fitCalls`）、`CountingFakeMeasurer`（`TextBoxTests.cpp:374`，自实现按宽除 8）、`RecordingFakeMeasurer`（`TextBoxTests.cpp:1978`，另记 `fed`）。`BuildTextLayout:270` 一旦改调 5 参，这三者的 4 参 override **一次都不会进**：
- **变红**：`TextBoxTests.cpp:2552`（`counter.fitCalls == 3`）、`:2954/2962/2970/2978/2984/3011/3022/3028/3034/3046`（`rec.fitCalls > 0` × 10）、`WidgetTests.cpp:1093`（`FitCalls() == lineCount`）、`:818/886/909`（`> afterBuild`）。
- **假绿（更危险）**：所有 `== 0` / `== afterBuild` 断言——`WidgetTests.cpp:813/877/881/904/915/931/939/982/996`、`TextBoxTests.cpp:2542/2545/2940/2991/3001`。「键命中 ⇒ 零 `FitText`」这条 Phase 29/38 的护栏会**静默失效**，`fed` 也会空掉（`Joined()` 返回空串）。

⇒ **硬落点（PD40-6）**：三个量具各加一条 5 参 override，**转发到自己的 4 参**——`return FitText(font, text, startCp, maxWidth);`（同名重载解析到本类 4 参，计数与 `fed` 原样保留；`WrapMeasurer` 则 `return TextMeasurer::FitText(...)` 保持「转发批一默认体」）。

**4 参默认体为何不动**：`TextMeasurer.h:62-67` 明文承诺「既有自定义 `TextMeasurer` / 测试派生类**零改动**即可工作」。若把 4 参改成转发 5 参（local 建表），虽行为不变，但**改了被承诺冻结的默认体**；且 4 参被外部（含测试 `FakeTextMeasurer`）直接实现 ⇒ 冻结最稳。5 参是**并列的第二个虚**，谁调谁选。

### 1.4 问三 = 表存储 + 失效键（★ 键收窄 5 维 + 单写者——对需求 D40-3 的订正）

**存储**：`TextWidget` 基类 `mutable` 成员（D40-2 ⓑ 落实），覆盖全部消费者（`DrawTextContent` / `OnPaint` / `CaretIndexFromLineX`）。★ **v1.1：唯一写者是 `EnsureCpByteOffsets`**——`BuildTextLayout` 只用**局部表**、绝不写基类成员（D40-L，理由见下）。

**★ 串身份：为什么基类表不能由 `BuildTextLayout` 发布（v1.1 must-fix M2 / 新增 R40-11）**

`BuildTextLayout` 有两个调用方，**喂的串不同**：

| 调用方 | 喂入串 | echo 影响 |
|---|---|---|
| `TextBox.cpp:466`（`RecalculateLines` 折行支） | `BuildDisplayText()` | **掩码串**：每码点 → `U+2022`（3 字节，`TextBox.cpp:236-259`；组合区间保留原文） |
| `TextWidget.cpp:342`（`GetTextLayout`，Label/Button 家族） | `m_text` | 明文（`TextWidget` 无 echo，恒 `Normal`） |

5 维键里的 `echo` 只区分**模式**，不区分「这张表是用哪个串建的」。一旦 `BuildTextLayout` 把表写进基类并盖当前键，后续 `OnPaint`（`textSource` = 掩码串）按键命中就会拿**明文表**去切掩码串——ASCII 明文 1 字节/码点、掩码 3 字节/码点 ⇒ `startByte` 放大 3 倍，`textSource.substr(startByte, …)` 在 `startByte > size()` 时抛 `std::out_of_range`。

★ **可达性如实记（v1.1 订正评审的路径判断）**：今日代码上这条路径**尚不可达**——`TextBox::GetPreferredSize` 在 `:201-202` 对「含 `\n` 或 `IsWrapActive()`」**提前返回** `MultilinePreferred()`（走 `RecalculateLines`，不进 `GetTextLayout`），而 `:205` 的基类调用只在单行非折行时发生、此时 `IsWrapLayoutActive()` 为假；`GetTextLayout` 的另两个调用点（`TextWidget.cpp:387` 的 `DrawTextContent`、`:444` 的 `GetPreferredSize`）对 TextBox 均不可达（`DrawTextContent` 的实际调用方只有 `Label:18` / `Button:306` / `CheckBox:107` / `Radio:116`，echo 恒 `Normal`）。⇒ 这是**潜伏坑**而非现行崩溃。**仍照修**：代价只是冷路径多建一次表，换来「键唯一决定串」这条结构性不变量。

**处置（D40-L，三条）**：
1. `BuildTextLayout` 内建**局部** `byteOffsets`（栈上 `std::vector`），供 `emitLine` 与 5 参 `FitText` 用，**不碰基类成员**；
2. 基类表只由 `EnsureCpByteOffsets(text)` 写 ⇒ 全仓**单一写者**（机检 C40-8）；
3. 键命中时加一条 **O(1) 串身份自检**：`m_cpByteOffsets.back() == text.size()`。掩码串与明文串长度必差 3 倍 ⇒ **命中错串必中**；命中后 **debug 触发 `FRAMEWORK_ASSERT`（弹窗 + 中止）、release 降级为「不复用、直接重建」**（正确性优先于省一次 O(N)）。这条自检同时也是「同键不同串」类未来回归的通用护栏，且**行为可断言**（T40-8）。

**失效键 = 字符串字节的决定因素，仅 5 维**：

```cpp
// TextWidget 基类（protected）
struct ByteMapKey{
	std::size_t revision  = 0;   // TextRevision()
	int         echo      = 0;   // EchoMode（TextWidget 恒 0）
	bool        composing = false;
	std::size_t compStart = 0;
	std::size_t compLen   = 0;
	bool operator==(const ByteMapKey&) const = default;   // C++20
};
[[nodiscard]] virtual ByteMapKey CurrentByteMapKey() const {   // TextWidget 默认：仅 revision
	return ByteMapKey{ m_textRevision, 0, false, 0, 0 };
}
// TextBox override（加入 echo/composing——显示串的其余 3 维）：
ByteMapKey TextBox::CurrentByteMapKey() const {
	return ByteMapKey{ TextRevision(), static_cast<int>(m_echoMode),
	                   m_isComposing, m_compositionStart, m_compositionLength };
}
```

**★ 为什么只需这 5 维、而不是 `LineKeyMatches` 的 12 维（需求 D40-3 原写「失效键 ⊇ 12 维」，本稿收窄——`C40` 如实记订正）**：

`byteOffsets[k]` 是**纯 UTF-8 结构量**——它只取决于「字符串的字节是什么」，**与 font / dpi / wrapWidth / hasMeasurer / wrapRequested 全无关**（这些只影响**宽度**，不影响码点↔字节映射）。字符串字节只由 `(m_text 版本, echo 模式, 组合态 3 项)` 决定 ⇒ 键 = 这 5 维即充分。

**「无半状态」证明（v1.1 重写——原稿的「四者传入的串都是当前显示串」是假设，现在是不变量）**：

- **单写者**：基类表只在 `EnsureCpByteOffsets(text)` 内被赋值 ⇒ 「表是用哪条串建的」由那一次调用的 `text` 唯一确定，无第二处覆盖（C40-8 机检）。
- **表侧不变量**：`m_cpByteValid ⇒ m_cpByteOffsets.back() == (建表时的 text).size()`，且在**每次命中**时用当次 `text.size()` 复核（D40-L-3）⇒ 「键相同但串不同」当场暴露，不会静默复用。
- **行侧不变量**：`m_lines` / `m_wrapLayout` 各自键命中 ⇒ 行结构是当前文本的行结构（Phase 38 既有，本 Phase 不动）。
- **两个不变量合起来**：行缓存给的是**码点区间**（`startCp` / `cpCount`），本表给的是**码点→字节**，二者都由「当前串」独立派生 ⇒ 用「当前行的码点区间」查「当前串的表」必然自洽；**不存在**「行是旧的、表是新的」错配（任一缓存失配都会各自重建，且表不会跨串存活）。

★ 代价如实记：折行路径上 `BuildTextLayout` 建局部表、`DrawTextContent` 再 `Ensure` 建成员表 ⇒ **冷路径两次建表**（各 O(N)，热路径 0 次）。这是为「单写者」付的账，值得。

★ **dpi 同源警告的消解**（需求 suggested-1 的实质）：`TextBox.cpp:511-512` 注释警告「dpi 算式与 `TextWidget.cpp` 的 wrap 指纹必须逐字同源」。本 Phase **表键根本不含 dpi** ⇒ 表的生命周期与 dpi 完全解耦，**连这个同源问题都不再适用于表**；而 `m_lines` 的 dpi 与 `m_wrapLayout` 的 dpi 仍各自按现状（那是行/布局缓存的事，本 Phase 不碰）。⇒ 需求 §7-3 要求「dpi 算式在基类只算一次」对本表**不适用**（表不需 dpi），这比「基类统一算一次」更彻底：**不需要算**。

**表构建的触发（消费者懒建）**：

```cpp
// TextWidget 基类（protected / const，mutate mutable 成员）——★ 定义落 TextWidget.cpp（S7：要用 BuildByteOffsets）
void EnsureCpByteOffsets(const std::string& text) const{
	if (m_cpByteValid && m_cpByteKey == CurrentByteMapKey()){
		if (m_cpByteOffsets.empty() || m_cpByteOffsets.back() == text.size())
			return;   // 命中 ⇒ 0 分配 0 扫描（正常路径：长度一致）
		// ★ D40-L-3 串身份自检：同键不同串（明文表 vs 掩码串）在这里当场暴露
		FRAMEWORK_ASSERT(m_cpByteOffsets.back() == text.size());   // 走到这里必然不等 ⇒ debug 弹窗并中止
		// release 降级：**不复用错串表**，直接重建（正确性优先于省一次 O(N)）
	}
	BuildByteOffsets(text, m_cpByteOffsets);
	m_cpByteKey   = CurrentByteMapKey();
	m_cpByteValid = true;
}
// 读取统一走访问器（别让裸下标散在三个站点）
[[nodiscard]] std::size_t ByteOffsetAt(std::size_t cpIndex) const{
	FRAMEWORK_ASSERT(cpIndex < m_cpByteOffsets.size());
	return m_cpByteOffsets[cpIndex];
}
```

★ **为什么「debug 红 + release 重建」而不是只 assert**：只 assert 的话，release 构建会带着错串表继续跑（越界）；而「降级重建」同时给出**可断言的行为**（T40-8 在两种构建下都能判：表被重建 ⇒ `back() == 新串 size()`），并把「同键不同串」从静默越界变成**最多慢一次**。

调用点（**三个**，各喂自己 slicing 的显示串）：`DrawTextContent`（`m_text`，见 §1.5 的 M4）、`OnPaint`（`textSource`）、`CaretIndexFromLineX`（`display`）。**`BuildTextLayout` 不在其中**（局部表，D40-L-1）。

### 1.5 问四 = K7 / K8 paint 重写（查表 O(1)，命令流逐位不变）

**K8 `DrawTextContent`（wrap 分支，`TextWidget.cpp:405-406`）**：

```cpp
// 现状
const std::size_t b0 = CodepointIndexToByteOffset(m_text, line.startCp);
const std::size_t b1 = CodepointIndexToByteOffset(m_text, line.startCp + line.cpCount);
// 改后（★ M4：必须自己 Ensure——GetTextLayout 键命中时直接 return 缓存、
//        根本不进 BuildTextLayout，不能指望上游建表）
EnsureCpByteOffsets(m_text);
const std::size_t b0 = ByteOffsetAt(line.startCp);
const std::size_t b1 = ByteOffsetAt(line.startCp + line.cpCount);
```

`lineText = m_text.substr(b0, b1-b0)` **逐位不变**（T1：`ByteOffsetAt(k) == CodepointIndexToByteOffset(m_text, k)`）。

**K7 `OnPaint`（`TextBox.cpp:1489-1527`）**：进入循环前 `EnsureCpByteOffsets(textSource);`，然后把 `:1489-1490` / `:1502-1503` / `:1526-1527` 的六处 `CodepointIndexToByteOffset(textSource, X)` 全改 `ByteOffsetAt(X)`。**关键**：`textSource` 在 Normal = `m_text`、echo = `BuildDisplayText()`，表建在**同一个 `textSource`** 上 ⇒ 切片字节不变（D40-L-3 的自检在 debug 下再兜一层）。

★ **软断点行尾空格 / 选区 / 下划线求交**（`hlStartByte` / `ulStartByte` 等）全部改查表，`textSource.substr(startByte, hlStartByte - startByte)` 的相对偏移**逐位不变**（因为 `startByte` 与 `hlStartByte` 都来自同一张表、同一串）。

**等价性不变式**：表是 `CodepointIndexToByteOffset` 的逐位预计算（T1）⇒ 任何 `CodepointIndexToByteOffset(textSource, X)` → `ByteOffsetAt(X)` 都是**纯替换、零语义变化**；`textSource.substr(a, b-a)` 的字节与改前逐位相同 ⇒ `DrawText` 命令的 `text` 字段逐位相同。

### 1.6 问五 = K9 重写（查表 O(1)，行内 `MeasureText` 前缀循环保留）

`CaretIndexFromLineX`（`TextBox.cpp:890-919`）现状 5 处换算，逐一改查表：

```cpp
const std::string display = BuildDisplayText();
EnsureCpByteOffsets(display);                       // ★ 新增：按 display 建/复用表
const auto [lineStartCp, lineEndCp] = LineRange(lineIndex);
const std::size_t startByte = ByteOffsetAt(lineStartCp);     // :895 改
const std::size_t endByte   = ByteOffsetAt(lineEndCp);       // :896 改
const std::string lineText = display.substr(startByte, endByte - startByte);
const std::size_t lineCpCount = lineEndCp - lineStartCp;         // :899 改：O(1)，免 ByteOffsetToCodepointIndex(lineText,...)
for (std::size_t i = 0; i < lineCpCount; ++i){
	const std::size_t byte = ByteOffsetAt(lineStartCp + i + 1) - startByte;   // :905 改
	const Size prefix = measurer.MeasureText(font, lineText.substr(0, byte));     // 保留（测量不可省）
	...
	const Size prevPrefix = (i == 0) ? Size{}
		: measurer.MeasureText(font, lineText.substr(0, ByteOffsetAt(lineStartCp + i) - startByte));  // :910 改
	...
}
```

- **`:899` 的 `ByteOffsetToCodepointIndex(lineText, lineText.size())` 直接消掉**：`lineText` 的码点数 = 码点区间长度 `lineEndCp - lineStartCp`（`LineRange` 已给）⇒ O(1)，不再扫 `lineText`。
- **行内 `MeasureText` 前缀循环保留**（测量本身不可省——`A40-3` 只承诺「字节偏移免费」，不承诺免测量；弱化成「O(N) + 行内测量」）。
- 命中 cp 索引逐位不变（改的只是「码点→字节」的取法，全由 T1 保证）。

### 1.7 问六 = 分配 churn 探针（沿 Phase 39 §1.6 四条硬要求）

**不入 CMake、不进 `src/Tests`；源码贴详设附录，命令写进 A40 记录。** 四条硬要求：

| # | 要求 | 对应 | 理由 |
|---|---|---|---|
| 1 | **同一支探针、同一复刻体，改前改后各跑一次** | A40-3 | 改前改后用同一 `loop_current` / `BuildTextLayout` 复刻体，倍率才是复杂度比（不是混入成员访问噪声的跨口径比） |
| 2 | **输出堆分配次数与字节**（全局 `operator new` 计数） | A40-2 | 断言「FT 每 `FitText` 的 `vector` 分配 → 0；GDI 尾拷贝 → 窗口长度」 |
| 3 | **输出四档 1k / 5k / 10k / 20k** | A40-3 | 「n→4n 耗时比 ≈ L·N → ≈ N」需多档判二次性 |
| 4 | **命令 + flag + 机器入档** | A40-7(记录) | 沿数字纪律；本机复算 = `g++ -std=c++20 -O2 -I ECDI/include probe.cpp ECDI/src/Core/UTF8.cpp -o probe.exe` |

★ **为什么强调「同体」**：A40-2 的「FT 每 FitText 分配 → 0」是**函数级**断言，必须让探针真的调 `FitText`（5 参），而不是复刻二分；A40-3 的倍率必须同体（改前 `loop_current` 复刻 vs 改后同一复刻体的表版）——同 Phase 39 R39-10。

### 1.8 问七 = 批 0 基准 + 问八 = 编译批次

**批 0（改任何源码前跑，独立一笔提交；采不到不得动 R1/R2）**：

| 表 | 内容 | 消费者 | 落点 |
|---|---|---|---|
| **A40-4 表** | 一组 `(text, startCp, maxWidth)` 经 **4 参** `FitText`（HEAD）的 `TextFit{fitCp,width}`，**含 `startCp≠0`、CJK/ASCII/emoji、overlong `C0 8A`、`startCp>=totalCp`、空串** | A40-4 / T40-2 | 复用/扩 `kFitGolden`（`TextBoxTests.cpp:147`；其 `:133` 注释自证「表 A（**Phase 39 批 0** 采集）」——v1.1 已核，出处引用无误） |
| **A40-5 基线** | 四函数体内 `CodepointIndexToByteOffset` / `ByteOffsetToCodepointIndex` 出现次数（**HEAD 亲采**）：`BuildTextLayout`=2（`:229-230`）、`DrawTextContent`=2（`:405-406`）、`OnPaint`=6（`:1489-1490/1502-1503/1526-1527`）、`CaretIndexFromLineX`=5（`:895-896/899/905/910`），目标全 0 | A40-5 / C40-1..4 | 机检脚本同批采 |

★ **用例数 538 的出处（v1.1 如实标注）**：**继承** Phase 39 收口实测（2026-10-29 四构建链 538/538），本稿**未重跑运行器**。改前改后的准确数一律以运行器汇总行 `Tests: N` 为准（C40-7），不拿本文档数字当真理。

**编译批次（D40-9）**：`TextWidget.h`（+表成员 / `ByteMapKey` / `EnsureCpByteOffsets` / `CurrentByteMapKey`）与 `TextMeasurer.h`（+5 参 `FitText`）**都是公共头** ⇒ 一次改两处 = 近全量重编（Phase 38 实测冷全量 35 min 量级）⇒ **R1+R2+R3+R4 同批落、同批编一次**，不得分两轮验。`TextWidget.cpp` / `TextBox.cpp` / `GDITextMeasurer.cpp` / `FontEngine.cpp` / `FreeTypeTextMeasurer.cpp` 是 .cpp，各编一个 TU。`Utf8Decode.h` 是 src 私有头（非公共头），改动不触发全量。

---

## §2 决策定案 D40-A..M（对应需求 D40-1..9 + 本稿新增四项）

| 定案 | 对应需求 | 结论 | 依据 |
|---|---|---|---|
| **D40-A** | D40-1 | 范围 **B + K7 + K8 + K9 全收** | 同一根因（无一次性偏移表）；「单修 B 等于拆半」（P39-N2） |
| **D40-B** | D40-2 | 架构 **ⓑ 落实 + 单写者**：表落 `TextWidget` 基类；**唯一写者 `EnsureCpByteOffsets`**；读取站点 `DrawTextContent` / `OnPaint` / `CaretIndexFromLineX` 各按自己切片的显示串懒建；`BuildTextLayout` 只用**局部表**、**不发布**（D40-L）；`CurrentByteMapKey()` protected virtual 提供串身份（base=revision；TextBox=revision+echo+composing） | §1.4；ⓐ（measurer 内缓存）只解决 B，不解决 K7/K8/K9（后者走自由函数，不经 measurer）；**双写者会让「键相同、串不同」静默复用**（M2） |
| **D40-C** | D40-3 | **表键收窄为 5 维**（revision/echo/composing），**非**需求原写的「⊇ `LineKeyMatches` 12 维」；dpi 不进键。★ **成立前提 = D40-L 单写者**（否则两个不同串可共享同键，5 维不健全） | §1.4：`byteOffsets` 是纯 UTF-8 结构量，与 font/dpi/wrap 无关；「无半状态」由「单写者 + 命中自检 + 行各自键独立」保证。★ **这是对需求的口径订正**，详设回写需求 v1.2 |
| **D40-D** | D40-4 | GDI 尾拷贝 → **倍增窗口**：`text.substr(byteStart, offsets[min(startCp+W,count)] - byteStart)`，W 倍增至 `fit < W` 或 `W == 剩余`；**前缀 extent 与窗口长无关 ⇒ 逐位不变**。★ **单位澄清（v1.1）**：`fitW` 是 **wchar 数**（`GDITextMeasurer.cpp:203-205`），`W` 是**码点数**；窗口 `wide.size() ≥ W` 恒成立 ⇒ `fit < W ⟹ fit < wide.size()` ⟹ 窗口未耗尽 ⟹ 前缀答案有效。astral（1 码点=2 wchar）会多倍增几轮、不改变结论 | GDI `GetTextExtentExPointW` 的 `fitW` 只依赖前缀；窗口化把「整尾拷贝 O(tail)」降到 O(消费段）。★ 这条推理必须逐字写进详设，否则后人会把「用 `fit < W` 判窗口未耗尽」修成 bug |
| **D40-E** | D40-5 | FT **复用传入 `cps`**：5 参不再 `DecodeUtf8(text)`（`:529` 删），改从 `cps[startCp..]` 线性扫描。★ **`totalCp` 必须取 `byteOffsets.size()-1`，不得取 `cps.size()`**（v1.1 M3）：`:509` 现状是 `ByteOffsetToCodepointIndex(text, text.size())`（SequenceLength 空间），它同时喂 `:510` 守卫与 `:523` 无 face 兜底；`cps.size()` 是 `DecodeUtf8` 空间，脏字节上**不等** ⇒ 会违反 A40-4 逐位相同 | 与现状 `DecodeUtf8(text)` 同为 `DecodeUtf8` 语义 ⇒ 线性扫描逐位相同；零 `vector` 分配。★ `for (i = startCp; i < codepoints.size(); ++i)` 本身是「用 SequenceLength 空间的 `startCp` 索引 DecodeUtf8 空间」的既有分歧——**原样保留、不得顺手修**（会改脏字节行为） |
| **D40-F** | D40-6 | YAGNI **解除** | ≥4 真实消费者（B×L、K7×L、K8×L、K9×1）+ wrap 引擎本就解 `cps` 一次 |
| **D40-G** | D40-7 | 4 参 `FitText` 签名 + 默认 fallback **一字不动**；表感知 = **纯新增 5 参重载** | §1.3；`TextMeasurer.h:62-67` 零改动契约 |
| **D40-H** | D40-8 | 表由**消费者懒建**、与 wrap 分支无关；**非折行 `RecalculateLines` 的 `m_text` 单趟扫描（P39-R1）零改动**（那只是行结构）；paint/hit-test 在非折行下用**显示串**建表 | §1.4 触发点；非折行 paint（`OnPaint`）与 hit-test（`CaretIndexFromLineX`）同样走 `textSource`/`display`，需要表 |
| **D40-I** | D40-9 | R1..R4 同批落、两个公共头同批编一次 | §1.8 |
| **D40-J** | ★ 本稿新增（§1.2） | **表用 SequenceLength 索引**（`offsets[k]=CodepointIndexToByteOffset(text,k)`），**非** `DecodeUtf8` 索引；`cps`（DecodeUtf8）与 `byteOffsets`（SequenceLength）**两个独立数组**，合法文本对齐、脏字节保留既有分歧。**定案：两趟**（不合并） | §1.2；用 DecodeUtf8 索引会在脏字节上索引错位、行为漂移。★ **T1 依赖跨 TU 隐式耦合**（`DecodeCodepointAt` 的步进恰等于 `SequenceLength`）——若有人把它的截断步进钳到 `size`，T1 静默失效且**不会编译报错** ⇒ T40-1 的 `"A\xC0"` 样本是这道唯一护栏，**标为必测**（R40-12） |
| **D40-K** | ★ 本稿新增 | 5 参 `FitText` 签名 = **两个独立参数** `const std::vector<char32_t>& cps` + `const std::vector<std::uint32_t>& byteOffsets`（**不引入** `DecodedText` 新公共类型）——**定案** | 保持 **public API +1**（仅重载；无新类型）；`cps` 供 FT、`byteOffsets` 供 GDI/默认体。`DecodedText` 会把 `<vector>`/`<span>` 语义拖进 `UTF8.h` 而买不到任何东西 |
| **D40-L** | ★ v1.1 新增（M2/M4/S5） | **基类表单写者**：`BuildTextLayout` 建**局部表**、不发布；`EnsureCpByteOffsets` 唯一写入；命中时做 O(1) **串身份自检**（`offsets.back() == text.size()`：debug `FRAMEWORK_ASSERT` 中止、release **降级重建**）；读取统一走 `ByteOffsetAt(k)`（带 assert，禁止裸下标）；`DrawTextContent` **自己 Ensure**（M4） | §1.4；`BuildTextLayout` 两个调用方喂不同的串（掩码 vs 明文），发布即埋雷；自检把「同键不同串」从**静默越界**变成 **debug 当场红 + release 降级重建**（R40-11 / T40-8） |
| **D40-M** | ★ v1.1 新增（S2） | 需求 **R4**（`Utf8Decode.h:20` 的 3× 过预留）**本 Phase de-scope**：`reserve(utf8.size())` 是**上界估计**（不加一趟计数无法收紧，收紧 = 多一趟 O(N) 换掉 3× 容量），其危害全在「L 次重复」；D40-E 删掉 FT 的 L 次 `DecodeUtf8` 后，剩余调用点只有 `BuildTextLayout:211`（每 rebuild 1 次）与 `FontEngine::MeasureText:474`（`measureCache` 命中，`:497-501`）⇒ **无放大** | §8 待你确认（**scope 变更须显式授权**；若你要求保留，则加一行 `reserve` 调整 + 一条分配探针判据，代价是多碰一个共享核心头） |

★ **D40-C / D40-J / D40-M 是对需求的三处实义订正**（键收窄、索引选型、R4 de-scope）——需求稿**不回改**（保持评审基线 v1.1），订正记在 §8，**详设时统一回写需求 v1.2**（沿 Phase 39 的 D39-G 形态订正先例）。其中 **D40-M 属 scope 变更**，须你显式授权后才生效。

---

## §3 方案 PD40-1..6（逐文件落点 + 代码草案）

### PD40-1 · `ECDI/include/ECDI/Widget/TextWidget.h`（protected 面 **+4**；public API +0）

```cpp
protected:
	// ── Phase 40：码点→字节偏移表（★ D40-B/D40-C/D40-L：基类持有，单写者，消费者懒建）──
	/// @brief 表身份（决定字符串字节的 5 维；与 LineKeyMatches 的 12 维**不同**——只管字节，不管宽度）
	struct ByteMapKey{ std::size_t revision=0; int echo=0; bool composing=false; std::size_t compStart=0; std::size_t compLen=0;
	                   bool operator==(const ByteMapKey&) const = default; };
	[[nodiscard]] virtual ByteMapKey CurrentByteMapKey() const;   // ★ 定义在 TextWidget.cpp（S7）
	void EnsureCpByteOffsets(const std::string& text) const;      // ★ 唯一写者；命中带串身份自检（D40-L）
	[[nodiscard]] std::size_t ByteOffsetAt(std::size_t cpIndex) const;   // ★ 读取访问器（带 assert；禁止裸下标）
	mutable std::vector<std::uint32_t> m_cpByteOffsets;        // offsets[k] = 第 k 码点首字节；size = count+1
	mutable bool        m_cpByteValid = false;
	mutable ByteMapKey  m_cpByteKey{};
```
★ 头新增 `#include <cstdint>`（`std::uint32_t`）；`<vector>` 已在（`:11`）。★ **`ByteMapKey` 的 `operator== = default` 需 C++20**（项目即 C++20）。★ `CurrentByteMapKey` / `EnsureCpByteOffsets` / `ByteOffsetAt` **三个实现都落 `TextWidget.cpp`**（前两者要用匿名 namespace 里的 `BuildByteOffsets`；`ByteOffsetAt` 与它们同处一 TU 便于自检一致）。

### PD40-2 · `ECDI/include/ECDI/Render/TextMeasurer.h`（public API **+1**；头新增 `<cstdint>` / `<vector>`）

- 加 `#include <cstdint>`、`#include <vector>`（`<string>` 已在）。
- 加 5 参 `virtual FitText(...)`（§1.3 草案 + 默认体）。**默认体在头内内联**（自包含，只依赖 `MeasureText` + 两入参）：含 `(void)cps;` 与 `byteOffsets.size() < 2` 早退（v1.1 防下溢）。
- **4 参 `FitText` 及其默认体一字不动**（D40-G）。

### PD40-3 · `ECDI/src/Widget/TextWidget.cpp`（BuildTextLayout **局部表** + DrawTextContent 查表 + 表设施实现）

- **匿名 namespace 加 `BuildByteOffsets`**（§1.1 草案；入口一条 `FRAMEWORK_ASSERT(text.size() <= UINT32_MAX)`）。
- **实现** `CurrentByteMapKey` / `EnsureCpByteOffsets` / `ByteOffsetAt`（§1.4 草案）。
- `BuildTextLayout`：`:211` `cps = DecodeUtf8(text)` **保留**；其后建**局部** `std::vector<std::uint32_t> byteOffsets; BuildByteOffsets(text, byteOffsets);` —— ★ **不写基类成员**（D40-L-1）。
- `:270` `measurer.FitText(font, text, scan, limit)` → **5 参** `measurer.FitText(font, text, scan, limit, cps, byteOffsets)`（传**局部**表）。
- `emitLine` `:229-230` 的 `CodepointIndexToByteOffset(text, lineStart/end)` → `byteOffsets[lineStart/end]`（局部表的下标；`emitLine` 需拿到该表——按值捕获引用，详设给签名 diff）。
- `DrawTextContent` `:405-406` → **先 `EnsureCpByteOffsets(m_text);`**（M4）再 `ByteOffsetAt(...)`（§1.5）。
- **不得触碰**：`DecodeUtf8` 的 `cps` 来源、行切分/禁则/软断点全部逻辑、`totalHeight` 计算、`GetTextLayout` 的六元组指纹（`:332-337`）。

### PD40-4 · `ECDI/src/Widget/TextBox.cpp`（OnPaint 查表 + CaretIndexFromLineX 查表 + CurrentByteMapKey override）

- `CurrentByteMapKey` override（§1.4 草案）。
- `OnPaint`：循环前 `EnsureCpByteOffsets(textSource);`；`:1489-1490/1502-1503/1526-1527` 六处换算 → `ByteOffsetAt(X)`。
- `CaretIndexFromLineX`：§1.6 草案（`:895/896/899/905/910` 全改；`:899` 直接 `lineEndCp - lineStartCp`）。
- **不得触碰**：`RecalculateLines` 非折行单趟扫描（P39-R1，`:475-496`）、`BuildDisplayText`（`:236-259`）、`LineKeyMatches/Save`（`:509-548`）、掉行/滚动/选区逻辑、`GetPreferredSize` 的 `:201-202` 分支判据。

### PD40-5 · `ECDI/src/Render/` 三后端 + **三个私有头**（5 参原生覆写）

★ v1.1（S3）：`GDITextMeasurer.h:44`、`FontEngine.h:141`、`FreeTypeTextMeasurer.h:30` **都要加 5 参声明**——三者均在 `src/Render/`（**src 私有头**，不计入公共头 100）。

- **GDI**（`GDITextMeasurer.h/.cpp`）：新增 5 参——`byteStart = byteOffsets[startCp]`（O(1)）；**倍增窗口**拷宽串（D40-D，含单位推理）；`totalCp = byteOffsets.size()-1`。4 参覆写**保留**（外部兼容；内部已由 `BuildTextLayout` 调 5 参）。
- **FontEngine**（`FontEngine.h/.cpp`）：新增 5 参——**`totalCp = byteOffsets.size() - 1`**（M3 订正，**不是** `cps.size()`）；`cps` 只用于从 `startCp` 起的 advance 扫描（删 `:529` 的 `DecodeUtf8`）；零分配。★ `for (i = startCp; i < cps.size(); ++i)` 的混空间既有分歧原样保留。
- **FreeTypeTextMeasurer**（`.h/.cpp`）：5 参转发 `m_engine->FitText(...5 参...)`；无 engine 兜底同现状（`ByteOffsetToCodepointIndex`→ 改 `byteOffsets.size()-1`，行为等价）。

### PD40-6 · `ECDI/src/Tests/*`（**既有量具三处必改** + 新增用例；详见 §4）

★★ **M1 硬落点（不改就会红/假绿）**：三个只覆写 4 参的量具各加一条 5 参 override，**转发到自己的 4 参**：

```cpp
// WrapMeasurer（WidgetTests.cpp:647）——保持「转发批一默认体」
TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
                const std::vector<char32_t>&, const std::vector<std::uint32_t>&) override{
	++m_fitCalls;
	return TextMeasurer::FitText(font, text, startCp, maxWidth);
}
// CountingFakeMeasurer（TextBoxTests.cpp:374）/ RecordingFakeMeasurer（TextBoxTests.cpp:1978）
// ——转发到本类 4 参（计数 + fed 原样保留）
TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
                const std::vector<char32_t>&, const std::vector<std::uint32_t>&) override{
	return FitText(font, text, startCp, maxWidth);   // 同名重载 ⇒ 命本类 4 参（`fed` 里仍是整串）
}
```

★ **测试接缝（S4）**：`m_cpByteOffsets` 是 protected、`BuildByteOffsets` 在匿名 namespace ⇒ T40-1 **不能**经 `EnsureCpByteOffsets` 断言（否则第二个样本会键命中拿到旧表），须照 `TestableLabel` 的 `using` 形态（`WidgetTests.cpp:444-457`）开一条**直调 `BuildByteOffsets`** 的静态接缝：

```cpp
class ByteMapProbe : public TestableLabel{
public:
	using TextWidget::ByteOffsetAt;          // 读侧
	static std::vector<std::uint32_t> Build(const std::string& s);   // 定义在 TextWidget.cpp 的测试可见包装（详设落点）
};
```
★ 若不愿为测试暴露内部符号，退路 = 在 `WidgetTests.cpp` 内**复刻** `BuildByteOffsets` 并按 T40-1 断言「复刻体 == `CodepointIndexToByteOffset`」——但那是**测复刻体不是测产品码**，判据打折，**不推荐**；本稿取「静态接缝」。这条取舍进详设必答。

---

## §4 测试 T40-1..8（538 → **546**，+8；★ 另有三处**既有量具被改**，见 PD40-6）

| # | 用例 | 断言 | 为什么它必须存在 |
|---|---|---|---|
| **T40-1** | **表项等价（核心不变式 T1）**：一组样本（合法 CJK/ASCII/emoji + overlong `C0 8A` 三档 + 截断 `A\xC0` + 非法首字节 `\x80\x8A`），经 **`BuildByteOffsets` 静态接缝**（PD40-6，**不经 `EnsureCpByteOffsets`**——否则第二个样本键命中拿到旧表）逐 `k` 断言 `table[k] == CodepointIndexToByteOffset(text,k)`，并断言 `table.back() == text.size()` | 逐位相等（`k ∈ [0,count]`） | 这是「表 == 换算」的**唯一直接证据**；脏字节上尤其关键（DecodeUtf8 与 SequenceLength 分歧时，表仍必须等于 SequenceLength 版）。★ `"A\xC0"` 是 T1 依赖的跨 TU 隐式耦合的**唯一护栏**（R40-12），**必测** |
| **T40-2** | **`FitText` 4 参 vs 5 参等价**：A40-4 表的 `(text,startCp,maxWidth)`，`TextFit` 逐位相等（含 `startCp≠0`、overlong、`startCp>=totalCp`、空串） | `fitCp`/`width` 逐位 | D40-G/D40-K 的契约；★ 只用 `startCp==0` 会假绿（R40-3 同型） |
| **T40-3** | **行表逐位（A40-1）**：既有 538 全绿 + wrap/非折行新样本 `m_lines` / `m_wrapLayout.lines` 逐位 | 现有断言 + 新样本 | 红线：R1 只改「字节换算取法」，行切分/禁则/`ownedEnd` 全不动 |
| **T40-4** | **命中逐位（A40-6）**：wrap/非折行下 `(lineIndex, innerX)` 样本，`CaretIndexFromLineX` 返回码点索引逐位 | 相等 | D40-H / §1.6；`:899` 简化不能改结果 |
| **T40-5** | **跨支 overlong 同源（A40-7）**：`C0 8A` / `E0 80 8A` / `F0 80 80 8A` 三档，表与 `BuildTextLayout:243` 的 `cps[i]==U'\n'` 断行**逐位一致**（都是 2 行） | 行表 + 表项 | §1.2 的洞就在这三档；★ 负对照见 R40-3 |
| **T40-6** | **paint 字节偏移等价**：wrap 长文本经 `RecordingBackend`，改前/改后每行 `DrawText` 的 `text` 字节逐位（含选区/下划线求交的相对偏移） | 命令流逐位 | §1.5 等价性的直接证据（`substr` 字节不变） |
| **T40-7** | **GDI 窗口化等价**（若可无头）：`TestableGDI` 下 4 参 vs 5 参 `FitText` 逐位（含跨窗口边界） | 逐位 | D40-D 倍增窗口的精确性（前缀 extent 与窗口长无关）；若 GDI 需真 DC 无法无头，则降级为**详设手工探针**并记 R40-4 |
| **T40-8** | **串身份护栏（M2 / D40-L）**：`TestableTextBox` 置 `Password` + word wrap（并造一个 `Comp` 组合态样本），先驱动折行（`GetPreferredSize` → `RecalculateLines` → 局部表）再用**两个不同串**经接缝调用 `EnsureCpByteOffsets`：(a) 同一个串两次 ⇒ 第二次**零分配**（表地址不变）；(b) 明文串后紧跟掩码串（**键相同**）⇒ 第二次**必须重建**（`ByteOffsetAt` 的 `back()` == 掩码串长，且切片落在 `display.size()` 内） | 表被重建 / 切片不越界 | D40-L 单写者的回归护栏：谁把表发布回 `BuildTextLayout`，或删掉命中自检，这条必红；★ 只断言 assert 会引入「debug-only」判据，故用「重建行为」作判据（两种构建都可判） |

★ **不做**：不断言耗时（D40-3 数字纪律）；不测 20k 行表内容（太长，只测 count 与复杂度探针）。

---

## §5 契约 C40-1..8（每条附机检；C40-1..4 的基线已在 HEAD 实跑）

| # | 契约 | 机检命令（函数体口径） | 今日基线 → 目标 |
|---|---|---|---|
| **C40-1** | `BuildTextLayout` 体内 `CodepointIndexToByteOffset` / `ByteOffsetToCodepointIndex` **== 0** | `awk '/^TextWidget::TextLayout TextWidget::BuildTextLayout/,/^}/' ECDI/src/Widget/TextWidget.cpp` → `grep -c -E "CodepointIndexToByteOffset\|ByteOffsetToCodepointIndex"` | **2**（`:229`/`:230`）→ **0** |
| **C40-2** | `DrawTextContent` 体内 == 0 | `awk '/^void TextWidget::DrawTextContent/,/^}/' ...` → `grep -c ...` | **2**（`:405-406`）→ **0** |
| **C40-3** | `OnPaint` 体内 == 0 | `awk '/^void TextBox::OnPaint/,/^}/' ECDI/src/Widget/TextBox.cpp` → `grep -c ...` | **6**（`:1489-1490/1502-1503/1526-1527`）→ **0** |
| **C40-4** | `CaretIndexFromLineX` 体内 == 0 | `awk '/^size_t TextBox::CaretIndexFromLineX/,/^}/' ...` → `grep -c ...` | **5**（`:895-896/899/905/910`）→ **0** |
| **C40-5** | 表构建**只**在 `BuildByteOffsets` 一处（`DecodeCodepointAt` 单源） | `grep -c "DecodeCodepointAt(" ECDI/src/Widget/TextWidget.cpp`（应恰好 1，位于 `BuildByteOffsets`）+ `grep -n "BuildByteOffsets" ...` | 新增 1 处 |
| **C40-6** | 5 参 `FitText` 声明/定义齐备：**3 个私有头**（`GDITextMeasurer.h` / `FontEngine.h` / `FreeTypeTextMeasurer.h`）+ **3 个 .cpp** + 默认体在 `TextMeasurer.h` | `grep -c "FitText(const Font& font, const std::string& text," ECDI/src/Render/GDITextMeasurer.h ECDI/src/Render/GDITextMeasurer.cpp ECDI/src/Render/FontEngine.h ECDI/src/Render/FontEngine.cpp ECDI/src/Render/FreeTypeTextMeasurer.h ECDI/src/Render/FreeTypeTextMeasurer.cpp` | 每文件 **2**（4 参 + 5 参）|
| **C40-7** | 注释无退役标识符；用例数 546 | `grep -n "m_lineStarts\|m_needsLineRecalc" --include=*.cpp --include=*.h ECDI/` == 空；用例数取运行器 `Tests: N` 字段 | 空 / **538 → 546** |
| **C40-8** | 基类表**单一写者**：`m_cpByteOffsets` 的赋值只出现在 `EnsureCpByteOffsets` 内 | `grep -n "m_cpByteOffsets\s*=" ECDI/src/Widget/*.cpp`（应恰好 1 处：`BuildByteOffsets(text, m_cpByteOffsets)` 为传引用、不算赋值；若出现 `= std::move(...)` 等第 2 处 ⇒ 违反 D40-L） | 0 → 应**恰好 1** 处 |

★ **C40-1..4 的 `awk` 锚点已在 HEAD 实跑确认**：四函数签名分别命中 `TextWidget.cpp:202` / `:376`、`TextBox.cpp:1384` / `:890`。★ C40-7 用例数**不用 grep 数**（`.Add("` 全仓口径差 10，同 P39 C39-6 教训），取运行器汇总行。

---

## §6 影响面

| 项 | 预估 |
|---|---|
| 源码（框架） | `ECDI/include/ECDI/Widget/TextWidget.h`（+`ByteMapKey`/+`EnsureCpByteOffsets`/+`CurrentByteMapKey`/+`ByteOffsetAt`/+`m_cpByteOffsets`）· `ECDI/include/ECDI/Render/TextMeasurer.h`（+5 参 `FitText` 及默认体）· `ECDI/src/Widget/TextWidget.cpp`（+`BuildByteOffsets`、三件表设施实现、BuildTextLayout **局部表**、DrawTextContent Ensure+查表）· `ECDI/src/Widget/TextBox.cpp`（+`CurrentByteMapKey` override、OnPaint/CaretIndexFromLineX 查表）· `ECDI/src/Render/GDITextMeasurer.{h,cpp}` / `FontEngine.{h,cpp}` / `FreeTypeTextMeasurer.{h,cpp}`（各 +5 参声明/定义；GDI 倍增窗口、FT 复用 `cps`） |
| 源码（测试） | `ECDI/src/Tests/WidgetTests.cpp`（`WrapMeasurer` +5 参 override、静态接缝）· `ECDI/src/Tests/TextBoxTests.cpp`（`CountingFakeMeasurer` / `RecordingFakeMeasurer` 各 +5 参 override、+8 用例与常量表）|
| ★ **既有用例被改（v1.1 如实记）** | **不是「只增不改」**：三个量具各加一条 5 参 override（转发到自身 4 参，计数/`fed` 原样保留）——**断言文本一条不改**，但 `WidgetTests.cpp` / `TextBoxTests.cpp` 确实被编辑；漏掉这一步会红 ~15 条、假绿 ~14 条（M1） |
| 公共头 | **100 → 100**（无新增头文件；`TextWidget.h` / `TextMeasurer.h` 是既有公共头，改内容不改数量）；`GDITextMeasurer.h` / `FontEngine.h` / `FreeTypeTextMeasurer.h` 均在 `src/Render/` = **src 私有头**，不计入 |
| public API | **+1**（5 参 `FitText` 重载；4 参签名 + 默认体不动；无新公共类型——D40-K） |
| protected 面 | **+4**（`ByteMapKey` 结构 / `CurrentByteMapKey` virtual / `EnsureCpByteOffsets` / `ByteOffsetAt`）+ `TextBox` override 1 处 |
| CMake | **0**（无新增源文件；`Tests/*` 已在册） |
| 用例 | **538 → 546（+8）**：T40-1 表项等价 1 · T40-2 FitText 等价 1 · T40-3 行表 1~2 · T40-4 命中 1 · T40-5 overlong 1 · T40-6 paint 等价 1 · T40-7 GDI（可无头才计）1 · **T40-8 串身份 1** |
| docs | **202 → 203**（本稿）；详设 +1 → **204**（基线：`git ls-files docs/*.md` = 201 tracked + Phase 40 需求稿 1 = 202 已落盘） |
| 机检 | **+8**（C40-1..8） |
| 编译批次 | **一批**：R1+R2+R3+R4 + 两个公共头同批重编（§1.8） |

---

## §7 风险 R40-1..12

| # | 风险 | 处置 |
|---|---|---|
| **R40-1** | ★ **索引语义选错**：表按 `DecodeUtf8` 建 ⇒ 脏字节上索引错位、行为漂移 | D40-J 钉死「SequenceLength 索引」；T40-1 在 `\x80\x8A`（DecodeUtf8 得 0 码点、SequenceLength 得 2）上断言表项 == `CodepointIndexToByteOffset` ⇒ 选错必红 |
| **R40-2** | `byte` 越过 `size` 后越界（`A\xC0` 末段） | `BuildByteOffsets` 与 `CodepointIndexToByteOffset` 同形（都不钳制、都只在 `byte < size` 时读 `data+byte`）；详设逐行 diff 复核 + T40-1 覆盖截断样本 |
| **R40-3** | 「用 `DecodeUtf8` 索引也能过合法样本」= **假绿** | 负对照：临时把 `BuildByteOffsets` 的步长改成 `DecodeUtf8` 语义（跳过非法字节），T40-1 的 `\x80\x8A` / `A\xC0` 样本**必须红**；红了才算判据有牙（同 Phase 39 R39-3） |
| **R40-4** | GDI 5 参无法无头测（需真 DC）⇒ D40-D 窗口化等价性缺护栏 | 详设给出 `TestableGDI`/mock DC 可行性；不可行则**手工探针**（同位置 GDI 对照）+ A40-4 覆盖 `FakeTextMeasurer` 的表路径；R40-4 记入详设必答 |
| **R40-5** | 倍增窗口的「fit==W 但 W<剩余」误用 extent（窗口 extent ≠ 整串 extent） | D40-D 伪码明确：只有 `W == 剩余` 且全放得下才用 `extent`；否则继续倍增。详设给逐行伪码 + R40-4 探针 |
| **R40-6** | 表在 `BuildTextLayout`（const）里写 mutable ⇒ 与 `GetTextLayout` 的 `m_wrapLayout` 缓存交互出半状态 | 表与行**两套独立键**（D40-C）；`EnsureCpByteOffsets` 幂等；T40-3 行表 + T40-1 表项两条断言各管一边 |
| **R40-7** | `CaretIndexFromLineX` 的 `:899` 简化（`lineEndCp-lineStartCp`）改错 | 该值 = `lineText` 码点数 = `LineRange` 码点区间长（`LineRange` 定义 `ownedEnd==end`）；T40-4 命中逐位守 |
| **R40-8** | 两个公共头分两轮编（35 min × 2） | D40-I：同批 |
| **R40-9** | 自定义 measurer 被 5 参改动波及（★ v1.1 订正：**分两类**） | (a) **无计数职责**（`FakeTextMeasurer`、外部自定义）：不实现 5 参即可，走默认体、行为等价（D40-G）；(b) ★ **三个带计数/`fed` 的量具必须各加 5 参 override** 转发自身 4 参——`WrapMeasurer`（`WidgetTests.cpp:647`）/ `CountingFakeMeasurer`（`TextBoxTests.cpp:374`）/ `RecordingFakeMeasurer`（`:1978`）。**不实现不是「无需」，是「计数缝全哑」**：红 ~15 条、假绿 ~14 条（M1／PD40-6） |
| **R40-10** | 用例预算 546 超 | 允许把 T40-3 并入 T40-2（同类断言），**不得减判据** |
| **R40-11** | ★ v1.1 新增：`BuildTextLayout` 若把表写进基类 ⇒ 掩码串（`Password`）× 明文表错配、`substr` 抛 `out_of_range`。**今日不可达**（`TextBox::GetPreferredSize:201-202` 提前返回 `MultilinePreferred`；`DrawTextContent` 调用方只有 Label/Button/CheckBox/Radio），属**潜伏坑** | D40-L：单写者（`BuildTextLayout` 用局部表）+ 命中自检（长度不等 ⇒ debug 中止 / release 重建）+ T40-8 回归护栏 |
| **R40-12** | ★ v1.1 新增：T1（表项 == `CodepointIndexToByteOffset`）依赖**跨 TU 隐式耦合**——`DecodeCodepointAt` 的返回步进恰等于 `SequenceLength(lead)`；若有人把它的截断步进钳到 `size`，T1 静默失效且**不会编译报错** | ① T40-1 的 `"A\xC0"` 样本**标为必测**并注明理由；② `BuildByteOffsets` 处写明「步长只取 `DecodeCodepointAt` 的返回值，不得按 `lead` 自行重算」；③ 详设把该耦合写进注释（跨 TU 契约） |

---

## §8 修订记录与裁定

- **v1.0（2026-10-29）** 初设落盘。八问逐题收口（§1）；决策 D40-A..K（§2，含 **D40-C 键收窄**、**D40-J 索引选型**、**D40-K 签名形态**）；六个落点 PD40-1..6（§3）；七条用例 T40-1..7（§4，538 → 545）；七条机检 C40-1..7（§5，基线 2/2/6/5 已实采）；影响面（§6）；十条风险（§7）。
- **v1.1（2026-10-29，收 Qoder 评审）** 4 must-fix + 7 should-fix + nits 全部落实：
  - **M1（改动最大）**：5 参是并列虚 ⇒ 三个只覆写 4 参的量具（`WidgetTests.cpp:647` / `TextBoxTests.cpp:374` / `:1978`）**一次都不进**，红 ~15 条、假绿 ~14 条。订正 §1.3 + PD40-6（各加 5 参 override 转发自身 4 参）+ R40-9 措辞 + §6 新增「既有用例被改」行。
  - **M2**：`BuildTextLayout` 双调用方喂不同串（`TextBox.cpp:466` 掩码 vs `TextWidget.cpp:342` 明文）⇒ 基类表**不可由它发布**。新增 **D40-L**（局部表 + 单写者 + 命中串身份自检 + `ByteOffsetAt` 访问器）+ R40-11 + T40-8。★ **可达性如实订正**：评审给的 `TextBox.cpp:205 → TextWidget.cpp:442-444` 路径在今日代码上**不成立**（`:201-202` 先返回 `MultilinePreferred()`），故列为**潜伏坑**而非现行缺陷——修复理由是不变量，不是救火。
  - **M3**：`FontEngine` 5 参 `totalCp` 必须取 `byteOffsets.size()-1`（不是 `cps.size()`），否则脏字节上 `:510` 守卫与 `:523` 无 face 兜底漂移；并注明 `for (i = startCp; …)` 的混空间既有分歧原样保留。
  - **M4**：`DrawTextContent` 自己 `EnsureCpByteOffsets(m_text)`（`GetTextLayout:332-339` 键命中时不进 `BuildTextLayout`），原稿「上游已建」不成立。
  - **S1..S7**：5 参默认体空表早退 + `(void)cps`；R4 de-scope 立 **D40-M**；三个私有头补 5 参声明（C40-6 覆盖 6 文件）；T40-1 走 `BuildByteOffsets` 静态接缝；PD40-3 由「无条件重建」改为「局部表 + Ensure」；D40-D 单位推理写进正文（wchar vs 码点）；表设施定义落 `TextWidget.cpp` + `ByteOffsetAt` 进 protected 账目（+3 → **+4**）。
  - **nits**：`sizeof`→`.size()`、`FRAMEWORK_ASSERT` 精确写法（`ECDIAssert.h:24/41`）、`kFitGolden` 出处（`TextBoxTests.cpp:147`，`:133` 注释自证属 Phase 39 批 0——**评审此条不成立**）、538 标注为「继承 P39 实测、本稿未重跑」、`uint32_t` 截断断言**当版定案**（入口一条断言，不做钳制）。
- ★ **本稿为需求 v1.1 的三处实义订正**（D40-C 键收窄 5 维、D40-J 索引选 SequenceLength、D40-M R4 de-scope）——需求稿**保持 v1.1 不回改**（评审基线），详设时统一回写 **需求 v1.2**（沿 D39-G 先例）。

### 裁定结果（v1.1）

| # | 事项 | 裁定 | 来源 |
|---|---|---|---|
| 1 | **D40-C 键收窄** | **按 5 维**，且**前提 = D40-L 单写者先落地**（否则「两个不同串共享同键」使 5 维不健全） | 评审意见（用户背书） |
| 2 | **D40-K 签名形态** | **两参数**（`cps` + `byteOffsets`），不引入 `DecodedText` 公共类型；守 public API +1 | 评审意见（用户背书） |
| 3 | **D40-J 两趟 vs 一趟** | **两趟**；并补护栏：T1 依赖跨 TU 隐式耦合 ⇒ `"A\xC0"` 样本**必测**（R40-12） | 评审意见（用户背书） |

### 仍待你确认（一项）

- **D40-M（R4 de-scope）**：需求稿 R4 把 `Utf8Decode.h:20` 的 3× 过预留列为本 Phase 的「顺手项」。本稿判断：该 reserve 是**上界估计**、收紧需多一趟 O(N)；其危害全在「L 次重复」，而 D40-E 删掉 FT 的 L 次 `DecodeUtf8` 后剩余调用点**无放大** ⇒ 建议 **de-scope**（不动共享核心头）。**这是 scope 变更，须你显式授权**。若不授权，我按原需求补一行 `reserve` 调整 + 一条分配探针判据。

★ 你确认 D40-M 后我进详设（PD40-1..6 逐文件逐行 diff + 批 0 实采 + 探针源码附录），详设再回写需求 v1.2。
