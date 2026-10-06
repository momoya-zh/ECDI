# Phase 29 · 文本多行能力 —— 详细设计（v1.2）

> 来源：初设 `phase29-text-multiline-preliminary-design.md` **v1.1（评审 PASS → 详设）**——初设评审给的**详设必答 ①–⑦** 逐题钉死（本稿 §1）。
> 状态：**v1.2**（2026-10-06）——**批一已实施**（FitText 三链落地，五链 339/339 全绿；三条实施勘误见 §10）｜v1.1：**✅ 评审通过（Conditional PASS → 两项文档级必修当场修毕 ⇒ PASS → Implementation）**（外部评审 2026-10-06：架构方向全部认可、15 项边界表全 ✅；**两项必修**=① D29-Ⅰ 状态变量歧义（scan/lineStart/lineEnd 三变量化）② 行宽来源统一（FT 禁 per-line MeasureText、GDI 回退后补测归 O5）；**强烈建议吸收**=③ GDI O4 明确为已知性能债务非 O(n) 契约 ④ TextLayout 指纹显式列键 ⑤ T29-2 措辞受 maxWidth 约束；吸收明细见 §9 v1.1）
> 定位：**实施规格**——断行状态机正式化、FitText 精确契约、共享 TextLayout 生命周期、逐文件 △、契约 C29、用例 T29、三批。

---

## 1. 详设必答（初设评审 ①–⑦ 逐题钉死）

### D29-Ⅰ ★★ 断行状态机与规则命中优先级（必答①）

**状态机正式化**（`BuildTextLayout(maxWidth)`——TextWidget 私有）：

```text
输入：cps = DecodeUtf8(m_text)（共享 util Utf8Decode.h）；maxWidth = GetWidth()
第一步：显式 \n 切硬段（\n 不属任何一行；连续 \n ⇒ 空段）
  hardSegs = [ [s₀,c₀), [s₁,c₁), … ]（码点区间；空段合法 → D29-Ⅴ）
对每个非空硬段（顺序）：
  scan = 段起点                                          // 本段第一个未消费码点
  循环（段内贪心）——★ 三状态变量语义互斥（评审必修①）：
    lineStart = scan                                     // 本行第一个码点（emit 前恒定，不再改写）
    ① fit = FitText(font, m_text, scan, maxWidth)        // 硬边界（C29-1 契约）
       hardEnd = scan + fit.fitCp
    ② if hardEnd ≥ 段末 ⇒ lineEnd = 段末 → emit → 本段结束
    ③ b = FindLastSoftBreak(cps, scan, hardEnd)          // 窗口 [scan, hardEnd] 内**最后一个**合法软断点
       // 软断点 = 空格后（消费）/ 连字符后（保留前行）/ CJK 主体字符后；无 ⇒ b = hardEnd（按字硬断）
    ④ 行尾禁则：while cps[b−1] ∈ 开括号集 ⇒ −−b（有界：b > lineStart）
    ⑤ 行首禁则：while b ∈ 闭标点集 ⇒ ++b（有界：b ≤ hardEnd；悬挂微超——D29-Ⅱ）
    ⑥ lineEnd = b                                        // 本行最终终点（禁则后定案）
       行内容 = [lineStart, lineEnd) 尾部空白裁剪
       nextScan = lineEnd + 跳过连续空格                  // 分隔空格不进任何一行
    ⑦ emit TextLine{lineStart, lineEnd − lineStart, width}  // width 来源 = D29-Ⅵ（非 MeasureText(切片)）
       scan = nextScan
```

- **状态变量语义**（评审必修①——三变量互斥，原稿歧义根除）：`lineStart` = emit 时恒定不再改写；`lineEnd` = 禁则终化后的行终点；`scan` = **下一行起点**（= nextScan）——原稿「scan = 跳过空格」与「scan = b」并存是 off-by-one/跳空格错误的根源。

- **规则命中优先级固定**（评审 §8——防双实现歧义）：**硬边界 > 窗口内最后合法软断点 > 行尾禁则（先） > 行首禁则（后） > 空白终化**。固定流水线序——禁则调整后**不回退重查软断点**（衔接必答⑥）。
- **冲突示例裁决**：「空格后紧接闭括号」（`abc ）…`）——空格先被软断点消费，闭括号触发行首禁则 ⇒ 悬挂进前行（行内容含空格+闭括号，微超 = 闭标点 advance）；「连字符后紧接闭括号」（`well-）`）——连字符后断、闭括号行首禁则悬挂同行。
- ★ **v1.1 评审钉死（详设必答①②⑥ 补充）**：① **断行算法细节冻结**（评审 §5–§8）——软断点三类（空格后[消费]/连字符后[保留前行]/CJK 主体后）+ 禁则微规则（行首禁闭标点/行尾禁开括号——最小字面集，非 UAX #14）+ 超长词按字硬断 + 确定性禁 locale + 宽度 float 严格比较不引入第二量化；② **禁则微超上界 = 单个禁则码点自身的 advance**（评审 §9——禁则回退并入前行后**不得继续吸收后续普通字符**，防 maxWidth=100 / 行宽 180 的怪异结果——D29-Ⅱ 结构性上界的语义化）；③ **FT O(n) 实现约束**（评审 §15——见 D29-Ⅵ；soft-break 回退不得导致重复扫描已处理码点——否则 O(n) 保证被实现细节破坏）。
- **空段**：`cpCount == 0` 的硬段 ⇒ 空 TextLine（D29-Ⅴ）。

### D29-Ⅱ ★ 禁则微超上界（必答②）

- 微超 = 被悬挂的**连续行首禁则链**的 advance 总和（`）。` 两连挂 = 两码点）；**链外普通字符不得因禁则被吸收**（行首禁则循环遇非成员即停——结构性上界）。
- 行尾禁则的 `−−b` **不产生微超**（开括号下移是行宽缩小方向）。
- 字面集（v1 最小）：行首禁则 = `」』）】〉》」，、。！？；：…`；行尾禁则 = `「『（【〈《`。扩充 = O3 开放项（T29-2 复跑）。

### D29-Ⅲ ★★ FitText 精确契约（必答③）

```cpp
struct TextFit {
    std::size_t fitCp;   ///< 从 startCp 起可消费的码点数（**相对数**——非绝对结束位置）
    float width;         ///< 已消费段测量宽（DIP）
};
virtual TextFit FitText(const Font& font, const std::string& text,
                        std::size_t startCp, float maxWidth) const;
```

- `startCp` = UTF-8 解码后的**逻辑码点索引**（text 全串绝对索引）；`fitCp` = **从 startCp 起实际可消费的码点数**——例：text = A B C D E，startCp = 2，fitCp = 2 ⇒ 消费 "C D"（评审例原样入契约）。
- **`FitText` 不理解空格/CJK/连字符/禁则**（职责边界——评审 §16：WrapEngine 管断在哪 / FitText 管最多放多少）。
- **码点边界保证**：fitCp 永不落在 UTF-8 字节序列中间（操作对象 = 解码后码点——T29-FIT-1）。
- ★ **v1.1 评审钉死（详设必答③④⑦ + §16）**：① **FitText 精确契约冻结**（评审 §12——本节即正式契约：`startCp` = UTF-8 解码后逻辑码点索引；`fitCp` = **从 startCp 起实际可消费的码点数（相对数）**，非绝对结束位置）；② **surrogate pair = 不可拆分单位**（评审 §13——GDI wchar fit 换算**绝不停在代理对中间**；独立边界测试 T29-FIT-2）；③ **默认二分实现 = 兼容 fallback**（评审 §14——仅保证既有自定义 TextMeasurer/测试类零改动，**非 GDI/FT 生产性能路径**；substring 构造成本 O(len)×O(log) fallback 可接受——详设记录在案）；④ **职责边界**（评审 §16）——**WrapEngine 管断在哪 / FitText 管从 startCp 最多放多少**；`FitText` **不理解**空格/CJK/连字符/禁则（防 API 失控）。

### D29-Ⅳ ★ GDI surrogate 完整边界（必答④）

- `GetTextExtentExPointW` 返回 **wchar fit** ⇒ 三步换算：
  1. `wide[fitW−1]` 为高代理（0xD800–0xDBFF）⇒ `−−fitW`（退到完整代理对边界——**代理对不可拆分**）；
  2. `cpFit = fitW − (wide[0..fitW) 内低代理数)`（线性计数——每代理对 2 wchar = 1 码点）；
  3. 返回 cp 口径。
- UTF-8 侧无此问题：`DecodeUtf8` 直接产 cp > 0xFFFF（4 字节序列），FT `FT_Get_Char_Index` 直取 SMP 码点。
- 独立边界测试 T29-FIT-2（`A😀B`）。

### D29-Ⅴ ★ 连续 `\n` 空行语义（必答⑤）

- `\n` = 硬断且**不属任何一行**；**连续 `\n` ⇒ 空 TextLine**（cpCount = 0，width = 0）。
- 空 TextLine：**占行高**（totalHeight 计入）**不发 DrawText 命令**（绘制跳过空行）。
- 首行/末行可为空行（`A\n` = 2 行；`\nA` = 2 行）——`lines.size()` 与用户文本语义一致（T29-WRAP-1）。

### D29-Ⅵ ★★ FT O(n) 实现约束（必答⑥）

- **扫描状态由 WrapEngine（BuildTextLayout）持有**：每行一次 `FitText(scan, maxWidth)`——fit 只扫**当前行窗口**；软断点回退/禁则调整 = **O(1) 码点分类查表**，**不得触发重新 FitText / 重新扫描已处理码点**。
- ★ **v1.1 必修②（行宽来源统一——评审必答②核心）**：`TextLine.width` **由 WrapEngine 扫描状态直接得到，禁对每行调 `MeasureText(切片)`**（原 D29-Ⅰ ⑦ 的 MeasureText 表述已删——与 D29-Ⅵ 冲突）。FT 路径 = 扫描期 advance 累积 / `FitText.width` 直接采用；软断点回退后 = 对最终 `[lineStart, lineEnd)` 逐码点 memo 求和（O(n)——每码点至多两次查询：fit 一次 + 终宽一次）；GDI 路径 = 无回退用 `fit.width`、**回退后补测一次归 O5**（开放项——评审 §18 认可此分界）。
- 禁（盯防）：BuildTextLayout 内对同一区间的重复 FitText、反复 substr。

### D29-Ⅶ ★ 默认 FitText = 兼容 fallback（必答⑦）

- 默认体（`TextMeasurer.h` **内联**——公共头自包含，仅依赖自身 `MeasureText` 与 `Core/UTF8.h` 换算）= 码点二分 + `MeasureText(切片)`：**兼容路径**——保证既有自定义 TextMeasurer / 测试类（MeasureProbe / CountingMeasurer / FakeTextMeasurer）零改动可用。
- **非生产性能路径**：GDI 生产 = 原生覆写（§D29-Ⅳ）；FT 生产 = memo 覆写（§D29-Ⅵ）。substring 构造成本 O(len) × O(log) 次——fallback 场景可接受（详设记录在案）。
- 调用侧**不得**依赖默认体做性能判断（盯防⑦）。

---

## 2. 逐文件改动（△1–△9）

| △ | 文件 | 改动 |
|---|---|---|
| △1 | `include/ECDI/Render/TextMeasurer.h` | +`struct TextFit {std::size_t fitCp; float width;}`；+`virtual TextFit FitText(const Font&, const std::string&, std::size_t startCp, float maxWidth) const`——**默认体内联**（码点二分 + `Core/UTF8.h` 换算——公共→公共 include ✓ 自包含）；**API +1** |
| △2 | `src/Render/GDITextMeasurer.h/.cpp` | FitText 覆写：复用 MeasureText 脚手架（GetDpiForWindow/GetDC/GetOrCreateFont/SelectObject）→ `GetTextExtentExPointW` → **D29-Ⅳ 三步换算**（代理对感知） |
| △3 | `src/Render/FontEngine.h/.cpp` | +`TextFit FitText(const Font&, const std::string&, std::size_t startCp, float maxWidth)`——从 startCp 起逐码点 advance **memo 累积**（复用批一 advanceCache；缺字形跳过同语义） |
| △4 | `src/Render/FreeTypeTextMeasurer.cpp` | FitText 覆写转发 `m_engine->FitText` |
| △5 | `include/ECDI/Widget/TextWidget.h` | public +`void SetWordWrap(bool)` / `[[nodiscard]] bool IsWordWrap() const`；private +`bool m_wordWrap = false` + `TextLine/TextLayout` 嵌套 + `mutable TextLayout m_wrapLayout` + 私有 `BuildTextLayout(maxWidth)` / `GetTextLayout(measurer)` |
| △6 | `src/Widget/TextWidget.cpp` | ① `SetWordWrap`（双缓存失效 + Invalidate）；② `GetPreferredSize` wrap 分支（wrap=true ⇒ 布局驱动 `{GetWidth(), totalHeight}`；width≤0/无窗口 ⇒ 退化单行）；③ `DrawTextContent` wrap 分支（区块定位按 totalHeight + 逐行 DrawText，空行跳过） |
| △7 | `src/Tests/FontEngineTests.cpp` | +FitText 基础/溢出两用例（FT 链无头） |
| △8 | `src/Tests/TextMeasurerTests.cpp` | +GDI FitText 两用例（代理对/宽度边界——MeasurerWindow RAII 沿用） |
| △9 | `src/Tests/WidgetTests.cpp` | +T29-1..6 五用例（真窗口 probe 沿用 T28-3 装置） |

- **零新文件**（FRAMEWORK_SOURCES = `GLOB_RECURSE CONFIGURE_DEPENDS`——CMake 0 ✓）；`TextMeasurer.h` 默认体自包含（公共→公共 include `Core/UTF8.h` ✓）。

---

## 3. 契约（C29-1..8）

- **C29-1** FitText：`startCp` = 解码后逻辑码点索引；`fitCp` = **相对可消费码点数**；`width` = 消费段宽（DIP）；永不落 UTF-8 序列中间。
- **C29-2** surrogate pair 不可拆分（GDI 换算退到完整对边界；FT 侧 cp 直取无此问题）。
- **C29-3** 禁则微超上界 = 连续行首禁则链 advance（普通字符不得吸收）；行尾禁则无微超。
- **C29-4** 空 TextLine 占行高、零 DrawText 命令。
- **C29-5** `wrap == false` ⇒ 100% Phase 28 原路径（布局引擎零接触——结构分隔非约定）。
- **C29-6** 一份 TextLayout 双消费端（GetPreferredSize 与 DrawTextContent 同源——禁各自 Wrap）。
- **C29-7** 确定性：分类/禁则 = 字面常量集；宽度 float 严格比较；禁 locale/NLS。
- **C29-8** **断行按各链自身度量**——跨链 fit 结果不承诺一致（链间宽度本不同源）；一致性要求 = **同链确定性**（A2 红线只约束 wrap=false 场景）。
- **C29-9** **O(n) 契约范围**（评审建议③）= **仅限 FT/advance-memo 路径**；GDI `UTF8ToWide` 重复转换成本 = **已知性能债务（O4）非正确性契约**——文档不得出现「Phase 29 整体 O(n)」与「GDI 可 O(n²)」并存的表述。
- **C29-10** **TextLayout 指纹显式键**（评审建议④）= `(textRevision, font.size, font.family, dpi, GetWidth())`——`GetTextLayout()` 入口**每次判定**缓存有效性（不依赖任何 setter「记得清」——与 Phase 28 preferred 缓存同构的显式键）；T29-3/T29-WRAP-3 为其验收锚。

---

## 4. 盯防（6 条）

1. `FitText` 不理解空格/CJK/禁则（职责边界——评审 §16）。
2. 布局阶段零 substr（UTF-8 切片仅绘制时；行 = 码点区间）。
3. wrap 分支不得触碰 Phase 28 preferred 缓存（`m_prefValid`/`m_prefCache` 正交——两缓存两分支）。
4. 禁则链循环有界（遇非禁则成员即停——C29-3 上界）。
5. 空 TextLine 计行高零命令（`lines.size()` 与用户语义一致）。
6. 禁则/分类字面集改动 ⇒ T29-2/T29-WRAP-2 复跑（盯防同 Phase 28 ⑥）。
7. 批一验收 = **所有既有 TextMeasurer 派生类编译级验证**（MeasureProbe/CountingMeasurer/FakeTextMeasurer 等——默认 FitText 体不得破坏任何子类）；
8. **FT 路径不得对每行二次 MeasureText**（= 必修②同源盯防；实现期发现重复扫描即改）。

---

## 5. 用例（11 条，335 → 346）

| # | 名（注册名） | 文件 | 断言核心 |
|---|---|---|---|
| T29-1 | `TextWidget.WrapLatinBasic` | WidgetTests | 拉丁按词切分：每行一条 DrawText、内容 = 该行、preferred = {宽, 行数×行高} |
| T29-2 | `TextWidget.WrapCjkKinsuku` | WidgetTests | CJK 逐字 + 禁则（v1.1 措辞 = **受 maxWidth 约束**：宽度允许时 `「你好」` 保持同行；必须换行时闭标点不得独占行首；四段式 = 输入/错误/允许/理想）——禁则四段式（输入「你好」：错误 = 「起行 / 允许 = 你好间断 / 理想 = 同行）+ 混排 |
| T29-3 | `TextWidget.WrapWidthDynamics` | WidgetTests | width 300→250→400 ⇒ 行数/preferred 同步变；不变 ⇒ 命中零调用；`SetWordWrap` 切换 ⇒ 失效 |
| T29-4 | `TextWidget.WrapDefaultOff` | WidgetTests | `wrap=false`：FitText/布局构建**零调用**（计数）+ 命令流 = 单条 DrawText 原样 |
| T29-5 | `TextWidget.WrapDegenerate` | WidgetTests | width=0 退化单行 / 超长词按字硬断 / 空串 / 单字符 |
| T29-6 | `TextWidget.BubbleEndToEnd` | WidgetTests | 固定宽面板 + wrap Label + realistic 语料 ⇒ 高度自适应 |
| T29-FIT-1 | `TextMeasurer.GdiFitTextCpBoundary` | TextMeasurerTests | ASCII/CJK/多字节混串——fitCp 永不落 UTF-8 序列中间 |
| T29-FIT-2 | `TextMeasurer.GdiFitTextSurrogate` | TextMeasurerTests | `A😀B`——GDI 换算不拆代理对 |
| T29-WRAP-1 | `TextWidget.WrapEmptyLines` | WidgetTests | `A\n\nB` = 3 行（空行占高零命令） |
| T29-WRAP-2 | `TextWidget.WrapKinsukuOverflow` | WidgetTests | 禁则微超 = 单标点悬挂、不吞普通字符 |
| T29-WRAP-3 | `TextWidget.WrapSameWidthNoRebuild` | WidgetTests | 同宽 SetSize ⇒ 布局命中零重建 |

- 落位：WidgetTests ×8（真窗口 probe 沿用 T28-3 装置）· TextMeasurerTests ×2（MeasurerWindow RAII）· FontEngineTests ×2（批一随 FitText 落地）。
- ★ **批一实施校正**：FontEngine 两条**已独立注册**（`FontEngine.FitTextMatchesMeasure` / `FontEngine.FitTextBoundaries`），TextMeasurer 侧另加一条默认体用例（`TextMeasurer.DefaultFitTextCpBoundary`）⇒ **批一实际注册 +3**（`TextMeasurer.GdiFitTextSurrogate` 为 T29-FIT-2）。原稿「注册 +10」的批次分配作废，改为按批实际登记（收口时在 §10 给出终值）。

---

## 6. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | △1 FitText 默认体 + △2 GDI 覆写 + △3 FontEngine::FitText + △4 FT 转发 + T29-FIT-1/2 | ✅ **已完成**（五链 339/339；+T29-FIT-3/4——见 §10；三条实施勘误见 §10.2） |
| 批二 | △5/△6 wrap 路径 + T29-1/2/3 + T29-WRAP-1/3 | 五链全绿 + A2 结构判据（默认关零调用） |
| 批三 | T29-4/5/6 + harness 气泡端到端（A5）+ 收口 | 五链全绿 + A1–A5 全判 + 五处台账 |

---

## 7. 影响面（详设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（TextMeasurer.h/TextWidget.h 扩员不加文件） |
| 公共 API | **+3**（`FitText` 虚方法 + `SetWordWrap`/`IsWordWrap` 对——★ **需求稿「+1 倾向」只计了测量原语，wrap 开关对漏计，详设如实修正**）｜★ 批一实绩 = **+1**（`TextFit` + `FitText`，均落 `TextMeasurer.h`）；`SetWordWrap`/`IsWordWrap` 批二落地 |
| 用例 | **335 → 346**（+11） |
| CMake | **0 改动**（GLOB_RECURSE CONFIGURE_DEPENDS——新 .cpp 自动入库） |
| 风险 | **中**（断行禁则语义 + FitText 双链度量差异[跨链断行不承诺一致——C29-8] + 指纹键演进；缓释 = 默认关结构红线 + A2 + memo 复用） |

---

## 8. 开放项

| # | 项 |
|---|---|
| O1 | 行内对齐（#51③：每行居中/右对齐经 `Line.width` 接入）——本 Phase 每行左对齐 |
| O2 | TextBox 编辑器内 wrap（独立挂账——需求 §3.2） |
| O3 | 禁则字面集扩充（按消费者反馈增补） |
| O4 | GDI FitText 的 UTF8ToWide 每调用 O(n)（k 行 ⇒ O(n·k) memcpy——量级可接受；实测瓶颈则缓存宽串）。★ **批一实测**：`FitText` 内**只转换一次**（`text.substr(byteStart)` ⇒ 尾段），未按行重复；债务成立但常数极小 |
| O5 | wrap 行宽二次测量（软断行每行 +1 MeasureText——FT 侧可省[布局即知]、GDI 侧必要）。★ **批一实测**：GDI `FitText` 在 `fit < 全长` 时**必补一次** `GetTextExtentPoint32W`（取消费前缀宽——`GetTextExtentExPointW` 的 `extent` 是**全串**宽不可用）⇒ 该成本已落在**每个 FitText 调用**上，不止软断行回退路径（批二/三复评） |

---

## 10. 实施回填（批一——2026-10-06）

### 10.1 批次执行表（批一）

| 项 | 内容 | 文件 | 实绩 |
|---|---|---|---|
| △1 | `TextFit` 结构 + `FitText` 默认体（码点二分 + `MeasureText(切片)`） | `include/ECDI/Render/TextMeasurer.h` | ✅ 内联自包含（公共→公共 include `Core/UTF8.h`）；**API +1** |
| △2 | `GDITextMeasurer::FitText` 原生覆写（`GetTextExtentExPointW` + D29-Ⅳ 三步换算） | `src/Render/GDITextMeasurer.h/.cpp` | ✅ 代理对感知；探针实测 `A😀B` ⇒ fitCp = 3/2/1 逐档正确 |
| △3 | `FontEngine::FitText`（advance memo 累积——O(n) 生产路径） | `src/Render/FontEngine.h/.cpp` | ✅ 单趟线性扫描（**非二分**：memo 命中下二分反而多跑 log n 趟，且线性天然「只扫到断点」） |
| △4 | `FreeTypeTextMeasurer::FitText` 转发 | `src/Render/FreeTypeTextMeasurer.h/.cpp` | ✅ 无 engine ⇒ 放行全串（与 `MeasureText` 返 `Size{}` 同族） |
| 测试 | T29-FIT-1（默认体码点边界）/ T29-FIT-2（GDI 代理对）/ T29-FIT-3（FT 与 MeasureText 逐位一致）/ T29-FIT-4（FT 边界） | `TextMeasurerTests.cpp` `FontEngineTests.cpp` | ✅ **注册 +4**（339 = 335 + 4） |

- **五链验收**：MinGW / Clang / ClangCL Debug + MinGW Release + MSVC Release（cl.exe 14.51）——**339/339 全绿**（各链复跑 3 次稳定）。
- **批一验收（盯防⑦）派生类零改动**：`GDITextMeasurer` / `FreeTypeTextMeasurer` / `RecordingBackend` / `FakeTextMeasurer`（WidgetTests + TextBoxTests 两处）/ `CountingMeasurer` —— **全部零改动编译通过**，证明默认体内联体自包含、不要求派生类补实现。

### 10.2 实施勘误（三条——**详设与代码的偏差，如实登记**）

| # | 勘误 | 原稿 | 实施 | 影响 |
|---|---|---|---|---|
| E1 | **`FitText` 的 const 限定** | 冻结合同写作 `const` 虚函数 | **非 const**（与 `MeasureText`/`LineHeight` 同族） | ★ 原稿 `const` **编译不通过**：默认体必须调 `MeasureText`（非 const），而 `const` 版只能 `const_cast` ⇒ 对 `GDITextMeasurer`（`MeasureText` 写 `m_measureCache`）是 **UB**。改非 const 是唯一正确解：调用方（`TextWidget`）本就持非 const 指针，**零调用代价**。★ 派生类的 `MeasureText` 缓存写入因此合法 |
| E2 | **`MeasureText` 循环体提取** | 未提及（原稿只加 `FitText`） | 把 memo 求值循环体提取为 `Impl::AdvanceOf`（`MeasureText` 与 `FitText` **共用**） | ★ **逐位一致的前提**：两方法若各自实现 memo/哨兵/load-flags/skip 语义，一旦漂移则「测出的宽」与「断行算出的宽」失配（表现 = 断行位置与绘制错位）。`FitText` 的 `width` 与 `MeasureText` **逐位相等**已由 T29-FIT-3 锚定（实测 `192.0000 == 192.0000`） |
| E3 | **码点总数取法** | 未提及 | `ByteOffsetToCodepointIndex(text, text.size())` | ★ 实施期我一度用 `CodepointIndexToByteOffset(text, SIZE_MAX)`——该函数是「码点索引 → **字节偏移**」且超界时**钳制到 `text.size()`** ⇒ 返回**字节数**；UTF-8 多字节串上把码点数放大 2~3 倍 ⇒ `totalCp` 偏大 ⇒ `fitCp` 越界（**探针实测定位**，非猜测）。★ 既有公共函数已够用 ⇒ **零新增 API**；两函数**换算方向必须成对使用** |

### 10.3 批一实测读数（探针 `.workbuddy/spike/p29/fit_probe.cpp`）

| 观测 | 读数 | 判据 |
|---|---|---|
| 默认体（每码点恒宽 8）· `"AB 你好"`（5 码点） | startCp=3 ⇒ fitCp=2、width=16.0 | 相对口径正确 |
| FT · `Hello 世界。这是一个测试`（15 码点，DPI 120） | startCp 0→14 ⇒ fitCp 15→1 单调递减；startCp≥15 ⇒ 0 | 越界语义正确 |
| FT · fitCp=15 的 width vs `MeasureText` 全串 | **192.0000 == 192.0000** | ★ **逐位一致**（E2 的直接证据） |
| GDI · `A😀B`（3 码点 / 6 字节 / 4 wchar） | full=36 ⇒ fitCp=3；ab=27 ⇒ fitCp=2；a=9 ⇒ fitCp=1；limit=0 ⇒ 0 | ★ **代理对不拆分** |
| GDI · 原生 vs `MeasureText` oracle（`"AB 你好"`，k/6 倍全宽） | k=0..6 逐档 fitCp 一致（0/1/2/3/3/4 与 oracle 同） | 两条**不同 Win32 原生调用**给出同语义 |

- ★ **跨链不比 fitCp**（C29-8）：GDI 比例字体与 FT/默认体模型的断点本就不同——验收只要求**同链确定性**；GDI 的实现内部则用 `MeasureText` 做独立 oracle（不同 API，同字体）。

### 10.4 收口核对（批一）

- 公共头 **94 → 94**（`TextMeasurer.h` 扩员不加文件）；公共 API **+1**（批一；总预算 +3，余 +2 批二）；**CMake 0**；用例 **335 → 339**（+4）。
- **零回归**：`wrap == false` 路径未触碰（批一未引入任何开关——C29-5 结构红线由批二兑现）；339 中 335 条既有用例全绿。
- **遗留**：O4/O5 已按实测更新（§8）；批二/三按原计划。

---

## 9. 修订记录

- **v1.2**（2026-10-06）**批一实施回填**（FitText 三链落地——五链 339/339 全绿）。★ **三条实施勘误**（§10.2）：**E1 `FitText` 非 const**（原稿 `const` 编译不通过：默认体须调非 const 的 `MeasureText`，`const` 版只能 `const_cast` ⇒ 对写缓存的 `GDITextMeasurer` 是 UB；改非 const 零调用代价）· **E2 `MeasureText` 循环体提取为 `Impl::AdvanceOf`**（两方法共用是**逐位一致**的前提——T29-FIT-3 锚定 192.0000==192.0000）· **E3 码点总数取法**（`CodepointIndexToByteOffset(…,SIZE_MAX)` 返回**字节数**而非码点数——超界钳制语义；改用既有 `ByteOffsetToCodepointIndex(text, text.size())`，**零新增 API**）。★ 批一实测（§10.3）：FT fitCp 单调 + width 与 MeasureText 逐位一致；GDI `A😀B` ⇒ 3/2/1 逐档代理对不拆；GDI 原生 vs `MeasureText` oracle 逐档同。★ 批一验收 = 六个既有 `TextMeasurer` 派生类**零改动编译通过**（盯防⑦）。API 批一 **+1**（总预算 +3）。
- **v1.1**（2026-10-06）评审吸收（**Conditional PASS → 两修当场修毕 ⇒ PASS → Implementation**）。**架构方向全部认可**（15 项边界表全 ✅；上轮六 🟡 全闭环）；评审定位 =「进入详设只需把算法边界、FitText 精确语义、缓存失效和测试样例继续钉死」并预警 **Phase 29 是 ECDI 第一次真正给 TextWidget 增加文本布局能力**（边界影响将来富文本/shaping/Bidi）。评审强 PASS 项：TextLayout 用 startCp+cpCount 而非 string（布局零 substr/零拷贝——「preferred=5 行实绘=6 行」恶疾被架构消灭 = **强 PASS**）；默认关 = 结构级零回归（T29-4 调用计数判据）；FitText 语义未降级成 glyph service。**两项必修（当场修毕）**：① **D29-Ⅰ 状态变量三分化**（`lineStart`/`lineEnd`/`scan` 语义互斥——原稿「scan」双含义是 off-by-one/跳空格错误根源）；② **行宽来源统一**（D29-Ⅰ ⑦ 的 `MeasureText(切片)` **删除**：FT = 扫描期 advance 累积/`FitText.width` 直接采用、回退后逐码点 memo 求和；GDI = 无回退用 `fit.width`、回退后补测一次归 O5——**禁止对每行再调 MeasureText**）。**强烈建议吸收（三条）**：③ **O(n) 契约范围明确**（**仅限 FT/advance-memo 路径**；GDI `UTF8ToWide` 重复转换 = 已知性能债务 O4 非正确性契约——概念冲突消解，C29-9）；④ **TextLayout 指纹显式列键**（五元组 + `GetTextLayout()` 入口每次判定——不依赖 setter 记得清，C29-10）；⑤ **T29-2 措辞受 maxWidth 约束**（「理想同行」仅宽度允许时；必须换行时禁闭标点独占行首——四段式消歧）。**测试扩展采纳（评审 §22）**：+T29-FIT-1（UTF-8 码点边界）/ +T29-FIT-2（代理对——含 😀 / A😀 / 😀B 三边界样本增强）/ +T29-WRAP-1（连续换行空行）/ +T29-WRAP-2（禁则微超）/ +T29-WRAP-3（同宽不重建）——**用例预算 341 → 346**。**非阻塞采纳**：批一验收 = **所有既有 TextMeasurer 派生类编译级验证**（盯防⑦）；FT 路径避免每行二次 MeasureText（= 必修②同源，盯防⑧）。未采纳：无（O4 不扩大范围——评审 §5 同意）。**评审路线图**：28 文本成本 → 29 多行布局 → #51 行对齐 → TextBox wrap → shaping/bidi/富文本（健康演进不一锅端）。
- **v1.0**（2026-10-06）初稿。**输入**：初设 v1.1（评审 PASS「比较明确的 PASS」，15 项全 ✅）+ 评审详设必答 ①–⑦ + 代码勘察（`TextStyle = {foreground, font}` 无 padding · `Core/UTF8.h` 公共换算 util · `CalculateTextPosition` 基类左对齐+垂直居中 · FRAMEWORK_SOURCES = GLOB_RECURSE ⇒ 零 CMake · GDI MeasureText 脚手架批三 fit 先例）。**七题钉死**：D29-Ⅰ 断行状态机（流水线序 = fit → 软断点回退 → 行尾禁则 → 行首禁则 → 空白终化；冲突优先级固定）· D29-Ⅱ 禁则微超 = 禁则链 advance（普通字符不得吸收）· D29-Ⅲ FitText 契约（相对码点数）· D29-Ⅳ GDI surrogate 三步换算 · D29-Ⅴ 连续 `\n` 空 TextLine · D29-Ⅵ FT O(n)（WrapEngine 持扫描状态）· D29-Ⅶ 默认体 = fallback。△1–△9（零新文件/零 CMake）· C29-1..8 · 盯防 6 · 用例 11 条（335 → **346**）· 三批 · **API +3**（FitText + SetWordWrap/IsWordWrap 对——★ 需求稿「+1 倾向」只计测量原语，wrap 开关对漏计，如实修正）· 94→94 / CMake 0 / 风险 中。
