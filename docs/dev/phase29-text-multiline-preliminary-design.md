# Phase 29 · 文本多行能力 —— 初步设计（v1.1）

> 来源：需求稿 `phase29-text-multiline-requirements.md` **v1.1（评审 PASS → 初设）**——评审四关键语义问题（宽度来源 / AutoSize 循环 / TextLayout 共享 / Fit API 三方案）+ 四红线四强约束逐题兑现。
> 状态：**v1.1**（2026-10-06）——**✅ 评审通过（PASS → Detailed Design，「比较明确的 PASS」）**（外部评审 2026-10-06：15 项状态表全 ✅——上轮六 🟡 全部闭环；详设钉死清单 **①–⑦**：断行状态机与规则优先级 / 禁则微超上界 / FitText 精确契约 / GDI 代理对边界 / 连续换行空行语义 / FT O(n) 实现约束 / 默认 FitText = 兼容 fallback 定位；另建议详设补 5 个边界测试——采纳，用例预算 341 → **346**；吸收明细见 §7 v1.1）
> 定位：**方案冻结稿**——wrap 宽度语义、AutoSize 语义、共享 TextLayout 架构、断行算法、测量原语三方案比较（裁决 = 方案 A）落成逐文件方案与精确批次。

---

## 1. 详设前置必答（需求评审关键语义问题逐题钉死）

### D29-A ★ wrap 宽度语义（关键问题①——需求 D6）

- **wrap 宽度 = `GetWidth()`**（TextWidget 当前内容区域宽度）。★ 事实核验：`TextStyle = {foreground, font}`（`TextStyle.h:13-16`）——**TextWidget 层无 padding 字段**（DefaultTheme 的 padding 属 TextBoxStyle 域，`DefaultTheme.cpp:36`），评审「maxWidth = widget.width」的预期成立。
- **拒绝独立 wrap-width 公共状态**：无 `SetWrapWidth`——宽度只有 widget.width 一个真相源（红线：防双宽度竞争）。
- 宽度 ≤ 0（从未 SetSize）⇒ **无有限宽度** ⇒ 退化单行（D29-B）。

### D29-B ★ AutoSize 语义（关键问题②——需求 D5）

- **wrap=true + width > 0**：preferred = `{ width: GetWidth(), height: lines × LineHeight }`——AutoSize 只调高（宽 no-op）。
- **wrap=true + width ≤ 0**：**退化单行**——preferred = 无约束单行测量（wrap 不凭空制造宽度约束 ⇒ AutoSize 循环消解，评审 §4 case 2）。
- **宽度动态**：`SetSize` 变宽 ⇒ 下次 `GetPreferredSize` 指纹失配 ⇒ 重排（行数/preferred 同步变——评审 §20 直测场景，T29-3）。

### D29-C ★★★ 共享 TextLayout 架构（关键问题③——需求 D7，评审唯一「强烈建议」架构点）

- **内部布局结果**（TextWidget 私有嵌套 + mutable 缓存）：
  ```cpp
  struct TextLine { std::size_t startCp; std::size_t cpCount; float width; };
  struct TextLayout { bool valid = false; std::vector<TextLine> lines;
                      float lineHeight = 0; float totalHeight = 0; };
  mutable TextLayout m_wrapLayout;
  ```
- **双消费端**：`GetPreferredSize`（wrap=true 分支读 `totalHeight`）与 `DrawTextContent`（wrap=true 分支逐行 `DrawText`）——**一份布局结果，禁止两次 Wrap**（评审 §9/§21——防「preferred 5 行 / 实绘 6 行」）。
- **行内容 UTF-8 切片只在绘制时生成**（评审 §18）：布局阶段行 = 码点 `[startCp, cpCount)` 区间（零 substr）；绘制时经共享 util `Core/UTF8.cpp` 的 `CodepointIndexToByteOffset`（`UTF8.cpp:35`——**既有共享 util，TextBox 同款**）切片。
- **红线①的结构保证**：`wrap == false` ⇒ 走 Phase 28 原路径（指纹缓存 + 单条 DrawText），**不触碰** `m_wrapLayout`（两个独立缓存、两个绘制分支——评审 §16/§17 字面兑现）。
- 指纹键（wrap=true 路径）：`(m_textRevision, font.size, font.family, dpi, GetWidth())`——宽度动态进键（评审 §20）。
- ★ **v1.1 评审钉死（详设必答⑤ + §19）**：① **连续 `\n` = 空 TextLine**（评审 §18——`A\n\nB` = **3 行**，空行占行高**不发 DrawText 命令**；`lines.size()` 与用户文本语义一致）；② **垂直定位**（评审 §19）——text block height = `totalHeight`（**非 widget height**）；区块在控件内垂直居中（沿现有 `CalculateTextPosition` 语义——`TextWidget.cpp:107-115` 已勘察，详设正式引用）；③ 行粒度（评审 §15 认可）= 每行一条 DrawText，禁多行合并扩张 RenderCommand 语义。

### D29-D ★★ 断行算法（D1——初设第一优先级；评审 §5–§8 细化兑现）

**贪心单遍 + 码点分类 + 禁则微规则**（确定性——字面常量表，禁 locale/NLS）：

- **软断点**（宽度超限时优先回退到的位置）：① 空格后（**分隔空格消费**——不进任何一行；连续空格整体消费）；② 连字符后（**连字符保留前行**——"super-" / "long-word"）；③ CJK 主体字符后。
- **禁则微规则**（v1 最小字面集，评审 §8「挑少量最明显标点规则」）：**行首禁则** = 闭括号/闭引号/句读（`」』）】》，。！？；：`）不得起行 ⇒ 断点回退并入前行（允许行宽微超——悬挂标点）；**行尾禁则** = 开括号/开引号（`「『（【《`）不得收行 ⇒ 推入下行。
- **超长词回退**：软断点窗口内无可用断点 ⇒ 按 fit 硬断（按字）。
- **显式 `\n` = 硬断**（不属任何一行；硬断后的行首空白**保留**——原文语义）。
- **宽度比较口径**：float 严格 `>`（与测量同源——不引入第二量化，Phase 27/28 教训）。
- **复杂度**：FT 链整体 O(n)（每字形经 Phase 28 advance memo 累积一次）；GDI 链每行一次原生 fit（§D29-E）——**禁隐式 O(n²)**（布局阶段零 substr/零重复全串扫描）。
- ★ **v1.1 评审钉死（详设必答①②⑥）**：① **断行状态机**（评审 §8——详设必须以状态机写死**规则命中冲突时的优先级**：fit 硬边界 → 窗口内最后合法软断点 → 禁则回退/前推 → 微超上限；防「空格后紧接闭括号」「连字符后紧接闭括号」类双规则歧义——两个实现者可各自写出"都符合文档"的版本）；② **禁则微超上界 = 单个禁则码点自身的 advance**（评审 §9——禁则回退并入前行后**不得继续吸收后续普通字符**，防 maxWidth=100 / 行宽 180 的怪异结果）；③ **FT O(n) 实现约束**（评审 §15——一次逻辑行的扫描状态由 WrapEngine 持有，`FitText` 只负责当前行的硬宽度 fit；**soft-break 回退不得导致重复扫描已处理码点**——否则 O(n) 保证被实现细节破坏）。

### D29-E ★★ 测量原语三方案比较（关键问题④——需求 D3 ⇒ **裁决方案 A**）

| 方案 | 形态 | 优 | 劣 | 裁决 |
|---|---|---|---|---|
| **A** | `TextMeasurer::FitText` 虚函数（**带默认实现** = MeasureText 二分） | GDI 原生 fit 单调用（批三产品码先例）；FT 覆写 = memo O(行) 累积；**带默认体 ⇒ 既有子类/测量探针零破坏**（默认体走 `MeasureText` 虚分派——harness MeasureProbe 计数继续工作） | 公共 API +1 | **✅ 采纳** |
| B | TextMeasurer 内部逐 glyph advance 查询 | 统一 primitive | **破坏 TextMeasurer 抽象**（「测量文本」→「测量字形」——评审 §11 的核心顾虑）；FT 内部件上提 | ✗ |
| C | 独立内部 WrapEngine 不扩公共 API | 零 API | TextWidget 必须**链感知**（GDI fit vs FT 累积分支）⇒ 违反分层（Widget 禁知后端差异） | ✗ |

- **裁决 = 方案 A**：`virtual TextFit FitText(const Font&, const std::string& text, std::size_t startCp, float maxWidth) const;`——`struct TextFit { std::size_t fitCp; float width; }`（**返回码点数**——评审 §12；`m_lineStarts` 码点索引同源）。**默认实现** = 共享 util 字节切片 + `MeasureText` 二分（O(log) 次调用）——任何既有 TextMeasurer 子类（含 harness MeasureProbe）**零改动可用**。
- **覆写**：GDI（`GDITextMeasurer::FitText`）= 原生 `GetTextExtentExPointW`（批三产品码先例）+ **wchar fit → 码点 fit 换算**（代理对感知——评审 §12 的 emoji/多字节边界）；FT（`FreeTypeTextMeasurer::FitText`）= `FontEngine` 增 `FitText`（advance memo 累积——O(行)，批一基建复用）。
- **API 账**：公共头 `TextMeasurer.h` 增 1 虚方法（带默认体）+ 1 值类型 ⇒ **API +1**（需求稿「+1 倾向」兑现；子类可不覆写）。
- ★ **v1.1 评审钉死（详设必答③④⑦ + §16）**：① **FitText 精确契约**（评审 §12）——`startCp` = UTF-8 解码后的逻辑码点索引；`fitCp` = **从 startCp 起实际可消费的码点数（相对数）**，非绝对结束位置（例：text = A B C D E，startCp = 2，fitCp = 2 ⇒ 消费 "C D"）；② **surrogate pair = 不可拆分单位**（评审 §13——GDI wchar fit 换算**绝不停在代理对中间**；独立边界测试 T29-FIT-2）；③ **默认二分实现 = 兼容 fallback**（评审 §14——仅保证既有自定义 TextMeasurer/测试类零改动，**非 GDI/FT 生产性能路径**；substring 构造成本须在详设记录）；④ **职责边界**（评审 §16）——**WrapEngine 管断在哪 / FitText 管从 startCp 最多放多少**；`FitText` **不理解**空格/CJK/连字符/禁则（防 API 失控）。

### D29-F ★ 指纹键扩展（D4——Phase 28 评审 §14 预告项兑现）

- **Phase 28 preferred 指纹键**（`(revision, font.size, font.family, dpi)`）→ 扩展 `(…, wrapOn, maxWidth)`：`wrap=false` ⇒ `maxWidth = 0` 哨兵（语义不变——T28-3 用例零改动兼容）；`wrap=true` ⇒ maxWidth = 当次查询的实际宽度（动态——D29-B 宽度动态的机制落点）。
- **wrap 布局缓存键**（D29-C）：`(revision, font, dpi, GetWidth())` 同构。
- `SetWordWrap` ⇒ `m_prefValid = false` + `m_wrapLayout.valid = false`（双缓存同失效）。

---

## 2. 方案（PD29-1..3）

### PD29-1 测量原语（D29-E 落地）

- `TextMeasurer.h`：+`struct TextFit` + `virtual TextFit FitText(...) const`（默认体 = 共享 util 切片 + `MeasureText` 二分）。
- `GDITextMeasurer`：覆写 = `GetTextExtentExPointW` 原生（**wchar→码点换算**：代理对感知）。
- `FreeTypeTextMeasurer` / `FontEngine`：覆写/实现 = advance memo 累积（从 `startCp` 起逐码点，贪心累计至超宽）。

### PD29-2 TextWidget wrap 路径（D29-A/B/C/F 落地）

- `SetWordWrap(bool)` / `IsWordWrap()`（**非 StyleField**——布局行为开关非主题视觉；TextStyle 动员成本高且主题无关）+ 双缓存同失效。
- `GetPreferredSize`：`wrap==false` ⇒ Phase 28 原路径（**逐字节不动**）；`wrap==true` ⇒ D29-B 语义（布局缓存驱动）。
- `DrawTextContent`：`wrap==false` ⇒ 原单条 DrawText；`wrap==true` ⇒ 布局逐行（区块垂直定位按 totalHeight，每行 y = 区块 y + i×行高，每行 x = 区块 x——左对齐；**Button+wrap 的每行居中留 #51③**——已知边界如实记）。

### PD29-3 断行引擎（D29-D 落地）

- TextWidget 私有 `BuildTextLayout(maxWidth)`：贪心单遍（软断点回退 + 禁则微规则 + 超长词硬断）——调 `FitText` per line；禁则字面集常量；`Core/UTF8.cpp` 共享 util 切片。

---

## 3. 用例（T29-1..6，335 → ~341）

- **T29-1** 拉丁 wrap 基础：真窗口 + 计数测量器（T28-3 装置沿用）——行切分（RecordingBackend 每行一条 DrawText、内容 = 该行）+ preferred = {宽, 行数×行高}。
- **T29-2** CJK/混排 + 禁则（评审 §17 明确化——**四段式断言**：输入 / 错误 / 允许 / 理想）：输入 `「你好」`；**错误** = `「` 起行；**允许** = `你`/`好` 间断；**理想** = 整串同行（宽度允许时）——文字描述改四段式防歧义。
- **T29-3** 指纹键扩展（宽度动态——评审 §20）：width 300→250→400 ⇒ 行数/preferred 同步变（miss）；不变 ⇒ 命中零调用；`SetWordWrap` 切换 ⇒ 双缓存失效。
- **T29-4** 默认关零回归（红线①）：`wrap=false` 时 `FitText`/布局构建**零调用**（调用计数）+ 既有渲染命令流不变。
- **T29-5** 退化与边界：width=0 退化单行；超长无空格词按字硬断；空串/单字符；显式 `\n` 硬断保留行首空白。
- **T29-6** 气泡端到端（A5）：固定宽面板 + wrap Label + 长文本 ⇒ 高度自适应断言（`realistic` 语料抽样）。
- **T29-FIT-1**（评审 §22）UTF-8 码点边界：ASCII/CJK/多字节混串——`fitCp` **永不落在 UTF-8 字节序列中间**。
- **T29-FIT-2**（评审 §13/§22）代理对：`A😀B`——GDI fit 换算**绝不拆代理对**（fitCp ≠ 半个码点）。
- **T29-WRAP-1**（评审 §18/§22）连续换行：`A\n\nB` = **3 行**（空行占行高、零 DrawText 命令）。
- **T29-WRAP-2**（评审 §9/§22）禁则微超：maxWidth + 闭标点——允许**单禁则码点**悬挂超宽、**禁继续吞后续普通字符**。
- **T29-WRAP-3**（评审 §22）同宽 `SetSize` **不重建布局**（cache hit path——D4/D29-C 键验证）。

---

## 4. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | PD29-1 FitText 三链（默认二分 + GDI 原生 + FT memo）+ Fit 单测（码点边界/代理对/surrogate） | 三链全绿 + 存量零回归 |
| 批二 | PD29-2/3 wrap 路径 + T29-1/2/3 | 三链全绿 + A2 结构判据 |
| 批三 | T29-4/5/6 + harness 气泡端到端（A5）+ 收口 | 五链全绿 + A1–A5 全判 |

---

## 5. 影响面（初设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（`TextMeasurer.h` 扩员不加文件） |
| 公共 API | **+1**（`TextMeasurer::FitText` 虚方法带默认实现 + `TextFit` 值类型——需求稿「+1 倾向」兑现；子类可不覆写） |
| 用例 | **335 → ~346**（+11：T29-1..6 + 评审 §22 建议的 FIT×2 / WRAP×3 边界测试——初设 v1.1 采纳） |
| CMake | **0 改动** |
| 风险 | **中**——断行禁则语义（D29-D 字面集）+ FitText 双链一致性 + 指纹键演进（触碰 Phase 28 新码）；缓释 = 默认关红线 + A2 结构判据 + memo 复用 |

---

## 6. 开放项

| # | 项 |
|---|---|
| O1 | 行内对齐（#51③ 接入 wrap：每行居中/右对齐）——本 Phase 每行左对齐，#51 落地时经 `Line.width` 自然接入 |
| O2 | TextBox 编辑器内 wrap（独立挂账——需求 §3.2） |
| O3 | 禁则字面集扩充（v1 最小集；按消费者反馈增补——确定性不破） |

---

## 7. 修订记录

- **v1.1**（2026-10-06）评审吸收。**结论：「Phase 29 Preliminary Design v1.0：PASS → Detailed Design」（比较明确的 PASS）**——15 项状态表全 ✅（上轮六 🟡 全部闭环）；评审定位 =「进入详设只需把算法边界、FitText 精确语义、缓存失效和测试样例继续钉死」，并预警 **Phase 29 是 ECDI 第一次真正给 TextWidget 增加文本布局能力**。评审强 PASS 项：D29-C TextLayout（startCp+cpCount 而非 string——布局阶段零 substr/零 UTF-8 拷贝，「preferred=5 行实绘=6 行」恶疾被架构消灭 = **强 PASS**）；D29-B（wrap 只增高不创造宽度——「给布局系统留出明确方向」）；**默认关 = 结构级零回归**（T29-4 调用计数判据——非口头默认关）；FitText 语义仍属「文本测量」未降级成 glyph service。**吸收（详设必答①–⑦ + 测试建议）**：① 断行状态机 + 规则命中冲突优先级（fit 硬边界 → 窗口内最后合法软断点 → 禁则回退/前推 → 微超上限）；② 禁则微超上界 = **单禁则码点 advance**（禁继续吸收后续普通字符）；③ **FitText 契约** = startCp 逻辑码点索引、fitCp = 相对可消费码点数（非绝对位置）；④ **surrogate pair 不可拆分**（T29-FIT-2 独立边界）；⑤ 连续 `\n` = 空 TextLine 占行高零命令；⑥ **FT O(n) 实现约束** = 扫描状态 WrapEngine 持有、soft-break 回退禁重复扫描；⑦ 默认二分 = 兼容 fallback（非生产性能路径）；⑧ T29-2 四段式断言（输入/错误/允许/理想——消「不断为「/你好」」文字歧义）。**测试扩展采纳（评审 §22）**：+FIT-1（UTF-8 码点边界）/ +FIT-2（代理对）/ +WRAP-1（连续换行空行）/ +WRAP-2（禁则微超）/ +WRAP-3（同宽 SetSize 不重建）——**用例预算 341 → 346**。未采纳：无。
- **v1.0**（2026-10-06）初稿。**输入**：需求稿 v1.1（评审 PASS，四关键语义问题 + 四红线四强约束）+ 勘察（`TextStyle = {foreground, font}` 无 padding——D29-A 预期成立 · `CodepointIndexToByteOffset` 共享 util `Core/UTF8.cpp:35` · `CalculateTextPosition` 基类 = 左对齐+垂直居中 · DefaultTheme padding=0 属 TextBoxStyle 域）。**六题钉死**：D29-A wrap 宽度 = `GetWidth()` 拒独立状态 · D29-B AutoSize 语义（无有限宽退化单行）· D29-C 共享 TextLayout（`Line{startCp,cpCount,width}` 双消费端 + 红线①结构分隔）· D29-D 断行算法（贪心 + 软断点三类 + 禁则微规则 + 超长词硬断 + 确定性）· D29-E 三方案比较 ⇒ **方案 A**（`FitText` 带默认体，GDI 原生/FT memo 覆写，返回码点数）· D29-F 指纹键扩展（wrapOn + maxWidth，向后兼容）。方案 PD29-1..3 · 用例 T29-1..6（335 → ~341）· 三批 · 94→94 / API +1 / CMake 0 / 风险 中。
