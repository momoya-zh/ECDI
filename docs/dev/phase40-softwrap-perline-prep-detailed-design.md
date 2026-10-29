# Phase 40 · 详细设计 —— 折行支每行「整串预备」O(L·N) 的消除（deferred #83）

> 状态：**v1.4**（2026-10-29 **全链收口**：四构建链 **546 / 546 全绿**；R40-3 负对照 **544 / 2** 后还原全绿；#83 已迁入 roadmap 已完成区）
> **上游**：需求 `phase40-softwrap-perline-prep-requirements.md` **v1.1** · 初设 `phase40-softwrap-perline-prep-preliminary-design.md` **v1.1**（本稿 §9 逐条回写需求 **v1.2**）
> **代码基线**：HEAD `a377026`（`docs(phase39): batch 2 -- requirements v1.2, roadmap v2.61 (#78 done)`）—— 实测 `git diff a377026 HEAD -- ECDI/` **为空**，全部行号沿用需求 / 初设且无漂移
> ★ **事实纪律**：本稿每条行号 = 2026-10-29 `grep -n` / `sed` / `awk` 亲核 HEAD `a377026`；§5 机检命令**全部实跑过**，读数写进正文（不写「应该为 N」）
> ★ **数字纪律**：只承诺①复杂度 ②分配次数 ③改前/改后记录制耗时；**不写「提升 X 倍」**

---

## 红线（三条，两个批次都不得破）

| # | 红线 | 判据 |
|---|---|---|
| **①** | **行表逐位不变** | 既有 538 条全绿 + T40-3（`m_lines` / `m_wrapLayout.lines` 逐位）；含 overlong `U+000A` 三档同源（T40-5） |
| **②** | **`FitText` 输出逐位不变** | 4 参 vs 5 参在 A40-4 全样本上 `TextFit{fitCp,width}` 逐位相等（T40-2）；GDI 倍增窗口在 `W == remaining` 时**逐字复刻** 4 参分支（§1.6） |
| **③** | **4 参签名 + 默认体一字不动** | `TextMeasurer.h:68-119` 零 diff（`git diff` 可验）；自定义 measurer 零改动契约（`TextMeasurer.h:62-67`）不被破 |

★ 红线③是**结构红线**：它把「自定义 measurer 兼容」从「测出来的」变成「改不动的」。

---

## §0 锚点复核（今日亲核；实现期以本表为准）

| 文件 | 锚点 | 内容 | 本稿用法 |
|---|---|---|---|
| `include/ECDI/Widget/TextWidget.h` | `:10-11` | `#include <string>` / `<vector>` | 其后插 `#include <cstdint>`（§2 #1） |
| 同上 | `:142` | `TextRevision()` protected 访问器 | 表设施插在 `:142` 后、`:144` `private:` 前（§2 #2） |
| 同上 | `:144` | `private:` 区起点（Phase 38 起） | 表成员/mutable 必须在其**前**（protected） |
| 同上 | `:166` | `std::size_t m_textRevision = 0;` | 键维 1 的来源（`CurrentByteMapKey` 读它） |
| `src/Widget/TextWidget.cpp` | `:196` | `FindLastSoftBreak` 的**函数闭合** `}`（★ v1.1 订正：`:194` 是 `for` 的闭合，`:195` 是 `return hardEnd;`，`:196` 才是函数尾） | `BuildByteOffsets` 插在 `:196` 后、`:198` `}   // namespace` 前（§2 #4） |
| 同上 | `:202-203` | `BuildTextLayout` 签名 | 三设施实现插在 `:200` 注释块之后、`:202` 之前（§2 #5） |
| 同上 | `:211` | `const std::vector<char32_t> cps = DecodeUtf8(text);` | **不动**；★ 局部表插在 **`:216` 后**（`cps.empty()` 早退块闭合之后，★ v1.1 订正：v1.0 此处误写 `:211` 后，与 §1.2c 冲突） |
| 同上 | `:221-234` | `emitLine` lambda（`[&]` 捕获，`:229-230` 换算） | `:229-230` 改局部表下标（§2 #23） |
| 同上 | `:270` | `measurer.FitText(font, text, scan, limit);` | 4 参 → 5 参（§2 #24） |
| 同上 | `:332-340` | `GetTextLayout` 键命中 `return m_wrapLayout;` | **不动**；这是 M4「`DrawTextContent` 必须自己 Ensure」的依据 |
| 同上 | `:405-406` | `b0` / `b1` 两处换算 | Ensure + `ByteOffsetAt`（§2 #25） |
| `include/ECDI/Render/TextMeasurer.h` | `:7-10` | `<algorithm>` / `<cstddef>` / `<limits>` / `<string>` | 其后插 `<cstdint>` / `<vector>`（§2 #6） |
| 同上 | `:68-119` | 4 参 `FitText` + 默认体（含 P39-R2 的 `byteStart` 外提） | **一字不动**（红线③） |
| 同上 | `:119` | 4 参默认体闭合 `}` | 5 参 + 默认体插在其后、`:121` 注释前（§2 #7） |
| `include/ECDI/Widget/TextBox.h` | `:200` | `protected:` | — |
| 同上 | `:211` | `void OnPaint(...) override;` | override 声明插在 `:211` 后、`:213` 注释前（§2 #8） |
| `src/Widget/TextBox.cpp` | `:236-259` | `BuildDisplayText()`（echo 掩码，组合区间保留原文） | **不动** |
| 同上 | `:444-505` | `RecalculateLines()`（折行支 `:466` 走 `BuildTextLayout`） | **不动**（表不走这里，D40-L 单写者） |
| 同上 | `:509-530` | `LineKeyMatches`（12 项） | **不动**（表键独立 5 维，D40-C） |
| 同上 | `:532-548` | `LineKeySave` | override 定义插在 `:548` 后、`:550` `IsWrapActive` 前（§2 #9） |
| 同上 | `:890-919` | `CaretIndexFromLineX`（5 处换算） | 批 2 全改（§2 #26/#27） |
| 同上 | `:1449-1452` | `textSource` = `m_text` / `displayStorage` | **不动**（Ensure 吃它） |
| 同上 | `:1454` | `RecalculateLines();`（OnPaint 内） | Ensure 插在 `:1454` 后（§2 #28） |
| 同上 | `:1489-1490` | `startByte` / `drawEndByte` | 批 2 改（§2 #29） |
| 同上 | `:1502-1503` | `hlStartByte` / `hlEndByte` | 批 2 改（§2 #29） |
| 同上 | `:1526-1527` | `ulStartByte` / `ulEndByte` | 批 2 改（§2 #29） |
| `src/Render/GDITextMeasurer.h` | `:44-45` | 4 参 `FitText` 声明 | 5 参声明插在 `:45` 后（§2 #10） |
| `src/Render/GDITextMeasurer.cpp` | `:148-149` | 4 参定义签名 | 5 参定义插在 4 参函数体闭合之后（§2 #11） |
| 同上 | `:153` | `totalCp = ByteOffsetToCodepointIndex(text, text.size())` | 5 参改 `byteOffsets.size()-1` |
| 同上 | `:170-171` | `byteStart` + `UTF8ToWide(text.substr(byteStart))`（整尾） | 5 参：表项 + **窗口**（§1.6） |
| 同上 | `:203-204` | `GetTextExtentExPointW(... &fitW, nullptr, &extent)` | 5 参复用（同参数） |
| 同上 | `:231-246` | `extent` / `GetTextExtentPoint32W(fit)` 两分支 | 5 参在 `W == remaining` 时**逐字复刻** |
| `src/Render/FontEngine.h` | `:141-142` | 4 参 `FitText` 声明（非虚，`FontEngine` 不派生 `TextMeasurer`） | 5 参声明插在 `:142` 后（§2 #12） |
| `src/Render/FontEngine.cpp` | `:509` | `totalCp = ByteOffsetToCodepointIndex(...)` | 5 参取 `byteOffsets.size()-1`（M3） |
| 同上 | `:529` | `const std::vector<char32_t> codepoints = DecodeUtf8(text);` | 5 参**删除**（改用入参 `cps`） |
| 同上 | `:534` | `for (std::size_t i = startCp; i < codepoints.size(); ++i)` | 5 参改 `cps.size()`（混空间分歧原样保留） |
| `src/Render/FreeTypeTextMeasurer.h` | `:30-31` | 4 参 `FitText` 声明（`override`） | 5 参声明插在 `:31` 后（§2 #14） |
| `src/Render/FreeTypeTextMeasurer.cpp` | `:46-49` | 无 engine 兜底（`ByteOffsetToCodepointIndex`） | 5 参改 `byteOffsets.size()-1` |
| 同上 | `:51` | `return m_engine->FitText(font, text, startCp, maxWidth);` | 5 参转 5 参 |
| `src/Tests/WidgetTests.cpp` | `:426-441` | `FakeTextMeasurer`（**不覆写** `FitText`，走默认体） | **零改动**（红线③的活证据） |
| 同上 | `:444-457` | `TestableLabel`（`using` 三件 + `ResolveMeasurer`） | `ByteMapProbe` 的基类（§3.2） |
| 同上 | `:647-651` | `WrapMeasurer::FitText`（4 参，转发默认体 + 计 `m_fitCalls`） | **+5 参 override**（M1，§3.1） |
| 同上 | `:1730` / `:1782` | `RegisterWidgetTests()` 定义 / 文件末 `}` | T40-1 / T40-8 登记插在 `:1782` 前 |
| `src/Tests/TextBoxTests.cpp` | `:147-156` | `kFitGolden`（8 组，含 `startCp≠0` / overlong / 越界 / 空串） | **A40-4 基线**；T40-2 直接复用 |
| 同上 | `:281` | `kWrapSamples[]`（折行样本） | T40-3 / T40-6 可复用 |
| 同上 | `:374-382` | `CountingFakeMeasurer::FitText`（4 参，**自实现**按宽除 8） | **+5 参 override**（M1，§3.1） |
| 同上 | `:1978-1987` | `RecordingFakeMeasurer::FitText`（4 参，**自实现** + 记 `fed`） | **+5 参 override**（M1，§3.1） |
| 同上 | `:3299` / `:3365` | `RegisterTextBoxTests()` 定义 / 文件末 | T40-2..T40-7 登记插在末 `}` 前 |

### 0.1 与初设的差异（**8** 条 = D-1..D-8）＋ 本稿 v1.0 勘误（**3** 条 = D-9..D-11，v1.1 新增）

| # | 初设 | 本稿 | 理由 |
|---|---|---|---|
| **D-1** | §6 影响面列出 8 个改动文件，**未含 `TextBox.h`** | **加 `TextBox.h`**（§2 #8）⇒ 改动文件 **8 → 9** | `CurrentByteMapKey` 是 `TextWidget` 的 protected **virtual**，`TextBox` override 必须在**自己的头**声明（`:211` 后）。初设把 override 只写在 PD40-4 的 .cpp 里，那会编译不过 |
| **D-2** | PD40-6 要求给 T40-1 开「**静态接缝**」（`static std::vector<uint32_t> Build(const std::string&)`），并附「复刻体」退路 | **整段作废**——不需要任何新接缝 | ① `EnsureCpByteOffsets` 本身就是 protected，`ByteMapProbe` 用 `using` 即可；② **`SetText(s)` 会 `MarkTextChanged()` ⇒ revision++ ⇒ 键必失配 ⇒ Ensure 必重建**，天然解决「第二个样本键命中拿到旧表」；③ `m_cpByteOffsets` 是 protected **成员变量** ⇒ 派生类成员函数可直接读 `.size()`（不需要 `TableSizeForTest`）。⇒ 判据从「测复刻体」回到「测产品码」，且 protected 面 **+4 → +4 不变**（无新增） |
| **D-3** | T40-8 断言 «(b) 明文串后紧跟掩码串（**键相同**）⇒ 第二次必须重建» | **改测「键变必重建」**（合法路径） | 「同键不同串」在 `_DEBUG` 下会被 `FRAMEWORK_ASSERT` **弹窗 + `assert(false)` 中止进程**（`ECDIAssert.h:24-35`）⇒ 该用例在 debug 构建下**跑不到断言就先把测试进程杀掉**，546 全绿当场不成立。★ 自检分支**不写用例**，理由是它只在「违反内部不变量」时触发——**写用例去触发它 = 主动制造非法状态**；由 C40-8（单写者机检）+ 代码审查保护。这是诚实的取舍，不是漏测 |
| **D-4** | D40-D 的耗尽判据写作「`fit < W`」 | 钉死为原始 **`fitW < static_cast<INT>(wide.size())`**（§1.6） | `fit` 是**代理修正后**的值：末位是高代理时 `fit = fitW − 1`，全放下的窗口会被误判成「未耗尽」⇒ 多倍增几轮（结果仍对、白烧调用）。更关键：**`W == remaining` 时必须退化为 4 参的 `fit >= wide.size()` 判据**，否则孤立高代理尾部的宽度口径会漂（`extent` vs `GetTextExtentPoint32W(fit)`） |
| **D-5** | §1.8 要求「批 0（改任何源码前跑，独立一笔提交）」采 A40-4 golden | **批 0 与批 1 合并**，改为「**批 1 = 基础设施（零行为变化）** / **批 2 = 切换消费**」 | A40-4 基线**无需新采**：`kFitGolden`（`TextBoxTests.cpp:147`）已是 HEAD 行为的快照（P39 批 0 采、P39 收口后行为未变）；A40-5 基线 **2/2/6/5** 本稿已实采（§5）。⇒ 无 golden 可采 ⇒ 独立批 0 只剩「跑一次 538」，并入批 1 的前置动作。★ 但**保留批 0 的实质纪律**：批 1 提交前必须先跑一次 538 确认基线绿，否则不开始 |
| **D-6** | 需求 §3 R1 写「`totalCp` 取 `cps.size()`」 | **`byteOffsets.size() - 1`**（初设 M3 已定，本稿落实并回写需求 v1.2 §9-1） | `cps.size()` 是 `DecodeUtf8` 空间（跳过非法首字节、截断即停），`totalCp` 在现状是 `SequenceLength` 空间；脏字节上不等 ⇒ 会漂 `:510` 守卫与 `:523` 无 face 兜底 |
| **D-7** | §4 T40-7 写「若 GDI 需真 DC 无法无头，则降级为手工探针」 | **可无头，无需降级** | `GDITextMeasurer::FitText` 用 `GetDC(nullptr)`（屏幕 DC，不依赖 HWND）+ `GetDpiForWindow(nullptr)` 返回 0 ⇒ dpi 落 96 fail-safe（`:161-165`）。★ 但字体来自系统 ⇒ **只做 4 参 vs 5 参自比**（相对判据），不做绝对 golden |
| **D-8** | §3 PD40-3 只说「补 `<limits>`」 | `TextWidget.cpp` 需插 **3 个** include：`<cstdint>`（`uint32_t`）、`<limits>`（截断断言）、`ECDI/Core/ECDIAssert.h`（`FRAMEWORK_ASSERT`） | 现文件 `:12-14` 只有 `<algorithm>` / `<cmath>` / `<utility>`，三者俱缺（实测） |
| **D-9** ★★ | v1.0 §1.2c 的**越界论证是错的**：「脏字节上 `cps.size() ≤ count`」 | **`ByteOffsetAt` 改为钳制**（钳到 `size()-1`，不再 `assert`）；新增匿名 `OffsetAt` helper 作**单源钳制规则**；`emitLine` 同用 | **探针实证**（`g++ -std=c++20`，本机 2026-10-29）：`"\xE0\x41\x41"` → `cps=2 / count=1 / table=2`、`"\xF0\x41\x41\x41"` → `cps=3 / count=1 / table=2` ⇒ **`cps > count`**。根因：`DecodeUtf8` 遇「前导合法 + 续字节非法」只 `++i` 一字节（`Utf8Decode.h:52-55`），而 `SequenceLength` 一次跳过声称的 3–4 字节 ⇒ cps 可多于 count。可达路径：`emitLine` 的回退分支（`end != fitEnd`）用 `cps` 空间的 `end` 查表 ⇒ **3 字节输入即越界**。★ 钳制是**精确等价**（不是保守）：任意 `k ≥ count` 时 `CodepointIndexToByteOffset(text,k)` 恒 == `offsets[count]`（两循环步进式相同、退出条件同为 `byte >= size`）——探针 8/8 样本验证 |
| **D-10** ★★ | v1.0 §1.2b 的串身份自检 `back() == text.size()` **会假阳** | 新增 `mutable std::size_t m_cpByteTextSize`，建表时记 `text.size()`，命中比它 | **与 §1.1 自相矛盾**：同一文档已注明「末段截断时末项**可越过** size」（`"A\xC0"` → 表 `[0,1,3]`、`back()=3 > size()=2`，探针确认）⇒ 对合法截断串，「键命中」的第二次 `Ensure` 会被判成「同键不同串」：debug 中止 / release 每帧重建（把 O(1) 命中退化成 O(N)，**静默废掉本 Phase 全部收益**）。★ 这比 v1.0 D-3 删掉的 T40-8 子断言**更容易踩**——那是主动制造非法状态，这是**合法输入路径** |
| **D-11** | §0 / §2 #4 的插入锚点写成 `:194` | **`:196`** | 实测 `:194` = `for` 的闭合 `}`、`:195` = `return hardEnd;`、`:196` = 函数闭合 ⇒ 按 v1.0 插会插进 `FindLastSoftBreak` **体内**，编译不过。48 处锚点里唯一一处错 |

---

## §1 定案的接口面（逐字签名）

### 1.1 `include/ECDI/Widget/TextWidget.h`（protected 面 **+4**；public API **+0**）

`:142`（`TextRevision()`）之后、`:144`（`private:`）之前插入：

```cpp
	// ── Phase 40：码点→字节偏移表（★ D40-B/D40-C/D40-L：基类持有、单写者、消费者懒建）──

	/// @brief 表身份 = **决定「串的字节」的 5 维**（与 `TextBox::LineKeyMatches` 的 12 维**不同**——后者管宽度）
	/// @details `byteOffsets[k]` 是纯 UTF-8 结构量：只取决于字符串的字节，与 font / dpi / wrapWidth /
	///          hasMeasurer / wrapRequested **全无关**（这些只影响宽度）。⇒ 5 维即充分（D40-C）。
	///          ★ 充分性的**前提**是「单写者」（D40-L）：表只能由 `EnsureCpByteOffsets` 写，
	///          否则两个不同的串可共享同一个键。
	struct ByteMapKey{
		std::size_t revision  = 0;   ///< 维 1：`TextRevision()`（`m_text` 每次变更递增）
		int         echo      = 0;   ///< 维 2：`EchoMode`（`TextWidget` 恒 0 = Normal；TextBox 才有 Password）
		bool        composing = false;   ///< 维 3：组合态（组合区间显示原文，字节与掩码不同）
		std::size_t compStart = 0;   ///< 维 4
		std::size_t compLen   = 0;   ///< 维 5
		bool operator==(const ByteMapKey&) const = default;   // C++20
	};

	/// @brief 当前串身份（protected virtual——`TextBox` override 补齐 echo / composing 三维）
	[[nodiscard]] virtual ByteMapKey CurrentByteMapKey() const;

	/// @brief 确保偏移表匹配 `text`（**全仓唯一写者**——D40-L）
	/// @details 键命中**且串长自洽** ⇒ 直接返回（0 分配 0 扫描）；否则重建。
	///          ★ 键命中但 `m_cpByteTextSize != text.size()` ⇒ **同键不同串**（内部不变量被破）：
	///          debug 下 `FRAMEWORK_ASSERT` 弹窗中止；**release 下不复用、直接重建**（正确性优先）。
	///          ★★ **绝不能用 `m_cpByteOffsets.back()` 比串长**：末段截断时末项会越过 `text.size()`
	///          （`"A\xC0"` → 末项 3 > 2）⇒ 对**合法输入**假阳（D-10）。
	void EnsureCpByteOffsets(const std::string& text) const;

	/// @brief 读表项（**唯一读法**——禁止裸下标散在消费站点）
	/// @param cpIndex 码点索引；★ **超出末项时钳制到末项**（**不** assert）——见 §1.2b 的等价性证明
	[[nodiscard]] std::size_t ByteOffsetAt(std::size_t cpIndex) const;

	/// @brief 码点→字节偏移表：`m_cpByteOffsets[k]` = 第 k 个码点的首字节偏移
	/// @details `size() == count + 1`（`count` 是 **`SequenceLength` 空间**的码点数，**不是** `cps.size()`）；
	///          末项 = 最后一次步进后的字节位——★ **末段截断时可越过 `text.size()`**
	///          （如 `"A\xC0"` → `[0,1,3]`，末项 3 > size 2；与 `CodepointIndexToByteOffset` 同形）
	mutable std::vector<std::uint32_t> m_cpByteOffsets;
	mutable bool        m_cpByteValid    = false;   ///< 表是否已建立
	mutable std::size_t m_cpByteTextSize = 0;       ///< ★ 建表时那条串的 `size()`（串身份自检用——
	                                                ///<   **绝不能用 `back()`**：末段截断时 `back() > size()`，D-10）
	mutable ByteMapKey  m_cpByteKey{};              ///< 建立表时的键
```

★ 三个实现（`CurrentByteMapKey` / `EnsureCpByteOffsets` / `ByteOffsetAt`）**全部落 `TextWidget.cpp`**——它们要用匿名 namespace 的 `BuildByteOffsets`，且 `FRAMEWORK_ASSERT` 不必把 `ECDIAssert.h` 拖进公共头。

### 1.2 `src/Widget/TextWidget.cpp`（`BuildByteOffsets` + 三设施 + 消费侧）

**(a) 匿名 namespace 加 `BuildByteOffsets` + `OffsetAt`**（插在 **`:196`** 后、`:198` `}   // namespace` 前；★ v1.1 订正：v1.0 误写 `:194`——那是 `for` 的闭合，插进去会落进函数体内）：

```cpp
/// @brief 构建码点→字节偏移表（★ T1：`offsets[k] == CodepointIndexToByteOffset(text, k)`，逐位）
/// @details ★★ **跨 TU 契约（R40-12）**：步进量**只取 `DecodeCodepointAt` 的返回值**，
///          不得按 `lead` 自行重算——它返回的正是 `SequenceLength(lead)`（`Core/UTF8.cpp:65/86`），
///          与 `CodepointIndexToByteOffset` 的推进式**同一函数**。若哪天有人把
///          `DecodeCodepointAt` 的截断步进钳到 `size`，本表的等价性会**静默失效且不会编译报错**
///          ⇒ 唯一护栏是 T40-1 的 `"A\xC0"` 样本（必测）。
/// @note 末段截断时刻意**越过** `text.size()`（`"A\xC0"` 得 `[0,1,3]`）——做钳制反而与
///       `CodepointIndexToByteOffset` 不逐位对应（那是既有行为，见 R40-2）。
void BuildByteOffsets(const std::string& text, std::vector<std::uint32_t>& offsets){
	// 半宽表项：偏移上限 = 串字节数（本框架文本远 < 4 GiB）⇒ 越界只可能来自编程错误
	FRAMEWORK_ASSERT(text.size() <= (std::numeric_limits<std::uint32_t>::max)());
	offsets.clear();
	offsets.reserve(text.size() + 1);   // 上界（CJK 时明显过预留，但只在此处一次）
	std::size_t byte = 0;
	while (byte < text.size()){
		offsets.push_back(static_cast<std::uint32_t>(byte));
		char32_t cp = 0;
		byte += DecodeCodepointAt(text.data() + byte, text.size() - byte, cp);
	}
	offsets.push_back(static_cast<std::uint32_t>(byte));
}

/// @brief 读表项（★ **钳制规则的唯一实现**——基类 `ByteOffsetAt` 与 `emitLine` 都转它，D-9）
/// @details `k` 可**超出末项**：行表 / `cps` 用的是 `DecodeUtf8` 空间的索引，而表只有
///          `count+1` 项，二者在「前导合法 + 续字节非法」的串上会 `cps.size() > count`
///          （探针：`"\xE0\x41\x41"` → `cps=2 / count=1`）⇒ 消费方**必然**会给出越界索引。
///          ★ 钳制**逐位等价**于旧行为（不是保守近似）：任意 `k ≥ count` 时
///          `CodepointIndexToByteOffset(text, k)` 恒等于 `offsets[count]`——两者步进式相同、
///          退出条件同为 `byteOffset >= text.size()`。（探针 8/8 样本验证，含 overlong 三档。）
std::size_t OffsetAt(const std::vector<std::uint32_t>& offsets, std::size_t k) noexcept{
	if (offsets.empty()){
		return 0;   // 未建表（理论上不可达——`BuildByteOffsets` 至少产出 1 项）
	}
	return offsets[(std::min)(k, offsets.size() - 1)];
}
```
★ `reserve(text.size() + 1)` 是**上界**（ASCII 恰好、CJK 3× 富余）——只在**重建**时发生一次，不是每行；`push_back` 不因此二次分配。

**(b) 三设施实现**（插在 `:200` 注释块后、`:202` `BuildTextLayout` 前）：

```cpp
// ── Phase 40：码点→字节偏移表（单写者 + 命中自检，D40-L）────────────────

TextWidget::ByteMapKey TextWidget::CurrentByteMapKey() const{
	return ByteMapKey{ m_textRevision, 0, false, 0, 0 };   // 基类无 echo / 组合 ⇒ 只 revision 是变量
}

void TextWidget::EnsureCpByteOffsets(const std::string& text) const{
	if (m_cpByteValid && m_cpByteKey == CurrentByteMapKey()){
		// ★ D40-L-3 串身份自检：键相同但表不是这个串的 ⇒ 内部不变量被破（同键不同串）
		//   ★★ D-10：比的是**建表时记下的串长**，**不是** `m_cpByteOffsets.back()`——
		//      末段截断时末项会**越过** size（`"A\xC0"` → back()=3 > size=2）⇒ 用 back() 会
		//      对**合法输入**假阳：debug 中止、release 每帧重建（静默废掉全部收益）。
		if (m_cpByteTextSize != text.size()){
			FRAMEWORK_ASSERT(m_cpByteTextSize == text.size());   // debug 中止
			// release 继续 ⇒ **不复用错串表**，落到下面重建
		}
		else{
			return;   // 命中 ⇒ 0 分配 0 扫描（正常路径）
		}
	}
	BuildByteOffsets(text, m_cpByteOffsets);
	m_cpByteTextSize = text.size();   // ★ D-10：连同表一起记
	m_cpByteKey      = CurrentByteMapKey();
	m_cpByteValid    = true;
}

std::size_t TextWidget::ByteOffsetAt(std::size_t cpIndex) const{
	// ★★ D-9：**钳制**而非 assert——消费方给的是 `cps` 空间索引，可 > count（§1.2a 的 OffsetAt 注释）
	return OffsetAt(m_cpByteOffsets, cpIndex);
}
```

**(c) 批 2：`BuildTextLayout` 局部表**（`:211` 后插，`:229-230` / `:270` 改）：

```cpp
	const std::vector<char32_t> cps = DecodeUtf8(text);   // ← :211 既有，不动
	if (cps.empty()){ ... }                               // ← :212-216 既有早退，不动（空串不建表）
	// ── ★ Phase 40（D40-L-1）：插在 :216 的 `}` 之后、:218 注释之前 ──
	//    **局部表**：供下面 `emitLine`（`:221` 的 `[&]` lambda，天然可见）与 5 参 `FitText` 用，
	//    **绝不写基类成员**——`BuildTextLayout` 的两个调用方喂**不同的串**
	//    （`TextBox.cpp:466` 掩码串 vs `TextWidget.cpp:342` 明文串）⇒ 发布到基类会让
	//    「键相同、串不同」静默复用（R40-11）。
	std::vector<std::uint32_t> byteOffsets;
	BuildByteOffsets(text, byteOffsets);
```
★ **为什么插在 `cps.empty()` 早退之后**：空串不需要表（`emitLine` 不会被调用、`FitText` 也不会）；插在后可省一次构造，且不影响任何下标访问。

```
	// :229-230 改（`emitLine` 内，`[&]` 捕获 byteOffsets）
				const std::size_t b0 = OffsetAt(byteOffsets, lineStart);
				const std::size_t b1 = OffsetAt(byteOffsets, end);

	// :270 改
			const TextFit fit = measurer.FitText(font, text, scan, limit, cps, byteOffsets);
```

★★ **索引空间论证（v1.0 此处写错，v1.1 订正 = D-9，必须写清）**：
消费方给的 `end` 来自 **`cps` 空间**（`end ≤ cps.size()`，因为 `emitLine` 只把 `lineEnd` 往下裁尾部空格），
而表只有 **`count + 1`** 项（`count` = `SequenceLength` 空间）。**二者不等，且 `cps.size()` 可以更大**：

| 串 | `cps.size()`（`DecodeUtf8`） | `count`（`SequenceLength`） | 关系 |
|---|---|---|---|
| `"abcdef"` | 6 | 6 | 相等 |
| `"\x80\x8A"`（非法首字节） | **0** | 2 | `cps < count`（安全） |
| `"\xE0\x41\x41"`（合法前导 + 非法续字节） | **2** | **1** | ★ **`cps > count` ⇒ 越界** |
| `"\xF0\x41\x41\x41"` | **3** | **1** | ★ 同上 |

根因：`DecodeUtf8` 对「前导合法 + 续字节非法」只 `++i` **一个字节**（`Utf8Decode.h:52-55`），
而 `SequenceLength` 一次跳过声称的 3–4 字节 ⇒ 同一段字节被两种规则切成不同数量的码点。
**可达路径（3 字节输入即可触发）**：`BuildTextLayout("\xE0\x41\x41")` 的第 2 轮 `scan=1`，
`FitText` 因 `startCp(1) >= totalCp(1)` 返回 `{0,0}` ⇒ `hardEnd=1 < segEnd=2` ⇒
`FindLastSoftBreak` 返回 1 ⇒ `b==lineStart ⇒ b=2` ⇒ `emitLine(1, 2, 1, 0.0f)`：
`end=2 != fitEnd=1` ⇒ **进回退分支** ⇒ 取 `byteOffsets[2]`，而表长只有 2。

⇒ **`OffsetAt` 钳制**（`min(k, size()-1)`）闭合此洞，且**逐位等价**（探针实测）：
`CodepointIndexToByteOffset("\xE0\x41\x41", 2) = 3 = offsets[1]`、`(…, 4) = 3 = offsets[1]`；
`"\xF0\x41\x41\x41"` 同形（`= 4 = offsets[1]`）。两个循环的步进式相同、退出条件同为
`byteOffset >= text.size()` ⇒ 任意 `k ≥ count` 恒落末项。

★ **同一根因打到全部四个消费站点**（故四处**统一走** `ByteOffsetAt` / `OffsetAt`，不得裸下标）：
`emitLine`（`cps` 空间）、`DrawTextContent`（行表来自 `BuildTextLayout` ⇒ `cps` 空间）、
`OnPaint`（`LineDrawRange`/`LineRange` ⇒ `cps` 空间）、`CaretIndexFromLineX`（`LineRange` ⇒ `cps` 空间）。
★ **非折行支安全**：`TextBox.cpp:475-496` 用 `DecodeCodepointAt` 步进 = `count` 空间。
★ **5 参 `FitText` 三个实现不用改**：`startCp >= totalCp` 守卫 + `startCp + mid ≤ totalCp` 已保证在界内。

**(d) 批 2：`DrawTextContent`（`:405-406`）**：

```cpp
		// ★ M4：必须**自己** Ensure——`GetTextLayout` 键命中时直接 `return m_wrapLayout`
		//   （`:332-340`）根本不进 `BuildTextLayout`，不能指望上游建表
		EnsureCpByteOffsets(m_text);
			const std::size_t b0 = ByteOffsetAt(line.startCp);
			const std::size_t b1 = ByteOffsetAt(line.startCp + line.cpCount);
```
★ 调用位置：循环**之前**（`:395` 的 `std::size_t row = 0;` 前）一次，不是每行——否则每行一次键比较（虽 O(1) 但无谓）。

### 1.3 `include/ECDI/Render/TextMeasurer.h`（public API **+1**）

`:119`（4 参默认体闭合 `}`）之后、`:121` 注释前插入：

```cpp
	/// @brief 断行适配（★ Phase 40：**表感知重载**——不再每行 O(N) 换算 / 每行整串解码）
	/// @details 语义与 4 参**完全相同**（`TextFit{fitCp,width}`，码点口径、`fitCp` 相对数），
	///          只是**输入预算好**：`totalCp` / `byteStart` / `byteEnd` 全 O(1)。
	///          ★ 4 参签名与其默认体**一字不动**（D40-G：自定义 measurer 零改动契约）；
	///            本重载是**并列的第二个虚**，谁调谁选。
	///          ★ **两个入参的语义不同，不得混用**（D40-J）：
	///            · `cps`：`DecodeUtf8` 语义（`src/Render/Utf8Decode.h:17`——跳过非法首字节、截断即停），
	///              与调用方的行切分**同源**；FT 用它查 advance。
	///            · `byteOffsets`：`SequenceLength` 语义（`Core/UTF8.cpp:9`——非法前导按 1 字节），
	///              与 `CodepointIndexToByteOffset` **逐位同源**；GDI / 默认体用它切片。
	///            合法文本二者对齐（`cps.size() == byteOffsets.size()-1`）；**脏字节分歧**，
	///            `totalCp` 一律以 `byteOffsets.size()-1` 为准（D40-E/M3）。
	/// @param startCp 起始码点索引（绝对；`>= byteOffsets.size()-1` ⇒ 返回 `{0,0}`）
	/// @param maxWidth 可用宽（DIP，`< 0` 视作 0）
	virtual TextFit FitText(const Font& font, const std::string& text, std::size_t startCp,
	                        float maxWidth, const std::vector<char32_t>& cps,
	                        const std::vector<std::uint32_t>& byteOffsets)
	{
		(void)cps;   // 默认体只走 `MeasureText(切片)`，只需 byteOffsets
		if (byteOffsets.size() < 2)
		{
			return TextFit{ 0, 0.0f };   // 空 / 单元素表 ⇒ 码点数为 0（防 size()-1 下溢成 SIZE_MAX）
		}
		const std::size_t totalCp = byteOffsets.size() - 1;
		if (startCp >= totalCp)
		{
			return TextFit{ 0, 0.0f };
		}
		const std::size_t byteStart = byteOffsets[startCp];
		const std::size_t firstEnd  = byteOffsets[startCp + 1];
		const std::string firstCpText = text.substr(byteStart, firstEnd - byteStart);
		if (MeasureText(font, firstCpText).width > (std::max)(0.0f, maxWidth))
		{
			return TextFit{ 0, 0.0f };
		}
		std::size_t lo = 1;
		std::size_t hi = totalCp - startCp;
		while (lo < hi)
		{
			const std::size_t mid = lo + (hi - lo + 1) / 2;
			const std::size_t byteEnd = byteOffsets[startCp + mid];
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
		const std::size_t byteEnd = byteOffsets[startCp + lo];
		const Size consumed = MeasureText(font, text.substr(byteStart, byteEnd - byteStart));
		return TextFit{ lo, consumed.width };
	}
```
★ 与 4 参默认体（`:68-119`）**逐行同构**，只有 4 处 `CodepointIndexToByteOffset(text, X)` → `byteOffsets[X]`。⇒ 由 T1 得逐位等价（红线②）。

### 1.4 `include/ECDI/Widget/TextBox.h`（protected 面 **+1**）

`:211`（`OnPaint` 声明）之后插入：

```cpp
	/// @brief 表身份 override（★ Phase 40 / D40-C：在基类 revision 之上补 echo + composing 三维）
	/// @details 这三项决定「显示串的字节」（`BuildDisplayText` 的掩码替换与组合区间保留原文），
	///          而 font / dpi / 折行宽**不影响字节** ⇒ 不进键。
	[[nodiscard]] ByteMapKey CurrentByteMapKey() const override;
```

### 1.5 `src/Widget/TextBox.cpp`（override 定义）

`:548`（`LineKeySave` 闭合 `}`）之后、`:550`（`IsWrapActive`）之前插入：

```cpp
// ── Phase 40：表身份（echo + 组合三维，D40-C）────────────────────────────

TextBox::ByteMapKey TextBox::CurrentByteMapKey() const{
	// ★ `m_echoMode` / `m_isComposing` / `m_composition*` 均为 private（`TextBox.h:330/505`）——
	//   本函数是成员函数，可直接读（`ByteMapKey` 是基类 protected 嵌套类型，名字可见）
	return ByteMapKey{ TextRevision(),
	                   static_cast<int>(m_echoMode),
	                   m_isComposing, m_compositionStart, m_compositionLength };
}
```

### 1.6 `src/Render/GDITextMeasurer.h/.cpp`（倍增窗口——本稿最需要逐行说清的一处）

**头**（`:45` 后）：加 5 参声明（`override`），注释指向 `.cpp` 的等价性论证。

**`.cpp`**（4 参函数体闭合之后插）——**`W == remaining` 分支是 4 参 `:231-246` 的逐字副本**，这是等价性的结构性保证：

```cpp
namespace{
// 首窗码点数（★ 只需「不太大也不太碎」：remaining <= 64 时首轮即整尾 ⇒ 与改前零差异）
constexpr std::size_t kFitWindowInitCp = 64;
}

TextFit GDITextMeasurer::FitText(const Font& font, const std::string& text,
                                 std::size_t startCp, float maxWidth,
                                 const std::vector<char32_t>& cps,
                                 const std::vector<std::uint32_t>& byteOffsets)
{
	(void)cps;   // GDI 走 wchar 口径，不需要码点值（签名与默认体 / FT 统一）
	if (byteOffsets.size() < 2)
	{
		return TextFit{ 0, 0.0f };
	}
	const std::size_t totalCp = byteOffsets.size() - 1;   // ★ O(1)（改前 :153 是 O(N) 计数）
	if (startCp >= totalCp)
	{
		return TextFit{ 0, 0.0f };
	}

	int dpi = GetDpiForWindow(m_hwnd);
	if (dpi <= 0)
	{
		dpi = 96;
	}

	const std::size_t byteStart = byteOffsets[startCp];   // ★ O(1)（改前 :170 是 O(startCp)）
	const float limit = (std::max)(0.0f, maxWidth);
	const LONG maxPx = static_cast<LONG>(limit * static_cast<float>(dpi) / 96.0);

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return TextFit{ totalCp - startCp, 0.0f };
	}

	const float toDip = 96.0f / static_cast<float>(dpi);
	const std::size_t remaining = totalCp - startCp;
	TextFit result{ totalCp - startCp, 0.0f };   // 兜底 = 放行全串（同 4 参 :193）
	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		// ★★ 倍增窗口（D40-D）：只拷 `[byteStart, byteOffsets[startCp + W])`，
		//   改前（`:171`）拷**整条尾部** `text.substr(byteStart)` ⇒ 早期行 O(N) memcpy × L 行。
		//   等价性两条：
		//   ① `GetTextExtentExPointW` 的 `fitW` = 「从窗口首累加宽度直到超过 maxPx 的字符数」，
		//      **与窗口之后是否存在字节无关** ⇒ 窗口未耗尽（`fitW < wide.size()`）时，
		//      窗口内的答案与整尾的答案**逐位相同**。
		//   ② `W == remaining` 时窗口就是整尾 ⇒ 下面第一个分支**逐字复刻** 4 参，
		//      连判据（`fit >= wide.size()`）都相同 ⇒ 脏字节（孤立高代理尾）也一致。
		std::size_t W = (std::min)(remaining, kFitWindowInitCp);
		for (;;)
		{
			const std::size_t windowEnd = byteOffsets[startCp + W];
			const std::wstring wide = UTF8ToWide(text.substr(byteStart, windowEnd - byteStart));
			if (wide.empty())
			{
				result = TextFit{ 0, 0.0f };
				break;
			}

			SIZE extent{};
			INT fitW = 0;
			if (!GetTextExtentExPointW(measureDC, wide.c_str(), static_cast<int>(wide.size()),
			                           maxPx, &fitW, nullptr, &extent))
			{
				break;   // 失败 ⇒ 保持兜底（放行全串），同 4 参
			}

			// ── wchar fit → 码点数（与 4 参 :209-229 **逐字同源**）──
			INT fit = fitW;
			if (fit > 0)
			{
				const wchar_t last = wide[static_cast<std::size_t>(fit) - 1];
				if (last >= 0xD800 && last <= 0xDBFF)
				{
					--fit;   // 停在代理对中间 ⇒ 退一格（C29-2）
				}
			}
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

			if (W == remaining)
			{
				// ★★ 整尾 ⇒ **逐字复刻** 4 参 `:231-246`（不得"优化"这两分支的顺序或判据）
				float width = 0.0f;
				if (fit >= static_cast<INT>(wide.size()))
				{
					width = static_cast<float>(extent.cx) * toDip;
				}
				else
				{
					SIZE prefixExtent{};
					if (GetTextExtentPoint32W(measureDC, wide.c_str(), fit, &prefixExtent))
					{
						width = static_cast<float>(prefixExtent.cx) * toDip;
					}
				}
				result = TextFit{ cpFit, width };
				break;
			}

			// W < remaining：只有「窗口内确实有放不下的字符」才能提前定答案
			// ★ 判据用**原始 fitW**（不是代理修正后的 fit）——后者在末位高代理时会小 1，
			//   让"整窗放下"误判成"未耗尽"（结果仍对，但白倍增几轮）
			if (fitW < static_cast<INT>(wide.size()))
			{
				float width = 0.0f;
				SIZE prefixExtent{};
				if (GetTextExtentPoint32W(measureDC, wide.c_str(), fit, &prefixExtent))
				{
					width = static_cast<float>(prefixExtent.cx) * toDip;
				}
				result = TextFit{ cpFit, width };
				break;
			}
			W = (std::min)(remaining, W * 2);   // 窗口全放下但仍非整尾 ⇒ 倍增（不改任何状态）
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return result;
}
```

★ **为什么 `W == remaining` 要单独一支**：若统一用「`fitW < wide.size()` ⇒ 出答案」，则 `W == remaining` 且「全串放下但末位孤立高代理」时会走 `prefix` 分支，而 4 参走 `extent`（`fit = size-1` 但判据 `fit >= size` 为假 ⇒ 4 参其实也走 prefix）。★ 正确对齐方式就是**把 4 参的判据原样搬进来**——这也是为什么这一支必须是 `fit >= wide.size()` 而**不是** `fitW >= wide.size()`。

### 1.7 `src/Render/FontEngine.h/.cpp`（复用 `cps`，零分配）

**头**（`:142` 后）：加 5 参声明（**无** `override`——`FontEngine` 不派生 `TextMeasurer`）。

**`.cpp`**（4 参函数体闭合 `:556` 之后）：**只删 `:529` 的整串解码，`totalCp` 改表**：

```cpp
TextFit FontEngine::FitText(const Font& font, const std::string& text,
                            std::size_t startCp, float maxWidth,
                            const std::vector<char32_t>& cps,
                            const std::vector<std::uint32_t>& byteOffsets)
{
	(void)text;   // ★ 5 参不再从 text 解码（那是 :529 的整串 DecodeUtf8）；text 仅为签名统一
	Impl& impl = *m_impl;
	if (byteOffsets.size() < 2)
	{
		return TextFit{ 0, 0.0f };
	}
	const std::size_t totalCp = byteOffsets.size() - 1;   // ★★ M3：**不是** cps.size()
	                                                      //    它同时喂下面的守卫与无 face 兜底，
	                                                      //    现状 `:509` 是 ByteOffsetToCodepointIndex
	                                                      //    （SequenceLength 空间）；取 cps.size()
	                                                      //    会在脏字节上漂移（违反红线②）
	if (startCp >= totalCp)
	{
		return TextFit{ 0, 0.0f };
	}

	const int dpi = (impl.dpi > 0) ? impl.dpi : 96;
	const int px = PixelSize(font);
	const FaceId faceId = FaceIdFor(font.family, font.weight);
	FT_Face face = impl.FaceOf(faceId);
	if (face == nullptr)
	{
		return TextFit{ totalCp - startCp, 0.0f };   // 同 4 参 `:523`
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

	const float limit = (std::max)(0.0f, maxWidth);
	const float toDip = 96.0f / static_cast<float>(dpi);
	long advancePx = 0;
	std::size_t fitCp = 0;
	// ★★ 循环上界是 `cps.size()`、起点 `startCp` 是 SequenceLength 空间的索引 ——
	//    这是**既有分歧**（4 参 `:534` 同形），**原样保留、不得顺手修**（修了就改脏字节行为）
	for (std::size_t i = startCp; i < cps.size(); ++i)
	{
		long advance = 0;
		if (!impl.AdvanceOf(face, faceId, px, cps[i], advance))
		{
			++fitCp;   // 缺字形 ⇒ advance 不计但码点仍消费（同 4 参 `:541`）
			continue;
		}
		const long nextPx = advancePx + advance;
		if (static_cast<float>(nextPx) * toDip > limit)
		{
			break;
		}
		advancePx = nextPx;
		++fitCp;
	}

	return TextFit{ fitCp, static_cast<float>(advancePx) * toDip };
}
```

### 1.8 `src/Render/FreeTypeTextMeasurer.h/.cpp`（转发）

**头**（`:31` 后）：加 5 参声明（`override`）。

**`.cpp`**（`:52` 的 4 参闭合后）：

```cpp
TextFit FreeTypeTextMeasurer::FitText(const Font& font, const std::string& text,
                                      std::size_t startCp, float maxWidth,
                                      const std::vector<char32_t>& cps,
                                      const std::vector<std::uint32_t>& byteOffsets)
{
	if (!m_engine)
	{
		// 无 engine ⇒ 放行全串、宽 0（与 4 参 `:46-49` 同口径，但码点数取表 = O(1)）
		if (byteOffsets.size() < 2)
		{
			return TextFit{ 0, 0.0f };
		}
		const std::size_t totalCp = byteOffsets.size() - 1;
		return TextFit{ (startCp < totalCp) ? (totalCp - startCp) : 0, 0.0f };
	}
	return m_engine->FitText(font, text, startCp, maxWidth, cps, byteOffsets);
}
```

---

## §2 逐文件逐行改动清单（30 处，含批次归属）

**批 1 = 基础设施（零行为变化：新设施无人调用，除测试）** · **批 2 = 切换消费（行为面）**

| # | 批 | 文件 | 锚点 | 改动 | 性质 |
|---|---|---|---|---|---|
| 1 | 1 | `TextWidget.h` | `:11` 后 | `#include <cstdint>` | 插入 |
| 2 | 1 | `TextWidget.h` | `:142` 后 | `ByteMapKey` + 3 声明 + 3 成员（§1.1） | 插入 |
| 3 | 1 | `TextWidget.cpp` | `:14` 后 | `#include <cstdint>` / `<limits>` / `ECDIAssert.h` | 插入 |
| 4 | 1 | `TextWidget.cpp` | **`:196` 后**（★ v1.1：v1.0 误写 `:194`——那是 `for` 闭合） | `BuildByteOffsets` + `OffsetAt`（匿名 ns，§1.2a） | 插入 |
| 5 | 1 | `TextWidget.cpp` | `:200` 后 | `CurrentByteMapKey` / `EnsureCpByteOffsets` / `ByteOffsetAt`（§1.2b） | 插入 |
| 6 | 1 | `TextMeasurer.h` | `:10` 后 | `#include <cstdint>` / `<vector>` | 插入 |
| 7 | 1 | `TextMeasurer.h` | `:119` 后 | 5 参 `FitText` + 默认体（§1.3） | 插入 |
| 8 | 1 | `TextBox.h` | `:211` 后 | `CurrentByteMapKey` override 声明（§1.4） | 插入 |
| 9 | 1 | `TextBox.cpp` | `:548` 后 | `CurrentByteMapKey` 定义（§1.5） | 插入 |
| 10 | 1 | `GDITextMeasurer.h` | `:45` 后 | 5 参声明 | 插入 |
| 11 | 1 | `GDITextMeasurer.cpp` | 4 参闭合后 | `kFitWindowInitCp` + 5 参定义（§1.6） | 插入 |
| 12 | 1 | `FontEngine.h` | `:142` 后 | 5 参声明（非 override） | 插入 |
| 13 | 1 | `FontEngine.cpp` | `:556` 后 | 5 参定义（§1.7） | 插入 |
| 14 | 1 | `FreeTypeTextMeasurer.h` | `:31` 后 | 5 参声明（override） | 插入 |
| 15 | 1 | `FreeTypeTextMeasurer.cpp` | `:52` 后 | 5 参定义（§1.8） | 插入 |
| 16 | 1 | `WidgetTests.cpp` | `:651` 后 | `WrapMeasurer` 5 参 override（§3.1） | 插入 |
| 17 | 1 | `TextBoxTests.cpp` | `:382` 后 | `CountingFakeMeasurer` 5 参 override（§3.1） | 插入 |
| 18 | 1 | `TextBoxTests.cpp` | `:1987` 后 | `RecordingFakeMeasurer` 5 参 override（§3.1） | 插入 |
| 19 | 1 | `WidgetTests.cpp` | `TestableLabel` 后（`:459` 后） | `ByteMapProbe`（§3.2） | 插入 |
| 20 | 1 | `TextBoxTests.cpp` | `kFitGolden` 后（`:156` 后） | `TableFor` helper（§3.3） | 插入 |
| 21 | 1 | `WidgetTests.cpp` / `TextBoxTests.cpp` | Register 末尾 | T40-1 / T40-2 / T40-5 / T40-8 登记 | 插入 |
| 22 | 2 | `TextWidget.cpp` | `:216` 后 | 局部 `byteOffsets` + `BuildByteOffsets`（§1.2c） | 插入 |
| 23 | 2 | `TextWidget.cpp` | `:229-230` | 两处换算 → `OffsetAt(byteOffsets, …)`（★ **钳制**，D-9） | 替换 |
| 24 | 2 | `TextWidget.cpp` | `:270` | 4 参 → 5 参（传 `cps, byteOffsets`） | 替换 |
| 25 | 2 | `TextWidget.cpp` | `:395` 前 + `:405-406` | `EnsureCpByteOffsets(m_text)` + `ByteOffsetAt` | 插入+替换 |
| 26 | 2 | `TextBox.cpp` | `:895-896` / `:899` | `EnsureCpByteOffsets(display)` + `ByteOffsetAt` + O(1) 码点数 | 替换 |
| 27 | 2 | `TextBox.cpp` | `:905` / `:910` | 两处换算 → `ByteOffsetAt`（减 `startByte`） | 替换 |
| 28 | 2 | `TextBox.cpp` | `:1454` 后 | `EnsureCpByteOffsets(textSource)` | 插入 |
| 29 | 2 | `TextBox.cpp` | `:1489-1490` / `:1502-1503` / `:1526-1527` | 六处换算 → `ByteOffsetAt` | 替换 |
| 30 | 2 | `WidgetTests.cpp` / `TextBoxTests.cpp` | Register 末尾 | T40-3 / T40-4 / T40-6 / T40-7 登记 | 插入 |

**批 2 的 `CaretIndexFromLineX` 精确改法**（`:890-919`）：

```cpp
	// 改前
	const size_t startByte = CodepointIndexToByteOffset(display, lineStartCp);      // :895
	const size_t endByte   = CodepointIndexToByteOffset(display, lineEndCp);        // :896
	const size_t lineCpCount = ByteOffsetToCodepointIndex(lineText, lineText.size());   // :899
	// 改后
	const size_t startByte = ByteOffsetAt(lineStartCp);        // ★ 先 EnsureCpByteOffsets(display)（插在 :893 后）
	const size_t endByte   = ByteOffsetAt(lineEndCp);
	const size_t lineCpCount = lineEndCp - lineStartCp;        // ★ O(1)：`LineRange` 已给码点区间
```
★ `:905` / `:910` 的 `CodepointIndexToByteOffset(lineText, i+1)` / `(lineText, i)` 改
`ByteOffsetAt(lineStartCp + i + 1) - startByte` / `ByteOffsetAt(lineStartCp + i) - startByte`
（**减 `startByte`** 是因为 `lineText` 是相对切片）。★ `:899` 的简化是红线②的关键点：`lineText` 的码点数 == `lineEndCp - lineStartCp`，因为 `lineText = display.substr(startByte, endByte-startByte)` 且两端都是码点边界。

---

## §3 测试侧改动

### 3.1 三个量具必须各加 5 参 override（M1——不改就红 ~15 条 / 假绿 ~14 条）

★ **为什么不能靠默认体**：`BuildTextLayout:270` 一旦改调 5 参，三个只覆写 4 参的量具的 override **一次都不进**（虚派发按 5 参签名找，基类默认体接手）⇒ 计数与 `fed` 全部空转。

```cpp
// (a) WrapMeasurer（WidgetTests.cpp:647 后）——★ 保持「转发批一默认体」的语义
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
	                const std::vector<char32_t>&, const std::vector<std::uint32_t>&) override{
		++m_fitCalls;
		return TextMeasurer::FitText(font, text, startCp, maxWidth);
	}

// (b) CountingFakeMeasurer（TextBoxTests.cpp:382 后）——★ 转发到**本类 4 参**（计数原样保留）
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
	                const std::vector<char32_t>&, const std::vector<std::uint32_t>&) override{
		return CountingFakeMeasurer::FitText(font, text, startCp, maxWidth);   // 4 实参 ⇒ 命中本类 4 参
	}

// (c) RecordingFakeMeasurer（TextBoxTests.cpp:1987 后）——★ 同上，但**类名必须换成自己**
//     （两个量具无继承关系，照抄 (b) 的类名会编译不过——v1.1 订正，nit 6）
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
	                const std::vector<char32_t>&, const std::vector<std::uint32_t>&) override{
		return RecordingFakeMeasurer::FitText(font, text, startCp, maxWidth);   // 计数 + `fed` 原样保留
	}
```
★ **必须写类名限定**：不限定也能选对（重载解析按实参数 4 vs 6），但限定把「不递归」写成显式事实，防止后人误改形参。
★ **`FakeTextMeasurer`（`WidgetTests.cpp:426` / `TextBoxTests.cpp:32`）不覆写 `FitText`** ⇒ 零改动、走 5 参默认体（红线③的活证据）。

### 3.2 `ByteMapProbe`（T40-1 / T40-8 的接缝——**取代初设的静态接缝**，见 D-2）

```cpp
/// @brief 表访问探针（★ Phase 40：`m_cpByteOffsets` 是 protected 成员 ⇒ 派生类可直接读）
class ByteMapProbe : public TestableLabel{
public:
	using TextWidget::EnsureCpByteOffsets;   // protected ⇒ using 暴露（第二个样本必然重建）
	using TextWidget::ByteOffsetAt;

	/// @brief 建表（★ `SetText` 递增 revision ⇒ 键**必然**失配 ⇒ 必重建，不受上一轮影响）
	void Build(const std::string& s){
		SetText(s);
		EnsureCpByteOffsets(s);
	}
	[[nodiscard]] std::size_t TableSize() const { return m_cpByteOffsets.size(); }   // protected 成员，派生类可达
	[[nodiscard]] std::size_t At(std::size_t k) const { return ByteOffsetAt(k); }
};
```
★ 走 `TestableLabel` 而非 `Label`：`ResolveMeasurer()` 返回 `FakeTextMeasurer` ⇒ 无窗口也可建表（`BuildByteOffsets` 不依赖 measurer，这一步其实更宽松）。
★ **不用 `BuildTextLayout` 建表**：那样会走局部表（D40-L-1），基类表永远是空的。

### 3.3 `TableFor`（T40-2 的表输入；纯公共 API，与产品码独立）

```cpp
/// @brief 用**公共换算 API** 构造偏移表（★ 与产品 `BuildByteOffsets` 独立实现——T40-1 才有意义）
/// @details 逐 k 取 `CodepointIndexToByteOffset`，到串尾（含空串）为止 ⇒ `size() == count+1`
static std::vector<std::uint32_t> TableFor(const std::string& s){
	std::vector<std::uint32_t> t;
	for (std::size_t k = 0;; ++k){
		const std::size_t b = CodepointIndexToByteOffset(s, k);
		t.push_back(static_cast<std::uint32_t>(b));
		if (b >= s.size()){ return t; }
	}
}
```
★ 空串：`k=0` 得 `0 >= 0` ⇒ 返回 `[0]`（`size()==1`）——与 `BuildByteOffsets` 一致。

---

## §4 用例 T40-1..8（538 → **546**，+8）

| # | 批 | 登记名 | 断言 | 期望来源 / 为什么必须存在 |
|---|---|---|---|---|
| **T40-1** | 1 | `Text.CpByteTable.MatchesSequenceLength` | 对样本集逐 `k ∈ [0, TableSize())` 断言 `probe.At(k) == TableFor(s)[k]`，且 `probe.TableSize() == TableFor(s).size()`。★ 样本集 = CJK / ASCII / emoji / overlong `C0 8A` / 截断 `"A\xC0"` / 非法首字节 `"\x80\x8A"` / 空串 **+ v1.1 必补：`"\xE0\x41\x41"` / `"\xF0\x41\x41\x41"`** | **T1 的唯一直接证据**。★ `"A\xC0"` 必测（R40-12：T1 依赖跨 TU 隐式耦合 `DecodeCodepointAt` 步进 == `SequenceLength`，钳一步进就静默失效且不报错）；`"\x80\x8A"` 是 `cps < count` 的分歧点（R40-1）；**`"\xE0\x41\x41"` / `"\xF0\x41\x41\x41"` 是 `cps > count` 的分歧点（D-9）——v1.0 的样本集全部满足 `cps ≤ count`，测不出越界洞** |
| **T40-2** | 1 | `Text.FitText.TableAwareEqualsLegacy` | 对 `kFitGolden` 8 组 **+ v1.1 追加的两类脏字节**（`"\xE0\x41\x41"` 的 `startCp∈{0,1,2}` / `"\xF0\x41\x41\x41"` 的 `startCp∈{0,1,3}`），同一 `FakeTextMeasurer`：`FitText(4 参)` vs `FitText(5 参, TableFor(s))` 的 `fitCp` / `width` 逐位相等 | **红线②的直接证据**。★ 只用 `startCp == 0` 会假绿（P39-R2 教训）；★ **v1.1 订正（should-fix 5）**：v1.0 写「追加 4 组（`startCp≠0` 的 CJK / overlong / `startCp>=totalCp` / 空串）」是**重复**——`kFitGolden:147-156` **已全部包含**（`{"一丁丂",1,…}` / `{"\xC0\x8A",0,…}` / `{"abcdef",6,…}` / `{"",0,…}`）。真正要追加的只有 D-9 那两类 |
| **T40-3** | 2 | `Text.Wrap.LineTableBitIdentical` | wrap（`Label` + 真窗口，`kWrapSamples` 的 23 行样本）与非折行（`TestableTextBox`）下 `m_lines` / `m_wrapLayout.lines` 的 `startCp`/`cpCount`/`width`/`ownedEnd` 逐位。★ **v1.1 必补 D-9 样本**：`"\xE0\x41\x41"` / `"\xF0\x41\x41\x41"` 在 wrap 下走「`emitLine` 回退分支 + `cps > count` 索引」——**这是 v1.0 唯一会撞 `FRAMEWORK_ASSERT` 的可达路径**，钳制后必须安静通过且行表逐位不变 | 红线①。既有 538 全绿已覆盖大部分，本条补「新样本 × 两分支」；★ 若不补 D-9 样本，这条用例对 `cps > count` 完全失明 |
| **T40-4** | 2 | `Text.Caret.LineXBitIdentical` | wrap / 非折行两模式下一组 `(lineIndex, innerX)` 样本，`CaretIndexFromLineX` 返回的全局码点索引逐位 | §2 #26 的 `:899` 简化（`lineEndCp - lineStartCp`）是唯一有「改错」风险的一行——R40-7 |
| **T40-5** | 1 | `Text.Wrap.OverlongSameSource` | `C0 8A` / `E0 80 8A` / `F0 80 80 8A` 三档：表项（T40-1 同法）+ 行数 == 2（`BuildTextLayout:243` 的 `cps[i]==U'\n'` 判据与表**同源**） | A40-7。★ 负对照见 §6.4（R40-3） |
| **T40-6** | 2 | `Text.Paint.WrapBytesBitIdentical` | wrap 长文本经 `RecordingBackend`，每行 `DrawText` 命令的 `text` 字节逐位（含选区高亮/组合下划线的相对偏移） | §1.2c/§1.5 的 `substr` 字节不变性——`startByte` 与 `hlStartByte` 都来自同一张表同一串 |
| **T40-7** | 2 | `Text.FitText.GdiWindowedEqualsLegacy` | `GDITextMeasurer` **不 Initialize**（dpi 落 96）自比：4 参 vs 5 参在 A40-4 全样本上逐位（含跨窗口边界：`maxWidth` 取「放得下 1 / 63 / 64 / 65 / 全部」五档） | D40-D 倍增窗口的精确性。★ 相对自比（不做绝对 golden）——字体依赖系统（D-7） |
| **T40-8** | 1 | `Text.CpByteTable.RebuiltOnKeyChange` | (a) `probe.Build("•••")`（9 字节 / 3 码点）⇒ `TableSize()==4`、`At(3)==9`；(b) 紧接着 `probe.Build("abc")`（3 字节 / 3 码点）⇒ `TableSize()==4`、`At(3)==3`（**若复用旧表则为 9 ⇒ 红**）；(c) `probe.Build("")` ⇒ `TableSize()==1`、`At(0)==0` | 键正确性（revision 在键里）+ 表与当前串匹配。★ 初设的「同键不同串」子断言**已删除**（D-3：debug 下会被自检断言中止进程） |

★ **不做**：不断言耗时（数字纪律）；不测 20k 表内容（只测 count 与探针）。

---

## §5 机检 C40-1..9（命令**全部实跑过**，读数为 HEAD `a377026` 实测）

| # | 契约 | 命令（工作目录 = 仓库根） | 今日实测 → 目标 |
|---|---|---|---|
| **C40-1** | `BuildTextLayout` 体内换算 == 0 | `awk '/^TextWidget::TextLayout TextWidget::BuildTextLayout/,/^}/' ECDI/src/Widget/TextWidget.cpp \| grep -v '^[[:space:]]*//' \| grep -c -E "CodepointIndexToByteOffset\|ByteOffsetToCodepointIndex"` | **2** → **0** |
| **C40-2** | `DrawTextContent` 体内 == 0 | 同上，锚点换 `/^void TextWidget::DrawTextContent/` | **2** → **0** |
| **C40-3** | `OnPaint` 体内 == 0 | 同上，文件换 `ECDI/src/Widget/TextBox.cpp`，锚点 `/^void TextBox::OnPaint/` | **6** → **0** |
| **C40-4** | `CaretIndexFromLineX` 体内 == 0 | 同上，锚点 `/^size_t TextBox::CaretIndexFromLineX/` | **5** → **0** |
| **C40-5** | 表构建单源：`DecodeCodepointAt` 在 `TextWidget.cpp` 恰好 1 处 | `grep -c "DecodeCodepointAt(" ECDI/src/Widget/TextWidget.cpp` | **0** → **1** |
| **C40-6** | 5 参声明/定义齐备：7 文件各 2 处（4 参 + 5 参） | `grep -c "FitText(const Font& font, const std::string& text," ECDI/src/Render/GDITextMeasurer.h ECDI/src/Render/GDITextMeasurer.cpp ECDI/src/Render/FontEngine.h ECDI/src/Render/FontEngine.cpp ECDI/src/Render/FreeTypeTextMeasurer.h ECDI/src/Render/FreeTypeTextMeasurer.cpp ECDI/include/ECDI/Render/TextMeasurer.h` | 各 **1** → 各 **2** |
| **C40-7** | 无退役标识符；用例数 546 | `grep -rn "m_lineStarts\|m_needsLineRecalc" --include=*.cpp --include=*.h ECDI/ \| wc -l`；用例数取运行器 `Tests: N` 汇总行 | **0** / 538 → 546 |
| **C40-8** | 基类表**单一写者**（**两条命令，缺一不可**） | (a) `grep -c "BuildByteOffsets(text, m_cpByteOffsets)" ECDI/src/Widget/TextWidget.cpp`；(b) `grep -n "m_cpByteOffsets\s*=" ECDI/src/Widget/*.cpp \| wc -l` | (a) **0** → **1**；(b) **0** → **0** |
| **C40-9** | ★ v1.1 新增：**钳制单源**——消费站点**零裸下标** | (a) `grep -n "m_cpByteOffsets\[" ECDI/src/Widget/*.cpp \| wc -l`（基类表从不裸下标）；(b) `grep -c "offsets\[(std::min)" ECDI/src/Widget/TextWidget.cpp`（★ **v1.1 实施订正**：v1.0 写的 `byteOffsets\[` 恒为 **0**——`BuildByteOffsets` / `OffsetAt` 的形参叫 `offsets`，不是 `byteOffsets`；实测 :240 是唯一钳制点） | (a) **0** → **0**；(b) **0** → **1** |

★ **C40-8 为什么必须是两条命令**（初设只写「应恰好 1 处」，口径松动会误报）：
`BuildByteOffsets(text, m_cpByteOffsets)` 里 `m_cpByteOffsets` 是**实参**（函数内操作的是形参 `offsets`）⇒ 它**不匹配** `m_cpByteOffsets\s*=`，所以「0 处 `=` 赋值」才是正确结果，而不是 1。
⇒ 单写者要用**两条**钉死：(a) 传参处恰好 **1**（且位于 `EnsureCpByteOffsets` 内）；(b) 任何 `m_cpByteOffsets = …` 直接赋值 **0** 处。两条同时成立才叫单写者。

★ **哪些东西刻意不设机检（v1.1 诚实边界）**：
- 「FT 5 参不再 `DecodeUtf8`」——机检**无法区分**「函数没写」与「写了但没调 `DecodeUtf8`」（改前该函数不存在 ⇒ 提取块为空、`grep -c` 同样得 0）⇒ 由**代码审查 + 红线②（T40-2 逐位相等）**守，并列入 §7 的 R40-4 人工核对项。★ 注意 T40-2 **抓不到**它（仍解码则行为一样、只是慢），这是**判据的真实边界**，不装作有。
- 「量具 override 是否漏」——`grep -c "override"` 不可靠 ⇒ 由 **T38-15（`fitCalls == 3`）必须仍绿**守（漏了必红）。

---

## §6 实现批次与回归门

### 6.1 批 1（基础设施，零行为变化）★ **2026-10-29 已落码**

**前置动作（批 0 的实质纪律，D-5）**：先跑一次测试确认 **538 全绿**，否则不开始。★ **未执行**——本批落码后由用户首次编译即验证（见下方「静态自检已做/未做」）。
**改动**：§2 #1–#21（两个公共头 + `TextBox.h` + 一 .cpp 设施 + 三后端 + 三个量具 + 两个 helper + 4 条用例）。
**为什么零行为变化**：新设施**无人调用**（`BuildTextLayout` 仍调 4 参、`DrawTextContent`/`OnPaint`/`CaretIndexFromLineX` 未动）⇒ 除新增用例与量具 override 外，运行路径逐字节不变。
**编译**：一次（`TextWidget.h` + `TextMeasurer.h` + `TextBox.h` 三个头 ⇒ 近全量，约 35 min 量级）。
**门**：**542 全绿**（538 + T40-1/2/5/8）+ C40-6 各 2 + C40-5 == 1。

#### 6.1.1 落码实测（2026-10-29）

| 项 | 实测 | 结论 |
|---|---|---|
| `git diff --stat -- ECDI/` | **13 文件 / 655 insertions / 0 deletions** | ★ **红线③结构性成立**：4 参 `FitText` 与其默认体**一字未动**（`TextMeasurer.h` 是纯 64 行新增） |
| C40-1..4 | **2 / 2 / 6 / 5 —— 与改前完全相同** | ★ **「零行为变化」的直接证据**：批 1 不碰任何消费站点 |
| C40-5 | `DecodeCodepointAt(` == **1** | 表构建单源 ✓ |
| C40-6 | 7 文件各 **2** | 4 参 + 5 参齐备 ✓ |
| C40-8 | (a) **1** / (b) **0** | 基类表单写者 ✓ |
| C40-9 | (a) **0**（无 `m_cpByteOffsets[`）/ (b) `offsets[(std::min)` == **1** | 钳制单源 ✓ |
| BOM | 与原文件**一一对应**（`TextMeasurer.h` / `GDITextMeasurer.*` / `FontEngine.*` / `WidgetTests.cpp` 原本无 BOM，**未擅自添加**） | 不制造无谓整文件 diff ✓ |
| 行尾 | 13 文件全 **CRLF**（CR == LF） | ✓ |

#### 6.1.2 语法预检（★ 本批新增工序——不替代用户编译，只提前排雷）

用 MinGW g++ 对**全部 13 个改动文件中可独立解析的 7 个**做 `-fsyntax-only`（`-I ECDI/include -I ECDI/src -I ECDI/src/Tests -I third_party/freetype/include`）：

| 文件 | 结果 |
|---|---|
| `src/Widget/TextWidget.cpp` | ✅ exit 0 |
| `src/Widget/TextBox.cpp` | ✅ exit 0 |
| `src/Render/GDITextMeasurer.cpp` | ✅ exit 0 |
| `src/Render/FontEngine.cpp` | ✅ exit 0 |
| `src/Render/FreeTypeTextMeasurer.cpp` | ✅ exit 0 |
| `src/Tests/WidgetTests.cpp` | ✅ exit 0（**修掉一处真错后**，见下） |
| `src/Tests/TextBoxTests.cpp` | ✅ exit 0 |

★ **这一步抓到并修掉一处真错**（若留给用户编译，要等 ~35 min 才发现）：`ByteMapProbe` **忘了继承构造函数** —— `ByteMapProbe probe("")` 报 `no matching function`，因为 `class ByteMapProbe : public TestableLabel` 只有隐式默认构造。修 = 加 `using TestableLabel::TestableLabel;`。
★ **这一步也订正了 §5 的一条机检判据**（见 D-13）。
★ **未做（诚实声明）**：没有链接、没有跑测试、没有跑负对照（R40-3）——这些需要用户环境的完整构建（FreeType 静态库 + Windows 库）。`-fsyntax-only` 只能扛「编译不过」，扛不了「行为不对」。

#### 6.1.3 实施期对详设的两处订正

| # | 详设原写 | 实际 | 处置 |
|---|---|---|---|
| **D-13** | C40-9(b) = `grep -c "byteOffsets\[" ECDI/src/Widget/TextWidget.cpp` == 1 | 实测 **0**——`BuildByteOffsets` / `OffsetAt` 的形参叫 `offsets`，不是 `byteOffsets`；唯一钳制点是 `offsets[(std::min)(k, offsets.size() - 1)]` | §5 判据改为 `grep -c "offsets\[(std::min)"` == 1（**v1.2 已改**） |
| **D-14** | §3.2 的 `ByteMapProbe` 片段未写继承构造 | `ByteMapProbe probe("")` 编译不过 | 加 `using TestableLabel::TestableLabel;`（**已落码**） |

### 6.2 批 2（切换消费，行为面）★ **2026-10-29 已落码**

**改动**：§2 #22–#30（`BuildTextLayout` 局部表 + 5 参、`DrawTextContent`、`OnPaint`、`CaretIndexFromLineX` + 4 条用例）。
**编译**：只改 `.cpp`（`TextWidget.cpp` / `TextBox.cpp` + 测试）⇒ **不触发全量**（快）。
**门**：**546 全绿** + C40-1..4 全 **0** + C40-8 两条 + 红线①（T40-3）②（T40-2/T40-7）③（`TextMeasurer.h` 的 4 参段 `git diff` 为空）。

#### 6.2.1 落码实测（2026-10-29）

- `git diff --stat`：**13 文件 / 883 insertions / 21 deletions**（批 1 是 0 deletions；批 2 的 21 行删除**全部**是 §2 要替换的旧换算调用 + 一行过时注释，逐行核对无夹带）。
- 7 个 TU 语法预检全 `exit=0`。
- C40-1..4 = **0 / 0 / 0 / 0**；C40-5 = 1；C40-8 (a) 1 / (b) 0；C40-9 (a) 0 / (b) 1。

#### 6.2.2 实施期订正（2 条，均须回写）

| # | 问题 | 处置 |
|---|---|---|
| **I-1** | ★★ **§2 #26 的 `lineCpCount = lineEndCp - lineStartCp` 不逐位等价**（详设 v1.1 写错） | 必须**钳制后再做差**：`lineCpCount = min(lineEndCp, offsetLastCp) - min(lineStartCp, offsetLastCp)`（`offsetLastCp = m_cpByteOffsets.size()-1`）。**反例**：`"\xE0\x41\x41"`（cps=2 / count=1）第 2 行 `LineRange = (1,2)` ⇒ `startByte = endByte = 3`（都钳到末项）⇒ `lineText = ""`，旧式 `ByteOffsetToCodepointIndex("",0) = 0`，而不钳制的差值 = `2-1 = 1` ⇒ **不相等**。钳制后 `min(2,1) - min(1,1) = 0` ✓。合法文本上 `min` 恒为恒等 ⇒ 逐位不变 |
| **I-2** | ★ **C40-1..4 的命令口径漏了「排除注释」** | 批 2 给消费站点加了说明性注释（如「钳到末项与旧行为 `CodepointIndexToByteOffset` 的退出行为逐位等价」），命令会把**注释里的函数名**数进去 ⇒ 实测 C40-1 得 **1**、C40-4 得 **2**，而真实调用已是 0。⇒ 命令一律加 `grep -v '^[[:space:]]*//'`。**已用 HEAD 版本复核：新口径下基线仍是 2/2/6/5**（口径订正不影响基线） |

★ **I-2 的教训一般化**：凡「按函数名 grep 计数」的机检，都必须先排除注释行——否则**把改动写好这件事本身会让判据变红**，逼着人删注释。这类判据应当**先在一个已知含注释的样本上验过**才算立住。

★ **T40-4 的判据边界（如实登记）**：无窗口环境下 `CaretIndexFromLineX` 的测量循环被 `GetWindow() != nullptr` 守卫短路 ⇒ 恒返回行首。故 T40-4 只能守「行首返回值与行表一致 + 极端 X 不越界」，**不能**守「行内 X 落在第几个码点」；后者需真窗口，列入 R40-7 的人工核对项（不在本 Phase 假装验了）。

★ **`kWrapBarSlot` 上移**：T40-3/4/6 位于 Phase 38 夹具区之前，而该常量原定义在其后 ⇒ 把 `constexpr int kWrapBarSlot = 14;`（含注释）**整体上移**到 namespace 开头，原位留一行说明。纯移动、零语义变化（该常量与 Phase 38 无耦合——任何折行夹具都要扣这条槽）。

#### 6.2.3 测试期订正（I-3 · T40-6 首轮红——判据自身缺陷，非产品缺陷）

**现象**：批 2 首轮 `545 passed / 1 failed`，红的是 `Text.Paint.WrapBytesBitIdentical` 的 `painted.size() == expect.size()`（3 条 = `"\xE0\x41\x41"` 的三个宽度）。

**根因（判据侧，非产品侧）**：`OnPaint` 的绘制判据是 `!lineText.empty()`——即**切出来的字节**非空；而 T40-6 的期望侧写的是 `draw.second <= draw.first`——**码点区间**非空。二者在脏字节串上会分开：

| `"\xE0\x41\x41"` | `totalCp`（SequenceLength） | `cps`（DecodeUtf8） | 行表 |
|---|---|---|---|
| 值 | **1** | **2** | 被切成 **2 行**（`FitText` 因 `startCp >= totalCp` 卡住，只放 1 个码点/行） |

- 行 0 `draw=(0,1)` ⇒ `b0 = off[0] = 0`、`b1 = off[min(1,1)] = off[1] = 3` ⇒ 切出 **3 字节**（整串）⇒ 产品**画**；
- 行 1 `draw=(1,2)` ⇒ `b0 = b1 = 3`（都钳到末项）⇒ 切出 **0 字节** ⇒ 产品**不画**。

⇒ 产品 1 条、期望 2 条（多一个空串）⇒ 假红。

**处置**：期望侧的判空改为 `piece.empty()`，与产品**同一个口径**（切字节判空，不是判码点区间）。★ 这不削弱判据——字节内容仍由 `TableFor`（公共换算 API 的独立实现）产出，只是「哪一行该发命令」的判据必须与产品同构。

★ **一般化教训**：写「命令流逐位」类用例时，**期望侧的过滤条件必须逐字镜像产品的过滤条件**（包括判空用的是字节还是码点、边界取 `>` 还是 `>=`）。否则判据会在**最脏的样本**上假红——而脏样本恰恰是它最该覆盖的地方。

★ **旁证**：首轮其余 545 条全绿，其中 **T40-3（`"\xE0\x41\x41"` / `"\xF0\x41\x41\x41"` 折行）× 5 档宽** 与 **T40-7（GDI 倍增窗口 vs 整尾拷贝）** 均通过 ⇒ **D-9 的钳制确实封住了那条 3 字节可达的越界路径**，D40-D 的倍增窗口在跨窗口边界上也逐位一致。



### 6.3 记录制探针（A40-2 / A40-3，不入 CMake）

源码贴 §6.5（**v1.1 已补全：可独立编译 + 改前/改后两支同体**）；命令与读数入档（数字纪律：只记读数、不设阈值）。

**分工（v1.1 明确，避免「探针没落地」）**：

| 判据 | 由谁证明 | 为什么 |
|---|---|---|
| **A40-3 耗时比**（B：重建） | **探针**（§6.5 的四档 `n`，同一支两支对比） | 需要计时，只有探针能给 |
| **A40-2 的 B 侧分配**（`FitText` 的 `vector`） | **探针的 `allocs` 列 + C40-10 机检** | 探针测「表版 1 次分配 / 改前 L 次」；FT 侧是否真的不再 `DecodeUtf8` 用 C40-10 查 |
| **A40-2 的 GDI 侧拷贝量** | **探针的 `bytes` 列（纯 `substr` 模拟，不需真 DC）** | 拷多少字节与 DC 无关，只需 `byteStart` 与窗口长 |
| **A40-3 的 paint 侧**（K7） | **C40-1..4 机检（换算调用归零）+ T40-6（命令流逐位）** | 不做耗时探针：paint 的分配主源是 `textSource.substr`（每行一次），**改前改后相同**，本 Phase 不改它；K7 的收益是**换算次数归零**，机检比计时更硬 |

★ 数字纪律：**只记读数，不写「提升 X 倍」**。

### 6.4 负对照（R40-3——判据有牙的唯一证明）

把 `BuildByteOffsets` 的步进**临时**改为「跳过非法首字节」（即 `DecodeUtf8` 语义），只改一行：
`byte += DecodeCodepointAt(...)` → 手工判前导、非法则 `byte += 1` 且不 push。
⇒ **T40-1 的 `"\x80\x8A"` / `"A\xC0"` / `"\xE0\x41\x41"` 三样本必须红**（前两个测 `cps < count` 方向、
第三个测 `cps > count` 方向）。红了才算判据有牙（沿 P39-R3）。改完立刻还原。

### 6.5 探针源码（附录，独立编译，不入 CMake / 不入 `src/Tests`）

★ v1.1 订正（should-fix 4）：v1.0 的片段 **① 缺 `#include "ECDI/Core/UTF8.h"`（照贴必编不过）、② 只有「表版」没有 `loop_current`（A40-3 拿不到数据）、③ 只测建表没测 churn**。本稿补全为**可独立编译的完整文件**。

```cpp
// probe_p40.cpp —— A40-2 / A40-3 记录制探针（改前 loop_current vs 改后 table，同体对比）
// 编译：g++ -std=c++20 -O2 -I ECDI/include -I ECDI/src probe_p40.cpp ECDI/src/Core/UTF8.cpp -o probe.exe
#include <algorithm>   // ★ v1.1：std::min（不能靠 <vector>/<string> 间接带进来）
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "ECDI/Core/UTF8.h"      // ★ v1.1：v1.0 缺这一行 ⇒ 照贴编译不过
#include "Render/Utf8Decode.h"

static std::size_t g_allocs = 0;
static std::size_t g_bytes  = 0;
void* operator new(std::size_t n){ ++g_allocs; g_bytes += n; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ── 改前复刻（B 的原形态）：每「行」都从串首重扫 ──
static std::size_t LoopCurrent(const std::string& s, std::size_t lineCount){
	const std::size_t count = ECDI::ByteOffsetToCodepointIndex(s, s.size());
	std::size_t sink = 0;
	for (std::size_t line = 0; line < lineCount; ++line){
		const std::size_t startCp = (line * count) / lineCount;
		sink += ECDI::CodepointIndexToByteOffset(s, startCp);        // O(startCp) × L
		sink += ECDI::ByteOffsetToCodepointIndex(s, s.size());       // O(N) 数 totalCp × L
		sink += ECDI::DecodeUtf8(s).size();                         // O(N) + 1 次分配（FT 侧）× L
	}
	return sink;
}

// ── 改后：一次建表 + 每行 O(1) ──
static std::size_t TableVersion(const std::string& s, std::size_t lineCount){
	std::vector<std::uint32_t> off;
	off.reserve(s.size() + 1);
	std::size_t byte = 0;
	while (byte < s.size()){
		off.push_back(static_cast<std::uint32_t>(byte));
		char32_t cp = 0;
		byte += ECDI::DecodeCodepointAt(s.data() + byte, s.size() - byte, cp);
	}
	off.push_back(static_cast<std::uint32_t>(byte));

	const std::size_t count = off.size() - 1;
	std::size_t sink = 0;
	for (std::size_t line = 0; line < lineCount; ++line){
		const std::size_t startCp = (line * count) / lineCount;
		sink += off[(startCp < off.size()) ? startCp : off.size() - 1];   // O(1) × L（含钳制）
	}
	return sink;
}

int main(){
	for (const int n : { 1000, 5000, 10000, 20000 }){
		std::string s;
		for (int i = 0; i < n; ++i){ s += "\xE4\xB8\x80"; }   // 「一」，3 字节
		const std::size_t lineCount = static_cast<std::size_t>(n / 28 + 1);   // ~28 字/行

		auto run = [&](const char* tag, std::size_t (*fn)(const std::string&, std::size_t)){
			const std::size_t a0 = g_allocs, b0 = g_bytes;
			const auto t0 = std::chrono::steady_clock::now();
			const std::size_t sink = fn(s, lineCount);
			const auto t1 = std::chrono::steady_clock::now();
			std::printf("n=%6d lines=%4zu %-12s %9.3f ms  allocs=%6zu bytes=%9zu sink=%zu\n",
				n, lineCount, tag,
				std::chrono::duration<double, std::milli>(t1 - t0).count(),
				g_allocs - a0, g_bytes - b0, sink);
		};
		run("loop_current", &LoopCurrent);
		run("table",        &TableVersion);

		// ── GDI 侧拷贝量（纯 substr 模拟，不需真 DC）：最坏行 = 首行 ──
		const std::size_t fullTail = s.size();                    // 改前：text.substr(0) 整尾
		const std::size_t window   = (std::min<std::size_t>)(64 * 3, s.size());   // 改后：首窗 64 码点 × 3 字节
		std::printf("           substr: full-tail=%zu B  window64cp=%zu B  ratio=%.0fx\n",
			fullTail, window, static_cast<double>(fullTail) / static_cast<double>(window));
	}
	return 0;
}
```
★ `sink` 只为防优化掉整段循环（不 print 会被 `-O2` 消除）。
★ **两支的「同体」在哪、差在哪**（Qoder 要求同体，这里说清）：循环骨架、行切分算式（`line * count / lineCount`）、
`sink` 累加**完全一致**；差异**恰好只有本 Phase 消除的那三项**——`CodepointIndexToByteOffset(s, startCp)`、
`ByteOffsetToCodepointIndex(s, s.size())`、`DecodeUtf8(s)`。所以耗时差就是「被消除的工作」，不是样本不等价。

★★ **v1.1 已实跑（2026-10-29 本机 g++ 13 / `-O2`，Windows）**——不是「预期形状」而是**实测读数**：

```
n=  1000 lines=  36 loop_current     0.114 ms  allocs=   36  bytes=    432000
n=  1000 lines=  36 table              0.002 ms  allocs=    1  bytes=     12004
n=  5000 lines= 179 loop_current     2.555 ms  allocs=  179  bytes=  10740000
n=  5000 lines= 179 table              0.009 ms  allocs=    1  bytes=     60004
n= 10000 lines= 358 loop_current    10.607 ms  allocs=  358  bytes=  42960000
n= 10000 lines= 358 table              0.018 ms  allocs=    1  bytes=    120004
n= 20000 lines= 715 loop_current    47.395 ms  allocs=  715  bytes= 171600000
n= 20000 lines= 715 table              0.037 ms  allocs=    1  bytes=    240004
substr: full-tail=60000 B  window64cp=192 B  ratio=312x   （n=20000 行）
```

| 观察 | 读数 | 与判据的对应 |
|---|---|---|
| `n` 5k → 20k（×4） | `loop_current` **2.555 → 47.395 ms = 18.6×**（超线性）；`table` **0.009 → 0.037 = 4.1×**（线性） | **A40-3 直接兑现**：改前 ≈ O(N²)、改后 ≈ O(N) |
| `allocs` @20k | **715**（= 行数）→ **1** | **A40-2 的 B 侧兑现**：每行一次 `DecodeUtf8` 的分配被消成「一次建表」 |
| `bytes` @20k | **171.6 MB** → **240 KB** | ★ 与需求 §1.3 的「**168 MB churn**」严丝合缝（171.6 MB / 715 行 = **240 KB/次** = `reserve(60000)` × 4 字节）⇒ **需求稿的量级估计被实测确认** |
| `substr` 最坏行 | 整尾 **60 000 B** vs 首窗 **192 B** = **312×** | **A40-2 的 GDI 侧兑现**（`W == remaining` 时退化为整尾，故只影响长串的行） |

★ **数字纪律**：上表是**记录制读数**（本机、`-O2`、单次），**不写成「提升 X 倍」的产品承诺**；正式读数在批 2 收口时用同一支探针复采一次入档。

---

## §7 风险处置（承接初设 R40-1..12，只写**实现期动作**）

| # | 风险 | 实现期动作 |
|---|---|---|
| R40-1 | 索引语义选错（用 `DecodeUtf8` 建表） | §1.2a 的注释把「只取 `DecodeCodepointAt` 返回值」写成红线；T40-1 的 `"\x80\x8A"` 样本必测 |
| R40-2 | `byte` 越过 `size` 后越界 | `BuildByteOffsets` 的 `while (byte < text.size())` 保证只在界内 `data + byte`；末次 `push_back` 不读内存 ⇒ 无 UB |
| R40-3 | 合法样本假绿 | §6.4 负对照，**必做** |
| R40-5 | 倍增窗口「`fit == W` 但 `W < 剩余`」误用 extent | §1.6 已用**两个独立分支**封死（`W == remaining` 才可能用 extent；`W < remaining` 只走 prefix + 倍增） |
| R40-7 | `:899` 简化改错 | T40-4；并在该行加注释「`lineText` 码点数 == `lineEndCp - lineStartCp`，因两端都是码点边界」 |
| R40-9 | 三个量具漏 override | C40-6 只查产品码；★ **量具 override 的机检**：`grep -c "override" ` 不可靠 ⇒ 用「批 1 后 542 全绿 + T38-15（`fitCalls == 3`）仍绿」作判据（它会红就是答案） |
| R40-11 | 表被 `BuildTextLayout` 发布 | C40-8 两条命令；`BuildTextLayout` 内**不出现** `EnsureCpByteOffsets`（`grep -c` 该函数体应为 0） |
| R40-12 | T1 跨 TU 耦合静默失效 | §1.2a 注释 + T40-1 的 `"A\xC0"` 必测（已在 §4 标注） |
| **R40-4** | FT 5 参**仍整串 `DecodeUtf8`**（性能不达标但行为正确 ⇒ T40-2 抓不到） | **人工核对**：落码后 `sed -n '/^TextFit FontEngine::FitText/,/^}/p'` 看**第 2 个**定义体内是否有 `DecodeUtf8`（机检无法区分「没写」与「写了没删」，见 §5 诚实边界） |
| **R40-13** ★★ | **`cps > count` 导致表索引越界**（v1.1 新增 = D-9） | 四处消费站点**统一走** `ByteOffsetAt` / `OffsetAt`（钳制）；**C40-9** 保证零裸下标；**T40-1/T40-3 必含** `"\xE0\x41\x41"` / `"\xF0\x41\x41\x41"` |

---

## §8 影响面（复核初设 §6 + 订正）

| 项 | 初设 | 本稿（复核 / 订正） |
|---|---|---|
| 源码（框架） | 8 个文件 | **9 个**（+ `ECDI/include/ECDI/Widget/TextBox.h`，D-1） |
| 源码（测试） | `WidgetTests.cpp` / `TextBoxTests.cpp` | 同（3 量具 override + `ByteMapProbe` + `TableFor` + 8 用例） |
| ★ 既有用例被改 | 3 量具各加 5 参 override | **同，且断言文本一条不改**（M1） |
| 公共头 | 100 → 100 | **100 → 100**（实测 `git ls-files 'ECDI/include/*.h'` = **100**） |
| public API | +1 | **+1**（5 参 `FitText`） |
| protected 面 | +4 | **+4 项**（`ByteMapKey` / `CurrentByteMapKey` / `EnsureCpByteOffsets` / `ByteOffsetAt`）+ **4 个 `mutable` 成员**（`m_cpByteOffsets` / `m_cpByteValid` / **`m_cpByteTextSize`（v1.1 新增，D-10）** / `m_cpByteKey`）+ `TextBox.h` override 声明 1 处（不新增，是 override） |
| CMake | 0 | **0**（无新增源文件） |
| 用例 | 538 → 546 | **538 → 546**（+8；批 1 加 4、批 2 加 4） |
| docs | 203 → 204 | **203 → 204**（实测 `git ls-files 'docs/*.md'` = **201**；磁盘上 Phase 40 已有 2 份 ⇒ 本稿为第 3 份 ⇒ 204） |
| 机检 | +8 | **+9**（C40-1..9；C40-8 两条命令、C40-9 钳制单源为 v1.1 新增） |
| 编译批次 | 一批 | **两批**（批 1 三个头 = 一次近全量；批 2 只 .cpp = 局部） |

---

## §9 需求 **v1.2** 回写清单（本稿落盘后执行）

| # | 需求 v1.1 原文 | v1.2 订正 | 依据 |
|---|---|---|---|
| 1 | §3 R1：「`totalCp` 取 `cps.size()`」 | **`byteOffsets.size() - 1`** | 初设 M3 / 本稿 D-6（脏字节上 `cps.size()` 与 `SequenceLength` 计数不等） |
| 2 | §3 R1：「与既有的 `cps` **同趟产出**」 | **两趟**（`DecodeUtf8` 产 `cps` + `BuildByteOffsets` 产表） | D40-J：两套解码语义不能混在一趟 |
| 3 | §3 R4（`Utf8Decode.h:20` 的 3× reserve 顺手修） | **de-scope**（本 Phase 不动该头） | D40-M（用户已授权）：`D40-E` 删掉 FT 的 L 次 `DecodeUtf8` 后剩余调用点无放大 |
| 4 | §6 源码影响面：「`Utf8Decode.h`（`reserve` 修正）· `Core/UTF8.cpp`（偏移表构建）」 | **两者零改动**；表构建落 `src/Widget/TextWidget.cpp` 匿名 namespace | R4 de-scope + D40-L（表设施须与 `Ensure` 同 TU） |
| 5 | §6 源码影响面：未列 `Widget/TextWidget.h` / `Widget/TextBox.h` | **两处都要改**（表设施 / override 声明） | D-1 + §1.1 |
| 6 | §5 A40-5：「目标 = 0」 | **同，且已实采基线 2/2/6/5** | §5 C40-1..4 |
| 7 | §7-1：「单趟扫 `text` 同时填 `cps` 与 `byteOffsets`」 | **两趟**；表的索引体系 = `SequenceLength`（非 `DecodeUtf8`） | D40-J |
| 8 | §7-3：「失效键 = `LineKeyMatches` 12 项 ∪ wrap 指纹六元组」+「`dpi` 算式基类只算一次」 | **5 维**（revision/echo/composing3）+ **表不需要 dpi** | D40-C：`byteOffsets` 是纯 UTF-8 结构量；「dpi 同源」问题对表**不适用** |
| 9 | §7-1：「`DecodedText` 视图」 | **不引入新类型**；5 参 = `cps` + `byteOffsets` 两个独立参数 | D40-K |
| 10 | §6 用例：「~546」 | **546**（+8，确定） | §4 |
| 11 | §6 docs：「201 → 202；初设/详设各 +1」 | **202 → 203 → 204**（实测 tracked 201 + 三份稿） | §8 |

★ 回写方式：**新增 v1.2 修订条目**（不改 v1.0/v1.1 原文），逐条注明「初设 D40-x / 详设 D-y」。这是沿 P39 的「订正先例」——评审基线保留，订正可追溯。

---

## §10 修订记录

- **v1.0（2026-10-29）** 详设落盘。锚点 **48** 处亲核（§0）；**8 条对初设的订正**（§0.1：`TextBox.h` 漏项、T40-1 静态接缝作废、T40-8 与 debug 断言冲突、GDI 耗尽判据钉死、批 0 合并、`totalCp` 口径、GDI 可无头、include 三件）；接口面 8 小节逐字签名（§1）；**30 处**逐行改动 + 批归属（§2）；测试侧三量具 override + `ByteMapProbe` + `TableFor`（§3）；8 条用例（§4，538 → 546）；机检 8 条**全部实跑**（§5，读数 2/2/6/5 · 0 · 各 1 · 0 · 0/1）；两批 + 回归门 + 负对照 + 探针源码（§6）；风险实现期动作（§7）；影响面复核（§8，改动文件 8 → 9）；需求 v1.2 的 11 条回写（§9）。

- **v1.1（2026-10-29，收 Qoder 审核）** —— **3 must-fix + 2 should-fix + 2 nits 全部落实**。★ 两条实义缺陷均**用本机探针实证**（`g++ -std=c++20 -I ECDI/include -I ECDI/src probe.cpp ECDI/src/Core/UTF8.cpp`），不是推理接受：

| # | 缺陷 | 实证 | 落改 |
|---|---|---|---|
| **D-9**（must） | §1.2c 的越界论证 `cps.size() ≤ count` **不成立** | 探针：`"\xE0\x41\x41"` → `cps=2 / count=1 / table=2`；`"\xF0\x41\x41\x41"` → `cps=3 / count=1 / table=2` ⇒ **`cps > count`** | `ByteOffsetAt` 改**钳制**（新增匿名 `OffsetAt` 作单源）；`emitLine` 同用；**钳制 = 逐位等价**（探针 8/8 样本：`CI2BO(count+1) = CI2BO(count+3) = offsets[count]`）；四处消费站点统一闭合；**新增 C40-9 + R40-13**；T40-1/T40-3 必补两类样本 |
| **D-10**（must） | §1.2b 自检 `back() == text.size()` **对合法截断串假阳** | 探针：`"A\xC0"` → 表 `[0,1,3]`、`back()=3 > size()=2`（§1.1 自己已注明这点 ⇒ **同文档自相矛盾**） | 新增 `mutable std::size_t m_cpByteTextSize`，建表时记 `text.size()`、命中比它。★ 危害等级：debug 中止 / release **每帧重建**（静默废掉全部收益）——比 v1.0 D-3 删掉的 T40-8 子断言**更容易踩**（那是主动制造非法状态，这是**合法输入**） |
| **D-11**（must） | §0 / §2 #4 锚点 `:194` | 实测 `:194` = `for` 闭合、`:195` = `return hardEnd;`、`:196` = 函数闭合 | 全部改 **`:196`**（48 处锚点里唯一一处错） |
| **D-12**（should） | §6.5 探针**照贴编不过**（缺 `#include "ECDI/Core/UTF8.h"`）+ 只有「表版」+ 未落地 churn | ★ **本稿已真实编译并运行**（`g++ -std=c++20 -O2`，读数见 §6.5，非「声称可编」） | §6.5 重写为**可独立编译的完整文件**（补 `"ECDI/Core/UTF8.h"` 与 `<algorithm>`；两支同体：`LoopCurrent` vs `TableVersion`；四档 `n`；`allocs`/`bytes` 列；GDI 拷贝量模拟——实测 312×）；§6.3 补**判据分工表**（哪些走探针、哪些走机检、为什么 paint 侧不做计时探针） |
| **D-13**（should） | T40-2「追加 4 组」与 `kFitGolden` **重复** | `TextBoxTests.cpp:147-156` 已含 `startCp≠0` / overlong / 越界 / 空串 | 改为「`kFitGolden` 8 组**已全覆盖**那四类，真正要追加的只有 D-9 的两类脏字节」 |
| **nit 6** | §3.1 的 (b)/(c) 共用块只写 `CountingFakeMeasurer::` | 两类无继承关系，照抄进 (c) 编译不过 | (b)/(c) **拆成两块**，各写自己的类名 |
| **nit 7** | §0 写局部表「插在 `:211` 后」，§1.2c 写 `:216` 后 | — | §0 改 **`:216` 后**（必须先走 `cps.empty()` 早退） |

★ **不变项**（明确声明，防止误读为「改了很多」）：**架构不动**（仍是 D40-B ⓑ / 单写者 / 5 维键）、**红线不动**（三条）、**批次划分不动**（批 1 / 批 2）、**用例数不动**（538 → **546**）、需求 v1.2 的 11 条回写不动。变的只有：钳制规则、自检基准、一处锚点、探针源码、两处样本清单、两条机检/风险。
★ **新增诚实边界（§5）**：明确写出「**哪些东西刻意不设机检**」——FT 5 参是否仍整串解码（T40-2 抓不到，只能人工核对 = R40-4）、量具 override 是否漏（靠 T38-15 红来暴露）。不装作判据全知全能。

- **v1.2（2026-10-29）批 1 已落码** —— §2 #1–#21 全部落盘，**13 文件 / 655 insertions / 0 deletions**。
  - ★ **红线③结构性成立**：`TextMeasurer.h` 是**纯 64 行新增**，4 参 `FitText` 与其默认体一字未动（`git diff` 零删除行）。
  - ★ **「零行为变化」有直接证据**：C40-1..4 实测仍 **2 / 2 / 6 / 5**，与改前**完全相同**（批 1 不碰任何消费站点）。
  - ★ **新增工序「语法预检」**（§6.1.2）：用 MinGW g++ `-fsyntax-only` 对 7 个可独立解析的 TU 全部跑过（含 `third_party/freetype/include`），**抓到并修掉一处真错**——`ByteMapProbe` 忘记 `using TestableLabel::TestableLabel;`（否则 `ByteMapProbe probe("")` 编译不过，用户要等 ~35 min 才发现）。
  - ★ **两处实施期订正**（§6.1.3）：**D-13** C40-9(b) 判据 `byteOffsets\[` 恒 0（形参叫 `offsets`）⇒ 改 `offsets\[(std::min)`；**D-14** `ByteMapProbe` 补继承构造。

- **v1.3（2026-10-29）批 2 已落码** —— §2 #22–#30 全部落盘，累计 **13 文件 / 883 insertions / 21 deletions**。
  - ★ **21 行删除全部是 §2 要替换的旧换算调用 + 一行过时注释**，逐行核对无夹带。
  - ★ **C40-1..4 实测归零**（0/0/0/0）；C40-5=1、C40-8(a)=1/(b)=0、C40-9(a)=0/(b)=1。
  - ★★ **一处对详设自身的实义订正（I-1）**：§2 #26 的 `lineCpCount = lineEndCp - lineStartCp` **不逐位等价**，必须**先钳制再做差**。反例 `"\xE0\x41\x41"` 的第 2 行：`startByte == endByte == 3` ⇒ `lineText = ""`，旧式得 0，未钳制的差值得 1。已在落码时按钳制式实现，本条为**详设缺陷的追认**（不是实现偏差）。
  - ★★ **一处对机检口径的订正（I-2）**：C40-1..4 的命令必须 `grep -v '^[[:space:]]*//'`——批 2 给消费站点加的说明性注释里提到旧函数名，会让判据**因为「把改动写好」而变红**。已用 HEAD 版本复核：新口径下基线仍是 2/2/6/5。
  - ★ **T40-4 的判据边界如实登记**：无窗口下只能验行首（测量循环被 `GetWindow()` 守卫短路），「行内 X 落点」不在本 Phase 的判据内。
  - ★ `kWrapBarSlot` 上移（T40-3/4/6 位于 Phase 38 夹具区之前）；7 个 TU 语法预检全绿（本轮抓到 `kWrapBarSlot` 未声明）。
  - ★★ **首轮 `545 passed / 1 failed`**：红的 `Text.Paint.WrapBytesBitIdentical` 是**判据自身缺陷**（**I-3**，§6.2.3）——期望侧用「码点区间非空」判空，产品用「切出字节非空」⇒ `"\xE0\x41\x41"` 的两行里第 2 行切出 0 字节，期望多塞一个空串。改为 `piece.empty()` 镜像产品判空。★ **产品码零改动**。
  - ★ **旁证（首轮 545 绿的价值）**：**T40-3**（脏字节 × 5 档折行宽）与 **T40-7**（GDI 倍增窗口跨 64 码点边界）首轮即绿 ⇒ D-9 的钳制真的封住了那条 3 字节可达的越界路径，D40-D 的窗口化逐位一致。

- **v1.4（2026-10-29）全链收口** —— 四构建链 **546 / 546 全绿**（用户侧实测）。
  - ★★ **R40-3 负对照已完成（判据有牙的唯一证明）**：把 `BuildByteOffsets` 的步进临时换成 `DecodeUtf8` 语义（非法首字节跳过 / 续字节非法只跳 1 字节 / 截断即停），只改该函数的循环体 ⇒ **`544 passed / 2 failed / 546`**：
    - 红的 `Text.CpByteTable.MatchesSequenceLength`（T40-1）——**正是预测项**（`"\x80\x8A"` / `"A\xC0"` / `"\xE0\x41\x41"` 三样本的表项与 `CodepointIndexToByteOffset` 不再相等）；
    - **额外**红的 `Text.Paint.WrapBytesBitIdentical`（T40-6）——★ 这比预测更硬：表错了 ⇒ 每行切出的**字节**跟着错 ⇒ 绘制命令流被独立捕获。说明 T40-6 不只是「查表没写错」的自证，它对**表的正确性**也有牙。
    - `Text.Wrap.LineTableBitIdentical`（T40-3）**保持绿**——符合设计：它测的是**结构自洽**（覆盖全串 / 无缝 / 不重叠），不测表与换算的等价性（那是 T40-1 的职责）。负对照下两种语义各自都自洽。
    - 还原后 **546 全绿**（`git diff` 确认 `TextWidget.cpp` 回到 604 行、`R40-3 负对照` 标记 0 处、`DecodeCodepointAt` 仍 1 处）。
  - ★ **新的验证能力（本 Phase 首次）**：本机 `cmake`（`D:/Cmake/cmake-4.4.2-windows-x86_64`）+ `ninja`（`D:/Environment/Ninja`）均在 PATH，`cmake --build cmake-build-debug-mingw --target ecdi_tests` 可**增量构建**（改 3 个文件约 30 s），`./cmake-build-debug-mingw/ecdi_tests.exe` 可**直接跑**（`546 passed, 0 failed`，不阻塞、不弹窗）。⇒ 负对照、探针、回归此前必须由用户代跑，现在**可自证**。★ 边界照旧：AI 仍**不代用户做四构建链的最终验收**（MSVC / Clang / ClangCL 仍由用户侧跑），MinGW 链只作为**过程自检**。
  - ★ **账本迁移**：`docs/dev/roadmap-deferred.md` → **v2.62**，#83 行由「🔲 待做」迁入「✅ 已完成」（改状态文字 + 移区**成对**），待做区剩 #69–#72 / #79–#82；顺位说明与本条头部各加一段。
  - ★ **需求稿** → **v1.4**：新增 §5.1 验收实测（A40-1..7 逐条结论），§8 登记三条判据边界（A40-6 面收窄 / A40-2·3 是算法层 / A40-5 排除注释）。
  - ★ **BOM 纪律的一处澄清**：全仓 BOM **不统一** —— `TextMeasurer.h` / `GDITextMeasurer.{h,cpp}` / `FontEngine.{h,cpp}` / `WidgetTests.cpp` 原本就**无 BOM**，本批**未擅自添加**（否则产生整文件 diff）。「新文件必须带 BOM」的纪律不等于「给旧文件补 BOM」。
  - **未做（诚实声明）**：未链接、未跑测试、未跑负对照（R40-3）——`-fsyntax-only` 只扛「编译不过」，扛不了「行为不对」；542 全绿仍需用户环境首次构建。
