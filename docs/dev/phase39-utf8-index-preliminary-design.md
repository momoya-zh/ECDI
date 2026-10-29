# Phase 39 · 初步设计 —— UTF-8 码点索引换算的 O(N²) 消除（deferred #78）

> 状态：**v1.1**（2026-10-29 初设落盘；**v1.1 = 外部评审（Qoder）修稿**——1 处 P0 阻断（行数读法：`RowCountForTest()` 对 overlong 样本读成 1 ⇒ 五条用例假红／迁就即假绿）已用 `LineCountForTest()` 通道解决（D39-H）；4 处 P1 机检口径、2 处 P2 缺口（A2/A3 无落点、`C0 0A` 事实错）、2 处 P3 建议全部落实，详见 §8）
> **需求基线**：`docs/dev/phase39-utf8-index-requirements.md` **v1.1**（§7 七项闸门 = 本稿 §1 的七个小节，逐题收口）
> **代码基线**：HEAD `9c158f2`（需求稿提交后**只有 `docs/` 变动**）⇒ `TextBox.cpp` / `Core/UTF8.cpp` / `Render/TextMeasurer.h` 与 `2bea0f7` **逐字节相同**，行号沿用需求稿且无漂移
> ★ **事实纪律**：本稿每条行号 = 2026-10-29 `grep -n` / `sed` 亲核；机检命令**都实际跑过一次**并把读数写进正文（不写「应该为 N」）。
> ★ **数字纪律**：只承诺①复杂度 ②分配次数 ③改前/改后记录制耗时；**不写「提升 X 倍」**（沿 Phase 28 / 38 先例）。

---

## §1 初设必答（需求 §7 七项逐题收口）

### 1.1 问一 = 单趟推进的逐行骨架 + `count` 等价的归纳证明

**现状（`TextBox.cpp:479-490`，逐行抄录）**

```cpp
const size_t count = GetCodepointCount();                              // :479  = ByteOffsetToCodepointIndex(m_text, m_text.size())
size_t start = 0;                                                      // :480
for (size_t i = 0; i < count; ++i){                                    // :481
    const size_t byte     = CodepointIndexToByteOffset(m_text, i);     // :482  O(i)
    const size_t nextByte = CodepointIndexToByteOffset(m_text, i + 1); // :483  O(i)
    const char32_t cp = DecodeFirstCodepoint(m_text.substr(byte, nextByte - byte));  // :484
    if (cp == U'\n'){                                                  // :485
        m_lines.push_back(TextLine{ start, i - start, 0.0f, i });      // :486  `\n` 不属任何一行
        start = i + 1;                                                 // :487
    }
}
m_lines.push_back(TextLine{ start, count - start, 0.0f, count });      // :490  末段（可为空 = 尾部空行）
```

**目标（草案，逐行可落地）**

```cpp
else{
    // ── 非折行支：单趟推进（Phase 39 R1）──
    // 每码点 O(1)：就地解码 + 按前导**声称**的长度步进 ⇒ 整体 O(N)。换算次数 2N → 0，substr N → 0。
    // 行表口径与改前逐位相同：`\n` 不属任何一行，行末 `ownedEnd` = 该 `\n` 的码点索引。
    // ★ 命名：`textSize`（全串长）与 `DecodeCodepointAt` 的 `size` 形参（**剩余**长度）不同义——
    //   两者同名是差一错的温床，故此处不用 `size`（v1.1 评审 P3②）。
    const size_t textSize = m_text.size();
    const char* const data = m_text.data();
    size_t byte  = 0;   // 当前码点首字节偏移（★ 末段截断时**可越过 textSize**——见 L1 注）
    size_t cp    = 0;   // 已消费码点数 == 当前码点索引
    size_t start = 0;   // 当前行首码点索引
    while (byte < textSize){
        char32_t ch = 0;
        const size_t len = DecodeCodepointAt(data + byte, textSize - byte, ch);   // 就地解码（不切子串）
        byte += len;
        if (ch == U'\n'){
            m_lines.push_back(TextLine{ start, cp - start, 0.0f, cp });
            start = cp + 1;
        }
        ++cp;
    }
    m_lines.push_back(TextLine{ start, cp - start, 0.0f, cp });
}
```

**四条引理（等价性的全部论据）**

| # | 引理 | 证明 |
|---|---|---|
| **L1** | 第 `k` 次迭代的 `byte` == `CodepointIndexToByteOffset(m_text, k)` | 两者的推进式同为 `pos += SequenceLength(lead)`（`Core/UTF8.cpp:38-41` vs 本骨架 `byte += len`，而 `len` **就是** `SequenceLength(lead)`，见 §1.2）；初值同为 0；`CodepointIndexToByteOffset` 的附加条件 `cpIndex < k` 在第 `k` 次迭代前恒成立。**归纳即得**。★ 注：`CodepointIndexToByteOffset` 用 `pos < text.size()` 作条件、推进**不做钳制**；本骨架同形 ⇒ 两者在**末段截断**处同样「越过 `textSize`」（`"A\xC0"` 的二次迭代：1 → 3，`textSize` = 2）——**这是刻意的**，做钳制虽然计数相同（越界后两者都退出循环），但证明就不再逐字对应 |
| **L2** | 迭代次数 == `count` == `ByteOffsetToCodepointIndex(m_text, m_text.size())` | 两者的状态机同为「`pos` / `byte` 未到串尾则推进一个 `SequenceLength`」；停止条件同为「到串尾」（后者传的 `byteOffset` 恰为 `size`，故 `pos < byteOffset` 与 `pos < size` 等价）⇒ 步数相同。**这是 D39-6「`count` 免费」的依据** |
| **L3** | `DecodeFirstCodepoint(m_text.substr(byte, nextByte - byte))` == 单趟的 `ch` | `substr(byte, nextByte - byte)` 的长度 = `min(nextByte, size) - byte`；由 L1，`nextByte - byte == len`（声称长），故长度 = `min(len, size - byte)` = **剩余长度被钳到 `len`**。而 `DecodeFirstCodepoint` 的分支判据是「串长 >= 2/3/4」⇒ 等价于「剩余 >= 2/3/4」⇒ 与 `DecodeCodepointAt` 的 `size >= 2/3/4`（`size` = 剩余）**逐分支对应**，见 §1.2 对齐表 |
| **L4** | 行表逐位相同 | L1–L3 ⇒ 每次迭代的「码点索引 + 是否 `\n`」与改前相同；`push_back` 的四个字段表达式与改前**同构**（`{start, i-start, 0, i}` ↔ `{start, cp-start, 0, cp}`）⇒ 序列逐位相同。空串：0 次迭代 ⇒ `{0, 0-0, 0, 0}`，与改前 `{0, 0, 0, 0}` 相同 |

**三个边界（手推，进 T39-4）**

| 输入 | 迭代 | 产出 |
|---|---|---|
| `""` | 0 次 | 1 行 `{0,0,0,0}` |
| `"a\xC0"`（尾截断 2 字节前导） | 2 次（`byte`: 0→1→3） | 1 行 `{0,2,0,2}`（第二码点 `ch` = `0xC0` ≠ `\n`） |
| `"\n"` | 1 次 | 2 行 `{0,0,0,0}` + `{1,0,0,1}`（尾部空行） |

★ **为什么循环条件从 `i < count` 必须改成 `byte < size`**：`count` 在新骨架里是**输出**（`cp`）而不是输入；若保留 `i < count` 就得先调 `GetCodepointCount()` 多扫一遍（又回到 2N）。D39-6 的「免费」指的就是这条。

### 1.2 问二 = 脏字节的等价性（两档：长度档 + 解码档，逐分支对齐）

现状 `DecodeFirstCodepoint`（`Core/UTF8.cpp:55-80`）与新的 `DecodeCodepointAt` **逐行对齐**（同一文件、同一 `SequenceLength`）：

| 现状行 | 现语义 | `DecodeCodepointAt` | 等价性要点 |
|---|---|---|---|
| `:56-57` | 空串 → `0` | `size == 0` → 写 `0` 并返回 **0** | 旧版的「空串」由调用方 `substr` 造出；新版 `byte < textSize` 保证 `textSize - byte >= 1`，只有 `DecodeFirstCodepoint(string)` 转发时才会走空串路径 |
| `:59-61` | `(lead & 0x80) == 0` → `lead`（长度 1） | 同 | ASCII |
| `:62-66` | `len == 2 && size >= 2` → `((lead & 0x1F) << 6) \| (b1 & 0x3F)` | 同（`size` = **剩余**长度） | ★ 旧版判的是 **substr 的长度** = `min(len, 剩余)` ⇒ `>= 2` ⟺ 剩余 `>= 2`（L3） |
| `:67-71` | `len == 3 && size >= 3` → 三字节拼装 | 同 | 同上 |
| `:72-77` | `len == 4 && size >= 4` → 四字节拼装 | 同 | 同上 |
| `:78-79` | 其余（截断 / 非法前导 / 连续字节）→ **`lead`** | 同 | ★ **返回值仍是 `len`**（可能 > 剩余）——步进口径与 `SequenceLength` 一致（L1 注） |
| `:9-15` | `SequenceLength`：非法前导按 **1** 字节（防死循环） | 直接复用（同一匿名 namespace 内） | **单源**：不复制、不重写（D39-G 的理由） |

★ **两档必须分开记**：**长度档**（`SequenceLength`，决定「跳几步」）与**解码档**（三分支拼装，决定「解出什么」）任何一档走偏，行表都会在脏字节上分叉——但症状不同：长度档走偏 ⇒ **行数**变；解码档走偏 ⇒ 只有**断行位置**变（行数可能不变）。T39-2 / T39-3 分别盯这两档。

### 1.3 问三 = `\n` 判定的等价性：落点定案（★ 需求 D39-7 的**形态订正**）

**（1）「枚举法」为什么不可行（要写死 85 种形态）**

要让「只看首字节」或「只看几个固定形态」等价，就必须枚举「解码值 == `U+000A`」的全部字节形态。解码是「各字节低 6 位串接、前导贡献高位」⇒ 解出 `0x0A` 要求**除最后一字节外 payload 全为 0**、**最后一字节低 6 位 == `0x0A`**：

| 声称长 | 前导约束 | 中间字节（低 6 位 = 0） | 末字节（低 6 位 = 0x0A） | 形态数 |
|---|---|---|---|---|
| 1 | `== 0x0A` | — | — | 1 |
| 2 | `== 0xC0` | — | `∈ {0x0A, 0x4A, 0x8A, 0xCA}` | 4 |
| 3 | `== 0xE0` | `∈ {0x00, 0x40, 0x80, 0xC0}` | 同上 4 | 16 |
| 4 | `== 0xF0` | 前者 4 × 前者 4 | 同上 4 | 64 |

⇒ 共 **85** 种。把这 85 种写成谓词 = **把解码器抄第二遍**，且**随解码语义漂移**（解码一改，谓词无编译期保护）⇒ **否决枚举法**。

**（2）定案：调用既有解码，但换一个「不切子串」的入口**

需求稿 A2 写的是「加 `DecodeFirstCodepoint(const char*, size_t)` 重载」——**初设发现不够**：单趟还要**步进量**（`len`）。若只给解码值，`TextBox` 就得自己按前导字节算长度 ⇒ 前导判定出现**第二份**，正是 D39-G 要避免的双源。⇒ **形态订正为**：

```cpp
// Core/UTF8.h（★ 新增 1 个公共函数；既有签名一个不动）
/// @brief 就地解码 `data` 处的一个码点，并返回它**声称**占用的字节数
/// @details 与 `DecodeFirstCodepoint` 同语义（同一 `SequenceLength`、同一三分支拼装），
///          区别只有两点：① 不要求「以串尾为界」——`size` 即剩余长度；② **同时给出步进量**。
///          ★ 返回值**可能 > `size`**（末段截断的序列仍按前导声称的长度步进）——这是
///          `CodepointIndexToByteOffset` 的既有行为（`Core/UTF8.cpp:38-41`），单趟扫描必须逐位复刻。
/// @param data  首个字节（`size == 0` ⇒ 不读 `data`，写 `codepoint = 0` 并返回 0）
/// @param size  剩余可读字节数
/// @param codepoint [out] 解码结果；**剩余不足一个完整序列 ⇒ 回退为 `lead`**；★ **任何情况下都会被写**
///              （`size == 0` 写 0）——调用方**不需要**预置初值，公共 API 不留脚枪（v1.1 评审 P3①）
/// @return 前导声称的序列长度（1–4）；非法前导 = 1
[[nodiscard]] size_t DecodeCodepointAt(const char* data, size_t size, char32_t& codepoint);
```

既有 `DecodeFirstCodepoint(const std::string&)`（`Core/UTF8.h:30`）**保留签名**，实现改为转发（`UTF8.cpp`）：

```cpp
char32_t DecodeFirstCodepoint(const std::string& text){
	char32_t codepoint = 0;
	(void)DecodeCodepointAt(text.data(), text.size(), codepoint);   // size == 0 ⇒ 写成 0
	return codepoint;
}
```

★ **为什么不用 `std::pair` / 小结构体返回**：`pair` 的名字不带语义（`first`/`second` 谁是谁要查文档），自定义结构体要再占一个公共类型名。`[[nodiscard]]` + out 参数在**只多 1 个符号**的前提下把两件事都说清楚了。★ 备选（若你否决 out 参数）：`struct DecodedCodepoint{ char32_t value; size_t length; }` ⇒ 公共类型 +1，调用点更顺（structured binding），本稿**不推荐**但可切换。

**（3）交叉支复核（把「两口径分叉」的边界划清）**

| 串 | `DecodeCodepointAt` 解出 | 非折行支断行 | `DecodeUtf8`（`ECDI/src/Render/Utf8Decode.h:28-31`）解出 | 折行支断行（`TextWidget.cpp:243`） | 两口径 |
|---|---|---|---|---|---|
| `C0 8A` | `U+000A`（len 2） | ✔ 2 行 | `{U+000A}`（续字节 `0x8A` 合法） | ✔ 2 行 | **一致** |
| `E0 80 8A` | `U+000A`（len 3） | ✔ 2 行 | `{U+000A}` | ✔ 2 行 | **一致** |
| `F0 80 80 8A` | `U+000A`（len 4） | ✔ 2 行 | `{U+000A}` | ✔ 2 行 | **一致** |
| `C0 4A` | `U+000A`（len 2，`0x4A & 0x3F == 0x0A`） | ✔ 2 行 | `{U+004A}`（`0x4A` 不是续字节 ⇒ `ok=false` 跳 1 字节） | ✘ **1 行** | ★ **既有分歧** |
| `C0 0A` | `U+000A` | ✔ 2 行 | `{U+000A}`（跳过 `C0` 后 `0x0A` 自成码点） | ✔ 2 行 | **行表相同**（都是 2 行）；真正的差异是「**哪个字节**被当成 LF」——非折行在偏移 **0**（`C0 0A` 合成），折行在偏移 **1**（`0x0A` 单字节）⇒ 码点数**都是 1**（v1.1 评审 P2 订正：初稿误写「1 vs 2」），行表不可见该差别 |

⇒ **本 Phase 的跨支判据（T39-5）只能取上表前三行**（`0x8x` 全合法续字节的 overlong）：它们的**码点数与断行位置**在两支下都一致，可以断言「同一串两支行数相同」。`C0 4A` / `C0 0A` 这类「续字节不是 `10xxxxxx`」的形态，两支的**码点切分本来就不同**（`ByteOffsetToCodepointIndex` 按 `SequenceLength` 走，`DecodeUtf8` 按续字节合法性走）——这是 **Phase 26/38 就存在的既有差异**，不属本 Phase，**只进非折行 golden（T39-3），绝不作跨支判据**（R39-4）。

### 1.4 问四 = R2 的等价性（`MeasureText` 调用序列 + 机检口径）

现状 `TextMeasurer.h:85-87`：

```cpp
const std::size_t firstEnd = CodepointIndexToByteOffset(text, startCp + 1);                      // :85
const std::string firstCpText = text.substr(
    CodepointIndexToByteOffset(text, startCp), firstEnd - CodepointIndexToByteOffset(text, startCp));  // :86-87  ★ 同一表达式两次
```

改为（**三并为二**）：

```cpp
const std::size_t byteStart    = CodepointIndexToByteOffset(text, startCp);      // ★ 外提（也是 :100 / :113 的同一个值）
const std::size_t firstEnd     = CodepointIndexToByteOffset(text, startCp + 1);  // 保留（推不出来）
const std::string firstCpText  = text.substr(byteStart, firstEnd - byteStart);
```

**等价性论证**：`CodepointIndexToByteOffset` 是**纯函数**（不读写任何状态），故 `byteStart` 外提只改变**求值次数**，不改变**求值结果**；`text.substr(byteStart, ...)` 的实参因此与改前逐位相同 ⇒ `firstCpText` 相同 ⇒ 其后每次 `MeasureText` 的**入参、次数、顺序**全部不变（`firstCp` 一次 → 二分体内 `log2(totalCp − startCp)` 次 → 收尾一次）。**`MeasureText` 调用序列逐次相同** = A5 的判据基础。

★ 顺带清掉第三处：`:113` 的 `const std::size_t byteStart = CodepointIndexToByteOffset(text, startCp);` 与外提值**同表达式** ⇒ 直接复用（不改语义，只少一次 O(startCp) 全扫）。故函数体内换算**调用点 7 → 4**（按出现次数计，`:87` 一行两次；§5 C39-4 机检）。

### 1.5 问五 = 批 0 基准的采集口径（两张 golden 表）

**纪律**：批 0 在**改任何源码之前**跑，提交为独立一笔（沿 Phase 38 先例）；**采不到就不得动 R1 / R2**。占位期望先落成常量表，跑一次后**用实测回填**（不是手算值当真理——C38-5 教训）。

| 表 | 内容 | 消费者 | 落点 |
|---|---|---|---|
| **A** | `FitText` 默认体在 `FakeTextMeasurer`（8 宽 / 16 行高）下的 `(text, startCp, maxWidth) → TextFit{fitCp, width}`，8 组 | A5 / T39-1 | `TextBoxTests.cpp` 新常量表 `kFitGolden` |
| **B** | 非折行支行表的**改前**快照：`rowCount` + 每行 `(own.first, own.second, draw.first, draw.second)`，**13 行样本 = A6 十一类（overlong 展开 3 形态）** | A1 / A6 / T39-2 / T39-3 | 新常量表 `kLineGolden` |

**表 A 预置值（手算，仅作批 0 首次运行的对齐校验；实测不符 ⇒ 以实测为准并记详设）**

| # | text | startCp | maxWidth | fitCp | width | 说明 |
|---|---|---|---|---|---|---|
| 1 | `"abcdef"` | 0 | 100 | 6 | 48 | 全部放得下 |
| 2 | `"abcdef"` | 2 | 24.9 | 3 | 24 | 非整档宽（防「整除」巧合） |
| 3 | `"abcdef"` | 2 | 7.9 | 0 | 0 | 首码点放不下 |
| 4 | `"一丁丂"`（9 字节） | 0 | 16 | 2 | 16 | 多字节 + 恰好边界 |
| 5 | `"一丁丂"` | 1 | 100 | 2 | 16 | **`startCp` 非 0**（`:85`/`:86` 的判别面） |
| 6 | `"abcdef"` | 6 | 100 | 0 | 0 | `startCp >= totalCp` ⇒ 早退 |
| 7 | `""` | 0 | 100 | 0 | 0 | 空串 |
| 8 | `"\xC0\x8A"` | 0 | 100 | 1 | 8 | 脏字节（`totalCp` = 1） |

★ 第 5 组是**关键组**：R2 改的正是「`startCp` 的字节偏移」算几次，`startCp == 0` 时这一项恒为 0、改不改都一样 ⇒ **只用 `startCp == 0` 的样本，R2 的等价性测不到任何东西**（同型假绿，与 R39-3 一起记）。

**表 B 预置值（手算，`own` / `draw` 与测量器无关，`rowCount` = **`m_lines.size()`**——不是 `RowCountForTest()`，见下方 ★P0）**

| # | text（字节） | 类别 | rowCount | 逐行 `(own, draw)` |
|---|---|---|---|---|
| 1 | `""` | 空串 | 1 | `(0,0)-(0,0)` |
| 2 | `"\n"` | 仅 `\n` | 2 | `(0,0)` + `(1,1)` |
| 3 | `"\n\n"` | 连续 `\n` | 3 | `(0,0)` + `(1,1)` + `(2,2)` |
| 4 | `"ab\n"` | 尾 `\n` | 2 | `(0,2)` + `(3,3)` |
| 5 | `"a\r\nb"` | CRLF（`\r` **不断行**） | 2 | `(0,2)` + `(3,4)` |
| 6 | `"\x80\x8A"` | 非法首字节（`SequenceLength` → 1） | 1 | `(0,2)` |
| 7 | `"a\xC0"` | 截断续字节 | 1 | `(0,2)` |
| 8 | `"\xC0\x8A"` | overlong `U+000A`（2 字节） | 2 | `(0,0)` + `(1,1)` |
| 9 | `"\xE0\x80\x8A"` | overlong `U+000A`（3 字节） | 2 | `(0,0)` + `(1,1)` |
| 10 | `"\xF0\x80\x80\x8A"` | overlong `U+000A`（4 字节） | 2 | `(0,0)` + `(1,1)` |
| 11 | `"\xF0\x9F\x98\x80 a"` | 4 字节 emoji | 1 | `(0,3)` |
| 12 | `"abcdefghij"` | 纯 ASCII 长串 | 1 | `(0,10)` |
| 13 | `"一二三四五"`（15 字节） | CJK 长串 | 1 | `(0,5)` |

★ 第 8–10 组**必须**是改前快照（不是「我认为应该怎样」）：它们的断行是 `DecodeFirstCodepoint` 接受 overlong 的**既成事实**，R1 要守的就是它（§1.3）。

★★ **P0（v1.1 评审阻断项）· 行数不能用 `RowCountForTest()`** —— `TextBox::GetPreferredSize`（`TextBox.cpp:201`）只在 `m_text.find('\n') != npos || IsWrapActive()` 时才走 `MultilinePreferred()`（读 `m_lines.size()`），否则走单行 preferred。表 B 的 **#8/#9/#10 不含字面 `0x0A` 字节** ⇒ 非折行态下 `find('\n')` 落空 ⇒ `RowCountForTest()` 读回 **1**，而真值是 **2** ⇒ T39-2/3/5/6/7 全部**假红**；若反过来把 golden 改成实测的 1，**R39-3 的负对照就永远不会红**，本 Phase 唯一的假绿护栏当场作废。
⇒ **本稿改用「Center 顶内缩反解」通道**（零产品改动，逐字见 PD39-4 的 `LineCountForTest()`）：`GetTextTopInset()`（protected，`:264`）在 Center 态**直接读 `m_lines.size()`**（`TextBox.cpp:611`）且自带 `RecalculateLines()`；无头行高兜底 16（`:590`）⇒ `n = (areaH − 2·(inset − pad)) / 16`。★ 前置：`n × 16 <= areaH`（**内容高 > 文本区高**时 `max(0, ·)` 会把 offset 夹到 0、行数信息消失）⇒ 表 B 样本统一用**高 200** 的控件。
★ 不采用的两条通道（评审同结论）：`GetMaxScrollOffset()`（`TextBox.h:375`）与 `LineIndexFromCodepoint()`（`:361`）都在 private 段、`using` 编不过；**「直接读第 1 行」不行**——`LineRange` 是裸 `m_lines[i]`、无边界检查 ⇒ 越界是 UB 而不是干净的红；**caret 通道**（`GetCaret()` public，Down 键推 0→1）语义上更贴用户可见后果，但它只能证「≥ 2 行」，无法给出行数上界 ⇒ 作 T39-5 的**辅助**断言，不作主通道。

### 1.6 问六 = 耗时探针的形态（★ v1.1：加上 A2 / A3 的落点与「同体」纪律）

**结论：不入 CMake、不进 `src/Tests`；源码贴详设附录，命令写进 A7 记录。**

四条硬要求（缺一条这条闸门就不算收口）：

| # | 要求 | 理由 |
|---|---|---|
| 1 | **同一支探针、同一复刻体，改前改后各跑一次** | 需求 §1.3 表 1 的「现状循环」是**复刻体**（`probe.cpp` 里照抄 `:481-489`），**不是真的 `RecalculateLines`** ⇒ 改后若改测真函数，前后**不可比**。⇒ 探针里保留两份：`loop_current`（复刻体，永不删）与 `loop_single_pass`（改后骨架同样复刻） |
| 2 | **输出堆分配次数**（兑现 **A2**） | 全局 `operator new` 计数；A2 的判据是「非折行支内分配 == 0 且**不得退化**」，现状已为 0 ⇒ 探针的两个循环体都要打印分配数 |
| 3 | **输出四档读数 1k / 5k / 10k / 20k**（兑现 **A3**） | A3 判据是「n → 4n 的耗时比从 ≈ 16 降到 ≈ 4」；只测一档无法判二次性 |
| 4 | **命令 + flag + 机器**入档（兑现 **A7**） | 沿数字纪律；本机复算口径 = `g++ -std=c++20 -O2 -I ECDI/include probe.cpp ECDI/src/Core/UTF8.cpp -o probe.exe` |

★ 为什么强调「同体」：A3 若拿「改前的复刻体」比「改后的真函数」，差值里混进了「真函数多一次折行判分支 / 键守卫 / 成员访问」的噪声，倍率就不再是复杂度比值。**只承诺复杂度的 Phase，不能用一个跨口径的比值当证据。**

理由（不入 CMake）：① 探针要 20k 码点 × 多次重复（秒级），进测试会拖慢全量；② 它只依赖 `Core/UTF8.cpp`，两个 TU 就能编，比接进 CMake 更省事（Phase 28 / 38 同法）；③ 贴源码 + 附命令 ⇒ 可复现，不靠「我记得当时怎么跑的」。★ 若你要求「跑一次 ctest 就能复算」，那就要进 CMake（多一个 target + 一个 flag）——**本稿按不入处理**，你要改我再加。

### 1.7 问七 = 公共头代价 / 头注释同步 / 勿重开清单

- **编译代价（K12）**：`Core/UTF8.h` 与 `Render/TextMeasurer.h` 都是公共头 ⇒ 一次只改一处也会触发几乎全量重编（Phase 38 实测冷全量 35 min 量级）⇒ **A2′ + R1 + R2 必须同批编一次**，不得分两轮验。`Core/UTF8.cpp` / `TextBox.cpp` 是 .cpp，改它们只编一个 TU。
- **头注释同步**：`Core/UTF8.h:26-29` 现在写着「8.5.2：`RecalculateLines` 行扫描 / `GetWordBounds` 分词共用——第二个消费者出现，YAGNI 解除正式加回」。新增 `DecodeCodepointAt` 后这句**必须补一句**「★ Phase 39：`RecalculateLines` 非折行支改用 `DecodeCodepointAt`（就地解码 + 步进量）——`DecodeFirstCodepoint` 保留给按子串取码点的调用点」，否则下一个人会以为两者择一（R39-7）。★ **纯插入新行、不就地改写既有文字**：改动 diff 越干净，C39-5 的「零删除**代码**行」才越有意义（注释行允许删，但别拿它当借口混删代码）。
- **勿重开（逐项确认）**：① 折行支（`TextBox.cpp:459-474`）**零改动**；② `LineRange` / `LineDrawRange`（`:568-581`）**零改动**；③ `BuildDisplayText`（`:236-259`）**零改动**（N4）；④ `Core/UTF8` 的两个换算函数**零改动**（N3）；⑤ 不引入索引表 / 缓存 / 新文件布局；⑥ 不承诺「X 倍」。

---

## §2 决策定案 D39-A..H

| 定案 | 对应需求 | 结论 | 依据 |
|---|---|---|---|
| **D39-A** | D39-1 | **单趟推进**（`byte` + `cp` 双游标），不加索引表 | §1.1 的 L1–L4；索引表要随 revision 失效（多一处失效责任），而本循环是纯顺序访问 |
| **D39-B** | D39-2 | 循环本体**留在 `TextBox.cpp`**；不抽「码点游标」类 | `\n` 扫描是本类私有语义；`byte`/`cp` 两个局部变量够用（YAGNI） |
| **D39-C** | D39-3 | B（每行整串预备）**不并入**，新立 **#83** | 要动 `FitText` 接缝，风险档不同；混做会毁掉 R1 的「逐位对拍」判据 |
| **D39-D** | D39-4 | R2 **做**（与 R1 同批编） | 纯等价变换；`MeasureText` 调用序列不变（§1.4） |
| **D39-E** | D39-5 | 行为 + 机检进测试；耗时只做记录制 | 断时序必 flaky（Phase 23 先例） |
| **D39-F** | D39-6 | `count` 由单趟**顺带**得到（`cp`）；循环条件改 `byte < size` | L2；`GetCodepointCount()` 在该函数内**不再调用**（C39-1 顺带覆盖） |
| **D39-G** | D39-7 | **A2′ = `Core/UTF8.h` 新增 `DecodeCodepointAt`（带 out 参数、返回步进量）**，既有 `DecodeFirstCodepoint` 转发 | §1.3（2）：需求稿的「解码重载」**不够**（还缺步进量）；枚举法 85 种形态不可行；A1 局部复刻 ⇒ `SequenceLength` 双源 |
| **D39-H** | ★ **v1.1 新增**（评审 P0） | 行数读法：**`LineCountForTest()` = Center 顶内缩反解**，**不用** `RowCountForTest()` | `GetPreferredSize`（`TextBox.cpp:201`）只在「含字面 `\n` 或折行生效」时读行表 ⇒ 无字面 `0x0A` 的 overlong 样本读回 1（假红）；改 golden 迁就 1 则 R39-3 负对照永不红（假绿）。Center 通道在 `GetTextTopInset`（`:611`）**直接读 `m_lines.size()`**、零产品改动、无头可跑（T37-8 已证） |

★ **D39-G 是需求 D39-7 的形态订正**：需求稿 v1.1 写的「加 `DecodeFirstCodepoint(const char*, size_t)` 重载」，本稿改为「加 `DecodeCodepointAt(data, size, out)`」——**公共 API 增量仍是 +1**（不是 +2），因为它同时替代了「解码值」与「步进量」两个需求。需求稿**不回改**（保持评审基线），此订正记在 §8，**详设时统一回写需求 v1.2**。

---

## §3 方案 PD39-1..5（逐文件落点 + 代码草案）

### PD39-1 · `ECDI/include/ECDI/Core/UTF8.h` + `ECDI/src/Core/UTF8.cpp`（公共头 100 → 100；public API **+1**）

```cpp
// UTF8.h 追加（放在 :30 的 DecodeFirstCodepoint 之后）
[[nodiscard]] size_t DecodeCodepointAt(const char* data, size_t size, char32_t& codepoint);

// UTF8.cpp：新增实现 + 把 :55-80 改为转发
size_t DecodeCodepointAt(const char* data, size_t size, char32_t& codepoint){
	if (size == 0){
		codepoint = 0;                                // ★ 无条件写（调用方不必预置——P3①）
		return 0;
	}
	const unsigned char lead = static_cast<unsigned char>(data[0]);
	if ((lead & 0x80) == 0){
		codepoint = static_cast<char32_t>(lead);      // 1 字节 ASCII
		return 1;
	}
	const size_t len = SequenceLength(lead);     // ★ 同一匿名 namespace 的辅助（单源）
	if (len == 2 && size >= 2){
		codepoint = ((lead & 0x1F) << 6) | (static_cast<unsigned char>(data[1]) & 0x3F);
		return len;
	}
	if (len == 3 && size >= 3){
		codepoint = ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(data[1]) & 0x3F) << 6)
		          | (static_cast<unsigned char>(data[2]) & 0x3F);
		return len;
	}
	if (len == 4 && size >= 4){
		codepoint = ((lead & 0x07) << 18) | ((static_cast<unsigned char>(data[1]) & 0x3F) << 12)
		          | ((static_cast<unsigned char>(data[2]) & 0x3F) << 6)
		          | (static_cast<unsigned char>(data[3]) & 0x3F);
		return len;
	}
	codepoint = static_cast<char32_t>(lead);          // 截断 / 非法前导 / 连续字节：回退 lead
	return len;                                       // ★ 步进仍按声称长（可能 > size）——见 L1 注
}
```

★ **`noexcept` 不加**：与 `DecodeFirstCodepoint` / `CodepointIndexToByteOffset` 等同族函数一致（内部 `SequenceLength` 才标 `noexcept`）；只加 `[[nodiscard]]`（返回的步进量**不能丢**）。

★ **`data` 的形参类型用 `const char*` 而非 `std::string_view`**：能直接接 `m_text.data() + byte`，且不引入 `<string_view>`（该头现在只 include `<cstddef>` / `<string>`）。

### PD39-2 · `ECDI/src/Widget/TextBox.cpp`（**只改 `else{}` 块**：:475-491 → 骨架）

- 删：`:479`（`count`）、`:481-489`（旧 `for` **整块**）、`:490`（旧末段 push）
- 加：§1.1 的骨架（含 `DecodeCodepointAt` 调用）
- **不得触碰**：`:459-474` 折行支（含 `m_scrollOffsetX = 0.0f` 第三写点）、`:493-496` 空表兜底、`:498-499` 存键
- 该文件**新增 include**：无（`Core/UTF8.h` 已因 `:482` 处的换算函数被包含）——初设已核，`TextBox.cpp` 顶部已 `#include "ECDI/Core/UTF8.h"`

### PD39-3 · `ECDI/include/ECDI/Render/TextMeasurer.h`（R2：**调用点 7 → 4**，行为不变）

```cpp
			// ★ Phase 39 R2：`startCp` 的字节偏移是循环不变量（二分体内 `startCp` 恒定）⇒ 外提一次；
			//   收尾处（原 :113）同表达式复用 ⇒ 函数体内换算调用点 **7 → 4**（C39-4，按**出现次数**计）。
			const std::size_t byteStart   = CodepointIndexToByteOffset(text, startCp);
			// ★ `startCp + 1` 推不出来（省它需要序列长）⇒ 保留。
			// ★★ 本行及其后所有代码行**一律不留行尾 `//` 注释**：C39-4 的另一口径是
			//    「去注释行后的命中行数」，行尾注释会把整行一起从计数里删掉（v1.1 评审 P1①）。
			const std::size_t firstEnd    = CodepointIndexToByteOffset(text, startCp + 1);
			const std::string firstCpText = text.substr(byteStart, firstEnd - byteStart);
			if (MeasureText(font, firstCpText).width > (std::max)(0.0f, maxWidth)){
				return TextFit{ 0, 0.0f };
			}
			// …二分体：删 `:100` 的 byteStart，保留 `:101` 的 byteEnd（依赖 mid）…
			// …收尾：删 `:113` 的 byteStart，保留 `:114` 的 byteEnd（依赖 lo）…
```

### PD39-4 · `ECDI/src/Tests/TextBoxTests.cpp`（+7 用例 + 1 个读行数通道）

**① 新增 `TestableTextBox::LineCountForTest()`（评审 P0 的定案落点）**

```cpp
    /// @brief ★ 行表行数（**唯一可用通道**——`m_lines` 是基类 private，`RowCountForTest()` 只在
    ///        「含字面 `\n` 或折行生效」时才是行数：`GetPreferredSize`（TextBox.cpp:201）的单行支
    ///        会把 overlong 的 `U+000A` 样本读成 1 行 ⇒ 假红）
    /// @details 原理：Center 态 `GetTextTopInset()` = pad + max(0,(areaH − n·16)/2)（TextBox.cpp:611
    ///          直接读 `m_lines.size()`、自带 RecalculateLines）；无头行高兜底 16（:589）。
///          ★ 前置：`n × 16 <= areaH`（相等时 offset 恰为 0、公式仍成立；**只有内容高 > 文本区高**
///          才会被 `max(0, ·)` 夹住、行数信息消失）⇒ 调用方必须给足高度（本组样本统一高 200）。
    [[nodiscard]] size_t LineCountForTest(){
        TextBoxStyleOverride c; c.verticalAlign = VerticalAlignment::Center;
        SetStyle(c);                                        // Top 分支会早退、不碰行表
        const float inset = GetTextTopInset();               // 内含 RecalculateLines()
        const float areaH = static_cast<float>(GetHeight()) - Padding() * 2.0f;
        TextBoxStyleOverride t; t.verticalAlign = VerticalAlignment::Top;
        SetStyle(t);                                        // 还原：不污染同用例后续的绘制断言
        return static_cast<size_t>((areaH - 2.0f * (inset - Padding())) / 16.0f + 0.5f);
    }
```

★ 三个数都是精确二进制浮点（16 / 200 / 0）⇒ 反解无舍入误差，`+0.5` 只是把整数落回整数。
★ **不用**的两条：`GetMaxScrollOffset()`（`TextBox.h:375`）/ `LineIndexFromCodepoint()`（`:361`）在 private 段，`using` 编不过；「直接读第 i 行」在 `i` 越界时是 **UB**（`LineRange` 是裸 `m_lines[i]`），不是干净的红。

**② 常量表与用例（复用既有夹具，不新增其它夹具）**

- `kFitGolden`（表 A，8 组）+ `kLineGolden`（表 B，13 行；`rowCount` 字段注明 = `m_lines.size()`）
- 七条用例函数 T39-1..7；T39-2/3/4/6 共用一个 `ExpectLines` helper（`rowCount` 走 `LineCountForTest()`，逐行走 `OwnForTest` / `DrawForTest`）
- **复用**：`TestableTextBox`（`ResolveMeasurer` 覆写 ⇒ 无头可测折行）、`OwnForTest`、`DrawForTest`、`Padding()`；**不新增**其它夹具（`LineCountForTest()` 是唯一新增的测试中转）

### PD39-5 · 机检 C39-1..6（见 §5）

---

## §4 测试 T39-1..7（531 → **538**，+7）

| # | 用例 | 断言 | 为什么它必须存在 |
|---|---|---|---|
| **T39-1** | `A5 等价性`：表 A 的 8 组 `(text, startCp, maxWidth)` → `TextFit` | `fitCp` / `width` 与 `kFitGolden` 逐位相等 | R2 是无判据的等价变换，只有 golden 能守；**#5 组（`startCp != 0`）是唯一能测到 R2 的组** |
| **T39-2** | `A6 十一类样本`（非折行）：表 B 全 **13 行** | `LineCountForTest()` == golden 行数 + 每行 `(own, draw)` 逐位 | A1 的主体；`\n` 不属任何一行 + 尾部空行都在这一条里 |
| **T39-3** | `overlong U+000A 专项`：表 B 的 #8/#9/#10 三档（2/3/4 字节）+ `C0 4A` | 行表与改前 golden 相同（**断行 ⇒ 2 行**） | §1.3 的洞就长在这三档上；★ 这三条**正是** `RowCountForTest()` 读成 1 的那三条（P0）——负对照见 R39-3 |
| **T39-4** | `\n` 边界 + 尾截断：`""` / `"\n"` / `"\n\n"` / `"ab\n"` / `"a\r\nb"` / `"a\xC0"` | 行表 + 末行 `ownedEnd == GetCodepointCount()` | `\r` 不断行、尾部空行、`byte` 越过 `textSize`（`"a\xC0"`）三件事 |
| **T39-5** | `跨支对照`：`C0 8A` / `E0 80 8A` / `F0 80 80 8A` 三串，折行态 vs 非折行态 | ① 两支 `LineCountForTest()` 相同（== 2）；② **辅助**（评审建议）：Down 键后 `GetCaret()` 由 0 → 1（`GetCaret` 是 public，`:129`）——用户可见后果口径 | 需求 §7-3 的「交叉支证据」要一条用例钉住；★ 只许用这三串（§1.3 表）；★ 行数**必须**走 `LineCountForTest()`——非折行态 `RowCountForTest()` 会给 1、折行态给 2，「相同」必然不等（P0 的原始症状） |
| **T39-6** | `两口径一致性`：表 B 全样本（行数走 `LineCountForTest()`） | ① `draw.second == own.second`（**非折行支无软断点可裁 ⇒ 两口径逐位相等**）；② 非末行 `own.second + 1 == 下一行 own.first`（**硬行**：`\n` 无主，行末 `ownedEnd` 恰为该 `\n` 的码点索引） | Phase 38 的 A-15 语义在**非折行支**上重写后仍成立（`ownedEnd` 由 `cp` 给出）；★ ② 的 `+1` 是「`\n` 不属任何一行」的唯一机读形态 |
| **T39-7** | `count 一致性`：表 B + 2 条 20k 长串（CJK / ASCII），单趟累计 `cp`（`n = LineCountForTest()` 后取末行 `ownedEnd`）== `GetCodepointCount()` | 相等 | 直接验证 L2；`GetCodepointCount()` 在该函数内已不调用，但仍是**外部口径**，用它做交叉校验最省事 |

★ **不做的测试**：不断言耗时（D39-E）；不测 20k 的行表内容（太长，只测 count）。

---

## §5 契约 C39-1..6（每条附机检，命令已实跑）

| # | 契约 | 机检命令（两段式，函数体口径） | 今日基线 → 目标 |
|---|---|---|---|
| **C39-1** | `RecalculateLines` 函数体内**零换算入口**：`CodepointIndexToByteOffset` / `ByteOffsetToCodepointIndex` / `GetCodepointCount` 三者皆 0 | `awk '/^void TextBox::RecalculateLines/,/^}/' ECDI/src/Widget/TextBox.cpp` → `grep -cE "CodepointIndexToByteOffset\|ByteOffsetToCodepointIndex\|GetCodepointCount"` | 今日 **3**（`:482` / `:483` / `:479`）→ **0**。★ v1.1 评审 P1：初稿只 grep 第一个函数，**覆盖不到** `:479` 的 `GetCodepointCount()`（它内部调 `ByteOffsetToCodepointIndex`）⇒ 模式必须并上这三个名字 |
| **C39-2** | 函数体内**零** `substr` | 同上输出 → `grep -c ".substr"` | **1** → **0** |
| **C39-3** | 函数体内**恰好一次** `DecodeCodepointAt`、**零次** `DecodeFirstCodepoint` | 同上输出 → `grep -c "DecodeCodepointAt("` / `grep -c "DecodeFirstCodepoint("` | 今日 **0 / 1** → 改后 **1 / 0** |
| **C39-4** | `FitText` 默认体内换算**调用点 7 → 4**（按**出现次数**，不是行数） | `awk '/virtual TextFit FitText\(const Font& font/,/^\t\t}$/' ECDI/include/ECDI/Render/TextMeasurer.h` → `grep -v "[[:space:]]//"` → `grep -o "CodepointIndexToByteOffset(" \| wc -l` | 今日 **7**（`:85` 1 + `:87` **2** + `:100` 1 + `:101` 1 + `:113` 1 + `:114` 1）→ **4**；含注释行 **8 → 5**。★ v1.1 评审 P1：初稿写「6」是**命中行数**（`:87` 一行两次），且 `grep -v` 会连**行尾注释**一起删整行 ⇒ 改后按行数算只有 3 ⇒ **两个口径必须都是出现次数**，且 PD39-3 的代码行一律不留行尾注释 |
| **C39-5** | `Core/UTF8.h` **零删除代码行**：`DecodeFirstCodepoint` 声明行逐字节不变 | `git diff -U0 -- ECDI/include/ECDI/Core/UTF8.h` 的 `-` 行中**不得出现该声明**（允许 `-` 行是注释行） | v1.1 评审 P1：初稿写「只允许 + 行」与 R39-7 的**头注释同步**自相矛盾（就地改一个字就出 `-` 行）。⇒ 收窄为「零删除**代码**行」，并把 R39-7 的要求写死为「**纯插入**新注释行，不就地改写 `:26-29`」 |
| **C39-6** | 注释里无退役标识符；用例数 538 | ① `grep -n "m_lineStarts\|m_needsLineRecalc" --include=*.cpp --include=*.h ECDI/` == 空；② 用例数取运行器汇总行的 **`Tests: N`** 字段（`TestFramework.cpp:169-171`） | 空 / **531 → 538**。★ v1.1 评审 P1：初稿把汇总字段写成 `Total:`，实际是 `Tests: N  Passed: P  Failed: F`。★ 也**不用 grep 数**：`.Add("` 全仓 541 处、`GetTestRegistry().Add(` 525 处，与实际 531 差 10 ⇒ grep 口径不可靠 |

---

## §6 影响面

| 项 | 预估 |
|---|---|
| 源码 | `ECDI/include/ECDI/Core/UTF8.h`（+1 声明）· `ECDI/src/Core/UTF8.cpp`（+1 函数体，`DecodeFirstCodepoint` 改转发）· `ECDI/src/Widget/TextBox.cpp`（`else{}` 块）· `ECDI/include/ECDI/Render/TextMeasurer.h`（R2，三处调用点）· `ECDI/src/Tests/TextBoxTests.cpp`（+7 用例 + 1 个 `LineCountForTest()` 中转 + 2 张常量表） |
| 公共头 | **100 → 100**（无新增头） |
| public API | **+1**（`DecodeCodepointAt`；`DecodeFirstCodepoint` 签名不变） |
| CMake | **0**（无新增源文件；测试文件已在册） |
| 用例 | **531 → 538**（+7） |
| docs | **199 → 200**（本稿；详设再 +1 → 201） |
| 机检 | **+6**（C39-1..6） |
| 编译批次 | **一批**：A2′ + R1 + R2 同时落地（两个公共头一起重编，K12） |

---

## §7 风险 R39-1..10

| # | 风险 | 处置 |
|---|---|---|
| **R39-1** | `byte` 越过 `size` 后误用 `data + byte`（UB） | 骨架保证 `data + byte` **只在 `byte < size` 时求值**（循环条件先判、循环体内不再重算指针）；详设给逐行 diff 复核 |
| **R39-2** | 误改折行支（`:459-474`） | diff 应只落在 `else{}` 块 + 删 3 行；T38-1..18 全绿是护栏 |
| **R39-3** | 「用只看首字节的实现也能过」= **假绿** | 负对照：把 `ch == U'\n'` 临时改成 `data[byte] == '\n'`，T39-3（`C0 8A` 三档）**必须红**；红了才算判据有效（沿 Phase 23/38 负对照纪律）。★ **同型第二例**：A5 若只用 `startCp == 0` 的样本，R2 的等价性同样测不到任何东西（§1.5 第 5 组就是为此设的） |
| **R39-4** | 把 `C0 4A` 之类**既有分歧**当跨支判据 ⇒ 假红 | 跨支只用 §1.3 表前三行（`0x8x` 续字节全合法）；`C0 4A` 只进非折行 golden |
| **R39-5** | 两个公共头分两轮编（35 min × 2） | A2′ 与 R2 **同批** |
| **R39-6** | `DecodeCodepointAt` 跨 TU 调用开销（`UTF8.cpp` 与 `TextBox.cpp` 不同 TU，无 LTO 不内联） | 20k 码点 × ~2–5 ns ≈ **0.04–0.1 ms**，相对 257 ms 可忽略，A3 的 n→4n 比不受影响；★ 若详设实测单趟 > 0.2 ms，则把该函数改头内 inline（代价 = `SequenceLength` 上提，+1 公共符号）——**不在初设做** |
| **R39-7** | `UTF8.h:26-29` 的头注释与新函数脱节 | PD39-1 同步**纯插入**一行；C39-5 的 diff 检查会看到 |
| **R39-9** | ★ **v1.1 新增（原 P0 残留风险）**：后续谁再把行数断言改回 `RowCountForTest()`，overlong 三条会**静默假红**（或反过来把 golden 改成 1 ⇒ 负对照永久假绿） | ① `LineCountForTest()` 的文档注释里把「为什么不能用 `RowCountForTest`」写死（PD39-4 已写）；② 表 B 的 `rowCount` 字段注明 `= m_lines.size()`；③ R39-3 的负对照**必须**在 `C0 8A` 上做——它同时也是「行数通道错了」的探测器（通道错 ⇒ 1 行 ⇒ 负对照不红） |
| **R39-10** | ★ **v1.1 新增**：探针跨口径 ⇒ A3 的倍率失真 | §1.6 第 1 条：改前改后**同一支探针、同一复刻体**；`loop_current` 永不删 |
| **R39-8** | 用例预算 538 超出 | 允许把 T39-4 并入 T39-2（同类断言），**不得减判据** |

---

## §8 修订记录与待裁定

- **v1.0（2026-10-29）** 初设落盘。七问逐题收口（§1）；决策 D39-A..G 定案（§2，含 **D39-G 对需求 D39-7 的形态订正**）；五个落点 PD39-1..5（§3）；七条用例 T39-1..7（§4，531 → 538）；六条机检 C39-1..6（§5，**命令均已实跑**，基线 2 / 1 / 6 / 57 行——★ v1.1 已改为按新口径的 **3 / 1 / 7**，见下条）；影响面（§6）；八条风险（§7，含**负对照**与**跨支边界**两条硬处置）。
- **v1.1（2026-10-29）外部评审（Qoder）修稿** —— 方案主体通过（L1–L4 与实码逐条对上、表 A 8 组与表 B 13 行手算全对、85 种形态算术对、~15 处行号无漂移）；**1 处 P0 阻断 + 4 处 P1 机检口径 + 2 处 P2 缺口 + 2 处 P3 建议**逐条落实：
  1. **★ P0（阻断）**：`RowCountForTest()` 从 preferred 反推，而 `GetPreferredSize`（`TextBox.cpp:201`）只在「含字面 `\n` 或折行生效」时才读行表 ⇒ 表 B 的 **#8/#9/#10（overlong，无字面 `0x0A`）** 在非折行态读回 **1**，真值 **2** ⇒ T39-2/3/5/6/7 **全假红**；若把 golden 迁就成 1，**R39-3 的负对照永久不红**（唯一假绿护栏作废）。⇒ 新增 **D39-H** + **`LineCountForTest()`**（Center 顶内缩反解，`GetTextTopInset` 在 `TextBox.cpp:611` 直接读 `m_lines.size()`，零产品改动、无头可跑，T37-8 已证），并在 **§1.5 / §4 / R39-9** 三处写死「行数 = `m_lines.size()`，不是 `RowCountForTest()`」。另记两条**否决**通道：private 段的 `GetMaxScrollOffset` / `LineIndexFromCodepoint`（`using` 编不过）、裸 `m_lines[i]` 越界是 UB；caret 通道降为 T39-5 的辅助断言。
  2. **P1①** C39-1 覆盖不到 `GetCodepointCount()`（它调 `ByteOffsetToCodepointIndex`，另一个函数）⇒ 模式并成三个名字，基线 **3 → 0**。
  3. **P1②** C39-4 两个口径都错：6 是**命中行数**（`:87` 一行两次，实际调用点 **7**），且 `grep -v` 会把带行尾注释的**整行**删掉（草案里 `firstEnd` 行就有尾注释 ⇒ 改后只剩 3）⇒ 改用 `grep -o | wc -l` 计**出现次数**（7 → 4，含注释 8 → 5），并规定 PD39-3 的代码行**不留行尾注释**。
  4. **P1③** C39-5「只允许 `+` 行」与 R39-7「改头注释」矛盾（就地改一个字必出 `-` 行）⇒ 收窄为「**零删除代码行**」，R39-7 写死「纯插入」。
  5. **P1④** 汇总行字段是 `Tests: N  Passed: P  Failed: F`（`TestFramework.cpp:169-171`），初稿误写 `Total:`。
  6. **P2①** §1.3 表第 5 行 `C0 0A` 的「码点数 1 vs 2」**不成立**（两支都是 1 个码点；真正的差别是「哪个字节被当成 LF」，偏移 0 vs 1，且行表不可见）——按事实纪律订正。
  7. **P2②** A2 / A3 在初设里没有落点 ⇒ §1.6 补齐四条硬要求：**同体**（改前改后同一支探针、同一复刻体，`loop_current` 永不删）· 输出**分配次数**（A2）· 输出**四档**（A3）· 命令/flag/机器入档（A7）。
  8. **P3①** `DecodeCodepointAt` 在 `size == 0` 时不写 out 参数是公共 API 脚枪 ⇒ 改为**无条件写 0**（转发版行为逐位不变，前置条件消失）。
  9. **P3②** 骨架里 `size`（全串）与 `DecodeCodepointAt` 形参 `size`（剩余）同名不同义 ⇒ 改名 **`textSize`**。
  10. **评审对两项待裁定的意见**：D39-G 形态订正**该批**（单趟既要解码值又要步进量）；N7 按「纯新增不算破」推进**合理**。⇒ 本版按此推进，仍等你一句话确认后我在详设回写需求 v1.2。

### 待你裁定（两项；Qoder 已给出倾向，仍等你一句话）

1. **N7 豁免**：`DecodeCodepointAt` 是**纯新增**公共函数（既有签名一个不动、零删除代码行）。若按 N7 字面「任何 public API 改动」则需豁免；本稿按「**新增**不算破 N7」推进。★ 评审意见：**合理**。
2. **需求 D39-7 的形态订正**：需求稿 v1.1 写的是「`DecodeFirstCodepoint` 加 `const char*` + `size_t` 重载」，本稿改为 **`DecodeCodepointAt(data, size, out)`**（一次调用同时给解码值与步进量）。若你坚持原形态，则 `TextBox` 还得自己算长度 ⇒ 前导判定双源（正是 D39-G 要避免的），**故本稿不推荐**。★ 评审意见：**该批**。确认后我在详设把需求回写成 v1.2。
