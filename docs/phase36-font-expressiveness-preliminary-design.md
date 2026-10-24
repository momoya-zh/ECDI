# Phase 36 · Font 表达力 初步设计（字重 / face index / 复合族名第二项）

> 状态：**v1.1**（2026-10-24 外部评审「有条件通过」吸收——2×P1 + 4×P2 + 2×P3 修毕 ⇒ **PASS**；v1.0 同日落盘）｜输入 = [需求稿 v1.1](phase36-font-expressiveness-requirements.md)｜上游 = deferred #70
> ★ **证据先行（v1.1 P1-① 修复后的事实链）**：全部探针**源码与读数均已落盘** `.workbuddy/spike/p36/`（gitignored，沿 p31 先例）——`p36a.cpp` + `p36a_readout.txt`（GDI 映射器 7 族 × 6 档）· `p36a2.cpp` + `p36a2_readout.txt`（合成加粗串宽 / 内嵌样式 / UI 族 / 550-560 边界）· `p36a3.cpp` + `p36a3_readout.txt`（**Light/Semibold 族归属**）· `p36b_nameparse.py` + `p36b_readout.txt`（**TTC/TTF name 表逐 face 枚举**——nameID 1/16/17/6）。**本稿全部决策由读数反喂，零凭文档假设**。
> ★ **Phase 31 铁律自检**：语义定案 = 「**族内最近字面**」这一独立于后端的规则，两链各自实现；GDI 探针是**验证样本**（实测恰与规则吻合），**不是契约来源**——论证顺序未颠倒。

---

## §1 初设必答（需求 §7 五问逐题收口）

### 1.1 问一 = K8 缓存/指纹十一项逐项定键

| # | 缓存 | 现行键 | 新键 | 漏改后果 |
|---|---|---|---|---|
| ① | GDI 字体缓存（两处） | `(size, family, dpi)` `GDIBackend.cpp:1372` · `GDITextMeasurer.cpp:51`；**键类型在头文件**（`GDIBackend.h:120` · `GDITextMeasurer.h:82/:90`） | `(weight, size, family, dpi)` | 换 weight 命中旧 HFONT ⇒ 加粗请求画出 regular |
| ② | `FontEngine::familyCache` | family 原串 `:56` | `(family, weight)` | 不同 weight 共用 FaceId ⇒ ⑤⑥⑦ 连带失效 |
| ③ | `FontEngine::faces` | 解析后路径 `:53` | `(路径, faceIndex)` | 同一 TTC 两 face 共用 FT_Face ⇒ UI 版拿不到 |
| ④ | `TextWidget` 指纹 ×2 | 逐字段比较 `TextWidget.cpp:320-336`/`:444-452`（成员注释 `TextWidget.h:144/:152-155`） | 比较增 `.weight` 一行 + 注释键维顺移 | 换字重不重排/不重测 preferred |
| ⑤ | `FontEngine::measureCache` | `(text, size, family, dpi)` `:61`/`:285` | `(weight, text, size, family, dpi)` | 同键先 400 后 700 ⇒ 命中缓存返回 regular 宽度 |
| ⑥ | `FontEngine::lineHeightCache` | `(size, family, dpi)` `:62`/`:389` | `(weight, size, family, dpi)` | 行高不随字重切换 |
| ⑦ | `GDITextMeasurer::m_measureCache` | `(text, size, family, dpi)` `.h:90`/`.cpp:97` | `(weight, text, size, family, dpi)` | 同 ⑤（GDI 侧） |
| ⑧–⑪ | advance memo / GL 图集 / glyphCache / ascentCache | `GlyphKey`（FaceId 在键）/ `(FaceId, px)` | **不动**（前置 = ② weight-aware） | — |

全库清点收尾：`FreeTypeTextMeasurer` 薄委托（`:37`）、GL `CornerMaskCache` 键 = 半径——**11 项为尽**。

### 1.2 问二 = D36-2 两层口径（探针 1/1b/1c + 2 读数反喂）

**GDI 实测语义**（读数见盘）：
1. **绝不跨族**（`Segoe UI`@600 ⇒ 本族 700，不进 Semibold 族）。
2. **族内最近字面、等距取轻**（500→400 · 550→400 · 560→700 · 600/650/900→700；边界 ≈555）。
3. **族内无字面 ⇒ 合成加粗**（`SimSun`@700：face 不变、tmWeight 700、串宽 **170→183 = +7.6%**——合成改 advance）。
4. ★ **v1.1 升级读数（探针 1c + 2 互证）**：`Segoe UI`@300/@200 ⇒ 400（**Light 不参与族内归并**）；`Microsoft YaHei Light`@400 ⇒ tmWeight 290 face "微软雅黑 Light"（独立族）；name 表侧：**Bold 类 face 的 nameID1 = 基族名**（`consolab.ttf`/`msyhbd.ttc` = "Consolas"/"Microsoft YaHei"）、**Light/Semibold 类 face 的 nameID1 = 独立族名 + nameID16 = 基族名**（`msyhl.ttc` = "Microsoft YaHei Light"/"Microsoft YaHei"、`seguisb.ttf` = "Segoe UI Semibold"/"Segoe UI"）——**GDI 的分族语义 = nameID-1 精确匹配，与注册表键文本无关**。

**定案（三层规则，两链同语义）**：
1. **候选集**（resolve 层，注册表强度匹配，宁滥勿缺——含样式词项与复合键两项）。
2. **nameID-1 过滤**（FontEngine 加载后）：face 的 nameID1 == 请求族名（EN/ZH 双尝试）者才进入归并——Light/Semibold 类独立族在此被滤除。★ v1.0 的「注册表前缀匹配直接归并」**已被否决**（会把 `Segoe UI Semibold` 误并进 `Segoe UI` 族 ⇒ 600 档两链分歧：GDI 给 700、GL 会给 600——探针 1 与 2 联合证伪）。
3. **归并**：存活档位集合中取 |w−face| 最小、平手取轻。

**墨量层（已知分歧，如实入契约 C36-4）**：族内无所选字面档（SimSun@700）——GDI 合成、GL 不合成 ⇒ face 相同、墨不同；GL 侧合成 = 记账不做（YAGNI）。**A3 串宽量具只判「请求档 == 族内真字面」格**（v1.0 已收窄，v1.1 增补：真字面格的判定 = 经 nameID-1 过滤后的存活档）。

### 1.3 问三 = GDI 映射器行为实测（读数 = `p36a_readout.txt` / `p36a2_readout.txt` / `p36a3_readout.txt`）

判定读数：内嵌样式恒赢（`Consolas Bold`@400 ⇒ 700）· `Microsoft YaHei UI` GDI 本就直达（@400/@700 face 不变）· 探针 1c 的 Light 族归属读数（§1.2-4）。

### 1.4 问四 = D36-4 窄版可行性（读数 = `p36b_readout.txt`）

| 文件 | nameID1 | nameID16 | nameID17 | PostScript | 归属 |
|---|---|---|---|---|---|
| msyh.ttc f0/f1 | Microsoft YaHei / **Microsoft YaHei UI** | — | — | MicrosoftYaHei(UI) | 独立族各一 |
| simsun.ttc f0/f1 | SimSun / **NSimSun** | — | — | SimSun / NSimSun | 独立族各一 |
| msyhbd.ttc f0/f1 | **Microsoft YaHei** / Microsoft YaHei UI | — | — | …**-Bold** | **基族内档** |
| msyhl.ttc f0/f1 | **Microsoft YaHei Light** | Microsoft YaHei | Light | …Light | **独立族**（被滤） |
| consolab.ttf | **Consolas** | — | — | Consolas-Bold | **基族内档** |
| seguisb.ttf | **Segoe UI Semibold** | Segoe UI | Semibold | SegoeUI-Semibold | **独立族**（被滤） |

⇒ **裁定 = 窄版**（零公共 API 字段）：resolve 出候选、**FontEngine 加载后按 nameID-1 过滤 + faceIndex 验证**。★ v1.1 P3-① 注：`msyhbd.ttc` 两 face 与 `msyh.ttc` 的 nameID1 **同族名**（face 身份靠「文件 + subfamily/PS 后缀」区分）⇒ **name 匹配判据不可只看 family 字段**——跨文件同族名时以「resolve 已按样式词定位文件」+ PS `-Bold` 后缀交叉验证。

### 1.5 问五 = 勿重开 ✓（富文本 / 多字体回退 / 中文族名匹配 / 编辑器内 wrap / 新控件绕行）

---

## §2 决策定案 D36-A..F

| # | 定案 | 论证 |
|---|---|---|
| **D36-A** | `Font` + `std::uint16_t weight = 400;`（0–1000 直通，**声明于 `family` 之后**[C7560，v1.1 P2 复核 ModelProbe.cpp:190/.474/.779 不破]） | GDI 零换算；FT 链按 §1.2 三层规则归并 |
| **D36-B** | §1.2 三层口径 + 墨量合成分歧（C36-4）；**A3 量具 = 真字面格** | 探针 1/1b/1c/2 |
| **D36-C** | 族名内嵌样式优先，weight 不降级（`"Consolas Bold"`+400 ⇒ Bold） | `p36a2_readout.txt` [2] |
| **D36-D** | **窄版**：`FontSource` **新增带默认实现的虚函数** `ResolveFileCandidates(family, weight)`（缺省 = `{ ResolveFile(family) }` ⇒ 既有实现者[含 `FontEngineTests.cpp:42` CountingFontSource]零改动）；`Win32FontSource` 覆写注册表枚举；`FontEngine::FaceIdFor(family, weight = 400)` **默认参数**（`FontEngineTests.cpp:220/225/230` 单参直调零改动编译）→ 候选 → nameID-1 过滤 → faceIndex 验证 → 归并。不加 `Font::faceIndex` 公共字段 | 探针 2 + 分层纪律 + v1.1 P1-② 兼容方案 |
| **D36-E** | 默认 400 = `FW_NORMAL` 逐位兼容（A1 红线）；K8 十一项定键 §1.1 | — |
| **D36-F** | 组合规则表 = 测试矩阵（§4 T36 组） | 全部读数 |

---

## §3 方案 PD36-1..8

- **PD36-1 `Font` 字段**：`Font.h` +`weight`（family 之后）+ 契约注释（0–1000 刻度 / 三层归并 / 合成分歧声明 / C7560 顺序）。
- **PD36-2 GDI 两处**：`lf.lfWeight = static_cast<WORD>(std::min<std::uint32_t>(font.weight, 1000u))`（**clamp 落点在此**——C36-1 v1.1 P2-④；uint16 上限 65535 不得直达 lfWeight）+ 两处缓存键 +weight（①，含 `GDIBackend.h:120` / `GDITextMeasurer.h` 键类型）。
- **PD36-3 候选集接口**：`FontSource.h` 新虚函数 `ResolveFileCandidates(family, weight)`（**默认实现 = `{ ResolveFile(family) }`**——新虚函数带默认实现 ⇒ 实现者零改动，沿 Phase 32 S7 / #60 先例）；`Win32FontSource` 覆写：注册表全键枚举 → 按 ` & ` 拆项 → 强度匹配（精确 / `<family>␣样式词`）→ 产出候选 `{文件, 样式词, 项文本}`；**legacy `ResolveFile(family)` 原样保留**（`FontEngineTests.cpp:178/182/189`、`TextMeasurerTests.cpp:486` 的既有语义 = weight-400 最优单文件，零变化）。
- **PD36-4 加载过滤与归并**：`FontEngine::FaceIdFor(family, weight = 400)`（默认参数）——空 family 走 legacy `DefaultFontFile` 路径原样；否则候选 → 逐候选 `(path, faceIndex)` 加载（faces 键 ③）→ nameID1 过滤（EN/ZH）→ §1.2 归并 → familyCache 键 ②。
- **PD36-5 GL 归并纯函数**：`src/Render/FontWeightRule.h`（内部头）——「存活档位集合 × weight → 所选档」纯函数 + 真值表测试；GDI 侧不消费（映射器即实现，探针 = 验证样本）。
- **PD36-6 测量链缓存键**：⑤⑥⑦ +weight。
- **PD36-7 TextWidget 指纹**：两处比较 +`.weight`；注释键维顺移（wrap 块 `:152-155` →「键维 2/3/4——size + family + weight」「DPI = 键维 5」「宽度 = 键维 6」；preferred 块 `:144` + `:444-452` 同步）。
- **PD36-8（v1.1 新增）**：`GLRenderer.cpp:229` 唯一 GL 解析点改 `FaceIdFor(font.family, font.weight)`（批二显式传参——默认参数只保编译兼容，语义必须显式）。

**批次（三批）**：批一（红线）= PD36-1/2 + ① 键 + T36-1/2；批二（解析链）= PD36-3/4/5/8 + ②③⑤⑥⑦ + T36-3/4/5/6；批三（收口）= PD36-7 + T36-7/8 + harness 交付（A6 固定指纹复跑）。

## §4 测试 T36-1..8（490 → ~500）

| # | 用例 | 判据 |
|---|---|---|
| T36-1 | 默认零回归（红线） | 不设 weight：490 条全绿 + **测试族零改动编译**（兼容机制 = D36-D 默认参数/默认实现——`FontEngineTests`/`TextMeasurerTests`/`CountingFontSource` 逐字未改）；默认路径 HFONT/face 行为与改前一致 |
| T36-2 | GDI weight 真值 | `Consolas`@600/650 ⇒ 700；`SimSun`@700 ⇒ face 宋体（合成读数同探针） |
| T36-3 | 归并真值表 + **nameID-1 过滤负对照** | 纯函数：400/500/550→400 · 560/600/650→700 · 平手取轻 · 单字面族恒该字面；**过滤负对照**：`Segoe UI`@600 ⇒ 700（Semibold 候选被滤，**不得**给 600）· @300 ⇒ 400（Light 被滤）；独立族直达：family="Microsoft YaHei Light" ⇒ msyhl.ttc（nameID1 即独立族） |
| T36-4 | 复合第二项 | `Microsoft YaHei UI` ⇒ msyh.ttc face1；@700 ⇒ msyhbd.ttc face1；`NSimSun` 直达 simsun.ttc face1 |
| T36-5 | face 匹配兜底 | hint 失配扫描 num_faces；全不命中 ⇒ face0 + 告警（不静默） |
| T36-6 | 缓存键失效（K8 守护） | 同 `(family,size,dpi)` 变 weight ⇒ **m_fontCache（tuple 键）** / measureCache / lineHeightCache / m_measureCache 各自重算（负对照：不改 weight 命中）——v1.1 P2-①：判据引用**实名 tuple 键**，无「FontKey」类型 |
| T36-7 | 指纹失效 | wrap/preferred 变 weight ⇒ 重排/重测 |
| T36-8 | A3 受控对照（真字面格） | `Consolas`@400/@700 · `Microsoft YaHei UI`@400/@700 · `Segoe UI`@700 两链串宽差 ≤ 容差（GDI 探针读数为基线）；合成格（SimSun@700）只判同 face + 登记墨量分歧 |

## §5 契约 C36-1..5

1. **C36-1** `Font::weight` = 0–1000，默认 400；**clamp 落点 = 消费边界**（PD36-2 的 `min(weight, 1000)` 后再送 `lfWeight`；GL 侧归并前同 clamp）。
2. **C36-2** 归并语义 = 候选 → **nameID-1 过滤** → 族内最近字面、平手取轻；不跨族。
3. **C36-3** 族名内嵌样式优先于 weight（weight 不降级既有样式）。
4. **C36-4** 合成分歧 = 已知限制（GDI 合成 / GL 不合成；face 相同、墨不同）——契约注释明写。
5. **C36-5** face 可达性 = 实现细节，**不新增公共字段**；公共 API 净变化 = `Font::weight` +1 字段 + `FontSource::ResolveFileCandidates` **带默认实现的虚函数**（实现者零改动——沿 Phase 32 S7 / #60 先例）。

## §6 影响面（v1.1 P1-②/P3-② 补全后口径）

- 公共头 **98 → 98**（`Font.h`/`TextWidget.h` 均为字段/注释级）；公共 API = **+1 字段 + 1 带默认实现的虚函数**。
- **内部改动面 14 文件**（{h,cpp} 拆算）：`Font.h` · `GDIBackend.h:120/.cpp` · `GDITextMeasurer.h/.cpp` · `FontEngine.h/.cpp` · `FontSource.h`（新虚函数）· `Win32FontSource.h/.cpp` · `GLRenderer.cpp:229`（PD36-8）· `TextWidget.h/.cpp` · 新建 `FontWeightRule.h`；**测试文件 0 改动**（D36-D 兼容机制）。
- 用例 **490 → ~500** · CMake **0**（`FontWeightRule.h` 不入 `include/ECDI/`）· 风险 **中→低**。

---

## §8 修订记录

- **v1.1**（2026-10-24）**外部评审「有条件通过」吸收——2×P1 + 4×P2 + 2×P3 修毕 ⇒ PASS**。① ★★ **P1-① 证据落盘**：探针 2 此前为内联脚本未存盘、探针 1/1b 读数未存档——现已全部落盘（`p36a.cpp`/`p36a2.cpp`/`p36a3.cpp`/`p36b_nameparse.py` + 四份读数文本，§1.3/§8 措辞同步改为指向盘上证据；评审独立复现全部读数正确，证据链现已闭合）。② ★★ **P1-② 影响面补全 + 兼容方案**：补 `GDIBackend.h:120`（键类型在头文件）· `GLRenderer.cpp:229`（GL 唯一解析点，PD36-8 显式传参）· `FontSource.h:27` 及实现者/调用者全集（`Win32FontSource.h:34` · `FontEngineTests.cpp:42/:178/:182/:189/:220/225/230` · `TextMeasurerTests.cpp:486`）；兼容机制 = **`ResolveFileCandidates` 带默认实现的新虚函数**（实现者零改动）+ **`FaceIdFor` 默认参数**（单参直调零改动编译）+ **legacy `ResolveFile` 原样保留** ⇒ T36-1「测试族零改动编译」承诺**成立且写明机制**。③ ★★ **P2-② 升级为规则修正（超出评审原意）**：评审要求三档真值格——实测发现更深一层：**GDI 分族 = nameID-1 精确匹配**（探针 1c：`Segoe UI`@300 ⇒ 400；探针 2：`msyhl.ttc`/`seguisb.ttf` nameID1 = 独立族名 + nameID16 = 基族名，而 `consolab.ttf`/`msyhbd.ttc` nameID1 = 基族名）⇒ v1.0 的「注册表前缀直接归并」会在 600 档产生**两链分歧**（GL 误选 Semibold）⇒ 规则升级为三层（候选 → nameID-1 过滤 → 归并），T36-3 扩入过滤负对照（@600 ⇒ 700、@300 ⇒ 400）与独立族直达格。④ **P2-① T36-6** 判据改实名 tuple 键（无「FontKey」幽灵类型）；**P2-③ PD36-7** 键维注释覆盖 wrap 块 `:152-155` + preferred 块 `:144/:444-452`；**P2-④ C36-1** clamp 落点写明（PD36-2 `min(weight,1000)`）。⑤ **P3-①** `msyhbd.ttc` 行补注（nameID1 = 完整基族名，跨文件同族名 ⇒ 匹配判据不可只看 family，PS `-Bold` 交叉验证）；**P3-②** 内部改动面口径统一 = **14 文件**（逐个列出）。⑥ 评审行号声明（`GDIBackend.h:120`/`FontSource.h:27`/测试五处/`TextWidget.h:153-155`）全量复核属实。⑦ 头部 v1.0 → **v1.1，PASS**。
- **v1.0**（2026-10-24）初稿。两项闸门探针同日完成（当时探针 2 未落盘——v1.1 P1-① 修复）。
