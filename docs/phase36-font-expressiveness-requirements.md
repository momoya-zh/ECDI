# Phase 36 · Font 表达力——字重 / face index / 复合族名第二项（deferred #70 ← harness REQ-10）

> 状态：**v1.0**（2026-10-24 落盘，**待评审**）｜上游 = `roadmap-deferred.md` **#70**（v2.32 新立、v2.33 勘误排期注记）｜来源 = harness `ECDI_fake_harness/docs/ecdi-capability-requests.md` **REQ-10**（2026-10-18 取证新立）
> ★ **立项语境**：2026-10-24 清理轮盘点——#70 是缺陷队列**唯一有真实消费者锚点**的开放项（variant-c 十处字重层级），用户拍板「先列需求稿，认真核对，根据项目事实来弄」。DesktopNest 暂缓与本 Phase 无排期冲突。
> ★ **事实纪律（本稿自约束）**：① 所有行号 = 2026-10-24 对 HEAD 实测（`70c5c44`；该两笔均为 docs-only 提交，源码行号自 `e913b8d` 起未变）；② **标注「harness 读数」的数字全部来自对方探针**（`req11_weight.cpp` / `req11_root.cpp` / `req11_facekind.cpp` / `family-sim.py` / `ttc-faces.py`），**ECDI 侧未复跑**（条 105 诚实边界），验收 A2/A6 才要求复现；③ 本机字体事实 = 本日**独立实测**（§2 K6，GDI `PrivateFontCollection` 装置），**非转抄 harness**；④ 沿 Phase 31 评审铁律：**§4 只列决策点与倾向，倾向 ≠ 定案**——「不能直接把倾向方案当成已定方案」「不得因为当前一侧的行为反过来定义框架契约」。

---

## §1 输入与问题陈述

### 1.1 三条子缺陷（harness REQ-10 取证 + ECDI 侧 2026-10-24 对码全部证实）

| # | 子缺陷 | ECDI 侧对码结论（带行号） |
|---|---|---|
| ① | **无字重** | `Font` 仅 `size` + `family` 两字段（`Font.h:17-18`）；生产侧 `lfWeight` 硬编码 `FW_NORMAL` **恰两处**（`GDIBackend.cpp:1384` · `GDITextMeasurer.cpp:63`；全源码含测试仅 4 处命中，另 2 处为 `TextMeasurerTests.cpp:81/492` 夹具）⇒ **任何调用方都无法请求粗体** |
| ② | **无 face index** | `FontEngine.cpp:236` = `FT_New_Face(impl.library, path.c_str(), 0, &face)` 恒取 face 0 ⇒ **TTC 的 face 1 在 GL 链不可达**（K6 实测：`msyh.ttc` face 1 = `Microsoft YaHei UI`） |
| ③ | **复合族名第二项断链** | `Win32FontSource::FamilyToFileName`（`:167`）三档匹配强度 = 完全相等 / `<family>␣`（样式变体）/ `<family>␣&␣`（**复合首项**）——对 `Microsoft YaHei UI` / `…UI Bold` 三档**全不中** ⇒ 返回空串 ⇒ `FontEngine.cpp:214` 走默认回退（告警 + SimSun） |

### 1.2 四手段矩阵（harness 读数，引用）

| 手段 | GDI 链 | GL 链 |
|---|---|---|
| `lfWeight`（无公共字段可传） | ✅（探针直改） | ❌（无字段、不传 variation 轴） |
| `family = "Microsoft YaHei Bold"` | ❌（**本地化规范名**：`GetTextFaceW` 对该族返回中文 `微软雅黑`，英文名失配 ⇒ 回退宋体） | ✅（strength-2 样式变体 → `msyhbd.ttc`） |
| `family = "Microsoft YaHei UI Bold"` | ✅ | ❌（子缺陷 ③） |
| `family = "微软雅黑 Bold"` | ✅ | ❌（③） |

⇒ **两条链唯一同时可用的 = 「不加粗」**（harness 探针结论；ECDI 侧对码与该矩阵无矛盾点）。
★ **墨迹比读数**（只改 `lfWeight` 400→700，链内对照）：5 个 harness 真实用族 **1.29–1.79×**——**harness 读数**，A2 只要求方向复现，不背书具体数值。

### 1.3 与 #69 / REQ-09 的边界（不重开）

#69（2026-10-17 修复）解决的是「**同一字符串两链解析到不同 face**」（族名→文件名反查）；本条是「**一个字符串根本表达不了想要的 style / face**」。#69 修复后族名写法两链都对，但能表达的仍只有「常规体 + 复合首项 face 0」。

### 1.4 ★ ECDI 侧核验新发现（harness 账本已回写订正）：② 与 ③ **耦合**

ⓒ（`FamilyToFileName` 支持复合键第二项）**单独落地**后，`Microsoft YaHei UI` 会解析到 `msyh.ttc`——但 `FT_New_Face` 恒取 face 0 ⇒ 拿到的是 `Microsoft YaHei`（非 UI 版）。比 SimSun 回退好，但**不是精确闭合**，且「静默给错 face」= #69 同类陷阱。⇒ **②③ 的闭合形态 = family → (文件, faceIndex) 二元组**（或加载后按 face 名扫描），应一体设计——「ⓒ 独立可做、零风险」只对「消除 SimSun 回退」成立。

---

## §2 现状勘察（K1–K9，2026-10-24 逐条带行号实测）

| # | 勘察项 | 事实 |
|---|---|---|
| **K1** | 数据模型 | `Font.h:15-19`：`float size = 14.0f`（**DIP 高度**，Phase 20 D4 语义）+ `std::string family`（UTF-8，空 = 系统默认）。纯数据、可值拷贝进命令 |
| **K2** | GDI 创建路径 | 两处**同构**：`GDIBackend.cpp` 与 `GDITextMeasurer.cpp` 各自 `CreateFontIndirectW`。缓存键 = `(size, family, dpi)` 三元组（`GDIBackend.cpp:1372` · `GDITextMeasurer.cpp:51`）；`lfHeight` = DIP→物理（lround）；`lfCharSet = DEFAULT_CHARSET`；**`lfWeight = FW_NORMAL` 硬编码**（`:1384` / `:63`）；`family` 经 `UTF8ToWide` 送 `lfFaceName`（`LF_FACESIZE` 截断保护，`GDIBackend.cpp:1390-1391`）。★ **两处必须同改**（渲染与测量不同步 = Phase 26 教训「测/画同源」破局） |
| **K3** | GL 加载路径 | `GLRenderer.cpp:229` `FaceIdFor(font.family)` = GL 链**唯一** family→face 解析点（`:267` advance 同源）。`FontEngine.cpp:190-241`：`familyCache` 键 = **family 原串**（`:56`/`:200`，含失败结果）；`faces` 缓存键 = **文件路径**；`FT_New_Face(…, 0, …)`（`:236`）；解析失败 ⇒ 明确告警 + 回退默认 face（`:211-215`，**不静默降级**——详设 △2） |
| **K4** | 文件名解析 | `Win32FontSource::ResolveFile`（`:122`，保留文件名直查且优先）→ `FamilyToFileName`（`:167`，注册表 `HKLM\…\Fonts`，三档强度，禁子串匹配——#69 落点）。默认字体 = `DefaultFontFile`（`:92`，`simsun.ttc` 优先——Phase 26 D26-6） |
| **K5** | 默认口径 | 空 `family`：GDI 链 `lfFaceName` 留空 → 系统映射；GL 链 `ResolveFile("")` → `DefaultFontFile` ⇒ **两链一致落 SimSun**（Phase 26 对齐决策）。⇒ 框架内未设 family 的全部文本**当前渲染在宋体上** |
| **K6** | **TTC face 枚举（本机独立实测 2026-10-24）** | GDI `PrivateFontCollection::AddFontFile` 装置：`msyh.ttc` = **2 faces**（`微软雅黑` + `Microsoft YaHei UI`）· `simsun.ttc` = **2 faces**（`宋体` + `新宋体`）· `msyhbd.ttc` = **2 faces**（同名 Bold）· `consola.ttf` = 1 face。⇒ **face index 缺口不只 YaHei 一家**（NSimSun 同样不可达）；harness `ttc-faces.py` 读数与本次独立实测**一致** |
| **K7** | 消费者盘点 | **框架内零 `family` 赋值**（全库 grep：仅 `TextWidget.cpp:326-327/450` 指纹比较 + ModelProbe 示例两处文件名写法 `examples/ModelProbe/ModelProbe.cpp:474/779` `"consola.ttf"`）⇒ 框架文本全部默认 SimSun；**weight 消费者 = 零**（variant-c 十处 `font-weight: 600/650/700` = 潜在首个，harness 侧） |
| **K8** | 缓存 / 指纹面（初设必答全清单） | weight/faceIndex 加入后须**逐项定键**：① GDI 字体缓存 `(size, family, dpi)`（`:1372`/`:51`）② `FontEngine::familyCache`（family 原串，`:56`）③ `FontEngine::faces`（文件路径）④ `TextWidget` wrap/preferred 指纹（含 `.family`/`.size`，`:326-327`/`:450`——**漏加 = 换字重不重排**）⑤ Phase 28 advance memo（FaceId 在键——文件级分离天然安全；variation 轴路线则不然）⑥ GL 字形图集键（FaceId 在键）。**漏任何一项 = 「改了字段没生效」类静默错误** |
| **K9** | 现有测试面 | 490 条中**无任何 weight / face index 断言**（从未覆盖，非回归）；字体相关既有面 = FontEngine 族名反查（#69 验收 8 族 × 2 链）· FitText/wrap 家族（Phase 29）· GDI 量化（Phase 34）。默认（不设 weight）路径**必须逐位不变**（A1） |

---

## §3 范围与非目标

**范围（两项，最终取舍由评审/初设定）**：
1. **`Font` 增加字重维度**（主线）——调用方可请求同族不同 weight；GDI 送 `lfWeight`、GL 按 weight 选 face；两链同解、调用方零改动（默认值 = 现行为）。
2. **face 可达性**——TTC face 1（UI 版 / NSimSun）与复合族名第二项：**按 §1.4 耦合结论一体设计**（family → (文件, face) 二元组或加载后 face 名匹配）。

**非目标（7 项）**：
1. 富文本 / 字距 / 阴影 / 渐变（→ #53 渲染表现力群，单消费者不立项）。
2. **多字体回退**（N6：一串文本跨族混排）——#69「仍不做」清单原文维持。
3. **中文族名**（`宋体`/`微软雅黑` 无单名注册表键，需读 OpenType name 表）——同上，#69 已如实登记不做。
4. TextBox **编辑器内** wrap（Phase 29 O2，独立挂账）。
5. **Variable font variation 轴**——初设只作**调查项**（本机常用 CJK 族全部为静态多文件形态，K6 佐证；不承诺实现）。
6. 跨平台字体管理 / 字体枚举 UI。
7. harness 侧过渡方案（拉丁带字重族名 + 中文压深代偿）不因本 Phase 改动——其切换由对方自行决定。

---

## §4 决策点 D36-1..6（倾向 ≠ 定案；每项初设必须给论证）

| # | 决策点 | 内容与倾向 |
|---|---|---|
| **D36-1** | **weight 的类型与语义** | 候选：(a) `uint16_t` **0–1000 直通**（= `lfWeight` 同刻度，GDI 侧零换算；语义文档化「400 = regular，700 = bold」，与 OSV2/CSS 习惯可换算）· (b) 枚举（Normal/Semibold/Bold…——丢失中间档 650）· (c) CSS 100–900。**倾向 (a)**：GDI 本就 0–1000、GL 侧做档位归并，语义最宽。★ 初设必答：非 GDI 后端（FT）对任意值 0–1000 的**归并规则** |
| **D36-2** | **两链一致性口径**（本 Phase 最高风险） | **Phase 31 论证顺序铁律适用**：先选定独立于后端的语义 → 两链各自实现 → 受控样本验证（不可颠倒；不得挑一侧实现当契约）。具体待定：同族无该字面时（如 `Consolas` + weight 600，族内只有 Regular/Bold），GDI 映射器挑「最近 face」、GL 侧按什么规则挑——**两链落点是否要求同 face** = 口径核心。倾向：**A3 验收要求同 face** ⇒ GL 归并规则必须显式设计（而非依赖 GDI 映射器行为反推） |
| **D36-3** | **weight 与族名内嵌样式的优先级** | `family="Consolas Bold"` + `weight=400` 谁赢？（族名已含样式 = 隐式请求）倾向：**族名内嵌样式优先**（不推翻 #69 已验收行为「请求粗体得粗体」），weight 仅在族名为纯族名时生效——初设钉死并写入 `Font` 契约注释 |
| **D36-4** | **face 可达性形态** | 候选：(a) `Font` 加 `faceIndex` 公共字段（调用方显式选 face——**GDI 链无对应概念**，`lfFaceName` 按名选择，字段在 GDI 链语义悬空）· (b) **resolve 层返回二元组**（`Win32FontSource` 内部 family→(file, face)，含复合键第二项匹配 + 按请求名匹配 face 名；**零公共 API 增量**）· (c) `Font.family` 允许 `name#index` 语法（污染字符串语义，不倾向）。**倾向 (b)**：动机（YaHei UI / NSimSun）全部可由族名表达；**但**「族名→face 名匹配」需要读 face 的 name 表或 `FT_Get_Postscript_Name`——初设给方案与代价。★ YaHei vs YaHei UI 的实际差异 = 行高/度量（harness 读数），**不新增字形能力**——本决策点允许降级为「只做复合键第二项匹配、face 选择按请求名尽力」的窄版 |
| **D36-5** | **默认值与向后兼容** | `weight` 默认 400 = `FW_NORMAL` ⇒ 不设该字段的全部既有调用方**逐位不变**（K7：框架内无人设 family，K5：默认 SimSun 不动）。公共头 98→98（`Font.h` 字段扩展，非新文件）； designated-initializer 调用点（ModelProbe 两处 `.size/.family` 具名初始化）零改动 |
| **D36-6** | **组合解析优先级** | `(family, weight)` 联合解析规则表：`"Microsoft YaHei"` + 700 ⇒ `msyhbd.ttc` face 0；`"Microsoft YaHei UI"` + 700 ⇒ `msyhbd.ttc` **face 1**；`"Consolas Bold"` + 任意 ⇒ `consolab.ttf`（D36-3）。★ 初设产出 = **一张穷举规则表**（族名形态 × weight 档 × 有无对应文件），每格给两链落点——禁止各链各自临场发挥 |

---

## §5 验收 A1–A6

| # | 验收 | 判据 |
|---|---|---|
| **A1** | **存量零回归（红线）** | 不设 `weight` 时：490 条既有用例全绿 + GDI/GL 渲染输出**逐位不变**（`Font` 新字段默认值路径不触碰任何既有分支）；K8 全部缓存/指纹行为与改前一致 |
| **A2** | **字重可达（GDI 链）** | 同族 weight 400 vs 700 链内墨迹比 > 1（方向判据——harness 读数 1.29–1.79× 仅作预期区间**引用**，不作为阈值）；≥3 族（含 CJK + 拉丁） |
| **A3** | **两链同 face（一致性口径）** | 同一 `(family, weight)` 下 GDI 与 GL 解析到**同一 face**（可比量 = **串宽**——#69 仪器教训：禁用 `tmAscent` vs `ascender` 这类会偶然同值的量具；正对照同 face 两侧同宽） |
| **A4** | **TTC face 1 可达** | `family="Microsoft YaHei UI"` 在 GL 链不再空串回退（K6 本机装置为准：msyh.ttc face 1 = UI 版）；`新宋体`（simsun.ttc face 1）同理可判 |
| **A5** | **组合规则表兑现** | D36-6 穷举规则表逐格有测试（至少覆盖表内 CJK 族全格 + 拉丁一组） |
| **A6** | **harness 复跑（固定指纹）** | REQ-10 四手段矩阵修复后**两链同解**；必须固定版本指纹 + 字体文件 + DPI 配置（Phase 31 A5 教训——否则结果变化无法归因） |

---

## §6 影响面

| 项 | 估值 | 说明 |
|---|---|---|
| 公共头 | **98 → 98** | `Font.h` 字段扩展（非新文件）；`TextStyle` 经 `StyleField<Font>` 自动携带 |
| 公共 API | **+1 字段**（`Font::weight`，默认 400） | 若 D36-4 取窄版 (b) 则 faceIndex **不加**公共字段；观测缝可能 +0～1（初设定） |
| 用例 | **490 → ~498**（估 +8） | A1 存量守恒 ×1~2 · A2 字重 ×2 · A3 同 face ×2 · A4/A5 TTC/组合 ×3；初设细化 |
| CMake | **0** | 无新文件（窄版） |
| 内部改动面 | `Win32FontSource`（复合第二项 + face 名匹配）/ `FontEngine`（face 选择 + 缓存键）/ `GDIBackend` + `GDITextMeasurer`（lfWeight 各一处 + 缓存键）/ `TextWidget` 指纹两处 | K8 清单为完整底账 |
| 风险 | **中** | 首次给公共数据模型加「影响两链解析」的维度；一致性口径（D36-2）是本 Phase 成败点——沿 Phase 31 先例「需求方向认可、口径公式待设计」处理 |

---

## §7 初设闸门（移交初设的必答项）

1. **K8 缓存/指纹六项逐项定键**——每项给出「weight（和/或 faceIndex）加入后」的新键与漏加后果；④（TextWidget `:326-327/450`）与 ①（GDI `:1372/51`）为必查。
2. **D36-2 一致性口径先行**——先产出口径定义，再写两链实现；GL 归并规则（最近档位的选择函数）给出**可测试的纯函数**形态（沿 Phase 34 `BackendContract` 共享件先例——两链共用一份规则代码强于两份等价断言）。
3. **GDI 无对应字面时的映射器行为实测**（小探针）：族内只有 Regular/Bold 时请求 600/650 的落点；跨族回退是否可能。**结论反喂 D36-2**，不许凭文档假设。
4. **D36-4 窄版/全版的取舍证据**：`FT_Get_Postscript_Name` / name 表匹配在本机四 TTC 上的实测可行性（K6 装置可直接复用）。
5. **勿重开**：富文本、多字体回退、中文族名、编辑器内 wrap、新控件类绕行（沿 REQ-12 教训——缺口是一条规则，不是一个新控件）。

---

## §8 修订记录

- **v1.0**（2026-10-24）初稿。**输入** = harness REQ-10（2026-10-18，受控对照取证）+ ECDI 侧 2026-10-24 对码核验（deferred #70 登记）+ 本机独立实测（K6 TTC 枚举、PrivateFontCollection 装置，与 harness `ttc-faces.py` 读数一致）。**§2 勘察 K1–K9 全部带行号**（含 K8 缓存/指纹六项底账 + K7 消费者盘点「框架内零 family 赋值」）；**§1.4 记录 ECDI 侧核验新发现（②③ 耦合，已回写 harness 账本订正 ⓒ）**；**§3 范围两项 + 非目标 7 项**；**§4 决策点 D36-1..6**（倾向 ≠ 定案，D36-2 一致性口径 = 本 Phase 成败点，Phase 31 论证顺序铁律明写）；**§5 验收 A1–A6**（A1 存量逐位不变红线 · A3 串宽量具纪律 · A6 固定指纹）；**§6 影响面**（头 98→98 / API +1 字段 / 用例 490→~498 / CMake 0 / 风险 中）；**§7 初设闸门五项**（含 GDI 映射器行为实测反喂 D36-2）。**harness 读数全部标注「harness 读数」**，ECDI 未复跑（条 105）——A2/A6 才要求复现。
