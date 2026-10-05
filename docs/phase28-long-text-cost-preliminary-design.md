# Phase 28 · 长文本的测量与绘制成本 —— 初步设计（v1.0）

> 来源：需求稿 `phase28-long-text-cost-requirements.md` **v1.1（评审 PASS → 初设）**——评审三条红线 + 初设执行顺序（K8 Spike 0 → D2 → D1 → D3 → D4 → D5）逐项兑现。
> 状态：**v1.0**（2026-10-05）待评审
> 定位：**方案冻结稿**——★ **K8 Spike 0 已完成**（§1，探针实测；红线 1 兑现），D2 四步裁决逐题作答（§1.4），D1/D3/D4 落成逐文件方案（§3）。

---

## 1. ★★ K8 Spike 0 结论（探针实测 2026-10-05——红线 1「先 K8 后优化」兑现）

### 1.1 探针与语料

- 探针：`.workbuddy/spike/textcost/measure_spike.cpp`（库外不入仓库，沿 △7 / `.workbuddy` 先例）；MinGW **Debug + Release 双跑**——Debug 数据仅用于暴露构建敏感性（FT 是纯 CPU 路径、Debug 膨胀 ~4×：miss max 102ms@7.4KB；GDI 是 Win32 API 调用、两侧几乎不变），**以下读数均以 Release 为准**。
- 语料：从 harness `realistic-session.jsonl` 重建的**真实测量序列**——937 个文本版本去重后 **452 个唯一版本 / 459KB / 最长 7.7KB**（ASCII ~79% + CJK ~5%、无 emoji）。

### 1.2 读数（Release MinGW）

| 项 | GDI 链 | FreeType 链 |
|---|---|---|
| miss（452 次）avg / max | 309µs / 6.8ms | **2755µs / 23.7ms** |
| miss 单位成本 | **64ns/字符** | **374ns/字符 = 5.8×** |
| hit（452 次）avg | 0.54µs | 0.55µs |
| 命中 O(len) 键分量 | 0.85µs@7.7KB（vs 0.07µs@15B） | 同量级（0.93µs） |
| 冷启动首调 | 9.2ms | 1.2ms |

- ★ **交叉验证**：452 次 FT miss × 2.76ms ≈ **1.25s ≈ harness GL 链总耗时 1.53s**（差额 = 重复版本 hit + LineHeight）——探针与消费侧 checkup 数据互证，miss 主导构成确认。
- ★ 修正一处我方先前的错误假设（需求稿勘察 K3 的推测）：`FontEngine` **有**整串测量缓存（与 GDI 同形），所以 3× 差距**不在命中路径**，全在 miss 路径。

### 1.3 K8 两问的答案

1. **124ms 峰值 ≠ 稳态单次测量成本**：Release 下 GDI miss max 6.8ms、FT miss max 23.7ms，124ms 无法由稳态解释。最可能构成 = **冷启动/一次性事件**（GDI 首调 9.2ms 本机；harness 在窗口 DC + 字体替换 + DPI 虚拟化下首调可再放大）± Debug 类构建的 FT 长串 miss（102ms@7.4KB，若其 install 为 Debug 库）。**处置：不作为设计对象**——验收已按红线 2 用机制 + 总量口径（A3-1..3 / A3-4 记录制），峰值只记录不复现。
2. **GL/GDI 3× 差距根因 = FT miss 逐码点全量 `FT_Load_Char` 且无 per-glyph memo**：① 每码点一次完整 load（cmap 查找 + outline load + hinting）；② **7KB 文本 ~2000 码点中仅 ~475 唯一 ⇒ 重复字形反复全量 load**（miss 贵的本质）；③ GDI 侧一次原生 `GetTextExtentPoint32W` 整串摊销，64ns/字符已近底层底线。

### 1.4 D2 四步裁决（评审 §5 逐题作答）

- **D2-A（miss 慢在哪）**：如上——逐码点全量 load + 重复字形无 memo；**hinting 不是主要可消除项**（见 D2-B 时序：advance-only 只省 20-30%）。
- **D2-B（advance 等价性）**：`ADVANCE_ONLY|NO_BITMAP` vs 现行旗标（`DEFAULT|TARGET_NORMAL|NO_BITMAP`）——**0 mismatch / 475 码点 × SimSun + MSYH 两 face @px18**（裸 `ADVANCE_ONLY` 亦 0 mismatch）。
- **D2-C（误差边界）**：无需定义——等价在本语料严格成立；但公共路径仍不依赖它（见 D2-D）。
- **D2-D（裁决）**：**主修法不走 advance-only，走 per-glyph advance memo**（缓存**现行旗标**的 advance）——比 advance-only 更彻底（重复字形零 load）、天然满足 Phase 26 同旗标纪律、像素等价结构性成立；advance-only 降级为 memo-miss 首载的**可选微优化**（+20-30%，等价已证，PD28-4 默认不做）。

### 1.5 对 A3-4 的诚实 re-scope（红线 2「机制/目标分开」的落实）

GDI 链 miss 已近原生底线（64ns/字符）⇒ **测量成本问题本质是 FT 链问题**；GDI 链总耗时已接近该架构地板，harness 侧 GDI 0.54s 的下降空间主要来自其调用构成而非框架。**≥10× 目标只对 FT 链承诺**（memo 后 miss 5-10×，总耗时投影 1.53s → 0.2-0.3s）；GDI 链 = 机制收益 + 无回归。

---

## 2. 代码勘察（B1–B10，全部带行号实测 2026-10-05）

- **B1** `Widget::AutoSize`（`Widget.cpp:231-244`）：先 `GetPreferredSize()` 再判同尺寸 no-op。
- **B2** `TextWidget::GetPreferredSize` / `DoMeasureText`（`TextWidget.cpp:118-141`）：无结果缓存，每次直落 `MeasureText` + `LineHeight`。
- **B3** `TextWidget::SetText` 双重载（`TextWidget.h:33-35`）——文本变更的**唯一入口**（revision bump 落点）；`TextBox::SetText` 覆写但其 `GetPreferredSize` 返回当前尺寸（v1 不参与 AutoSize），不消费本缓存。
- **B4** `FontEngine::MeasureText` 逐码点循环（`FontEngine.cpp:211-250`）：`FT_Load_Char` 每码点调用、**无 per-glyph memo**；命中走整串缓存（键 `(text,size,family,dpi)`）。
- **B5** `PixelSize`（`FontEngine.cpp:118-126`）：`llround(size·dpi/96)`（14 DIP @120 = 18px）——memo 键的 px 口径。
- **B6** 缓存上界策略（`FontEngine.cpp:252-254`）：满 4096 全清、无 LRU（详设 §9 O9 口径）——memo 沿用同策略。
- **B7** `TextWidget::OnPaint`（`TextWidget.cpp:110-117`）：`ctx.DrawText(...m_text...)` **单条整串命令**——80.1ms 尖峰的发射点。
- **B8** `GDIBackend::DrawText`（`GDIBackend.cpp:374-396`）：`UTF8ToWide` + `TextOutW` 整串；后端自有 clip 栈（PushClip/IntersectClipRect）——**clip 前缀截断的实施点**。
- **B9** `TextBox` 已可视行逐行绘制（`TextBox.cpp:1278-1282`，`firstVisible..lastVisible` × `m_lineStarts`）——**零改动对照**（需求稿 v1.1 对评审 §10 的事实修正）。
- **B10** `Window::GetDpiScale` 公共（`Window.h:105`）+ `TextWidget::ResolveMeasurer` 经 `GetWindow()`——**指纹的 DPI 可零新 API 获取**。

---

## 3. 方案（PD28-1..PD28-4）

### PD28-1 ★★ FontEngine per-glyph advance memo（FT miss 主力——最大杠杆）

- `FontEngine::Impl` 增 `advanceCache`：键 `(FaceId, px, glyphIndex)` → **现行旗标**下取得的提示后 advance（26.6 定点）；`MeasureText` 循环改 `gid = FT_Get_Char_Index(face, cp)` → 查 memo → miss 才 `FT_Load_Char` 并回填。
- 上界 **8192、满清**（与 glyphCache/measureCache 同策略，B6/O9 口径）；FaceId + px 进键 ⇒ 字号/face 变化天然失效。
- **像素等价结构性成立**：memo 命中返回与现行为**同旗标同值**的 advance、求和顺序不变 ⇒ 测量结果逐位一致（T28-1 命令流快照 + T27-10 口径复跑兜底）。
- 预期：重复字形占 ~76% 的长文本 miss **降 5-10×**（23.7ms → 2.5-4ms@7.4KB 外推）。

### PD28-2 TextWidget 指纹短路（D1/D3 落位——TextWidget 层闭环，不做 Widget 通用缓存）

- `TextWidget` 增 `mutable size_t m_textRevision`（B3 `SetText` 双重载 bump）+ `mutable bool m_prefValid` + `mutable Size m_prefCache` + `mutable PrefKey{revision, font, dpi}`。
- `GetPreferredSize`：measurer 可达且 key 匹配 ⇒ **直接返回缓存**（零测量调用、零键拷贝——A3-1/A3-2 机制兑现）；miss → `DoMeasureText` + 回填。
- DPI = `lround(GetWindow()->GetDpiScale() * 96)`（B10，零新 API）；**无窗口（无头测试）⇒ 不短路**、退回现行为（可测性不受影响）。
- font 进键 = 值比较（family + size，`Font` 是值类型）。
- 评审 §4 的「Widget 层通用缓存」**明确不做**——非文本 Widget 的输入/子树失效/constraint 进键一整类问题不进本 Phase。

### PD28-3 GDI `DrawText` clip 前缀截断（D4/#10 落位——**后端执行侧**，命令流零改动）

- `GDIBackend::DrawText`（B8）：`TextOutW` 前用自有 clip 栈求**水平可视跨度**；跨度 ≤ 0 ⇒ 整条跳过；文本 extent ≤ 跨度 ⇒ 整串照旧（快路径，常见 case 零开销）；否则 `GetTextExtentExPointW(maxExtent = 跨度)` 求 fit 数 + **+1 守卫字符**后只 `TextOutW` 前缀。
- **像素等价论证**：① TextOut 内字形定位 = 累积 advance，前缀与整串的前缀部分**逐位同位**（框架无 shaping/kerning——冻结边界）；② 第 fit+1 个守卫字符覆盖跨界字形，其余越界部分仍被**活动裁剪区**裁掉 ⇒ 可见像素逐位等价（T28-4 memory-DC 探针逐位验证，含跨界字形边界样本）。
- **命令流零改动**：widget 层与命令层完全不动 ⇒ 全部既有 `RecordingBackend` 用例天然回归；GL 后端零改动（图集路径 5.1ms 已达标）；TextBox 零改动（B9）。
- **#10 处置**：本项 = 其「PushClip/clipRect 替代字符串截断」在后端执行侧的兑现；收口时对账剩余范围（TextBox TODO 部分经 B9 确认已不存在）。

### PD28-4（可选批内项，默认不做）advance-only memo-miss 首载

- D2-B 已证等价（0/475 × 2 face）；memo miss 首载可换 `ADVANCE_ONLY|NO_BITMAP`（+20-30%）。**默认不启用**（收益小、多一条一致性假设面）；仅当批一实测 memo 命中收益不足时启用，启用须带等价回归用例（D2-C 届时补量化边界）。

---

## 4. 用例（T28-1..T28-6，330 → ~336）

- **T28-1** advance memo 一致性：长文本（含高重复码点）开关 memo，两次测量/绘制命令流逐字节一致（RecordingBackend 快照）。
- **T28-2** advance memo 上界：>8192 唯一字形灌入 → 满清后测量仍正确（沿 T26 缓存上界先例）。
- **T28-3** TextWidget 指纹：文本/字体/DPI 各自变更触发重测；无变化命中**零测量调用**（measurer miss 计数缝验证——`MeasureCacheMissCount` 已存在，GDITextMeasurer.h:42 同款）。
- **T28-4** GDI clip 前缀像素等价：memory-DC 探针（GetDIBits），部分可见 Label 的前缀截断 vs 整串绘制逐位一致（含跨界字形边界样本）。
- **T28-5** GDI clip 前缀三边界：完全不可见 / 完全可见 / 恰好贴合。
- **T28-6** 长文本回归锚：7KB Label measure + draw 全链命令流快照。

---

## 5. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | PD28-1 advance memo + T28-1/2 | 三链全绿 + 命令流逐位零回归 |
| 批二 | PD28-2 指纹短路 + T28-3（PD28-4 视批一读数） | 三链全绿 + miss 计数用例 |
| 批三 | PD28-3 GDI clip 前缀 + GDI 像素探针 + T28-4/5/6 | 三链全绿 + 像素等价 |
| 批四 | harness 侧复跑对照（A3 读数）+ 文档回填收口 | A3-1..3 全过 + A3-4 记录制 |

---

## 6. 影响面（初设校准后）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94** |
| 公共 API | **+0**（DPI 走 `GetWindow()->GetDpiScale()`——B10；PD28-4 启用也不加 API） |
| 用例 | **330 → ~336**（+6） |
| CMake | **0 改动** |
| 风险 | **低**（较需求稿下调：两处像素等价均有结构性论证——memo 同旗标同值同序、前缀 + 守卫字符 + 活动裁剪；最大残余风险 = T28-4 发现边界像素差 ⇒ 回退方案 = 守卫 +2 或该分支整串回退） |

---

## 7. 修订记录

- **v1.0**（2026-10-05）初稿。**输入**：需求稿 v1.1（评审 PASS）+ **K8 Spike 0 实测**（`.workbuddy/spike/textcost/measure_spike.cpp`，Debug + Release 双跑，语料 = realistic 夹具重建的 452 唯一版本 / 459KB）+ 代码勘察 B1–B10。**K8 两问作答**：124ms 峰值 = 冷启动/一次性事件不作为设计对象；3× 差距 = FT miss 逐码点全量 load 且无 per-glyph memo（374 vs 64 ns/字符 = 5.8×；7KB 文本 ~2000 码点仅 ~475 唯一）。**D2 四步裁决**：D2-B 等价 0 mismatch（475 码点 × SimSun/MSYH @px18）；主修法 = **per-glyph advance memo**（存现行旗标 advance）而非 advance-only（后者仅 +20-30%，降级为可选 PD28-4）。**方案**：PD28-1 memo（FT miss 5-10×）· PD28-2 TextWidget 指纹短路（DPI 走 `GetDpiScale` 零新 API）· PD28-3 GDI clip 前缀（后端执行侧、命令流零改动、+1 守卫字符像素等价）· PD28-4 可选。用例 T28-1..6（330 → ~336）；四批；影响面 94→94 / API +0 / CMake 0；**A3-4 re-scope：≥10× 只承诺 FT 链**（GDI 已近地板——红线 2 落实）。
