# Phase 28 · 长文本的测量与绘制成本 —— 详细设计（v1.1）

> 来源：初设 `phase28-long-text-cost-preliminary-design.md` **v1.1（评审 PASS → 详设，13 项全 PASS）**——评审给定**详设必答六冻结点**（初设 §3.5）逐题钉死（本稿 §1）。
> 状态：**v1.1**（2026-10-05）——**✅ 评审通过（PASS → Implementation，「可以开工了」）**（外部评审 2026-10-05：设计成熟度高 / 实现风险低～中低 / **不需要再开设计迭代**；六冻结点全部认可为 C28-1..7 契约；**两条非阻塞建议已吸收**：① T28-1 强化 = 增加 **advance memo 真命中**断言（`AdvanceMemoMissCount` 观测缝，沿既有 `MeasureTextCacheMissCount` 先例——观测缝落内部件 ⇒ **API +0**，v1.2 勘误）；② **`GetTextExtentExPointW` 失败 ⇒ 回退原路径整串 `TextOutW`**（不是 return——「测量失败 ≠ 文本不可见」）；一条顺手项（`pos.x ≥ clip.right` 早退——实现时可做、非设计要求）；一条平台注记（`long` 宽度——保持现状不扩范围）；吸收明细见 §9 v1.1）
> 定位：**实施规格**——方案已在初设冻结（PD28-1/2/3 + PD28-4 可选），本稿落成**逐文件 △、契约 C28、盯防、用例落位与精确批次**。

---

## 1. 详设必答（初设 §3.5 六冻结点逐题钉死）

### D28-A ★ advanceCache 数据结构 + FaceId 生命周期契约（冻结点 1）

- **类型**：`std::map<GlyphKey, long> advanceCache`——**直接复用既有 `GlyphKey`**（`FontEngine.h:29-41`：`{FaceId faceId, uint32_t glyphIndex, int pixelSize, uint8_t hinting}`，`operator<` 已实现）——与渲染链 glyphCache 同键型，**零新键类型**。
- **值语义**：`long` = 成功 load 下 `face->glyph->advance.x >> 6`（与现行为**同一取整口径**——FontEngine.cpp:243）；缺字形（`FT_Load_Char != 0`）= 哨兵 **`kMissingAdvance = LONG_MIN`** ⇒ 循环跳过不计（与现行为逐位一致，且重复缺字形也零 load）。
- **FaceId 生命周期契约（评审 §17）**：`nextFaceId` **单调递增、永不复用**（`FontEngine.cpp:48` **既有不变量**——faces 按解析后路径缓存、facesById 反查、familyCache 含失败缓存）；face 生命周期 = FontEngine 析构（FaceOf 恒有效）。⇒ `(FaceId, px, glyphIndex)` 键**结构上不可能**错误命中旧 face 的 advance。**升格为契约 C28-2 + 盯防 ②**：未来若引入 face 卸载/重载，必须同步失效 advanceCache。
- **px 进键**：`PixelSize(font)`（B5 口径）——字号/DPI 变化天然失效；`hinting` 进键（恒 Normal，与 GlyphKey 同形）。
- **上界**：`kMaxAdvanceCache = 8192`、满清（与 glyphCache/measureCache 同策略，O9 口径；不引入 LRU——评审 §18 认可）。
- **观测缝（v1.1 吸收评审建议①）**：Impl +`advanceMemoMissCount`，FontEngine 增只读 `AdvanceMemoMissCount()`——**沿既有 `MeasureTextCacheMissCount()`（同文件 ：201）与 `GDITextMeasurer::MeasureCacheMissCount`（GDITextMeasurer.h:42）同族先例**；供 T28-1 断言「第二次测量零 FT load」（结果一致 + memo 确实生效）。★ **v1.2 勘误**：FontEngine 为**内部件**（`src/Render/FontEngine.h`，不入公共头计数）⇒ **公共 API +0**（v1.1 误记 +1）。
- **平台注记（评审 §2，不扩范围）**：值类型 `long` 在 Windows 为 32-bit（与现有代码一致）；Linux x86-64 下为 64-bit——语义无碍，**保持现状**，跨平台时随统一整型纪律一并处理。

### D28-B ★ TextWidget PrefKey 精确契约（冻结点 2）

- **键**：`(m_textRevision, m_style.font.value.size, m_style.font.value.family, dpi)` 四字段直接比较（不打包 struct、不引入 operator==——成员逐项比）。
- **revision**：`mutable std::size_t m_textRevision`——`SetText(const std::string&)` 与 `SetText(std::string&&)`（`TextWidget.cpp` 两处）各 `++m_textRevision`（**唯一文本入口**；TextBox 覆写 SetText 但**不消费**本缓存——其 preferred = 当前尺寸）。
- **font 进键 = 值比较**：`SetFont`/`SetStyle`/`ApplyTheme` 改字体**无需 bump**——键含 font 值，样式变更天然 miss（family 为短串，比较开销可忽略）。
- **dpi** = `lround(GetWindow()->GetDpiScale() * 96.0f)`（`GetDpiScale` 为 const——`const Window*` 直接可调，**无需 const_cast**）；跨屏变化 ⇒ 键失配 ⇒ 重测（DPI 失效语义与两链测量缓存含 DPI 口径一致）。
- **无窗口 ⇒ 不短路**（初设冻结、评审认可）：`GetWindow() == nullptr` 时退回现行为 `DoMeasureText(*measurer)`——无头测试体系（Phase 7.2 / FakeTextMeasurer 接缝）零污染。
- **命中行为**：**零 `MeasureText` 调用、零键构造拷贝**（C28-5；T28-3 用 `MeasureCacheMissCount` 差值验证）。
- 绘制期 `DrawTextContent` 的每帧 `MeasureText` **不在本 Phase 范围**——既有整串缓存命中 ~0.5µs 已近免费（Spike 0 数据），不加第二层缓存。

### D28-C ★ GDIBackend::DrawText 三分支 + guard 边界语义（冻结点 3+4）

**前置事实**：`DrawText`（GDIBackend.cpp:374-396）现流程 = `UTF8ToWide` → `GetOrCreateFont` → SelectObject → SetBkMode/SetTextColor → `TextOutW` 整串；后端 clip 用 **SaveDC/RestoreDC 栈**（`m_clipStack` 存 SaveDC id，**不存矩形**）⇒ 可视域用 **`GetClipBox(m_memoryDC, &rect)`** 原生获取（物理像素，与 `pos` 同空间——D6 截断口径）。

**执行顺序（三分支 + 阈值闸，评审 §12 顺序的落位细化）**：

```
0. 阈值闸：wideText.size() <= kTextPrefixMinWchars(=256)
     ⇒ 原路径 TextOutW 整串（短串零新增开销——评审 §10 fast path 的兑现形态）
1. GetClipBox == NULLREGION / ERROR ⇒ return（完全不可见；垂直完全不可见的
   常规情形已被 Phase 27 widget 级剔除拦截——此分支为冗余保险）
2. GetTextExtentExPointW(dc, wide, len, maxSpan, &fit, nullptr, &size)——一次调用
   同时拿到 fit 数与 size（含 cy）；maxSpan = clip.right − max(pos.x, clip.left)（钳 0）
3. 垂直无交集（pos.y ≥ clip.bottom ∥ pos.y + size.cy ≤ clip.top）⇒ return
4. fit >= len（水平完全可见）⇒ TextOutW 整串
5. 否则（水平部分可见）⇒ TextOutW 前缀 min(fit + 1, len)（+1 守卫字符）
6. **`GetTextExtentExPointW` 失败 ⇒ 回退原路径整串 `TextOutW`**（v1.1 吸收评审建议②——
   **不是 return**：测量失败 ≠ 文本不可见，最安全是保持旧行为；同族语义对齐
   `GetOrCreateFont` 失败跳过的既有风格）
7. 实现顺手项（评审 §8，非设计要求）：`pos.x ≥ clip.right` 可在求 maxSpan 前早退——
   正确性本已由活动裁剪区保证，仅省一次 extent 调用
```

- **guard 边界语义**：`GetTextExtentExPointW` 的 fit = 累积 advance ≤ maxSpan 的字符数；第 fit+1 字形的**起点** ≥ maxSpan 但其覆盖度可能因 overhang 前探——**+1 守卫让该字形照常发射、由活动裁剪区裁掉**；前缀内字形定位 = 累积 advance 与整串发射逐位同位（框架无 shaping/kerning——冻结边界）。
- **kTextPrefixMinWchars = 256** 放 GDIBackend.cpp 匿名 namespace；是**性能调参**（改动须复跑 T28-4/5 探针——盯防 ⑥）。
- **复杂裁剪区（COMPLEXREGION）**：GetClipBox 返回包围盒 ⇒ 前缀可能偏大（多画被裁字符）——**正确性不受影响**（仍由 GDI 裁剪），仅优化幅度减小；不做 region 级精算（YAGNI）。

### D28-D ★ T28-4 硬门槛与边界样本清单（冻结点 5）

- **硬门槛语义**：探针逐位比较**任何 1 字节不一致 ⇒ PD28-3 该分支回退**（守卫 +2 重试 → 仍不过 ⇒ 该分支整串回退原路径并在详设回填记录）——**不为性能硬保留**（初设 v1.1 预案兑现）。
- **探针形态**：库外 `.workbuddy/spike/textcost/gdi_prefix_probe.cpp`（沿 △7/T27-10 先例，不入注册表）：真实隐藏窗口 + GDIBackend，同一棵树整串绘制 vs 前缀绘制各渲一帧，`GetDIBits` 逐字节对比。
- **边界样本矩阵**：

| 维度 | 样本 |
|---|---|
| 字体 | SimSun（GDI 空 family 实际解析）、MSYH、Arial（纯拉丁对照） |
| 字形类别 | CJK 全角 · ASCII · **合成粗体**（GDI 仿真 overhang 最大）· 悬垂字形（`j`/`g`/`,`）· 全角标点行首 |
| clip 边界 | fit 边界恰贴合 · 差 1px · **clip 右缘落在字形中部**（overhang 场景）· clip = 整 DC（无有效裁剪）· NULLREGION |
| DPI | 96 / 120 |
| 内容 | realistic 语料 7KB tool 输出真实样本（复用 `realistic_texts.txt`）· 短串（阈值闸两侧） |

### D28-E ★ 测试注册表与探针分界（冻结点 6 的用例部分 + 对账）

- **注册表 +5（330 → 335）**：T28-1/2（FontEngineTests.cpp，无头——`Win32FontSource` 直接可用先例）· T28-3a/b（WidgetTests.cpp，**真实隐藏 Window**——WindowBackgroundTests.cpp 的 RenderServices 注入先例）· T28-6（RendererTests.cpp，RecordingBackend 命令流快照）。
- **库外探针（不入注册表）**：T28-4/T28-5 = `gdi_prefix_probe.cpp`（三边界 + 样本矩阵全跑）——**沿 Phase 27 T27-10 先例**；收口核对时按此校正用例总数（同族「~331 → 330」先例）。
- CMake **0 改动**（用例并入既有文件，RunAllTests 零改动）。

---

## 2. 逐文件改动（△1–△6）

| △ | 文件 | 改动 |
|---|---|---|
| △1 | `FontEngine.h` | **零改动**（advanceCache 在 Impl——.cpp pimpl；键复用 GlyphKey） |
| △2 | `FontEngine.cpp` | ① 匿名 namespace +`kMaxAdvanceCache = 8192` + `kMissingAdvance`；② Impl +`std::map<GlyphKey, long> advanceCache`；③ `MeasureText` 循环改 memo 路径：`gid = FT_Get_Char_Index(face, cp)` → 查 `(FaceId, px, gid, hinting)` → miss 则 `FT_Load_Char`（现行旗标）并回填（失败回填哨兵）；求和顺序不变（C28-4） |
| △3 | `TextWidget.h` | private +`mutable std::size_t m_textRevision = 0` + `mutable bool m_prefValid = false` + `mutable Size m_prefCache{}` + `mutable std::size_t m_prefRevision = 0` + `mutable Font m_prefFont{}` + `mutable int m_prefDpi = 0`（**零公共 API**） |
| △4 | `TextWidget.cpp` | ① `SetText` 双重载各 +`++m_textRevision`；② `GetPreferredSize` 按 D28-B 改（无窗口不短路分支保留现行为） |
| △5 | `GDIBackend.cpp` | 匿名 namespace +`kTextPrefixMinWchars = 256`；`DrawText` 按 D28-C 三分支改造（GetClipBox / GetTextExtentExPointW / 前缀 TextOutW）；`GDIBackend.h` 零改动 |
| △6 | Tests | `FontEngineTests.cpp` +T28-1/2 · `WidgetTests.cpp` +T28-3a/b（真窗口先例区）· `RendererTests.cpp` +T28-6（命令流快照区）· 探针 `.workbuddy/spike/textcost/gdi_prefix_probe.cpp`（库外） |

---

## 3. 契约（C28-1..C28-7）

- **C28-1** memo 值 = **现行旗标**（`FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP`）下 `advance.x >> 6`；缺字形 = `kMissingAdvance` 哨兵、循环跳过——与现行为逐位一致。
- **C28-2** memo 键 = `GlyphKey{FaceId, glyphIndex, px, hinting}`；**FaceId 永不复用**（FontEngine.cpp:48 既有不变量升格契约）；px/hinting 进键 ⇒ 字号与渲染策略变化天然失效。
- **C28-3** 上界 8192 满清（O9 口径）；满清后正确性由 T28-2 锚定。
- **C28-4** 求和顺序不变（逐码点累加 `>>6` 整数）⇒ 测量结果**逐位一致**（memo 只是取值路径不同）。
- **C28-5** TextWidget 指纹命中 ⇒ **零 `MeasureText` 调用**；键 = (revision, font.size, font.family, dpi)；无窗口不短路。
- **C28-6** GDI 前缀只在「>256 wchar 且 clip 活动且水平部分可见」时启用；**命令流零改动**（widget/命令/GL 全不动）。
- **C28-7** **T28-4 硬门槛**：探针逐位一致是 PD28-3 的放行条件；不过 ⇒ fallback（+2 → 整串回退）。

---

## 4. 盯防（6 条）

1. memo 只存 **advance**，不存位图——glyphCache 职责不动（两缓存勿混）。
2. **FaceId 永不复用**——未来 face 卸载/重载特性必须同步失效 advanceCache（C28-2 升格盯防）。
3. `FT_Set_Pixel_Sizes` **仍在 MeasureText 入口调用**——`result.height` 依赖 `face->size->metrics`（LineHeight/Ascent 会改 face 当前 px，不得省略）。
4. 指纹命中路径**不得触碰 measurer**（零虚调用）——T28-3 以 miss 计数差值验证，不以耗时验证。
5. GDI 前缀只改 `TextOutW` 长度——**不得**改命令流、不得碰 GL 路径、不得动 widget 层。
6. `kTextPrefixMinWchars`/守卫数是调参——任何改动**必须复跑 T28-4/5 探针**。

---

## 5. 用例（T28-1..T28-6 落位）

- **T28-1**（FontEngineTests.cpp）**双断言（v1.1 强化——结果一致 + memo 确实生效）**：① 双引擎交叉顺序逐位一致：引擎 A 先测长文本 T1（memo 热）→ 引擎 B 冷测同文本 → 逐位相等；交换顺序复测；② **真命中断言**：T2 与 T1 共享字形全集（重排/子串拼合）——A 测 T1 后 `AdvanceMemoMissCount() = n1`，再测 T2（字符串级 miss、字形级全命中）后 `= n1`（**零新增 FT load**）；对照：冷引擎 B 测 T2 后计数增加 ≥ 1（覆盖 C28-1/4/5-缝）。
- **T28-2**（FontEngineTests.cpp）上界满清：合成 >8192 唯一码点文本灌入 → 满清后再测普通文本，与全新引擎逐位一致（C28-3）。
- **T28-3a**（WidgetTests.cpp，真隐藏 Window）指纹命中：长文本 Label `AutoSize` 两次，第二次 `MeasureCacheMissCount` 差值 = 0（C28-5 零调用）。
- **T28-3b**（WidgetTests.cpp）指纹失效：`SetText` 换文本 / `SetFont` 换字号 各自触发重测（miss 差值 = 1）；短文本（阈值下）不受影响。
- **T28-4/5**（库外探针）D28-D 样本矩阵逐位对比 + 三边界（完全不可见 / 完全可见 / 恰好贴合）——硬门槛 C28-7。
- **T28-6**（RendererTests.cpp）7KB Label 全链命令流快照（`RecordingBackend`，防回归锚）。

---

## 6. 批次（细化到文件）

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | △2 FontEngine memo + T28-1/2 | 三链全绿 + 存量命令流逐位零回归 |
| 批二 | △3/△4 TextWidget 指纹 + T28-3a/b | 三链全绿 + miss 计数断言 |
| 批三 | △5 GDI 前缀 + 探针（T28-4/5）+ T28-6 | 三链全绿 + **T28-4 硬门槛通过** |
| 批四 | harness 侧复跑对照（A3 读数）+ 五处台账回填收口 | A3-1..3 全过 + A3-4 记录制 |

---

## 7. 影响面（详设定稿）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（△1/△3/△5 的头文件改动均为 private 成员/零改动） |
| 公共 API | **+0**（观测缝 `AdvanceMemoMissCount()` 落**内部件** `src/Render/FontEngine.h`——不入 `include/ECDI/**` 公共头计数；v1.2 批一勘误，v1.1 误记 +1） |
| 用例 | **330 → 335**（+5 注册表；T28-4/5 = 库外探针沿 T27-10 先例——**初设的 ~336 按此校正**） |
| CMake | **0 改动** |
| 风险 | **低**（唯一硬门槛 = T28-4，fallback 预案已冻结） |

---

## 8. 开放项

| # | 项 |
|---|---|
| O1 | PD28-4（advance-only 首载）启用判据：批一实测 memo 后 FT miss 仍 > GDI miss 3× 且 harness 复跑未达 A3-4 量级时，评审启用（D2-C 届时补量化边界） |
| O2 | `kTextPrefixMinWchars`/守卫数调参：探针数据驱动，改动须复跑 T28-4/5（盯防 ⑥） |
| O3 | 绘制期 `DrawTextContent` 每帧 `MeasureText`（既有缓存 ~0.5µs）——本轮不动；若收口读数仍显著再评估 |

---

## 9. 修订记录

- **v1.2**（2026-10-05）批一实施落地 + API 会计勘误。① ★ **API 影响面勘误**：`AdvanceMemoMissCount()` 落**内部件** `src/Render/FontEngine.h`（不入 `include/ECDI/**` 公共头计数）⇒ **公共 API +0**（v1.1 误记 +1 观测缝——评审建议的落点不变，会计口径修正）。② ★ **批一落地**：△2 advance memo（复用 GlyphKey + `kMissingAdvance` 哨兵 + 8192 满清 + **gid = 0 永不入 memo** 的 C28-1 安全语义——防「缺字形」与「charmap 命中 .notdef」病态混淆）+ T28-1（真命中双断言：结果逐位一致 + 热引擎共享字形文本零新增 FT load）/ T28-2（>8192 满清后语义不变）落 FontEngineTests.cpp；**三链 332/332**（MinGW / Clang / ClangCL——存量 330 零回归 +2）。③ 注册表 330 → 332（收口核对时按 +5 全量校正）。
- **v1.1**（2026-10-05）评审吸收。**结论：「Phase 28 Detailed Design v1.0：PASS → Implementation，可以开工了」**——设计成熟度**高**、实现风险**低～中低**、**不需要再开设计迭代**；六冻结点全部认可并已成 C28-1..7 契约；评审特别认可：复用 GlyphKey（不造第二套键体系）、`kMissingAdvance` 把失败结果也 memo（且保持求和语义）、PrefKey 三依赖各管各的（文本→revision / 字体→值比较 / DPI→值比较——「干净的 cache invalidation」）、无窗口不短路保护无头测试体系、**阈值闸 256 保住短文本 fast path**（「否则优化长文本反而让普通 GUI 变慢」）、fit+1 的 overhang 论证 + 无 shaping/kerning 前提显式声明、T28-3 用调用计数而非耗时（防噪声）、库外 probe 分界（「不为测试总数漂亮硬塞像素测试进 headless registry」）、四批顺序（GDI 像素级行为最后碰）；Phase 28 三优化 = 测量内部成本 → 测量调用次数 → 绘制提交长度，**不重复优化同一层**。**吸收两条非阻塞建议**：① **T28-1 强化** = 增加 advance memo **真命中**断言——新增 `FontEngine::AdvanceMemoMissCount()` 观测缝（沿既有 `MeasureTextCacheMissCount()` / `GDITextMeasurer::MeasureCacheMissCount` 同族先例；**影响面修正：API +0 → +1 观测缝**），T28-1 升级为「结果一致 + 零新增 FT load」双断言；② **D28-C 失败语义补全** = `GetTextExtentExPointW` 失败 ⇒ **回退原路径整串 `TextOutW`**（不是 return——「测量失败 ≠ 文本不可见」），分支表升 7 条。**顺手项与注记**（非设计要求）：`pos.x ≥ clip.right` 早退（实现时可做）；`long` 平台宽度注记（保持现状不扩范围）。评审同时划红线：实现阶段**严格执行 T28-4**（不因理论成立跳过）；Phase 28 不临时引入通用测量缓存 / shaping / line layout / batching / LRU / 新公共 API（观测缝除外）/ GL 改动。未采纳：无。
- **v1.0**（2026-10-05）初稿。**输入**：初设 v1.1（评审 PASS，13 项全 PASS）+ §3.5 六冻结点逐题钉死 + 代码核验（FaceId 永不复用为**既有不变量** FontEngine.cpp:48 · GlyphKey 复用 · GDIBackend clip 为 SaveDC 栈 ⇒ `GetClipBox` 方案 · SetText 双重载原文 · FontEngineTests 无头先例 / WindowBackgroundTests 真窗口先例 / WidgetTests FakeTextMeasurer 接缝）。**D28-A..E**：advanceCache 复用 GlyphKey（值 = 现行旗标 advance>>6，缺字形哨兵）· PrefKey 四字段直比（无窗口不短路）· DrawText 阈值闸 256 wchar + GetClipBox + GetTextExtentExPointW fit+1 守卫三分支 · T28-4 硬门槛 + 边界样本矩阵 · 注册表 +5（330 → **335**，T28-4/5 库外沿 T27-10 先例——初设 ~336 按此校正）。△1–△6 · 契约 C28-1..7 · 盯防 6 条 · 用例 T28-1..6 落位 · 四批 · 94→94 / API +0 / CMake 0 / 风险 低。
