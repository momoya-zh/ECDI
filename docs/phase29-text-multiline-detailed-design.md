# Phase 29 · 文本多行能力 —— 详细设计（v1.4 · ✅ 全链收口）

> 来源：初设 `phase29-text-multiline-preliminary-design.md` **v1.1（评审 PASS → 详设）**——初设评审给的**详设必答 ①–⑦** 逐题钉死（本稿 §1）。
> 状态：**v1.4**（2026-10-07）——**✅ 全链收口**（批一/二/三全部落地，五链 349/349，A1–A5 全判；详设 v1.4 §12 收口）｜v1.3：批二实施（四条勘误见 §11.2）｜v1.2：批一实施（三条勘误见 §10）｜v1.1：**✅ 评审通过（Conditional PASS → 两项文档级必修当场修毕 ⇒ PASS → Implementation）**（外部评审 2026-10-06：架构方向全部认可、15 项边界表全 ✅；**两项必修**=① D29-Ⅰ 状态变量歧义（scan/lineStart/lineEnd 三变量化）② 行宽来源统一（FT 禁 per-line MeasureText、GDI 回退后补测归 O5）；**强烈建议吸收**=③ GDI O4 明确为已知性能债务非 O(n) 契约 ④ TextLayout 指纹显式列键 ⑤ T29-2 措辞受 maxWidth 约束；吸收明细见 §9 v1.1）
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
| T29-WRAP-3 | `TextWidget.WrapSameWidthNoRebuild` | WidgetTests | 同宽 SetSize ⇒ 布局命中零重建；★ **批二另加** `TextWidget.WrapDefaultOffZeroFit`（A2 结构判据——默认关零 FitText 调用 + 单条 DrawText） |

- 落位：WidgetTests ×8（真窗口 probe 沿用 T28-3 装置）· TextMeasurerTests ×2（MeasurerWindow RAII）· FontEngineTests ×2（批一随 FitText 落地）。
- ★ **批一实施校正**：FontEngine 两条**已独立注册**（`FontEngine.FitTextMatchesMeasure` / `FontEngine.FitTextBoundaries`），TextMeasurer 侧另加一条默认体用例（`TextMeasurer.DefaultFitTextCpBoundary`）⇒ **批一实际注册 +3**（`TextMeasurer.GdiFitTextSurrogate` 为 T29-FIT-2）。原稿「注册 +10」的批次分配作废，改为按批实际登记（收口时在 §10 给出终值）。

---

## 6. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | △1 FitText 默认体 + △2 GDI 覆写 + △3 FontEngine::FitText + △4 FT 转发 + T29-FIT-1/2 | ✅ **已完成**（五链 339/339；+T29-FIT-3/4——见 §10；三条实施勘误见 §10.2） |
| 批二 | △5/△6 wrap 路径 + T29-1/2/3 + T29-WRAP-1/3 | ✅ **已完成**（五链 345/345；+A2 结构判据用例；四条实施勘误见 §11.2） |
| 批三 | T29-4/5/6 + harness 气泡端到端（A5）+ 收口 | ✅ **已完成**（五链 349/349；A1–A5 全判——§12.2；+T29-WRAP-2；E8 勘误见 §12.2） |

---

## 7. 影响面（详设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（TextMeasurer.h/TextWidget.h 扩员不加文件） |
| 公共 API | **+4**（详设原计 +3：`FitText` 虚方法 + `SetWordWrap`/`IsWordWrap` 对——★ **需求稿「+1 倾向」只计了测量原语，wrap 开关对漏计，详设如实修正**）｜★ 实测：批一 **+1**（`TextFit` + `FitText`）· 批二 **+3**（`SetWordWrap`/`IsWordWrap` 对 **+2**、`PaintContext::GetTextMeasurer()` **+1**——★ **详设漏计，见 §11.2 E6**）⇒ **合计 +4，超原预算 1** |
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
| O5 | wrap 行宽二次测量（软断行每行 +1 MeasureText——FT 侧可省[布局即知]、GDI 侧必要）。★ **批一实测**：GDI `FitText` 在 `fit < 全长` 时**必补一次** `GetTextExtentPoint32W`（取消费前缀宽——`GetTextExtentExPointW` 的 `extent` 是**全串**宽不可用）⇒ 该成本已落在**每个 FitText 调用**上，不止软断行回退路径。★ **批二实测**：wrap 布局已按 v1.1 必修②实现——`emitLine` 仅当 **`end != fitEnd`**（回退/尾部空白裁剪改变了终止位置）时才补测一次；无回退（整段放得下）直接采用 `fit.width` ⇒ **零二次测量**（T29-1 的 6 行全走 fast path） |

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

## 11. 实施回填（批二——2026-10-06）

### 11.1 批次执行表（批二）

| 项 | 内容 | 文件 | 实绩 |
|---|---|---|---|
| △5 | `SetWordWrap`/`IsWordWrap` + `TextLine`/`TextLayout` 嵌套 + 布局缓存五元组 + `BuildTextLayout`/`GetTextLayout` 私有面 | `include/ECDI/Widget/TextWidget.h` | ✅ 断行开关默认 **false**（红线①）；**API +2** |
| △6① | `SetWordWrap`：**双缓存失效**（preferred 指纹 + 布局指纹同置无效）+ `Invalidate()`；同值调用零失效 | `src/Widget/TextWidget.cpp` | ✅ 正交两缓存各自置无效（盯防③） |
| △6② | `GetPreferredSize` wrap 分支：布局驱动 `{GetWidth(), totalHeight}`；宽 ≤ 0 / 无窗口 ⇒ **退化单行**（走 Phase 28 原路径） | 同上 | ✅ `IsWrapLayoutActive()` 三条件闸 |
| △6③ | `DrawTextContent` wrap 分支：区块按 `totalHeight` 垂直居中 + 逐行 `DrawText`（空行跳过）+ UTF-8 切片**仅绘制时** | 同上 | ✅ 命令流逐行可断言（T29-1/2/WRAP-1） |
| 断行引擎 | D29-Ⅰ 状态机（三状态变量互斥）+ 软断点三类 + 禁则两集（字面常量表）+ 空白终化 + 尾部空白裁剪 | 同上（匿名 namespace 内 `IsLineStartForbidden`/`IsLineEndForbidden`/`IsCjkBody`/`IsSoftBreakBefore`/`FindLastSoftBreak`） | ✅ 确定性（无 locale/NLS——C29-7） |
| 测试 | T29-1 拉丁按词 / T29-2 CJK 禁则四段式 / T29-3 宽度动态 / T29-WRAP-1 空行 / T29-WRAP-3 同宽零重建 / A2 默认关零调用 | `WidgetTests.cpp` | ✅ **注册 +6**（345 = 339 + 6） |

- **五链验收**：MinGW / Clang / ClangCL Debug + MinGW Release + MSVC Release（cl.exe 14.51）——**345/345 全绿**。
- **A2 结构判据前半成立**：`wrap == false`（默认）⇒ `FitText` **零调用**、preferred = 单行原语义、Paint = **单条** `DrawText` 整串、宽度不足也**不换行**（横向溢出交 Clip）——T29-4 的完整像素/命令流等价留批三。

### 11.2 实施勘误（四条——**详设与代码的偏差，如实登记**）

| # | 勘误 | 原稿 | 实施 | 影响 |
|---|---|---|---|---|
| E4 | **禁则两侧调用的命名**（实施期自查发现并修正） | ④ 行尾禁则、⑤ 行首禁则 | ④ 用 `IsLineEndForbidden`（**开括号集**）判 `cps[b−1]`；⑤ 用 `IsLineStartForbidden`（**闭标点集**）判 `cps[b]` | ★ 我首版把两个函数名**对调**（④ 调 `IsLineStartForbidden(cps[b-1])`）——「行尾禁开括号」被写成「行尾禁闭标点」。语义靠测试锚定后发现（T29-2 段落 C 断言 `ab` / `（cd`）⇒ 当场修正。**教训：函数名要照着「禁则作用的**位置**」读，不是照着字符集名字读** |
| E5 | **行首禁则的回提上界** | D29-Ⅰ ⑤ 写「有界：b ≤ hardEnd」 | 上界取 **`segEnd`** | ★ 若上界取 `hardEnd`：`b` 提到 `hardEnd` 时 `cps[b]` 已越硬边界，闭标点**提不上来** ⇒ 规则形同虚设（硬边界永远赢）。取 `segEnd` 才让「闭标点悬挂进前行」真正发生；超宽部分 = 悬挂链 advance = D29-Ⅱ 的**微超**，结构性有界（链遇非禁则成员即停）。★ 与 D29-Ⅱ「普通字符不得吸收」不冲突——吸收的**只有**连续禁则成员 |
| E6 | **`PaintContext::GetTextMeasurer()` 新增**（★ **API 超预算**） | △5/△6 未列此改动；总预算 **+3** | `PaintContext`（**公共头**）+1 访问器 | ★ **详设漏计**：wrap 绘制路径需要一个 `FitText` 可达的测量器，而 `PaintContext` 只转发 `MeasureText`/`LineHeight`（不转发 `FitText`——它是绘制门面，不是测量门面）。两条路：① 转发 `FitText`（形态更窄但要为未来所有测量原语都加转发）；② 交出测量器引用（**本选择**——一个访问器一次解决，且**同源可证**：与 `MeasureText`/`LineHeight` 是同一对象）。★ 代价如实记账：**公共 API +3 → +4**（超原预算 1） |
| E7 | **空段/推进守卫**（原稿未覆盖） | — | ① 空段直接 emit 空行（不复用主循环）；② `b == lineStart` ⇒ 强制 `++b`（按字硬断**一字**）；③ `nextScan <= scan` ⇒ 强制 `scan + 1` | ★ 堵的是**死循环**：若行宽 < 一个码点（如宽度 1 DIP），`FitText` 返 `fitCp = 0` ⇒ 原稿会 emit 空行且 `scan` 不推进 ⇒ **Paint 挂死**（比断行错位严重得多）。三处守卫把「绝不零推进」变成结构性保证（T29-5 超长词用例的锚点，批三补） |

### 11.3 批二实测读数（用例断言 = 可复核的确定性模型）

| 观测 | 读数 | 判据 |
|---|---|---|
| 拉丁按词（6 词 / 宽 40 / 每码点 8 DIP） | 6 行、逐行 `aaaa`…、y 步进 16、首行 y=52 | 软断点 = 空格后 + 分隔空格被消费（不进任何一行） |
| CJK 禁则（`你好）世界` / 宽 16 ⇒ 2 码点/行） | 行 = `你好）` / `世界` | ★ 闭标点**悬挂进前行**（行宽 24 > 16 = 微超 8 = 一个标点 advance）；**非** `你好` / `）世界` |
| 行尾禁则（`ab（cd` / 宽 24） | 行 = `ab` / `（cd` | ★ 开括号**不独占行尾**（被挪到下行） |
| 宽度动态（宽 32 → 16 → 128） | 行数 6 → 12 → 2 | 指纹宽度维驱动重排 |
| 同宽 `SetSize`（仅改高） | `FitText` **零调用** | C29-10 指纹键**不含高** |
| 连续换行 `A\n\nB` | 3 行、**2 条** DrawText、B 的 y 步进 = 2×行高 | ★ 空行占行高、零命令（D29-Ⅴ / C29-4） |
| 空串 | 1 行（高 = 行高）、零 DrawText | 「0 行」会让 preferred 高度归零 ⇒ 用 1 空行 |
| **默认关（A2 判据）** | `FitText` **零调用** / **1 条** DrawText 整串 / 宽度不足也不换行 | 红线①：wrap=false = 100% Phase 28 原路径 |

### 11.4 收口核对（批二）

- 公共头 **94 → 94**（`TextWidget.h`/`PaintContext.h` 扩员不加文件）；公共 API **+3**（批二）⇒ **累计 +4**（超详设预算 +3 一项，见 E6）；**CMake 0**；用例 **339 → 345**（+6）。
- **零回归**：345 中 339 条既有用例全绿；`wrap == false` 结构路径未被触碰（A2 判据实证）。
- ★ **环境教训（如实记档）**：批二中途 MSVC Release 出现**确定性 segfault**（反汇编形态 = 虚调用打在失效 `this` 上：`mov (%rcx),%rax` + 栈上 `std::string` 临时作 arg2）。**根因不在代码**——该目录的 exe 是**增量链接的陈旧产物**（`TextWidget.h` 新增成员 ⇒ 与旧 `ECDI.lib` 的对象布局不一致 ⇒ 虚表/成员偏移读到垃圾）。**干净全量重建后 345/345 全绿**（VS 生成器 + 全新 Ninja/cl 目录各一次）。★ **纪律：改公共头布局后必须全量重建**；增量链接的 exe 不可信。
- **遗留**：T29-4/5/6（默认关命令流逐字节等价 / 退化边界 / harness 气泡端到端）→ 批三。

---

## 12. 实施回填（批三 + 全链收口——2026-10-07）

### 12.1 批次执行表（批三）

| 项 | 内容 | 文件 | 实绩 |
|---|---|---|---|
| T29-4 | `wrap=false` 命令流**逐字段**基线：`[PushClip, DrawText, PopClip]` 三条、文本/位置/颜色/字体四字段 = 手工 Phase 28 基线（左对齐 + (H−行高)/2 垂直居中）、全程零 `FitText` | `WidgetTests.cpp` `Test29WrapDefaultOff` | ✅ **A2 终判**——「原样」有独立参照物（期望值手工算，非调用被测代码） |
| T29-5 | 退化边界：width=0 ⇒ **退化单行**（布局引擎零接触）/ 超长无空格词**按字硬断**（20 字母 ⇒ 4×5 恰好贴边）/ **宽 < 一码点 ⇒ 按字断、绝不挂死**（E7 守卫锚）/ 空串 1 行 / 单字符 | `Test29WrapDegenerate` | ✅ A3 收尾（超长词/贴边/空串/单字符全补齐） |
| T29-WRAP-2 | 禁则微超直测：单标点悬挂（`abc）`/`de`）· **连续链悬挂**（`abc））`/`de`——链外普通字符**不得吸收**）· 全禁则段有界终止 | `Test29WrapKinsukuOverflow` | ✅ C29-3 上界的直接证据 |
| T29-6 | 气泡端到端：固定宽 200 + realistic 混排语料 ⇒ AutoSize 高度 = 行数×行高；★ **A4 线性判据 = `FitText` 调用数 == 行数**（每行恰一次 fit——盯防⑧的结构性证据）；宽度动态可逆 | `Test29BubbleEndToEnd` | ✅ 需求 §1.1 消费者场景的库内形态 |
| **E8 修复** | `GDITextMeasurer::FitText` 像素上限 **ceil → 截断**（见 §12.2——批一潜伏缺陷被批三测试揭出） | `GDITextMeasurer.cpp` | ✅ 探针 13/13 档与 DIP oracle **任意 DPI 精确相等** |
| A5 交付 | Release 静态库（MSVC cl.exe 14.51，HEAD）`cmake --install` 装入 harness 消费前缀 `third_party/ecdi` + **库外气泡探针** | `.workbuddy/spike/p29/bubble_e2e.cpp` | ✅ **E2E PASS（0 failures）**——见 §12.3 |

- **五链验收**：MinGW / Clang / ClangCL Debug + MinGW Release + MSVC Release（cl.exe 14.51，**干净全量重建**）——**349/349 全绿**。

### 12.2 实施勘误（批三 · 一条——**批一潜伏缺陷被揭出**）

| # | 勘误 | 原状 | 修复 | 发现经过 |
|---|---|---|---|---|
| E8 | **GDI FitText 的 DIP→像素换算用了 `ceil`** | `maxPx = ceil(limit·dpi/96)`——**超出 DIP 限宽**：limit=26.25 DIP ⇒ 27px ⇒ GDI 放行 27px 前缀，折回 DIP = 27 > 26.25 ⇒ **违反「width 恒不超 maxWidth」契约**（C29-1） | **截断（floor）**：`px ≤ floor(limit·dpi/96) ⟺ px·96/dpi ≤ limit`（px 为整数）⇒ GDI 接受集与 DIP 口径在**任意 DPI 精确相等**，宁少一码点不超宽（与代理对回退同保守方向） | ★ **批三测试在缩放 100%（dpi=96）下首跑即失败**（T29-FIT-2 的 DIP oracle k=5 档：gdi=3 码点/27 vs oracle=2/18）——**用户指出主屏缩放已切到 100%**（此前 125% 时违规区未被 13 个采样点踩中 ⇒ 批一/批二假绿）。★ 探针实测定位 + 修复后 13/13 档一致。**教训：DPI 相关的舍入缺陷会随显示环境变化「迟到曝光」——跨 DPI 精确性要靠换算纪律（floor + 整数像素口径），不靠采样运气** |

### 12.3 A5 读数（库外气泡探针——与 harness 消费方式**同构**）

- **形态**：探针只链接**安装前缀**（`third_party/ecdi` 的 `ECDI.lib` + 公共头）、只用公共工厂 `CreateDefaultRenderServices()`——「外部消费者能否真的用起来」（需求 A5 / 评审 §19）的直接证据；harness 仓库本身**零改动**（其 gui 侧启用 wrap = 一行 `SetWordWrap(true)`，留给 harness 会话）。
- **读数**（MSVC cl.exe 14.51 / MD Release，真实窗口 + 真实 GDI 后端 + 真实字体度量）：

| 步骤 | 读数 |
|---|---|
| ① 默认关（现状） | preferred = **1463×14** 单行（长消息整串流出——正是 #52 挂账的痛点形态）；整串 1 条 DrawText |
| ② 开 wrap + AutoSize | **6 行、高 84**（气泡长高 6 倍）；宽 = 气泡宽 260 |
| ③ 逐行绘制 | 6 条 DrawText、y 步进**精确 = 行高 14**、每行宽 ≤ 气泡宽 + 禁则松弛 |
| ④ 宽度动态 | 260px=6 行 → **160px=10 行** → 复原 260px ⇒ 行数复原 |
| ⑤ 关回 wrap | 回到单行 14（开关可逆——消费者可灰度） |

- **E2E PASS（0 failures）**。

### 12.4 A1–A5 全判（收口判定表）

| 项 | 判据（需求 §5） | 结果 |
|---|---|---|
| **A1** | 存量全绿（默认关 ⇒ 既有用例零回归） | ✅ **349/349 五链**（335 存量 + 14 新增全绿；存量用例零改动） |
| **A2** | 像素等价 + **旧路径保留**（结构判据） | ✅ T29-4 命令流逐字段 = Phase 28 基线（三条命令形状 + 四字段）+ **零 FitText**（批二计数用例 + 批三终判双重锚）；命令流一致 + 后端确定性 ⇒ 像素等价 |
| **A3** | wrap 正确性（拉丁按词 / CJK 逐字 / 混排 / 超长无空格词 / 恰好贴边 / 空串与单字符；**每行一条 DrawText**） | ✅ T29-1/2/5 + T29-WRAP-1/2 全覆盖；RecordingBackend 命令流逐行断言；禁 RenderCommand 语义扩张未违反 |
| **A4** | 性能有界 + 宽度动态 | ✅ **`FitText` 调用数 == 行数**（T29-6 ②——每行恰一次 fit、扫描状态不重扫）；同宽零重建（T29-WRAP-3）；宽度 300→5 行/250→6/400→4 的直测形态 = T29-3 + T29-6 ④；布局零 substr（行 = 码点区间） |
| **A5** | harness 气泡端到端（消费闭环） | ✅ §12.3 库外探针 E2E PASS + Release 库已交付前缀；harness gui 侧启用 = 一行改动（留 harness 会话） |

### 12.5 收口核对（全链）

- 公共头 **94 → 94**（`TextMeasurer.h`/`TextWidget.h`/`PaintContext.h` 扩员不加文件）；公共 API **+4**（详设原预算 +3，超 1 项 = E6 `PaintContext::GetTextMeasurer()`，如实记账）；**CMake 0**（GLOB 自动入库）；用例 **335 → 349**（+14：批一 4 + 批二 6 + 批三 4）。
- **批次流水**：批一 `cdafbcf`（FitText 三链）→ 批二 `9be3e7a`（wrap 路径）→ 批三（本节 + 收口提交）。
- **遗留（不阻塞收口）**：① harness gui 侧气泡启用 wrap = 一行 `SetWordWrap(true)`（harness 会话）；② **O1** 行内对齐（#51③ 消费 `Line.width`）/ **O2** TextBox 编辑器内 wrap（独立挂账）/ **O3** 禁则字面集扩充（按消费者反馈）/ **O4** GDI 每调用 UTF8ToWide（量级可接受）/ **O5** GDI 回退后补测（已在 emitLine 收敛为「仅回退时一次」）。
- **环境教训（如实记档）**：① **DPI 舍入缺陷会随显示环境迟到曝光**（E8——用户切缩放 100% 揭出批一潜伏缺陷）；② 改公共头布局后**必须全量重建**（批二教训）；③ MSVC 生成器产物在 `Release/` 子目录——跑错路径 = 跑了陈旧二进制。

---

## 9. 修订记录

- **v1.4**（2026-10-07）**批三实施回填 + ✅ 全链收口**（五链 349/349，A1–A5 全判）。★ **一条实施勘误（E8——批一潜伏缺陷被揭出）**：GDI FitText 的 DIP→像素换算 `ceil` 超顶 DIP 限宽（limit=26.25 ⇒ 27px 放行 ⇒ 违反「width 恒不超 maxWidth」）；**修复 = 截断 floor**（`px ≤ floor(limit·dpi/96) ⟺ DIP ≤ limit`——任意 DPI 精确相等）；**发现经过 = 用户指出主屏缩放已切 100%**（此前 125% 时违规区未被采样踩中 ⇒ 假绿；测试无错，DPI 变化揭了它）。★ **批三用例**：T29-4（命令流逐字段 = 手工 Phase 28 基线）、T29-5（退化边界含 E7 守卫锚「宽 < 一码点不挂死」）、T29-WRAP-2（微超链悬挂、不吞普通字符）、T29-6（气泡端到端 + **A4 线性判据 = FitText 调用数 == 行数**）。★ **A5 交付**：Release 库装 harness 前缀 + **库外气泡探针 E2E PASS（0 failures）**（单行 1463×14 → wrap 6 行 → 动态可逆）。★ 用例 345 → **349**（+4）；API 终值 **+4**（超预算 1 = E6）；公共头 94→94；CMake 0。**遗留** = harness gui 一行启用（harness 会话）+ O1–O5。
- **v1.3**（2026-10-06）**批二实施回填**（wrap 路径落地——五链 345/345 全绿）。★ **四条实施勘误**（§11.2）：**E4 禁则命名对调**（首版 ④ 调 `IsLineStartForbidden(cps[b-1])` ⇒「行尾禁开括号」被写成「行尾禁闭标点」；靠 T29-2 段落 C 断言发现并修正——**函数名按「禁则作用的位置」读**）· **E5 行首禁则回提上界 = `segEnd`**（取 `hardEnd` 会让硬边界永远赢、规则形同虚设；超宽 = 悬挂链 advance = D29-Ⅱ 微超）· **E6 `PaintContext::GetTextMeasurer()` 新增 ⇒ API 超预算**（wrap 绘制需要 `FitText` 可达的测量器；选「交出引用」而非「转发 FitText」——**公共 API 合计 +4，超详设 +3 一项，如实记账**）· **E7 空段/推进守卫**（`b == lineStart` ⇒ 强制一字、`nextScan <= scan` ⇒ 强制 +1——堵**行宽 < 一码点时的 Paint 死循环**）。★ 批二实测（§11.3）：拉丁 6 行按词 + 空格消费 · CJK 闭标点**悬挂进前行**（`你好）` / `世界`，微超 = 一个标点 advance）· 开括号不独占行尾 · `A\n\nB` = 3 行 2 命令（空行占高零命令）· 同宽 SetSize 零 `FitText` 调用 · **默认关零调用 + 单条 DrawText**（A2 判据前半）。★ **环境教训**：MSVC Release 出现确定性 segfault（虚调用打在失效 `this`——`mov (%rcx),%rax` + 栈上 `std::string` 临时），**根因 = 增量链接的陈旧 exe**（公共头加成员 ⇒ 与旧 `ECDI.lib` 布局不一致）；干净全量重建后全绿 ⇒ **改公共头布局后必须全量重建**。
- **v1.2**（2026-10-06）**批一实施回填**（FitText 三链落地——五链 339/339 全绿）。★ **三条实施勘误**（§10.2）：**E1 `FitText` 非 const**（原稿 `const` 编译不通过：默认体须调非 const 的 `MeasureText`，`const` 版只能 `const_cast` ⇒ 对写缓存的 `GDITextMeasurer` 是 UB；改非 const 零调用代价）· **E2 `MeasureText` 循环体提取为 `Impl::AdvanceOf`**（两方法共用是**逐位一致**的前提——T29-FIT-3 锚定 192.0000==192.0000）· **E3 码点总数取法**（`CodepointIndexToByteOffset(…,SIZE_MAX)` 返回**字节数**而非码点数——超界钳制语义；改用既有 `ByteOffsetToCodepointIndex(text, text.size())`，**零新增 API**）。★ 批一实测（§10.3）：FT fitCp 单调 + width 与 MeasureText 逐位一致；GDI `A😀B` ⇒ 3/2/1 逐档代理对不拆；GDI 原生 vs `MeasureText` oracle 逐档同。★ 批一验收 = 六个既有 `TextMeasurer` 派生类**零改动编译通过**（盯防⑦）。API 批一 **+1**（总预算 +3）。
- **v1.1**（2026-10-06）评审吸收（**Conditional PASS → 两修当场修毕 ⇒ PASS → Implementation**）。**架构方向全部认可**（15 项边界表全 ✅；上轮六 🟡 全闭环）；评审定位 =「进入详设只需把算法边界、FitText 精确语义、缓存失效和测试样例继续钉死」并预警 **Phase 29 是 ECDI 第一次真正给 TextWidget 增加文本布局能力**（边界影响将来富文本/shaping/Bidi）。评审强 PASS 项：TextLayout 用 startCp+cpCount 而非 string（布局零 substr/零拷贝——「preferred=5 行实绘=6 行」恶疾被架构消灭 = **强 PASS**）；默认关 = 结构级零回归（T29-4 调用计数判据）；FitText 语义未降级成 glyph service。**两项必修（当场修毕）**：① **D29-Ⅰ 状态变量三分化**（`lineStart`/`lineEnd`/`scan` 语义互斥——原稿「scan」双含义是 off-by-one/跳空格错误根源）；② **行宽来源统一**（D29-Ⅰ ⑦ 的 `MeasureText(切片)` **删除**：FT = 扫描期 advance 累积/`FitText.width` 直接采用、回退后逐码点 memo 求和；GDI = 无回退用 `fit.width`、回退后补测一次归 O5——**禁止对每行再调 MeasureText**）。**强烈建议吸收（三条）**：③ **O(n) 契约范围明确**（**仅限 FT/advance-memo 路径**；GDI `UTF8ToWide` 重复转换 = 已知性能债务 O4 非正确性契约——概念冲突消解，C29-9）；④ **TextLayout 指纹显式列键**（五元组 + `GetTextLayout()` 入口每次判定——不依赖 setter 记得清，C29-10）；⑤ **T29-2 措辞受 maxWidth 约束**（「理想同行」仅宽度允许时；必须换行时禁闭标点独占行首——四段式消歧）。**测试扩展采纳（评审 §22）**：+T29-FIT-1（UTF-8 码点边界）/ +T29-FIT-2（代理对——含 😀 / A😀 / 😀B 三边界样本增强）/ +T29-WRAP-1（连续换行空行）/ +T29-WRAP-2（禁则微超）/ +T29-WRAP-3（同宽不重建）——**用例预算 341 → 346**。**非阻塞采纳**：批一验收 = **所有既有 TextMeasurer 派生类编译级验证**（盯防⑦）；FT 路径避免每行二次 MeasureText（= 必修②同源，盯防⑧）。未采纳：无（O4 不扩大范围——评审 §5 同意）。**评审路线图**：28 文本成本 → 29 多行布局 → #51 行对齐 → TextBox wrap → shaping/bidi/富文本（健康演进不一锅端）。
- **v1.0**（2026-10-06）初稿。**输入**：初设 v1.1（评审 PASS「比较明确的 PASS」，15 项全 ✅）+ 评审详设必答 ①–⑦ + 代码勘察（`TextStyle = {foreground, font}` 无 padding · `Core/UTF8.h` 公共换算 util · `CalculateTextPosition` 基类左对齐+垂直居中 · FRAMEWORK_SOURCES = GLOB_RECURSE ⇒ 零 CMake · GDI MeasureText 脚手架批三 fit 先例）。**七题钉死**：D29-Ⅰ 断行状态机（流水线序 = fit → 软断点回退 → 行尾禁则 → 行首禁则 → 空白终化；冲突优先级固定）· D29-Ⅱ 禁则微超 = 禁则链 advance（普通字符不得吸收）· D29-Ⅲ FitText 契约（相对码点数）· D29-Ⅳ GDI surrogate 三步换算 · D29-Ⅴ 连续 `\n` 空 TextLine · D29-Ⅵ FT O(n)（WrapEngine 持扫描状态）· D29-Ⅶ 默认体 = fallback。△1–△9（零新文件/零 CMake）· C29-1..8 · 盯防 6 · 用例 11 条（335 → **346**）· 三批 · **API +3**（FitText + SetWordWrap/IsWordWrap 对——★ 需求稿「+1 倾向」只计测量原语，wrap 开关对漏计，如实修正）· 94→94 / CMake 0 / 风险 中。
