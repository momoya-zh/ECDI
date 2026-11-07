# Phase 44 · 段内内联样式（styled spans，#88）· 需求稿

> 状态：**v1.1 评审已吸收 / 待事实勘察 / 闸门待重裁**（2026-11-07）。★ **条 74：状态由评审结论决定**——v1.0 评审结论 = **不建议按现状拍板进初设**（P0×3 / P1×7 / P2×3），本稿吸收全部意见后**仍不进初设**：闸门 1/2/4 需重裁，闸门 2 以勘察为前置（§6），勘察项 O44-1..4 是进初设的闸门。
> 上游 = `roadmap-deferred.md` **#88 行**（2026-11-07 用户拍板「富文本另立项」）。★ **引用口径订正（v1.1，条 114）**：总账引用一律用**行编号（#88 / #53）不用行号**——v1.0 所引 `:184/:198` 已被同批 v2.82 头部插入漂移（现 `:185/:199`），且 HEAD `c137400` 的总账头部仍是 **v2.80**（v2.81/#88 属未提交工作区）⇒「全部行号对 HEAD 亲核」只对**源码与已提交文档**成立，总账版本以工作区为准。
> **触发来源** = 消费工程 `ECDI_fake_harness` 能力需求账本 **REQ-20**（`D:\Project\ECDI_fake_harness\docs\ecdi-capability-requests.md:895-912`，v5.23）——本仓已读原文并逐项对码核验。
> ★ **源码行号**已对 HEAD `c137400` 亲核（2026-11-07，含 v1.1 新增 K15–K18 与评审引用复核）；基线：用例 **588**、公共头 **101**、设计文档 **214**（含本稿），`tools/doc_lint.py` 实测（71 缺陷全为存量 L01）。
> ★★ **门禁机制订正（v1.1）**：`doc_lint.py:101-108` 用 `git ls-files` ⇒ **untracked 新稿不在扫描集**——v1.0 时「71 缺陷与本稿无关」是侥幸成立（评审只读模拟补测：BOM ✓ / 仅 1 条 INFO L02（LF 行尾，同目录 phase43 亦 LF）/ 0 defect）。本稿自 v1.1 起**先 `git add` 再跑门禁**；落地门禁顺序 = `git add` → `python tools/doc_lint.py --select L04,L09,L11,L13 --quiet`（列举式）→ 配阳性对照 → 才提交。

**红线（四条，任一破了本 Phase 即失败）**

1. **零 span 路径逐位等价**：未设 / 清空 spans 时，命令流 / preferred / wrap 布局 / 缓存行为与今日**逐位不变**（结构性保证，不是「像素看起来一样」）；既有文本域用例（Phase 28/29/31/36/38/40/41 全部）**一行不改仍绿**。
2. **测画同源**：每 run 的宽来自**同一 TextMeasurer 实例**的既有求值路径（C29-1 口径的逐 run 推广）；断行边界永不落在 UTF-8 序列中间 / surrogate pair 中间（C29-1/C29-2 继承）。★ v1.1 强化：**emitLine 回退补测站**（`TextWidget.cpp:339`）必须同改，否则软折行行 `width ≠ 绘制 x 累积`（K18 / D44-3②）。
3. **命令流与 DrawText 执行体零改动**（v1.1 收窄措辞）：`DrawTextCommand` / `RenderCommand` variant / `RecordingBackend` **零增删**；`GDIBackend` / `GLRenderer` 的 **DrawText 执行体**一行不动——混排靠「每 run 一条**既有** DrawText 命令」，不让后端做排版（`RenderCommand.h:25` D9 继承）。★ **显式豁免**：字体实例化 / face 选择层（`GetOrCreateFont` / `FaceIdFor` 及其缓存键）是闸门 2 收斜体时的**合法改动面**——v1.0 红线写「两后端一行不动」与 §4.1 斜体改动清单自相矛盾，按 §5.1 既有正确口径收窄。
4. **不借机扩到编辑器通路**（v1.1 改界）：可编辑 `TextBox` 的光标 / 选区 / IME / Undo × spans 本期**不做**（Phase 29 先例）；★ **只读 TextBox 的显示接入**不在本红线内——它是闸门 4-①（倾向本期收，消费者主路径正是它，K17）。块级富内容（标题 / 代码块 / 列表）属应用层，框架不承接。

---

## §1 输入与问题陈述

### 1.1 输入原文（harness 账本 REQ-20，2026-11-07 立案；以下**逐字引用**）

> **缺口（一句话）**：markdown 段内粗体 / 斜体 / 行内 code 需要「同一文本流内混排样式」，而框架文字视觉按控件单值（TextStyle/Font）——「这半句加粗」在框架里不存在。
>
> **★ 代价（为何不在应用层绕行）**：绕行 = 把内联样式拆成多个 Label 拼接 ⇒ ① 打破「一 block 一 TextBox」的选中/复制假设（`--select-probe` / `--selectable-rows` 2026-11-07 刚铺完 tool 体）；② 跨控件折行要自管（框架 wrap 按控件），CJK 混排是重灾区；③ rebuild/hit/answer/grow/cull/select 探针冲击面比表格大且属静默重基线。同 REQ-15 教训：不在应用层塞 `\n` 式绕行。
>
> **验收判据**：① ECDI 侧单测：同一 Label 内粗体/常规/mono 三段混排，测量与折行正确、逐段样式生效（GDI/GL 两链一致）；② 判别力：回退任一段样式 ⇒ 恰该用例红；③ 本仓消费后：转录段内粗体/斜体/行内 code 真渲染，选中/复制仍取块全文**（口径消费时定）**；④ 负对照：既有单字体路径（全部现行探针）不回归。

★ **v1.1 订正（条 79 证据强度）**：v1.0 标「输入原文」却删了判据③的「（口径消费时定）」与代价行的 REQ-15 教训 / tool 体说明——前者把「口径未定」写成既定判据，直接影响闸门 4 的裁决依据。现逐字恢复；**「口径消费时定」是闸门 4-① 的前置事实**：选中/复制取块全文的具体口径由消费期定，框架本期只保证「复制通路与样式正交」的事实基础（K11）。

### 1.2 归属与先例链（能力缺口，非缺陷）

今天的行为**自洽**：内联标记按原文显示、内容不丢——缺的是能力，不是实现说谎（与 Phase 43 的「注释承诺反面」性质不同）。先例链完整且均已对码核验：

| 时点 | 落点 | 原文（摘要） |
|---|---|---|
| Phase 28 | `phase28-long-text-cost-requirements.md:39` / `:72` | 富文本（styled spans）= **非目标，另立项** |
| Phase 36 | `phase36-font-expressiveness-requirements.md:64` | 富文本 → **#53 渲染表现力群**，单消费者不立项 |
| #53 记账 | `roadmap-deferred.md` **#53 行** | 重启条件 = 第二消费者出现 |
| **2026-11-07 裁决** | `roadmap-deferred.md` **#88 行**（v2.81 立、工作区现 v2.83） | 用户澄清口径（防内部 demo 自嗨，非机械数「第二」）⇒ 重启条件成立；**另立项不并 #53**（体量不匹配） |

★ 仓内代码与公共头 grep `RichText/TextRun/InlineStyle/TextSpan/StyleRun/SetRangeFont` **零命中**（仅 `phase7-textinput-requirements.md:21` 一处文档性提及）——能力确实不存在，无暗路可走。

### 1.3 事实矩阵（源码对 HEAD `c137400` 亲核；K15–K18 为 v1.1 评审复核新增）

| # | 事实 | 证据（文件:行） |
|---|---|---|
| **K1** | 文字视觉 = **控件级单值** `TextStyle m_style`（foreground + font），注释自述「唯一来源」 | `TextWidget.h:23`（类注释）· `:108-109`（成员） |
| **K2** | `TextStyle` 只有两字段；运行时覆盖是**整值** `optional<Font>`，无区间概念——复用它做 span 覆盖**必然带 size** | `TextStyle.h:13-16`（struct）· `:19-22`（Override） |
| **K3** | `Font` = size/family/weight 三字段，**无 italic/style 字段**；weight 粒度 = 整控件；族内无对应字面时 **GDI 合成加粗、GL 不合成**（已知分歧 C36-4） | `Font.h:16-24`（`:20-23` weight 语义） |
| **K4** | 布局引擎全程**一个 font**：`BuildTextLayout` 入口取一次 `m_style.font.value`，5 参 `FitText` 也喂它 | `TextWidget.cpp:302` · `:379` |
| **K5** | 绘制 = **每行一条命令、单 font 单色**（wrap 分支）；非 wrap = **整串一条**命令（★ 不是每 run 一条——旧实现命令数 = 行数或 1） | `TextWidget.cpp:522`（wrap 逐行 DrawText）· `:534-540`（非 wrap） |
| **K6** | `TextMeasurer` 接口全单 Font：`MeasureText(const Font&, …)` / `FitText` 两重载 / `LineHeight(const Font&)` | `TextMeasurer.h:52` · `:70-121`（4 参）· `:138-183`（5 参）· `:207` |
| **K7** | 命令与门面都是单 font：`DrawTextCommand{pos,text,color,font}`；`PaintContext::DrawText` 同形 | `RenderCommand.h:26-32` · `PaintContext.h:38-39` |
| **K8** | 两后端「一条命令一个字体」：GDI `DrawTextImpl → GetOrCreateFont(font)`（`lfWeight` 消费）；GL `FaceIdFor(family, weight)` + `Ascent(font)` 定基线 | `GDIBackend.cpp:887-898` · `:1615` / `:1640` · `GLRenderer.cpp:239-254` · `GDITextMeasurer.cpp:64` |
| **K9** | 两套指纹缓存的键都含**单 font**（size/family/weight 逐字段比），**无 span 维**；`TextBox` 行表键同族（维 2-4 = font 三字段） | `TextWidget.h:218`（m_prefFont）· `:227`（m_wrapFont）· `TextWidget.cpp:444-446` / `:574-576` · `TextBox.cpp:504-506` |
| **K10** | preferred 测量也是单 font 整串一次 | `TextWidget.cpp:604-608`（DoMeasureText） |
| **K11** | 选中/复制取 `m_text` **明文**子串——与视觉样式无关 ⇒ 「复制仍取块全文」的事实基础天然成立（前提：不动编辑通路） | `TextBox.cpp:1089-1098`（CopySelectionToClipboard） |
| **K12** | FT 测量设施按 Font + per-glyph memo：`MeasureText` / `FitText` 两重载 / `LineHeight` / `AdvancePx` 全可逐 run 复用，**零新增测量原语** | `src/Render/FontEngine.h:96`（FaceIdFor）· `:121`（AdvancePx）· `:129` / `:141-153` / `:158` |
| **K13** | ★ **斜体位已被读取、但只用于排除**：Phase 36 批二已解析 `OS/2.fsSelection` bit0（consolai/consolaz 实测），当前语义 = 斜体面**穿族名过滤也出局** ⇒ 开斜体 = 把「排除」改「按位选择」，不是从零读表 | `src/Render/FontEngine.cpp:106`（字段）· `:276`（读位）· `:397-399`（排除点） |
| **K14** | `DrawTextContent` 是**基类单站点**，四个显示型控件消费 ⇒ spans 落基类即全部天然继承；`TextBox` 另有自己的样式系（隐藏基类 m_style），接入需单独做 | `Label.cpp:18` · `Button.cpp:308` · `CheckBox.cpp:107` · `Radio.cpp:116` · `TextBox.h:210-212` |
| **K15** | ★★ **行高不是 size 的单值函数**（v1.1 新增，证伪 v1.0 D44-4）：GDI `LineHeight` 经 `GetOrCreateFont(font, dpi)` 取**该 family/weight 的字面**读 `otmMac*` 行框；FT 缓存键 = `(weight, size, family, dpi)`、face = `FaceIdFor(family, weight)` ⇒ **同字号换 family/weight，行高可不同** | `GDITextMeasurer.cpp:405-488`（`:426` 取 font）· `FontEngine.cpp:616-643`（`:621` 键 / `:629` face） |
| **K16** | ★★ **基线随 face 变**（v1.1 新增）：GL 基线 = `floor(pos.y) + floor(Ascent(font))`；`Ascent` 缓存键 = `{FaceIdFor(family, weight), px}` ⇒ 同字号不同 family/weight 的 run **基线可不同**；现行布局行高单源（入口取一次、totalHeight = 行高×行数）在 spans 下失效 | `GLRenderer.cpp:254` · `FontEngine.cpp:650` / `:657` · `TextWidget.cpp:303` / `:426` |
| **K17** | ★★ **消费者主路径 = 只读 TextBox + 流式整段覆盖**（v1.1 新增，改判闸门 1/4 的依据）：harness 流式 delta 每帧 `row->label->SetText(block->text)` 单控件整段覆盖；`--selectable-rows` 下 assistant 正文行用**只读 TextBox**（可选区/复制），`RowRec.label` 类型已是共同基类 `TextWidget*`；`--select-probe` 验转录正文选中复制 | harness `rich-content-plan.md` §0.3（`:29-32`，引 `AppStream.cpp:489-497`）· `gui/src/App.h:551-557` · `README.md:266` |
| **K18** | ★★ **逐 run 复用有两个硬站点**（v1.1 新增）：① `FitText` 的 `text`/`cps`/`byteOffsets` 是**全串**契约（`startCp` 绝对索引；D40-J 两套计数语义不得混用）⇒ run 切片不是「换个串喂进去」；② `emitLine` 回退时**单 font 补测一次**定 `TextLine::width` ⇒ 漏改这站，软折行行宽与绘制 x 累积分裂 | `TextMeasurer.h:60`（全串）· `:126-135`（D40-J）· `TextWidget.cpp:331-341`（`:339` 补测 / `:341` width 入行） |

### 1.4 两条现象（本 Phase 的靶子）

| 编号 | 现象 | 证据链 | 判定 |
|---|---|---|---|
| **R88-A** | 「这半句加粗」无 API 通路：`SetFont`/`SetStyle` 作用于整控件（K1/K2），weight 虽已表达（K3）但粒度不对；harness 只能把 `**粗体**` 按原文显示 | K1-K5 + 仓内 grep 零命中 | **能力缺口**（行为自洽、内容不丢） |
| **R88-B** | 应用层绕行代价不可接受：拆多 Label ⇒ 选中/复制假设破裂 + 跨控件折行自管（CJK 重灾区）+ 探针静默重基线 | REQ-20 代价行（§1.1 逐字）+ K17（tool 体已铺完） | **绕不起** ⇒ 必须框架内做 |

### 1.5 消费者证据矩阵（v1.1 按 K17 订正）

| 消费者 | 位置 | 用法 | 本 Phase 之后 |
|---|---|---|---|
| **harness（框架外唯一真实消费者）** | 转录富渲染：块级全套应用层先行（`rich-content-plan.md` §G，**明禁拆多控件绕行**）；★ **可选中复制主路径 = 只读 TextBox**（K17），流式 delta 每帧整段 `SetText` | 目前内联标记按原文显示 | 同一 Label / **只读 TextBox** 内粗体/斜体/mono 真渲染（闸门 4-① 倾向本期收）；选中/复制仍取块全文（**口径消费时定**——§1.1 逐字括号）；流式 delta 不丢样式（闸门 1） |
| 仓内产品码 | 新 API，今日零调用 | — | 不涉及存量 |
| ModelProbe / demo / 既有探针 | 全部单字体路径 | 默认无 spans | **逐位不变**（红线 1） |

★ **v1.1 订正**：v1.0 此处写「同一 Label/TextBox 内真渲染」却在 D44-7 把 TextBox 整体排除——自相矛盾且消费者落不了地（主路径正是只读 TextBox）。现按闸门 4 拆两问（§6）：只读显示接入 vs 编辑器通路。

---

## §2 方案选型

### 2.1 候选对比

| 方案 | 内容 | 改动面 | 判定 |
|---|---|---|---|
| **A run 模型 + 多命令（采纳）** | `TextWidget` 持 span 表（码点区间 + 样式覆盖）；布局引擎逐 run 测量/折行（复用既有单 Font 接口）；绘制每 run 发一条**既有** `DrawTextCommand` | TextWidget 层 + 新公共类型头 +（斜体若收）Font/face 选择层 +（闸门 4-① 若收）只读 TextBox 显示面 | ✅ 命令流/DrawText 执行体零改动（红线 3）；测量/折行/禁则/字节表全部复用 Phase 29/40 设施 |
| **B 扩 DrawTextCommand 带 runs 数组** | 一条命令内混排，后端逐 run 画 | 命令 variant + GDI + GL + RecordingBackend 全动 | ❌ 红线 3 破；后端承接排版，违反 `RenderCommand.h:25` D9「Backend 不做排版」；RecordingBackend 既有断言全部重基线 |
| **C 应用层多 Label 拼接** | 框架不动，harness 自己拆 | 0 | ❌ 正是 REQ-20 明拒的绕行（R88-B）；且不是框架能力，立案无意义 |
| **D 独立 RichText 控件族** | 新 `RichTextWidget` 自成管线 | 新控件 + 第二套布局/绘制 | ❌ 第二套文本管线 = wrap/禁则/字节表/缓存全部复制，漂移必然（Phase 38 D38-A 已裁「复用优于重写」）；消费者要的是「同一 Label 内」 |
| **E 只记账不做** | 维持 #53 式等待 | 文档 | ❌ 重启条件已成立、用户已拍板另立项（§1.2）——再等即违裁决 |

### 2.2 为什么选 A（机理）

今日管线的每一层都是「单 font」，但**每一层的接口都已经按 Font 参数化**（K6/K7/K8/K12）——缺的从来不是测量/绘制原语，而是**「把一串文本按区间拆成多次单 font 调用」的编排层**。A 方案 = 只加这一层编排：span 表 → 布局引擎逐 run 喂既有 `FitText`/`MeasureText` → 绘制逐 run 发既有命令。禁则字符集、软断点分类、码点↔字节表、空行语义、尾部空白裁剪**全部原样继承**（它们只认码点、不认样式——K4 的状态机与样式正交）。零 span 时编排层退化为「一个 run 覆盖全串」= 今日单 font 路径，红线 1 由此结构性成立。

★ **v1.1 补机理边界**：「锁字号」并**不**使行高/基线问题消失——K15/K16 证明同字号换 family/weight 仍扰动行高与基线。「行高/基线唯一来源」是初设必答项（O44-1），不是方案 A 自带的免费结论。

---

## §3 核心设计决策（v1.1 重订；裁决见 §6）

### D44-1 · span 表示与 API 形状（v1.1 改判候选；闸门 1 重裁）

新公共类型 `TextSpan`（码点区间 `[startCp, startCp+cpCount)` + 样式覆盖），区间**不重叠、按 startCp 有序**；未被覆盖的码点 = 基类 `m_style` 默认样式。

★★ **v1.0 三处自相矛盾作废**（评审 P0-3）：「SetText 换内容即清 spans」+「SetText 一字不动」+「SetText 递增 span revision」三者不可同真；且 Phase 41 刚把 `TextBox::SetText` override 收敛为基类子集——改基类 SetText 即全派生受影响。更重的消费侧事实：**流式 delta 每帧 `SetText(block->text)`**（K17）⇒「清 spans」会让每个 delta 丢样式、逼消费者每帧重解析后补设。**「SetText 清 spans」方案拒**。

闸门 1 改判两候选：
- **ⓐ 原子入口 `SetStyledText(text, spans)`**：文本与样式表**同一次调用、同一身份**；`SetText` 本体零改动（红线 1 结构性最强）；流式侧把 `SetText` 调用点换成 `SetStyledText`（每帧一次原子替换）。
- **ⓑ 增量 `SetTextSpans` + 串身份失效**：`SetText` 不清 spans，span 表有效性与**串身份**自比——键命中但串身份不符 ⇒ 不复用（Phase 40 `m_cpByteTextSize` 串身份自检是现成先例，`TextWidget.cpp` `EnsureCpByteOffsets`）；越界区间按契约退化。流式代码零改动，但需防「旧 spans 误贴新串」的窗口期语义。

**覆盖字段集在本闸门一并钉死**（评审 P1-9）：若闸门 3 取锁字号，覆盖类型**结构上排除 size**（独立 `SpanStyle` 类型，**不复用** `TextStyleOverride`——K2 的整值 `optional<Font>` 必然带 size，「锁字号」会退化成口头约定而非结构约束）；或钉死 size 传入时的显式退化语义。

### D44-2 · 样式维度与斜体涟漪（v1.1 降级为「勘察后裁」；闸门 2）

v1 必收：**weight（粗体）/ family（mono）/ color** 覆盖——今日 `Font`/`TextStyle` 已全表达，spans 只降粒度，两链零新增机制。**斜体**是唯一需要新表达力的维度（K3），K13 证明读表设施已在，但 **v1.0 低估了涟漪**（评审 P1-5）。已亲核的站点清单（≥8 类）：

| 类 | 站点（对 HEAD 亲核） |
|---|---|
| `FaceIdFor` 签名 + 调用 | 定义 `FontEngine.cpp:286`；调用 `:427` / `:468` / `:517` / `:580` / `:629` / `:650` / `:657` / `:667` / `:680` / `:699` + `GLRenderer.cpp:240`（共 11 处调用） |
| 缓存键 ×4 族 | `GDIBackend.cpp:1628`（HFONT 键）· `GDITextMeasurer.cpp:52` / `:98`（font/measure 双键）· `FontEngine.cpp:459` / `:621` / `:650`（measure/lineHeight/ascent 三键） |
| 指纹比较 ×3 站点 | `TextWidget.cpp:444-446`（wrap）/ `:574-576`（preferred）· `TextBox.cpp:504-506`（行表键维 2-4） |
| `Font` 聚合初始化（C7560） | `FontWeightTests.cpp:78-79`（designated 初始化先例；新字段声明序与全库初始化面需普查） |
| GDI 字体创建 | `GDIBackend.cpp:1615` / `GDITextMeasurer.cpp:64`（lfItalic 落点） |

★ **设计推论（未实现，需勘察实测）**：GDI `lfItalic=TRUE` 在无斜体面时**合成斜切**，GL 侧按 C36-4 同族政策**只选真斜体面、不合成** ⇒ 斜体会引入**新的两链分歧**——v1.0 的 T44-9/L44-5 只规避了粗体分歧，没覆盖这条。**闸门 2 = ⓐ 暂缓**：先跑 O44-2 涟漪勘察（站点实测 + 合成歧义探针），再裁收/记账。

### D44-3 · 测量与折行（v1.1 补三个必答站点；TextMeasurer 接口 +0 是约束不是结论）

布局引擎在既有状态机（`TextWidget.cpp:298-428`）内逐 run 化，但**不是「换个串喂进去」**（评审 P1-8，K18）：

1. **FitText 全串契约**（`TextMeasurer.h:60` / `:126-135`）：`text`/`cps`/`byteOffsets` 是全串、`startCp` 绝对索引、D40-J 两套计数语义不得混用 ⇒ run 切片要么**重建局部表 + 剩余宽度预算 + fitCp 映射回全局码点**，要么改「全串行走 + 逐码点查所属 run 字体」——两形状初设对比钉死（O44-4）。
2. **emitLine 回退补测站**（`TextWidget.cpp:331-341`）：回退时单 font `MeasureText` 补测定 `TextLine::width`（`:339`/`:341`）⇒ 必须改逐 run 求和，否则**恰在软折行行**出现「width ≠ 逐 run 绘制 x 累积」，C44-2 直接破（T44-2 是唯一覆盖相）。
3. **per-run 宽存哪**：`TextLine` 现 4 字段（`TextWidget.h:118-126`）——加字段还是侧表，初设钉死（声明序 C7560 同族注意）。

软断点 / 禁则 / 空格消费 / 空行语义**一字不改**（只认码点不认样式）。`TextMeasurer` 公共接口**零增量**仍是硬约束（自定义 measurer / 测试派生类零改动，D40-G 同族）。

### D44-4 · 字号与行高/基线（v1.1 证伪重写；闸门 3）

★★ **v1.0 的「锁同字号 ⇒ 行高/基线/光标高问题整体消失」被源码证伪**（评审 P0-1）：`LineHeight` 取 **face 的 hhea 行框**（K15：GDI 经 font 实例取 `otmMac*`；FT 键明列 family/weight），`Ascent` 键含 `FaceIdFor`（K16）⇒ span 必收 family/weight，**同字号下逐 run 行高与基线仍可能不同**；`TextWidget.cpp:303`/`:426` 的单 font 行高口径在 spans 下失效。

- 闸门 3 的 **ⓑ 锁同字号倾向可保留**，但理由改写为：「REQ-20 三样式全同字号场景，锁 size 只消掉**字号差**这一维」——**不**声称行高/基线问题消失。
- ★★ **「行高/基线唯一来源」（基字体 vs 逐 run max vs 其他）升级为初设必答项 O44-1**，两链同口径；光标高联动（只读 TextBox 若显示光标）一并答。
- T44-1 的「高 = 行高」判据**重写**：高度断言以 O44-1 钉死的口径为准，钉死前不写死断言。

### D44-5 · 缓存指纹补 span 维（v1.1 随闸门 1 形状联动）

preferred 指纹与 wrap 指纹**各补一维 span revision**；命中判据其余维不变。基字体比较仍逐字段（spans 身份由 revision 承载，不展开进键）。失效语义沿 `SetWordWrap` 双缓存同置先例（`TextWidget.cpp:113-128`）。★ **v1.1 订正**：revision 的递增源随闸门 1 形状——ⓐ 原子入口一次递增文本+span 双身份；ⓑ `SetText` 只递增文本 revision（其既有行为），span 表有效性另靠**串身份自检**（K17 流式场景：同键不同串 ⇒ 不复用，Phase 40 D40-L-3 同族）。v1.0「SetText 递增 span revision + 清 spans」表述作废。

### D44-6 · 绘制发射（每 run 一条既有命令）

wrap 分支：每行区间 ∩ span 边界 ⇒ 逐 run 切片，x = 行起点 + 前序 run 宽累积（**与 D44-3② 的 width 同源**，不二次测），每 run 一条 `ctx.DrawText`；非 wrap 分支同构拆整串。空 run 零命令；零 spans ⇒ 与今日同一条命令（红线 1）。命令**数量**只在有 spans 时从 1（或行数）变 N，命令**类型/字段**零变化（红线 3）。UTF-8 切片走 Phase 40 字节表（`ByteOffsetAt` 钳制语义继承）。

### D44-7 · 范围边界（v1.1 拆两问；闸门 4）

spans 落 `TextWidget` 基类 ⇒ Label/Button/CheckBox/Radio 四个显示型控件**天然继承**（K14，各自零改动）。`TextBox` 拆两问：

- **闸门 4-① 只读 TextBox 显示接入——倾向本期收**（评审 P0-2 改判）：消费者可选中复制主路径正是只读 TextBox（K17），§G 明禁拆多控件，排除它 = 能力落地即悬空、REQ-20 判据③无处兑现。只读显示**不需要**光标编辑/IME/Undo 全通路；选中/复制走既有 `m_text` 明文通路（K11，与样式正交）。勘察项 = TextBox 自有绘制路径（隐藏 m_style，K14）× spans 的交互面（O44-3）。
- **闸门 4-② 编辑器通路（可编辑 TextBox × spans）——仍记 L44-1**：光标/选区/IME/Undo 全通路第二工程，沿 Phase 29「编辑器 wrap 排除在 v1、Phase 38 才开」先例；重启条件 = 真实编辑场景消费需求。

---

## §4 详细规范与行为契约

### 4.1 改动清单（预估，初设可订；★ 含落地锚点续订义务）

| 文件 | 改动 | 预估 |
|---|---|---|
| `ECDI/include/ECDI/Widget/TextSpan.h`（新） | `TextSpan` + 覆盖类型（字段集随闸门 1/3 钉死，D44-1）+ 契约注释 | +40 ~ +60 |
| `ECDI/include/ECDI/Widget/TextWidget.h` | span 表成员 + 公共入口（形状随闸门 1）+ 指纹维 | +25 ~ +35 |
| `ECDI/src/Widget/TextWidget.cpp` | 布局状态机逐 run 化（D44-3 三站点）+ 绘制逐 run 发射（D44-6）+ preferred 逐 run 求和 + 指纹补维 + 行高/基线口径（O44-1） | +140 ~ +200 |
| `ECDI/src/Widget/TextBox.cpp`（**仅闸门 4-① 收时**） | 只读显示路径接 spans（O44-3 勘察后定面）；编辑通路零改动 | 勘察后估 |
| `ECDI/include/ECDI/Core/Font.h` + face 选择层（**仅闸门 2 勘察后裁收时**） | italic 字段 + D44-2 涟漪清单全站点（红线 3 显式豁免面） | 勘察后估 |
| `ECDI/src/Tests/`（文本域测试文件） | +9 用例起（T44-1..9，含 2 负对照；闸门 4-① 收则加只读 TextBox 相，初设钉总数） | +220 ~ +320 |
| `CMakeLists.txt` | **0**（GLOB_RECURSE 自动入库） | 0 |
| **落地锚点续订义务**（v1.1 补，P2） | 实施收口时：用例 **588→597**（或初设钉死值）同步 `README.md:64` / `:91` 与 `docs/dev/README.md:24`；公共头 **101→102** 同步同三处（L13 zone）；`docs/dev/README.md`「🔄 当前」区迁「✅ 已完成」 | 文档 |

### 4.2 行为契约表（改后）

| 形态 | preferred | wrap 布局 | 命令流 | 选中/复制 |
|---|---|---|---|---|
| 零 spans（未设 / 清空） | 逐位不变 | 逐位不变 | 逐位不变（每行/整串一条） | 不变 |
| 有 spans · 单行放得下 | 宽 = Σ run 宽；高 = **O44-1 钉死的行高口径** | 1 行 | 每 run 一条（font/color 逐 run） | 不变（明文通路 K11） |
| 有 spans · 需折行 | {GetWidth(), 行数×行高口径} | 断点按混排测宽 + 既有禁则 | 每行逐 run | 同上 |
| 有 spans · 流式 delta 整段覆盖（K17） | 随闸门 1 形状：ⓐ 原子替换 / ⓑ 串身份失效 ⇒ 不丢样式、不误贴旧表 | 同左 | 同左 | 不变 |
| spans 越界 / 空表 / 全覆盖 | 越界钳制或忽略（初设钉死，**不得静默错切**）；空表 = 零 spans 行；全覆盖 = 纯 run 路径 | 同左 | 同左 | 不变 |
| 只读 TextBox（闸门 4-① 收时） | 沿 TextBox 既有 preferred 通路 + spans | 沿 Phase 38 行表 + spans | 逐 run | **取块全文**（口径消费时定——§1.1） |

### 4.3 边界与不变式

| # | 不变式 | 说明 |
|---|---|---|
| **C44-1** | 零 spans ⇒ 命令流 / preferred / 布局 / 缓存行为**逐位等价** | 红线 1；T44-6 钉住；既有文本域用例一行不改仍绿 |
| **C44-2** | run 宽「测」与「画」同源，**含 emitLine 回退补测站**（K18②） | 红线 2；C29-1 的逐 run 推广；T44-1/2/3 联合钉住 |
| **C44-3** | span 边界与断行边界恒在**码点**边界（UTF-8 序列 / surrogate pair 不可拆） | C29-1/C29-2 继承；T44-5 脏字节样本 |
| **C44-4** | 断行确定性：禁则 / 软断点字符集 / 空格消费语义**零变化**，无 locale/NLS | Phase 29 红线继承；样式不参与断点分类 |
| **C44-5** | 命令流只出现**既有** `DrawTextCommand`（variant 成员零增删、字段零变化） | 红线 3；RecordingBackend 既有设施直接可断言 |
| **C44-6** | 指纹完备：span 表任一变化 ⇒ 两缓存 miss 重算；命中 ⇒ 零 measurer 调用 | D44-5；T44-4 计数 measurer 钉住 |
| **C44-7** | （v1.1 新增）**串身份自检**：span 表键命中但串身份不符 ⇒ 不复用（debug 断言 / release 重建，Phase 40 D40-L-3 同族） | 闸门 1-ⓑ 的护栏；流式 delta（K17）不得误贴旧 spans |

---

## §5 影响面与规模预算

### 5.1 受影响面

| 面 | 影响 |
|---|---|
| 渲染命令 / DrawText 执行体 | **零**（红线 3；斜体若收，动的是 face 选择层——红线 3 显式豁免面，不是 DrawText 执行体） |
| 布局 / 失效管线 | 指纹 +1 维；不新增布局脏源（spans 不改几何，沿 Phase 41 D41-5 口径；`Widget.h` 总纲「文本不改几何、撑开须显式 AutoSize」，D44-5 已补 preferred 指纹维 ⇒ AutoSize 不读旧缓存） |
| 公共 API / 公共头 | **+1 头**（TextSpan.h）· 入口数随闸门 1（ⓐ +1 原子入口 / ⓑ +3 增量入口）——能力新增 Phase，增量显式预算 |
| 既有用例 | **0 条改动**（红线 1） |
| 下游消费工程 | harness 块级全套不受影响先行；内联消费在落地后做（只读 TextBox 主路径随闸门 4-①），**当前不预做** |

### 5.2 规模预算表

| 维度 | 预算 | 判定标准 |
|---|---|---|
| 公共头 | **101 → 102** | +TextSpan.h（斜体收否均在既有头内改） |
| 公共 API 增量 | 闸门 1-ⓐ：**+1 方法 +1 类型** / ⓑ：**+3 方法 +1 类型** | 初设钉死 |
| 既有用例 | **588 / 588 保持全绿** | 存量零破坏 |
| 新增用例 | **+9 起**（588 → 597 起；闸门 4-① 收则加相，初设钉总数） | 含 2 条负对照 |
| 设计文档 | **213 → 214**（已兑现） | 本稿 |
| `CMakeLists.txt` | **0** | GLOB |

### 5.3 新增测试规划（含负对照；v1.1 订正 T44-1/8/9）

| # | 用例 | 判据（无头可观测优先；两链项注明） |
|---|---|---|
| **T44-1** | 三段混排测量：同一 Label「常规 + 粗体 + mono」三 run | preferred 宽 = Σ 各 run 测宽（计数/假测量器可逐 run 核对）· ★ **高度断言以 O44-1 钉死的行高口径为准**（v1.1：v1.0「高 = 行高」被 K15/K16 证伪——同字号换 family/weight 行高可不同，钉死前不写死断言）· 单 run 控件与今日 SetFont 路径测值一致 |
| **T44-2** | 混排折行：wrap 宽恰落在粗体 run 内部 | 行数 / 逐行 startCp / cpCount / width 按逐 run 累积断言；★ **必含软断点回退相**（守 emitLine 补测站，K18②——此相是 C44-2 在软折行行的唯一覆盖）；断点不落 UTF-8 中间；禁则样本与单字体版同构 |
| **T44-3** | 逐 run 命令流：RecordingBackend 收命令 | 每 run 恰一条 DrawTextCommand · font/color 逐 run 正确 · x 累积 = 前序 run 测宽和（C44-2）· 命令类型零新增（C44-5） |
| **T44-4** | 指纹补维：span 入口调用 / 清空 / 流式整段覆盖后 miss | 计数 measurer：变更后首次 GetPreferredSize/绘制 ⇒ 重测；二次命中 ⇒ 零调用（C44-6）；★ 闸门 1-ⓑ 时加「同键不同串 ⇒ 不复用」相（C44-7） |
| **T44-5** | 脏边界：CJK + surrogate pair + 非法字节样本上的 span 边界 | 边界恒码点口径（C44-3）；越界区间按 4.2 退化走，不静默错切；`"A\xC0"` 族样本字节表钳制语义不回归（Phase 40 T40-1 同族） |
| **T44-6** | 零 spans 等价护栏 | 同一 Label「设空表 / 未设」与今日路径：命令流逐字段相等 · preferred 相等 · wrap 布局相等（C44-1） |
| **T44-7** | **负对照①（判别力，REQ-20 判据②）**：回退粗体 run 的 weight 为 400 | 恰 T44-1/3 的粗体断言红、其余绿——证明「逐段样式生效」不是恒真断言（施工期臂，入库形式初设钉） |
| **T44-8** | **负对照②（入库护栏，v1.1 机理订正）**：断言「**命令数 = run 数（3）∧ font 不全相等**」 | ★ v1.0 机理写错：旧实现非 wrap **1 条**命令、wrap **每行 1 条**（K5），根本不会有 3 条——「三条命令 font 恒相等」不是旧行为。订正后：命令数断言抓「未拆 run」（旧实现 1 条 ⇒ 红），font 断言抓「拆了 run 但共用基字体」的退化施工臂；对照臂 = 施工期退化成每 run 一条但共用基字体 |
| **T44-9** | 两链一致（REQ-20 判据①，真窗口/无头 GL 探针）：GDI vs GL 同夹具 | 逐 run 宽 / 断行位置两链一致，容差沿既有口径（LineHeight ≤1px 先例，Phase 31）；粗体样本用**有真粗体面**的族避开 C36-4（斜体若收，其合成歧义另需探针——D44-2 推论）。★ **v1.1 补探针口径**：无头 GL 读数 = `glReadPixels` 在 `SwapBuffers` **之前** + 同位置 GDI 对照；记录断言启用（`-D_DEBUG`）与显示器缩放（本机 125% 先例） |
| （闸门 4-① 收时） | 只读 TextBox 显示相：spans 渲染 + 选中/复制取块全文 | 复制内容 = 明文区间（K11 口径）；样式不入剪贴板；具体断言随 O44-3 勘察钉 |

### 5.4 REQ-20 验收映射（v1.1 随闸门 4 拆分订正）

| REQ-20 判据 | 本稿落点 |
|---|---|
| ① 三段混排测量/折行正确、逐段生效、两链一致 | T44-1/2/3/9 |
| ② 回退任一段样式 ⇒ 恰该用例红 | T44-7（+ T44-8 护栏） |
| ③ 消费后选中/复制仍取块全文**（口径消费时定）** | ★ v1.1：闸门 4-① 收只读 TextBox ⇒ **本期即可兑现**（K17 主路径 + K11 明文通路）；口径细节消费时定；编辑器通路场景仍顺延 L44-1 |
| ④ 既有单字体路径零回归 | 红线 1 + C44-1 + T44-6 + 588 存量全绿 |

---

## §6 闸门（v1.1 重订；裁决权在用户）与勘察前置

### 6.1 四道闸门

- [ ] **闸门 1 · API 形状（重裁）**：ⓐ 原子 `SetStyledText(text, spans)` vs ⓑ 增量 `SetTextSpans` + 串身份失效（不清 spans）。**「SetText 清 spans」已拒**（K17 流式 delta 每帧丢样式）。倾向 **ⓐ**：身份最显式、SetText 本体零改动（红线 1 结构性最强）、防旧 spans 误贴新串；ⓑ 的优势 = 流式代码零改动，代价 = 串身份自检机制 + 窗口期语义。★ **覆盖字段集（排除 size 与否，联动闸门 3）在本闸门一并钉死**（D44-1）。
- [ ] **闸门 2 · 斜体（暂缓，勘察前置）**：ⓐ 收 vs ⓑ 记账后补——**先跑 O44-2 涟漪勘察再裁**（≥8 类站点已列 D44-2；GDI 合成 vs GL 真面两链歧义需探针实测）。v1.0「倾向收、超预算才退」缺勘察支撑，作废。
- [ ] **闸门 3 · 混字号**：ⓑ v1 锁同字号（倾向保留，**理由已重写**——只消字号差一维，不声称行高/基线问题消失，D44-4）vs ⓐ 收（行高 max + 基线对齐）。★ 无论取哪边，**「行高/基线唯一来源」= O44-1 初设必答**（K15/K16：同字号 family/weight 差仍扰动）。
- [ ] **闸门 4 · TextBox（拆两问）**：**4-① 只读显示接入**——倾向**本期收**（消费者主路径，K17；排除即落地悬空）；**4-② 编辑器通路**——倾向仍排除（L44-1，Phase 29 先例）。前置事实：判据③「口径消费时定」（§1.1 逐字括号）。

### 6.2 勘察项（进初设的闸门；条 74：状态由评审结论决定）

| # | 勘察题 | 出处 |
|---|---|---|
| **O44-1** | **行高/基线唯一来源**：基字体 vs 逐 run max vs 其他；两链同口径；只读 TextBox 光标高联动；T44-1 高度断言随之钉死 | P0-1（K15/K16） |
| **O44-2** | **斜体涟漪实测**：D44-2 站点清单逐处确认 + `Font` 初始化面普查（C7560）+ GDI lfItalic 合成 vs GL 真斜体面探针（新 C36-4 同族分歧定量） | P1-5 |
| **O44-3** | **只读 TextBox 显示路径 × spans 交互面**：自有绘制（隐藏 m_style，K14）/ Phase 38 行表 10 维键 / 选中高亮与 run 边界；改动面估算 | P0-2 |
| **O44-4** | **逐 run 测量形状**：FitText 切片局部表 vs 全串逐码点查字体（K18①）；emitLine 回退站改法（K18②）；per-run 宽存储（TextLine 加字段 vs 侧表） | P1-8 |

---

## §7 记账与被拒项

| # | 项 | 处置 |
|---|---|---|
| **L44-1** | **编辑器通路**（可编辑 TextBox 光标/选区/IME/Undo × spans） | 本期不做（D44-7 / 闸门 4-②）。★ v1.1 收窄：只读显示接入已拆给闸门 4-①，本条只剩编辑通路。**重启条件** = 真实编辑场景消费需求；届时沿 Phase 38 先例（protected 面复用，不重写管线） |
| **L44-2** | 混字号（行高 max + 基线对齐） | 若闸门 3 取 ⓑ 则记账；重启条件 = 真实混字号消费需求。★ 注意与 O44-1 不同：O44-1 是**同字号下** family/weight 差的行高/基线口径（本期必答），本条是**字号差**（可后置） |
| **L44-3** | 行内 code 的**背景底片** | v1 的 code 样式 = mono family + 颜色；逐 run 背景无命令原语（DrawTextCommand 背景透明，`RenderCommand.h:30`），不扩命令（红线 3）⇒ 记账，归 #53 群口径重估 |
| **L44-4** | 下划线 / 删除线 / 链接色语义 | 不做；装饰原语属 #53 群（渲染表现力），按 #88 立项时的口径澄清重估 |
| **L44-5** | C36-4 已知分歧继承到 spans | 粗体：GDI 合成 / GL 不合成，T44-9 用真粗体面族规避。★ **v1.1 扩**：斜体若收，**合成斜切 vs 真斜体面**是新同族分歧（D44-2 推论）——O44-2 探测定量后再定规避/修复口径 |
| **L44-6** | harness 账本 REQ-20 状态续写 | 闸门裁决后回写「Phase 44 · 需求稿 v1.1 · 闸门结果 + 勘察结论」；**不代改**其验收口径（判据③「口径消费时定」逐字保留） |
| **L44-7** | （v1.1 新增）根 `README.md` Status 表 **Phase 43 行缺失** | **存量欠账**（表停在 Phase 42，43 收口时未登记）——不记本稿新账；本轮只补 Phase 44 行，43 行另行补齐 |

---

## §8 修订记录

- **v1.1（2026-11-07）★ 评审吸收（P0×3 / P1×7 / P2×3 全收）；状态 = 待事实勘察 / 闸门待重裁，不进初设**：
  ① **P0-1**：D44-4「锁字号 ⇒ 行高/基线问题消失」被 K15/K16 证伪（LineHeight 键含 family/weight、Ascent 键含 FaceIdFor）⇒ 理由重写、「行高/基线唯一来源」升 O44-1 初设必答、T44-1 高度判据重写。
  ② **P0-2**：TextBox 拆两问——只读显示接入（闸门 4-① 倾向收，K17 消费者主路径）+ 编辑器通路（L44-1）；§1.5 / §5.4 / 红线 4 同步；新增 K17 与 O44-3。
  ③ **P0-3**：「SetText 清 spans」+「一字不动」+「递增 revision」三矛盾作废；闸门 1 改判 ⓐ 原子 / ⓑ 串身份失效两候选（K17 流式 delta 为改判依据）；D44-5 联动重写；新增 C44-7。
  ④ **P1-4**：红线 3 收窄为「命令流 + DrawText 执行体」，face 选择层显式豁免（消 v1.0 红线 × §4.1 自相矛盾）。
  ⑤ **P1-5**：斜体涟漪 ≥8 类站点亲核清单入 D44-2；闸门 2 降为「勘察后裁」；GDI 合成 vs GL 真面新分歧推论入 L44-5 / O44-2。
  ⑥ **P1-6（条 114）**：总账引用改行编号（#88/#53 行）不用行号——同批 v2.82 插入已漂移（:184→:185 / :198→:199），HEAD 总账仍 v2.80；「全部行号对 HEAD 亲核」限定为源码与已提交文档。
  ⑦ **P1-7（条 79）**：§1.1 改逐字引用——恢复判据③「（口径消费时定）」与代价行 REQ-15 教训 / tool 体说明；该括号成为闸门 4-① 前置事实。
  ⑧ **P1-8**：D44-3 补三必答站点（FitText 全串契约 / emitLine 回退补测站 / per-run 宽存储）= K18 + O44-4；C44-2 强化；T44-2 加软断点回退相。
  ⑨ **P1-9**：覆盖字段集在闸门 1 钉死——不复用 TextStyleOverride（K2 整值 optional<Font> 必带 size），锁字号须是结构约束。
  ⑩ **P1-10（条 76/119③）**：T44-8 机理订正——旧实现非 wrap 1 条 / wrap 每行 1 条（K5），不存在「三条命令」；判据改「命令数 = run 数 ∧ font 不全相等」，对照臂 = 拆 run 但共用基字体。
  ⑪ **P2×3**：根 README Status 表补 Phase 44 行（43 缺失 = 存量欠账 L44-7）；§4.1 补落地锚点续订义务（588→597 / 101→102 同步 README.md:64/:91 + docs/dev/README.md:24）；T44-9 补无头 GL 探针口径（glReadPixels 在 SwapBuffers 前 + 同位置 GDI 对照 + 记录 -D_DEBUG 与缩放）。
  ⑫ **门禁机制订正**：untracked 新稿不在 doc_lint 扫描集（git ls-files）⇒ 先 git add 再门禁；列举式 `--select L04,L09,L11,L13 --quiet` + 阳性对照后才提交。
  ⑬ **保留项（评审核实成立，未动）**：K1–K14 全部行号 · 方案 A 机理与 D9 · 红线 1 结构性等价 · §5.1 不新增布局脏源（D41-5 + AutoSize 不读旧缓存）· 模板同构 Phase 43 · 数字锚点与 BOM。
- **v1.0（2026-11-07）** 初稿：事实矩阵 K1–K14 · 现象 R88-A/B · 方案 A–E 对比 · 决策 D44-1..7 · 契约 C44-1..6 · 测试 T44-1..9 · 闸门 4 项 · 记账 L44-1..6 · REQ-20 四判据映射。★ **评审结论：不建议按现状拍板进初设**（P0×3 / P1×7 / P2×3，见 v1.1 逐条吸收）。

---

> 需求稿 v1.1 评审吸收完成（2026-11-07）：**待事实勘察（O44-1..4）/ 闸门待重裁（1/2/4）**——勘察与裁决完成前不进初设（条 74）。