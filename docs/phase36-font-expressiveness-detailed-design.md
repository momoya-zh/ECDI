# Phase 36 · Font 表达力 详细设计（字重 / face index / 复合族名第二项）

> 状态：**v1.2**（2026-10-24 批一/批二实施回填——§9；评审吸收后的事实层勘误 E36-1..3；架构零回退）｜输入 = [初设 v1.1](phase36-font-expressiveness-preliminary-design.md) + [需求稿 v1.1](phase36-font-expressiveness-requirements.md)｜上游 = deferred #70
> ★ **证据链（全部落盘 `.workbuddy/spike/p36/`）**：探针 1/1b/1c（GDI 映射器）· 探针 2（name 表解析器）· **探针 3 v2（p36c，本日扩展 OS/2 列**：`usWeightClass` + `fsSelection`，clang-cl `/MD` 链生产同款 vendored FreeType 2.14.3 静态库——`p36c_ftnames.cpp` + `p36c_readout.txt`）。
> ★ **v1.1 两大设计换血（均为读数反喂）**：① **档位来源 = face 自身 `OS/2.usWeightClass`**（初设的「注册表样式词→weight 映射表」废除——本方读数复核评审读数：seguisb=600 · segoeuisl=350 · segoeuil=300 · seguibl=900 · consolab=700 全吻合，唯一偏差 msyhl 真实 = **290** 非表记 300；GDI `tmWeight` 与 `usWeightClass` 对真实字面逐一相等 = 双仪器互证）；② **斜体面排除 = `OS/2.fsSelection` bit0**（consolai fsSel=1、consolaz fsSel=33 [ITALIC] 实测——它们 nameID1 = 'Consolas' 能穿过族名过滤，必须显式排除）。

---

## §1 评审修订收口（v1.0 → v1.1 的事实层修正）

### 1.1 【P0-1 修】「family 即文件名」直查路径保留且优先

`Win32FontSource::ResolveFile` 的既有路径序 = ①**文件名直查**（`Win32FontSource.cpp:135-148`：fonts 目录 + FileExistsWide）→ ②注册表 `FamilyToFileName`。v1.0 的 `ResolveFileCandidates` 覆写只写「注册表枚举」⇒ **ModelProbe 的 `consola.ttf` 写法（`ModelProbe.cpp:474/779`）会解析失败回退 SimSun**。
**修正**：`ResolveFileCandidates(family)` = **①文件名直查命中 ⇒ 单候选 `{family, hint=0}`（排在首位）** + **②注册表枚举候选**；`FontEngine` 侧归并平手时**候选序优先**（下标小者胜——P3-15 后的 tie-break）⇒ 直查路径在 weight=400 时的最终选择与 legacy 逐位一致（A1）。

### 1.2 【P0-2 修】ZH 族名可达断言删除，回归需求非目标 3

评审实测：本机注册表 Fonts 键值名**无任何中文项** ⇒ ZH 请求在 resolve 层**不可能产生候选**（过滤层不能凭空造候选——v1.0 把 ZH 可达写在过滤层属逻辑倒置）。**裁定 = 采纳评审建议**：
- 删除 v1.0 §1.2/§2.3/§4 的全部「ZH 请求可达」表述（`微软雅黑` 直达断言作废）；
- `FaceEntry` **不引入 `nameId1Zh` 字段**（死代码不进——条 95）；过滤判据 = `nameId1En == request` 单判（候选文本本就是 EN）；
- **边界钉住**（T36-4 新增负对照）：`FaceIdFor("微软雅黑")` ⇒ 回退默认 face + Warning——ZH 请求在 GL 链不解析 = 需求非目标 3 的既有边界（GDI 链 ZH 可用 = **既有跨链不对称**，v1.1 如实登记，非本 Phase 引入）；
- `nameId1En` 直读仍必要（探针 3：`family_name` 偏好 nameID-16——msyhl f0 读 "Microsoft YaHei"/Light，必须用 nameID1 才能滤掉）。

### 1.3 【P0-3 修】「等宽族字重串宽不变」断言删除（已被已有读数证伪）

`p36a2_readout.txt` [3]：Consolas 400 → w=**187**、560/700 档 → w=**200**（+7%）——「等宽 Bold 推进不变」的假设不成立（读数含 CJK 字符的 GDI 字体回退加粗干扰，Latin 纯串未单测）。**仪器教训记录**：未经单变量验证的「常识不变量」不得作判据。T36-2 收窄 = SimSun 串宽回归带一条（合成加粗的 advance 变化本身就是「字重可达」的 GDI 侧证据——A2 库内载体，见 §4 批次）。

### 1.4 【P1-4 修】numFaces 读取顺序

`FT_New_Face(path, hint)` 在 hint 越界时**打开即失败**，拿不到 `num_faces`。**修正 = p36c 自己的写法**（`p36c_ftnames.cpp:37-40`）：先 `FT_New_Face(path, 0)` 读 `num_faces`（该 face 即 face0，入缓存复用），再逐 index 重开；hint 仅决定**扫描起点序**（hint 先试、其余升序），不再是「首开参数」。

### 1.5 【P1-5 + P2-12 合并修】档位 = `OS/2.usWeightClass`；斜体 = `fsSelection` bit0 排除

- 斜体面实测能穿过族名过滤（`consolaz.ttf` nameID1 = 'Consolas'）——v1.0 的「样式词映射表 + Italic 跳过」既未定义组合词（"Bold Italic"）又有 msyhl 290/300 偏差。**修正 = 双双废除**：
  - **档位 weight = face 自身的 `OS/2.usWeightClass`**（FontEngine 加载时随 nameID1 一并读入 `FaceEntry`；OS/2 缺失 ⇒ 记 400 + 告警）；
  - **斜体排除 = `fsSelection & 0x01`**（Italic 位——consolai=1 / consolaz=33 实测；排除发生于候选过滤层，先于归并）；
  - resolve 候选结构随之瘦身为 `{filePath, faceIndexHint}`（P2-7：`weight` 参数与字段删除——枚举本就全档，请求档由归并层决定，死参数不进）。
- `Win32FontSource::ResolveFileCandidates` = **单参 `(family)`**；`EnumerateFontKeys` 提取照旧（P0-1 双源：直查 + 注册表项）。

### 1.6 【P1-6 修】A2 双载体落位

A2（字重可达）= **库内**：T36-2 SimSun 串宽回归带 [1.05, 1.10]（真实窗口经 `GDITextMeasurer`，`lfWeight` 接线的端到端证据）+ **库外**：批三 harness 交付时 A6 探针扩墨迹比读数（5 族 1.29–1.79× 复现，固定指纹）。批次表（§5）补 A2 行，不再落空。

### 1.7 其余 P2/P3 吸收（逐条）

| # | 修正 |
|---|---|
| P2-8 | `FaceEntry` 字段（+`nameId1En` +`os2Weight` +`italic`）归 **△7**（`FontEngine.cpp` Impl `:46-49`）；△6 = `FaceIdFor` 签名 + `:85-89` doxygen 同步 |
| P2-9 | T36-6 改用**既有观测缝** `MeasureTextCacheMissCount()`（`FontEngine.cpp:263`）：400 首测 miss+1 → 同参复测 +0 → 变 weight miss+1；m_fontCache/lineHeightCache 无观测缝 ⇒ 键完整性由盯防①代码走查承担（如实记录不可观测） |
| P2-10 | T36-8 改断 **face 身份**：`FaceIdFor("Segoe UI",600) == FaceIdFor("Segoe UI",700)` 且 `!= @400`（宽度相等在同 FaceId 下是同义反复） |
| P2-11 | 注释同步补：`TextWidget.cpp:312`「五元组」→ 六元组（+weight）· `GDIBackend.h:119` · `GDITextMeasurer.h:80/:88` 的「键 = (size, family, dpi)」→ 四元组 |
| P3-13 | `familyCache` 行号 = **`:56`**（:55 是 nextFaceId）；「tuple weight 最前」措辞改「**键一律含 weight**」（familyCache = `pair<string,uint16>`、faces = `pair<string,int>`，两者非 tuple） |
| P3-14 | 490 = **运行自报口径**（全局注册表内含框架自注册条目，均可执行；`RunAllTests.cpp` 本身无 Add(，仅调 RunAll）——锚点不改，本条备查 |
| P3-15 | `PickNearestTier` 返回 **tiers 下标**；`WeightTier` 结构删除（直接 `std::vector<std::uint16_t>` 输入） |
| P3-16 | 空 family 的 familyCache 键 = `("", ClampWeight(weight))`——与普通键同形，写入 §2.3-① |

**复核确认（评审正面清单）**：K8 穷尽性独立复核通过（全库逐字段比较点仅 TextWidget 两处）；行号抽查 ±1 内吻合（仅 :55 例外已修）；「Segoe UI@900 ⇒ GL 给 Black」的评审自查假设已自我证伪撤回（seguiBL n1en = "Segoe UI Black" 被滤）。

---

## §2 接口与结构定稿（v1.1 修订）

### 2.1 `FontSource.h`（△4）

```cpp
struct FontSourceCandidate {
    std::string filePath;   // 可 FT_New_Face 的路径（编码契约同 ResolveFile）
    int faceIndexHint;      // 复合键匹配项位置（0 起；非复合/文件直查 = 0）——仅提示
};
// 新虚函数，带默认实现（实现者零改动）：
virtual std::vector<FontSourceCandidate> ResolveFileCandidates(const std::string& family) const;
// 默认实现：{ ResolveFile(family) 非空 ⇒ 单候选 {path, 0} }——非 Win32/测试源与 legacy 一致
```

### 2.2 `Win32FontSource`（△5/△6）

`ResolveFileCandidates` 覆写 = **①文件名直查**（镜像 legacy 路径序：fonts 目录 + FileExistsWide，命中 ⇒ `{family, 0}` 首位）+ **②注册表枚举**（`EnumerateFontKeys` 提取共用；键名剥后缀 → ` & ` 拆项 → 每项独立判「== family（3）/ family+␣前缀（2）」→ 命中项产出候选，**无 weight 字段**——档位归 face 自身 OS/2）。`FamilyToFileName`/`ResolveFile`/`DefaultFontFile` 逐字不动。

### 2.3 `FontEngine::FaceIdFor(family, weight = 400)`（△6/△7）六步流程

1. **空 family** ⇒ legacy 原样（`ResolveFile("") → DefaultFontFile`）+ `familyCache[("", ClampWeight(weight))]`。
2. `familyCache.find((family, ClampWeight(weight)))` 命中即返。
3. `candidates = source->ResolveFileCandidates(family)`；为空 ⇒ 告警 + legacy 默认回退 + 入缓存（不静默）。
4. 逐唯一 path：**`FT_New_Face(path, 0)` 先开读 `numFaces`**（face0 入缓存）→ 扫描序 = 各候选 hint 优先、其余升序 → 每 face 入 `faces[(path, idx)]` 时读 `nameId1En`（FT_Get_Sfnt_Name，platform 3 · nameID 1 · lang 0x409，UTF-16BE→UTF-8）+ `os2Weight`（OS/2 usWeightClass，缺表记 400 + 告警）+ `italic`（fsSelection bit0）。
5. **过滤**：`nameId1En == family` 且 `!italic` 的 face 成为存活档（weight = 其 os2Weight）。
6. `PickNearestTier(存活档 weights, ClampWeight(weight))` → 下标 → 对应 `(path, idx)` ⇒ `familyCache.emplace`；全灭 ⇒ 告警 + legacy 默认回退 + 入缓存。

### 2.4 缓存键（K8 必改 7 处；措辞 = 「键一律含 weight」）

| 位置 | 新键 |
|---|---|
| `GDIBackend.h:120`（:119 注释同步） | `std::tuple<std::uint16_t, float, std::string, int>`（weight, size, family, dpi） |
| `GDITextMeasurer.h:82`（:80 注释同步） | 同上 |
| `GDITextMeasurer.h:90`（:88 注释同步） | `std::tuple<std::uint16_t, std::string, float, std::string, int>`（weight, text, size, family, dpi） |
| `FontEngine.cpp:61` measureCache | 同上（五元组） |
| `FontEngine.cpp:62` lineHeightCache | `std::tuple<std::uint16_t, float, std::string, int>` |
| `FontEngine.cpp:56` familyCache | `std::pair<std::string, std::uint16_t>`（family, weight） |
| `FontEngine.cpp:53` faces | `std::pair<std::string, int>`（path, faceIndex） |

### 2.5 `FontWeightRule.h`（新建内部头，△13）

```cpp
// |w-request| 最小者；平手取轻（w 小者）；再平手取下标小者——与 GDI 探针 1 全格一致
std::size_t PickNearestTier(const std::vector<std::uint16_t>& tiers, std::uint16_t request);
inline std::uint16_t ClampWeight(std::uint32_t w) { return (std::uint16_t)std::min<std::uint32_t>(w, 1000u); }
```

### 2.6 其余调用点与注释面

`GLRenderer.cpp:229` ⇒ `FaceIdFor(font.family, font.weight)`（显式）；`TextWidget.h:144/:152-155` + `.cpp:312/:320-336/:444-452`（P2-11 全集）；`Font.h` +`weight`（family 后[C7560]）+ 契约注释。

---

## §3 逐文件差异 △1..△14（v1.1 修订）

| △ | 文件 | 改动 |
|---|---|---|
| 1 | `include/ECDI/Core/Font.h` | +`weight`（family 后[C7560]）+ 契约注释 |
| 2 | `src/Render/GDIBackend.h` | `:119` 注释 + `:120` 键类型 +weight |
| 3 | `src/Render/GDIBackend.cpp` | `:1372` 键构造；`:1384` `lfWeight = static_cast<WORD>(ClampWeight(font.weight))` |
| 4 | `src/Render/GDITextMeasurer.h` | `:80/:82/:88/:90` 注释 + 键类型 +weight |
| 5 | `src/Render/GDITextMeasurer.cpp` | `:51` 键；`:63` lfWeight 同 △3；`:97` 键 |
| 6 | `src/Render/FontEngine.h` | `FaceIdFor` 签名 +weight 默认参；`:85-89` doxygen 同步（字段在 △7） |
| 7 | `src/Render/FontEngine.cpp` | Impl（`:46-49`）`FaceEntry` +`nameId1En/os2Weight/italic`；FaceIdFor 六步（§2.3）；`ReadFaceNameId1` 助手；familyCache `:56`/faces `:53`/measureCache `:61`/lineHeightCache `:62` 键 |
| 8 | `src/Render/FontSource.h` | +`FontSourceCandidate` + `ResolveFileCandidates(family)` 带默认实现（§2.1） |
| 9 | `src/Platform/Win32/Win32FontSource.h` | +覆写声明 +`EnumerateFontKeys` 声明 |
| 10 | `src/Platform/Win32/Win32FontSource.cpp` | 覆写实现（直查 + 注册表双源，§2.2）+ `EnumerateFontKeys` 提取（FamilyToFileName 改调，**逐字等价**——T26-6/T69 锚） |
| 11 | `src/Render/GLRenderer.cpp` | `:229` 显式传 `font.weight` |
| 12 | `include/ECDI/Widget/TextWidget.h` | `:144/:152-155` 注释键维顺移 |
| 13 | `src/Widget/TextWidget.cpp` | `:312` 五元组→六元组注释；`:320-336`/`:444-452` 比较 +`.weight` |
| 14 | `src/Render/FontWeightRule.h` | **新建**（§2.5） |

**测试文件 0 改动**（§2.1 默认实现 + §2.3 默认参数）。

## §4 契约 C36-1..5（v1.1 终稿）

1. **C36-1** `Font::weight` = 0–1000，默认 400；clamp = 消费边界（`ClampWeight`：FontEngine 归并前 / GDI lfWeight 前）；**缓存键存原值**（Clamp 后值仅用于归并与 lfWeight——用户改回合法值不残留污染）。
2. **C36-2** 归并 = 候选集（**文件直查优先 + 注册表枚举**）→ **nameID1-EN 直读过虑 ∧ 非斜体**（fsSelection bit0）→ **OS/2 usWeightClass 档位**最近平手取轻；不跨族；`family_name` 不作判据；**ZH 族名不解析**（非目标 3——GL 链 ZH 请求回退默认 face + 告警；GDI 链 ZH 可用 = 既有跨链不对称，已登记）。
3. **C36-3** 族名内嵌样式优先于 weight（weight 不降级既有样式——p36a2 [2]）。
4. **C36-4** 合成分歧 = 已知限制（GDI 合成 advance +7.6% / GL 不合成）。
5. **C36-5** 公共 API = **+1 字段 + 1 带默认实现的虚函数**（单参 `ResolveFileCandidates(family)`——无死参数）。

## §5 测试 T36-1..8（490 → ~500）与批次

| # | 装置 | 精确断言（v1.1 修订） |
|---|---|---|
| T36-1 | 全量回归 | 490 全绿；测试源文件 git diff 空；FaceIdFor 单参对 #69 八族返回值 = 改前（含 `consola.ttf` 文件名写法——P0-1 回归锚） |
| T36-2 | 真窗口 GDI | SimSun @400 vs @700 串宽比 ∈ [1.05, 1.10]（探针 1.076 的回归带）——**A2 库内载体**（P0-3 后仅此一条宽度判据，Consolas 等宽假设已证伪删除） |
| T36-3 | `FontWeightRule` 纯函数 | tiers={400,700}：400/500/550→400 · 560/600/650/700/900→700；{400}：任意→0；平手 {300,400}@350 → 300（取轻）；`ClampWeight(65535)=1000` |
| T36-4 | FT 链（真实源） | `FaceIdFor("Microsoft YaHei UI") != FaceIdFor("Microsoft YaHei")`；@700 同构不等（msyhbd f1 vs f0）；`FaceIdFor("NSimSun") != FaceIdFor("SimSun")`；**负对照（P0-2 边界钉）**：`FaceIdFor("微软雅黑")` = 默认 face 且 Warning 日志非空 |
| T36-5 | FT 链（兜底） | hint 失配 ⇒ 扫描命中（复合第二项天然覆盖）；候选全灭 ⇒ face0 + 告警不静默 |
| T36-6 | **观测缝**（P2-9） | `MeasureTextCacheMissCount()`：YaHei UI @400 首测 +1 → 同参复测 +0 → @700 +1（键含 weight 的黑盒证明）；m_fontCache/lineHeightCache 无缝 ⇒ 盯防①代码走查 |
| T36-7 | TextWidget 指纹 | 变 weight ⇒ FitText +1；不变 ⇒ 命中 |
| T36-8 | **face 身份**（P2-10） | `FaceIdFor("Segoe UI",600) == FaceIdFor("Segoe UI",700)` 且 `!= @400`（Semibold 滤除）；`@300 == @400`（Light 滤除）；`FaceIdFor("Microsoft YaHei Light") != FaceIdFor("Microsoft YaHei")`（独立族直达）；跨链串宽对照 = A6 库外（T26-9 先例） |

**批次**：批一（红线）= △1/2/3/4/5 + T36-1/2；批二（解析链）= △6/7/8/9/10/11/14 + T36-3/4/5/6/8；批三（收口）= △12/13 + T36-7 + harness 交付 + **A2 库外墨迹比 + A6 固定指纹复跑**（P1-6 双载体落位）。

## §6 影响面（v1.1 终稿）

公共头 **98 → 98** · API = **+1 字段 + 1 单参默认实现虚函数** · 用例 **490 → ~500**（490 = 运行自报口径，P3-14 备查）· CMake **0** · 内部 **14 文件**（§3，测试 0 改动）· 风险 **低**。

**盯防九条**（v1.1 修订）：① K8 七键逐一核对（m_fontCache/lineHeightCache 无观测缝 = 代码走查项）② FT_Get_Sfnt_Name UTF-16BE 字节序 ③ nameID1 缺失 ⇒ 过滤出局 + 告警 ④ `GLRenderer.cpp:229` 显式传参 ⑤ legacy 三函数逐字等价（T26-6/T69 锚 + `consola.ttf` 直查路径）⑥ **文件直查候选首位 + 平手取下标小者 = legacy 优先序**（A1）⑦ ClampWeight 两处同式 ⑧ familyCache 含回退结果（T26-13 锚）⑨ OS/2 缺表记 400 + 告警。

---

## §9 实施回填（批一 / 批二）

> ★ 实施期勘误与落地记录（详设 v1.1 的事实层修正——架构零回退）：

- **E36-1（批二实施期发现，P0-① 的深化）**：文件直查候选（`consola.ttf`）的 face nameID1 = 'Consolas' ≠ 请求串 `'consola.ttf'`——**若不过滤豁免，直查路径会被族名过滤误杀**（v1.1 §1.1 只救了「直查在候选序首位」，未救「过滤层」）⇒ `FontSourceCandidate` 增 **`directFile` 标记**（§2.1 修订：三字段），FontEngine 对 directFile 路径**整路径豁免 nameID-1/斜体过滤**（face 即用户点名，tier 仍取其 OS/2）；默认实现单候选 = `directFile=true`（非 Win32/测试源 legacy 语义保持）。T36-8 以 `FaceIdFor("consola.ttf") == FaceIdFor("Consolas")` 钉死。
- **E36-2（批二实施期盘点）**：`FaceIdFor(font.family)` 内部调用点实测 **9 处**（GlyphIndex/MeasureText/FitText/LineHeight/Ascent×3/Glyph/GlyphKey）——详设盯防④只点名 GLRenderer 一处；九处全部随批二显式传 `font.weight`（漏一处 = 静默错档）。
- **E36-3（P3-⑯ 定案细化）**：familyCache 键 = `(family, weight)` **weight 原值**（含空 family `("", weight)`——非 ("", ClampWeight) 字面；clamp 仅作用于归并/lfWeight，键存原值与 C36-1 一致）。
- **批一落地**（`762c483`）：△1-5 + △14a（仅 ClampWeight）+ T36-2 ⇒ **MinGW 491/491**。
- **批二落地**（本提交）：△6/7/8/9/10/11/14b + T36-3/4/5/6/8 ⇒ **MinGW 496/496 退出码 0**——490 存量全绿（T26-6/T26-12/T26-13/T69 回归锚全过 = `FamilyToFileName` 逐字等价 + 计数语义 + 缓存行为实证保持）；新用例六判别式全成立（Segoe 600==700≠400 / 300==400 / 900==700 · YaHei UI≠YaHei@400/700 · NSimSun · YaHei Light 独立族 · consola.ttf 直查豁免 · Consolas@650==@700 斜体排除 · ZH 回退边界）。

---

## §8 修订记录

- **v1.1**（2026-10-24）**外部评审「有条件通过」吸收——3×P0 + 3×P1 + 5×P2 + 4×P3 修毕 ⇒ PASS**（评审明示不退回初设；架构骨架复核成立）。① ★ **P0-1**：`ResolveFileCandidates` 补「family 即文件名」直查路径且首位（`Win32FontSource.cpp:135-148` 镜像）——ModelProbe `consola.ttf` 写法与 T36-1 八族回归获救；平手取下标小者 = legacy 优先序。② ★ **P0-2（采纳评审建议砍）**：ZH 族名可达断言全删——注册表键名无中文项（评审 reg query 实测）⇒ resolve 层不可能为 ZH 请求产生候选，v1.0 把 ZH 可达写在过滤层 = 逻辑倒置；回归非目标 3；`FaceEntry` 不引入 `nameId1Zh`（条 95）；T36-4 改边界钉住（ZH 请求 ⇒ 默认 face + 告警；GDI 可用 = 既有跨链不对称登记）。③ ★ **P0-3**：「等宽族 700==400 串宽」断言删除——`p36a2_readout.txt` [3] 实测 187→200 证伪（含 CJK 回退加粗干扰）；仪器教训入册（未经验证的常识不变量不作判据）；T36-2 收窄 SimSun 带。④ ★ **P1-4**：numFaces 读取顺序 = 先开 face0 再逐 index（p36c 正确写法 `:37-40`——hint 越界打开即失败拿不到 num_faces）。⑤ ★★ **P1-5 + P2-12 合并 = 档位来源换血**：评审本日 OS/2 探针 + 我方 p36c v2 扩列独立复核吻合（seguisb=600 · segoeuisl=350 · segoeuil=300 · seguibl=900 · consolab=700；唯一偏差 msyhl 真实 **290**）⇒ **档位 = face 自身 `OS/2.usWeightClass`**（样式词映射表废除）；**斜体排除 = `fsSelection` bit0**（consolai=1 / consolaz=33 实测——nameID1='Consolas' 能穿族名过滤）；候选结构瘦身为 `{filePath, faceIndexHint}`（P2-7 死参数删除，单参签名）；GDI tmWeight 与 usWeightClass 对真实字面逐一相等 = 双仪器互证。⑥ ★ **P1-6**：A2 双载体落位（库内 T36-2 串宽带 + 库外 A6 墨迹比复现），批次表补行。⑦ P2-8（FaceEntry 字段归 △7）· P2-9（T36-6 改 `MeasureTextCacheMissCount()` 观测缝；无缝键 = 盯防走查）· P2-10（T36-8 改断 face 身份，宽度同义反复废除）· P2-11（注释面补 `TextWidget.cpp:312`/`GDIBackend.h:119`/`GDITextMeasurer.h:80/:88`）· P3-13（familyCache = `:56`；措辞改「键一律含 weight」）· P3-14（490 = 运行自报口径备查）· P3-15（PickNearestTier 返回下标、WeightTier 删）· P3-16（空 family 键 = ("", ClampWeight(weight))）全部吸收。⑧ 评审正面确认（K8 穷尽性复核 · 行号 ±1 · Segoe@900 分歧自我撤回）记录在案。⑨ 头部 v1.0 → **v1.1，PASS**。
- **v1.0**（2026-10-24）初稿。探针 3 证伪初设 `family_name` 假设（nameID-16 偏好）⇒ 过滤直读 nameID-1。
