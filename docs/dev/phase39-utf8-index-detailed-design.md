# Phase 39 · 详细设计 —— UTF-8 码点索引换算的 O(N²) 消除（deferred #78）

> 状态：**v1.3**（2026-10-29 **批 0 + 批 1 均已通过（538/538）＋ R39-3 负对照已验**，待批 2 收口）
> **上游**：需求 `phase39-utf8-index-requirements.md` **v1.1** · 初设 `phase39-utf8-index-preliminary-design.md` **v1.1**（本稿 §9 逐条承接其 D39-A..H / T39-1..7 / C39-1..6 / R39-1..10）
> **代码基线**：HEAD `9c158f2`（需求 / 初设提交后**只有 `docs/` 变动**）⇒ `Core/UTF8.{h,cpp}` / `Widget/TextBox.cpp` / `Render/TextMeasurer.h` / `Tests/TextBoxTests.cpp` 与 `2bea0f7` **逐字节相同**，行号沿用且无漂移
> ★ **事实纪律**：本稿每条行号 = 2026-10-29 `grep -n` / `sed` 亲核；§5 的机检命令**全部实跑过**并把读数写进正文
> ★ **数字纪律**：只承诺①复杂度 ②分配次数 ③改前/改后记录制耗时；**不写「提升 X 倍」**

---

## 红线（两条，任何批次都不得破）

| # | 红线 | 判据 |
|---|---|---|
| **①** | **非折行支行表逐位不变** | 表 B 十三行 + T39-1..7（A1/A6）；不满足就是**行为变更**，不是优化 |
| **②** | **R2 只做纯等价变换** | `MeasureText` 的**入参、次数、顺序**逐次相同（§2.3 逐行）；表 A 八组 golden |

★ 两条红线都不涉及折行支（`:459-474`）—— 本 Phase **一行都不碰**。

---

## §0 锚点复核（今日亲核；实现期以本表为准）

| 文件 | 锚点 | 内容 | 本稿用法 |
|---|---|---|---|
| `ECDI/include/ECDI/Core/UTF8.h` | `:26-30` | `DecodeFirstCodepoint` 的注释块 + 声明 | 注释**纯插入**一行；声明**不动**（C39-5） |
| 同上 | `:32` | 匿名 namespace 结束的 `}`（`namespace ECDI{` 的闭合） | 新声明插在 `:30` 之后、`:32` 之前 |
| `ECDI/src/Core/UTF8.cpp` | `:9-15` | `SequenceLength`（匿名 namespace，**非法前导按 1**） | 新函数**复用**它（单源） |
| 同上 | `:35-43` / `:45-53` | 两个换算函数 | **零改动**（N3） |
| 同上 | `:55-80` | `DecodeFirstCodepoint`（`:56-57` 空串 → 0；`:59-79` 三分支 + 回退） | **整体替换**为新函数 + 转发（§2.2） |
| `ECDI/include/ECDI/Render/TextMeasurer.h` | `:77` | `totalCp = ByteOffsetToCodepointIndex(...)` | 不动 |
| 同上 | `:85-88` | 首码点切片（**同一表达式两次** + 前置判定） | 替换（§2.3） |
| 同上 | `:100` / `:113` | 两处 `byteStart = CodepointIndexToByteOffset(text, startCp)` | **删除**（复用外提值） |
| 同上 | `:101` / `:114` | `byteEnd`（依赖 `mid` / `lo`） | **保留** |
| `ECDI/src/Widget/TextBox.cpp` | `:459-474` | 折行支（含 `m_scrollOffsetX = 0.0f` 第三写点） | **零改动** |
| 同上 | `:475` / `:491` | `else{` 与它的闭合 `}` | **保留**（只换中间 476-490） |
| 同上 | `:479` / `:481-489` / `:490` | `count` / 旧 `for` 整块 / 旧末段 push | **删除并替换**（§2.4） |
| 同上 | `:493-496` / `:498-499` | 空表兜底 / 存键 | **零改动** |
| 同上 | `:1345` | Down 键「最后一行 ⇒ no-op」守卫 | T39-5 辅助断言的依据 |
| `ECDI/include/ECDI/Widget/TextBox.h` | `:128` / `:129` | `MoveCaretToEnd()` / `GetCaret()`（**public**） | T39-4/T39-7 的外部口径（经 `TextBox.cpp:431` 等于 `GetCodepointCount()`，见 A-4） |
| 同上 | `:322` / `:348` | `private:` 区段起点 / `GetCodepointCount()` 声明 | ★ 证明它**私有**、`using` 暴露不了（A-4 的根因） |
| `ECDI/src/Tests/TextBoxTests.cpp` | `:32-43` | `FakeTextMeasurer`（8 宽 / 16 行高，**不覆写 `FitText`**） | 表 A 的测量器 |
| 同上 | `:45-108` | `TestableTextBox`（`:72-74` `RowCountForTest`、`:77`/`:80` `OwnForTest`/`DrawForTest`、`:96` `ResolveMeasurer`） | 新增 `LineCountForTest()` |
| 同上 | `:3017` / `:3075` | `RegisterTextBoxTests()` 起始 / 末条 `Add` + `}` | 七条登记插在 `:3075` 之前 |
| `ECDI/src/Tests/TestFramework.cpp` | `:169-171` | 汇总行 `Tests: N  Passed: P  Failed: F` | C39-6 的读数口径 |

### 0.1 与初设的差异（本稿确认或订正，共四条）

| # | 初设 | 本稿 | 理由 |
|---|---|---|---|
| **A-1** | 表 B「13 行 = A6 十一类（overlong 展开 3 形态）」 | **不变，仍 13 行**；`C0 4A`（overlong + **非法续字节**）**不进表 B**，只在 T39-3 里 inline 构造 | `C0 4A` 的 `0x4A` 不是 `10xxxxxx`，它属于「overlong × 非法续字节」的**交叉变体**，不属于 A6 的十一类任何一类。塞进表 B 会让「十一类」这个口径失真 |
| **A-2** | 初设 T39-5 的「Down 键 → `GetCaret()` 0→1」是**辅助**断言 | **保留**，并补上它成立的实码依据：`TextBox.cpp:1345` 的 `curLine + 1 >= m_lines.size()` 守卫 ⇒ 只有 1 行时 Down 是 no-op（caret 留在 0），2 行时才走到 `:1349` 的 `CaretIndexFromLineX(1, ·)`，而空行的 `lineText.empty()` 分支（`:896`）直接返回 `lineStartCp` = 1。★ **v1.1**：「1 行 ⇒ no-op」不再是「只是依据」——它升级为 T39-5 的**断言 ③**（单行箱 `"abc"`，`Down` 后 `GetCaret() == 0`），与 ② 成对，守卫的两侧都被钉住 | 没有这条依据，「0→1」只是猜测；只断 ② 则「永远返回 1」的错误实现也能过（守卫失效无覆盖） |
| **A-3** | 初设 §3 PD39-3 只写「三处」，未给 `byteStart` 的**确切位置** | 明确插在**早退 `startCp >= totalCp` 之后**（`:81` 之后），否则空串路径会多算一次 O(startCp) | 逻辑上等价但白烧一次换算；且放在早退前会让 `startCp` 超界时做无用功 |
| **A-4** | 初设 T39-4/T39-7 写「末行 `ownedEnd == GetCodepointCount()`」 | ★ **改用 `MoveCaretToEnd(); GetCaret()`** 作为外部口径 | `GetCodepointCount()` 是 **private**（`TextBox.h:322` 的 `private:` 早于 `:348` 的声明），而**私有成员不能用 `using` 暴露**（访问检查发生在 using 声明处，编译期直接报错）。公共通道：`MoveCaretToEnd()`（`:128`）的实现就是 `m_caret = GetCodepointCount()`（`TextBox.cpp:431`），`GetCaret()`（`:129`）把它读出来 ⇒ **语义完全等价**，且沿用了真实的私有实现（比在测试里重算更可信）。★ 无头可跑：既有用例 `TextBoxTests.cpp:1172` 已这么用 |

---

## §1 定案的接口面（逐字签名）

### 1.1 `ECDI/include/ECDI/Core/UTF8.h`（公共头 100 → 100；public API **+1**）

在 `:30` 的 `DecodeFirstCodepoint` 声明之后插入（`:31` 空行保留）：

```cpp
/// @brief 就地解码 `data` 处的一个码点，并返回它**声称**占用的字节数（★ Phase 39）
/// @details 与 `DecodeFirstCodepoint` **同语义**（同一 `SequenceLength`、同一三分支拼装），差别只有两点：
///          ① 不要求「以串尾为界」——`size` 由调用方给（单趟扫描给的是**剩余长度**）；
///          ② **同时给出步进量**——`TextBox::RecalculateLines` 的非折行支既要解码值（判 `\n`）
///             又要步进量，只给解码值会逼调用方自己判前导字节（`SequenceLength` 立刻双源）。
///          ★ 返回值**可能 > `size`**：末段截断的序列仍按前导声称的长度步进——这是
///             `CodepointIndexToByteOffset`（`:35-43`）的既有行为，单趟扫描必须逐位复刻。
///          ★ **`codepoint` 任何情况下都会被写**（`size == 0` 写 0）——调用方不必预置初值。
/// @param data  首个字节（`size == 0` 时**不读**）
/// @param size  剩余可读字节数
/// @param codepoint [out] 解码结果；**剩余不足一个完整序列 ⇒ 回退为 `lead`**
/// @return 前导声称的序列长度（1–4）；非法前导 = 1（与 `SequenceLength` 一致）
[[nodiscard]] size_t DecodeCodepointAt(const char* data, size_t size, char32_t& codepoint);
```

★ **为什么 out 参数而不是 `std::pair` / 小结构体**（初设已论证，此处只留结论）：`pair` 的 `first`/`second` 不带语义；小结构体要再占一个公共类型名。out 参数只多 **1 个符号**。

### 1.2 `ECDI/src/Core/UTF8.cpp`（`:55-80` 整体替换）

```cpp
size_t DecodeCodepointAt(const char* data, size_t size, char32_t& codepoint){
	if (size == 0){
		codepoint = 0;                                  // ★ 无条件写（公共 API 不留脚枪）
		return 0;
	}
	const unsigned char lead = static_cast<unsigned char>(data[0]);
	if ((lead & 0x80) == 0){
		codepoint = static_cast<char32_t>(lead);        // 1 字节 ASCII
		return 1;
	}
	const size_t len = SequenceLength(lead);            // ★ 与换算函数同一份规则（单源）
	if (len == 2 && size >= 2){
		codepoint = static_cast<char32_t>(((lead & 0x1F) << 6) |
			(static_cast<unsigned char>(data[1]) & 0x3F));
		return len;
	}
	if (len == 3 && size >= 3){
		codepoint = static_cast<char32_t>(((lead & 0x0F) << 12) |
			((static_cast<unsigned char>(data[1]) & 0x3F) << 6) |
			(static_cast<unsigned char>(data[2]) & 0x3F));
		return len;
	}
	if (len == 4 && size >= 4){
		codepoint = static_cast<char32_t>(((lead & 0x07) << 18) |
			((static_cast<unsigned char>(data[1]) & 0x3F) << 12) |
			((static_cast<unsigned char>(data[2]) & 0x3F) << 6) |
			(static_cast<unsigned char>(data[3]) & 0x3F));
		return len;
	}
	// 截断 / 非法前导 / 连续字节：按 1 字节 ASCII 处理（与 SequenceLength 同语义）
	codepoint = static_cast<char32_t>(lead);
	return len;                                         // ★ 步进仍按声称长（可能 > size）
}

char32_t DecodeFirstCodepoint(const std::string& text){
	char32_t codepoint = 0;
	(void)DecodeCodepointAt(text.data(), text.size(), codepoint);   // size == 0 ⇒ 写成 0
	return codepoint;
}
```

★ 转发版**逐位等价**于改前的 `:55-80`：空串 → 0（`:56-57`）、ASCII（`:60-61`）、三分支（`:63-77`）、回退 `lead`（`:79`）——差别只有「不再构造 `std::string`」这一层，而它本来就不构造（形参是引用常量）。
★ `(void)` 前缀是**必须**的：`[[nodiscard]]` 下丢弃返回值会触发 `-Wunused-result`（MSVC `/W4` 的 C4834）。

### 1.3 `ECDI/include/ECDI/Render/TextMeasurer.h`（R2：`:83-87` 替换 + `:100` / `:113` 删除；`:88` 的 `if` 逐字不动）

**改前（逐行原样，`:83-87`——`:88` 的 `if` 是锚点，不在替换范围）**

```cpp
			// ★ 「至少能放一个」是二分的前置不变式：若连**首码点**都放不下 ⇒ 返回 0
			//   （否则二分区间无解）。单码点宽 = 该码点**单独**测一次——O(1) 次 MeasureText。
			const std::size_t firstEnd = CodepointIndexToByteOffset(text, startCp + 1);
			const std::string firstCpText = text.substr(
				CodepointIndexToByteOffset(text, startCp), firstEnd - CodepointIndexToByteOffset(text, startCp));
```

**改后**

```cpp
			// ★ Phase 39 R2：`startCp` 的字节偏移在二分体内**恒定**（`startCp` 不变、`mid` 只影响终点）
			//   ⇒ 提到循环外算一次；收尾处（原 `:113`）同表达式复用。函数体内换算调用点 **7 → 4**（C39-4）。
			const std::size_t byteStart = CodepointIndexToByteOffset(text, startCp);
			// ★ 「至少能放一个」是二分的前置不变式：若连**首码点**都放不下 ⇒ 返回 0
			//   （否则二分区间无解）。单码点宽 = 该码点**单独**测一次——O(1) 次 MeasureText。
			//   `startCp + 1` 推不出来（省它需要「序列长」，而那是 `Core/UTF8.cpp` 匿名 namespace 的内部
			//   辅助）⇒ 这次换算保留。
			const std::size_t firstEnd = CodepointIndexToByteOffset(text, startCp + 1);
			const std::string firstCpText = text.substr(byteStart, firstEnd - byteStart);
```

**`while (lo < hi)` 体内**：删 `:100` 的 `const std::size_t byteStart = ...`，保留 `:101` 的 `byteEnd`。
**收尾**：删 `:113` 的 `const std::size_t byteStart = ...`，保留 `:114` 的 `byteEnd`、`:115` 的 `substr(byteStart, ...)`（此时 `byteStart` 指外提值）。

★ **等价性**：`CodepointIndexToByteOffset` 是纯函数（不读写状态、不看时间）⇒ 外提只改**求值次数**。`substr(byteStart, firstEnd - byteStart)` 的实参逐位相同 ⇒ `firstCpText` 相同 ⇒ 其后每次 `MeasureText` 的**入参 / 次数 / 顺序**完全不变（1 次首码点 → `log2(totalCp − startCp)` 次二分 → 1 次收尾）。
★ **行为不变的另一半**：`byteStart` 的值在二分体内恒定，故原 `:100` 的 `byteStart` 与 `:113` 的值**本来就相同**——这不是"看起来相同"，是可以逐位断言的（T39-1 的 `startCp != 0` 组）。

### 1.4 `ECDI/src/Widget/TextBox.cpp`（`else{}` 块：只换 `:476-490`）

```cpp
		// ── 非折行支：单趟推进（Phase 39 R1——`\n` 扫描与码点计数同一趟）──
		// 每码点 O(1)：就地解码 + 按前导**声称**的长度步进 ⇒ 整体 O(N)（原：每码点两次 O(i) 换算 + 一次 substr）。
		// 行表口径与改前**逐位相同**：`\n` 不属任何一行（行末 `ownedEnd` = 该 `\n` 的码点索引）。
		// ★ 命名：`textSize`（全串长）≠ `DecodeCodepointAt` 的 `size`（剩余长度）——不同义，故不共用名字。
		const size_t textSize = m_text.size();
		const char* const data = m_text.data();
		size_t byte  = 0;   // 当前码点首字节偏移（★ 末段截断时**可越过 textSize**——见 §1.5 引理 L1）
		size_t cp    = 0;   // 已消费码点数 == 当前码点索引
		size_t start = 0;   // 当前行首码点索引
		while (byte < textSize){
			char32_t ch = 0;
			const size_t len = DecodeCodepointAt(data + byte, textSize - byte, ch);
			byte += len;
			if (ch == U'\n'){
				m_lines.push_back(TextLine{ start, cp - start, 0.0f, cp });   // `\n` 不属任何一行
				start = cp + 1;
			}
			++cp;
		}
		m_lines.push_back(TextLine{ start, cp - start, 0.0f, cp });           // 末段（可为空 = 尾部空行）
```

★ **`data + byte` 只在 `byte < textSize` 时求值**（循环条件先判、体内不重算指针）⇒ 越过 `textSize` 后**不再做指针运算**（R39-1）。
★ 缩进：`else{}` 块内是 **2 个 tab**（与 `:479-490` 现状一致），代码块内再 +1。

---

## §2 逐文件逐行改动清单

| # | 文件 | 行 | 动作 | 净行数 |
|---|---|---|---|---|
| **2.1** | `Core/UTF8.h` | `:30` 后 | 插入声明 + 12 行注释 | **+13** |
| | | `:26-29` | 注释**纯插入**一行「★ Phase 39：…」 | **+1** |
| **2.2** | `Core/UTF8.cpp` | `:55-80`（**26** 行） | 替换为 §1.2 代码块（**39** 行：`DecodeCodepointAt` 33 + 空行 1 + 转发版 5） | **+13** |
| **2.3** | `Render/TextMeasurer.h` | `:83-87`（**5** 行） | 替换为 §1.3 代码块（**9** 行） | **+4** |
| | | `:100` / `:113` | 删两行 `byteStart` | **−2** |
| | | | **小计** | **+2** |
| **2.4** | `Widget/TextBox.cpp` | `:476-490`（**15** 行） | 替换为 §1.4 代码块（**20** 行） | **+5** |
| **2.5** | `Tests/TextBoxTests.cpp` | `:80` 后 | `LineCountForTest()` + `ExpectLines` + 两表 + 七函数 | **+~200** |
| | | `:3075` 前 | 七条 `GetTestRegistry().Add` | **+7** |

★ **行数账的数法（v1.1 订正，此前三处手数错）**：旧行数 = 被替换区间的闭区间长度（`awk 'NR>=55 && NR<=80' | wc -l`）；新行数 = §1.x 代码块**围栏之间**的行数（含块内空行、不含围栏）；净 = 新 − 旧。**三个新块实测 39 / 9 / 20**，批 2 收口时按 `git diff --stat` 回填实数，若与本节不符即记 §9。
★ **§8 的总账必须与本节逐项相等**（`UTF8.h +14` · `UTF8.cpp +13` · `TextMeasurer.h +2` · `TextBox.cpp +5`）。
★ **不动清单（逐项确认）**：折行支 `:459-474`（含 `m_scrollOffsetX = 0.0f` 第三写点）· `LineRange` `:568-573` · `LineDrawRange` `:575-581` · `BuildDisplayText` `:236-259` · 空表兜底 `:493-496` · 存键 `:498-499` · `Core/UTF8` 的两个换算函数 · `DecodeUtf8`（`ECDI/src/Render/Utf8Decode.h`）· 任何 setter · CMake。

### 2.5 测试侧新增（`TextBoxTests.cpp`，加在 `TestableTextBox` **类内**：`:80` 的 `DrawForTest` 之后、`:99` 的 `};` 之前）

```cpp
    /// @brief ★ 行表行数（Phase 39 唯一可用通道）
    /// @details 为什么**不能**用 `RowCountForTest()`：那个是从 preferred 高反推的，而
    ///          `TextBox::GetPreferredSize`（TextBox.cpp:201）只在「含字面 `\n` 或折行生效」时
    ///          才走 `MultilinePreferred()`（读 `m_lines.size()`）。本 Phase 的 overlong 样本
    ///          （`C0 8A` 等）**不含字面 0x0A 字节** ⇒ 非折行态读回 **1**、真值 **2**
    ///          ⇒ 五条用例假红；若把 golden 迁就成 1，则 R39-3 的负对照**永远不红**。
    /// @details 原理：Center 态 `GetTextTopInset()`（protected，`:264`）在
    ///          `TextBox.cpp:611` **直接读 `m_lines.size()`**、且自带 `RecalculateLines()`；
    ///          无头行高兜底 16（`:589`）⇒ `n = (areaH − 2·(inset − pad)) / 16`。
    ///          ★ 前置：`n × 16 <= areaH`（内容高 > 文本区高时 `max(0, ·)` 会把 offset 夹到 0、
    ///          行数信息消失）⇒ 调用方必须给足高度（本组样本统一高 200）。
    [[nodiscard]] size_t LineCountForTest(){
        TextBoxStyleOverride c; c.verticalAlign = VerticalAlignment::Center;
        SetStyle(c);                                   // Top 分支会早退、不碰行表
        const float inset = GetTextTopInset();          // 内含 RecalculateLines()
        const float areaH = static_cast<float>(GetHeight()) - Padding() * 2.0f;
        TextBoxStyleOverride t; t.verticalAlign = VerticalAlignment::Top;
        SetStyle(t);                                   // ★ 还原：不污染同用例后续的绘制断言
        return static_cast<size_t>((areaH - 2.0f * (inset - Padding())) / 16.0f + 0.5f);
    }
```

★ 三个数（16 / 200 / 0）都是精确二进制浮点 ⇒ 反解无舍入误差，`+ 0.5` 只是把整数落回整数。
★ **必须还原 Top**：`OnPaint` 的 `textY` 依赖 `GetTextTopInset()`，同用例里若还要断言绘制坐标，Center 会让它整体下移。

---

## §3 批 0：两张 golden 表（逐字可粘）

★ **预置值可机检**：`python tools/phase39_gold_probe.py` —— 22 组（表 A 8 + 表 B 13 + `C0 4A` 1）。脚本逐字复刻 `UTF8.cpp` 的四个辅助 / `TextBox.cpp:479-490` / `FitText` 默认体 / `FakeTextMeasurer`。
★ **它自己做过负对照**：篡改任一组 → `FAIL` 行 + **exit 1**；恢复 → `exit 0`。所以「22/22 ok」不是一条恒真的判据。
★ **实测结论（2026-10-29）**：四构建链 538/538 ⇒ 预置值与 HEAD 真实行为**一致**，**未发生回填**。

### 3.1 表 A · `kFitGolden`（A5 / T39-1；`FakeTextMeasurer` 8 宽 / 16 行高）

```cpp
/// @brief 表 A（Phase 39 批 0 采集）：默认 `FitText` 的等价性基线（A5）
/// @details ★ #5 组 `startCp != 0` 是**唯一能测到 R2 的组**：`startCp == 0` 时被外提的
///          `byteStart` 恒为 0，算一次与算两次结果相同 ⇒ 只用 `startCp == 0` 的样本，
///          R2 的等价性测不到任何东西（同型假绿，见 R39-3）。
/// @note 期望值 = 2026-10-29 手算（沿初设 §1.5）。★ **批 0 实跑若不符，以实测为准并回填**——
///       手算只是首次运行的对齐校验，不是真理（C38-5 教训）。
struct FitGold{
	const char* text;
	std::size_t startCp;
	float       maxWidth;
	std::size_t fitCp;
	float       width;
};

static const FitGold kFitGolden[] = {
	{ "abcdef",   0, 100.0f, 6, 48.0f },   // 全部放得下
	{ "abcdef",   2,  24.9f, 3, 24.0f },   // 非整档宽（防「整除」巧合）
	{ "abcdef",   2,   7.9f, 0,  0.0f },   // 首码点都放不下
	{ "一丁丂",    0,  16.0f, 2, 16.0f },   // 多字节 + 恰好边界
	{ "一丁丂",    1, 100.0f, 2, 16.0f },   // ★ 关键组：startCp != 0
	{ "abcdef",   6, 100.0f, 0,  0.0f },   // startCp >= totalCp ⇒ 早退
	{ "",         0, 100.0f, 0,  0.0f },   // 空串
	{ "\xC0\x8A", 0, 100.0f, 1,  8.0f },   // 脏字节（totalCp == 1）
};
```

### 3.2 表 B · `kLineGolden`（A1 / A6 / T39-2；非折行支行表的**改前**快照）

```cpp
/// @brief 表 B（Phase 39 批 0 采集）：非折行支行表的改前快照
/// @details `lineCount` 字段 = **`m_lines.size()`**（经 `LineCountForTest()` 读），
///          **不是** `RowCountForTest()`——后者在 overlong 三行上读成 1（P0）。
///          `rows[3]` 用完的行留 `{0,0,0,0}`；本表最大行数 = 3（`"\n\n"`）。
struct LineGoldRow{ std::size_t ownBeg, ownEnd, drawBeg, drawEnd; };
struct LineGold{
	const char*  text;
	std::size_t  lineCount;
	LineGoldRow  rows[3];
};

static const LineGold kLineGolden[] = {
	// 类别                      行数  逐行 (ownBeg, ownEnd, drawBeg, drawEnd)
	{ "",                 1, { { 0, 0, 0, 0 } } },                                    // 空串
	{ "\n",               2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                     // 仅 \n
	{ "\n\n",             3, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 } } },     // 连续 \n
	{ "ab\n",             2, { { 0, 2, 0, 2 }, { 3, 3, 3, 3 } } },                     // 尾 \n（尾部空行）
	{ "a\r\nb",           2, { { 0, 2, 0, 2 }, { 3, 4, 3, 4 } } },                     // CRLF（\r 不断行）
	{ "\x80\x8A",         1, { { 0, 2, 0, 2 } } },                                     // 非法首字节
	{ "a\xC0",            1, { { 0, 2, 0, 2 } } },                                     // 截断续字节
	{ "\xC0\x8A",         2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                     // overlong U+000A（2 字节）
	{ "\xE0\x80\x8A",     2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                     // overlong U+000A（3 字节）
	{ "\xF0\x80\x80\x8A", 2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                     // overlong U+000A（4 字节）
	{ "\xF0\x9F\x98\x80 a", 1, { { 0, 3, 0, 3 } } },                                   // 4 字节 emoji
	{ "abcdefghij",       1, { { 0, 10, 0, 10 } } },                                   // 纯 ASCII 长串
	{ "一二三四五",         1, { { 0, 5, 0, 5 } } },                                    // CJK 长串
};
```

★ 第 8–10 行是**改前行为的快照**，不是"应该怎样"：它们的断行来自 `DecodeFirstCodepoint` 接受 overlong（`Core/UTF8.cpp:62-77`），R1 要守的正是它。
★ 转义陷阱：`"\xC0\x8A"` 后面不能再接十六进制字符（`"\xC0A"` 会被解析成一个超范围转义）；本表所有 `\x` 串都以 `"` 或 `,` 收尾。

### 3.3 回填流程（批 0 = 只加测试、零产品码）

1. 落盘 `kFitGolden` / `kLineGolden` + `LineCountForTest()` + `ExpectLines()` + T39-1/T39-2（**先用手算值**），在 HEAD 上构建并跑。
2. **不符的组**：以实测回填，并在本稿 §9 记一条（写明哪组、手算值、实测值、原因）。
3. 全绿后提交「批 0」——**此后才允许动产品码**。★ 采不到就不得动 R1 / R2。

### 3.4 两处已知的"预期一致"（若不符，先怀疑夹具而不是手算）

- `LineCountForTest()` 的 `n` 与表 B 的 `lineCount` 必须相等；不相等先查「高度是否 ≥ `n×16`」（高 200 ≫ 3×16）。
- 表 B 的 `drawBeg == ownBeg` 对**所有**非折行样本成立（`LineDrawRange = {startCp, startCp + cpCount}`，`LineRange = {startCp, ownedEnd}`，而 `ownedEnd == startCp + cpCount` 在该支恒真）。

---

## §4 用例 T39-1..7（登记名 / 断言 / 期望来源）

登记名与函数名（插在 `:3075` 的 `Add` 之后、`}` 之前）：

```cpp
    GetTestRegistry().Add("TextBox.Utf8ScanGoldenFit",    &Test39Utf8ScanGoldenFit);      // T39-1（批 0）
    GetTestRegistry().Add("TextBox.Utf8ScanSamples",      &Test39Utf8ScanSamples);        // T39-2（批 0）
    GetTestRegistry().Add("TextBox.Utf8ScanOverlongLf",   &Test39Utf8ScanOverlongLf);     // T39-3（批 0）
    GetTestRegistry().Add("TextBox.Utf8ScanBoundaries",   &Test39Utf8ScanBoundaries);     // T39-4（批 0）
    GetTestRegistry().Add("TextBox.Utf8ScanCrossBranch",  &Test39Utf8ScanCrossBranch);    // T39-5（批 0）
    GetTestRegistry().Add("TextBox.Utf8ScanTwoRanges",    &Test39Utf8ScanTwoRanges);      // T39-6（批 0）
    GetTestRegistry().Add("TextBox.Utf8ScanCountAgrees",  &Test39Utf8ScanCountAgrees);    // T39-7（批 0）
```

★ 七条**全部**在批 0 落地并**在 HEAD 上先绿**——它们断言的是**改前行为**，改后仍须绿。这是本 Phase 判定"逐位不变"的全部依据。

**共用 helper**（与本表一起加在测试文件顶部区域）

```cpp
/// @brief 码点总数（**外部口径**，供 T39-4 / T39-7 交叉校验）
/// @details ★ 不能直接调 `GetCodepointCount()`——它是 **private**（`TextBox.h:322` 的 `private:`
///          早于 `:348` 的声明），而私有成员**不能用 `using` 暴露**（访问检查在 using 声明处）。
///          公共通道：`MoveCaretToEnd()`（`:128`）的实现就是 `m_caret = GetCodepointCount()`
///          （`TextBox.cpp:431`）⇒ 读回 `GetCaret()`（`:129`）即得**同一个**私有实现的结果。
///          ★ 副作用：移动 caret + Invalidate——caret **不在**行表的 10 维键里，故不干扰行表。
static std::size_t CountViaCaret(TestableTextBox& box){
	box.MoveCaretToEnd();
	return box.GetCaret();
}

/// @brief 表 B 的通用断言：行数走 `LineCountForTest()`（P0），逐行走两口径
/// @details 高 200 = 给 `n×16 <= areaH` 留足余量（最多 3 行 ⇒ 48）
static void ExpectLines(TestableTextBox& box, const LineGold& g){
	box.SetWordWrap(false);
	box.SetSize(120, 200);
	box.SetText(std::string(g.text));
	const std::size_t n = box.LineCountForTest();
	EXPECT_EQ(n, g.lineCount);
	if (n != g.lineCount)
		return;                                  // ★ 防越界：`LineRange` 是裸 `m_lines[i]`，越界 = UB
	for (std::size_t i = 0; i < n && i < 3; ++i){
		const auto own  = box.OwnForTest(i);
		const auto draw = box.DrawForTest(i);
		EXPECT_EQ(own.first,   g.rows[i].ownBeg);
		EXPECT_EQ(own.second,  g.rows[i].ownEnd);
		EXPECT_EQ(draw.first,  g.rows[i].drawBeg);
		EXPECT_EQ(draw.second, g.rows[i].drawEnd);
	}
}
```

| # | 函数体要点 | 期望来源 |
|---|---|---|
| **T39-1** | `FakeTextMeasurer m; Font f{};` → 遍历 `kFitGolden`：`const TextFit fit = m.FitText(f, std::string(g.text), g.startCp, g.maxWidth);` 断言 `fit.fitCp == g.fitCp` 且 `EXPECT_NEAR(fit.width, g.width, 0.001f)` | 表 A |
| **T39-2** | 遍历 `kLineGolden` 全 13 行 → `ExpectLines(box, g)` | 表 B |
| **T39-3** | ① `kLineGolden[7..9]` 三行 → `ExpectLines`；② **inline** `C0 4A`：`LineGold{ "\xC0\x4A", 2, {{0,0,0,0},{1,1,1,1}} }` → `ExpectLines` | 表 B + §4 注 |
| **T39-4** | 子集（`""` / `"\n"` / `"\n\n"` / `"ab\n"` / `"a\r\nb"` / `"a\xC0"`）+ 末行 `OwnForTest(n-1).second == CountViaCaret(box)`；`"a\xC0"` 额外断 `LineCountForTest() == 1`（`byte` 越过 `textSize` 不产生额外行） | 表 B + `CountViaCaret`（A-4） |
| **T39-5** | 三串（`C0 8A` / `E0 80 8A` / `F0 80 80 8A`）：非折行 box 与折行 box（`SetWordWrap(true)`，宽 120 ⇒ **折行宽也 = 120**：无头 ⇒ `m_vBar == nullptr` ⇒ `GetTextAreaWidth` 的条槽让位分支（`TextBox.cpp:772`）不进，且 `padding` 默认 0）各读 `LineCountForTest()`，断言都 == **2** 且相等；② 非折行 2 行 box：`MoveCaretToStart(); OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));` 断言 `GetCaret() == 1`；③ **单行负例**（`"abc"`，同法）断言 `GetCaret() == 0`——②③ 成对才钉住 `TextBox.cpp:1345` 的末行守卫（1 行 ⇒ no-op、2 行 ⇒ 才走 `:1349`） | §1.3 交叉支表 |
| **T39-6** | 遍历表 B：`draw.second == own.second`；非末行 `own.second + 1 == 下一行 own.first`（硬行；本支**全部**是硬行） | 表 B |
| **T39-7** | 遍历表 B：`n = LineCountForTest()`；断言 `OwnForTest(n-1).second == CountViaCaret(box)`（= `m_lines` 末行归属终点 == 码点总数）；另加两条 20k（CJK `"字"×20000` / ASCII `"a"×20000`）只断该等式（不查逐行） | `CountViaCaret`（A-4，内部即 `GetCodepointCount()`） |

★ **T39-3 与 T39-2 是子集关系**（#8–#10 已在表 B 内）。它独立存在的理由：① 负对照（R39-3）需要**名字就能指认**的靶点；② 它承载表 B **之外**的 `C0 4A`（overlong × 非法续字节的交叉变体，不属于 A6 十一类）。
★ **T39-1 的 `Font f{}`**：`FakeTextMeasurer` 忽略 `Font`（`TextBoxTests.cpp:35` 的 `MeasureText(const Font&, ...)` 未使用形参），故不需要控件。
★ **`EXPECT_NEAR` 而非 `EXPECT_EQ`** 用于 `width`：`FitText` 的宽度是 `float` 累加，`==` 在跨编译器下不可靠；`fitCp` 是整数，用 `EXPECT_EQ`。

---

## §5 机检 C39-1..6 → 可执行命令（工作目录 = 仓库根；**全部实跑过**）

```bash
TB=ECDI/src/Widget/TextBox.cpp
TM=ECDI/include/ECDI/Render/TextMeasurer.h
```

| # | 命令 | 期望 | 今日实测 |
|---|---|---|---|
| **C39-1** | `awk '/^void TextBox::RecalculateLines/,/^}/' $TB \| grep -cE "CodepointIndexToByteOffset\|ByteOffsetToCodepointIndex\|GetCodepointCount"` | **0** | **3**（`GetCodepointCount` 1 + `CodepointIndexToByteOffset` 2） |
| **C39-2** | `awk '/^void TextBox::RecalculateLines/,/^}/' $TB \| grep -c ".substr"` | **0** | **1** |
| **C39-3** | `awk '/^void TextBox::RecalculateLines/,/^}/' $TB \| grep -c "DecodeCodepointAt("` / `… \| grep -c "DecodeFirstCodepoint("` | **1** / **0** | 今日 **0** / **1** |
| **C39-4** | `awk '/virtual TextFit FitText\(const Font& font/,/^\t\t}$/' $TM \| grep -v "[[:space:]]//" \| grep -o "CodepointIndexToByteOffset(" \| wc -l` | **4** | **7**（含注释行 8） |
| **C39-5** | `git diff -U0 -- ECDI/include/ECDI/Core/UTF8.h \| grep "^-" \| grep -v "^-.*///\|^-.*//"` | **空**（零删除**代码**行） | — 改后验 |
| **C39-6** | `grep -rn --include=*.cpp --include=*.h "m_lineStarts\|m_needsLineRecalc" ECDI/` = 空；用例数取运行器汇总行 **`Tests: N`**（`TestFramework.cpp:169-171`） | 空 / **538** | 空 / 531 |

★ **C39-4 必须用 `grep -o | wc -l`（出现次数）**：`grep -c` 数的是**行**，而 `:87` 一行有两次（⇒ 7 会被数成 6）；且 `grep -v "[[:space:]]//"` 会把带**行尾注释**的整行删掉（若 `firstEnd` 行有尾注释，改后只剩 3）。⇒ 配合 §1.3 的"代码行不留行尾注释"。
★ **C39-1 必须并上 `GetCodepointCount`**：它内部调 `ByteOffsetToCodepointIndex`，只 grep `CodepointIndexToByteOffset` 覆盖不到（初设 P1 的原始缺陷）。
★ **C39-5 不得写成"零 `-` 行"**：§1.1 要求给 `UTF8.h:26-29` 的头注释**纯插入**一行，若就地改写会出 `-` 行——判据是「零删除**代码**行」，注释行允许删。

---

## §6 实现批次与回归门

| 批 | 内容 | 用例 | 累计 | 回归门 |
|---|---|---|---|---|
| **批 0** | **只加测试**：`LineCountForTest()` + `ExpectLines` + 两张表 + T39-1..7 + 七条登记。**零产品码** | +7 | **538** | ✅ **2026-10-29 已通过：四构建链 538/538**（MSVC / Clang / ClangCL / MinGW）= 改前行为快照已钉住，golden **未回填**（预置值与实测一致） |
| **批 1** | **产品码四文件一次落地**：`Core/UTF8.h` + `Core/UTF8.cpp` + `Widget/TextBox.cpp` + `Render/TextMeasurer.h` | ±0 | **538** | ✅ **2026-10-29 已通过：538/538**，C39-1..4/6 达标（`0 / 0 / 1-0 / 4 / 空`）；✅ **R39-3 负对照已做**：改一行后 `533 passed, 5 failed`，红的正是预测的 5 条（§9 v1.3） |
| **批 2** | **收口**：需求回写 v1.2（D39-G 形态）· `roadmap-deferred.md` #78 → ✅ · `docs/README.md` 锚点（用例 538）· 探针改前/改后两轮读数入档 | ±0 | 538 | 四构建链（MSVC / Clang / ClangCL / MinGW）且断言启用态 + 显示器缩放一并记录 |

★ **为什么 R1 与 R2 同批**（K12）：`Core/UTF8.h` 与 `Render/TextMeasurer.h` 都是公共头，改任一个都触发几乎全量重编（Phase 38 实测冷全量 35 min 量级）⇒ 分两轮 = 70 min 且中间态无信息量。
★ **归因表（同批落地的代价，用测试集隔离来偿还）**：

| 症状 | 归因 |
|---|---|
| 仅 T39-1 红 | **R2**（`TextMeasurer.h`） |
| T39-2/3/4/6/7 有红、T39-1 绿 | **R1**（`TextBox.cpp`）或 **A2′**（`Core/UTF8.cpp`） |
| 编译失败 / 缺符号 | **A2′**（声明 / 转发） |
| 538 中存量红（T38-* / T29-*） | 先看是否碰了折行支（红线①） |

★ **每批通用门**（沿 Phase 37 / 38）：① 用例计数 == 累计值；② **doc_lint（口径 v1.1 订正，见下）**；③ 改动文件 BOM=True / CR=0；④ `git status` 与本批内容逐项对上（不得夹带）。

★★ **门禁 ② 有两个洞，都必须堵（v1.1 实测，前者自 Phase 37 起就存在，后者至今没人写过）**

**洞 1 · `--select` 不支持 `..` 区间 ⇒ 恒真**：`tools/doc_lint.py:582` 只按**逗号**切分，`--select L02..L11` 被当成一个名为 `L02..L11` 的规则，没有任何 finding 的 `check` 等于它 ⇒ **恒报 `0 defect`**。实测对照：`--select L04` = **3 条**，`--select L02..L11` = **0 条**。⇒ 改列举式。

**洞 2 · lint 只扫 `git ls-files` ⇒ untracked 稿完全不在视野内**：`tracked_files()`（`doc_lint.py:101-108`）就是 `git ls-files`。**实测**：本稿与初设当时都是 `??`（untracked）、只有需求稿 tracked（`git ls-files | grep phase39` 只回一行）；把一行 **2 列**的坏表格插进 §2 后跑 `--select L04`，**仍报 0**——不是"干净"，是**文件根本没被读**。⇒ 门禁顺序必须是 **`git add` → 跑 lint → `git commit`**，在 add 之前跑出来的 0 一律视为**假绿**。

```bash
# 工作目录 = 仓库根；Git Bash（与 §5 同一 shell）
git add <本批改动的文件>            # ★★ 必须先 add：untracked 稿对 lint 不可见（洞 2）
python tools/doc_lint.py --select L02,L03,L04,L05,L06,L07,L08,L09,L10,L11 --quiet --out .lint.tmp.txt
grep -c "^\[DEFECT\]" .lint.tmp.txt                              # 必须 == 12（既存基线，不得增加）
grep -c "^\[STYLE\]"  .lint.tmp.txt                              # 必须 == 3
grep -cE "^\[(DEFECT|STYLE)\].*phase39" .lint.tmp.txt            # 必须 == 0（本 Phase 三稿零非 info）
rm -f .lint.tmp.txt
```

★ 判据③**必须**带 `^(DEFECT|STYLE)` 前缀：报告里 `phase39` 会出现一次 **INFO**（`L02 … requirements.md eol=CRLF`，行尾风格不入 defect），裸 `grep -c phase39` 会恒等于 1 ⇒ 又造出一条假红。
★ **首次执行时的自检（R39-11 的负对照）**：`git add` 之后临时插一行少列的表格，确认输出里出现 `[DEFECT] … phase39 …` 再删除——报不出说明又以别的形式恒真。**2026-10-29 实测记录**：在 add 之前做这个负对照，结果是「不红」，由此才发现洞 2。
★ **在 `git add` 之前自查单文件**（本稿 v1.1 即用此法）：`sys.path.insert(0,"tools"); import doc_lint; doc_lint.scan_markdown(report, path, text, opts)` —— 直接用同一份规则扫指定文件，绕过 `git ls-files` 限制。**必须配阳性对照**：拿 `phase38-…preliminary-design.md`（已知 9 条 L04）跑同一脚本，得 **9** 才算脚本有效；本 Phase 两稿得 **0**，故「干净」不是假绿。

★ **既存基线（2026-10-29 实测：12 defect + 3 style，**改动前即存在**）**：phase38 初设 `:114-122` 九条 L04（表格 5 列 vs 表头 6 列）· phase38 详设 `:696` / `:730` / `:732` 三条 L04 · L09 两条（`phase19-…det…:487` / `phase38-…detailed:633`，连续 3 空行）· L11 一条（`framework-implementation-audit.md:685`，修订记录倒序）。
★ **处置**：这些**全部不属本 Phase、且未获授权不动**（原子授权纪律）⇒ 门禁只要求「**不新增**」；清理另立待办（建议并入 roadmap 的「文档卫生」类条目，由用户拍板）。
★ **唯一例外**：`--select L01` 有大量既存 BOM defect（`Platform/Win32/*`、`Render/*` 等），故门禁范围**不含 L01**——BOM 由门禁 ③ 逐文件自查。

---

## §7 风险处置（承接初设 R39-1..10，补「实现期动作」；本稿新增 R39-11）

| # | 风险 | 实现期动作 |
|---|---|---|
| **R39-1** | `byte` 越过 `textSize` 后误用 `data + byte`（UB） | §1.4 的形态保证「指针只在 `byte < textSize` 时求值」；实现后逐行读一遍循环体，确认没有第二处 `data + …` |
| **R39-2** | 误改折行支 | `git diff` 必须只落在 `:476-490`（**15 → 20 行**，§2.4）；T38-1..18 全绿是护栏 |
| **R39-3** | 「只看首字节」的实现也能过 = **假绿** | **负对照（必做，做完改回）**：在 `byte += len;` **之前**插 `const unsigned char lead = static_cast<unsigned char>(data[byte]);`，把 `if (ch == U'\n')` 改成 `if (lead == 0x0A)`（= 最初那版「只看首字节」的形态）。此时 **T39-3 的 `C0 8A` / `E0 80 8A` / `F0 80 80 8A` 三档必须红**（行数 2 → 1），且 T39-2 的 #8–#10 同时红。红了才说明判据有效；然后**改回**并在 §9 记一条「负对照已执行」。★ 若这一版全绿 ⇒ 判据是装饰品，本 Phase 立即停 |
| **R39-4** | 把 `C0 4A` 当跨支判据 ⇒ 假红 | T39-5 **只**用 `0x8x` 续字节全合法的三串；`C0 4A` 只进 T39-3 |
| **R39-5** | 两个公共头分两轮编 | 批 1 一次落地（§6） |
| **R39-6** | `DecodeCodepointAt` 跨 TU 调用开销 | 20k 码点 × ~2–5 ns ≈ 0.04–0.1 ms（相对 257 ms 可忽略）；★ 若批 2 的探针实测单趟 > 0.2 ms，才考虑头内 inline（代价 = `SequenceLength` 上提，+1 符号） |
| **R39-7** | `UTF8.h:26-29` 头注释与新函数脱节 | §1.1 的**纯插入**一行；C39-5 的 diff 会看到 |
| **R39-8** | 用例预算 538 超出 | 允许 T39-4 并入 T39-2（同类断言），**不得减判据** |
| **R39-9** | 后人把行数断言改回 `RowCountForTest()` | `LineCountForTest()` 的文档注释已写死原因（§2.5）；表 B 的 `lineCount` 注释写明 `= m_lines.size()`；R39-3 的负对照同时是「通道错了」的探测器（通道错 ⇒ overlong 读 1 行 ⇒ 负对照不红） |
| **R39-10** | 探针跨口径 ⇒ A3 倍率失真 | 批 2 的探针**同一支、同一复刻体**：`loop_current`（照抄改前 `:481-489`）与 `loop_single_pass`（照抄 §1.4）并存，永不删 |
| **R39-11** | **门禁恒真**（两个洞：① `--select` 不支持 `..` ⇒ 旧命令恒报 0；② lint 只扫 `git ls-files` ⇒ **untracked 稿不在视野**，add 之前跑恒 0） | §6 已改列举式 + 钉死「`git add` → lint → commit」顺序；★ **批 0 首次执行时做负对照**：`git add` 后临时在稿里插一行少列表格，确认输出出现 `[DEFECT] …phase39…` 再删除。2026-10-29 已在 add 之前试过一次——**不红**，这正是洞 2 的发现过程 |

---

## §8 影响面（复核初设 §6，本稿确认）

| 项 | 预估 | 复核 |
|---|---|---|
| 源码 | `Core/UTF8.h` **+14**（声明 1 + 注释 12 + 头注释 1）· `Core/UTF8.cpp` **+13**（26 → 39）· `Render/TextMeasurer.h` **净 +2**（5 → 9，再 −2）· `Widget/TextBox.cpp` **+5**（15 → 20）· `Tests/TextBoxTests.cpp` **+~200**（实现后回填实数） | ✓ **与 §2 逐项相等**（v1.1 重算） |
| 公共头 | **100 → 100**（无新增头） | ✓ |
| public API | **+1**（`DecodeCodepointAt`；`DecodeFirstCodepoint` 签名不动） | ✓ |
| CMake | **0**（无新增源文件） | ✓ |
| 用例 | **531 → 538**（+7） | ✓ |
| docs | **200 → 201**（本稿；收口时需求 v1.2 / README 锚点另计） | ✓ |
| 机检 | **6 条**（C39-1..6） | ✓ |

---

## §9 修订记录

- **v1.0（2026-10-29）** 详设落盘。§0 锚点逐条亲核（含 **A-1..A-4** 四条与初设的差异：`C0 4A` 不进表 B、T39-5 辅助断言补上实码依据、`byteStart` 外提位置定在早退之后、**`GetCodepointCount()` 是 private ⇒ 交叉校验改走 `MoveCaretToEnd(); GetCaret()`**）；§1 逐字给出三处接口面与两处替换的完整代码；§2 逐文件行数账 + 不动清单 + `LineCountForTest()` 全文；§3 两张 golden 表逐字可粘（表 A 8 组 / 表 B 13 行）+ 回填流程；§4 七条用例的登记名 / helper（含 `CountViaCaret`）/ 断言要点 / 期望来源；§5 六条机检命令**全部实跑**并给出今日基线（3 / 1 / 0-1 / 7 / 空 / 531）；§6 三批切分 + 归因表；§7 逐条承接 R39-1..10 并补实现期动作（**含负对照的执行方法**）。
- **v1.1（2026-10-29，评审后订正）** 四条订正，逐条给出实码依据：
  - **① 事实性错误（T39-5）**：「宽 120 ⇒ 折行宽 106」**不成立**。`GetTextAreaWidth`（`TextBox.cpp:760-773`）的条槽让位要求 `m_vBar != nullptr`（`:772`），而 `m_vBar` **只在** `:46` 的绑定入口赋值——无头夹具无窗口 ⇒ `m_vBar == nullptr` ⇒ 让位分支不进；默认 `padding = 0`（`DefaultTheme.cpp:43`）⇒ **折行宽 = 120**。结论（2 行）不受影响（overlong 解出的 `\n` 是**硬断**，与宽度无关），但原注释会误导实现者以为测到了条槽路径。
  - **② 账目算术（§2.2 / 2.3 / 2.4 + §8）**：三处手数错，实测重算 —— `UTF8.cpp` `:55-80`（26 行）→ §1.2 块（39 行）= **+13**（原写 +6）；`TextMeasurer.h` `:83-87`（5 行）→ §1.3 块（9 行）= **+4**，再 −2 ⇒ **净 +2**（原写 +2 / −2 = 净 0，与 §8 的「净 0」自洽但与实测不符）；`TextBox.cpp` `:476-490`（15 行）→ §1.4 块（20 行）= **+5**（原写 +3）。§2 新增**数法**（旧 = 闭区间长度 / 新 = 围栏之间行数 / 净 = 差）与「§8 必须逐项相等」的检查条。
  - **③ 措辞与断言不同步（A-2 / T39-5）**：原文读起来像「1 行 no-op」只是依据。**改为断言 ③**（单行箱 `"abc"`，`Down` 后 `GetCaret() == 0`），与 ② 的 `== 1` 成对 ⇒ `TextBox.cpp:1345` 守卫的两侧都被钉住；只断 ② 时「永远返回 1」的错误实现也能过。
  - **④ 门禁恒真（§6 + 新增 R39-11）**：两个洞。① `--select L02..L11` **从不生效**——`doc_lint.py:582` 只按逗号切分，`L02..L11` 被当成单个未知规则名 ⇒ 恒报 `0 defect`（实测 `--select L04` = 3 条 vs `--select L02..L11` = 0 条）。② **更隐蔽**：`tracked_files()`（`:101-108`）= `git ls-files` ⇒ **untracked 稿完全不在视野内**；本稿与初设当时都是 `??`，插一行坏表格后 `--select L04` 仍报 0——**是没被读，不是干净**。⇒ 改列举式 + 钉死 `git add` → lint → `commit` 顺序 + 硬判据（12 / 3 / phase39 零非 info），并登记既存 **15 条非 info**（12 defect + 3 style）的清单与「未获授权不动、只要求不新增」的处置。
  - 顺带：§1.3 标题 `:83-88` → **`:83-87`**（`:88` 的 `if` 是锚点、逐字不动，原写法会让人以为 `if` 也被替换）；R39-2 的「只落在 `:476-490` + 删 3 行」→ 「15 → 20 行」。
- **v1.2（2026-10-29，批 0 落地）** `ECDI/src/Tests/TextBoxTests.cpp` **+290 行 / 零删除**，产品码未动：
  - `TestableTextBox::LineCountForTest()`（类内，`:82-107`）——D39-H 的 Center-inset 反解；前置 `n×16 <= areaH`；**用完还原 Top**。
  - 两张 golden 表 + `CountViaCaret` + `ExpectLines`（`TestableTextBox` 之后）。
  - **T39-1..7** 七个函数 + 七条登记（`.Add("` 全仓 541 → **548**）。
  - **静态自检**：`g++ -std=c++20 -fsyntax-only -Wall -Wextra` 通过，**新增行零警告**。
  - **构建验证**：四构建链（MSVC / Clang / ClangCL / MinGW）**538/538 全绿**；golden **一次通过、未回填**（§3）。
  - **新增工具** `tools/phase39_gold_probe.py`（22 组复核，带负对照；`exit 1` 可作门）。**注意它不在批 0 的授权面内**——是用户在同轮追加批准后才加的。
  - ★ 一个实施期观察：T39-7 的两条 20k 串在**现状实现**下会明显变慢（O(N²)，Debug 约 257 ms/次 × 2），批 1 后应降到毫秒级；这不是死循环。
- **v1.3（2026-10-29，批 1 落地）** 产品码四文件，**538/538 全绿**：
  - **账目逐项对上 §2**：`UTF8.h` **+14/−0**（1 头注释 + 12 说明 + 1 声明，**零删除** ⇒ C39-5 达标）· `UTF8.cpp` **+13**（26 → 39）· `TextBox.cpp` **+5**（15 → 20，只动 `else{}`）· `TextMeasurer.h` **+2**（5 → 9，再 −2）。
  - **C39 机检**：`C39-1 = 0` · `C39-2 = 0` · `C39-3 = 1 / 0` · `C39-4 = 4` · `C39-6a` 空。
  - ★ **一个只有跑过才知道的坑**：C39-1 首次跑出 **1** —— 不是漏改，而是我在 `:482` 的注释里写了「与 `CodepointIndexToByteOffset` 同形」，**机检按标识符 grep，不认注释**。改口径是不行的（那会削弱判据），正解是改注释措辞（「与旧走的换算同形」）。⇒ 判据要求 0 时，**注释里也不能出现该标识符**。
  - ★★ **等价性对拍（比用例更强的一次验证）**：临时探针把 **HEAD 版 `DecodeFirstCodepoint` 的原文**嵌进去作参照（不调新转发版，否则退化成「新 vs 新」），对拍「旧：逐码点换算 ×2 + `substr` + 解码」 vs 「新：单趟 `DecodeCodepointAt`」——**14 组定值 + 50 000 条随机字节串（长度 0–24，含非法/截断/overlong）+ 2 条 20k 长串，行表三字段逐位相同，`exit 0`**；另比对「`DecodeCodepointAt(data,k)` == `DecodeFirstCodepoint(substr(0,k))` 对所有前缀 k」（覆盖转发与截断语义）。
  - **耗时（同一探针的复刻路径，`-O2`，非产品函数直测）**：CJK 20k **321.84 ms → 0.037 ms（8800×）**、ASCII 20k **162.64 → 0.019 ms（8400×）**；20k → 200k 为 **10.9×**（线性成立）。★ 这是「算法层」读数；产品路径的端到端读数留给批 2 的探针。
  - ✅ **R39-3 负对照已执行（2026-10-29）**：把 `:489` 的 `if (ch == U'\n')` 临时改成 `if (data[byte - len] == '\n')`（退化成「只看首字节」），结果 **`Test summary: 533 passed, 5 failed, 538 total`**，红的正是预测的 5 条：

| 用例 | 失败点数 | 断言 |
|---|---|---|
| `TextBox.Utf8ScanSamples` | 3 | `n == g.lineCount`（#8/#9/#10 三档） |
| `TextBox.Utf8ScanOverlongLf` | 4 | 同上（3 档 + `C0 4A`） |
| `TextBox.Utf8ScanTwoRanges` | 3 | `n == g.lineCount` |
| `TextBox.Utf8ScanCountAgrees` | 3 | `n == g.lineCount` |
| `TextBox.Utf8ScanCrossBranch` | 7 | `nPlain == 2` ×3、`nPlain == nWrap` ×3、`GetCaret() == 1` ×1 |

`Utf8ScanGoldenFit`（T39-1）与 `Utf8ScanBoundaries`（T39-4）保持绿 —— 与设计预期逐条吻合（T39-4 的子集里不含 overlong 样本）。
★ **一个额外的收获**：`CrossBranch` 里红的 `GetCaret() == 1` 是 **A-2 的断言②**，而**断言③（单行负例 `GetCaret()==0`）仍绿**——这恰好证明 ②③ 是**守卫的两侧**而不是重复断言：只留 ② 的话，负对照只能说明「行数变了」，分不清「2 行退化成 1 行」与「Down 永远返回 1」。**判据有牙**。
