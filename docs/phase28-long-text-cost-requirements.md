# Phase 28 · 长文本的测量与绘制成本 —— 需求稿（v1.0）

> 来源：外部消费者 `ECDI_fake_harness` **2026-10-05 体检报告**（三笔基建 + checkup 全指标数据）。★ 本稿立项前已对报告做**逐项核验**：工件真实（`protocol/fixtures/realistic/gen_realistic.py` + `evals/checkup-*.json` 11 件 + `--cull-probe` 探针套件）、数字逐项对 JSON 一致、账本引用（#10/#21）与原文相符——核验结论与纪律记录见详设（Phase 27）§10.3 的同族先例。
> 状态：**v1.0**（2026-10-05）待评审
> 定位：**性能需求**（消费侧数据拉动立项，deferred **#50**）——Phase 27 剔除解决「命令**条数**」，本 Phase 解决**「单条长串的成本」**与**「重复测量的成本」**。

---

## 1. 背景

### 1.1 问题的精确定义

Phase 27 收口后，视口外子树已零命令（2000 行场景 −98.9%）；但体检暴露了两个**剔除管不到**的成本：

1. **测量链**：autosize 模式下，一段 6 秒回放的文本测量累计 **0.54s（GDI）/ 1.53s（GL）**，单次峰值 **124ms**——超过一个 tick（33ms）的 3.7 倍。结构根因初步定位在 `AutoSize` 的调用形态（K1/K2）与缓存命中/失效路径（K3/K4），精确构成待初设勘察（K8）。
2. **绘制链（GDI）**：剔除削减的是命令*条数*，但**一条可见命令可以携带 7KB 文本**——GDI `TextOutW` 整串处理把单帧顶到 **80.1ms**（`maxExecuteUs=80146`，超过 33ms tick = 掉帧）；GL 同场景 5.1ms（字形图集已解）。

### 1.2 基线数据（`ECDI_fake_harness/evals/checkup-*.json`，2026-10-05，数字已核验）

| 场景 | 链 | 测量调用 | 测量字符 | 测量总耗时 | 单次测量峰值 | 命令/帧 avg | 执行 avg | 执行 max |
|---|---|---|---|---|---|---|---|---|
| realistic autosize | GDI | 1658 | 899,146 | 0.54s | **124ms** | 217 | 13.8ms | 35.0ms |
| realistic autosize | GL | 1569 | 892,485 | **1.53s** | 67ms | 212 | 3.8ms | 24.0ms |
| realistic list | GDI | 405 | 6,575 | 37ms | 11.1ms | 153 | 19.9ms | **80.1ms** |
| realistic list | GL | 405 | 6,561 | 7.6ms | 0.5ms | 153 | 1.2ms | 5.1ms |

- 夹具：realistic（种子确定性）220 块 / 717 deltas / 1213 消息 / 317KB——Markdown 内容、最长 7KB 的 tool 输出、200+ 字符单行、真实 ts 节奏；`validate.py` 全绿。
- ★ **调用数与字符量两侧几乎相同**（1658 vs 1569 次；899K vs 892K 字符）⇒ GL/GDI 的 3× 测量差距在**单次成本**（miss 或命中路径），不在调用量——K8 根因勘察的判别数据。
- GL 执行端全场景 **4–17×**（1.0–3.8ms vs 7.1–19.9ms）；GDI 存在 ~8ms 帧地板且与命令数无关（demo 166 条与 scale 147 条同为 ~8ms）——瓶颈在整客户区 BitBlt/呈现，属后端形态（#44 记账域），**不在本 Phase 范围**。
- sweep 确认 Phase 27 剔除在 realistic 全部滚动位置稳定（157 avg / 164 max，持平）。

### 1.3 与相邻条目的边界（互补不重复）

| 条目 | 关系 |
|---|---|
| **#10**（文本裁切裁剪区域——`PushClip`/clipRect 替代字符串截断，Phase 8 表，TextBox TODO，「O(n²) 截断 → 裁剪区域」） | **本 Phase 预期消费**：绘制端抓手的行级方案与 #10 同题；是否整条出清待初设裁决（D4） |
| **#21**（脏区合帧） | 受益不实现——本 Phase 不做脏区 |
| **#33**（按需建控件 / 虚拟化） | 互补：#33 管**控件数与内存**（不建），本 Phase 管**单帧构建/执行/测量成本**（建了但便宜） |
| **富文本（styled spans）** | **非目标**——夹具中的 Markdown 是内容多样性，不是渲染需求；富文本另立项（见 §3.2） |
| Phase 26（FreeType 文本栈 + GL） | 已完成的地基：字形图集 / 两侧测量缓存 / 测量-绘制同旗标纪律 |
| Phase 27（视口剔除） | 已完成的前置：`PaintContext::IsRectVisible` 构建侧累计裁剪交集——**行级消费是其自然延伸**（K6） |

---

## 2. 现状勘察（K1–K8，全部带行号实测 2026-10-05）

- **K1 AutoSize 先测后判**（`Widget.cpp:231-244`）：`AutoSize()` 先调 `GetPreferredSize()` **再**判「同尺寸 no-op」⇒ 每次 relayout 对未变化的控件也**全量测量**。
- **K2 `GetPreferredSize` 无结果缓存**（`TextWidget.cpp:118-124` → `DoMeasureText:136-141`）：每次调用直落 `measurer.MeasureText(font, m_text)`——harness 每消息 `Arrange` + 逐行 `AutoSize`（`App.cpp:630,652`），717 deltas ⇒ 重复测量全量落到 measurer。
- **K3 两侧测量缓存同形、miss 成本差一个量级**：
  - GDI（`GDITextMeasurer.cpp`）：整串键 `(text,size,family,dpi)` 命中即返（连 `GetDC` 都跳过）；miss = `UTF8ToWide` + **一次** `GetTextExtentPoint32W` 原生调用。
  - FreeType（`FontEngine.cpp:211-256`）：**同样有整串缓存**（同键形态）；miss = `DecodeUtf8` + **逐码点 `FT_Load_Char`**（旗标 `FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP`，与渲染链同旗标——v1.1 纪律：测量/绘制异源会错位）。★ 同旗标约束**限制了单方面改旗标的空间**——是否可对测量走 `FT_LOAD_ADVANCE_ONLY` 类快路径而不破坏同源性，属初设必答（D2）。
- **K4 缓存上界与命中路径成本**：两侧上界同为 4096、**满即全清**（`FontEngine.cpp:252-254`「LRU 不做，见详设 §9 O9」；GDI `GDITextMeasurer.h:64`）⇒ 长文本场景（717 个增量版本的长串键）有清空重探风险；且键构造 `make_tuple(text, ...)` **每次调用拷贝整个字符串**、`std::map` 比较逐字节——命中路径本身 O(len)（~1200 字符均值 × 1658 次 ≈ 899K 字符的纯键管理开销）。
- **K5 绘制端整串发射**：`TextWidget::OnPaint` → `ctx.DrawText(m_text)` 一条命令携带全串；`GDIBackend::DrawText` → `TextOutW` 整串（`GDIBackend.cpp:374-396`）⇒ 被裁剪区裁掉的部分 GDI 照样整串排版。GL 走图集逐字形，被 scissor 裁掉的字形渲染便宜（同一命令 5.1ms vs 80.1ms 的根源）。
- **K6 Phase 27 基础设施可复用**：`PaintContext::IsRectVisible`（构建侧累计裁剪交集）目前只在 `Widget::Paint` **子树级**消费（`Widget.cpp:262`）——文本控件内部的**行级**消费是其自然延伸，也是与 #10 的交汇点。
- **K7 基线数据**：见 §1.2（全部对 JSON 核验）。
- **K8 ★★ 开题待勘察（初设第一优先级）**：
  1. **124ms 单次测量峰值的精确构成**（GDI 链也出现——`GetTextExtentPoint32W` 单次原生调用理论上远达不到，需 profile：是否含字体创建/首次 `GetDC`/测量探针计时口径/键拷贝+map 比较的极端场景）；
  2. **GL/GDI 测量总耗时 3× 的成因**（调用数与字符量已排除调用量因素——剩下的假设：FT miss 逐码点 hinting 成本 vs 原生单次调用；命中路径键管理；缓存清空重探频次——harness `measureCalls`/`measureChars` 字段可判别）。

---

## 3. 范围

### 3.1 做（倾向——三抓手）

1. **AutoSize / GetPreferredSize 结果复用**：文本/字体/DPI/宽度约束未变则不重测（指纹短路；顺带消掉 K4 的键拷贝成本——这是 autosize 测量耗时的大头候选）。
2. **长文本测量成本**：形态待初设（D2/D3）——miss 路径降本（advance-only 快路径探针 vs 同旗标一致性约束的裁决）、命中路径键管理（指纹前置/视图键）、缓存淘汰策略复核。
3. **绘制端长串成本（GDI 尖峰）**：文本控件**行级切分 + 行级剔除**（K6 的 `IsRectVisible` 行级消费；被裁剪的行不进入命令流）——与 #10 同题，预期消费其 TODO（D4 裁决出清范围）。

### 3.2 非目标

- **富文本渲染**（styled spans / 行内混排样式）——另立项；本 Phase 只管成本不管样式能力。
- **shaping / bidi / 复杂文种**——`FontEngine` 冻结边界明示不属文本栈。
- **#33 虚拟化**（控件数）/ **#21 脏区实现**（合帧）/ **GL 执行端优化**（已 4–17× 达标）/ **后端呈现层 8ms 地板**（BitBlt 形态，#44 记账域）/ **#46 取整口径**。
- 不改 `TextMeasurer` 公共接口语义（测量返回恒 DIP——Phase 20 契约）。

---

## 4. 待决点（倾向已给，待评审）

- **D1** ★ AutoSize 结果缓存的**键与失效语义**：放 `TextWidget` 层还是 `Widget` 层；指纹字段（文本指针/哈希、字体、DPI、宽度约束）；DPI 跨屏变化必须失效（对齐既有缓存键含 DPI 的口径）。
- **D2** ★★ **测量 miss 路径**（= K8 根因勘察，**初设第一优先级**）：先 profile 定构成，再裁决修法——`FT_LOAD_ADVANCE_ONLY` 类快路径与「测量/绘制同旗标」约束的冲突如何裁决（advance 一致性可否用探针证等价）。
- **D3** 命中路径的键拷贝消除：指纹前置短路（倾向）vs `string_view` 视图键（生命周期风险——文本由控件持有，需论证）。
- **D4** ★★ 绘制端**行级切分的像素等价约束**：切分后逐行发射必须与整串发射**逐字节一致**（T27-10 探针口径扩展到长文本）；#10 是否随本 Phase 整条出清（其 TextBox TODO 部分与本 Phase 行级方案的关系）。
- **D5** 判据冻结：§5 A3 的量化目标在初设冻结口径（夹具版本、探针字段、对照基线 = checkup-*.json）。

---

## 5. 验收方向（A1–A6，待初设细化）

- **A1** 存量 **330** 用例全绿 + 新增用例（AutoSize 复用命中/失效、行级剔除边界、长串回归锚）。
- **A2** **像素等价**：测量与绘制改动在两后端**逐字节零差异**（T27-10 探针口径扩展——缓存/切分不得改变任何像素）。
- **A3** 基准判据（harness realistic 夹具 + `--cull-probe` 复跑，前后对照 checkup 基线）：
  - autosize 测量总耗时**降一个量级**（≥10×）；
  - GDI 单帧执行尖峰 **< 33ms**（tick 内，不掉帧）；
  - 命中路径无 O(len) 键管理开销（指纹短路生效）。
- **A4** 对账（D27-E 式）：存量调用路径零回归——缓存复用不得改变任何既有测试的命令流。
- **A5** GL 链不回归：执行端 4–17× 保持、T27-10 像素等价复跑 PASS。
- **A6** 五处台账同步 + harness 侧复测报告（消费方闭环，沿 Phase 27 先例）。

---

## 6. 影响面预算（初估，待初设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（缓存/指纹全部内部实现细节） |
| 公共 API | **+0**（倾向；若 D2/D3 需要观测缝则 +1，待初设） |
| 用例 | **330 → ~336**（+6 倾向） |
| CMake | **0 改动** |
| 风险 | **低～中**——缓存失效语义（DPI/文本变更）+ 像素等价约束（D4）；测量侧有「同旗标一致性」既定纪律兜底 |

---

## 7. 修订记录

- **v1.0**（2026-10-05）初稿。**输入**：`ECDI_fake_harness` 2026-10-05 体检报告（三笔基建 = PaintProbe 探针套件 + MeasureProbe + realistic 确定性夹具；`evals/checkup-*.json` 11 件基线——**工件与数字已经本仓库逐项核验一致**）。勘察 K1–K8 带行号（AutoSize 先测后判 `Widget.cpp:231-244` · 无结果缓存 `TextWidget.cpp:118-141` · 两侧整串缓存同形而 miss 成本差量级 · 4096 满清无 LRU · 整串 DrawText/TextOutW · `IsRectVisible` 行级延伸）；三抓手 / 五待决 / 六验收 / 影响面（公共头 94→94 · API +0 · 用例 330→~336 · CMake 0）；deferred **#50** 立项登记（roadmap v1.81）。
