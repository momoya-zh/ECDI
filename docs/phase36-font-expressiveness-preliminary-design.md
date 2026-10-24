# Phase 36 · Font 表达力 初步设计（字重 / face index / 复合族名第二项）

> 状态：**v1.0**（2026-10-24 落盘，**待评审**）｜输入 = [需求稿 v1.1](phase36-font-expressiveness-requirements.md)（评审吸收，PASS）｜上游 = deferred #70
> ★ **证据先行**：需求 §7 闸门要求的两项实测**本日已完成**，探针与读数存 `.workbuddy/spike/p36/`（gitignored，沿 p31/p36 先例）：**探针 1/1b**（`p36a.exe`/`p36a2.exe`，MinGW g++ 链真实 GDI：7 族 × 6 档 tmWeight/GetTextFaceW 矩阵 + 合成加粗串宽 + 内嵌样式 + 边界）· **探针 2**（TTC name 表解析器，四文件逐 face 的 nameID 1/6/16/17——即 FT `face->family_name` / `FT_Get_Postscript_Name` 读到的数据）。**本稿全部关键决策由读数反喂，零凭文档假设**。
> ★ **Phase 31 铁律自检**：本稿语义定案 = 「**族内最近字面**」这一独立于后端的规则，两链各自实现；GDI 探针是**该规则的验证样本**（实测恰与规则吻合），**不是契约的来源**——论证顺序（定义 → 实现 → 验证）未颠倒。

---

## §1 初设必答（需求 §7 五问逐题收口）

### 1.1 问一 = K8 缓存/指纹十一项逐项定键

| # | 缓存 | 现行键 | 新键 | 漏改后果 |
|---|---|---|---|---|
| ① | GDI 字体缓存（两处） | `(size, family, dpi)` `GDIBackend.cpp:1372` · `GDITextMeasurer.cpp:51` | `(weight, size, family, dpi)` | 换 weight 命中旧 HFONT ⇒ 加粗请求画出 regular |
| ② | `FontEngine::familyCache` | family 原串 `:56` | `(family, weight)` | 不同 weight 共用 FaceId ⇒ ⑤⑥⑦⑧⑨⑩⑪ 全部连带失效 |
| ③ | `FontEngine::faces` | 解析后路径 `:53` | `(路径, faceIndex)` | 同一 TTC 的 face0/face1 共用 FT_Face ⇒ UI 版永远拿不到 |
| ④ | `TextWidget` 指纹 ×2 | 逐字段比较（`.size`/`.family`）`TextWidget.cpp:320-336`/`:444-452`（成员 `TextWidget.h:153`） | 比较增 `.weight` 一行（副本是 `Font` 值，自动携带） | 换字重不重排/不重测 preferred |
| ⑤ | `FontEngine::measureCache` | `(text, size, family, dpi)` `:61`/`:285` | `(weight, text, size, family, dpi)` | 同键先 400 后 700 ⇒ 命中缓存返回 regular 宽度 |
| ⑥ | `FontEngine::lineHeightCache` | `(size, family, dpi)` `:62`/`:389` | `(weight, size, family, dpi)` | 行高不随字重切换（合成字面行高不同） |
| ⑦ | `GDITextMeasurer::m_measureCache` | `(text, size, family, dpi)` `.h:90`/`.cpp:97` | `(weight, text, size, family, dpi)` | 同 ⑤（GDI 侧） |
| ⑧ | advance memo | `GlyphKey` `:64` | **不动**（FaceId 在键） | — |
| ⑨ | GL 字形图集 | `GlyphKey` `GLGlyphAtlas.h:93` | **不动** | — |
| ⑩ | `FontEngine::glyphCache` | `GlyphKey` `:60` | **不动** | — |
| ⑪ | `ascentCache` | `(FaceId, px)` `:63`/`:418` | **不动** | — |

⑧–⑪ 安全性前置 = ② 的解析变 weight-aware（FaceId 随之不同）——`GlyphKey = {faceId, glyphIndex, pixelSize, hinting}`（`FontEngine.h:30-34`）已核验。全库清点收尾（v1.1 已做）：`FreeTypeTextMeasurer` 为薄委托（`:37`）、GL `CornerMaskCache` 键 = 半径——**清单 11 项为尽**。

### 1.2 问二 = D36-2 两链一致性口径（探针 1/1b 读数反喂）

**GDI 映射器实测语义**（探针 1，7 族 × 6 档）：
1. **绝不跨族**——`Segoe UI` 请求 600 ⇒ 本族 700，**不进** `Segoe UI Semibold`（它是独立注册族；`Segoe UI Semibold`@400 ⇒ tmWeight 600，`Segoe UI Light`@700 ⇒ 合成 700，face 仍 Light——族内合成再证）。
2. **族内最近字面，等距取轻**：`500→400`、`550→400`、`560→700`、`600/650/900→700`（Consolas；各族一致）⇒ 边界在 (550, 560]，与「|w−face| 最小、平手取轻」吻合。
3. **族内无字面 ⇒ 合成加粗**：`SimSun`@700 ⇒ face=宋体、tmWeight=700、串宽 **170→183（+7.6%）**——合成会改 advance。

**定案（口径分两层）**：
- **语义层（两链同一规则）**：`(family, weight)` ⇒ **族内可用字面集合中取 |w−face| 最小者，平手取轻**。GL 侧以注册表枚举出同族档位集合后按此规则选文件（PD36-3）；GDI 侧映射器天然实现同一语义（探针 = 验证样本）。
- **墨量层（如实宣布已知分歧）**：族内无所选字面的请求档（如 SimSun@700）——GDI **合成**（advance +7.6% 实测）、GL **不合成**（栅格化所选字面本体）。⇒ **face 相同、墨不同**；GL 侧合成（`FT_Outline_EmboldenXY` 族）= 记账不做（无消费者，YAGNI）。
- **A3 量具适用域随之收窄**：串宽同 face 判据**只在「请求档 == 族内真字面」格上判**（合成格不判串宽，判 face 相同）；T36 用例表按此分格。

### 1.3 问三 = GDI 映射器行为实测（= 1.2 的读数，已落 `.workbuddy/spike/p36/p36a*.cpp` + 读数文本）

另两组判定读数：**族名内嵌样式恒赢**（`Consolas Bold`@400 ⇒ tmWeight 700、face="Consolas Bold"——D36-C 证据）· **`Microsoft YaHei UI` 在 GDI 链本就直达**（@400/@700 face 均 = "Microsoft YaHei UI"，真字面来自 msyhbd.ttc）。

### 1.4 问四 = D36-4 窄版可行性（探针 2 读数）

| 文件 | face | family（EN） | family（ZH） | PostScript |
|---|---|---|---|---|
| msyh.ttc | 0 | Microsoft YaHei | 微软雅黑 | MicrosoftYaHei |
| msyh.ttc | 1 | **Microsoft YaHei UI** | Microsoft YaHei UI | MicrosoftYaHeiUI |
| simsun.ttc | 0 | SimSun | 宋体 | SimSun |
| simsun.ttc | 1 | **NSimSun** | 新宋体 | NSimSun |
| msyhbd.ttc | 0 / 1 | YaHei / YaHei UI | 微软雅黑 / 同 EN | MicrosoftYaHei(-Bold) / MicrosoftYaHeiUI(-Bold) |

⇒ **按 `family_name` 区分 face 完全可行**（face 1 的 EN/ZH 名均与 face 0 可区分）；PS 名带 `-Bold` 后缀可作 style 交叉验证。**裁定 = 窄版**：resolve 层产 `(路径, faceIndex)`（零公共 API），加载后以 face 的 name 数据**验证**匹配（FT 侧实读以详设复核——FT 自身对 locale 的挑选逻辑需一次实读确认）。

### 1.5 问五 = 勿重开 ✓

富文本 / 多字体回退 / 中文族名匹配（name 表虽有 ZH 名，仍属 #69 边界不做）/ 编辑器内 wrap / 新控件绕行——维持需求 §3 非目标。

---

## §2 决策定案 D36-A..F（对需求 D36-1..6 逐条回填）

| # | 定案 | 论证 |
|---|---|---|
| **D36-A**（需求 D36-1） | `Font` 增 `std::uint16_t weight = 400;`，**0–1000 直通**（= `lfWeight` 同刻度），**声明于 `family` 之后**（C7560，v1.1 P2） | GDI 零换算；FT 链按 1.2 规则归并；枚举会丢 650 档 |
| **D36-B**（D36-2） | §1.2 两层口径（语义 = 族内最近字面平手取轻；墨量合成差异 = 已知分歧） | 探针 1/1b 读数 |
| **D36-C**（D36-3） | **族名内嵌样式优先，weight 不降级**（`"Consolas Bold"` + weight 400 ⇒ Bold 字面）；weight 仅在族名为纯族名（经 ③ 归一后）时参与归并 | 探针 1b [2] |
| **D36-D**（D36-4） | **窄版**：`Win32FontSource::ResolveFile(family, weight)` → `(文件, faceIndex)`，faceIndex 由加载后 name 验证兜底；不加 `Font::faceIndex` 公共字段 | 探针 2 + 分层纪律（name 表解析属 FontEngine，不下沉 Win32FontSource） |
| **D36-E**（D36-5） | 默认 400 = `FW_NORMAL` 逐位兼容（A1 红线）；K8 十一项定键随本稿 §1.1 | — |
| **D36-F**（D36-6） | 组合规则表（穷举格）：请求族名形态 × weight 档 × 有无真字面，每格两链落点——**表即测试矩阵**（§4 T36 组） | 1.2/1.3/1.4 读数 |

---

## §3 方案 PD36-1..7

- **PD36-1 `Font` 字段**：`Font.h` +`std::uint16_t weight = 400;`（置于 `family` 之后）+ 契约注释（0–1000 刻度、族内归并语义、合成分歧声明、C7560 顺序注记）。
- **PD36-2 GDI 两处**：`lf.lfWeight = static_cast<WORD>(font.weight)`（`GDIBackend.cpp:1384` · `GDITextMeasurer.cpp:63`）+ 两处缓存键 +weight（①）。
- **PD36-3 解析链重构**：`FamilyToFileName(family)` → **族档枚举归并**：沿用注册表全键枚举，把每键按 ` & ` 拆项，对**每一项**做「== 请求族名（3）/ 请求族名+样式词前缀（2）」匹配 ⇒ 产出候选 `(文件, 样式weight)` 集合；按 1.2 规则归并出目标档 ⇒ 返回文件；**复合键第二项在此自然可达**（`Microsoft YaHei UI` ⇒ `msyh.ttc`@400 / `msyhbd.ttc`@700，`ResolveFile` 返回形态不变、新增 faceIndex 出参或结构体返回值——初设定 = 返回小结构 `{path, faceIndexHint}`，`FontSource` 接口随之微调，内部头）。
- **PD36-4 加载匹配**：`FontEngine::FaceIdFor(family, weight)`——经 resolve 得 `(path, hint)` 后，从 hint 起（失败则 0..num_faces−1）逐 face 读 name 匹配请求族名（EN/ZH 双尝试），命中即缓存；`faces` 键 = `(path, faceIndex)`（③）、`familyCache` 键 = `(family, weight)`（②）。
- **PD36-5 GL 归并纯函数**：`weight → 同族候选档` 的规则函数抽成**内部头**（`src/Render/FontWeightRule.h`，纯函数 + 真值表测试；GDI 侧不消费——映射器即实现，探针为验证样本；诚实记录：「共享代码强于断言」在此处只覆盖 GL 侧规则本体）。
- **PD36-6 测量链缓存键**：⑤⑥⑦ +weight（`FontEngine.cpp` 两处 + `GDITextMeasurer` 一处）。
- **PD36-7 TextWidget 指纹**：两处比较各 +`.weight`；`TextWidget.h:153` 注释「键维 2/3」→「3/4」。

**批次（三批）**：
- **批一（红线批）**：PD36-1 + PD36-2 + ① 键 + T36-1/2（默认零回归逐位对照 + GDI weight 真值）。验收 = 490 条全绿 + 默认路径像素对照（可选目视）。
- **批二（解析链批）**：PD36-3/4/5 + ②③⑤⑥⑦ 键 + T36-3/4/5/6（归并真值表 / 复合第二项 / face 匹配 / 缓存键失效）。
- **批三（收口批）**：PD36-7 + T36-7/8（指纹失效 + A3 受控对照探针「同 (family,weight) 两链同 face」串宽判据——真字面格）+ harness 交付（Release 库 + A6 复跑）。

## §4 测试 T36-1..8（预算 490 → ~500，含 K8 键守护）

| # | 用例 | 判据 |
|---|---|---|
| T36-1 | 默认零回归（红线） | 不设 weight：全链 490 条全绿 + GDI HFONT 缓存键命中行为不变（旧键测试族零改动编译） |
| T36-2 | GDI weight 真值 | `Consolas`@600/650 ⇒ 700（探针同读数）；`SimSun`@700 ⇒ face 仍宋体 |
| T36-3 | 归并规则真值表 | `FontWeightRule` 纯函数：400/500→400 · 550→400 · 560/600/650→700 · 等距取轻 · 单字面族恒该字面 |
| T36-4 | 复合第二项 | `Microsoft YaHei UI` ⇒ msyh.ttc face1（name 验证）；`Microsoft YaHei UI`@700 ⇒ msyhbd.ttc face1；`新宋体` 判据另测（family="NSimSun"） |
| T36-5 | face 匹配兜底 | hint 失配时扫描 num_faces 命中；全不命中 ⇒ 回退 face0 + 告警（不静默） |
| T36-6 | 缓存键失效（K8 守护） | 同 `(family,size,dpi)` 变 weight ⇒ measure/lineHeight/fontCache 各自重算（⑤⑥⑦⑧ 逐键）；`FontKey` 判等含 weight 的负对照 |
| T36-7 | 指纹失效 | wrap/preferred 指纹变 weight ⇒ 重排/重测（负对照：不改 weight 命中） |
| T36-8 | A3 受控对照（真字面格） | `Consolas`@400/@700、`Microsoft YaHei UI`@400/@700 两链串宽差 ≤ 容差（探针 1b 的 GDI 读数为基线）；合成格（SimSun@700）只判同 face + 登记墨量分歧 |

## §5 契约 C36-1..5

1. **C36-1** `Font::weight` = 0–1000 刻度，默认 400；>1000 行为 = clamp（消费边界，沿 Color 决策 23 先例——后端消费时 clamp）。
2. **C36-2** 归并语义 = 族内最近字面、平手取轻；**不跨族**（与 GDI 实测一致）。
3. **C36-3** 族名内嵌样式优先于 weight（weight 不降级既有样式）。
4. **C36-4** 合成分歧 = 已知限制：族无所选字面时 GDI 合成、GL 不合成（face 相同、墨不同）；契约注释明写。
5. **C36-5** face 可达性 = 实现细节（resolve 返回值 + name 验证），**不新增公共 API**；`Font` 仍只有 `size/family/weight` 三个字段。

## §6 影响面（对需求 §6 的更新）

公共头 **98 → 98**（`TextWidget.h` 仅为注释/成员微调）· API **+1 字段**（维持）· 用例 **490 → ~500**（T36-1..8 + 负对照）· CMake **0**（`FontWeightRule.h` 为内部头，不入 `include/ECDI/`）· 内部改动面 7 文件（`Font.h`/`GDIBackend.cpp`/`GDITextMeasurer.{h,cpp}`/`FontEngine.{h,cpp}`/`Win32FontSource.{h,cpp}`/`TextWidget.{h,cpp}` + 新 `FontWeightRule.h`）· 风险 **中→低**（口径已由探针钉死，K8 定键完整）。

---

## §8 修订记录

- **v1.0**（2026-10-24）初稿。**两项闸门探针同日完成**（`.workbuddy/spike/p36/`：探针 1/1b GDI 映射器 7 族 × 6 档 + 合成加粗串宽 + 内嵌样式 + 550/560 边界；探针 2 TTC name 表逐 face 枚举——修复了「TTC 表偏移为文件头相对」的解析初版错误）。**§1 必答五问收口**（K8 十一项定键表 / D36-B 两层口径[含 A3 量具适用域收窄] / D36-C 内嵌样式恒赢 / D36-D 窄版裁定 / 勿重开 ✓）；**§2 D36-A..F 定案**（语义 = 族内最近字面平手取轻，GDI 探针定位为验证样本而非契约来源——Phase 31 铁律自检通过）；**§3 方案 PD36-1..7 + 三批**（批一 = 红线批）；**§4 T36-1..8**（490→~500）；**§5 C36-1..5**（含 C36-4 合成分歧如实入契约）；**§6 影响面**（头 98→98 / +1 字段 / CMake 0 / 风险 中→低）。
