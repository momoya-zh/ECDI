# Phase 38 · TextBox 编辑器内软折行 —— 初步设计

> 状态：**v1.2**（2026-10-28 **自查订正**——D38-I 论据③ 的「条不透明 ⇒ 盖住溢出文本」前提**证伪**，连带订正五处表述并回流改写需求稿 A6①；v1.1 同日吸收外部评审「**有条件通过**」——**P0×3 / P1×4 / P2×7 全部落稿**，另加本侧复验衍生的 **3 项设计变更**；v1.0 同日落盘）｜上游 = 需求稿 `phase38-textbox-soft-wrap-requirements.md` **v1.2**（随本次吸收同步订正）｜审核稿 = `phase38-textbox-soft-wrap-design-review.md`｜台账 = `roadmap-deferred.md` **#77**（v2.57）
> ★★ **v1.2 订正摘要（一条论证的前提被证伪 ⇒ 方案本体不变、可见性账重记）**：v1.1 的 **D38-I 论据③** 称「条是子控件、后画，且 `ScrollBar.cpp:217` 是**不透明** `DrawRect` ⇒ 条可见时悬挂溢出段被条**覆盖**」。**实测为假**——默认主题 `ScrollBarStyle.trackColor = Color::FromRGBA8(240,240,245,**0**)`（`DefaultTheme.cpp:110`，v1.6「轨道默认透明」2026-10-01 用户拍板），后端对 `a < 1` 走预乘 `BlendAlphaSolid`（亲核 `GDIBackend.cpp:741-745` + `:73`）⇒ alpha 0 **零像素、盖不住任何文本**，不透明的只有**滑块**（`:111`）。⇒ **正确结论**：D38-I 的收益不是「常态可见 / 条态被盖」，而是「**两态恒可见**」——把 R38-5 的**可见性缺陷**降为**局部观感账**（条可见时悬挂尾可能与滑块重叠）；与「不改」的比较更强（不改 = clip 宽 == 折行宽 ⇒ **两态恒不可见**）。连带订正 **S4 / T38-9 / T38-17 / R38-3 / R38-5** 五处同族表述（v1.1 的 §8 条目⑤ **原文保留 + 行内标注作废**），并把执行注记写进 **C38-8**（凡以「绘制顺序/不透明度」为前提的论证，复核必须核到**主题默认值 + 后端 alpha 分支**，不得只核调用顺序）。上游回流：需求稿新增 **K15**、**范围项 7**、**D38-10**、**闸门 8**、**A5 白名单⑤**，A6① 豁免理由改写。★ **用例预算（513 → 530，+17）、决策定案 D38-A..I、风险条数 R38-1..9 均不变**。
> ★ **v1.1 评审吸收摘要**（评审方抽查约 80 处行号锚点约 74 处命中、实跑 `ecdi_tests.exe` = **513 passed**、核 HEAD `6a3807b` / 公共头 100，并**按 HEAD 源码把 `BuildTextLayout` 一比一复刻成 `tools/phase38_wrap_probe.py` 实算样本表与 A4 夹具**）：**P0-1** 显示串穿透漏了 `TextWidget.cpp:269`（决定「断在哪」的那次 `FitText`）⇒ 落点由「两处」改**三处语义落点 / 五处 `m_text`** · **P0-2** A4 的第二态 `H=32` 下**负对照也是假绿**（旧谓词两态同为 40/2）⇒ 第二态改 **`H=24`** + 新增「两态必须各自落在该实现的不动点上」的前置纪律 · **P0-3** §1.4 样本表 **3/7 与引擎实算不符**（S3 串丢一个空格、S5 行宽 w32 应为 w16、S7 第二断言恒真无判别力）⇒ 全表改由**脚本产出**、S5 整条换串、S7 换量具。★ **本侧复验衍生（评审未要求、本稿自加）**：**D38-I**（折行态 clip 宽取**控件内边界**，修 R38-5 的「整字永久不可见」）· **D38-D 去掉 `折行宽 > 0` 合取项**（否则 `0 < W ≤ 2p+14` 窄带内棘轮仍可达 = P1-3 的正解）· 失效键 **11 维 → 10 维**（`singleLine` 被 `wrapRequested` 严格覆盖；★ 组合三维**经复验必须保留**——`CancelComposition` 的快照支存在越序点，见 §1.3 维 10 与 C38-6）。
> ★ **本稿任务**：逐条收口需求 **§7 闸门七项**（★ v1.2：需求已追加**第 8 项** = D38-10 的 clip 宽四问，本稿在 v1.1 时即以 D38-I 回答完毕 ⇒ **八项全部收口**，详见 §1.7 补充确认④），并给出**可实施的逐文件落点**。沿 Phase 31 / 37 纪律：**§2 定案须给论证，倾向 ≠ 定案**。
> ★ **事实纪律**：所有行号 = **2026-10-28 对 HEAD `6a3807be19a30d8512e18ef2d68bf2d1b7842cbf` 实测**（`git log -1` 核实，逐条 `grep -n`/`sed` 亲核；v1.1 吸收时**每条评审结论亦独立复验后才落稿**，复验结果如实记于 §8）；harness 侧读数（`ComposerBox p=6`、`REQ-15` 的「一行即可」、#69 的 152/133）= **引用未复跑**（条 105）。规模锚点：公共头 **100** · 用例 **513** · `docs/**` **197** / `docs/dev/*.md` **196**（本日实测，含审核稿本身）。
> ★ **样本表纪律（v1.1 新增，P0-3 的教训制度化）**：§1.4 全表的期望值**一律由 `tools/phase38_wrap_probe.py` 实算产出**（该脚本 = HEAD 引擎的一比一复刻：硬段切分 → `FitText` 码点二分 → `FindLastSoftBreak` → ④⑤ 禁则 → ⑦ 跳空格 → 尾部空白裁剪），**详设抄脚本输出、不得手推**——v1.0 手推 7 串失准 3 串，正是 P0-3 的成因。
> ★ **本稿的两条硬红线（贯穿全稿，任何落点都要过这两关）**：① `SetWordWrap` 缺省 `false` ⇒ 既有 513 条用例 + 命令级读数**逐位不变**；② `Label` 侧 `BuildTextLayout` 的既有输出（`startCp` / `cpCount` / `width` / `totalHeight` / 空格归属）**逐位不变**——编辑器的差异一律由**新增字段/新增参数/新增分支**承载。

---

## §1 初设必答（需求 §7 闸门七项逐题收口；★ v1.2：需求追加**闸门 8** = D38-10 的 clip 宽四问，由 §2 的 **D38-I** + §4 **T38-17** + §5 **C38-3/C38-8** + §7 **R38-3/5/9** 回答，收口情况见 §1.7 的 v1.2 补充确认④ ⇒ **八项全部收口**）

### 1.1 问一 = `LineRange` 新契约全文（四态 + 端点口径 + A3 机检推导）

**口径声明（闸门点名要的答案，不留「实现时再定」）**：

| 入口 | 端点取 | 语义 | 消费者 |
|---|---|---|---|
| `LineRange(i)`（既有名，**签名不变**） | `{ startCp, ownedEnd }` = **归属口径** | 本行「负责」的码点区间；软断点分隔空格**在内**、硬断 `\n` **不在内** | `LineIndexFromCodepoint` 邻域 · `CaretIndexFromLineX`（`:789-791`）· Selection 求交（`:1388-1398`）· IME 下划线求交（`:1412-1421`）· 双击选词的可达性 |
| `LineDrawRange(i)`（**新增**） | `{ startCp, startCp + cpCount }` = **裁剪口径** | 本行**实际发射**的文本 | `OnPaint` 行文本 `DrawText`（区间与裁串 `:1378-1381`、发射 `:1403-1405`）单点 |

★ **为什么绘制要用裁剪口径（★ v1.1 换论证——v1.0 的算式不成立）**：v1.0 写「若绘制改吃 `ownedEnd`，行视觉宽超出折行宽一格（`S1` 下 40→48）」——**复算证伪**：S1 在 `ownedEnd` 口径下的行宽恰 = 48 = 折行宽，**不构成超出**（`ownedEnd` 只把已被 `FitText` 收进窗口的分隔空格算回来）。正确理由两条：
① **命令层洁净 + 两消费者发射口径一致**：软断点处的分隔空格对读者不可见（它*就是*断点），发射给后端 = `DrawTextCommand.text` 带一串无意义尾空格；且 `Label` 侧恒发裁剪串（`TextWidget.cpp:396-398` 按 `startCp`/`cpCount` 取字节）⇒ 若编辑器发归属串，两个消费者的命令级读数**结构性分叉**，T38-1/T38-3 取 `text` 的断言无法与既有 `Label` 基准同表。
② **语义分离本身**：绘制要答「本行实际发射什么」，可达性/选区/命中要答「本行负责哪些码点」——两者在软行上**必然不同**（差 = 行尾被裁空格数），一个字段承载不了两个语义（这正是 D38-2 选择「加 `ownedEnd`」而不是「改 `cpCount` 口径」的原因，= 需求 P0-2 要求的两口径分离）。
★ **并订正评审给出的替代论据（不得照抄）**：审核稿 §5 末尾主张「`ownedEnd ≤ hardEnd ≤ FitText 边界` ⇒ 用 `ownedEnd` 绘制不会超宽」。该不等式在 **⑤ 悬挂态不成立**——`:295` 的上界取 `segEnd` 而非 `hardEnd`（`:292-294` 在案注释即为此），S4 实测 `ownedEnd 4 > hardEnd 3`、行宽 32 > 折行宽 24。⇒ 两口径分离的**结论不变**，但正确表述是：两口径**只差行尾空格**（`ownedEnd − drawEnd` = 被裁空格数）；**无 ⑤** 时 `b ≤ hardEnd` ⇒ 归属口径也不超宽；**有 ⑤** 时两口径**同等超宽**（超宽来自禁则上提，与口径选择无关）⇒ 超宽的处置属 **D38-I / R38-5**，不属本条论证。

**四态契约（`count` = 显示串码点数；`display` 与真实串码点 1:1，`TextBox.cpp:231-254`）**：

| 态 | 判据 | `drawEnd` vs `ownedEnd` | 与下一行的关系 | 例（`FakeTextMeasurer`，8 宽/行高 16；★ 脚本实算） |
|---|---|---|---|---|
| **软行**（空格/CJK/硬断点结束） | 行尾由 `b < segEnd` 给出，`b` 后有被裁掉的分隔空格 | `drawEnd ≤ ownedEnd`，差 = 行尾空格数 | **`ownedEnd(i) == start(i+1)`** | `S1` L0：`{0, drawEnd 5, ownedEnd 6}`，L1 `start 6` |
| **硬行**（`\n` 结束） | `\n` 位于 `ownedEnd(i)` | `drawEnd ≤ ownedEnd`（`\n` 前的行尾空格同样被裁） | **`ownedEnd(i) + 1 == start(i+1)`**（`\n` 本身不属任何一行——与 `TextBox.h:264-265` 在案契约同型） | `S3` L1：`ownedEnd 6`，L2 `start 7` |
| **末行** | `i == size()-1` | 非折行态 `drawEnd == ownedEnd`；折行态文档尾空格只裁 `drawEnd` | **`ownedEnd == count`**（`End` 键 = `GetCodepointCount()` `:1235` 恒可达） | `S6` L2：`{6, drawEnd 8, ownedEnd 8}` |
| **尾部空行**（`"ab\n"` 型） | 空段 ⇒ `cpCount 0` | `drawEnd == ownedEnd == segStart` | 空行**占行高、零绘制命令**（D29-Ⅴ 语义原样保留） | 脚本实算 `"ab\n"` w40 ⇒ L0 `{0,2,w16,own2}` · L1 `{3,0,w0,own3}`，与今日 `LineRange(1) == (3,3)` **逐位同** |

**A3 机检式的推导**（由上表两条等式直接拼出，不需要抽样）：把全表按 `i` 遍历求 `Σ (ownedEnd(i) − start(i))`，并把每行的「`\n` 缺口」`gap(i) = start(i+1) − ownedEnd(i) ∈ {0, 1}` 相加 ⇒ 若首行 `start == 0`、末行 `ownedEnd == count`，且每处 `gap ∈ {0,1}` 且 `cps[ownedEnd(i)] == U'\n' ⇔ gap == 1`，则**每个码点属于恰一行（`\n` 除外）**。⇒ 用例写成「遍历全表逐行对账 + 三条边界等式」，一条用例即钉死 `−1` 类缺陷（旧实现 `:480-481` 对软行会稳定少吐最后一个真实码点）与归属缺陷（`ownedEnd` 若不等于下一行起点 ⇒ 分隔空格无人认领 ⇒ 点击越界）。★ 脚本已内建该对账（`show()` 的 `A3 gap check`），S1–S7 全表**逐串 OK**（含新增的 S5/S5b/S5c），详设可直接把该输出贴进用例注释作为期望来源。

★ **`TextBox.h:349` 的口径冲突随本 Phase 一并订正**：注释写「行数 = `size()-1`」而实现按 `size()`（`OnPaint :1370`、`GetMaxScrollOffset :529`、`OnKeyDown :1251`）⇒ 落地时注释改「行数 = `size()`（含尾部空行）」，**不得让实现者按注释口径写折行代码**。

### 1.2 问二 = 折行宽单一来源：算式、独立性证明、显式推翻 `:669`、A4 正/负对照

**算式（唯一入口 = 既有的 `GetTextAreaWidth()` `:662-674`，不新建宽度函数）**：

```
deduct(i) = (i.barVisible || i.wrapRequested) ? (i.barThickness + max(padding, 2)) : 0
wrapWidth == GetTextAreaWidth() == GetWidth() − 2·padding − deduct
```

其中 `wrapRequested = IsWordWrap() && !m_singleLine`（**纯配置量、`noexcept`**——`GetTextAreaWidth()` 现签名带 `noexcept`，不得为它引入可能抛出的调用；`ResolveMeasurer()` 非 `noexcept` ⇒ 只进 `IsWrapActive()`，见 §3 PD38-3）。`barThickness` 恒为主题值 **12**（`DefaultTheme.cpp:115`；条构造体内即 `ApplyTheme`，`ScrollBar.cpp:17` ⇒ **无头也拿到 12**）；★ 复验补强：条**无条件**在构造期创建（`TextBox::TextBox()` `:57` → `CreateVerticalScrollBar()` `:59`，体内 `:42-56` 的 `AddChild`）⇒ `m_vBar != nullptr` 在无头恒成立，`:670` 的判空不会把扣条短路掉，本 Phase 的数值构造因此**不依赖窗口**。`ScrollBarInset(padding) = max(padding, 2)`（`:34-36`）⇒ `p=0` 时扣 **14**。

**独立性证明（E3 环的形式解）**：记 `n = |BuildTextLayout(measurer, display, wrapWidth)|`。
- 改前：`wrapWidth` 依赖 `barVisible = (n·L > A)` ⇒ `n = f(g(barVisible(n)))` = **自指**；
- 改后：`wrapWidth` 的输入集 = `{GetWidth, padding, barThickness, wrapRequested}`，**不含** `barVisible`、**不含** `n` ⇒ `n` 不再出现在自身的依赖图里 ⇒ 不动点**平凡存在且唯一**（同一输入 ⇒ 同一输出，无迭代）。
- 单调性旁注（需求 v1.1 已把「逐帧振荡」订正为「单调收敛」）：本方案不是「让振荡变慢」，而是**把自指摘掉**——振荡与收敛都是自指系统的性质，解除了自指就不必再讨论收敛性。

**显式推翻 `:669`（必须在正文留字，不得静默改代码）**：该注释「★ 不会反向影响内容高：`GetMaxScrollOffset` 只经 `GetTextAreaHeight`（高度）⇒ 无循环依赖（K9 / D4）」在其时代**为真**——因为内容高 = `(\n 数 + 1) × L`，与**宽无关**，所以「宽扣条」不可能反馈到「内容高」。本 Phase 把内容高改成「视觉行数 × L」，而视觉行数由**宽**决定 ⇒ 前提消失，该论证**作废**。新论证 = 上段独立性证明；`GetTextAreaHeight()` 与条摆位（`LayoutScrollBar`）**一行不改**（§3 非目标）。

**A4 正/负对照的形式论证（P0-3 点名要求，给数字不给口号；★ v1.1 按 P0-2 换第二态）**：
夹具 = `TestableTextBox`（`FakeTextMeasurer` 8 宽 / 16 行高）· `padding = 0` · `width = 40` · 文本 `"abcdefgh"`（8 码点，无空格无 CJK ⇒ 只能按可用宽硬断）· `wrapRequested = true`。
★ **前置纪律（P0-2 的教训，写死）**：**两态必须各自落在该实现的不动点上**——若某 `H` 下「条可见态」对该实现根本不存在（内容高恰等于视口 ⇒ `GetMaxScrollOffset() == 0` ⇒ 条永不出现），则两态实为同一态，负对照**失去判别力**。v1.0 取 `H=32` 正踩此坑（旧谓词下 `48−32` 与 `32−32` 都判不出条）。

| 实现形态 | 态 A：`H=64`（条隐藏） | 态 B：**`H=24`**（条可见） | A4 判据（**同一实现**两态的 `wrapWidth` 与行数逐项相等） |
|---|---|---|---|
| **本方案（常驻扣条槽）** | `wrapWidth = 40−14 = 26` ⇒ fit 3 字 ⇒ **n=3**，内容高 48 ≤ 64 ⇒ 条隐藏 ✓ 自洽 | 同一 `wrapWidth = 26` ⇒ **n=3**，48 > 24 ⇒ 条可见 ✓ 自洽 | **绿**（26/3 == 26/3） |
| **负对照（回退 `:670` 谓词为「仅条可见才扣」）** | `wrapWidth = 40` ⇒ fit 5 字 ⇒ **n=2**，内容高 32 ≤ 64 ⇒ 条隐藏 ✓ 自洽 | 迭代到 `wrapWidth = 26` ⇒ **n=3**，48 > 24 ⇒ 条可见 ✓ 自洽 | **红**（40/2 ≠ 26/3） |
| ★ **v1.0 的第二态 `H=32`（作废）** | 本方案 `(26,3,隐藏)`；负对照 `(40,2,隐藏)` | 本方案 `(26,3,可见)`；负对照 **`(40,2,隐藏)`**（32−32=0 ⇒ 条永不出现） | **负对照两态同为 40/2 ⇒ 假绿**——与「5 帧恒定」同一类判据缺陷，只是换了位置复现（P0-2） |
| **v1.0 的「连续 5 帧读数恒定」（作废）** | 两态各自都是**自洽不动点** ⇒ 帧间恒定 ⇒ 未修代码也绿 | 同 | **假绿**（需求 P0-3 的原证） |

⇒ A4 的判据形态 = 「同一控件、两态（条隐藏 / 条可见）⇒ `GetTextAreaWidth()` 与逐行区间**逐项相等**」，且**回退本条谓词必须让该用例变红**（实施期以「回退生产修复 + 只留用例」的既有轮次手法自证；若实测该夹具不具判别力 ⇒ 按需求 A4② 如实登记缺口，不以「代码已改」代替验收）。
★ **复算证据（本侧复跑 `tools/phase38_wrap_probe.py` 的 `fixed_point()`，逐字摘录）**：`H=64 NEW=(26.0, 3, 48.0, False) / OLD=(40.0, 2, 32.0, False)`；`H=32 NEW=(26.0, 3, 48.0, True) / OLD=(40.0, 2, 32.0, False)`；`H=24 NEW=(26.0, 3, 48.0, True) / OLD=(26.0, 3, 48.0, True)`——即 OLD 在 `(64,32)` 两态同值（假绿）、在 `(64,24)` 两态异值（红）✓。
**残余观感账（gate ② 的正面回答）**：条隐藏时右侧留出一格**永不使用的 14px 槽**，文本比可见区早断一格——这是 Notepad 型折行的既有做法，属**有意代价**，入账于 §7 R38-3。「装得下却冒条」不会由宽度侧引入：`MultilinePreferred` 返回 `H = ceil(n·L + 2p) ≥ n·L + 2p` ⇒ `A = H − 2p ≥ n·L` ⇒ `GetMaxScrollOffset() = max(0, n·L − A) = 0` ⇒ **`AutoSize` 后条必隐藏**（K8 契约① 的 `ceil` 归属保持不变，本 Phase 只在 §1.6 声明 `L` 口径）。★ v1.1 补：这 14px 槽**不再吃掉悬挂字符**（clip 宽改取控件内边界，D38-I）——槽只影响折行位置，不影响可见范围。

### 1.3 问三 = 失效键逐维清单（每维 → 一条 A8 断言；登记与判别成对）

键 = `TextBox` 私有字段组，`RecalculateLines()` **入口每次比对**（C29-10 手法，不依赖任何 setter「记得清」）。★ **v1.1 维数订正 = 10 维 / 10 个键字段**（v1.0 写 11 维）：**删 `singleLine` 维**（被 `wrapRequested` 严格覆盖，论证见下）、**保留组合三维**（v1.0 的理由是假的、但结论对——真实理由 = `CancelComposition` 的越序点，见维 10）。删维沿用的正是本稿退役 `m_needsLineRecalc` 的同一条纪律：**键里不留可被其它维推导的冗余字段**。

| # | 维度 | 来源 | 变了不重排会怎样（反例） | A8 判别 |
|---|---|---|---|---|
| 1 | `revision` | `TextWidget::m_textRevision`（**private `:140`** ⇒ 本 Phase 新增 protected 访问器 `TextRevision()`） | 键入/粘贴/剪切/Undo 后行表停在旧文本 ⇒ 绘制缺字、命中错位 | 正：追加文本 ⇒ 行数变（T38-14a）；反：同值不改 ⇒ 零测量（T38-15） |
| 2 | `font.size` | `TextWidget::m_style.font.value.size` | 字号变小但行表仍按大字号断行 ⇒ 每行溢出被 clip、超出部分**不可达**（`scrollOffsetX ≡ 0`） | T38-14b |
| 3 | `font.family` | 同上 `.family` | 换窄族 ⇒ 旧行表提前断（观感）；换宽族 ⇒ 旧行表超宽被裁 | T38-14c |
| 4 | `font.weight` | 同上 `.weight`（Phase 36 K8-④ 的同族账：**漏加过一次**） | 加粗后字宽变大不重排 ⇒ 行尾超折行宽、被 clip 不可达 | T38-14d |
| 5 | `dpi` | ★ v1.1 订正代码（P2-5）：`Widget` 上**没有** `GetDpiScale()`（只有 `Window::` / `PlatformWindow::`）⇒ v1.0 的 `lround(GetDpiScale()×96)` **不可编译**。既有口径 = `const Window* window = GetWindow(); const int dpi = (window != nullptr) ? static_cast<int>(std::lround(window->GetDpiScale() * 96.0f)) : 0;`（`TextWidget.cpp:316-321` 同式，无窗口 = **0**、**不短路**） | 跨屏移动不重排 ⇒ 整树按旧 DPI 的宽断行 | ★ **无用例**——本仓无多屏/无头改 DPI 夹具 ⇒ 缺口如实入账 R38-2（机制与 C29-10 已证维度同型，非新增风险面） |
| 6 | `wrapWidth` | 本 Phase 新依赖（**`SetSize` 今天在 `:256-265` 完全不置行表脏** = K7 的宽度类缺口） | 缩窗不重排 ⇒ 行宽 > 新折行宽，右侧被 clip 且不可滚达 | T38-14e（两态宽度各跑一遍，兼作 A4 的姊妹断言） |
| 7 | `hasMeasurer` | `ResolveMeasurer() != nullptr`（无头 ⇒ 恒 `\n`-only；挂窗 ⇒ 可折行） | 先无头求 preferred、后 `AddChild` 挂窗 ⇒ 行表停在 `\n`-only，文字一行到底 | T38-16（真窗口侧；白名单④） |
| 8 | `wrapRequested` | `IsWordWrap() && !m_singleLine` ⇒ **一个字段承载两个开关** | `SetWordWrap(false)` 后行表仍折行 ⇒ 关掉开关文字还在折；`SetSingleLine(true)` 后仍按折行行数算内容高 ⇒ 纵向偏移被 `:542` 钉死、下方行**永久不可达**（K14） | T38-14f（`SetWordWrap` 分量）+ T38-14g（`SetSingleLine` 分量）——**两个开关各造一次「只变这一维」** |
| 9 | `echoMode` | `m_echoMode` | 切 `Password` 后掩码点宽与原文不同却沿用旧行表 ⇒ 折行位置与绘制不符（K9） | T38-14h + S7 量具（§1.4） |
| 10 | `composing / compStart / compLen`（三维合一行） | `m_isComposing` / `m_compositionStart` / `m_compositionLength` | ★ **v1.1 换理由（v1.0 的前提经复验为假，P2-6）**：v1.0 称「唯 `UpdateComposition` 的纯游标推进不 bump」——**该路径不存在**（`UpdateComposition` 每次都 `ReplaceTextRange` `:906-907` + `m_needsLineRecalc` `:912` + `MarkTextChanged()` `:913`）。**真实的越序点在 `CancelComposition` 的快照支**：`:952` `RestoreSnapshot(snapshot)` 体内 `:1085` bump **之后**、`:1087` 就调 `EnsureCaretVisible()` ⇒ 该调用经惰性门 `:537` 触发一次重排**并把键存成「新 revision + 旧组合标志」**；而清标志的 `:955-958` 在 `RestoreSnapshot` **返回之后**才执行、且**不再 bump** ⇒ 若组合三维不在键里，掩码框取消组合后的行表**永久停在幻影组合态**（`BuildDisplayText` `:241-243` 对 `[compStart, compStart+compLen)` 显示原文，其余打点 ⇒ 显示串确实不同） | T38-14i（掩码 + 组合 + `CancelComposition` 快照支 ⇒ 行表 == 直接构造同文本的行表）；★ 判别力**只在 `EchoMode::Password` 下存在**（Normal 态 `BuildDisplayText` 直通 `:232-233`，组合标志不入显示串）——用例必须显式开掩码，否则恒真 |

★ **`singleLine` 不单列一维的证明（v1.1 删维的依据）**：`m_singleLine` 对行表的全部影响只经 `wrapRequested` 一个通道——折行支的入口判据含它（`WrapRequested()`），非折行支是**纯 `\n` 扫描**（不读 `singleLine`、不读任何宽度），且 `BuildDisplayText()` 也不读它（`:231-254` 只依赖 `m_echoMode` + 组合三维）⇒ `singleLine` 变 ⇒ `wrapRequested` 必变 ⇒ 维 8 已覆盖。**保留它 = 一个永不独立触发的冗余字段**，与退役 `m_needsLineRecalc` 是同一类判断。★ 但**验收不删**：T38-14g 仍单独造「只变 `singleLine`」的用例（判别的是**维 8 的覆盖性**，不是维 9 的存在性）。

★ **`m_needsLineRecalc` 退役（D38-5③ 的答案）**：本日实测 **13 处置脏站点与 13 处 `MarkTextChanged()` 严格成对**（`TextBox.cpp:128/139/306/328/358/371/389/401/912/934/969/999/1084` 对 `:129/140/307/329/359/372/390/402/913/935/970/1000/1085`，逐行 `grep` 亲核）⇒ 维 1（`revision`）**严格覆盖**该布尔位的全部信息量，其余 9 维更是它覆盖不到的 ⇒ 保留 = 一个写而不读的标志位（违反「不留死代码」）。退役落点 = 删字段（`TextBox.h:350`）+ 删 13 处写入（含 `RecalculateLines` 自己的 `:457` 复位）+ **8 处惰性门**（`:510/537/611/650/679/770/1246/1355`）改为无条件调用 `RecalculateLines()`（体内键判定）+ 订正三处引用它的注释（`TextBox.h:257/261`、`TextBox.cpp:536/609/649`）。★ **名字保留**是刻意的：8 处调用点的**形态零改动** ⇒ 消费点全表的 diff 面积最小，也让 C38-1 的机检只看一张表。

### 1.4 问四 = 无头可算性（`FakeTextMeasurer` 8 宽 / 16 行高下的样本表，可直接抄进详设）

**坐标口径先钉死**（沿 Phase 37 §1.7 手法，A2 的断言前提）：

| 量 | 口径 | 依据（本日亲核） |
|---|---|---|
| `CaretIndexFromPosition(Point)` 入参 | **控件局部** | 调用点全部先减 `abs`；`:777` `localPos.y − GetTextTopInset() + m_scrollOffsetY` |
| `CaretIndexFromLineX(lineIndex, innerX)` 入参 | **逻辑前缀宽**（= 可视 x − 左内缩 **+ scrollOffsetX**） | `:781` 换算 + `:820` `ResetPreferredColumn` 同式；★ 折行态 `scrollOffsetX ≡ 0` ⇒ innerX 就是可视 x − padding |
| `CalculateCaretPosition` 返回 | **控件局部** | `:698` `Point{ GetTextLeftInset()+caretX, GetTextTopInset()+caretY }` |
| `DrawTextCommand.pos` | = `Paint(ctx, x, y)` 传入值 + 内缩（命令层不再加偏移） | `OnPaint :1362-1364` `textX/textY` · `:1366` `viewX` |
| 无头 `L` | **`GetLineHeight()` 恒 16.0f**（`:486-493` 硬编码，**不走** `ResolveMeasurer()`） | ★ 与 `measurer->LineHeight()` 在 `FakeTextMeasurer` 上同值 ⇒ 见 §1.6 |

★ **无头降级面（A2② v1.1 改形的落点）**：`CaretIndexFromLineX` 判 `GetWindow() != nullptr`（`:796`）⇒ 无头时行内 X 定位**恒返回行首**。⇒ 无头判据只写「第 i 行视觉 y ⇒ 命中该行**起始码点**」，行内精确定位列 T38-16（真窗口白名单③）。

**样本表**（★ v1.1 全表由 `tools/phase38_wrap_probe.py` 实算产出，本侧复跑核对；`width` = 该行**裁剪后**宽；`区间` = `[startCp, drawEnd)` ‖ `归属` = `ownedEnd`）：

| # | 文本（码点数） | 折行宽 | 期望视觉行（脚本实算） | 钉住的缺陷 |
|---|---|---|---|---|
| **S1** 拉丁空格 `"ab cd ef"` (8) | 48 | L0 `[0,5)` w40 归属 6 · L1 `[6,8)` w16 归属 8 | 软断点分隔空格归 L0（`ownedEnd(0)==start(1)`）；旧 `−1` 会吐掉 `cp5` ⇒ **A3 + A6① 双判别** |
| **S2** CJK 无空格 `"一二三四五六"` (6) | 40 | L0 `[0,5)` w40 归属 5 · L1 `[5,6)` w8 归属 6 | `IsCjkBody`（★ v1.1 锚点订正 **`:173-179`**）断点；**无空格可裁** ⇒ `drawEnd == ownedEnd`（对照组，证明归属规则不会凭空吞码点） |
| **S3** 混排 `"ab cd \nef gh"`（★ v1.1 补 `\n` 前那个空格 ⇒ **12** 码点；v1.0 写 12 但串只有 11 ⇒ 期望三行**对不上自己的串**，P0-3） | 40 | L0 `[0,2)` w16 归属 3 · L1 `[3,5)` w16 归属 6 · L2 `[7,12)` w40 归属 12 | 硬行 `ownedEnd+1 == start`（L1→L2：6+1=7）与软行 `ownedEnd == start`（L0→L1）**同表混存** ⇒ E2 的正解；`FitText` 越段（L1 的 `hardEnd=8 > segEnd=6`）被 **★ `:273`**（v1.0 误写 `:266`，该行是 `const std::size_t lineStart = scan;`）收敛 ⇒ 复用引擎的真实行为。★ 第二档宽 24 实算 = **4 行**（`[0,2)/[3,5)/[7,9)/[10,12)`），T38-7 直接取用 |
| **S4** 悬挂微超 `"一二三。四"` (5) | 24 | L0 `[0,4)` **w32**（> 折行宽）归属 4 · L1 `[4,5)` w8 归属 5 | ⑤ 行首闭标点禁则（`:295`）上提 `。` ⇒ 行宽 32 > 24 ⇒ **A6① 的豁免面**；★ v1.1 处置换方案：被裁的 8px = **一整个码点**（不是「微超」），`scrollOffsetX ≡ 0` 下**永久不可见而光标可停** ⇒ 由 **D38-I** 把 clip 宽改成控件内边界修掉（★ v1.2 订正：**两态都可见**，不是「条隐藏态可见」——轨道 alpha 0，条盖不住文本，见 D38-I 论据③），残余账见 R38-5 / R38-9 |
| **S5** 行尾开括号回退 `"ab（cd"` (5) | 24 | L0 `[0,2)` w16 归属 2 · L1 `[2,5)` w24 归属 5 | ★ **v1.1 整条换串**（v1.0 的 `"一二三（四"` **根本不触发 ④**——纯 CJK 串里每个表意字都是软断点，`FindLastSoftBreak`（`:190-196`）在 `（` 之前就返回了 ⇒ ④ 的 `while` 一次都不进）。换拉丁前缀后：`hardEnd=3`（`ab（`）、无软断点 ⇒ `b=hardEnd=3` ⇒ ④（`:286`）见 `cps[2]==（` ⇒ `--b` ⇒ `b=2` ⇒ `（` 被挪到下一行 ✓ **真正判别 ④**（E1「复用即继承字面集」） |
| **S5b** ④b 守卫 `"（a"` (2) | 8 | L0 `[0,1)` w8 归属 1 · L1 `[1,2)` w8 归属 2 | ★ v1.1 新增：首字符即开括号且只放得下一字 ⇒ ④ 把 `b` 退到 `lineStart` ⇒ **④b 守卫 `:289`** 恢复「按字硬断一字」⇒ 不产生空行（D29-Ⅴ：空行只来自显式 `\n`）。缺这条则「禁则把行退成空行」的边界**无覆盖** |
| **S5c** 纯 CJK + 开括号（对照组，= v1.0 的 S5） `"一二三（四"` (5) | 32 | L0 `[0,3)` w24 归属 3 · L1 `[3,5)` **w16**（★ v1.0 误写 w32）归属 5 | 保留为**对照组**并写清结论：**④ 在纯 CJK 串上不可达**（软断点先于禁则命中）⇒ 详设不得把本串当作 ④ 的用例；它判别的是「CJK 断点 + 裁剪宽」的组合，与 S5 互补 |
| **S6** 超长词硬断 `"abcdefgh"` (8) | 26 | L0 `[0,3)` w24 · L1 `[3,6)` w24 · L2 `[6,8)` w16（归属均 = 裁剪） | 无软断点 ⇒ `FindLastSoftBreak` 返回 `hardEnd`（★ v1.1 锚点订正 **`:190-196`**）；★ 同一样本换 40 / 26 两档宽即 A4 的正/负对照构造（§1.2 表） |
| **S7** 掩码哨兵 `"secret"` (6)，`EchoMode::Password` | 40 | L0 `[0,5)` w40 归属 5 · L1 `[5,6)` w8 归属 6（与非掩码同串**逐字段相等**） | ★ **v1.1 换量具（P0-3）**：v1.0 的「第二断言 = `"ab cd ef"` 掩码态归属表与 S1 逐字段相等」**恒真无判别力**——`FakeTextMeasurer::MeasureText` 按**码点数 × 8** 计宽（`TextBoxTests.cpp:34-40`），而掩码串与真实串**码点 1:1**（`TextBox.cpp:231-254`）⇒ 喂 `m_text` 还是喂 `BuildDisplayText()` 结果都相等，等值证明不了任何事。**新量具（取审核稿建议 ②，①留作后备）**：记录型测量器 `RecordingTextMeasurer` 覆写 `FitText`/`MeasureText`，断言**传入串的字节**含 `E2 80 A2`（U+2022）且**不含**明文字母 ⇒ 直接钉「折行吃的是显示串」（C38-4）；后备 ① = 按字符宽度区分的测量器（`•` 记 8、ASCII 记 4），喂错串则行数必变 |

⇒ **一条 A3 用例遍历 S1–S7 全表（9 个样本：S1/S2/S3/S4/S5/S5b/S5c/S6/S7）× 每串 2 档宽 = 18 次全表对账**，即同时判别 `−1`（S1/S3）与归属（S1/S3）两类缺陷；★ v1.1 订正：S7 的掩码判别**不在 A3 里**（A3 只管码点守恒），移到 T38-14h/i 的记录型测量器。详设可逐字抄本表，但**期望值必须重跑脚本确认**（本表即脚本输出，不得手改）。

### 1.5 问五 = 复用 vs 重写的最终选择（D38-1）

**定案 = ⓐ 可见性下移**（`TextLine` / `TextLayout` / `BuildTextLayout` 由 `TextWidget.h` 的 `private`（区起 `:99`）下移到 `protected`，`TextBox` 自带缓存与键）。

ⓐ 的三条论据 + ⓑ 的一条**结构性障碍**（不是审美之争，是依赖方向）：
1. ⓑ（抽 `src/Widget/TextWrapEngine.h` 内部件）**装不下**：`TextWidget.h:150` 有数据成员 `mutable TextLayout m_wrapLayout;` ⇒ 公共头**必须见到该类型的完整定义** ⇒ 内部头要么被公共头 `#include`（`include/` 引用 `src/` = 依赖方向倒置、且安装头文件树缺件），要么把类型抬进 `include/` ⇒ **公共头 100 → 101** 且 `TextLine` 变成**公开 API**（比 protected 更宽的面）。ⓐ 的 0 新头 + protected 面 +3 是**更小**的 API 变化。
2. **不算投机抽象**：`BuildTextLayout` 已在库内运行两个相位（Phase 29/36，`Label` 侧 T29-1..6 / T29-WRAP-1..3 覆盖），本 Phase 是**第二个真实消费者当场出现** ⇒ 抽象由需求触发，不是为将来预留（项目纪律「抽象在第二个真实消费者出现时才加」的正面案例）。
3. ⓒ 重写否：两份禁则字面集必然漂移 + 第二份从诞生起零覆盖（E1）。★ v1.1 计数订正（P2-3）：字面集 = `IsLineStartForbidden`（`:137-156`）**14** 个码点 + `IsLineEndForbidden`（`:157-170`）**6** 个 = **20** 个（v1.0 写「27 + 6」不实）；另有 `IsCjkBody`（`:173-179`，4 个区间）与 `IsSoftBreakBefore`（`:185-187`，空格/连字符/CJK 主体三类）。

**显示串参数穿透的落点（★ v1.1 由「两处」改「三处语义落点 / 五处 `m_text`」，P0-1）**：

| 语义落点 | 行号 | 现状 | 改后 | 漏掉的后果 |
|---|---|---|---|---|
| ① **解码** | `:210` | `DecodeUtf8(m_text)` | `DecodeUtf8(text)` | 索引按真实串 ⇒ 掩码框逐行区间全错 |
| ② **补测**（`emitLine` 的条件重测分支） | `:228` / `:229` / `:230` | `CodepointIndexToByteOffset(m_text, lineStart)` / `(m_text, end)` / `measurer.MeasureText(font, m_text.substr(b0, b1-b0))` | 三处 `m_text` 全换 `text` | **索引按显示串、宽度按真实串**的混合口径 ⇒ 行宽与断点不符 |
| ③ ★ **断行适配（决定「断在哪」的那次调用）** | **`:269`** | `measurer.FitText(font, m_text, scan, limit)` | `FitText(font, text, scan, limit)` | **v1.0 漏项**：`FitText` 的默认体（`TextMeasurer.h:68-117`）在传入串上做 `CodepointIndexToByteOffset` + `MeasureText(切片)` ⇒ 宽度口径**完全由传入串决定**；漏改则 `fitCp` 按真实串宽度算（`•` 3 字节 vs ASCII 1 字节）而 `lineStart` 按显示串推进 ⇒ 掩码框折行位置偏移，**正是文档自己警告的混合口径，只是方向相反** |

⇒ 新签名 `BuildTextLayout(TextMeasurer&, const std::string& text, float maxWidth) const`；`TextWidget` 唯一调用点 `:333` 传 `m_text`（⇒ `Label` 行为逐位不变）。★ **机检定位升级（P0-1 的第三问）**：C38-4 由「验收手段」升为**落点表完备性检查**——改后 `BuildTextLayout` 函数体（`:202-309`）内 `grep m_text` 命中数 == **0**（本日实测改前为 **4 行 5 处** = `:210/228/229/230/269`，与审核稿逐字一致）；★ 注意 `:396-398`（`DrawTextContent` 的发射）与 `:333`（调用点）的 `m_text` **属 `Label` 路径、必须保留**，机检范围严格限定在 `BuildTextLayout` 体内。

★ 为什么复用即闭合 K11：`SyncTextInputCaret` `:714-719` → `GetCaretClientGeometry` `:702-713` → `CalculateCaretPosition` `:676-700` 全链只读同一张行表 ⇒ 候选窗跟随**零新增接线**。

### 1.6 问六 = `L` 口径定义（K13 两条接缝，逐处声明）

| 接缝 | 位置 | 无头取值 | 真窗口取值 | 本 Phase 的用处 |
|---|---|---|---|---|
| **`L_geom`** = `GetLineHeight()` | `TextBox.cpp:486-493`（**硬编码 16.0f 兜底**，`:492`；**不读** `ResolveMeasurer()`） | 恒 `16.0f` | `window->GetTextMeasurer().LineHeight(font)` | 绘制 `lineY`（`:1382`）· 垂直居中内容高（`:515`）· `GetMaxScrollOffset`（`:529`）· `EnsureCaretVisible`（`:548`）· 命中定行（`:775`）· 光标 y（`:696`）· 引擎 `totalHeight`（`TextWidget.cpp:308`）**不参与** TextBox 几何 |
| **`L_pref`** = `measurer->LineHeight()` | `TextBox.cpp:222`（`MultilinePreferred`）与 `TextWidget.cpp:206`（引擎 `totalHeight` 的行高来源） | `ResolveMeasurer()`（`FakeTextMeasurer` = 16.0f；`FracMeasurer` = 19.2f，`TextBoxTests.cpp:78/:96`） | 同 `L_geom` | 仅 `MultilinePreferred` 的内容高 |

**声明（写死，防临场发挥）**：**A2③ / A3 / A4 / A6 的所有 `L` = `L_geom`**（几何与命中只认它）；**A7① 的 `L` = `L_pref`**（它就是 `MultilinePreferred` 现用的 `:222`，改用别的来源 = 动既有行为 ⇒ 破 A1）。
⇒ 两者的**一致性论证**：真窗口下同一 `window->GetTextMeasurer()` ⇒ 恒等；无头 `FakeTextMeasurer` 下 16 == 16 ⇒ 恒等；**唯一分歧面** = `FractionalLineHeightTextBox` 型夹具（19.2 vs 硬编码 16）⇒ ★ **本 Phase 的折行用例一律不用该夹具**（它专为 `ceil` 方向而设，与折行正交），且「两条行高来源统一」**属 §3 非目标七**，要拉回先改需求稿再评。登记于 §7 R38-4。

### 1.7 问七 = 勿重开 ✓（七项逐项确认）

`Home`/`End` 语义（D38-H = ⓐ，另立占号）· 禁则字面集扩充（O3，复用即继承）· 每行独立水平对齐（O1）· 富文本 / 逐行字体 / 可变行高（`GetLineHeight` 契约不动）· `GetTextAreaHeight` 与条摆位（`LayoutScrollBar` 零改动）· `TextBox.h:79` 契约文本（只把行数口径换成视觉行）· K13 行高来源统一（§1.6 只定口径）——**七项全部维持非目标，本稿未夹带任何一项**。★ 特别声明：本稿**不**新增公共观测缝（`GetLineCount()` 之类），行表断言全部经 `TestableTextBox` 的 `using` 暴露（先例 ★ v1.1 锚点订正 `TextBoxTests.cpp:52`（`using TextBox::GetTextTopInset;`）/ `:60-63`（`RecalculateLines` / `LineRange` / `CaretIndexFromPosition` / `GetWordBounds`））。
★ **v1.1 的两项非重开声明（防被读成夹带）**：① **D38-I（clip 宽）不是「重开条摆位」**——`LayoutScrollBar` 与 `GetTextAreaHeight()` 一行不改，改的只是 `OnPaint :1369` 的 `PushClip` 宽度表达式，且**仅折行态**；② **D38-D 去掉 `折行宽 > 0`** 不是「新增最小宽约束」——恰恰相反，是**不加**约束、让窄带退化为「一字一行」（R38-9），两者都在需求 §3 的范围内（需求 v1.2 已把 clip 宽补为范围项 7 / D38-10）。
★ **v1.2 的两项补充确认**：③ 需求 v1.2 在非目标表新加的两行——**`GetTextAreaWidth()` 的取值不改**（D38-I 只改 `:1369` 那个 clip 矩形**用哪个宽**，该函数的表达式与返回值逐位不变 ⇒ `LayoutScrollBar` / `GetTextAreaHeight` / `SyncScrollBar` 全部零改动，与 §1.2 的独立性证明不冲突）· **⑤ 悬挂链的上界不改**（`TextWidget.cpp:295` 取 `segEnd` 属 `Label` 侧既有语义，收紧它 = 改引擎禁则 = 破 A1 红线②）⇒ 两行**均维持非目标**，本稿未夹带。④ 需求 v1.2 追加的**闸门 8**（D38-10 的四问）由本稿 **D38-I 论据 ①③⑥ + T38-17 + C38-3/C38-8** 回答：四问逐条对应 = 「为什么不会让不该出现的文本露头」→ **D38-I 论据③ 的 v1.2 订正版 + §1.4 S4 实测**（除 ⑤ 与窄带推进保证 `:282` 外，引擎产出的每行宽 ≤ 折行宽 ⇒ 放宽 clip 只揭示悬挂尾）· 「条可见态观感代价须量化」→ **R38-3 / R38-9**（★ 且明确**不得**写「条会盖住所以无代价」——K15③ 已证轨道 alpha 0）· 「A1 的条件化写法」→ **PD38-6⑤ 的代码草案**（`if (IsWrapActive())` 分支换源、else 逐位同）· 「判据形态 + 负对照」→ **T38-17**（`PushClipCommand.rect.width` + 悬挂行命令包含关系 + 回退 clip 源必红）。⇒ 闸门八项**全部收口**，§1 的标题「七项」是 v1.0/v1.1 时点数，**不回改**（第 8 项在需求侧登记为「已被初设回答，追加只为留痕」）。

---

## §2 决策定案 D38-A..I（对应需求 D38-1..10）

| # | 定案 | 论证 / 与被否项的关系 |
|---|---|---|
| **D38-A** | 复用形态 = **ⓐ 可见性下移** + `BuildTextLayout` 接 `text` 参数 + `TextLine` 追加 `ownedEnd`（**末尾**） | §1.5；ⓑ 的障碍 = 公共头必须见 `TextLayout` 完整定义（`TextWidget.h:150`）；ⓒ 否（E1）。★ 显示串穿透 = **三处语义落点 / 五处 `m_text`**（`:210` 解码 · `:228-230` 补测 · **`:269` `FitText`**，P0-1） |
| **D38-B** | 行表 = **`std::vector<TextLine> m_lines`**（D38-2 ⓑ + `ownedEnd`）；`LineRange` 归属口径、`LineDrawRange` 裁剪口径；**非折行态由本类自建、不经引擎** | 自建保证 A1 结构性成立（`drawEnd == ownedEnd ==` 今日 `−1` 口径的值 ⇒ 逐位同）；持引擎结果保证「行表只有一张」（D38-6 ⓑ 否：会让行表重新变两份真相）。★ `TextLine` 的 `width` 字段一并到手，为将来 `BlockWidth` / 逐行对齐留位（**不是**为它改可见性——本 Phase 不用它） |
| **D38-C** | 折行宽 = **`GetTextAreaWidth()` 单函数 + 常驻条槽**；谓词分层：`WrapRequested()`（`noexcept`，进宽度函数）/ `IsWrapActive()`（含 `ResolveMeasurer()`，进重建与钉 0） | §1.2；ⓑ（文本钻到条下）否 = 观感退化（★ v1.2 注：ⓑ 是「**全部行**按未扣条宽折行、再靠 clip 让位」⇒ 每行都可能被条压住；D38-I 是「折行宽**仍**扣条槽、只把 **clip** 放宽到控件内边界」⇒ 只有 ⑤ 产生的悬挂尾会进槽，其余行恒 ≤ 折行宽 ⇒ 两者不是同一形态，D38-I 不构成对 ⓑ 否决的翻案）；ⓒ（两趟求自洽）否 = 把两趟全量 `FitText` 塞进每帧路径（`:1287-1290` 每帧 `SyncScrollBar`），且自指解除后不需要迭代。★ A4 的第二态 = **`H=24`**（P0-2；`H=32` 下负对照假绿） |
| **D38-D** | 激活判据 = **本类判据** `WrapRequested() && ResolveMeasurer() != nullptr`（★ v1.1 **删去 `GetTextAreaWidth() > 0` 合取项**，P1-3）；★ `!m_singleLine` 经 `WrapRequested()` 进判据 | 硬理由（`!m_singleLine`）= K14 三道闸：`:542-544` 纵向偏移钉死 · `:615` 的 `need = !m_singleLine && GetMaxScrollOffset() > 0` ⇒ **条永不可见**（★ v1.1 措辞订正：条本身在构造期**恒创建**，`:59` `CreateVerticalScrollBar()`；v1.0 写「条不创建」不实）· `:646` 滚轮吞掉 ⇒ 单行框折行 = 第 2 行起**内容不可达**，比不折行更糟。ⓐ（复用 `IsWrapLayoutActive`）否 = `:123-129` 第三条 `GetWindow() != nullptr` 把无头全挡（K6）。★ **删宽度合取项的论证（P1-3 的正解）**：保留它则当 `0 < W ≤ 2p+14` 时本类判据**假** ⇒ preferred 落到 `:199` 基类调用 ⇒ 基类 `IsWrapLayoutActive()`（只要求 `m_wordWrap && GetWidth() > 0 && GetWindow()`）**真** ⇒ 返回 `{GetWidth(), totalHeight}` 经 `:201` `+2p` ⇒ **宽度棘轮在该窄带照旧**，而 `AutoSize` 恰恰是从小宽往大宽爬、**必经该带**；且同一窄带内「preferred 按折行算高、行表按 `\n`-only 建表」= N0 的口径分裂再现。删掉后本类判据与基类的 wrap 意图**在所有宽度上一致** ⇒ `:199` 在 `wrapRequested` 态永不可达 ⇒ 棘轮**全宽域不可达**（不再只是「`GetTextAreaWidth() > 0` 区间内」）。代价 = 窄带内 `limit ≤ 0` 的退化形态，见 R38-9（引擎侧结构性有界：`:270` `hardEnd` 可 == `scan` ⇒ `:282` 硬断一字 ⇒ `:302` 绝不零推进；脚本实算 `limit = 0 / −4 / 4` 三档均为**逐码点成行、`n ≤ count`、无死循环**） |
| **D38-E** | 失效键 = **10 维 / 10 字段自比键**（★ v1.1 由 11 维删 `singleLine`，**保留组合三维**）；`m_needsLineRecalc` **退役**；`RecalculateLines()` **名字与调用形态保留**，体内改「键命中 ⇒ 直接返回」 | §1.3；ⓐ（补置脏）做不到 = K7 字体路径非虚（`:1287-1289` 在案 R1 注释），`ApplyTheme`（`:75-99`）/`SetStyle`（`:99-127`）只 `Invalidate()` ⇒ setter 置脏在字体维**结构性不可达**。★ 组合三维的保留理由 = `CancelComposition` 快照支的**越序点**（`RestoreSnapshot` 内 `:1087` 的 `EnsureCaretVisible()` 会在 `:955-958` 清标志之前存键），非 v1.0 所称的「纯游标推进不 bump」（该路径不存在，P2-6） |
| **D38-F** | preferred：闸 = 「含 `\n` **或** `IsWrapActive()`」⇒ 恒走 `MultilinePreferred`；行数 = `m_lines.size()`（删 `:216-220` 的第二次 `\n` 计数）；const 读行表 = `const_cast` 自查（`:510/:679` 先例） | D38-7 + 需求 P0-1 硬约束：**折行分支禁止经 `TextWidget::GetPreferredSize` 的 wrap 支**（`:433-437` 返回 `GetWidth()` 再被 `:201` `+2p` = 宽度棘轮的机制）；`MultilinePreferred` 三条在案契约（`ceil` / 无测量器退当前尺寸 / 宽取当前宽）全部保持。★ v1.1：闸用 `IsWrapActive()`（已不含宽度合取项）⇒ **与 D38-D 同源**，preferred 与绘制在所有宽度上判据一致 |
| **D38-G** | `m_scrollOffsetX` 折行期恒 0，**单点钉**（`:569` 写点外包一层 `if (!IsWrapActive())`） | K10：读点 4 处（`:694`/`:781`/`:822`/`:1366`）都是 `− m_scrollOffsetX` 形式 ⇒ 钉 0 后逐位退化为现状，四个读点零改动（同 `:542-544` 的 `m_singleLine` 钉 Y 先例） |
| **D38-H** | `Home`/`End` **不动**（= ⓐ）+ N0 处置 = **ⓑ 单批闭环** | E5：`Home`/`End` 现状全文首尾（`:1223-1241`，**一行行表都不读**）⇒ 接进行表是独立的公共行为变更，与折行机制正交 ⇒ `roadmap-deferred` 新立占号（PD38-9）；ⓐ 的止血是给一个即将删掉的分支加守卫，且 `GetPreferredSize` 属红线面（D38-9 倾向原样采纳）。★ **已知现状必须写清**：折行后 `Home` 落全文首 ⇒ 与 Up/Down 的视觉行语义**不同源**，这是**现状**不是漏改（C38-7） |
| **D38-I** | ★ **v1.1 新增（P1-2 的正解，需求 v1.2 补为 D38-10）**：折行态 `OnPaint` 的 `PushClip`（`:1369`）宽 = **控件内边界 `GetWidth() − 2·padding`**，**不是** `GetTextAreaWidth()`；非折行态**逐位保持** `GetTextAreaWidth()`（A1） | ① **修的是什么**：⑤（`:295`）上提闭标点 ⇒ 行宽可 > 折行宽，而 `scrollOffsetX ≡ 0`（D38-G）⇒ 若 clip 也取折行宽，悬挂字符**整字永久不可见**（S4 实测被裁 8px = 一个完整码点，而光标可停、选区可选中）。首个真实消费者 = harness `ComposerBox`（**中文聊天输入框**，行尾悬挂闭标点高频）⇒ 大概率首发即命中，v1.0 只把它写进 A6① 豁免 = 严重度低估。② **为什么无环**：clip 是**发射期**参数，**不是任何布局输入**（不进 `wrapWidth`、不进 `n`、不进 `GetMaxScrollOffset`）⇒ §1.2 的独立性证明不受影响（`wrapWidth` 的输入集仍是 `{GetWidth, padding, barThickness, wrapRequested}`）。③ ★★ **条可见态的真实代价（v1.1 原③写「条后画且 `ScrollBar.cpp:217` 是**不透明** `DrawRect` ⇒ 溢出段被条覆盖」= **错**，本次订正并连带订正 R38-5 / T38-17 / T38-9 / S4 四处同族表述）**：条确为**子控件**且**后画**（`Widget.cpp:506` `PushClip(self)` → `:508` `OnPaint` → `:515-518` 子控件），**但默认主题下轨道 alpha = 0**——`ScrollBarStyle.trackColor = Color::FromRGBA8(240,240,245,**0**)`（`DefaultTheme.cpp:110`，v1.6「轨道默认透明」2026-10-01 用户拍板），且该处注释**已核实后端语义**「`GDIBackend::DrawRect` 对 `a < 1` 走 `BlendAlphaSolid` ⇒ alpha 0 **不产生像素**」（★ v1.2 亲核到码：`GDIBackend.cpp:741-745` 即 `if (color.a < 1.0f){ BlendAlphaSolid(...); return; }`，而 `BlendAlphaSolid`（`:73` 起）写**预乘** BGRA + `AlphaBlend(AC_SRC_ALPHA)` ⇒ `a = 0` ⇒ 源 RGB 与 A 全 0 ⇒ 目标像素不变；衬底模式分支 `:718-739` 的 `WriteRoundedRectPremultiplied` 同预乘语义）⇒ 轨道那次 `DrawRect`（`ScrollBar.cpp:217`）**盖不住任何东西**，只有**滑块**（`thumbColor` alpha 255，`:111`）会盖住与它重叠的那一段。⇒ **代价的正确表述** = 条隐藏态（`AutoSize` 后的常态）悬挂字符**完整可见**；条可见态悬挂字符**仍然可见**，只是**可能与滑块重叠被压住一截**（观感账，非可见性账）。⇒ **与不改的比较**：不改 = 悬挂字符在**两态下恒不可见**（clip 宽 == 折行宽，K15①）；改 = 常态完整可见 + 条可见时可能被滑块压一截 ⇒ **取改**。该观感账入 R38-3 / R38-9，判据侧 T38-17 的条可见半段**不得**断言「被条覆盖」，改为断言 clip 宽不变 + 悬挂行 `DrawTextCommand` 仍发射（覆盖与否属像素级、超无头判据面）。④ **为什么不放开 `scrollOffsetX`**：放开 = 折行态引入水平滚动 ⇒ 与「折行就是为了消灭水平滚动」的立项动机直接冲突，且读点 4 处 + `ClampScrollOffsetX` 语义全要改（范围膨胀）。⑤ **附带收益**：D38-D 删掉宽度合取项后，窄带内 `GetTextAreaWidth()` 可为 **0 或负**（实测 W=10/p=0 ⇒ −4）⇒ 若 clip 仍取它就会出现**负宽 clip**；取控件内边界则该退化面消失（W=10 ⇒ clip 10）。⑥ **可判**：`PushClipCommand` 是既有命令类型，`RecordingBackend` 可直接断言 `rect.width`（T38-17） |

---

## §3 方案 PD38-1..9（逐文件落点，含代码草案）

**PD38-1 · `ECDI/include/ECDI/Widget/TextWidget.h`**（公共头 100 → 100；public API +0；protected 面见 §6）

```cpp
	// ★ Phase 38：以下三件由 private 下移到 protected（第二个真实消费者 = TextBox，§1.5）
	struct TextLine{
		std::size_t startCp = 0;
		std::size_t cpCount = 0;    ///< 已做尾部空白裁剪（**口径不变**——A1 红线）
		float width = 0.0f;
		std::size_t ownedEnd = 0;   ///< ★ Phase 38 新增（**声明在末尾**——C7560 序）：归属终点（含软断点分隔空格、不含硬断 \n）
		                            ///<   TextWidget 自身**不读**它（`Label` 输出逐位不变）；编辑器可达性/选区/命中用它
	};
	struct TextLayout{ std::vector<TextLine> lines; float totalHeight = 0.0f; };
	[[nodiscard]] TextLayout BuildTextLayout(TextMeasurer& measurer, const std::string& text, float maxWidth) const;  // ★ +text
	[[nodiscard]] std::size_t TextRevision() const noexcept{ return m_textRevision; }   // ★ TextBox 键维 1（m_textRevision 保持 private）
```

`IsWrapLayoutActive()` / `GetTextLayout()` / `BlockWidth()` / 全部缓存字段 **保持 private**（TextBox 不需要，给了就是扩面）。

**PD38-2 · `ECDI/src/Widget/TextWidget.cpp`**（★ v1.1 由「四处」改**六处**——含 P0-1 补的 `:269`；`Label` 输出零变化）

```cpp
// ① :210 落点一（解码）
const std::vector<char32_t> cps = DecodeUtf8(text);
// ② :212 空文本行（ownedEnd = 0 == count ✓）
layout.lines.emplace_back(TextLine{ 0, 0, 0.0f, 0 });
// ③ emitLine（:220-233）：归属 = 裁剪前的 lineEnd；★ 补测三处 m_text → text（落点二）
//    :228 CodepointIndexToByteOffset(text, lineStart) / :229 (text, end) / :230 text.substr(b0, b1 - b0)
layout.lines.emplace_back(TextLine{ lineStart, end - lineStart, width, lineEnd });   // :232
// ④ ★ :269 落点三（P0-1——决定「断在哪」的那次 FitText，漏改 = 索引按显示串、宽度按真实串）
const TextFit fit = measurer.FitText(font, text, scan, limit);
// ⑤ :258 空段行
layout.lines.emplace_back(TextLine{ segStart, 0, 0.0f, segStart });
// ⑥ :333 唯一调用点（Label 侧传 m_text ⇒ 行为逐位不变）
m_wrapLayout = BuildTextLayout(measurer, m_text, static_cast<float>(width));
```
★ **不得触碰**：`:396-398`（`DrawTextContent` 的发射按 `startCp`/`cpCount` 取字节）——那是 `Label` 的裁剪口径发射点，改它就破 A1；`:308` 的 `totalHeight` 与 `:323-328` 的六元组键同样零改动（`TextBox` 不用基类缓存）。

**PD38-3 · `ECDI/include/ECDI/Widget/TextBox.h`**

```cpp
protected:
	/// @brief 折行是否被**请求**（纯配置，noexcept——GetTextAreaWidth 用它决定是否常驻留条槽）
	[[nodiscard]] bool WrapRequested() const noexcept{ return IsWordWrap() && !m_singleLine; }
	/// @brief 折行是否**生效**（= 请求 ∧ 有测量能力；本类判据，★ 不含 GetWindow()、★ v1.1 不含「折行宽 > 0」——D38-D）
	[[nodiscard]] bool IsWrapActive() const;
	/// @brief 行绘制区间 [start, start+cpCount)（裁剪口径——软断点行尾空格**不发射**）
	std::pair<size_t, size_t> LineDrawRange(size_t lineIndex) const;
private:
	std::vector<TextLine> m_lines;     ///< 视觉行表（★ 取代 m_lineStarts；行数 = size()，含尾部空行）
	// ── 10 维自比键 / 10 个键字段（§1.3；★ 无 singleLine 维——被 wrapRequested 严格覆盖）──
	bool        m_linesValid = false;
	std::size_t m_linesRevision = 0;   // 维 1
	Font        m_linesFont{};         // 维 2/3/4（size / family / weight）
	int         m_linesDpi = 0;        // 维 5（无窗口 = 0，不短路）
	float       m_linesWrapWidth = 0.0f;   // 维 6
	bool        m_linesHasMeasurer = false;   // 维 7
	bool        m_linesWrapRequested = false; // 维 8（含 !m_singleLine 分量）
	EchoMode    m_linesEcho = EchoMode::Normal;   // 维 9
	bool        m_linesComposing = false;         // 维 10（三项——保留理由见 §1.3 维 10 的越序点）
	std::size_t m_linesCompStart = 0, m_linesCompLen = 0;
```
删除 `m_lineStarts`（`:349`）与 `m_needsLineRecalc`（`:350`）；订正 `:77-78`（口径同源 ⇒ 真相源计数）、`:257`（Center 路径的自查重算：`m_needsLineRecalc` 已退役 ⇒ 改述为「键守卫」）、`:261-262`（`RecalculateLines` = 键守卫重建，`\n` 扫或引擎二选一）、`:264-265`（`LineRange` 归属口径全文，§1.1 表）、`:329`（二分对象改 `m_lines[].startCp`）、`:349`（**行数 = `size()`**，含尾部空行——§1.1 末）、`:352`（`m_scrollOffsetX` 折行期恒 0）。

**PD38-4 · `TextBox.cpp` — `RecalculateLines()` 重写为键守卫分发**

```cpp
void TextBox::RecalculateLines(){
	const bool wrap = IsWrapActive();                       // ★ 不判宽度（D38-D）
	const float wrapWidth = wrap ? GetTextAreaWidth() : 0.0f;
	TextMeasurer* measurer = wrap ? ResolveMeasurer() : nullptr;
	if (m_linesValid && KeyMatches(wrap, wrapWidth, measurer)) return;   // ★ 命中 ⇒ 零 FitText/零 MeasureText（A8 反断言）
	if (wrap){
		m_lines = BuildTextLayout(*measurer, BuildDisplayText(), wrapWidth).lines;   // ★ 显示串（K9/S7）
	}else{
		// 非折行支 = 原 \n 扫描逐位保留（A1）：{start, cpCount, width 0, ownedEnd}
		m_lines.clear(); m_lines.push_back({0, 0, 0.0f, 0}); ...   // 遇 \n 于 i：本行 count=i−start、ownedEnd=i，新行 start=i+1
	}
	if (m_lines.empty()) m_lines.push_back({0, 0, 0.0f, 0});   // 空串 ⇒ 1 个空行（与引擎 :212 同口径）
	SaveKey(wrap, wrapWidth, measurer); m_linesValid = true;
}
```
★ 空表兜底是必需的：原实现恒推 0（`:448`）⇒ `m_lineStarts` 永不为空；新分发若漏掉该行 ⇒ `LineIndexFromCodepoint` 的 `:461` 判空返回 0、但 `:778` / `:1251` / `:1370` 会拿 `size()-1` 下溢 ⇒ 用例 T38-1 的第三组基准必须含**空文本**。
★ **成本账（v1.1 补，D38-D 删宽度合取项后必须交代）**：`BuildDisplayText()` 在 `EchoMode::Normal` 下是 `return m_text;`（`:232-233`）= **一次串拷贝**，仅发生在**键未命中的重建**（每次编辑 / 改尺寸 / 换字体一次），键命中路径**零拷贝零测量**；`OnPaint` 已有的 `textSource` 零拷贝优化（`:1349-1353`）**不受影响**（两者不在同一表达式里，`RecalculateLines` 不吃 `textSource`）。窄带（`limit ≤ 0`）下 `n ≤ count` ⇒ 一次重排的 `FitText` 次数仍**线性于码点数**，不产生超线性放大（R38-9）。

**PD38-5 · `TextBox.cpp` — 两个区间入口（`−1` 退场）**

```cpp
std::pair<size_t, size_t> TextBox::LineRange(size_t lineIndex) const{          // 归属口径
	const TextLine& l = m_lines[lineIndex];
	return { l.startCp, l.ownedEnd };                    // ★ 不再由下一行起点反推、不再 −1
}
std::pair<size_t, size_t> TextBox::LineDrawRange(size_t lineIndex) const{      // 裁剪口径
	const TextLine& l = m_lines[lineIndex];
	return { l.startCp, l.startCp + l.cpCount };
}
```
`LineIndexFromCodepoint`（`:460-474`，= 需求 §2.1 **C1**）二分改读 `m_lines[mid].startCp`，**算法零改动**（`startCp` 仍严格递增——软行 `ownedEnd == 下一行 start`、硬行 `+1`，见 §1.1 表）；★ v1.1 补：`:465` 的 `hi = size()-1` 与 `:461` 的判空**必须一并改名**（v1.0 的站表漏列 C1，P1-4）。

**PD38-6 · `TextBox.cpp` — 宽度、preferred、钉 X、clip、setter 触发（★ v1.1 由四处改六处）**

```cpp
// ① GetTextAreaWidth（:662-674）：谓词加「折行请求 ⇒ 常驻留槽」；★ 注释里的 :669 论证换成 §1.2 的新论证
if (m_vBar != nullptr && (m_vBar->IsVisible() || WrapRequested()))
	width -= static_cast<float>(m_vBar->GetThickness()) + ScrollBarInset(m_style.padding.value);
// ② GetPreferredSize（:195-202）——★ 闸与 D38-D 同源，全宽域不进 :199
if (m_text.find('\n') != std::string::npos || IsWrapActive()) return MultilinePreferred();
// ③ MultilinePreferred（:216-220 删自数 \n）
const_cast<TextBox*>(this)->RecalculateLines();          // 键守卫：命中即零测量
const size_t lines = m_lines.size();                      // ★ 视觉行数（唯一真相源）
// ④ EnsureCaretVisible（:558-571）横向段外包
if (!IsWrapActive()){ if (Window* window = GetWindow()){ ... m_scrollOffsetX = ClampScrollOffsetX(...); } }
// ⑤ ★ v1.1 新增（D38-I）：OnPaint 的 clip 宽（:1369）——仅折行态换源，非折行态逐位同（A1）
const float clipW = IsWrapActive()
	? static_cast<float>(GetWidth()) - m_style.padding.value * 2.0f   // 控件内边界 ⇒ 悬挂字符可见
	: GetTextAreaWidth();                                             // 改前原式，逐位不变
ctx.PushClip(Rect{ textX, areaTop, clipW, GetTextAreaHeight() });
// ⑥ ★ v1.1 新增（P1-1）：SetSingleLine（:162-167）补重绘请求——它现在改激活判据
void TextBox::SetSingleLine(bool singleLine){
	if (m_singleLine == singleLine) return;
	m_singleLine = singleLine;
	Invalidate();   // ★ 键只解决「算得对」，不解决「何时算」（C38-9）；条的重同步免费——OnPaint :1290 每帧 SyncScrollBar()
}
```
★ ②③ 合起来 = N0 的关闭（A7）：`wrap` 态宽度恒返回**当前宽**（`:228` 契约③），基类 wrap 支的 `{GetWidth(), totalHeight}` 再加 `2p` 那条通路**在 `wrapRequested` 全宽域不可达**（★ v1.1 订正措辞，P1-3：v1.0 写「从代码结构上不可达」**过强**——闸若含 `GetTextAreaWidth() > 0`，则 `0 < W ≤ 2p+14` 窄带内仍可达；删掉该合取项后才成立全宽域不可达，论证见 D38-D）。
★ ⑥ 的边界（P1-1 复验）：`SetWordWrap`（`TextWidget.cpp:100-114`）与 `SetEchoMode`（`TextBox.cpp:148-153`）**已有** `Invalidate()`；**唯 `SetSingleLine` 缺**（体内只改标志位，注释还写着「无需重绘」——那是它只影响 Enter 语义时代的判断，本 Phase 把它变成激活判据的一个分量后该注释即作废，须一并改写）。无头下 `Invalidate()` 是 no-op（`Widget.h:204`：无窗口 ⇒ 无重绘请求）⇒ 该断言**只能真窗口验**（T38-16，A5 白名单追加第 ⑤ 项）。
★ ⑤ 的边界（★ **v1.2 新增——文件内会出现两条互相矛盾的在案注释，必须一并订正，否则实现者无所适从**）：`GetTextAreaWidth()` 体内 `:667-668` 的 v1.5 在案注释写「★ 让位量 = 条厚 + 条的内缩——条不再贴边，内缩那一段也必须让出来，**否则文本会钻到条底下（末列/末行字符被条压住的原始症状会以新的形式回来）**」，而 D38-I 恰恰是**故意**让悬挂尾画进那一段 ⇒ 两条注释在同一函数/同一绘制路径上**方向相反**。处置：⑤ 落地时**必须同时改写 `:667-668`**，把它限定为「让位量约束的是**折行宽与光标 clamp 宽**（布局量），**不是 clip**；悬挂尾进槽是 D38-I 的有意行为」，并顺手订正其「被条压住」的措辞（K15③：轨道 alpha 0 ⇒ 只有**滑块**会压住）。⇒ C38-3 的机检因此从「三个宽度各自单一来源」扩为「三个宽度单一来源 **+ `:667-668` 与 `:669` 两条注释均已改写**」（`:669` 的推翻见 §1.2，两条都不得静默改码）。

**PD38-7 · `TextBox.cpp` — 消费点全表（★ v1.1 按 P1-4 重排：编号与需求 §2.1 的 C1–C11 对齐、只列**读取点**、惰性门单列一栏）**

`m_lineStarts` 实测 = **13 处读取命中 / 9 个站点**（★ 审核稿称「11 处」，本侧逐行复核为 13：漏计 `:479`，且把 `:480-481` 记作一处）+ 3 处写入（`RecalculateLines` 自己：`:447/448/455`）+ 6 处注释（`:463/504/536/609/649` 与 `TextBox.h:96/257/329`）。

| 需求编号 | 站点（行号） | 读取命中 | 现读法 | 改后 | 需改？ | 惰性门 |
|---|---|---|---|---|---|---|
| **C1** | `LineIndexFromCodepoint` `:460-474` | `:461`（判空）· `:465`（`hi = size()-1`）· `:468`（比较） | 二分「最后一个 `start ≤ cp`」 | 三处改读 `m_lines`（`empty()` / `size()-1` / `[mid].startCp`），**算法零改动** | ★ **改名（v1.0 站表漏列）** | — |
| **C2** | `LineRange` `:476-484` | `:479`（start）· `:480-481`（end = 下一行起点 `−1`） | 行尾反推 | **必改**：`{ startCp, ownedEnd }`（PD38-5），`−1` 退场 | ★ **必改**（E2） | — |
| **C3** | `GetTextTopInset` Center `:509-518` | `:515`（`size() × lineH`） | 逻辑行数 | `m_lines.size()`（口径随表变） | 改名（★ `Top` 快路径 `:504-507` **零触碰** = A1 的结构性保证，D37-C） | `:510` 去门 |
| **C4** | `GetMaxScrollOffset` `:527-532` | `:529`（`size() × GetLineHeight()`） | 同上 | 同上 | 改名 | `:537` 去门 |
| **C5** | `EnsureCaretVisible` `:534-574` | `:565`（`m_lineStarts[lineIndex]` 定前缀测宽起点） | 行起点 | `m_lines[lineIndex].startCp`；★ `:569` 横向写点外包 `if (!IsWrapActive())`（D38-G） | 改名 + **钉 X** | —（本函数由 `:537` 门守卫，见 C4） |
| **C6** | `SyncScrollBar` `:604-641` | **无直接读取**（经 `GetMaxScrollOffset` = C4） | — | 代码不改；★ 它是**每帧重算与 E3 环的载体**（`:615` 的 `need` 判据 + `:1290` 每帧调用）⇒ D38-C 的落点 | 否（口径随 C4 变） | `:611` 去门 |
| **C7** | `OnMouseWheel` `:643-658` | **无直接读取**（经 C4） | — | 不改（K14 的单行闸 `:646` 保持） | 否 | `:650` 去门 |
| **C8** | `CalculateCaretPosition` `:676-700` | `:684`（行起点定 x 前缀） | 同上 | `m_lines[lineIndex].startCp` | 改名（★ 与绘制同源 = A2③ 的另一半） | `:679` 去门 |
| **C9** | `CaretIndexFromPosition` `:767-783` → `CaretIndexFromLineX` `:785-814` | `:778`（clamp 到 `size()-1`）；`CaretIndexFromLineX` **不直接读表**（读 `LineRange` = C2） | Y 定行 + 行内 X 定码点 | `:778` 改 `m_lines.size()-1`；`CaretIndexFromLineX` **零改动**（自动吃归属口径 ⇒ 行尾空格可达 = A6①） | ★ **半改**（`:778` 改名；`:789-791` 的行尾 clamp 随 C2 变，代码不动） | `:770` 去门 |
| **C10** | `OnKeyDown` Up/Down `:1243-1261` | `:1251`（末行界 `curLine + 1 >= size()`） | 逻辑行界 | `m_lines.size()` | 改名（口径随表变） | `:1246` 去门 |
| **C11** | `OnPaint` 行循环 `:1355-1424` | `:1370`（`lineCount = size()`）· `:1378-1381`（`LineRange` 取区间与裁串） | 逐逻辑行绘制 | 改名 + **绘制取 `LineDrawRange`、选区（`:1388-1398`）/组合下划线（`:1412-1421`）求交仍取 `LineRange`**（一对区间拆成两个局部量）+ ★ **clip 宽换源（PD38-6⑤，D38-I）** | ★ **唯一有语义判断的站** | `:1355` 去门 |
| — | `Home`/`End` `:1223-1241` · `Left/Right` `:1202-1222` · `GetWordBounds` `:829` | **不读行表** | — | **零改动**（D38-H；跨软行走码点自然路径） | 否 | — |

★ **8 处惰性门单列**（`:510/537/611/650/679/770/1246/1355`）：全部改为**无条件调用** `RecalculateLines()`（体内键判定 ⇒ 命中即返回，成本 = 一次键比对）；★ 它们**不是读取点**（v1.0 把两类混在一张表里 = P1-4 的口径问题），但**必须与读取点同表登记**，否则「去门」漏一处 = 该路径读到旧行表。

**PD38-8 · `TextWidget.h:61-70` 注释扩写**（K12④）：`SetWordWrap` 的 `true` 语义现在覆盖两类消费者——`Label`（宽度=控件宽、逐行发射）与 `TextBox`（折行宽 = `GetTextAreaWidth()`、编辑器归属口径、`scrollOffsetX` 钉 0、clip 取控件内边界、单行框不生效）；★ 并写明**回滚边界**：`TextBox` 上折行不依赖 `GetWindow()`，无窗口即可重排（D38-D）。

**PD38-9 · `roadmap-deferred.md`**：① #77 行「进展」追加「初设 v1.0 → v1.1（评审吸收）」；② **D38-H 的占号新立一行**（#82：`Home`/`End` 视觉行语义 + `Ctrl+Home/End`，重启条件 = 首个需要行级导航的消费者）。★ 外部动作不变：**harness 那一行 `SetWordWrap(true)` 在拿到含本 Phase 的前缀之前不得加**（加了 = 长高且宽度棘轮）——本稿只登记、不代对方仓执行。

---

## §4 测试 T38-1..17（★ v1.1：513 → 530，+17）

| # | 用例 | 验收 | 手法 |
|---|---|---|---|
| **T38-1** | 非折行态命令级基准逐位（3 组：含 `\n` 多行 / 单行 / **空文本**） | A1 | `RecordingBackend` + `get_if<DrawTextCommand>` 取 `pos.y` / `pos.x` / **`text`**（Phase 37 K7 手法），`FakeTextMeasurer` ⇒ 不依赖 DPI 与字体族。★★ **基准采集 = 实施前置**：源码基准 = `6a3807be19a30d8512e18ef2d68bf2d1b7842cbf`（本日 `git log -1` 核实），采集必须在**动 `TextBox` 之前**完成；**禁止先改再回头生成「改前」常量** |
| **T38-2** | `Label` 侧红线：`BuildTextLayout` 默认输出逐位（`startCp`/`cpCount`/`width`/`totalHeight` + 空格归属），`ownedEnd` 不参与 | A1 | `WidgetTests` 既有 wrap 锚（T29-1..6 / T29-WRAP-1..3）复跑 + 一条新断言：同输入两态（改前常量表 vs 改后表） |
| **T38-3** | S1 拉丁空格 四路同源（绘制区间 / 行首命中 / 光标 y / Up-Down 行号） | A2 | §1.4 表逐字段 |
| **T38-4** | S2 CJK 无空格 四路同源 | A2 | 同上 |
| **T38-5** | S3 `\n` + 软折行混排 四路同源（★ v1.1 串改 `"ab cd \nef gh"` 12 码点，两档宽 40 / 24） | A2 | 同上（含硬/软 `gap` 混存） |
| **T38-6** | 禁则与退化 **五**样本四路同源（★ v1.1 由三条扩为五条）：S4 悬挂微超 / **S5 行尾开括号回退（新串 `"ab（cd"`）** / **S5b ④b 守卫（`"（a"` w8）** / **S5c 纯 CJK 对照组（④ 不可达）** / S6 超长词硬断 | A2 | ★ 复用引擎的风险面正是这几条，必须各跑一遍；期望值取 `tools/phase38_wrap_probe.py` 输出（§1.4 表） |
| **T38-7** | 码点守恒遍历 **9 样本**全表 × 每串 2 档宽 = **18 次全表对账** | A3 | §1.1 机检式（`ownedEnd(i)` 与 `start(i+1)` 的 0/1 对账 + 三条边界等式）；★ 脚本的 `A3 gap check` 已逐串 OK，用例断言与脚本输出**逐字对齐** |
| **T38-8** | 折行宽 ⊥ 条状态：同控件两态（★ v1.1：**H=64 / H=24**）⇒ `GetTextAreaWidth()` 与逐行区间**逐项相等** | A4 | ★ **判据形态取 §1.2 表的四行**（含 `H=32` 为何假绿、以及「两态须各自为不动点」的前置纪律）；负对照 = 回退 PD38-6① 的谓词 ⇒ 本条必须红（实施期回退轮自证，不过则如实登记缺口）。★ 复算依据 = 脚本 `fixed_point()`：`OLD(64)=(40,2)` / `OLD(24)=(26,3)` ⇒ 异值 ⇒ 红 |
| **T38-9** | 空格可达性：S1 下 caret 可停 `cp5`、`Left/Right` 可穿过、点击行尾空格落该行（行首归属形态） | A6① | 无头（A2② 改形后的形态）；★ v1.1：悬挂行**不再整条豁免**——按 D38-I，悬挂字符**在两态下均可见且可命中**（★ v1.2 订正：原写「条隐藏态下」= 状态依赖，错；轨道 alpha 0 ⇒ 条不覆盖文本，见 D38-I 论据③；断言移到 T38-17），本条只对「行宽 > **控件内边界**」的极端串保留豁免（R38-9） |
| **T38-10** | 跨软行 `Shift` 拖选高亮连续无断口 + IME 组合下划线跨软行切段 | A6②③ | `RecordingBackend` 取 `DrawRectCommand` 序列，断言相邻行的 `x+w` 衔接（无 8px 断口） |
| **T38-11** | `Undo`/`Redo` 后行表与文本一致 + 折行期 `GetScrollOffsetX() ≡ 0` | A6④⑤ | 键入折行长文 → `Undo` → 行表 == 直接构造同文本的行表 |
| **T38-12** | preferred 与绘制一致 + **连续两次 `AutoSize()` 宽不变** | A7①② | `GetPreferredSize().height == ceil(n·L_pref + 2p)` 且 `n == OnPaint` 行数；`.width == GetWidth()` **恒等**（旧实现必红：`:436`+`:201`）；★ v1.1 追加**窄带一档**（`W = 2p+14` 与 `W = 2p+1`）⇒ 两次 `AutoSize` 宽仍不变（D38-D 删宽度合取项的判别面，P1-3） |
| **T38-13** | 非折行态 preferred 逐位同（`wrap=false` / 含 `\n` / 单行） | A7③ | 常量表，同 T38-1 手法 |
| **T38-14** | 键逐维正断言 **a–i**（★ v1.1 由 a–h 扩为 a–i）：a revision / b `font.size` / c `font.family` / d `font.weight` / e `wrapWidth` / f `wrapRequested` 的 **`SetWordWrap` 分量** / g `wrapRequested` 的 **`SetSingleLine` 分量** / h `echoMode` / i **组合三维**（`Password` + `UpdateComposition` + `CancelComposition` 快照支） | A8 | 每维「只变这一维 ⇒ 行数或区间必变」；★ g 判别的是**维 8 对 `singleLine` 的覆盖性**（该维已删，断言不删）；★ i **必须开掩码**（Normal 态恒真 = 无判别力，§1.3 维 10）；★ S7 的记录型测量器量具落在 h（C38-4） |
| **T38-15** | 键反断言：所有维度同值 ⇒ 重排调用 == 0（`FitText` 与 `MeasureText` 计数均为 0），且**行数 == `FitText` 调用数**（重建一次时） | A8 + 风险④ | 新增测试局部 `CountingFakeMeasurer`（先例 = `WidgetTests.cpp:539-553` 的 `CountingMeasurer`，**需补 `FitText` 覆写转发**——现物只计 `MeasureText`）；★ S7 的 `RecordingTextMeasurer` 可与它同源派生（记录传入串 + 计数） |
| **T38-16** | 真窗口白名单**四**件合一（★ v1.1 由三件扩为四件）：① 焦点态绘制（`:1430`）· ② 行内 x 精确定位（`:796`）· ③ 无头求 preferred 后挂窗 ⇒ 重排（`hasMeasurer` 维）· ④ ★ **`SetSingleLine(false)` 后无需外部失效即重排重绘**（PD38-6⑥ / C38-9；无头下 `Invalidate()` 是 no-op ⇒ 只能在此验，A5 白名单第 ⑤ 项） | A5 白名单 ①③④⑤ | 复用 ★ `TextBoxTests.cpp:1606-1624`（v1.0 误写 `:1598-1640`）的 `CaretProbe` 装置（Phase 37 T37-4 同型扩展，**`Paint` 传 `abs`**）；★ N0 现状复现亦在此条（无头复现不了，需求 A5④） |
| **T38-17** | ★ **v1.1 新增（D38-I）**：折行态 clip 宽 = **控件内边界**——`PushClipCommand.rect.width == GetWidth() − 2·padding`（**不是** `GetTextAreaWidth()`），且 S4 悬挂行的 `DrawTextCommand` **完整落在 clip 内**（`pos.x + 行宽 ≤ clip.x + clip.width`）；非折行态 clip 宽**逐位同改前** | A6①（收窄后的豁免面）+ D38-I | 夹具 = `W=38 / H=40 / p=0 / wrap on` ⇒ 折行宽 `38−14 = 24`、S4 两行、内容高 32 ≤ 40 ⇒ **条隐藏** ⇒ clip 宽 38、悬挂行 `[0,32]` ⊂ `[0,38]` ⇒ 可见。**负对照** = clip 仍取 `GetTextAreaWidth()`（24）⇒ `32 > 24` ⇒ 本条红（量具纪律）；★ **v1.2 订正条可见半段**：同夹具把 `H` 压到 24 使条出现（内容高 32 > 24）⇒ 断言 = **clip 宽仍 38（不因条出现而变）+ 悬挂行 `DrawTextCommand` 仍发射**；★ **不得**断言「溢出段被条覆盖」——v1.1 原句据此写「条为子控件、后画且不透明」是**错的**（`ScrollBarStyle.trackColor` alpha = **0**，`DefaultTheme.cpp:110`；后端对 `a < 1` 走 `BlendAlphaSolid` ⇒ 轨道零像素，只有滑块会压住重叠段）⇒ 「是否被滑块压住」属**像素级**观感，超无头命令判据面，入 R38-3 / R38-9 而**不入断言**（D38-I 论据③） |

★ 与需求稿 §6 的 **+14 → +17** 差异说明（★ v1.1 由 +16 再 +1）：A2 估 4 条，初设按样本 S1/S2/S3/禁则组拆 4 条（禁则组 = 一条五断言）；A8 的「键维正断言」与「反断言 + 线性判据」各成一条（T38-14/15）；A6 的 4 项压成 3 条（④⑤ 同状态机、合并）；★ **新增 T38-17** = D38-I 的判别面（需求 v1.1 的验收表里没有 clip 宽这一项 ⇒ 需求 v1.2 已把它补进 A6① 与 D38-10）⇒ **拆合相抵后 +17，属判据细化非范围膨胀**（Phase 37 §4 同型说明）。★ 存量红线：全部 513 条必须**在不改一条既有断言**的前提下绿；若某条既有用例因 `m_lineStarts` 改名而需改**编译面**（`using`/探针），只允许机械改名，不允许改期望值。

---

## §5 契约 C38-1..9（每条附机检）

| # | 条款 | 机检方式 |
|---|---|---|
| **C38-1** | **一张行表、两个口径**：`LineRange` == 归属（`startCp`,`ownedEnd`）、`LineDrawRange` == 裁剪（`startCp`,`startCp+cpCount`）；**只有** `OnPaint` 的行文本发射用裁剪口径，其余消费者一律归属口径 | `grep -n "LineDrawRange" ECDI/src/Widget/TextBox.cpp` 命中集合 == **1**（绘制）；`LineRange` 体内**无** `- 1` 表达式；`grep -n "m_lineStarts"` 命中 == **0** |
| **C38-2** | **码点守恒**：`ownedEnd(last) == count`；`start(i+1) − ownedEnd(i) ∈ {0,1}`，取 1 当且仅当 `cps[ownedEnd(i)] == '\n'` | T38-7 遍历全表（非抽样，9 样本 × 2 档宽 = 18 次） |
| **C38-3** | ★ **v1.1 重写（D38-I 后「三者同源」不再成立）**：**三个宽度、各自单一来源、互不成环**——折行宽 == 光标 clamp 宽 == `GetTextAreaWidth()`（`:662-674` 唯一表达式；折行态该值**不含** `IsVisible()` 的任何输入）；**clip 宽 = 控件内边界 `GetWidth() − 2·padding`**（仅折行态；非折行态仍 == `GetTextAreaWidth()`，逐位同）。★ **三者不得互相派生**：clip 是发射期参数、**不是布局输入**（不进 `wrapWidth`、不进 `n`、不进 `GetMaxScrollOffset`）⇒ §1.2 的独立性证明不被 D38-I 削弱 | `grep -n "GetTextAreaWidth" TextBox.cpp` 命中 == **5 行**（★ v1.2 订正计数：v1.1 写「4 处」漏了 `:523` 的注释行）= **代码 4 处**（`:569` clamp / 定义 `:662` / `:693` 光标上限 / `:1369` clip）+ **注释 1 处**（`:523`「与 `GetTextAreaWidth` 对称」），且**新增宽度表达式 == 0**；`:1369` 的 clip 表达式必须含 `IsWrapActive()` 三目；★ **v1.2 追加两条注释级机检**：`GetTextAreaWidth()` 体内 **`:667-668`**（v1.5「否则文本会钻到条底下…被条压住」）与 **`:669`**（K9/D4「无循环依赖」）**两条在案注释均须改写**（前者限定为「让位量约束的是布局宽、不是 clip」并订正「被条压住」的措辞——K15③；后者按 §1.2 显式推翻）⇒ 机检 = 改后该函数体内 `grep "钻到条下\|无循环依赖"` 命中 == **0**，且**不得静默改码**（PD38-6⑤ 的边界注记）；T38-8（两态相等）+ T38-17（clip 宽与负对照） |
| **C38-4** | **显示串唯一**：折行只吃 `BuildDisplayText()`；`BuildTextLayout` 内不得再出现真实串。★ **v1.1 定位升级（P0-1）= 落点表完备性检查**，不只是验收手段 | `BuildTextLayout` 函数体（`:202-309`）内 `grep m_text` 命中 == **0**（改前实测 **4 行 5 处** = `:210/228/229/230/269`；★ `:333` 调用点与 `:396-398` 发射点的 `m_text` **必须保留**，机检范围严格限定在函数体内）；T38-14h 的 `RecordingTextMeasurer` 断言喂入串含 `E2 80 A2` 且不含明文 |
| **C38-5** | **`Label` 零参与**：`ownedEnd` 不被 `TextWidget` 的任何绘制 / preferred / `BlockWidth` 表达式读取 | `grep -n "ownedEnd" ECDI/src/Widget/TextWidget.cpp` 命中仅在 `emplace_back` 构造处（**3 处** = `:212/:232/:258`）；T38-2 |
| **C38-6** | **失效只经键 + 显示串不变式**：① 除 `RecalculateLines()` 内的键比对外，**禁止**任何「setter 记得置脏」的行表失效（`m_needsLineRecalc` 已退役，不得复活）；② ★ **v1.1 新增**——任何改变 `BuildDisplayText()` 输出的状态转移**必须 bump `m_textRevision`**（这是键维 1 覆盖文本维的前提）。★ **已知唯一越序点**：`CancelComposition` 快照支（`RestoreSnapshot` 体内 `:1085` bump → `:1087` `EnsureCaretVisible()` 触发重排并存键 → 返回后 `:955-958` 才清组合标志、**不再 bump**）⇒ **组合三维必须留在键里**（§1.3 维 10）。修法二选一，本稿取 ⓐ：ⓐ 键里保留三维（零风险）；ⓑ 把 `:955-958` 提到 `RestoreSnapshot` 之前（**否**——改一条已被 IME 用例覆盖的撤销路径的语句序，收益仅省 3 个字段） | `grep -rn "m_needsLineRecalc"` 命中 == **0**；`grep -n "m_isComposing = \|m_compositionStart = \|m_compositionLength = "` 的 **9 处写入**逐处核对是否 bump 成对（`:898/899/908`→`:913`、`:928/930/931`→`:935`、`:955/957`→**越序**、`:964/966`→`:970`）；新维度进键必须同步新增一条 T38-14 子断言（沿 Phase 36 K8 底账纪律）；T38-14i |
| **C38-7** | **配套语义边界**：折行期 `m_scrollOffsetX ≡ 0`（钉点在 `EnsureCaretVisible` 单处）；`Home`/`End` 仍为全文首/尾、与 Up/Down 的视觉行语义**不同源 = 已知现状**；`Left/Right`/`GetWordBounds` 不因折行改变 | T38-11⑤；`grep -n "m_scrollOffsetX = " TextBox.cpp` 写点 == **1**（+ 构造初值）；`OnKeyDown` 的 `Home`/`End` 分支零 diff |
| **C38-8** | **默认关闭逐位不变**：`SetWordWrap` 缺省 `false` ⇒ `GetTextAreaWidth` / `GetPreferredSize` / `RecalculateLines` / **`OnPaint` 的 clip**（★ v1.1 补：D38-I 只在折行态换源）四入口的非折行支与改前同（含 `:669` 注释替换属注释级零行为） | T38-1 + T38-13 + **T38-17 的非折行半段** 常量表 + 513 存量全绿；★ `WrapRequested()` 为 `noexcept` ⇒ `GetTextAreaWidth()` 的 `noexcept` 规格不变；★★ **v1.2 执行注记（详设/实现阶段强制）**：任何以「绘制顺序 / 不透明度 / 覆盖关系」为前提的论证（本条的 clip 面、D38-I、R38-3/R38-5 都属此类），复核必须**同时**核到 ① 调用顺序（`Widget.cpp:506-518`）② **主题默认值**（`DefaultTheme.cpp` 的 `trackColor`/`thumbColor` alpha）③ **后端 alpha 分支**（`GDIBackend::DrawRect` 对 `a < 1` 走 `BlendAlphaSolid`）——v1.1 只核了 ①，得出「条盖住溢出文本」的假结论（§8 v1.2 条①） |
| **C38-9** | ★ **v1.1 新增（P1-1）**：**改判据的 setter 必须请求重绘**——任何改变 `WrapRequested()` / `IsWrapActive()` 取值的 setter 必须 `Invalidate()`（键只解决「算得对」，不解决「何时算」）。现状：`SetWordWrap`（`TextWidget.cpp:100-114`）✓ · `SetEchoMode`（`TextBox.cpp:148-153`）✓ · **`SetSingleLine`（`:162-167`）✗ ⇒ 本 Phase 补**（PD38-6⑥），且体内「无需重绘」的注释须一并改写（它只影响 Enter 语义时代的判断已作废） | 逐 setter 读体内是否含 `Invalidate()`（3 处全表）；T38-16④（**真窗口**——无头 `Invalidate()` 是 no-op，`Widget.h:204`）；★ 条的重同步**不需要**额外接线：`OnPaint :1290` 每帧 `SyncScrollBar()` |

---

## §6 影响面

| 项 | 估值 | 说明 |
|---|---|---|
| 公共头 | **100 → 100** | 全在既有头内改动（`TextWidget.h` 可见性下移 + `TextBox.h` 成员类型）；★ D38-A ⓑ 的「+1 内部头」路线已被 §1.5 的依赖方向论证否掉，故无新头 |
| 公共 API | **public +0** | 无新枚举、无新公共方法、无新公共头文件 |
| protected 面 | **+7**（需求 §6 估「+3」⇒ **本稿如实上修**，沿 Phase 29 E6 先例） | 基类下移 3（`TextLine` / `TextLayout` / `BuildTextLayout`）+ 基类新访问器 1（`TextRevision()`）+ 派生新 protected 3（`WrapRequested()` / `IsWrapActive()` / `LineDrawRange()`）；★ 全部 protected，外部使用者可见面不变 |
| 用例 | **513 → 530**（+17；★ v1.1 由 +16 再 +1 = T38-17） | §4 表；真窗口用例 1 条（T38-16，四件合一），其余 16 条无头 |
| CMake | **0** | 测试全部落 `TextBoxTests.cpp`（既有文件）+ `RunAllTests` 手工登记声明与调用；★ `tools/phase38_wrap_probe.py` 是**文档复算脚本**（评审方产出、本侧复跑），不进构建 |
| 源码改动面 | `TextWidget.h`（3 下移 + 1 字段 + 1 访问器 + `:61-70` 注释扩写）· `TextWidget.cpp`（★ **六处**：`:210/:212/:220-233`（含 `:228-230`）`/:258/**:269**/:333`）· `TextBox.h`（成员类型 + 10 键字段 + 3 protected 方法 + **七处**注释订正 + 删 `m_lineStarts`/`m_needsLineRecalc`）· `TextBox.cpp`（`RecalculateLines` / `LineRange` / 新 `LineDrawRange` / `GetTextAreaWidth` / `GetPreferredSize` / `MultilinePreferred` / `EnsureCaretVisible` / **`OnPaint` 的 clip + 行区间** / ★ **`SetSingleLine` 补 `Invalidate()`** / **9 站 13 处读取改名** + 8 处惰性门 + 13 处置脏删除）· 测试 | ★★ **后端一致性口径（沿需求 §6 收紧表述）**：折行发生在**命令发射之前**（纯布局/几何，GDI/GL 零改动），但**断点由 `FitText` 决定** ⇒ 真字体跨后端折行数**不承诺相同**（#69：`Consolas` GDI 152 vs GL 133，引用未复跑）；像素级判据只在 `FakeTextMeasurer` 下跨后端可比。**不得写「GDI/GL 天然一致」** |
| 风险 | **中-高** | 见 §7（★ v1.1 由 7 条扩为 9 条）；本 Phase 最大的三个不对称 = ① 首次把只读引擎接进编辑器语义（归属 / 显示串 / 可达性），② 消费点 9 站 13 处读取 + 8 处惰性门漏改即绘制-命中错位，③ ★ **键只保证「算得对」不保证「何时算」**（C38-9 / R38-8） |

---

## §7 风险 R38-1..9

| # | 风险 | 缓解 |
|---|---|---|
| **R38-1** | **消费点漏改**（Phase 37 N1 同族教训：多消费点必须逐点列全表） | §3 PD38-7 全表（★ v1.1 与需求 §2.1 的 C1–C11 对齐、读取点与惰性门分栏、补齐 v1.0 漏列的 C1）· C38-1 机检 · T38-3..6 四路同源 × 5 组样本（漏改任一站 ⇒ 至少一组红） |
| **R38-2** | **`dpi` 维无判据**（无头恒 0，本仓无多屏/改 DPI 夹具） | ★ **如实入账为已知缺口**（机制与 C29-10 已证同型 ⇒ 不是新增风险面，但**未被本 Phase 判别**）；不为此新造夹具（夹具 = 范围膨胀） |
| **R38-3** | **常驻条槽的观感代价**（条隐藏时右侧 14px 永不使用的槽 + 早断一格） | §1.2 明确为**有意代价**（Notepad 型折行既有做法）；D38-3 ⓑ（文本钻到条下）已否；★ **v1.1 减轻**：该槽**不再吃掉悬挂字符**（clip 取控件内边界，D38-I）⇒ 代价只剩「折行位置比可见区早一格」；★ **v1.2 补该减轻的反面账**：槽现在**会被悬挂字符占用**，故条可见时悬挂字符可能与**滑块**重叠被压一截（轨道 alpha 0 ⇒ 不覆盖，`DefaultTheme.cpp:110`，D38-I 论据③）⇒ 这是 D38-I 换来的**局部观感账**，与「早断一格」并列为该槽的两条代价，均**不入断言**（像素级）；★ 首个真实消费者若反对，重开的是 **D38-3 的形态**而非本 Phase 的其余决策 |
| **R38-4** | **行高双接缝（K13）在折行下暴露**：preferred 用 `L_pref`、几何用 `L_geom`，`FracMeasurer`（19.2）型夹具下两者不等 ⇒ 「高度与绘制行数不匹配」 | §1.6 口径写死 + 本 Phase 折行用例**不用**该夹具；统一两接缝属 §3 非目标七 ⇒ 要拉回先改需求稿再评；`FractionalLineHeight` 既有用例（`TextBoxTests.cpp:1279`）保持**不开折行** |
| **R38-5** | ★ **悬挂字符可见性（v1.1 严重度上修 + 处置换方案，P1-2）**：`:295` ⑤ 上提闭标点 ⇒ 行宽可 > 折行宽，而 `scrollOffsetX ≡ 0`。★ **v1.0 判为「微超不可达」并只写进 A6① 豁免 = 严重度低估**：复算 S4 被裁 8px = **一整个码点**（`。` 全在 clip 外），即「**光标可停、选区可选中、字却看不见**」；且首个真实消费者 = harness `ComposerBox`（**中文聊天输入框**，行尾悬挂闭标点是中文排版高频形态）⇒ 大概率首发即命中 | **处置 = D38-I**（折行态 clip 宽取控件内边界 `GetWidth() − 2·padding`）：悬挂字符在**条隐藏与条可见两态下均完整发射且可命中**（★ v1.2 订正：v1.1 曾写「条隐藏态完整可见 / 条可见态被条覆盖」——后半句**错**，默认主题轨道 alpha = 0（`DefaultTheme.cpp:110`）⇒ 轨道零像素、条**盖不住**文本，只有**滑块**会压住与之重叠的一段）⇒ 症状由「**两态恒不可见**」降为「**恒可见，条出现时可能与滑块重叠**」，即从**可见性缺陷**降为**局部观感账**。判据 = T38-17（含负对照；★ 条可见半段只断 clip 宽不变 + 命令仍发射，不断像素覆盖）。★ **不放开 `scrollOffsetX`**：放开 = 折行态引入水平滚动，与立项动机直接冲突（D38-I 论据 ④）。残余见 R38-9 |
| **R38-6** | **每帧成本**（`OnPaint` 每帧 `SyncScrollBar` → `RecalculateLines`，`:1287-1290`） | 键命中 ⇒ 零 `FitText`/零 `MeasureText`（T38-15 反断言）+ 线性判据「行数 == `FitText` 调用数」；★ 最坏 = 一次重排全量 `FitText`（`SetSize` 后首帧）+ 一次 `BuildDisplayText()` 拷贝（Normal 态，PD38-4 成本账），`AutoSize` 路径经同一键 ⇒ 不重复测；大文档虚拟化另账（#49/#33） |
| **R38-7** | **退役 `m_needsLineRecalc` 引入回归**（13 站点 + 8 惰性门是既有失效体系的承重墙） | 本日实测 13 站点与 13 次 `MarkTextChanged()` **严格成对** ⇒ 键维 1 严格覆盖（§1.3）；T38-11（Undo/Redo）+ T38-14a（revision 维）+ 513 存量三条一起兜；★ 若实施期发现任一站点不成对 ⇒ **保留布尔位**并登记缺口，不为退役而退役 |
| **R38-8** | ★ **v1.1 新增（P1-1）· setter 触发缺口**：键只保证「被调用时算得对」，**不保证「该算时被调用」**（键只在 `RecalculateLines()` 入口比对，而入口只在有消费点/重绘时才进）。`SetSingleLine`（`:162-167`）改激活判据却**不 `Invalidate()`** ⇒ 切换后行表、内容高、条显隐全变，但界面停在旧状态直到下一次**无关**失效 | **PD38-6⑥ 补 `Invalidate()`**（条的重同步免费：`OnPaint :1290` 每帧 `SyncScrollBar()`）+ **C38-9 契约化**（三个改判据的 setter 全表 + 逐处机检）+ **T38-16④ 判别**（真窗口；无头 `Invalidate()` 是 no-op ⇒ 白名单第 ⑤ 项）。★ **结构性余量如实登记**：本 Phase 只补齐**已知的**三个 setter；将来任何新增的判据输入（如「折行最小宽」）都必须同时进 C38-9 的全表，否则同一缺口会复活 |
| **R38-9** | ★ **v1.1 新增（D38-D + D38-I 的残余账）· 悬挂量无上限 + 窄带退化**：① ⑤ 的上界取 `segEnd`（`:292-294` 在案）⇒ **连续闭标点串**的悬挂量无固定上限，超过控件内边界时 D38-I 也救不回来（仍被裁）；② D38-D 删掉 `折行宽 > 0` 后，`0 < W ≤ 2p+14` 窄带内 `GetTextAreaWidth()` 可为 **0 或负**（脚本实测 `W=10 / p=0 ⇒ −4`）⇒ 文本退化为「**一字一行**」 | ① 入账为**已知边界**（不为它放开 `scrollOffsetX`，也不给 ⑤ 加上界——加上界 = 改引擎禁则语义 = 破 A1 红线②）；A6① 的豁免面据此**收窄**为「行宽 > **控件内边界**」（不是「> 折行宽」），需求 v1.2 已同步。② 引擎侧**结构性有界**：`:250` 的 `limit = max(0, maxWidth)` 夹住负宽 ⇒ `:270` `hardEnd` 可 == `scan` ⇒ `:282` 硬断一字 ⇒ `:302` 绝不零推进 ⇒ **`n ≤ count`、无死循环**（脚本实算 `limit = 0 / −4 / 4` 三档均逐码点成行）；clip 侧由 D38-I 取控件内边界 ⇒ **不出现负宽 clip**（D38-I 论据 ⑤）。★ **不加最小宽约束**（= 范围膨胀）；窄带观感账如实记：控件被压到小于一个条槽时，折行框表现为逐字成行 |

---

## §8 修订记录

- **v1.2（2026-10-28）** **D38-I 论据③ 的前提证伪 ⇒ 一条论证 + 五处表述订正 + 上游需求稿 A6① 豁免理由回流改写**。本轮不是评审驱动，而是**落盘 v1.1 后自查 `ScrollBar` 主题默认值时发现的**（沿「doc 自称完成必须对码复核」纪律）。
 ① ★★ **证伪**：v1.1 的 D38-I 论据③ 写「条是子控件、后画，且 `ScrollBar.cpp:217` 是**不透明** `DrawRect` ⇒ 条可见时溢出段被条**覆盖**」。**实测为假**——`DefaultTheme::GetScrollBarStyle()` 的 `trackColor = Color::FromRGBA8(240,240,245,**0**)`（`DefaultTheme.cpp:110`，v1.6「轨道**默认透明**」2026-10-01 用户拍板），且**同处注释已核实后端语义**：「`GDIBackend::DrawRect` 对 `a < 1` 走 `BlendAlphaSolid` ⇒ alpha 0 **不产生像素**」⇒ 轨道那次 `DrawRect` **零像素、盖不住任何文本**；不透明的只有**滑块**（`thumbColor` alpha 255，`:111`）。子控件后画的顺序（`Widget.cpp:506` `PushClip(self)` → `:508` `OnPaint` → `:515-518`）**属实**，但「后画」不等于「盖住」。
 ② **订正后的正确表述（写进 D38-I 论据③）**：条隐藏态悬挂字符**完整可见**；条可见态悬挂字符**仍然可见**，只是**可能与滑块重叠被压住一截** ⇒ D38-I 的收益不是「常态可见、条态被盖」，而是「**两态恒可见**，把 v1.1 判定的**可见性缺陷**降为**局部观感账**」。与「不改」的比较仍然成立且更强：不改 = clip 宽 == 折行宽（K15①）⇒ 悬挂字符在**两态下恒不可见**。
 ③ **连带订正五处同族表述**：`§1.4` S4 行（「条隐藏态可见」→「两态都可见」）· `§4` T38-9（同）· `§4` T38-17 的条可见半段（**删掉**「断言退化为『溢出段被条覆盖』」，改为「断言 clip 宽不因条出现而变 + 悬挂行 `DrawTextCommand` 仍发射」，并写明像素覆盖**不入断言**）· `§7` R38-3（补「槽现在会被悬挂字符占用 ⇒ 条可见时可能与滑块重叠」这条反面账）· `§7` R38-5（处置文本重写）。★ v1.1 的 §8 条目⑤ **原文保留 + 行内标注作废理由**（留痕纪律，不静默改写历史条目）。
 ④ **上游回流（需求稿 v1.1 → v1.2）**：A6① 的**豁免理由**改写——豁免不该来自「反正看不见所以不用管」；新增 **K15**（clip 链与条的不透明度，三处 `sed` 直读）作为 D38-10 的事实基础；K10 的代价句由「A6① 需豁免或不等式」改判为「**必须修的缺陷** ⇒ D38-10」；新增**范围项 7** 与 **D38-10**；非目标表补两行（`GetTextAreaWidth()` 取值不改、⑤ 上界不改）。
 ⑤ ★ **自查方法论入账**：这条错误在 v1.1 里**同时出现在 4 个章节**且互相印证（论证 → 风险 → 判据 → 样本），说明「一处错、多处引用」在本稿的表驱动结构下会**放大**而不是被交叉检查捕获 ⇒ 详设阶段对任何「靠绘制顺序/不透明度兜底」的论证，**必须核到主题默认值与后端 alpha 分支**，不得只核调用顺序（写入 §5 C38-8 的执行注记）。
 **结构变化**：§2 D38-I 论据③ 重写（条数 6 不变）· §4 T38-9/T38-17 判据文本订正（**用例数 17 不变**）· §7 R38-3/R38-5 处置文本订正（**风险条数 9 不变**）· 上游需求稿 K14 → **K15**、范围六 → **七** 项、D38-9 → **D38-10**。★ **本稿的方案本体、决策定案、用例预算（513 → 530）均不变**。
- **v1.1（2026-10-28）** 外部评审「**有条件通过**」吸收——评审方抽查约 80 处行号锚点约 74 处命中、实跑 `ecdi_tests.exe` = **513 passed / 0 failed**、核 HEAD `6a3807b` / 公共头 100，并按 HEAD 源码把 `BuildTextLayout` **一比一复刻成 `tools/phase38_wrap_probe.py`** 实算 §1.4 样本表与 §1.2 A4 夹具（本侧**复跑核对**，输出逐字一致）；承重论证（`:349` 口径冲突 / `:669` 作废 / E2 的 `−1` / E3 的自指环 / K14 三道闸 / K13 双接缝 / 13+13 置脏配对 / 8 处惰性门）被逐条亲核为真。**P0×3 / P1×4 / P2×7 逐条处理（每条落稿前本侧独立复验，复验结果如实记）**：
 ① **P0-1 · 显示串穿透漏 `TextWidget.cpp:269` ⇒ §1.5 落点表重写 + PD38-2 由四处改六处 + C38-4 定位升级**。**复验 = 属实**：`sed -n '202,310p'` + `grep -n m_text` 实测函数体内 **4 行 5 处**（`:210/228/229/230/269`），与审核稿逐字一致；`:269` 的 `FitText` 正是「决定断在哪」的那次调用，其默认体（`TextMeasurer.h:68-117`）在传入串上做字节换算 + `MeasureText(切片)` ⇒ 漏改即「索引按显示串、宽度按真实串」的混合口径。修法照单全收：落点表改**三处语义落点**（解码 / 补测 / `FitText`）、C38-4 由「验收手段」升为**落点表完备性检查**并限定机检范围在函数体内（`:333`/`:396-398` 的 `m_text` 属 `Label` 路径、必须保留）。需求稿 D38-1 ★② / §6 / §7 闸门 5 的「两处落点」同步订正为 v1.2。
 ② **P0-2 · A4 第二态 `H=32` 下负对照假绿 ⇒ §1.2 表重排 + T38-8 夹具改数字 + 新增前置纪律**。**复验 = 属实**：复跑脚本 `fixed_point()` 得 `OLD(H=64)=(40.0,2,32.0,False)`、`OLD(H=32)=(40.0,2,32.0,False)`（两态同值 ⇒ 假绿）、`OLD(H=24)=(26.0,3,48.0,True)` 而 `NEW(H=64)=(26.0,3,48.0,False)`（两态异值 ⇒ 红）。修法照单全收：第二态改 **`H=24`**、v1.0 的 `H=32` 行**保留在表内并标作废**（作为「判据不具判别力」的第二类反证）、补写「**两态必须各自落在该实现的不动点上**」的前置纪律。
 ③ **P0-3 · §1.4 样本表 3/7 与引擎实算不符 ⇒ 全表改由脚本产出 + S5 整条换串 + S7 换量具**。**复验 = 属实**（复跑脚本逐串核对）：S3 的串 `"ab cd\nef gh"` 实为 **11** 码点、实算 **2 行**（`[0,5)` w40 own5 · `[6,11)` w40 own11`），补一个空格成 `"ab cd \nef gh"`（12）后**精确命中文档的三行期望** ⇒ v1.0 是**串丢字符**而非期望算错；S5 的 L1 宽实为 **w16**（v1.0 写 w32）；S7 的第二断言在 `FakeTextMeasurer`（按码点数 × 8）+ 掩码 1:1 下**恒真**。★ **本侧复验另得一条评审未指出的更重问题**：v1.0 的 S5（`"一二三（四"`）**根本不触发 ④**——纯 CJK 串里每个表意字都是软断点，`FindLastSoftBreak`（`:190-196`）在 `（` 之前就返回 ⇒ ④ 的 `while`（`:286`）一次都不进 ⇒ 该样本**对「复用即继承字面集」零判别力**。处置：S5 换串为 **`"ab（cd"` w24**（实算 `[0,2)` w16 own2 · `[2,5)` w24 own5，④ 真实触发）；新增 **S5b `"（a"` w8**（实算 `[0,1)` w8 own1 · `[1,2)` w8 own2 ⇒ 判别 **④b 守卫 `:289`**，此前无覆盖）；旧串降为 **S5c 对照组**（宽改 w16）并在表内写明「**④ 在纯 CJK 串上不可达**」⇒ 样本 7 → **9**、T38-7 由 14 次对账改 **18 次**。S7 的量具换成审核稿建议 ②（`RecordingTextMeasurer` 断言喂入串含 `E2 80 A2`、不含明文），① 留作后备；**「等值即证走显示串」的说法删除**。★ **制度化**：样本表纪律入头部（期望值一律由脚本产出，详设不得手推）。
 ④ **P1-1 · `SetSingleLine` 触发缺口 ⇒ PD38-6⑥ + C38-9 + T38-16④ + R38-8**。**复验 = 属实**：`:162-167` 体内只改标志位、注释写「无需重绘」；对照 `SetEchoMode`（`:148-153`）/ `SetReadOnly`（`:155-160`）/ `SetWordWrap`（`TextWidget.cpp:100-114`）三处**都有** `Invalidate()` ⇒ 唯 `SetSingleLine` 缺。取审核稿两个选项中的**前者**（补 `Invalidate()`），并**契约化**为 C38-9（三个改判据的 setter 全表 + 机检），理由 = 只补一处会把「将来新增判据输入」的同类缺口留在暗处。★ 补一条审核稿未提的事实：无头下 `Invalidate()` 是 no-op（`Widget.h:204`）⇒ 判别只能落真窗口（A5 白名单追加第 ⑤ 项）。
 ⑤ **P1-2 · R38-5 严重度 + 「clip 宽取控件内边界」备选 ⇒ 采纳为定案 D38-I（新）+ T38-17（新）+ C38-3/C38-8 重写 + 需求 v1.2 补范围项 7 / D38-10 / A6① 收窄**。**复验 = 属实**：S4 实测行宽 32 / 折行宽 24 ⇒ 被裁 8px = 一个完整码点，`scrollOffsetX ≡ 0` ⇒ 永久不可见而光标可停；harness 首个消费者是中文聊天框 ⇒ 高频命中。★ **本侧把「备选」升为定案并补齐五条论证**（审核稿只给了方向）：无环性（clip 不是布局输入 ⇒ §1.2 的独立性证明不受影响）、不「钻到条下」的机制（`Widget.cpp:506/508/515-517` 的子控件后画顺序 + `ScrollBar.cpp:217` 不透明 `DrawRect`）← ★ **v1.2 订正：这条论证的前提是错的**——默认主题 `trackColor` alpha = **0**（`DefaultTheme.cpp:110`）⇒ 轨道零像素、**盖不住**文本，只有滑块会压住重叠段；正确表述见 D38-I 论据③，连带订正 R38-3 / R38-5 / T38-9 / T38-17 / S4 五处、并回流订正需求稿 A6① 的豁免理由（K15 新增）【v1.2 订正止，下列原文续】**原论证的其余三条仍成立**：A1 结构性保持（**仅折行态**换源、非折行态逐位同）、附带收益（D38-D 删宽度合取项后窄带内 `GetTextAreaWidth()` 可为负 ⇒ 若 clip 仍取它会得到**负宽 clip**）、可判性（`PushClipCommand` 是既有命令类型）。否掉的替代 = 放开 `scrollOffsetX`（与立项动机冲突，读点 4 处 + `ClampScrollOffsetX` 语义全要改）。
 ⑥ **P1-3 · 棘轮「结构上不可达」过强 ⇒ D38-D 删 `折行宽 > 0` 合取项 + PD38-6② 措辞订正 + T38-12 追加窄带一档**。**复验 = 属实**：`0 < W ≤ 2p+14` 时本类判据假 ⇒ preferred 落 `:199` ⇒ 基类 `IsWrapLayoutActive()`（`TextWidget.cpp:123-129`，只要有窗口且 `GetWidth() > 0`）为真 ⇒ `:436` 返回 `GetWidth()` 经 `:201` `+2p` ⇒ 棘轮照旧；`AutoSize` 从小宽往大宽爬 ⇒ 必经该带。★ 取审核稿两个选项中的**换闸**（而非只把「不可达」降级为「区间内不可达」），理由 = 降级只是把缺陷写清楚、没消除它，且同一窄带还会造成「preferred 按折行算高、行表按 `\n`-only 建表」的 N0 式口径分裂。换闸后的退化面（`limit ≤ 0`）由**脚本实算**证明有界：`limit = 0 / −4 / 4` 三档均逐码点成行、`n ≤ count`、无死循环（引擎的 `:250` 夹取 + `:282`/`:289`/`:302` 三重推进保证）⇒ 入账 R38-9，**不加最小宽约束**。
 ⑦ **P1-4 · PD38-7 站表口径混用 + 漏 C1 ⇒ 整表重排**。**复验 = 属实且本侧把计数修得更准**：`m_lineStarts` 实测 = **13 处读取命中 / 9 个站点**（审核稿称 11 处：漏计 `:479`，并把 `:480-481` 记作一处）+ 3 处写入（`:447/448/455`）+ 6 处注释。新表**按需求 §2.1 的 C1–C11 编号**逐站对齐（v1.0 的 C1 指 `GetTextTopInset`、需求的 C1 指 `LineIndexFromCodepoint` ⇒ 编号冲突已消除），**读取点与惰性门分栏**（8 处门单列，并注明「门不是读取点、但必须同表登记，漏一处 = 该路径读旧表」），v1.0 漏列的 **C1（`:461/465/468`）补齐**、C8 的「零改动」订正为「**改名**」（`:684` 直接读 `m_lineStarts[lineIndex]`）。
 ⑧ **P2×7 全部落稿**：⑴ `FindLastSoftBreak` 锚点 `:186-193` → **`:190-196`**（两处：§1.4 S6 与表头）· ⑵ `IsCjkBody` `:178-184` → **`:173-179`** · ⑶ 禁则字面集「27 + 6」→ **`IsLineStartForbidden` 14（`:137-156`）+ `IsLineEndForbidden` 6（`:157-170`）= 20**，另补 `IsCjkBody`（`:173-179`）/ `IsSoftBreakBefore`（`:185-187`）· ⑷ `FitText` 越段收敛点 `:266` → **`:273`**（`:266` 是 `const std::size_t lineStart = scan;`）· ⑸ §1.3 维 5 的 `lround(GetDpiScale()×96)` **不可编译**（`Widget` 上无 `GetDpiScale()`）⇒ 改为 `window ? lround(window->GetDpiScale()*96) : 0`（`TextWidget.cpp:316-321` 同式）· ⑹ §1.3 维 11 的理由「唯 `UpdateComposition` 的纯游标推进不 bump」**为假**（`:906-907` + `:912-913` 每次都成对）⇒ **换真实理由**（`CancelComposition` 快照支的越序点：`RestoreSnapshot` 内 `:1085` bump → `:1087` `EnsureCaretVisible()` 存键 → 返回后 `:955-958` 才清标志且不再 bump）⇒ **三维保留**并升格为 C38-6② 的显示串不变式 · ⑺ 规模与测试锚点订正：`docs/**` **197** / `docs/dev/*.md` **196**（v1.0 写 195/191）、`using TextBox::GetTextTopInset` 在 **`:52`**（v1.0 写 `:55`）、`CaretProbe` 在 **`:1606-1624`**（v1.0 写 `:1598-1640`）。
 ⑨ ★ **§1.1 的「40→48 超出折行宽」论证替换（审核稿 §5 末尾项，但本侧换了理由）**：v1.0 的算式**不成立**（S1 的 `ownedEnd` 口径行宽恰 = 48 = 折行宽，不构成超出）。★ **审核稿给出的替代论据本侧亦不采纳**——其「`ownedEnd ≤ hardEnd ≤ FitText 边界` ⇒ 不会超宽」在 **⑤ 悬挂态不成立**（`:295` 上界取 `segEnd`，S4 实测 `ownedEnd 4 > hardEnd 3`、行宽 32 > 24）。本稿改用自己的两条：① 命令层洁净 + 与 `Label` 发射口径（`TextWidget.cpp:396-398`）一致；② 语义分离本身（两口径在软行上必然相差「行尾被裁空格数」，一个字段承载不了两个语义）；并写明「两口径只差行尾空格；无 ⑤ 时归属口径也不超宽，**有 ⑤ 时两口径同等超宽**」⇒ 超宽的处置属 D38-I / R38-5，不属该论证。
 ⑩ ★ **本侧复验衍生的第三项变更：失效键 11 维 → 10 维**（删 `singleLine` 维）。论证 = `m_singleLine` 对行表的影响**只经 `wrapRequested` 一个通道**（折行支入口含它；非折行支是纯 `\n` 扫描、不读它；`BuildDisplayText()` 也不读它）⇒ 保留 = 永不独立触发的冗余字段，与退役 `m_needsLineRecalc` 同一纪律。**验收不删**：T38-14g 仍单独造「只变 `singleLine`」的用例（判别维 8 的覆盖性）。★ 组合三维**未删**（v1.0 的理由假、结论对，见 ⑧⑹）。
 **未采纳项与理由**：⑴ 审核稿 §5 末尾对「两口径分离」的**替代论据**不采纳（其不等式在 ⑤ 悬挂态不成立，见 ⑨）——结论采纳、理由换掉；⑵ 审核稿 §5 表 P2-6 建议「键里保留三项无害（防御）」——本稿**升级为有证依据**（找到真实越序点 + 写成 C38-6② 的不变式 + 配 T38-14i 判别），不接受「无害所以留着」这种无判别力的登记；⑶ 审核稿 P1-3 的两个选项中只取「换闸」，不取「降级措辞」（理由见 ⑥）。其余 P0/P1/P2 全部吸收，无驳回。
 **结构变化**：§1.4 样本 7 → **9**（S5 换串 + S5b/S5c 新增）· §1.5 落点 2 → **3 语义 / 5 处 `m_text`** · §1.3 键 11 → **10 维** · §2 D38-A..H → **D38-A..I**（新增 D38-I clip 宽）· §3 PD38-2 四处 → **六处**、PD38-6 四处 → **六处**、PD38-7 站表**按 C1–C11 重排 + 惰性门分栏** · §4 T38-1..16 → **T38-1..17**（+16 → **+17**，513 → **530**）· §5 C38-1..8 → **C38-1..9**（C38-3/C38-4/C38-6/C38-8 重写或扩充）· §7 R38-1..7 → **R38-1..9**（R38-3/R38-5 改写，R38-8/R38-9 新增）· 上游需求稿 v1.1 → **v1.2**（D38-1 ★② 落点、§6 改动面、§7 闸门 5、A6① 豁免收窄、D38-4 的宽度合取项、新增范围项 7 / D38-10、A5 白名单第 ⑤ 项）。
- **v1.0（2026-10-28）** 初稿落盘，闸门七项逐题收口（需求 §7）。① **§1.1 `LineRange` 新契约全文**：四态表（软行 / 硬行 / 末行 / 尾部空行）+ **端点口径钉死**（`LineRange` = 归属 `ownedEnd`、新增 `LineDrawRange` = 裁剪 `startCp+cpCount`；★ 绘制用裁剪的理由 = 防软断点空格把行宽顶出折行宽一格，S1 下 40→48 ← **该论证 v1.1 已替换，算式不成立**）+ A3 机检式的完整推导（`gap(i) ∈ {0,1}` 对账 + 三条边界等式，非抽样）+ `TextBox.h:349`「`size()-1`」与实现（`size()`）的口径冲突随本 Phase 订正。② **§1.2 折行宽单一来源**：算式（复用 `GetTextAreaWidth()`，不新建宽度函数）+ **独立性证明**（改后 `wrapWidth` 的输入集不含 `barVisible`、不含 `n` ⇒ 自指摘除，不再讨论收敛性）+ **对 `:669` 的显式推翻文本**（原论证当时成立的理由 = 内容高与宽无关）+ **A4 正/负对照三行表**（`W=40` / `"abcdefgh"` / 8宽16高夹具：本方案两态均 26/3 ⇒ 绿；回退谓词 40/2 vs 26/3 ⇒ 红；「5 帧恒定」两态各自都是自洽不动点 ⇒ 假绿——P0-3 的反证在表里给死 ← **第二态 `H=32` v1.1 已改 `H=24`：该 H 下负对照同为 40/2 ⇒ 也是假绿**）+ 条槽观感与「`AutoSize` 后条必隐藏」的不等式。③ **§1.3 失效键 11 维清单**（← **v1.1 改 10 维**）：每维给「变了不重排会怎样」的反例 + 指到一条 A8 用例；★ **`dpi` 维如实入账为无判据缺口**（R38-2）；★ **`m_needsLineRecalc` 退役**（实测 13 站点与 13 次 `MarkTextChanged()` 严格成对 ⇒ `revision` 维严格覆盖，保留 = 写而不读的死标志位；`RecalculateLines()` **名字与 8 处调用形态保留**以最小化消费点 diff）。④ **§1.4 无头可算性**：坐标口径表（局部 / 逻辑前缀宽 / 命令 = `Paint` 传入值）+ **S1–S7 样本三件套**（逐态手工推演 `FindLastSoftBreak` / ④⑤ 禁则 / ⑦ 跳空格，详设可直抄 ← **v1.1 改为脚本产出、不得手推：手推 7 串失准 3 串**），含 A2 点名的**禁则组三条**（S4 悬挂微超 / S5 行尾开括号回退 / S6 超长词硬断 ← **v1.1：S5 原串不触发 ④，已换串并补 S5b/S5c**）与 S7 掩码双断言（含「同值即证走显示串」的判别力设计 ← **v1.1：该断言恒真，量具已换**）。⑤ **§1.5 复用形态定案 ⓐ**，并给出 ⓑ 的**结构性障碍**（`TextWidget.h:150` 的 `mutable TextLayout` 成员 ⇒ 公共头必须见完整定义 ⇒ ⓑ 要么依赖方向倒置要么公共头 +1 且类型抬为公开 API）+ 显示串穿透的**两处落点**（`:210` 与 `:228-230`，缺一即掩码必偏 ← **v1.1 补第三处 `:269`（`FitText`），共五处 `m_text`**）+ 「非投机抽象」论证（第二个真实消费者 = 本 Phase 自身）。⑥ **§1.6 `L` 口径定义**：`L_geom`（`:486-493`，无头硬编码 16）与 `L_pref`（`:222` / `:206`）逐处声明——A2/A3/A4/A6 用 `L_geom`、A7 用 `L_pref`；一致性论证（真窗口同测量器 / `FakeTextMeasurer` 同值）+ 唯一分歧面 = `FractionalLineHeight` 型夹具 ⇒ 本 Phase 折行用例不用它（R38-4），统一两来源属非目标七。⑦ **§1.7 勿重开七项逐项 ✓**（含「不新增公共观测缝」声明）。⑧ 定案 **D38-A..H**（含 D38-D 的 `!IsSingleLine` 硬理由 = K14 三道闸、D38-G 的单点钉 X、D38-H 的 `Home`/`End` 不动 = 已知现状 + 占号新立 ← **v1.1 新增 D38-I；D38-D 删 `折行宽 > 0` 合取项**）· 方案 **PD38-1..9**（逐文件含代码草案 + C1–C11 消费点全表带「需改？」列 ← **v1.1 按需求编号重排、补齐 C1、惰性门分栏**）· 测试 **T38-1..16 = 513 → 529（+16）**（与需求估 +14 的差异 = 判据细化非范围膨胀，逐条对账；真窗口仅 1 条 = A5 白名单三件合一 ← **v1.1 = T38-1..17 / 530 / 白名单四件**）· 契约 **C38-1..8**（每条附机检：`grep` 命中集合恰 N 处 / 零 diff / 函数体内禁出现某标识符 ← **v1.1 = C38-1..9**）· 风险 **R38-1..7**（← **v1.1 = R38-1..9**）。⑨ 影响面如实上修：公共头 100 → 100 · **public +0 / protected +7**（需求估 +3 ⇒ 上修，沿 Phase 29 E6 先例）· CMake 0 · 后端一致性按收紧口径表述。

