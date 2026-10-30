# Phase 38 详细设计 — TextBox 编辑器软折行（O2 兑现）

> **版本**：**v1.5**（2026-10-29——v1.1 独立对码复核；v1.2 批 0/1 回流；v1.3 批 2 回流 + 批 2/3 合并；v1.4 批 3 回流；v1.5 = **批 4 回流（全 18 条落地）+ 四条判据口径订正**，详见 §9）　**基线**：HEAD `6a3807b`（`docs(phase37): full-chain closure`）　**账目**：`roadmap-deferred.md` #77（v2.57）
> **上游**：需求稿 [`phase38-textbox-soft-wrap-requirements.md`](phase38-textbox-soft-wrap-requirements.md) **v1.2** · 初设稿 [`phase38-textbox-soft-wrap-preliminary-design.md`](phase38-textbox-soft-wrap-preliminary-design.md) **v1.2**（其审核稿 = [`phase38-textbox-soft-wrap-design-review.md`](phase38-textbox-soft-wrap-design-review.md)，审核对象为**初设 v1.0**）
> **下游**：实现（批 0..4，见 §6）· `tools/phase38_wrap_probe.py`（样本表**唯一**期望值来源）
> **五阶段定位**：需求确认 ✓ → 初步设计 ✓ → **详细设计（本稿）** → 实现 → 测试

> ★★ **本稿的性质**：把初设的**决策**（D38-A..I）与**落点**（PD38-1..9）翻译成**可直接落键的代码**。凡本稿给出代码骨架处，实现阶段**不得再自行设计**；若实现期发现骨架不可编译或与红线冲突，**先改本稿并登记 §9，再改码**——不允许「码改了、文档没跟上」。
>
> ★★ **锚点纪律**：本稿全部行号均为 **2026-10-29 当日**对 HEAD `6a3807b` 的 `grep -n` / `Read` 亲核结果。**初设 v1.2 的 14 处锚点漂移已在 §0 逐条登记并订正**，本稿一律使用订正后的值；实现期若再发现漂移，同样先登记再改。
>
> ★★ **样本表纪律**（初设头部制度的延续）：§3 全表 = `PYTHONIOENCODING=utf-8 python tools/phase38_wrap_probe.py` 的**逐字输出**，本稿不得手推、不得手改。用例（§4）的期望值一律回指 §3 的表行号。
>
> ★★★ **v1.1 订正摘要（三项实质，全部由 v1.0 落盘后的独立对码复核产出——不是新一轮评审；每项均先跑脚本/读源码再落稿）**：
> **① `ownedEnd` 的取值错了（`= b` 应为 `= nextScan`）⇒ 分隔空格"无主"，A3 在四类真实串上直接违反。** v1.0 沿用初设口径 `ownedEnd = lineEnd`（`emitLine` 第二形参 = 局部量 `b`），但引擎的 ⑦ 终化（`TextWidget.cpp:299-303`）会把 `b` 之后的连续空格**跳进一个不属于任何行的空隙**；而 `FindLastSoftBreak`（`:190-196`）的返回位置**语义不统一**——空格断点返回空格**之后**（空格归前行），CJK 断点返回字**之后、空格之前** ⇒ 后者紧跟空格时该空格无主。**脚本实测（`exec` 复刻引擎，本日复跑）**：`"一二三 四"@24` ⇒ `gap = 1` 而 `cps[3]` 是空格（非 `'\n'`）；`"一二三  四"@24` ⇒ `gap = 2`；`"一二三 \n四"@24` ⇒ `gap = 2`；`"abc   "`@40 ⇒ **末行 `ownedEnd = 5 ≠ count = 6`**（尾部空格串超出剩余容量 ⇒ 整段无主）。第一条违反 `gap==1 ⇔ cps[ownedEnd]=='\n'`、中间两条违反 `gap ∈ {0,1}`（`gap=2`）、第四条违反边界等式 `ownedEnd(last) == count`——四类**全部落在 harness 的中文聊天框场景**（CJK 后跟空格是高发形态，需求 E4 已把「编辑器必须可达」列为判据，但初设只在文字上承认、未落数据结构）。⇒ **订正** = `emitLine` 之后回填 `layout.lines.back().ownedEnd = nextScan`（**+1 行**，§2.1 ⑨）⇒ 切分（`startCp`/`cpCount`/`width`/`totalHeight`）**逐位不变**（红线② 结构性成立，脚本对 11 个既有样本逐字段比对确认），只有新字段取值变 ⇒ 复验 **11 既有 + 7 新增 = 18 项 A3 全绿**。连带：§2.1 八行 → **九行** · §3 新增 **S8 组** · T38-7 判据换形（无第三态）· 新增 **T38-18** · §7 新增 **R38-10** · 用例 530 → **531**。
> **② `m_scrollOffsetX` 的钉 0 落点不完备（D38-G）⇒ 从非折行态切入折行态时旧偏移残留。** v1.0 只在 `EnsureCaretVisible` 内钉 0，而 `SetWordWrap(true)`（`TextWidget.cpp:100-115`）只 `Invalidate()`、**不调** `EnsureCaretVisible`（全 25 个调用点里没有一个在 setter 之后）⇒ 首帧 `viewX = textX − 旧 offset` ⇒ 整段文本左移、左侧被 clip 吃掉，直到下一次键盘/鼠标操作才恢复（用户可见的重绘错位）。⇒ **订正** = 在 `RecalculateLines()` 的**折行重建分支**补**写点 3**（`m_scrollOffsetX = 0.0f;`）：判据一变（`SetWordWrap`/`SetSingleLine`/改宽/换字体）键必变 ⇒ 必重建 ⇒ 偏移必归零；时序上 `RecalculateLines()`（`:1356`）在 `viewX`（`:1366`）**之前** ⇒ **同帧修复**。连带 C38-7 计数 2 → **3**（2 在 `EnsureCaretVisible` + 1 在 `RecalculateLines`）、T38-11 判据强化。
> **③ `RecalculateLines()` 的测量器被取三次且解引用无判空** ⇒ 改为**一次采集**（`TextMeasurer* measurer = ResolveMeasurer();` + `wrap = WrapRequested() && measurer != nullptr`），与 §2.2 自己写下的「一次采集」纪律对齐（v1.0 违反了自己的纪律）。
> ★ **登记级三项**：`TextMeasurer.h` 实际路径是 `ECDI/include/ECDI/Render/`（**不是** `Widget/`——实现者按 §2.9 的裸文件名去找会扑空）· `LineKeySave` 的 `wrap` 形参**不进键** ⇒ MSVC `/W4` 下 C4100（建议 `(void)wrap;` 或去形参，本稿取后者）· 「扣了条槽但折行未生效」的理论态（`WrapRequested()` 真 / `IsWrapActive()` 假，仅「无头 + 无测量器 + wrap on」可达）入 **R38-10** 登记，不修。

---

## 红线（两条，任何批次都不得破）

| # | 红线 | 判别 |
|---|---|---|
| **①** | `SetWordWrap` 缺省 `false` ⇒ **全部 513 条既有用例零改动全绿**，且非折行态的**命令级读数逐位不变**（`DrawTextCommand.pos/text`、`DrawRectCommand.rect`、`PushClipCommand.rect`） | T38-1 / T38-13 / T38-17 非折行半段 + 513 存量 |
| **②** | `Label` 侧 `BuildTextLayout` 的输出（`startCp` / `cpCount` / `width` / `totalHeight` / 空格归属）**逐位不变**——编辑器语义只由**新字段 `ownedEnd` + 新参数 `text` + 新分支**承载 | T38-2 + C38-5 机检 |

★ **A1 的结构性保证（本稿反复依赖的三条）**：
1. 非折行支的行表由 `TextBox` **自建**（§2.2 的 `else` 分支），不经引擎 ⇒ `drawEnd == ownedEnd ==` 今日 `LineRange` 的 `−1` 口径值，**逐位相等**（§2.2 末给出五组等价性演算）。
2. `GetTextTopInset()` 的 `Top` 快路径（`TextBox.cpp:506-507`）**在惰性门之前返回** ⇒ 本稿对该门的改动（§2.7 第 1 行）在默认对齐下**零触碰**（D37-C 的结构性保证继续成立）。
3. 折行态的**四个**换源点（`GetPreferredSize` 的闸 / `OnPaint` 的 clip / `EnsureCaretVisible` 的钉 X / ★ v1.1 的 `RecalculateLines` 钉 0）**全部以 `IsWrapActive()`（或同源的 `wrap` 局部量）为条件**，`false` 时表达式与改前**同一棵语法树**——★ 第四个是 v1.1 补的（A-17），前三个在 v1.0 已有。

---

## §0 锚点复核与漂移登记（当日亲核；实现期以本表为准）

初设 v1.2 的行号有 **14 处漂移**（其中 1 处为「精度不足」而非「错」）。本稿全部订正，**不改初设正文**（初设已定稿，漂移在此登记即闭环——沿 Phase 37 的勘误先例）。

| # | 初设 v1.2 写的 | 当日实测 | 影响 |
|---|---|---|---|
| **A-1** | `BuildTextLayout` 函数体 `:202-309` | **`:202-310`**（`:310` 是收尾 `}`） | C38-4 的机检范围必须写 `:202-310`，否则漏掉最后一行 |
| **A-2** | `IsLineStartForbidden :137-156` / `IsLineEndForbidden :157-170` | **`:137-155`** / **`:158-168`**（`:157` 是 doc 注释行） | 仅注释级引用 |
| **A-3** | `IsCjkBody`「4 个区间」 | **5 个区间**（`:174-178`：`4E00-9FFF` / `3400-4DBF` / `20000-2FFFF` / `3040-309F` / `30A0-30FF`） | 文字订正 |
| **A-4** | 构造函数 `:57`、`CreateVerticalScrollBar()` 在 `:59` | 构造 `:58`、调用 **`:60`** | D38-D 的「条恒创建」论据行号 |
| **A-5** | `TextBox.h:258` 引 `TextBox.cpp:657-658` 作 `const_cast` 先例 | **已失效**——真实先例在 **`:510-511`**（`GetTextTopInset`）与 **`:679-680`**（`CalculateCaretPosition`） | ★ `TextBox.h:258` 的注释本身**就是待订正对象**（§2.3 注释清单第 2 条） |
| **A-6** | 「`SetStyle`（`:99-127`）只 `Invalidate()`」 | **两处都不对**：`TextBox::SetStyle` = `:100-118`、`TextWidget::SetStyle` = `:90-96` | D38-E 的「setter 置脏结构性不可达」论据行号 |
| **A-7** | `SetWordWrap :100-114` / `IsWrapLayoutActive :123-129` | **`:100-115`** / **`:123-130`** | C38-9 的 setter 全表 |
| **A-8** | `CancelComposition` 的 `RestoreSnapshot(snapshot)` 在 `:952` | **`:953`**（`:952` 是 `m_undoStack.pop_back()`） | C38-6 的越序点定位 |
| **A-9** | `ApplyTheme :75-99` / `SetSize :256-265` / `EnsureCaretVisible` 横向段 `:558-571` / `Home`+`End` `:1223-1241` / `GetTextTopInset` Top 快路径 `:504-507` / `GetPreferredSize` 引 `:195-202` | **`:75-98`** / **`:256-266`** / **`:559-570`**（`:562-570` 是 `if (Window*)` 块）/ **`:1223-1242`** / **`:506-507`**（`:504-505` 是注释）/ 函数起 **`:169`**，闸在 **`:195-196`**、基类调用 **`:199`**、`+2p` **`:200-201`** | 全部落点行号 |
| **A-10** | `Widget.h:204` = `Invalidate()` 的 no-op 依据 | **精度不足**：`:204-205` 是 `SetVisible` doc 块里**记述** no-op 的注释；**声明在 `Widget.h:294`**、**实现在 `Widget.cpp:585-593`**（`if (Window* window = GetWindow()){ window->Invalidate(); }`）。⇒ 无头 no-op 的**证据链**应引实现，注释只作旁证 | C38-9 / T38-16④ 的论据 |
| **A-11** | `GetTextAreaWidth` 命中「4 处代码 + 1 处注释」（C38-3） | **复核为真**：代码 4（`:569` / 定义 `:662` / `:693` / `:1369`）+ 注释 1（`:523`） | 无漂移（登记为已核） |
| **A-12** | `m_lineStarts`「13 处读取 / 9 站点 + 3 写入 + **6 处注释**」 | 读取 13 ✓（`:461/465/468/479/480/481/515/529/565/684/778/1251/1370`）、写入 3 ✓（`:447/448/455`）、注释 **9 处**（`.cpp` 5：`:463/504/536/609/649`；`.h` 4：`:96/257/329/349`） | ★ 注释计数 6 → **9**；§2.4 的改名清单据此补齐 |
| **A-13** | `m_needsLineRecalc`「13 置脏 + 8 惰性门」 | **复核为真**：置脏 13（`:128/139/306/328/358/371/389/401/912/934/969/999/1084`）、复位 1（`:457`）、惰性门 8（`:510/537/611/650/679/770/1246/1355`）、注释 5（`.h:257/261/350`、`.cpp:536` 及门旁注） | 无漂移 |
| **A-14** | S5c 对照组「宽改 w16」（§8 v1.1 条③）vs 样本表写 `32`（§1.4） | **两处冲突**。脚本实算：w32 与 w24 **输出完全相同**（均 2 行 `L0[0,3)w24 own3 · L1[3,5)w16 own5`）⇒ 第二档取 24 **零判别力**；**w16 = 3 行**（`[0,2)/[2,3)/[3,5)`）⇒ 有判别力。★ 本稿定案 **S5c 两档 = 32 / 16**，与 §8 条③ 一致、订正 §1.4 表 | T38-7 的 18 次对账中 S5c 那一组 |

★ **本稿新增（脚本复算得到、初设未记）**：S3 的第二档（12 码点串 `"ab cd \nef gh"` @ w24）= **4 行** `L0[0,2)w16 own3 · L1[3,5)w16 own6 · L2[7,9)w16 own10 · L3[10,12)w16 own12`——初设 §1.4 只写了 draw 端点 `[0,2)/[3,5)/[7,9)/[10,12)`，**归属端点未记**（`own3/own6/own10/own12`）。⇒ §3 表补齐，T38-7 据此断言。

### 0.1 ★★★ v1.1 追加三条（v1.0 落盘后的独立复核；均为**本稿自身**的事实错误，不是初设的）

| # | v1.0 写的 | 当日实测（脚本 / 源码） | 影响 |
|---|---|---|---|
| **A-15** | §2.1 ⑤ `ownedEnd = lineEnd`（= `b`） | ★★★ **取值错**。`FindLastSoftBreak`（`TextWidget.cpp:190-196`）返回位置语义不统一：空格断点返回空格**后一位**（`is_soft_break_before(cps[b-1])` 命中 `0x20` ⇒ `b` 越过空格）、CJK 断点返回字**后一位**（命中 CJK ⇒ `b` 停在字后、**空格之前**）⇒ 后者紧跟空格时，`:301` 的跳空格把该空格跳进「不属于任何行」的空隙。脚本实算：`"一二三 四"@24` ⇒ `(0,3,own3)` + start4 ⇒ **`gap = 1` 而 `cps[3] = ' '`**；`"abc   "`@40 ⇒ **末行 `own = 5 ≠ count = 6`** | A3 的 `gap ∈ {0,1} ⇒ gap==1 ⇔ '\n'` 与边界等式 `ownedEnd(last)==count` **同时被违反**；且这些空格在编辑器里**不可点击、不可选中**（`LineRange` 与 `LineDrawRange` 都不含它）= 需求 E4 点名的缺陷**未被数据结构接住** ⇒ 订正为 `ownedEnd = nextScan`（§2.1 ⑨），新增 S8 组 / T38-18 / R38-10 |
| **A-16** | §4 T38-15 ② 「重建一次时 **行数 == `fitCalls`**」 | ★★ **判据在含 `\n` 的样本上必假红**：空段走 `:257-259` 的 `continue` 分支，**不调 `FitText`** ⇒ `"ab\n"` 是 2 行但 `fitCalls == 1`；`"a\n\nb"` 是 3 行但 `fitCalls == 2` | 线性判据必须**限定样本**为「无 `\n` 的单段串」（如 S6 `"abcdefgh"`），否则实现期会误判为「超线性放大」而回退优化 |
| **A-17** | §2.2 骨架「一次采集」（`wrap` / `wrapWidth` / `hasMeasurer`） | ★ 实际取了**三次** `ResolveMeasurer()`：`IsWrapActive()` 内 1 次 + `hasMeasurer` 1 次 + `BuildTextLayout(*ResolveMeasurer(), …)` 1 次，且解引用处**无判空**（只靠逻辑保证非空） | 违反本稿自订纪律；且 `ResolveMeasurer()` 是 **virtual 非 noexcept**（`TextWidget.h:83`）⇒ 三次虚调用出现在**每帧 `OnPaint → SyncScrollBar → RecalculateLines`** 路径上 ⇒ 改为一次采集（§2.2 已订正骨架） |

★ **复核方法（留痕）**：`A-15` 由 `exec(open('tools/phase38_wrap_probe.py').read()[:cut])` 载入脚本的 `build_layout`，对 11 个既有样本 + 7 个新样本跑「旧 `ownedEnd = b`」与「新 `ownedEnd = nextScan`」两版对账；`A-16`/`A-17` 由 `Read` 直读 `TextWidget.cpp:252-305` 与 v1.0 骨架自身比对。

---

## §1 定案的接口面（逐字签名）

### 1.1 `TextWidget.h` — 可见性下移（protected 面 +4）

```cpp
	// ── ★ Phase 38：以下四件由 private 下移到 protected（第二个真实消费者 = TextBox；D38-A ⓐ）──
protected:

	struct TextLine{
		std::size_t startCp = 0;    ///< 行首码点索引（**绝对**——相对全串）
		std::size_t cpCount = 0;    ///< 本行码点数（**已做尾部空白裁剪**——D29-Ⅰ ⑥；★ 口径不变 = 红线②）
		float       width   = 0.0f; ///< 本行宽（DIP）；★ **TextBox 非折行支恒 0.0f，本 Phase 无消费者**
		std::size_t ownedEnd = 0;   ///< ★ Phase 38 新增（**声明在末尾**——聚合初始化序不破既有三件）：
		                            ///<   归属终点（含软断点分隔空格、不含硬断 `\n`）。
		                            ///<   `TextWidget` 自身**不读**它（C38-5）；编辑器的可达性/选区/命中用它。
	};

	struct TextLayout{
		std::vector<TextLine> lines;
		float totalHeight = 0.0f;
	};

	/// @brief 断行布局（★ Phase 38：**+`text` 参数**——编辑器传显示串，`Label` 传 `m_text`）
	/// @param text 参与布局的串（★ 折行**只吃显示串**：C38-4；`Label` 侧传 `m_text` ⇒ 行为逐位不变）
	[[nodiscard]] TextLayout BuildTextLayout(TextMeasurer& measurer,
	                                        const std::string& text, float maxWidth) const;

	/// @brief 文本版本（★ Phase 38：`m_textRevision` 保持 private，派生类经此读——TextBox 键维 1）
	[[nodiscard]] std::size_t TextRevision() const noexcept{ return m_textRevision; }

private:   // ← 原 `:99` 的 private 区**下移到此**，其余成员与字段全部保持 private
```

**保持 private（给了就是扩面）**：`DoMeasureText` · `GetTextLayout` · `IsWrapLayoutActive` · `BlockWidth` · 全部 `m_pref*` / `m_wrap*` 缓存字段 · `m_textRevision` · `m_wordWrap`。

★ **`SetWordWrap` 注释扩写（PD38-8 / K12④）**：`TextWidget.h:61-70` 的 doc 块追加一段——`true` 的语义现覆盖两类消费者：`Label`（宽 = 控件宽、逐行发射、裁剪口径）与 `TextBox`（折行宽 = `GetTextAreaWidth()`、**归属口径可达**、`m_scrollOffsetX` 钉 0、clip 取控件内边界、`m_singleLine` 时不生效）；★ 并写明**回滚边界**：`TextBox` 上折行**不依赖 `GetWindow()`**（判据是 `ResolveMeasurer()`），无窗口即可重排。

### 1.2 `TextBox.h` — 新增 protected（+3）与私有键字段

```cpp
protected:
	/// @brief 折行是否被**请求**（纯配置，`noexcept` ⇒ 可进 `GetTextAreaWidth()` 的 `noexcept` 体）
	/// @details ★ 分层理由（D38-C）：`GetTextAreaWidth() const noexcept` 只能调 `noexcept`；
	///   而 `ResolveMeasurer()` 是 **virtual 非 noexcept**（`TextWidget.h:83`）⇒ 两层判据**不可合并**。
	[[nodiscard]] bool WrapRequested() const noexcept{ return IsWordWrap() && !m_singleLine; }

	/// @brief 折行是否**生效**（= 请求 ∧ 有测量能力）
	/// @details ★ **不含** `GetWindow()`（无头可折行，K6）· ★ **不含**「折行宽 > 0」（D38-D：
	///   含它则 `0 < W ≤ 2p+14` 窄带内 preferred 落回基类 wrap 支 ⇒ 宽度棘轮照旧）。
	[[nodiscard]] bool IsWrapActive() const;

	/// @brief 行**绘制**区间 `[startCp, startCp + cpCount)`（裁剪口径——软断点行尾空格不发射）
	/// @details ★ 唯一消费者 = `OnPaint` 的行文本 `DrawText`（C38-1）；其余一律 `LineRange`（归属口径）。
	[[nodiscard]] std::pair<size_t, size_t> LineDrawRange(size_t lineIndex) const;

private:
	std::vector<TextLine> m_lines;   ///< ★ 视觉行表（取代 `m_lineStarts`；**行数 = `size()`**，含尾部空行）

	// ── 10 维自比键（D38-E；★ 无 `singleLine` 维——被维 8 `wrapRequested` 严格覆盖）──
	bool        m_linesValid = false;
	std::size_t m_linesRevision = 0;              // 维 1
	Font        m_linesFont{};                    // 维 2/3/4（size / family / weight）
	int         m_linesDpi = 0;                   // 维 5（无窗口 = 0，**不短路**）
	float       m_linesWrapWidth = 0.0f;          // 维 6
	bool        m_linesHasMeasurer = false;       // 维 7
	bool        m_linesWrapRequested = false;     // 维 8（含 `!m_singleLine` 分量）
	EchoMode    m_linesEcho = EchoMode::Normal;   // 维 9
	bool        m_linesComposing = false;         // 维 10（三项——越序点见 C38-6）
	std::size_t m_linesCompStart = 0;
	std::size_t m_linesCompLen = 0;

	/// @brief 键比对 / 存键（★ 动态量由 `RecalculateLines()` **一次采集后传入**，
	///        两者共用同一份 ⇒ 不存在「比对的宽与存的宽不同」）
	/// @details ★ v1.1（A-17 的同类清理）：**去掉 `bool wrap` 形参**——它从不进键
	///   （`wrap == WrapRequested() && hasMeasurer`，被维 7 + 维 8 严格覆盖），
	///   留着只有两个后果：MSVC `/W4` 的 C4100，以及让读者以为「有一个 wrap 维」。
	[[nodiscard]] bool LineKeyMatches(float wrapWidth, bool hasMeasurer) const;
	void LineKeySave(float wrapWidth, bool hasMeasurer);
```

**删除**：`std::vector<size_t> m_lineStarts;`（`:349`）· `bool m_needsLineRecalc = true;`（`:350`）。

★ **protected 面合计 +7**（基类下移 3：`TextLine`/`TextLayout`/`BuildTextLayout` + 基类新访问器 1：`TextRevision()` + 派生新 3：`WrapRequested()`/`IsWrapActive()`/`LineDrawRange()`）；`LineKeyMatches`/`LineKeySave` 是 **private**，不计入。★ **public API +0**、**公共头 100 → 100**、**CMake 0**。

### 1.3 `TextBox.h` 注释订正清单（**七处**，与代码同批落地；不得静默改码）

| # | 位置 | 现文 | 改后 |
|---|---|---|---|
| 1 | `:77-78` | 「③ 行数口径 = `\n` 数 + 1，与 `RecalculateLines` 同源」 | 「③ 行数口径 = **`m_lines.size()`**（视觉行数）——非折行态恰 = `\n` 数 + 1（逐位同改前）；折行态 = 引擎产出的视觉行数。**唯一真相源 = 行表**，`MultilinePreferred` 不再自数 `\n`」 |
| 2 | `:257-258` | 「若 `m_needsLineRecalc` 未清则自查重算（…`TextBox.cpp:657-658`）」 | 「Center 路径无条件调 `RecalculateLines()`（**键守卫**在体内，命中即返回）；`const_cast` 先例见 `TextBox.cpp:510-511` 与 `:679-680`」★ 顺带修 **A-5** 的失效引用 |
| 3 | `:261` | 「重算行起始缓存（扫描 `\n`；仅 `m_needsLineRecalc` 时调用——惰性）」 | 「重建视觉行表（**键守卫**：10 维自比命中 ⇒ 直接返回）；折行态走 `BuildTextLayout`，非折行态走 `\n` 扫描」 |
| 4 | `:264-265` | 「行号 → 该行码点区间 `[start, end)`（`end` 不含行尾 `\n`）」 | 全文替换为**归属口径**契约（初设 §1.1 四态表）：`end = ownedEnd`；软行 `ownedEnd(i) == start(i+1)`、硬行 `+1`、末行 `== count`、尾部空行 `drawEnd == ownedEnd == segStart` |
| 5 | `:329` | 「二分 `m_lineStarts`」 | 「二分 `m_lines[].startCp`（仍严格递增——软行 `==`、硬行 `+1`）」 |
| 6 | `:349` | 「行数 = `size()-1`」 | ★ **该行整行删除**（字段删除）；**口径冲突随之关闭**：新字段注释写「**行数 = `size()`**（含尾部空行）」 |
| 7 | `:352` | 「水平滚动偏移（跟手模式…）」 | 追加「★ 折行期**恒 0**（D38-G 单点钉在 `EnsureCaretVisible`）」 |
| 附 | `:96` | 「…不失效 `m_lineStarts` 会按旧缓存画成」 | 改名 `m_lines`（纯文字） |

---

## §2 逐文件逐函数改动清单

### 2.1 `ECDI/src/Widget/TextWidget.cpp` — **九行**落点（= 初设口径的「六处语义落点」+ v1.1 的归属回填；`Label` 输出零变化）

| # | 行 | 现状 | 改后 |
|---|---|---|---|
| ① | `:202` | `TextLayout TextWidget::BuildTextLayout(TextMeasurer& measurer, float maxWidth) const{` | `TextLayout TextWidget::BuildTextLayout(TextMeasurer& measurer, const std::string& text, float maxWidth) const{` |
| ② | `:210` | `const std::vector<char32_t> cps = DecodeUtf8(m_text);` | `… = DecodeUtf8(text);` |
| ③ | `:212` | `layout.lines.emplace_back(TextLine{ 0, 0, 0.0f });` | `… TextLine{ 0, 0, 0.0f, 0 });`（`ownedEnd = 0 == count` ✓ 四态「末行」） |
| ④ | `:228-230` | `CodepointIndexToByteOffset(m_text, lineStart)` / `(m_text, end)` / `m_text.substr(b0, b1 - b0)` | 三处 `m_text` → **`text`**（`emitLine` 是 `[&]` 捕获 ⇒ 参数可见） |
| ⑤ | `:232` | `layout.lines.emplace_back(TextLine{ lineStart, end - lineStart, width });` | `… TextLine{ lineStart, end - lineStart, width, lineEnd });`　★ `ownedEnd` 先填第二形参 `lineEnd`（= `b`），**随后由 ⑨ 回填为 `nextScan`**；★ 不是 `end`（那是裁剪后的 `drawEnd`） |
| ⑥ | `:258` | `layout.lines.emplace_back(TextLine{ segStart, 0, 0.0f });` | `… TextLine{ segStart, 0, 0.0f, segStart });`（尾部空行四态） |
| ⑦ | `:269` | `const TextFit fit = measurer.FitText(font, m_text, scan, limit);` | `… measurer.FitText(font, text, scan, limit);`　★ **P0-1 的落点**：这是决定「断在哪」的那次调用，漏改 = 索引按显示串、宽度按真实串 |
| ⑧ | `:333` | `m_wrapLayout = BuildTextLayout(measurer, static_cast<float>(width));` | `m_wrapLayout = BuildTextLayout(measurer, m_text, static_cast<float>(width));`（`Label` 侧传 `m_text` ⇒ 逐位不变） |
| ⑨ | ★ **`:303` 之后**（`emitLine(...)` 已 append、⑦ 终化已定稿 `nextScan`） | —（新增 1 行） | `layout.lines.back().ownedEnd = nextScan;`　★ **v1.1 订正（A-15）**：归属终点 = **下一行起点**，把 ⑦ 跳过的连续分隔空格一并收进本行（原 `ownedEnd = b` 在「CJK 断点 + 紧随空格」时让空格**无主**，`"abc   "` 甚至让末行 `ownedEnd < count`） |

```cpp
			emitLine(lineStart, b, hardEnd, fit.width);          // :297（原样，ownedEnd 暂 = b）

			// ⑥ ⑦ 终化：nextScan = lineEnd 后**跳过连续空格**（:299-303 原样保留）
			std::size_t nextScan = b;
			while (nextScan < segEnd && cps[nextScan] == U' ') { ++nextScan; }
			if (nextScan <= scan) { nextScan = scan + 1; }        // 防御：绝不零推进

			// ★★ Phase 38（v1.1 · A-15）：归属终点回填 = 下一行起点。**必须落在 :303 之后**，
			//    因为 nextScan 在这一步才定稿。软行由此恒有 ownedEnd(i) == start(i+1)（gap 0），
			//    硬行/段末仍是 segEnd（gap 1 且 cps[segEnd] == '\n'）⇒ A3 不再有第三态。
			layout.lines.back().ownedEnd = nextScan;

			scan = nextScan;
```

★ **为什么 `ownedEnd = nextScan` 而不是在 `emitLine` 里算**：`emitLine` 调用于两处——`:274`（整段放得下，`ownEnd` 本身就是 `segEnd`，无需回填）与 `:297`（软断行，`nextScan` 在其后两步才定稿）。回填写法只有 **1 行**、且**不碰 `emitLine` 的签名与内部**（`cpCount` / `width` / `fitEnd` 快路径全部零改动）⇒ 红线② 的结构性保证最强。★ 脚本两版对比已确认：11 个既有样本的 `(startCp, cpCount, width)` 与 `totalHeight` **逐字段相同**，仅 `ownedEnd` 在 CJK-空格类样本上变化。

★ **顺带修正 A3 判据的形态（v1.1）**：原判据 `gap ∈ {0,1} 且 (gap==1 ⇔ cps[ownedEnd]=='\n')` 隐含「`gap==1` 只可能是 `\n`」，**在 `ownedEnd = b` 下为假**（存在 `gap==1` 且 `cps[ownedEnd]==' '` 的第三态）。订正后判据收紧为 **`gap == 0`（软行）或 `gap == 1 ∧ cps[ownedEnd] == '\n'`（硬行），无第三态**——这比原判据**更强**（原判据在实际实现上会漏检 A-15 那类缺陷，因为它只在 `gap` 为 1 且非 `\n` 时才报，而 `"abc   "` 的末行缺陷根本不产生 `gap`，走的是边界等式）。

⇒ **函数体内（`:202-310`）`m_text` 命中由 4 行 5 处降为 0**（C38-4）。

★ **不得触碰**：
- `:396-398`（`DrawTextContent` 的发射：按 `line.startCp` / `line.cpCount` 从 **`m_text`** 取字节）——那是 `Label` 的**裁剪口径发射点**，改它即破红线②。
- `:308`（`totalHeight = lineHeight × lines.size()`）· `:316-321`（dpi）· `:323-328`（六元组键）· `:334-338`（存键）——`TextBox` 不用基类缓存，全部零改动。
- `:100-115`（`SetWordWrap`）· `:123-130`（`IsWrapLayoutActive`）——`TextBox` **不复用** `IsWrapLayoutActive`（第三条 `GetWindow() != nullptr` 把无头全挡，D38-D）。

### 2.2 `TextBox.cpp` — `RecalculateLines()` 重写（`:445-458` 整体替换）

```cpp
void TextBox::RecalculateLines(){

	// ★ 键的动态三分量**一次采集**，命中判定与存键共用同一份（不得各算一次——宽度可能在两次之间变）
	TextMeasurer* measurer  = ResolveMeasurer();                        // ★ v1.1（A-17）：一次采集（v1.0 调了三次）
	const bool    wrap       = WrapRequested() && measurer != nullptr;  // = IsWrapActive()，但不二次虚调用
	const float   wrapWidth  = wrap ? GetTextAreaWidth() : 0.0f;
	const bool    hasMeasurer = (measurer != nullptr);

	if (m_linesValid && LineKeyMatches(wrap, wrapWidth, hasMeasurer))
		return;   // ★ 命中 ⇒ 零 FitText / 零 MeasureText（A8 反断言 = T38-15）

	m_lines.clear();

	if (wrap){
		// 折行支：复用 Phase 29 引擎（D38-A ⓐ）。★ 喂**显示串**（C38-4 / K9）——
		//   `EchoMode::Normal` 下 `BuildDisplayText()` 是 `return m_text;`（`:232-233`）= 一次串拷贝，
		//   只发生在**键未命中的重建**；键命中路径零拷贝零测量（成本账见初设 PD38-4）。
		m_lines = BuildTextLayout(*measurer, BuildDisplayText(), wrapWidth).lines;

		// ★ v1.1（D38-G 补全 · 写点 3）：折行态水平偏移**恒 0**。落在这里而不是只落在
		//   `EnsureCaretVisible` —— 后者不被 `SetWordWrap` / `SetSingleLine` 触发（25 个调用点无一在 setter 后），
		//   从非折行态切过来时旧 offset 会残留 ⇒ 首帧 viewX = textX − 旧 offset ⇒ 整段左移且左侧被 clip。
		//   判据一变 ⇒ 键必变 ⇒ 必重建 ⇒ 必归零；且本函数在 OnPaint 的 viewX（:1366）之前（:1356）⇒ 同帧生效。
		m_scrollOffsetX = 0.0f;
	}
	else{
		// 非折行支 = 原 `\n` 扫描**逐位保留**（红线①）：每行 {start, cpCount, 0.0f, ownedEnd}
		const size_t count = GetCodepointCount();
		size_t start = 0;
		for (size_t i = 0; i < count; ++i){
			const size_t byte     = CodepointIndexToByteOffset(m_text, i);
			const size_t nextByte = CodepointIndexToByteOffset(m_text, i + 1);
			const char32_t cp = DecodeFirstCodepoint(m_text.substr(byte, nextByte - byte));
			if (cp == U'\n'){
				m_lines.push_back(TextLine{ start, i - start, 0.0f, i });   // `\n` 不属任何一行
				start = i + 1;
			}
		}
		m_lines.push_back(TextLine{ start, count - start, 0.0f, count });   // 末段（可为空 = 尾部空行）
	}

	// ★ 空表兜底**必需**：原实现恒 `push_back(0)`（`:448`）⇒ 表永不为空；
	//   漏掉本行则 `:778` / `:1251` / `:1370` 的 `size()-1` **下溢**（T38-1 第三组基准 = 空文本，专盯此处）
	if (m_lines.empty())
		m_lines.push_back(TextLine{ 0, 0, 0.0f, 0 });

	LineKeySave(wrapWidth, hasMeasurer);
	m_linesValid = true;
}
```

★ **A1 等价性演算（五组，实现后逐组复核）**——非折行支下 `LineRange(i)` / `m_lines.size()` 与改前**逐位相等**：

| 文本 | 改前 `m_lineStarts` → `LineRange(i)` | 改后 `m_lines` → `LineRange(i)` | `size()` |
|---|---|---|---|
| `""` | `[0]` → `(0,0)` | `[{0,0,·,0}]` → `(0,0)` | 1 = 1 ✓ |
| `"abc"` | `[0]` → `(0,3)` | `[{0,3,·,3}]` → `(0,3)` | 1 = 1 ✓ |
| `"ab\ncd"` | `[0,3]` → `(0,2)`,`(3,5)` | `[{0,2,·,2},{3,2,·,5}]` → `(0,2)`,`(3,5)` | 2 = 2 ✓ |
| `"ab\n"` | `[0,3]` → `(0,2)`,`(3,3)` | `[{0,2,·,2},{3,0,·,3}]` → `(0,2)`,`(3,3)` | 2 = 2 ✓ |
| `"\n\n"` | `[0,1,2]` → `(0,0)`,`(1,1)`,`(2,2)` | `[{0,0,·,0},{1,0,·,1},{2,0,·,2}]` → 同 | 3 = 3 ✓ |

⇒ `:515` / `:529` / `:778` / `:1251` / `:1370` 五个「读 `size()`」的消费点在非折行态**数值不变**；`LineDrawRange(i) == LineRange(i)`（`cpCount == ownedEnd − startCp`）⇒ `OnPaint` 的 `lineText` 也不变。

### 2.3 `TextBox.cpp` — 键的两个辅助（新增，紧接 `RecalculateLines` 之后）

```cpp
bool TextBox::LineKeyMatches(float wrapWidth, bool hasMeasurer) const{

	// ★ C29-10 手法：显式列键、入口每次判定——**不依赖任何 setter「记得清」**（D38-E）
	int dpi = 0;
	if (const Window* window = GetWindow()){
		dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
	}
	const Font& f = TextWidget::m_style.font.value;
	return m_linesRevision     == TextRevision()
	    && m_linesDpi          == dpi
	    && m_linesWrapWidth    == wrapWidth
	    && m_linesHasMeasurer  == hasMeasurer
	    && m_linesWrapRequested == WrapRequested()
	    && m_linesEcho         == m_echoMode
	    && m_linesComposing    == m_isComposing
	    && m_linesCompStart    == m_compositionStart
	    && m_linesCompLen      == m_compositionLength
	    && m_linesFont.size    == f.size
	    && m_linesFont.family  == f.family
	    && m_linesFont.weight  == f.weight;   // ★ Phase 36 K8-④ 同族账：漏加过一次
}

void TextBox::LineKeySave(float wrapWidth, bool hasMeasurer){

	int dpi = 0;
	if (const Window* window = GetWindow()){
		dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
	}
	m_linesRevision      = TextRevision();
	m_linesDpi           = dpi;
	m_linesWrapWidth     = wrapWidth;
	m_linesHasMeasurer   = hasMeasurer;
	m_linesWrapRequested = WrapRequested();
	m_linesEcho          = m_echoMode;
	m_linesComposing     = m_isComposing;
	m_linesCompStart     = m_compositionStart;
	m_linesCompLen       = m_compositionLength;
	m_linesFont          = TextWidget::m_style.font.value;
}

bool TextBox::IsWrapActive() const{
	return WrapRequested() && ResolveMeasurer() != nullptr;   // ★ 无 GetWindow()、无宽度合取项
}
```

★ `wrap` **不是键维**——它等于 `WrapRequested() && hasMeasurer`，被维 7 + 维 8 严格覆盖（键里不留可推导的冗余字段，与删 `singleLine` 维同一条纪律）。★ v1.1 订正：v1.0 保留该形参「只为让调用点显式表达同一次采集」，代价是 **C4100（`/W4`）** + 语义误导 ⇒ **已从两处签名删除**；「一次采集」由 `RecalculateLines()` 的三个局部量（`measurer` / `wrap` / `wrapWidth`）表达。
★ `dpi` 的算式与 `TextWidget.cpp:316-321` **逐字同**（`Widget` 上没有 `GetDpiScale()`——初设 P2-5 订正，本稿沿用）。
★ **`dpi` 维无判据**（无头恒 0、本仓无多屏夹具）⇒ 如实入账 **R38-2**，不为此新造夹具。

### 2.4 `TextBox.cpp` — 两个区间入口（`:476-484` 替换 + 新增）

```cpp
std::pair<size_t, size_t> TextBox::LineRange(size_t lineIndex) const{
	// ★ 归属口径（初设 §1.1）：`ownedEnd` 直取——**不再由下一行起点反推、`−1` 退场**（E2）
	const TextLine& l = m_lines[lineIndex];
	return { l.startCp, l.ownedEnd };
}

std::pair<size_t, size_t> TextBox::LineDrawRange(size_t lineIndex) const{
	// ★ 裁剪口径：本行**实际发射**的文本（软断点行尾空格不进命令——与 Label 侧 `:396-398` 同口径）
	const TextLine& l = m_lines[lineIndex];
	return { l.startCp, l.startCp + l.cpCount };
}
```

`LineIndexFromCodepoint`（`:460-474`，= **C1**）：三处改读 `m_lines`，**算法零改动**——
`:461` `if (m_lines.empty())` · `:465` `size_t hi = m_lines.size() - 1;` · `:468` `if (m_lines[mid].startCp <= cp)`。
（`startCp` 仍严格递增：软行 `ownedEnd == start(i+1)`、硬行 `+1` ⇒ 二分前提不破。）

### 2.5 `TextBox.cpp` — 宽度 / preferred / 钉 X / clip / setter（六处）

```cpp
// ① GetTextAreaWidth（:662-674）——谓词加「折行请求 ⇒ 常驻留条槽」（D38-C）
float TextBox::GetTextAreaWidth() const noexcept{
	float width = static_cast<float>(GetWidth()) - m_style.padding.value * 2.0f;
	// ★ Phase 38：折行**请求**态即常驻留槽（不看 IsVisible）——E3 的自指环由此摘除：
	//   本表达式的输入集 = {GetWidth, padding, barThickness, WrapRequested()}，
	//   **不含 barVisible、不含行数 n** ⇒ 唯一平凡不动点（初设 §1.2 独立性证明）。
	// ★ 让位量约束的是**布局宽**（折行宽 / 光标 clamp 宽），**不是 clip**——
	//   悬挂尾进槽是 D38-I 的**有意**行为；且默认主题轨道 alpha = 0（DefaultTheme.cpp:110）
	//   ⇒ 条**盖不住**文本，只有滑块会压住与之重叠的一段（K15③）。
	//   （★ 本段替换原 :667-668 的 v1.5「否则文本会钻到条底下…被条压住」——C38-3 注释级机检）
	if (m_vBar != nullptr && (m_vBar->IsVisible() || WrapRequested())){
		width -= static_cast<float>(m_vBar->GetThickness()) + ScrollBarInset(m_style.padding.value);
	}
	return width;
}
```
★ **原 `:669` 的 K9/D4「无循环依赖」注释一并删除**，换成上面的 E3 论证（C38-3 的注释级机检：改后该函数体内 `grep "钻到条下\|无循环依赖"` 命中 == 0）。

```cpp
// ② GetPreferredSize（:195-196 的闸）——★ 与 D38-D 同源，全宽域不进 :199
	if (m_text.find('\n') != std::string::npos || IsWrapActive())
		return MultilinePreferred();

// ③ MultilinePreferred（:204-229）——删第二次 \n 计数（:216-220），行数取行表
Size TextBox::MultilinePreferred() const{
	TextMeasurer* measurer = ResolveMeasurer();
	if (measurer == nullptr)
		return Widget::GetPreferredSize();          // 契约②：无测量能力 ⇒ 当前尺寸（逐位保留）

	const_cast<TextBox*>(this)->RecalculateLines();  // 键守卫：命中即零测量
	const size_t lines = m_lines.size();             // ★ 视觉行数 = 唯一真相源

	const float lineH = measurer->LineHeight(TextWidget::m_style.font.value);   // = L_pref（K13）
	const float pad   = m_style.padding.value;
	const float contentH = static_cast<float>(lines) * lineH + pad * 2.0f;
	return Size{ static_cast<float>(GetWidth()), static_cast<float>(std::ceil(contentH)) };   // 契约①③ 保留
}

// ④ EnsureCaretVisible（:559-570 横向段）——D38-G 单点钉 + 钉 0 显式化
	if (IsWrapActive()){
		m_scrollOffsetX = 0.0f;   // ★ 折行期恒 0（横向滚动与折行互斥；四个读点 :694/:781/:822/:1366 零改动）
	}
	else if (Window* window = GetWindow()){
		const size_t lineIndex = LineIndexFromCodepoint(m_caret);
		const std::string display = BuildDisplayText();
		const size_t startByte = CodepointIndexToByteOffset(display, m_lines[lineIndex].startCp);
		const size_t caretByte = CodepointIndexToByteOffset(display, m_caret);
		const float prefixWidth = window->GetTextMeasurer().MeasureText(
			TextWidget::m_style.font.value, display.substr(startByte, caretByte - startByte)).width;
		m_scrollOffsetX = ClampScrollOffsetX(m_scrollOffsetX, prefixWidth, GetTextAreaWidth());
	}
```
★ **对初设 PD38-6④ 的一处修正（v1.0 登记，v1.1 再补）**：初设写「`:569` 写点外包一层 `if (!IsWrapActive())`」——**只跳过写入不足以钉 0**（从非折行态切过来时旧值会残留）。本稿改为 `if (IsWrapActive()) { … = 0.0f; } else if (Window*) { … }`。
★★★ **但 v1.1 复核发现「两处写点」仍不完备（A-17 / D38-G 的实质缺口）**：这两个写点都在 `EnsureCaretVisible` 体内，而 **`SetWordWrap(true)` 不调 `EnsureCaretVisible`**——`TextWidget.cpp:100-115` 只置双缓存失效 + `Invalidate()`；`SetSingleLine`（本 Phase 补的 `Invalidate()`）同。全仓 25 个 `EnsureCaretVisible()` 调用点（`:260/310/332/362/375/393/405/420/428/436/916/938/973/1003/1021/1087/1143/1155/1170/1193/1271` …）**没有一个在 setter 之后**。⇒ 失败路径：非折行态把长文本光标推到行尾 ⇒ `scrollOffsetX = 200` ⇒ `SetWordWrap(true)` ⇒ 重绘 ⇒ `OnPaint` 首帧 `viewX = textX − 200` ⇒ **整段折行文本左移 200px、左侧 200px 被 clip 吃掉**，直到用户敲键/点击才恢复。
⇒ **补第三写点（落在 `RecalculateLines()` 的折行重建分支末尾，§2.2 已给骨架）**：判据一变 ⇒ 键必变（维 8 / 维 6）⇒ 必重建 ⇒ 必归零；且 `RecalculateLines()`（`:1356`）在 `viewX`（`:1366`）**之前** ⇒ **首帧即生效**，不存在「一帧错位」。
⇒ **不变式（写进 C38-7）**：设 `U ≡ (IsWrapActive() ⇒ m_scrollOffsetX == 0)`。写点 1（`EnsureCaretVisible` 的 `else if (Window*)` 支）只在**非折行态**执行 ⇒ 保持 `U`；写点 2（同函数折行支钉 0）⇒ 保持 `U`；写点 3（`RecalculateLines` 折行重建）覆盖**切换点**——切换必然换键、必然重建 ⇒ 恢复 `U`。三写点合一 ⇒ `U` 在每次重排后恒成立。
⇒ **C38-7 的机检计数随之订正（v1.1：2 → 3）**：`grep -n "m_scrollOffsetX *=" TextBox.cpp` 由 **1 处**（今日 `:569`）变为 **3 处** = **2 处在 `EnsureCaretVisible` 体内 + 1 处在 `RecalculateLines` 体内**（+ `.h` 字段初值 ⇒ 全仓 4 处）。「单点钉」的含义是**行表/光标两条路径各一处、且都不能省**，不是「全仓只有一句」。

```cpp
// ⑤ OnPaint 的 clip 宽（:1369）——D38-I：仅折行态换源
	// ★ Phase 38（D38-I）：折行态 clip 取**控件内边界**，不取 GetTextAreaWidth()——
	//   ⑤ 行首禁则（TextWidget.cpp:295）上提闭标点 ⇒ 行宽可 > 折行宽，而 scrollOffsetX ≡ 0
	//   ⇒ 若 clip 也取折行宽，悬挂字符**整字永久不可见**（S4 实测被裁 8px = 一个完整码点）。
	//   clip 是**发射期**参数、不是任何布局输入 ⇒ 初设 §1.2 的独立性证明不受影响。
	const float clipW = IsWrapActive()
		? static_cast<float>(GetWidth()) - m_style.padding.value * 2.0f
		: GetTextAreaWidth();                       // 非折行态：改前原式，逐位不变（红线①）
	ctx.PushClip(Rect{ textX, areaTop, clipW, GetTextAreaHeight() });

// ⑥ SetSingleLine（:162-167）——C38-9：改判据的 setter 必须请求重绘
void TextBox::SetSingleLine(bool singleLine){
	if (m_singleLine == singleLine)
		return;
	m_singleLine = singleLine;
	// ★ Phase 38（C38-9）：本开关现在是 WrapRequested() 的一个分量 ⇒ 切换后行表、内容高、
	//   条显隐全变。键只保证「被调用时算得对」，**不保证「该算时被调用」** ⇒ 必须 Invalidate()。
	//   （原文「键盘语义开关（Enter 不换行）——无需重绘」的判断在本 Phase 作废，一并改写。）
	//   ★ 条的重同步免费：OnPaint :1290 每帧 SyncScrollBar()。
	Invalidate();
}
```

### 2.6 `TextBox.cpp` — `OnPaint` 行循环（`:1376-1424`）：一对区间拆两个口径

```cpp
	for (int li = firstVisible; li <= lastVisible; ++li){
		const size_t lineIndex = static_cast<size_t>(li);
		// ★ Phase 38（C38-1）：绘制吃**裁剪**口径、选区/下划线求交吃**归属**口径。
		//   两口径的 startCp 同值 ⇒ startByte 共用一份。
		const auto [startCp, drawEndCp] = LineDrawRange(lineIndex);
		const size_t ownEndCp = LineRange(lineIndex).second;
		const size_t startByte   = CodepointIndexToByteOffset(textSource, startCp);
		const size_t drawEndByte = CodepointIndexToByteOffset(textSource, drawEndCp);
		const std::string lineText = textSource.substr(startByte, drawEndByte - startByte);
		const float lineY = textY + static_cast<float>(li) * lineH - m_scrollOffsetY;

		// ① 行内 Selection 高亮 —— 求交用**归属**口径（行尾分隔空格可被选中）
		if (HasSelection()){
			const size_t selMin = GetSelectionMin();
			const size_t selMax = GetSelectionMax();
			if (selMin < ownEndCp && selMax > startCp){
				const size_t hlStartCp = (std::max)(selMin, startCp);
				const size_t hlEndCp   = (std::min)(selMax, ownEndCp);
				/* … hlStartByte / hlEndByte / 两次 MeasureText / DrawRect 逐位保留 … */
			}
		}

		// ② 行文本绘制 —— 用**裁剪**口径的 lineText（唯一消费者，C38-1）
		if (!lineText.empty()){
			ctx.DrawText(Point{ viewX, lineY }, lineText,
				TextWidget::m_style.foreground.value, TextWidget::m_style.font.value);
		}

		// ③ 组合串下划线 —— 求交用**归属**口径
		if (m_isComposing && m_compositionLength > 0){
			const size_t compStartCp = m_compositionStart;
			const size_t compEndCp   = m_compositionStart + m_compositionLength;
			if (compStartCp < ownEndCp && compEndCp > startCp){
				const size_t ulStartCp = (std::max)(compStartCp, startCp);
				const size_t ulEndCp   = (std::min)(compEndCp, ownEndCp);
				/* … 逐位保留 … */
			}
		}
	}
```
★ 改动点**只有 5 处标识符**：`endCp` → `drawEndCp`（定义）/ `endByte` → `drawEndByte` / ①③ 两处求交的 `endCp` → `ownEndCp` / 新增 `ownEndCp` 定义。`:1382` 的 `lineY`、`:1398` 的 `DrawRect`、`:1403-1405` 的 `DrawText`、`:1420-1421` 的下划线矩形**表达式逐位保留**。
★ **非折行态**：`drawEndCp == ownEndCp`（§2.2 演算）⇒ 命令流与改前逐位相同（红线①）。

### 2.7 `TextBox.cpp` — 8 处惰性门 → 无条件调用（形态零改动，只删 `if`）

| 门（行） | 所在函数 | 改后 |
|---|---|---|
| `:510-511` | `GetTextTopInset() const` | `const_cast<TextBox*>(this)->RecalculateLines();`（★ `Top` 快路径 `:506-507` **在此之前返回** ⇒ 默认对齐零触碰） |
| `:537-538` | `EnsureCaretVisible()` | `RecalculateLines();` |
| `:611-612` | `SyncScrollBar()` | `RecalculateLines();` |
| `:650-651` | `OnMouseWheel()` | `RecalculateLines();` |
| `:679-680` | `CalculateCaretPosition() const` | `const_cast<TextBox*>(this)->RecalculateLines();` |
| `:770-771` | `CaretIndexFromPosition()` | `RecalculateLines();` |
| `:1246-1247` | `OnKeyDown()` Up/Down | `RecalculateLines();` |
| `:1355-1356` | `OnPaint()` | `RecalculateLines();` |

⇒ 每处的成本 = 一次 10 维键比对（无测量、无分配）。三处旁注（`:536` / `:609` / `:649`）的「惰性重算 / 依赖 `m_lineStarts`」措辞一并改写为「键守卫重建 / 依赖 `m_lines`」。

### 2.8 `TextBox.cpp` — 13 处置脏删除 + 9 处读取改名

**删除**（`m_needsLineRecalc = true;` 共 13 行，含其行尾注释）：`:128` `:139` `:306` `:328` `:358` `:371` `:389` `:401` `:912` `:934` `:969` `:999` `:1084`；**另删** `:457` 的 `= false` 复位（随 §2.2 整体替换消失）。
★ **删除的前置证据（当日复核，A-13）**：13 处置脏与 13 处 `MarkTextChanged()` 严格成对（`:129/140/307/329/359/372/390/402/913/935/970/1000/1085`）⇒ 键维 1 `TextRevision()` 严格覆盖该布尔位。
★ **R38-7 的兜底**：实现期若发现**任一**站点不成对 ⇒ **保留布尔位**并在 §9 登记缺口，不为退役而退役。

**读取改名**（`m_lineStarts` → `m_lines`，13 处）：

| 行 | 现 | 改 | 需求编号 |
|---|---|---|---|
| `:461` | `m_lineStarts.empty()` | `m_lines.empty()` | C1 |
| `:465` | `m_lineStarts.size() - 1` | `m_lines.size() - 1` | C1 |
| `:468` | `m_lineStarts[mid] <= cp` | `m_lines[mid].startCp <= cp` | C1 |
| `:479` | `const size_t start = m_lineStarts[lineIndex];` | 随 §2.4 整体替换 | C2 |
| `:480-481` | `… ? m_lineStarts[lineIndex+1] - 1 : GetCodepointCount()` | 随 §2.4 整体替换（**`−1` 退场**） | C2 |
| `:515` | `m_lineStarts.size()` | `m_lines.size()` | C3 |
| `:529` | `m_lineStarts.size()` | `m_lines.size()` | C4 |
| `:565` | `m_lineStarts[lineIndex]` | `m_lines[lineIndex].startCp` | C5 |
| `:684` | `m_lineStarts[lineIndex]` | `m_lines[lineIndex].startCp` | C8 |
| `:778` | `m_lineStarts.size() - 1` | `m_lines.size() - 1` | C9 |
| `:1251` | `curLine + 1 >= m_lineStarts.size()` | `curLine + 1 >= m_lines.size()` | C10 |
| `:1370` | `const size_t lineCount = m_lineStarts.size();` | `… = m_lines.size();` | C11 |

**注释改名**（9 处，A-12）：`.cpp` `:463` `:504` `:536` `:609` `:649` · `.h` `:96` `:257` `:329` `:349`（`:349` 整行随字段删除）。

**零改动站**（登记以备机检）：`Home`/`End` `:1223-1242` · `Left`/`Right` `:1202-1222` · `GetWordBounds` `:829+` · `CaretIndexFromLineX` `:785-814`（**自动吃归属口径** ⇒ 行尾空格可达 = A6①）· `SyncScrollBar` `:604-641` 代码体（口径经 C4 变）· `OnMouseWheel` `:646` 的单行闸 · `LayoutScrollBar` `:589-602` · `GetTextAreaHeight` `:522-525`。

### 2.9 `TextBoxTests.cpp` — 夹具（新增，不动既有）

**① `TestableTextBox`（`:44-69`）追加暴露**（**add-only**：既有 12 个 `using` 一个不动；★ `using TextBox::LineRange;`（`:61`）**签名不变** ⇒ 既有断言零改动）：

```cpp
	// ★ Phase 38：折行三件（protected ⇒ using 暴露）
	using TextBox::LineDrawRange;
	using TextBox::IsWrapActive;
	using TextBox::WrapRequested;
	using TextBox::CaretIndexFromLineX;

	/// ★ 视觉行数 —— 见下方「v1.3 订正」：不能直接读 m_lines
	[[nodiscard]] size_t RowCountForTest() const{
		return static_cast<size_t>(GetPreferredSize().height / 16.0f + 0.5f);
	}
	[[nodiscard]] std::pair<size_t,size_t> OwnForTest(size_t i){ RecalculateLines(); return LineRange(i); }
	[[nodiscard]] std::pair<size_t,size_t> DrawForTest(size_t i){ RecalculateLines(); return LineDrawRange(i); }
```
★★ **v1.3 订正（v1.2 及更早写的是 `LineCountForTest(){ … return m_lines.size(); }`——编译不过）**：`m_lines` 是 `TextBox` 的 **private** 成员（行表数据私有是 D38-E 的一部分），**派生类的成员函数同样访问不到**——「加一个中转」绕不过访问控制，中转只是换了个函数名。⇒ 行数改用 **public 面反推**：`p = 0` 且假测量器行高 16 ⇒ `preferred.height == ceil(n × 16)` ⇒ `n = height / 16`。★ 这不是凑数：它顺带把「行表行数」与「preferred 高度」钉成一致——两者若分叉本条就红（T38-12 也走同一路径）。★ 若将来确实需要暴露行数，正确做法是在 `TextBox` 加一个 protected `LineCount()`（protected 面 +1），而不是在夹具里绕。
★ 两个 `*ForTest` **必须自带 `RecalculateLines()`**：本 Phase 退役了旧失效布尔位，行表只在**键未命中**时重建；若夹具忘了先调一次，断言读到的是**上一轮的表**（假绿的经典形态）。`RecalculateLines()` 非 const ⇒ 两个中转也不能是 const（`RowCountForTest` 反而是 const——它走 `GetPreferredSize`，内部自己 `const_cast` 重算）。

**② 折行探针（无头）**——`TestableTextBox::ResolveMeasurer()`（`:66`）恒返回静态假测量器 ⇒ **无需窗口即可折行**（这正是 D38-D 不把 `GetWindow()` 放进判据的收益）；形态沿 `:1606-1624` 的 `CaretProbe`：

```cpp
struct WrapProbe{
	TestableTextBox box{std::string("")};
	WrapProbe(){ box.SetSize(40, 64); box.SetWordWrap(true); }   // p 缺省 0 ⇒ 折行宽 = 40 − 14 = 26
	void Set(const std::string& s, int w, int h){ box.SetText(s); box.SetSize(w, h); }
};
```
★ 无头 ⇒ `Invalidate()` 是 no-op（`Widget.cpp:585-593`）⇒ **C38-9 的那条断言只能在 T38-16 的真窗口装置里验**（A-10）。

**③ `CountingTextBox` + `CountingFakeMeasurer`（T38-15）**——★ `TestableTextBox` 的测量器是**焊死的静态 `ms_fake`**（`:68`）⇒ 计数型测量器**必须另起一个可测类**，不能复用：

```cpp
/// @brief ★ 计数型假测量器（T38-15）。先例 = WidgetTests.cpp:539-553 的 CountingMeasurer，
///        但那个是 final 且**只覆写 MeasureText/LineHeight**（当日复核）⇒ 不可复用。
/// ★ 三个虚函数**均非 const**（`ECDI/include/ECDI/Render/TextMeasurer.h`:50 / :68 / :141）——写成 const override 不可编译。
class CountingFakeMeasurer final : public TextMeasurer{
public:
	int fitCalls = 0;
	int measureCalls = 0;

	Size MeasureText(const Font&, const std::string& text) override{
		++measureCalls;
		return Size{ static_cast<float>(CountCp(text)) * 8.0f, 16.0f };
	}
	/// ★ **必须覆写**：`Render/TextMeasurer.h:68-117` 的默认体用码点二分反调 MeasureText ⇒
	///   两个计数互相污染，T38-15 的线性判据（行数 == fitCalls）就不成立。
	TextFit FitText(const Font&, const std::string& text, std::size_t startCp, float maxWidth) override{
		++fitCalls;
		const std::size_t total = CountCp(text);
		if (startCp >= total) return TextFit{ 0, 0.0f };
		const std::size_t byWidth = static_cast<std::size_t>((std::max)(0.0f, maxWidth) / 8.0f);
		const std::size_t n = (std::min)(byWidth, total - startCp);   // ★ 契约：fitCp 是**相对**数、不得越串尾
		return TextFit{ n, static_cast<float>(n) * 8.0f };
	}
	float LineHeight(const Font&) override{ return 16.0f; }

private:
	static std::size_t CountCp(const std::string& s){
		std::size_t n = 0;
		for (char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;   // 同 :36-38 口径
		return n;
	}
};

class CountingTextBox : public TextBox{
public:
	using TextBox::TextBox;
	using TextBox::RecalculateLines;
	/// ★ 同 §2.9① 的 v1.3 订正：行数经 preferred 反推（`m_lines` 是基类 private）
	[[nodiscard]] size_t RowCountForTest() const{
		return static_cast<size_t>(GetPreferredSize().height / 16.0f + 0.5f);
	}
	static CountingFakeMeasurer& Counter() noexcept{ return ms_counter; }
protected:
	TextMeasurer* ResolveMeasurer() const override{ return &ms_counter; }
private:
	static CountingFakeMeasurer ms_counter;
};
CountingFakeMeasurer CountingTextBox::ms_counter;   // ★ 静态定义（同 :101/:103 先例）
```

**④ `RecordingFakeMeasurer`（T38-14h 的 S7 量具；★ v1.5：实现名带 `Fake`，与 `CountingFakeMeasurer` 同族）**：与 ③ 同型派生（同样三个非 const 覆写），额外把每次收到的串**按字节**追加进 `std::vector<std::string> m_fed;`，断言 = 存在一条含 `"\xE2\x80\xA2"`（U+2022 `•`）且**全表无一条**含明文字母（`secret` 的任一字符）。同样需要自己的 `RecordingTextBox`（理由同 ③）。

**⑤ 真窗口夹具（T38-16 + T38-11②）**：复用 `CaretProbe` 装置（**`TextBoxTests.cpp:1795` 起**；v1.5 新增「文本 + 尺寸」重载，默认构造委托给它 ⇒ 既有用例零改动）——`Application app;` + `RenderServices{ GDIBackend, GDITextMeasurer }` + `app.Create("ECDI_T38_Wrap", 200, 120, std::move(services))` + `AddChild(std::move(owned))` + `SetSize(150, 30)` + `OnFocusGained()` + `box->Paint(ctx, 0, 0)`；★ 扩一个 `SetSingleLine` 开关相（白名单④）与一个「先无头求 preferred、后 `AddChild` 挂窗」相（白名单③）。析构 `window->Release()`。

### 2.10 `WidgetTests.cpp` — `TestableLabel` 追加暴露（T38-2；**add-only**）

`TestableLabel`（`:444-452`）现状只有 `using Label::Label;` + `ResolveMeasurer()` 覆写 + 静态 `ms_fake`。红线② 要断言 `BuildTextLayout` 的**输出字段**，而该函数改后是 protected ⇒ 追加：

```cpp
class TestableLabel : public Label
{
public:
	using Label::Label;
	// ★ Phase 38 T38-2：断言 Label 侧 BuildTextLayout 输出逐位不变（红线②）
	using TextWidget::BuildTextLayout;
	using TextWidget::TextLayout;
	using TextWidget::TextLine;
protected:
	TextMeasurer* ResolveMeasurer() const override{ return &ms_fake; }
private:
	static FakeTextMeasurer ms_fake;
};
```
★ 三个 `using` 是**纯增量**（既有 T29-1..6 / T29-WRAP-1..3 / T28-3a/b / T36-7 / T31-2a..c 全部只用 `GetPreferredSize` 与 `Paint`，不受影响）。
★ 登记落 `RegisterWidgetTests()` 尾部（`:1699-1717` 一带），前缀沿既有 `TextWidget.Wrap*` 家族。

---

## §3 样本表（★ `tools/phase38_wrap_probe.py` 逐字输出，2026-10-29 复跑）

`FakeTextMeasurer` 口径：**每码点 8 DIP、行高 16**（`TextBoxTests.cpp:34-41`）。`w` = 该行**裁剪后**宽；`own` = `ownedEnd`。

### 3.1 九样本 × 两档宽 = 18 次（T38-7 的全表对账依据）

| # | 文本（码点数） | 档 | 引擎实算 | A3 |
|---|---|---|---|---|
| **S1** | `"ab cd ef"` (8) | 48 | L0 `[0,5)` w40 own6 · L1 `[6,8)` w16 own8 | OK |
| | | 24 | L0 `[0,2)` w16 own3 · L1 `[3,5)` w16 own6 · L2 `[6,8)` w16 own8 | OK |
| **S2** | `"一二三四五六"` (6) | 40 | L0 `[0,5)` w40 own5 · L1 `[5,6)` w8 own6 | OK |
| | | 24 | L0 `[0,3)` w24 own3 · L1 `[3,6)` w24 own6 | OK |
| **S3** | `"ab cd \nef gh"` (12) | 40 | L0 `[0,2)` w16 own3 · L1 `[3,5)` w16 own6 · L2 `[7,12)` w40 own12 | OK |
| | | 24 | L0 `[0,2)` w16 own3 · L1 `[3,5)` w16 own6 · L2 `[7,9)` w16 own10 · L3 `[10,12)` w16 own12 | OK |
| **S4** | `"一二三。四"` (5) | 24 | L0 `[0,4)` **w32**（> 折行宽 ⇒ 悬挂）own4 · L1 `[4,5)` w8 own5 | OK |
| | | 16 | L0 `[0,2)` w16 own2 · L1 `[2,4)` w16 own4 · L2 `[4,5)` w8 own5 | OK |
| **S5** | `"ab（cd"` (5) | 24 | L0 `[0,2)` w16 own2 · L1 `[2,5)` w24 own5 | OK |
| | | 16 | L0 `[0,2)` w16 own2 · L1 `[2,4)` w16 own4 · L2 `[4,5)` w8 own5 | OK |
| **S5b** | `"（a"` (2) | 8 | L0 `[0,1)` w8 own1 · L1 `[1,2)` w8 own2 | OK |
| | | 16 | L0 `[0,2)` w16 own2（**1 行**） | OK |
| **S5c** | `"一二三（四"` (5) | 32 | L0 `[0,3)` w24 own3 · L1 `[3,5)` **w16** own5 | OK |
| | | **16**（★ A-14 订正：w24 与 w32 输出**完全相同**、零判别力） | L0 `[0,2)` w16 own2 · L1 `[2,3)` w8 own3 · L2 `[3,5)` w16 own5 | OK |
| **S6** | `"abcdefgh"` (8) | 26 | L0 `[0,3)` w24 own3 · L1 `[3,6)` w24 own6 · L2 `[6,8)` w16 own8 | OK |
| | | 40 | L0 `[0,5)` w40 own5 · L1 `[5,8)` w24 own8 | OK |
| **S7** | `"secret"` (6)（`Password`） | 40 | L0 `[0,5)` w40 own5 · L1 `[5,6)` w8 own6 | OK |
| | | 24 | L0 `[0,3)` w24 own3 · L1 `[3,6)` w24 own6 | OK |
| **tail** | `"ab\n"` (3) | 40 | L0 `[0,2)` w16 own2 · L1 `[3,3)` **w0 own3**（尾部空行） | OK |

### 3.2 A4 夹具（`W=40 / p=0 / "abcdefgh"`；T38-8 / T38-12 的数值来源）

```
wrapWidth=26 H=64 -> n=3 contentH=48 maxScroll=0  barVisible=False
wrapWidth=26 H=32 -> n=3 contentH=48 maxScroll=16 barVisible=True
wrapWidth=26 H=24 -> n=3 contentH=48 maxScroll=24 barVisible=True
wrapWidth=40 H=64 -> n=2 contentH=32 maxScroll=0  barVisible=False
wrapWidth=40 H=32 -> n=2 contentH=32 maxScroll=0  barVisible=False
wrapWidth=40 H=24 -> n=2 contentH=32 maxScroll=8  barVisible=True

不动点比对（本方案 always-deduct vs 回退谓词 visible-only）：
  H=64  NEW=(26.0, 3, 48.0, False)   OLD=(40.0, 2, 32.0, False)
  H=32  NEW=(26.0, 3, 48.0, True)    OLD=(40.0, 2, 32.0, False)   ← ★ OLD 假绿（两态同值）
  H=24  NEW=(26.0, 3, 48.0, True)    OLD=(26.0, 3, 48.0, True)
```
⇒ **T38-8 的两态取 `H=64` 与 `H=24`**（P0-2）：`H=64` 下 NEW 恒 `(26,3,48,False)` 两态相等 ⇒ 正对照绿；`H=24` 下 OLD 两态都是 `(40,2)` 但 NEW 是 `(26,3)` ⇒ **回退谓词必红**（负对照有判别力）。`H=32` **不得**用作负对照（OLD 两态同为 `40/2` ⇒ 假绿）。

### 3.3 D38-I 的 T38-17 夹具（由 §3.1 S4 行推出，脚本可复算）

`W=38 / H=40 / p=0 / wrap on / "一二三。四"`：折行宽 = `38 − (12 + max(0,2)) = 24` ⇒ **S4@24 = 2 行**（L0 `[0,4)` **w32** own4 · L1 `[4,5)` w8 own5），内容高 = `2 × 16 = 32 ≤ 40` ⇒ `maxScroll = 0` ⇒ **条隐藏**。
- clip 宽（本方案）= `38 − 0 = 38`；悬挂行占位 `[0, 32]` ⊂ `[0, 38]` ⇒ **完整可见** ✓
- clip 宽（负对照 = 仍取 `GetTextAreaWidth()`）= `24`；`32 > 24` ⇒ **`。` 整字被裁** ⇒ 本条红 ✓
- 条可见半段：同夹具把 `H` 压到 **24**（内容高 32 > 24）⇒ 断言 **clip 宽仍 38**（不因条出现而变）+ 悬挂行 `DrawTextCommand` **仍发射**。★ **不得**断言「被条覆盖」——轨道 alpha 0（`DefaultTheme.cpp:110` + `GDIBackend.cpp:741-745` / `:73`），是否被**滑块**压住属像素级，超无头判据面（R38-3 / R38-9）。

### 3.4 ★★★ S8 组：分隔空格归属（v1.1 新增，A-15 的判别面；期望值 = 修订版脚本实算）

★ **来源与口径**：`FakeTextMeasurer`（8 宽 / 16 行高）· `p = 0` · `wrap on`。**「旧」= `ownedEnd = b`（v1.0 / 初设口径）· 「新」= `ownedEnd = nextScan`（v1.1 订正）**。★ `draw` / `width` **两版逐位相同**（这正是红线② 的结构性保证），只有 `own` 变。

| # | 文本（码点数） | 折行宽 | 新（v1.1）实算 | 旧（v1.0）实算 | 旧口径违反的判据 |
|---|---|---|---|---|---|
| **S8a** | `"一二三 四"` (5) | 24 | L0 `[0,3)` w24 **own4** · L1 `[4,1)` w8 own5 | L0 `own3` | `gap = 1` 而 `cps[3] = ' '`（非 `'\n'`）⇒ A3 第三态 |
| **S8b** | `"一二三  四"` (6) | 24 | L0 `[0,3)` w24 **own5** · L1 `[5,1)` w8 own6 | L0 `own3` | `gap = 2` ⇒ 越出 `gap ∈ {0,1}` |
| **S8c** | `"一二三 \n四"` (6) | 24 | L0 `[0,3)` w24 **own4** · L1 `[5,1)` w8 own6 | L0 `own3` | `gap = 2`（空格 + `\n` 两个无主码点） |
| **S8d** | `"abc   "` (6) | 40 | **1 行** `[0,3)` w24 **own6** | 1 行 `own5` | **`ownedEnd(last) = 5 ≠ count = 6`** ⇒ 违反边界等式 ② |

★ **根因（一句话）**：`FindLastSoftBreak`（`TextWidget.cpp:190-196`）对两类软断点**返回位置语义不同**——空格断点返回空格**后一位**（`cps[b-1] == 0x20` ⇒ 空格落在 `[start, b)` 内），CJK 断点返回字**后一位**（`cps[b-1]` 是 CJK ⇒ 若后跟空格，空格在 `b` 之后）。`:301` 的 ⑥⑦ 终化把 `b` 之后的连续空格跳给 `nextScan`，于是这些空格**既不在 `drawEnd` 也不在旧 `ownedEnd`** 里。⇒ `ownedEnd = nextScan` 是「归属 = 下一行起点」这条定义的**唯一自洽取法**，顺带把 S8d 的尾部空格串收进末行。
★ **为什么这些样本必须进验收**：S8a/S8c 的空格在旧口径下**不可点击、不可选中、`Left/Right` 跨不过去**（三个消费点全部走 `LineRange`），而 harness 的中文聊天框里「中文 + 空格 + 继续输入」是常态；S8d 则是「行尾空格串」——`AutoSize` 后仍会出现在 `"支持一下   "` 这类输入中途态。

---

## §4 用例 T38-1..18 → 具体登记名、断言、期望值来源

**513 → 531（+18）**（★ v1.1：+17 → +18，新增 T38-18）。登记形态（`TextBoxTests.cpp` 的 `RegisterTextBoxTests()` 尾部，`WidgetTests.cpp` 的 `RegisterWidgetTests()` 尾部）：
```cpp
	GetTestRegistry().Add("TextBox.SoftWrapOffPaintBaseline", &Test38SoftWrapOffPaintBaseline);   // T38-1
```
★ 既有 513 条**一条断言都不改**；若某条因 `m_lineStarts` 改名而需改**编译面**（`using` / 探针），只允许机械改名。

| # | 登记名（`namespace ECDI::Test`） | 文件 | 断言要点 | 期望值来源 |
|---|---|---|---|---|
| **T38-1** | `TextBox.SoftWrapOffPaintBaseline` | TextBoxTests | 非折行态**三组**（含 `\n` 多行 / 单行超宽 / **空文本**）的 `RecordingBackend` 命令流：`get_if<DrawTextCommand>` 的 `pos.x`/`pos.y`/`text`、`get_if<PushClipCommand>` 的 `rect` 逐字段 == 常量表 | 常量表 = **批 0 在 HEAD 上采集**（§6；源码基准 `6a3807be19a30d8512e18ef2d68bf2d1b7842cbf`） |
| **T38-2** | `TextWidget.WrapLayoutOutputUnchanged` | WidgetTests | `TestableLabel`（§2.10 追加的 3 个 `using`）⇒ 对 T29 的既有串逐字段断言 `startCp`/`cpCount`/`width`/`totalHeight`；★ **并断言 `ownedEnd` 不参与 `Label` 的任何输出**（`DrawTextCommand.text` 与改前逐位同） | 既有 T29-1..6 / T29-WRAP-1..3 的常量 + §3.1 |
| **T38-3** | `TextBox.SoftWrapS1FourWaySameSource` | TextBoxTests | S1@48：① `LineDrawRange(0) == (0,5)` ② `LineRange(0) == (0,6)` ③ `CaretIndexFromPosition` 在 L1 的 y 带内命中 **6**（无头 ⇒ 恒行首，A2②）④ ★ **v1.3 换形**：改断 `DrawTextCommand.pos.y == {0, 16}`——`CalculateCaretPosition` 是 **private**（派生类不可 `using` 暴露）；命令级断言更硬（同时证明布局行高与发射行高同源）⑤ Up/Down 的行号 | §3.1 S1 ✅ |
| **T38-4** | `TextBox.SoftWrapS2CjkNoSpace` | TextBoxTests | S2@40：`drawEnd == ownedEnd`（**无空格可裁** ⇒ 证明归属规则不凭空吞码点） | §3.1 S2 |
| **T38-5** | `TextBox.SoftWrapS3HardSoftMixed` | TextBoxTests | S3@40（3 行）与 S3@24（4 行）：硬行 `own+1 == start`（L1→L2：`6+1=7`）与软行 `own == start`（L0→L1：`3==3`）**同表混存** | §3.1 S3 |
| **T38-6** | `TextBox.SoftWrapKinsokuFiveSamples` | TextBoxTests | **五**样本四路同源：S4@24（悬挂 w32 > 折行宽 24）· S5@24（④ 行尾开括号回退 ⇒ `（` 挪到 L1）· S5b@8（④b 守卫 ⇒ 不产生空行）· S5c@32（**对照组：④ 在纯 CJK 串上不可达**）· S6@26（超长词硬断） | §3.1 |
| **T38-7** | `TextBox.SoftWrapCodepointConservation` | TextBoxTests | **遍历** §3.1 全表 9 样本 × 2 档 = **18 次** + `tail` 1 次 + **§3.4 的 S8 组 4 串 = 23 次**（★ v1.3：v1.1 写 22，漏数了 `tail`——它是 §3.1 表里独立的一行，且是「空行 `ownedEnd` 取自身」的唯一形态），每次逐行断言（★ v1.1 判据换形，**只允许两态**）：`gap(i) = start(i+1) − ownedEnd(i)` **== 0**（软行）**或** `== 1 ∧ cps[ownedEnd(i)] == U'\n'`（硬行）；+ 三条边界等式：`start(0)==0` · `ownedEnd(last)==count` · `drawEnd(i) ≤ ownedEnd(i)`。★ **v1.0 的 `gap ∈ {0,1}` 写法已作废**——它在 `ownedEnd = b` 下会把「`gap==1` 且 `cps[ownedEnd]==' '`」判成违反而在 `ownedEnd = nextScan` 下永不出现，判据必须与选定的取法同形 | §3.1 + §3.4（脚本 `A3 gap check` 全表 OK） |
| **T38-8** | `TextBox.SoftWrapWidthIndependentOfBar` | TextBoxTests | `W=40/p=0/"abcdefgh"`，`H=64` 与 `H=24` 两态：`GetTextAreaWidth()` **相等**（均 26）且逐行区间**逐项相等**（均 3 行） | §3.2 |
| **T38-9** | `TextBox.SoftWrapSpaceReachable` | TextBoxTests | S1@48：caret 可停 `cp5`、`Left`/`Right` 可穿过、点击 L0 行尾空格落该行（无头 ⇒ 行首归属形态） | §3.1 S1 |
| **T38-10** | `TextBox.SoftWrapSelectionAndImeContinuous` | TextBoxTests | 跨软行 `Shift` 拖选：`DrawRectCommand` 序列相邻行 `x+w` 衔接（**无 8px 断口**）；IME 组合下划线跨软行**切段**（每段落在本行） | §3.1 S1/S3 |
| **T38-11** | `TextBox.SoftWrapUndoRedoAndPinX` | TextBoxTests | 键入折行长文 → `Undo` → 行表 == 直接构造同文本的行表；折行期 `m_scrollOffsetX == 0.0f`。★★ **v1.1 强化切换相（A-17 的判别面）**：① 非折行态把光标推到长单行行尾 ⇒ 断言 `GetScrollOffsetX() > 0`（前置成立）；② `SetWordWrap(true)`（**不经任何光标移动 / 不经 `EnsureCaretVisible`**）⇒ 断言 `GetScrollOffsetX() == 0.0f`；③ 再 `Paint()` 一次 ⇒ 断言 `DrawTextCommand.pos.x == textX`（不是 `textX − 旧 offset`）。★ ② 在 v1.0 的两写点下**必红**（残留旧值），故本条同时是 D38-G 补全的负对照 · ★★ **v1.5 订正**：本相**必须真窗口**（`m_scrollOffsetX` 只在 `else if (Window*)` 支写入，无头恒 0），且 ② 的断言要跟一次**重建**（`SetWordWrap` 不调 `RecalculateLines`）⇒ 判据改成「② 重建后 offset == 0；③ 同一帧 `Paint` ⇒ `pos.x == Padding()`」，详见 §9 v1.5 ①② | ✅ 批 4 已实现（真窗口 2 条之一） |
| **T38-12** | `TextBox.SoftWrapPreferredStable` | TextBoxTests | `GetPreferredSize().height == ceil(n × L_pref + 2p)` 且 `n == OnPaint` 发射的行数；`.width == GetWidth()` **恒等**；★ **连续两次 `AutoSize()` 宽不变**；★ 追加**窄带两档**（`W = 2p+14` 与 `W = 2p+1`）⇒ 两次 `AutoSize` 宽仍不变（D38-D 删宽度合取项的判别面） | §3.2 |
| **T38-13** | `TextBox.SoftWrapOffPreferredBaseline` | TextBoxTests | 非折行态 preferred 逐位同（`wrap=false` / 含 `\n` / 单行）——常量表 | **批 0 采集** |
| **T38-14** | `TextBox.SoftWrapKeyPerDimension` | TextBoxTests | **a–i 九子断言**，每维「只变这一维 ⇒ 行数或区间必变」：a `revision` / b `font.size` / c `font.family` / d `font.weight` / e `wrapWidth` / f `wrapRequested` 的 `SetWordWrap` 分量 / g `wrapRequested` 的 `SetSingleLine` 分量（★ 判别的是**维 8 的覆盖性**，该维已删、断言不删）/ h `echoMode`（★ S7 的 `RecordingTextMeasurer` 量具落在此：喂入串含 `E2 80 A2`、不含明文）/ i **组合三维**（★ **必须开 `Password`**——Normal 态 `BuildDisplayText` 直通 `:232-233`，组合标志不入显示串 ⇒ 恒真无判别力） · ★★ **v1.5 订正**：判据从「行数/区间必变」改成「**`FitText` 被再次调用**」——假测量器忽略字体 ⇒ b/c/d 三维视觉布局不变，只有「量了没有」能区分残表；f/g 两维走非折行支不调 `FitText` ⇒ 改断「表形变单行」；i-b（起点）**无法单独变更**，只断言「换起点那一轮必重建」，隔离性登记为余量，详见 §9 v1.5 ②③ · ✅ 批 4 已实现 | §3.1 S7 |
| **T38-15** | `TextBox.SoftWrapKeyHitZeroMeasure` | TextBoxTests | `CountingFakeMeasurer`：① 键全同 ⇒ 第二次 `RecalculateLines()` 的 `fitCalls == 0 && measureCalls == 0`；② 重建一次时 **`行数 == fitCalls`**（线性判据，防超线性放大）——★ **v1.1 限定样本（A-16）**：**必须用无 `\n` 的单段串**（如 S6 `"abcdefgh"`@26 ⇒ 3 行 / 3 次）。含 `\n` 的串**必假红**：空段走 `TextWidget.cpp:257-259` 的 `continue`，**不调 `FitText`** ⇒ `"ab\n"` 是 2 行但 `fitCalls == 1`、`"a\n\nb"` 是 3 行但 `fitCalls == 2` | §2.9 + §3.1 S6 |
| **T38-16** | `TextBox.SoftWrapRealWindowWhitelist` | TextBoxTests | **真窗口四件合一**：① 焦点态绘制（`:1430`）② 行内 x 精确定位（`:796` 的 `GetWindow() != nullptr` 守卫）③ 无头求 preferred 后 `AddChild` 挂窗 ⇒ **重排**（`hasMeasurer` 维）④ ★ `SetSingleLine(false)` 后**无需外部失效**即重排重绘（C38-9；无头 `Invalidate()` 是 no-op ⇒ 只能在此验） | `CaretProbe`（**现 `:1795` 起，含本批新增的「文本 + 尺寸」重载**）· ★ v1.5 订正：③ 的无测量器 `preferred` = **当前尺寸**（{40, 64}），不是单行内容高 16，详见 §9 v1.5 ⑥ · ✅ 批 4 已实现 |
| **T38-17** | `TextBox.SoftWrapClipIsInnerBoundary` | TextBoxTests | 折行态 `PushClipCommand.rect.width == GetWidth() − 2·padding`（**不是** `GetTextAreaWidth()`），且 S4 悬挂行 `DrawTextCommand` **完整落在 clip 内**（`pos.x + 行宽 ≤ clip.x + clip.width`）；条可见半段（`H=24`）断言 clip 宽不变 + 命令仍发射；**非折行半段**（`H=10` ⇒ 条可见）断言 clip 宽 = `GetTextAreaWidth()` = 24（与折行态的 38 形成对照）。★★ **v1.4 关键订正：clip 必须取 `clipWidths[1]`，不是 `back()`**——见下方 §9 v1.4 ① | §3.3 + v1.4 ① |
| **T38-18** | `TextBox.SoftWrapSeparatorSpaceOwned` | TextBoxTests | ★★ **v1.1 新增（A-15 的判别面）**：S8 组在**行表**层的归属——① S8a `LineRange(0) == (0,4)` 且 `LineDrawRange(0) == (0,3)`（**分隔空格归前行、但不发射** = 两口径分离在软断点空格上的实证）；② S8b/S8c 逐行 `gap` 只有 0 / 1（后者仅在 `\n` 处）；③ S8d `LineRange(last).second == GetCodepointCount()`（尾部空格串不得丢）；④ **可达性**：`CaretIndexFromLineX(0, 24.0f)` 的返回值落在 `[0,4)`（含空格位 cp3；无头下 `:796` 守卫 ⇒ 只断「落在本行归属区间内」，行内精确定位归 T38-16②）；⑤ **负对照**：注释掉 §2.1 ⑨ 的回填 ⇒ ①③ **必红** | §3.4 |

★ **+18 与需求 §6 的 +14 差异** = 判据细化非范围膨胀（A2 估 4 条 → 拆 4；A8 正/反断言各成一条；A6 的 4 项压成 3 条；T38-17 = D38-I 的判别面；★ **T38-18 = A-15 的判别面**，v1.1 新增）。逐条对账见初设 §4 末。★ **v1.1 的用例预算 = 513 → 531（+18）**。

---

## §5 机检 C38-1..9 → 可执行命令

（工作目录 = 仓库根；`$TB = ECDI/src/Widget/TextBox.cpp`、`$TW = ECDI/src/Widget/TextWidget.cpp`）

| # | 条款 | 命令 | 期望 |
|---|---|---|---|
| **C38-1** | 一张行表、两个口径 | `grep -c "LineDrawRange" $TB` | **2**（定义 1 + `OnPaint` 消费 1）★ 初设写「命中 == 1」= 只数消费点；本稿订正为**含定义共 2**，且**消费点 == 1** |
| | | `sed -n '/^std::pair<size_t, size_t> TextBox::LineRange/,/^}/p' $TB \| grep -c -- "- 1"` | **0**（`−1` 退场） |
| | | `grep -rn --include=*.cpp --include=*.h "m_lineStarts" ECDI/` | **0** ✅ 实测 · ★ v1.3 加 `--include`：`ECDI/` 下有未跟踪的草稿（`.scratch_p37.md`，10-27 遗留）会命中，它不是代码 |
| **C38-2** | 码点守恒（★ v1.1 判据收紧：`gap` 只允许 **0**（软行）或 **1 且 `cps[ownedEnd]=='\n'`**（硬行），**无第三态**） | T38-7 全表遍历（**非抽样**） | ✅ **23/23 绿**（★ v1.3：9 样本 × 2 档 + `tail` + S8 组 4 串；v1.1 的 22 漏数了 `tail`）· 实测 2026-10-29 |
| | | `grep -n "lines.back().ownedEnd = nextScan" $TW` | **1**（§2.1 ⑨ 的归属回填；★ 缺它则 S8 组全红） |
| **C38-3** | 三个宽度、各自单一来源、互不成环 | `grep -c "GetTextAreaWidth" $TB` | ★★ **v1.3 实测订正（5 → 9）**：**代码 5 处** = `RecalculateLines` 的折行宽采集 · `EnsureCaretVisible` 的 clamp 宽 · 定义 · `CalculateCaretPosition` 的 `maxCaretX` · `OnPaint` 的 clip 非折行支；**注释 4 处** = 折行宽来源说明 · `GetTextAreaHeight` 的对称说明 · D38-I 的两句（「取控件内边界，**不取** `GetTextAreaWidth()`」）。★ 比 v1.0 估的 4+1 多出**一处代码 + 三处注释**：① `RecalculateLines` 折行支必须在这里取折行宽（批次合并的直接结果，见 §6 的 v1.3 注记）；② D38-I 的论证**必须**点名 `GetTextAreaWidth()` 才说得清「为什么不取它」——为凑机检数字删掉理由，是把判据换成了装饰 |
| | | `sed -n '/^float TextBox::GetTextAreaWidth/,/^}/p' $TB \| grep -c "钻到条下\|无循环依赖"` | **0**（两条在案注释均已改写；★ 不得静默改码） |
| | | `grep -n "IsWrapActive" $TB \| grep PushClip` 附近三目 | clip 表达式**必须**含 `IsWrapActive()` 三目 |
| **C38-4** | 显示串唯一 | `sed -n '202,310p' $TW \| grep -c "m_text"` | **0**（★ 范围 `:202-310`，A-1；改前实测 4 行 5 处 = `:210/228/229/230/269`） |
| | | `sed -n '392,400p' $TW \| grep -c "m_text"` | **≥ 2**（`:396-398` 的发射点**必须保留**） |
| | | T38-14h 的 `RecordingTextMeasurer` | 喂入串含 `E2 80 A2`、不含明文 |
| **C38-5** | `Label` 零参与 | `grep -n "ownedEnd" $TW` | ★★ **v1.2 实测订正（4 → 2）**：命中 **2 处** = **1 处注释** + **1 处写**（`layout.lines.back().ownedEnd = nextScan;`）。v1.1 写「3 处构造 + 1 处写 = 4」**是错的**——三处构造都用**位置初始化**（`TextLine{ 0, 0, 0.0f, 0 }` / `{ lineStart, end - lineStart, width, lineEnd }` / `{ segStart, 0, 0.0f, segStart }`），**字段名不出现在源码里**。⇒ 判据的正确形态 = 「2 处，且**无任何读取表达式**（无 `ownedEnd` 作右值 / 无 `->ownedEnd`）」；另可加 `grep -c "\.ownedEnd"` == **1**（唯一成员访问 = 那一处写） |
| **C38-6** | 失效只经键 + 显示串不变式 | `grep -rn --include=*.cpp --include=*.h "m_needsLineRecalc" ECDI/` | **0** ✅ 实测 · ★ v1.3：**注释里也不许留这个标识符**——否则「反正是注释」会变成真残留的掩护。实现在注释里统一改称「旧失效布尔位」 |
| | | `grep -n "m_isComposing = \|m_compositionStart = \|m_compositionLength = " $TB` | 逐处核对 bump 成对：`:898/899/908`→`:913`、`:928/930/931`→`:935`、`:955/957`→**越序**（`RestoreSnapshot` 在 **`:953`**，A-8）、`:964/966`→`:970` |
| **C38-7** | 配套语义边界 | `grep -n "m_scrollOffsetX *=" $TB` | **3**（★ v1.1 再订正：初设写 1、v1.0 写 2）——**2 处**在 `EnsureCaretVisible` 体内（折行支钉 0 / 非折行支跟手写）+ **1 处**在 `RecalculateLines` 折行重建分支（§2.2 写点 3，覆盖**切换相**）；+ `.h` 字段初值 ⇒ 全仓 **4**。★ 三缺一即破不变式 `U`（§2.5④） |
| | | `git diff HEAD -- $TB \| grep -A3 "case KeyCode::Home"` | **零 diff**（`Home`/`End` 不动 = D38-H；折行后 `Home` 落全文首 ⇒ 与 Up/Down **不同源 = 已知现状**，占号 #82） |
| **C38-8** | 默认关闭逐位不变 | T38-1 + T38-13 + T38-17 非折行半段 + **513 存量全绿** | 0 红 |
| | ★ 执行注记 | 任何以「绘制顺序 / 不透明度 / 覆盖关系」为前提的论证，复核必须**同时**核到 ① 调用顺序（`Widget.cpp:506`→`:508`→`:515-518`）② **主题默认值**（`DefaultTheme.cpp:110` trackColor alpha **0** / `:111` thumbColor alpha 255 / `:115` barThickness **12**）③ **后端 alpha 分支**（`GDIBackend.cpp:741-745` → `BlendAlphaSolid :73` 预乘 + `AlphaBlend(AC_SRC_ALPHA)`） | 三项齐核（初设 v1.1 只核了 ① ⇒ 假结论） |
| **C38-9** | 改判据的 setter 必须请求重绘 | `for f in "TextWidget.cpp:SetWordWrap" "TextBox.cpp:SetEchoMode" "TextBox.cpp:SetSingleLine"; do …; done` 逐 setter 读体内是否含 `Invalidate()` | **3/3 含**（现状 `SetWordWrap :100-115` ✓ · `SetEchoMode :148-153` ✓ · `SetSingleLine :162-167` **✗ ⇒ 本 Phase 补**） |
| | | T38-16④ | **真窗口**（无头 `Invalidate()` 是 no-op：`Widget.h:294` 声明 + `Widget.cpp:585-593` 实现，A-10） |

---

## §6 实现批次切分与回归门

| 批 | 内容（**只含本批**） | 用例增量 | 累计 | 回归门 |
|---|---|---|---|---|
| **批 0** ✅ | ★ **基准采集（前置、不可后补）**：在 HEAD `6a3807b` 上写 T38-1 / T38-13 的常量表并跑绿，**只加测试、零产品码改动** | +2 | **515** | ✅ **2026-10-29 实测 515/515**（MinGW g++ Debug）；★ 采集时订正一处断言口径：`Widget::Paint` 自带 `PushClip(self)`（`Widget.cpp:506`）⇒ clip 条数 **非空 2 / 空文本 1**，rect 取 `clips.back()` |
| **批 1** ✅ | **add-only**：`TextWidget.h` 可见性下移 + `ownedEnd` + `TextRevision()` + `:61-70` 注释扩写；`TextWidget.cpp` **九处**（§2.1，含 ⑨ 的归属回填）；`TestableLabel` 加 3 个 `using`；T38-2 | +1 | **516** | ✅ **2026-10-29 实测 516/516**；C38-4 = **0** ✓ · C38-5 = **2 处（1 注释 + 1 写）** ✓（见 §5 的 v1.2 订正）· ★ 本批**不动 `TextBox`** ⇒ 红线① 结构性成立 |
| **批 2** ✅ | **行表换芯**：`TextBox.h`（`m_lines` + 10 键字段 + 3 protected + 2 private 辅助 + 七处注释 + 删两字段）；`TextBox.cpp`（§2.2 / §2.3 / §2.4 / §2.7 / §2.8）；夹具（§2.9）；T38-3..7 + T38-15 + **T38-18** | **+7** | **523** | ✅ **2026-10-29 实测 523/523**（MinGW g++ Debug）· C38-1 = `LineDrawRange` 2 ✓ / `LineRange` 内 `- 1` 0 ✓ / `m_lineStarts` 0 ✓ · C38-2 = 23/23 ✓ · C38-6 = 0 ✓ · C38-7 = 3 ✓ · 516 存量逐位绿 · ★★ **本批与批 3 的**产品码**合并落地**（见下方 v1.3 注记）；**测试**仍按批次分两批加，故本条只计 +7 |
| **批 3** ✅ | **接线**：`GetTextAreaWidth`（§2.5①+注释）· `GetPreferredSize`（②）· `MultilinePreferred`（③）· `EnsureCaretVisible` 钉 X（④）· `OnPaint` clip（⑤）+ 行循环拆口径（§2.6）· `SetSingleLine`（⑥）；T38-8/9/10/12/17 | +5 | **528** | ★★ **产品码随批 2 合并落地**（§9 v1.3 ①）；用例五条已加，`-fsyntax-only` 通过（新增代码零告警）· 运行读数待 CLion 构建回填（预期 **528/528**） |
| **批 4** ✅ | **收口**：T38-11（Undo/Redo + 钉 X 切换相）· T38-14（键 a–i）· T38-16（真窗口白名单四件） | +3 | **531** | ★ **三条已加** + `-fsyntax-only` 通过（见 §9 v1.5 ④⑤ 的两处编译级订正）· `doc_lint` 按新用例实测 **tests=531 headers=100 docs=198** · ✅ **四构建链实测 531/531**（MSVC / Clang / ClangCL / MinGW，2026-10-29）· ★ **真窗口由 1 条变为 2 条**（T38-11② 必须真窗口，见 §9 v1.5 ①） |

★ **每批的通用门**（沿 Phase 37 惯例）：① `ecdi_tests.exe` 全绿且计数 == 累计值；② `python tools/doc_lint.py --select L02..L11 --quiet` == 0 defect；③ 改动文件 BOM=True / CR=0（`.py` 除外）；④ `git status` 与批内容**逐项对上**（不得夹带）。
★ **批间不得合并提交**：批 2 与批 3 之间必须有一次全绿快照——批 3 一旦出问题，回退面是「接线」而不是「行表」。
★★★ **v1.3 实施注记（这条纪律在本 Phase 无法按原样执行，理由如下，已如实登记而非绕过）**：批 2 的**产品码**与批 3 的**产品码**在同一次编辑里落地了。三条硬理由：
  ① **`RecalculateLines()` 的折行支本身就调 `GetTextAreaWidth()`**（§2.2 的 `wrapWidth`）⇒ 若 `GetTextAreaWidth()` 的谓词还停在「条可见才让位」，折行宽就会**随条显隐变化**：首次建表时条不可见 ⇒ 折行宽 = 全宽；条出现后 ⇒ 折行宽 −14 ⇒ 行表变 ⇒ 内容高变 ⇒ 条又可能消失 ⇒ **自指环仍然在**。E3 的独立性证明依赖 §2.5① 与 §2.2 **同时**成立，拆开做等于把环留着过一个批次。
  ② **T38-3..7 的期望值对不上**：样本表写的是「折行宽 = w」，而测试只能设**控件宽**（`w + 14`）。§2.5① 不落地 ⇒ 折行宽不确定 ⇒ 批 2 的七条用例无法与样本表对账，只能等批 3 才能验 —— 那「批 2 全绿」就失去了内容。
  ③ 两批改的是**同一批函数**（`RecalculateLines` / `GetPreferredSize` / `MultilinePreferred` / `EnsureCaretVisible` / `OnPaint`），文件级无法分离；`git add -p` 式的行级拆分只会造出「编译过但语义自相矛盾」的中间态。
  ⇒ **保留的回归门**：测试仍按批次分两次加（批 2 的 7 条已绿 = 523，批 3 的 5 条待加 = 528），因此「行表是否正确」与「接线是否正确」在**测试层面**仍可分别归因；且 516 条存量在换芯 + 接线同时到位的情况下**逐位全绿**，这是红线① 更强的实证（而不只是「未接线时没坏」）。
  ⇒ **对后续 Phase 的建议**：批次切分应按「**同一函数内的改动能独立成立**」划，而不是按「文件 / 概念层次」划。本 Phase 的 §6 是按后者划的，所以撞上了这个耦合。
★ **外部动作（只登记、不执行）**：harness 侧 `ComposerBox` 构造里的 `SetWordWrap(true)`，**在拿到含批 3 的前缀之前不得加**（加了 = 长高 + 宽度棘轮）。`--grow-probe` 的软折行相同理。**本仓不代对方仓执行任何写回。**

---

## §7 风险处置（承接初设 R38-1..9 + 本稿 v1.1 新增 R38-10，补「实现期动作」列）

| # | 风险 | 实现期动作 |
|---|---|---|
| **R38-1** | 消费点漏改 | §2.8 的两张表（13 读取 + 9 注释）逐行打勾；C38-1 的 `grep -rn "m_lineStarts" ECDI/` == 0 是**硬门**；T38-3..6 四路同源 × 5 组样本（漏改任一站 ⇒ 至少一组红） |
| **R38-2** | `dpi` 维无判据 | 如实入账，**不新造夹具**（夹具 = 范围膨胀）。§9 登记为已知缺口 |
| **R38-3** | 常驻条槽的观感代价（右侧 14px 槽 + 早断一格 + ★ 条可见时悬挂尾可能与**滑块**重叠） | 三条代价**均不入断言**（像素级）；T38-17 只断 clip 宽与命令发射。首个真实消费者若反对，重开的是 **D38-3 的形态**，不是本 Phase 其余决策 |
| **R38-4** | 行高双接缝（K13：`L_geom` `:486-493` 无头硬编码 16 vs `L_pref` `:222`/`:206`） | 本 Phase **折行用例一律不用** `FractionalLineHeightTextBox`（`:78-99`，`FracMeasurer` 19.2）；既有 `FractionalLineHeight` 用例保持**不开折行**；统一两接缝 = 非目标七，要拉回先改需求稿 |
| **R38-5** | 悬挂字符可见性 | 处置 = **D38-I**（§2.5⑤）；症状由「两态恒不可见」降为「恒可见，条出现时可能与滑块重叠」。**不放开 `scrollOffsetX`**（放开 = 折行态引入水平滚动，与立项动机冲突） |
| **R38-6** | 每帧成本（`OnPaint :1290` 每帧 `SyncScrollBar` → `RecalculateLines`） | 键命中 ⇒ 零 `FitText`/零 `MeasureText`（T38-15 反断言 + 线性判据）；最坏 = 一次重排 + 一次 `BuildDisplayText()` 拷贝；大文档虚拟化另账（#49/#33） |
| **R38-7** | 退役 `m_needsLineRecalc` 引入回归 | 前置证据 = 13↔13 严格成对（A-13 当日复核）；T38-11 + T38-14a + 513 存量三条一起兜。★ **若任一站点不成对 ⇒ 保留布尔位**并登记缺口，不为退役而退役 |
| **R38-8** | setter 触发缺口（键不保证「该算时被调用」） | §2.5⑥ 补 `Invalidate()` + C38-9 契约化（3 setter 全表）+ T38-16④（真窗口）。★ **结构性余量如实登记**：将来任何新增的判据输入都必须同时进 C38-9 全表，否则同一缺口复活 |
| **R38-9** | 悬挂量无上限 + 窄带退化 | ① ⑤ 的上界取 `segEnd`（`:292-294` 在案）⇒ 连续闭标点串悬挂量无固定上限，超过**控件内边界**时 D38-I 也救不回 ⇒ A6① 的豁免面收窄为「行宽 > 控件内边界」（**不是**「> 折行宽」）。**不给 ⑤ 加上界**（= 改引擎禁则 = 破红线②）。② 窄带 `0 < W ≤ 2p+14` 内 `GetTextAreaWidth()` 可为 0 或负（脚本实测 `W=10/p=0 ⇒ −4`）⇒ 引擎侧结构性有界（`:250` `limit = max(0, maxWidth)` · `:270` `hardEnd` 可 == `scan` · `:282` 硬断一字 · `:302` 绝不零推进 ⇒ `n ≤ count`、无死循环）；clip 侧由 D38-I 取控件内边界 ⇒ **不出现负宽 clip**。**不加最小宽约束**（= 范围膨胀） |
| **R38-10** | ★★ **v1.1 新增 · 两条结构性余量（均不修、均登记）**：① **`\n` 前后空格的分属**——`ownedEnd = nextScan` 让 ⑦ 跳过的空格归前行，但 `\n` **前**的空格归前行、**后**的归下一段起始（`segStart = i+1`）⇒ 两行归属仍连续（`gap == 1` 且 `cps == '\n'`），只是「行首前导空格」在 `LineRange` 里属下一行、点击落在**行首**而非行首之前——这与 `TextWidget` 的既有分段语义一致，不作额外定义；② **「扣了条槽但折行未生效」理论态**——`WrapRequested()` 真而 `IsWrapActive()` 假（**仅**「无头 + `ResolveMeasurer() == nullptr` + wrap on」可达）：`GetTextAreaWidth()` 已扣 14px，而 `RecalculateLines()` 走非折行支 ⇒ 文本区窄 14px 但仍是单行。**实际不可达**（`TestableTextBox` / `CountingTextBox` / `RecordingTextBox` 恒注入测量器；真窗口 `Window::GetTextMeasurer()` 恒非空）⇒ **不修**：修法一（让 `GetTextAreaWidth()` 依赖 `ResolveMeasurer()`）会破它的 `noexcept` 契约，修法二（折行支放宽到「无测量器也按折行建表」）等于给 N0 开新洞 | ① 由 T38-18④ 的区间归属断言兜住（不新增行为）· ② 如实登记为已知边界，不加约束、不加用例 |

---

## §8 影响面（与初设 §6 一致，本稿复核后确认）

| 项 | 值 |
|---|---|
| 公共头 | **100 → 100**（无新头；ⓑ 的「+1 内部头」路线已被依赖方向论证否掉） |
| public API | **+0** |
| protected 面 | **+7**（基类下移 3 + 基类访问器 1 + 派生 3）；★ private 新增 2 辅助（`LineKeyMatches`/`LineKeySave`）不计入 |
| 用例 | **513 → 531**（+18；★ v1.1 由 +17 增 1 = **T38-18**）；真窗口 **2** 条（T38-16 四件合一 + **T38-11 的切换相**，见 §9 v1.5 ①），其余 **16** 条无头 |
| CMake | **0**（测试全落既有 `TextBoxTests.cpp` / `WidgetTests.cpp`；`tools/phase38_wrap_probe.py` 是文档复算脚本，不进构建） |
| 文档锚点 | `docs/**` **197 → 198** · `docs/dev/**/*.md` **196 → 197**（本稿落盘即 +1） |
| 源码改动面 | `TextWidget.h`（4 下移/新增 + 注释扩写）· `TextWidget.cpp`（**9 处**，含 §2.1 ⑨）· `TextBox.h`（成员换芯 + 10 键字段 + 3 protected + 2 private + **七处**注释订正 + 删 2 字段）· `TextBox.cpp`（§2.2–§2.8：2 新函数 + 6 处接线 + 8 门 + 13 删 + 13 改名 + 9 注释）· 测试（3 夹具类 + 17 用例） |
| ★ 后端一致性口径 | 折行发生在**命令发射之前**（纯布局/几何，GDI/GL 零改动），但**断点由 `FitText` 决定** ⇒ 真字体跨后端折行数**不承诺相同**（#69：`Consolas` GDI 152 vs GL 133，**引用未复跑**）。像素级判据只在 `FakeTextMeasurer` 下跨后端可比。**不得写「GDI/GL 天然一致」** |

---

## §9 修订记录

- **v1.5（2026-10-29）** **批 4 回流（T38-11 / T38-14 / T38-16 落地，全 18 条预算闭合）**——四条判据口径订正 + 两条编译级订正 + 一条观察面登记：
  ① ★★★ **T38-11 的切换相必须真窗口，且 ② 的断言位置错**：`m_scrollOffsetX` 只在 `EnsureCaretVisible` 的 `else if (Window* window = GetWindow())` 支写入（`TextBox.cpp` 实测；`IsWrapActive()` 为真时钉 0、否则跟手写）⇒ 无头环境下**恒 0**，连「前置：非折行态滚出了偏移」都立不起来 ⇒ 真窗口由 **1 条变 2 条**。并且 `SetWordWrap`（`TextWidget.cpp:100-115`）只置双缓存失效 + `Invalidate()`、**不调 `RecalculateLines`** ⇒ 紧跟在 setter 之后的 `GetScrollOffsetX()` 读到的仍是旧值——v1.1 的「② `SetWordWrap(true)` ⇒ 断言 `== 0`」按字面写**必红**。⇒ 判据改为「② `SetWordWrap(true)` **+ 一次重建** ⇒ offset == 0；③ 同一帧 `Paint()` ⇒ 首条 `DrawTextCommand.pos.x == Padding()`」。★ 这三段合起来才是 D38-G 写点 3 的真正判别面：缺写点 3 则 ② 红；若只在 `EnsureCaretVisible` 里钉而在 `OnPaint` 之后才归零，则 ③ 红。
  ② ★★ **T38-14 的判据从「行数或区间必变」改为「`FitText` 被再次调用」**：假测量器（8 宽 / 16 行高）**忽略字体** ⇒ b/c/d 三维改 size/family/weight 时新旧行表**逐位相同**，用「行数变了」当判据是**假绿**（残表根本看不出来）；而「这一维改了之后有没有重新量」才是键真正保证的东西。★ 代价 = 需要 `RecordingFakeMeasurer`（`FitText` 计数 + 记录喂入串，§2.9④）。★ 但 **f/g 两维例外**：`SetWordWrap(false)` / `SetSingleLine(true)` 走非折行支，那条支**不调 `FitText`** ⇒ 判据换成「表形变成单行」（`ownedEnd(0) == 码点总数`）——旧折行表若不重建，这里会读到 5 而不是 9。★ 这三类判据分工写进了用例注释，不只是文档。
  ③ ★ **T38-14i 的 `compositionStart` 无法只变这一维（判据余量，如实登记）**：`m_compositionStart` 只在 `UpdateComposition` 的 `!m_isComposing` 首次分支写入（`TextBox.cpp`）⇒ 换起点必须「先取消再重新组合」，中间 `isComposing` **必然翻转**，三维修不可能隔离。⇒ 该子项只断言「换起点那一轮必重建 + 新位置的原文确实进了布局串」；隔离性由键的**单条 `&&` 链结构**保证（三项写在同一 return 表达式里，写一个就写了三个），**不能由行为隔离证明**。★ 这是判据的余量，不是缺陷——登记比假装验到更诚实。
  ④ **编译级订正一**：`TestableTextBox::FontForTest()` **不能**裸写 `m_style.font`——`TextBox` 自己有同名 `m_style`（类型 `TextBoxStyle`，**无 `font` 字段**）遮蔽了基类 `TextWidget::m_style`（`TextStyle`）⇒ 必须写 `TextWidget::m_style.font.value`。★ 这条也解释了为什么 `TextBox.cpp` 里到处是 `TextWidget::m_style.font.value` 的全限定写法——不是风格，是必需。
  ⑤ **编译级订正二**：`RecordingTextBox` 必须 `using TextBox::UpdateComposition;` / `using TextBox::CancelComposition;`——两者是 **protected**（`TextBox.h` 的组合区），`TestableTextBox` 早就 `using` 了，新类漏了就四条调用全报 "is protected within this context"。
  ⑥ ★ **T38-16③ 的期望值**：**无测量器**时 `GetPreferredSize()` = **当前尺寸 `{40, 64}`**，不是「单行内容高 16」——`TextBox::GetPreferredSize` 走单行支 → `TextWidget::GetPreferredSize` 的 `if (measurer == nullptr) return Widget::GetPreferredSize();` 兜底。⇒ 断言改为「挂窗前 = {40, 64}（诚实退回当前尺寸），挂窗后折行生效 ⇒ 高 > 64」。★ 这条同时是 §2.9 那类「期望值必须回指源码」的又一次实证：凭直觉写 16 会假红。
  ⑦ **T38-16④ 的观察面边界**：`Window` **不暴露**「待绘制标志」（`Invalidate()` 只是转调 `m_platformWindow->Invalidate()`）⇒「是否请求了重绘」这件事在公开面上不可观察。⇒ 该子件只断言「下一次 `Paint` 用的是新判据（2 → 1 → 2 条 `DrawText`）」；「请求了重绘」那一半由 **C38-9 的机检**（3 个 setter 体内均有 `Invalidate()`）兜住。★ 不假装用行为验到了机检能验的东西，也不反过来用机检替代行为。
  ⑧ **实测读数**：`-fsyntax-only D:/Project/ECDI/ECDI/src/Tests/TextBoxTests.cpp`（`-std=c++20`，3 s）**零 error**（两次编译级订正后）· `doc_lint` = **tests=531 headers=100 docs=198**、phase38 零 defect ⇒ 预算 513 → **531（+18）** 闭合 · ✅ **运行读数：四构建链（MSVC / Clang / ClangCL / MinGW）实测 531/531，0 failed**（2026-10-29）——批 0 的 515、批 1 的 516、批 2 的 523、批 3 的 528、批 4 的 531 五次读数全部来自真实构建（三次增量 + 一次删库冷全量 + 本轮四链）。★ **Phase 38 用例预算 513 → 531（+18）全部落地并通过**。
  ★ **未改动**：D38-A..I、10 维键、PD38-1..9、T38-1..18 条数与预算（531）、C38-1..9、R38-1..10 **全部不变**；§8 的唯一条数变化是真窗口 1 → **2**（① 的直接后果）。

- **v1.4（2026-10-29）** **批 3 回流**——五条用例落地（T38-8/9/10/12/17，预算 523 → 528）；一条**实质判据错误**订正：
  ① ★★★ **「判 clip 取最后一条」是错的（v1.2 ② 的结论有误）**：`Widget::Paint` 在 `OnPaint` 之后**还会遍历子控件**（`Widget.cpp` 的 `for(auto& child : m_children) child->Paint(...)`），而 `TextBox` 的垂直条是子控件（`m_vBar`，最后 AddChild）⇒ 条**可见时**它自己也 `PushClip`。于是命令序列是 `PushClip(self)` → `PushClip(文本区)` → `PopClip` → `PushClip(条)` → `PopClip` → `PopClip`，**`clips.back()` 是条（宽 12）而不是文本区**。⇒ 正确取法 = **按发射顺序收集，取 `clipWidths[1]`**（`[0]` 恒 = 控件自身边界，`[1]` = 文本区；仅当 `OnPaint` 未早退即文本非空）。★ 批 0 的 T38-1 用 `clips.back()` **当时是对的但不是因为它对**——那三组样本的条都不可见（内容高 ≤ 视口），子控件没参与；换成条可见的样本立刻错。★ 这条错误的影响面很具体：T38-17 的②（条可见半段）如果用 `back()` 断 38，会读到 12 ⇒ 假红；如果实现者「为了让它绿」而把断言改成 12，则判据彻底失去意义（12 是条宽，与 clip 语义无关）。
  ② ★ **T38-8 的判别面改用「行表相等」而不是读私有宽**：`GetTextAreaWidth()` 是 private，测试读不到 ⇒ 用两态（条隐藏 / 条可见）**行表逐项相等**当指纹。若宽依赖 `barVisible`，条可见态会少 14px ⇒ 折行宽 26→12 ⇒ `"abcdefgh"` 从 3 行暴涨，行表相等立刻红。
  ③ ★ **T38-17 的非折行半段改用 `H=10`**（原 §3.3 只给了折行两态）：`H=10 < 16`（单行行高）⇒ 内容溢出 ⇒ 条可见 ⇒ `GetTextAreaWidth()` = 38 − 14 = **24**，与折行态的 38 形成对照 ⇒ 证明两态**确实走了不同表达式**（若两态都断 38，这条分支没有判别力）。
  ④ **实测读数**：语法/类型自检 `g++ -fsyntax-only -Wall -Wextra` 通过（`EXIT=0`，3 s；新增代码零告警，剩余告警全在既有 `:1500/1516/1537/1769` 的 `TextBoxStyleOverride{ .x = ... }` 设计化初始化上，且正式构建不带 `-Wall`）· 运行读数待用户 CLion 构建后回填（预期 **528/528**）。★ **`-fsyntax-only` 单文件 3 秒 vs 冷全量 35 分钟**——以后提交前一律先跑它，别拿全量构建当编译器。
  ★ **未改动**：D38-A..I、10 维键、PD38-1..9、T38-1..18 预算（531）、C38-1..9、R38-1..10 **全部不变**。
- **v1.3（2026-10-29）** **批 2 实施回流 + 批 2/3 合并登记**——批 2 实测 **523/523**；四条判据/口径订正，一条纪律偏差登记：
  ① ★★★ **批 2 与批 3 的产品码合并落地**（§6 已就地注记三条硬理由：`RecalculateLines` 的折行支依赖 `GetTextAreaWidth()` 的常驻槽谓词、T38-3..7 的期望值依赖确定的折行宽、两批改的是同一批函数）。**测试仍按批次分两次加**（批 2 的 7 条已绿），故「行表」与「接线」在测试层面仍可分别归因；516 条存量在换芯 + 接线同时到位时**逐位全绿**。★ 这条偏差不是「省事」——若按原批次做，中间态会带着 E3 的自指环。
  ② ★★ **§2.9① 的夹具 `LineCountForTest(){ return m_lines.size(); }` 编译不过**（v1.0–v1.2 均有此错）：`m_lines` 是 `TextBox` 的 **private** 成员，**派生类的成员函数同样访问不到**——原文注释自己写着「`m_lines` 是 private ⇒ using 不够，需一个 public 中转」，但「中转」绕不过访问控制，只是换了个函数名。⇒ 改为 `RowCountForTest()`，用 **public 面反推**：`p = 0` + 假测量器行高 16 ⇒ `preferred.height == ceil(n × 16)`。★ 副产品是判据更强了（行表行数与 preferred 高度必须一致）。§2.9③ 的 `CountingTextBox` 同步订正。
  ③ ★★ **C38-3 的期望值错（5 → 9）**：**代码 5**（`RecalculateLines` 的折行宽采集 / `EnsureCaretVisible` 的 clamp / 定义 / `CalculateCaretPosition` 的 `maxCaretX` / `OnPaint` 的 clip 非折行支）+ **注释 4**。v1.0 估的「4 代码 + 1 注释」漏了两项：折行宽采集（批次合并的直接产物）与 D38-I 的两句论证（**必须**点名 `GetTextAreaWidth()` 才说得清「不取它」）。★ 为凑机检数字删掉 D38-I 的理由，等于把判据换成装饰。
  ④ ★ **T38-7 的样本数错（22 → 23）**：`tail`（`"ab\n"`）是 §3.1 表里独立的一行，v1.1 的「9 样本 × 2 档 + S8 组 4 = 22」把它漏了。它恰恰是「空行 `ownedEnd` 取自身」的唯一形态 ⇒ 漏它则边界等式② 少一半判别力。实测 **23/23 全绿**。
  ⑤ ★ **T38-3④ 的判据换形**：`CalculateCaretPosition` 是 **private**（`TextBox.h`），派生类不可 `using` 暴露 ⇒ 改断 `DrawTextCommand.pos.y == {0, 16}`。这更硬——它同时证明「布局行高」与「发射行高」是同一份（正是四路同源要证的那件事）。
  ⑥ **机检命令加 `--include=*.cpp --include=*.h`**（两条 `== 0` 硬门）：`ECDI/` 下有未跟踪草稿 `.scratch_p37.md` 会被 `-r` 命中。★ 同时定纪律：**注释里也不许留退役标识符**（统一改称「旧失效布尔位」/「旧行起点表」），否则「反正是注释」会成为真残留的掩护。这两条硬门现已实测 0/0。
  ⑦ **实测读数汇总**：`523 passed / 0 failed / 523 total`（MinGW g++ Debug，断言启用；全部无头用例 ⇒ 与显示器缩放无关）。★ **两次独立验证**：一次是增量构建（13:0x），一次是**删掉 153 MB 构建产物后冷全量重编**（14:3x，CLion 构建）⇒ 同一读数，排除「增量构建残留旧 obj 蒙对」的可能· C38-1 = 2/0/0 ✓ · C38-2 = 23/23 ✓ · C38-3 = 9（订正后）✓ · C38-6 = 0 ✓ · C38-7 = 3 ✓ · C38-9 = 3/3 setter 含 `Invalidate` ✓。
  ★ **未改动**：D38-A..I、10 维键、PD38-1..9 落点划分、T38-1..18 预算（531）、C38-1..9 条数、R38-1..10 条数**全部不变**。
- **v1.2（2026-10-29）** **实施回流（批 0 / 批 1 落地后）**——两处**判据口径**订正，方案本体与预算不变：
  ① ★★ **C38-5 的机检期望值错（4 → 2）**：v1.1 写「3 处构造 + 1 处写 = 4 处 `ownedEnd` 命中」，实测 **2 处**（1 注释 + 1 写）。根因 = 三处构造全用**位置初始化**（`TextLine{ 0, 0, 0.0f, 0 }` 等），**字段名不出现在源码**。⇒ 判据改为「2 处且无读取表达式」+ 可选的 `grep -c "\.ownedEnd"` == 1。★ 这一条**不影响红线② 的证明**（`TextWidget` 仍无任何读取），只是原期望值写错了。
  ② ★ **批 0 的 clip 断言口径**（v1.0 未提，写测试时才核出）：`Widget::Paint` 自己 `PushClip(self)`（`Widget.cpp:506`）⇒ 每次 `Paint` 至少 1 条 `PushClipCommand`；非空文本 **2 条**（Widget self + `TextBox.cpp:1369`）、空文本（`:1345` 早退）**1 条**。T38-1 据此断言条数 + 取 `clips.back()` 的 rect。★ 该结论对 **T38-17** 同样适用（判 clip 必须取**最后一条**，否则断的是 Widget 自身矩形——两者在 `p=0`、条不可见时数值相同，只有条数能区分）。
  ③ **批次实测**：批 0 = `515/515`（MinGW g++ Debug，`f013e7e`）· 批 1 = `516/516`（C38-4 = 0 ✓ / C38-5 = 2 ✓）。★ 实现期未发现新的锚点漂移（`TextWidget.cpp` 九处落点与 §2.1 表逐行对上）。
  ★ **未改动**：D38-A..I、10 维键、PD38-1..9 落点划分、T38-1..18 预算（531）、C38-1..9 条数、R38-1..10 条数**全部不变**。

- **v1.1（2026-10-29）** **v1.0 落盘后的独立对码复核**（不是新一轮评审；方法 = 逐条 `grep -n` / `Read` 亲核锚点 + `exec` 载入 `tools/phase38_wrap_probe.py` 的 `build_layout` 跑两版对账）。**三项实质订正 + 三项登记级**，每条均先验证再落稿：
  ① ★★★ **`ownedEnd` 取值错（`= b` → `= nextScan`）**——**A-15**。`FindLastSoftBreak` 对空格断点与 CJK 断点**返回位置语义不同**（前者空格后一位、后者字后一位），而 ⑦ 终化（`TextWidget.cpp:299-303`）把 `b` 之后的连续空格跳给 `nextScan` ⇒ 旧取法让这些空格**既不在 `drawEnd` 也不在 `ownedEnd`**。**脚本实证 4 类违反**：`"一二三 四"@24`（`gap=1` 而 `cps=' '`）· `"一二三  四"@24`（`gap=2`）· `"一二三 \n四"@24`（`gap=2`）· `"abc   "`@40`（**末行 `ownedEnd=5 ≠ count=6`**）。⇒ 订正 = `layout.lines.back().ownedEnd = nextScan;`（**+1 行**，§2.1 ⑨）；`(startCp, cpCount, width)` 与 `totalHeight` **逐字段不变**（红线② 结构性成立，11 个既有样本脚本比对确认）⇒ 复验 **11 既有 + 7 新增 = 18 项 A3 全绿**。连带：§2.1 八行→**九行** · **§3.4 S8 组**（4 样本）· T38-7 判据换形（`gap` 只允许 0 或 1∧`\n`，**无第三态**）· **T38-18** 新增 · C38-2 / C38-5 计数订正 · **R38-10①** · 用例 530 → **531**。
  ② ★★ **D38-G 的钉 0 落点不完备**——**A-17 的一半**。v1.0 把两个写点都放在 `EnsureCaretVisible`，而 `SetWordWrap(true)`（`TextWidget.cpp:100-115`）只 `Invalidate()`、**全仓 25 个 `EnsureCaretVisible()` 调用点无一在 setter 之后** ⇒ 「非折行态滚出 offset → 开 wrap → 首帧整段左移」的用户可见缺陷。⇒ 补**写点 3**（`RecalculateLines()` 折行重建分支末尾，§2.2），时序上该函数（`:1356`）在 `viewX`（`:1366`）之前 ⇒ **同帧生效**；并给出不变式 `U ≡ (IsWrapActive() ⇒ m_scrollOffsetX == 0)` 的三写点守恒论证。连带 C38-7 计数 2 → **3**（全仓 4）、T38-11 强化为**三段式切换相**（含负对照）。
  ③ ★ **`ResolveMeasurer()` 三次虚调用 + 解引用无判空**——**A-17 的另一半**。改为一次采集（`TextMeasurer* measurer = ResolveMeasurer();` + `wrap = WrapRequested() && measurer != nullptr`），与 §2.2 自己写下的「一次采集」纪律对齐；并把从不进键的 `bool wrap` 形参从 `LineKeyMatches` / `LineKeySave` **删除**（`/W4` C4100 + 语义误导）。
  ④ ★ **T38-15 的线性判据在含 `\n` 样本上必假红**（**A-16**）：空段走 `TextWidget.cpp:257-259` 的 `continue`、**不调 `FitText`** ⇒ `"ab\n"` 是 2 行但 `fitCalls == 1`。⇒ 判据限定「无 `\n` 的单段串」（S6）。
  ⑤ **`TextMeasurer.h` 的路径补全**为 `ECDI/include/ECDI/Render/TextMeasurer.h`（v1.0 只写裸文件名，实现者按 `Widget/` 找会扑空）。
  ⑥ **`WrapRequested()` / `IsWrapActive()` 分界造成的「扣槽未折行」理论态**入 **R38-10②**（实际不可达，不修）。
  ★ **未改动初设的任何决策**：D38-A..I 九条、10 维键、PD38-1..9 的落点划分、C38-1..9 的条数、R38-1..9 的条数**全部原样承接**；§0 的 A-1..A-14 锚点订正**全部复核为真**（本轮新增 A-15..A-17 属**本稿自身**的错，与初设无关）。★ **v1.0 的三条自评保留**：锚点复核方法、样本表脚本纪律、`D38-G` 的实现形态修正（「只跳过写入不足以钉 0」）在 v1.1 仍成立——v1.1 只是把它**再推一步**（写点位置也不够）。
- **v1.0（2026-10-29）** 详设初稿落盘。承接需求稿 v1.2 + 初设稿 v1.2，把 D38-A..I / PD38-1..9 翻译成可直接落键的代码骨架（§1 签名 · §2 逐文件逐函数 · §3 样本表 · §4 用例 · §5 机检 · §6 批次）。**本稿相对初设的七处实质推进（均已在正文就地标注，此处汇总留痕）**：
  ① **锚点漂移 14 条全部登记并订正**（§0 表 A-1..A-14）——其中 A-5（`TextBox.h:258` 引 `TextBox.cpp:657-658` 已失效，真实 `const_cast` 先例在 `:510-511`/`:679-680`）、A-6（「`SetStyle :99-127`」两处都不对）、A-10（`Widget.h:204` 是**注释**、声明在 `:294`、实现在 `Widget.cpp:585-593`）三条会直接把实现者带错路；A-14（S5c 两档宽在初设内部自相矛盾：§1.4 写 32、§8 写 16，而脚本实测 w24 与 w32 **输出完全相同** ⇒ 第二档零判别力）本稿定案 **32 / 16**。
  ② **`m_lineStarts` 注释站点计数 6 → 9**（A-12：`.cpp` 5 处 `:463/504/536/609/649` + `.h` 4 处 `:96/257/329/349`）⇒ §2.8 的改名清单补齐，C38-1 的 `grep == 0` 才真的是硬门。
  ③ **D38-G 的「单点钉」实现形态修正**：初设写「`:569` 写点外包一层 `if (!IsWrapActive())`」——**只跳过写入不足以钉 0**（从非折行态切过来时旧 `m_scrollOffsetX` 会残留，T38-11 的切换相必红）。本稿改为 `if (IsWrapActive()) { m_scrollOffsetX = 0.0f; } else if (Window*) { … }`，并**连带订正 C38-7 的机检计数**（`m_scrollOffsetX =` 由 1 处 → **2 处**，且两处都必须在 `EnsureCaretVisible` 体内；「单点」= 单**函数**，非单语句）。
  ④ **C38-1 的 `LineDrawRange` 命中数订正**：初设写「== 1」，实际 `grep -c` 会数到**定义 + 消费 = 2**；本稿拆成「总数 2、消费点 1」两条。
  ⑤ **C38-4 的机检范围订正为 `:202-310`**（A-1：初设写 `:202-309`，漏掉收尾 `}` 那一行）+ 追加一条**反向**机检（`:392-400` 内 `m_text` 命中 ≥ 2 ⇒ 钉住「`Label` 发射点必须保留」，防止实现者为了凑 `grep == 0` 把红线② 一起改掉）。
  ⑥ **§3 样本表由脚本重跑产出并补全**：初设 §1.4 只给了每样本的**一档**宽（S3 的第二档只记了 draw 端点、未记归属端点），本稿按 T38-7 的「9 样本 × 2 档 = 18 次对账」把**两档全部实算落表**（含 S3@24 的 `own3/own6/own10/own12`），并为此给 `tools/phase38_wrap_probe.py` 补了 5 个样本调用与一个 `SECOND` 两档扫描块（脚本改动**只增不改**，既有输出逐字不变）。
  ⑦ **夹具层的三条落地约束（初设未涉及，写骨架时当日核出——漏任一条都是「编译过但断言假绿」）**：㈠ `TextMeasurer` 的三个虚函数 **均非 const**（`TextMeasurer.h:50` `MeasureText` / `:68` `FitText` / `:141` `LineHeight`）⇒ 计数型/记录型测量器写成 `const override` **不可编译**；㈡ `TestableTextBox::ResolveMeasurer()` 返回的是**焊死的静态 `ms_fake`**（`TextBoxTests.cpp:66/68`）⇒ T38-15 的计数测量器与 T38-14h 的记录测量器**各需一个自己的可测类**（§2.9 ③④），不能往 `TestableTextBox` 上挂；㈢ ★ **`m_needsLineRecalc` 退役后，测试夹具必须自带重算**——旧写法「`using TextBox::RecalculateLines` + 用例里手动调一次」在键守卫下依然可行，但**忘调即读到上一轮的表**（假绿）⇒ §2.9① 的三个 `*ForTest` 中转**内建 `RecalculateLines()`**，并因此**不能是 const**。（另：`FitText` 的 `fitCp` 是**相对**码点数且**不得越串尾**（`TextMeasurer.h:19-22` 契约 C29-1）⇒ 计数型覆写必须 `min(按宽算的 n, total − startCp)`，否则 `hardEnd` 越界会让 T38-15 的「行数 == fitCalls」线性判据失真。）
  ★ **未改动初设的任何决策**：D38-A..I 九条定案、10 维键、PD38-1..9 的落点划分、T38-1..17 的用例预算（513 → 530）、C38-1..9 的契约条数、R38-1..9 的风险条数**全部原样承接**；上述七处均为**锚点/计数/实现形态**层面的订正，不触及方案本体。
  ★ **实现期回流规则**：任何「骨架不可编译 / 与红线冲突 / 又发现锚点漂移」都**先改本稿并在本节追加条目**，再改码。
