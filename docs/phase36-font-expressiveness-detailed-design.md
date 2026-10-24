# Phase 36 · Font 表达力 详细设计（字重 / face index / 复合族名第二项）

> 状态：**v1.0**（2026-10-24 落盘，**待评审**）｜输入 = [初设 v1.1](phase36-font-expressiveness-preliminary-design.md)（PASS）+ [需求稿 v1.1](phase36-font-expressiveness-requirements.md)｜上游 = deferred #70
> ★ **探针 3（p36c）= 初设移交的唯一实证项，本日完成并改写了设计**：`.workbuddy/spike/p36/p36c_ftnames.cpp` + `p36c_readout.txt`（clang-cl `/MD` 链**生产同款 vendored FreeType 2.14.3** 静态库——`cmake-build-release-visual-studio/freetype.dir/Release/freetype.lib`）。**核心发现：FT 的 `face->family_name` 优先返回 nameID-16（排版族）而非 nameID-1**——`msyhl.ttc` face0 返回 `family='Microsoft YaHei' style='Light'`（其 nameID1 = "Microsoft YaHei Light"）；`seguisb.ttf` 同型。⇒ **初设 §1.2「用 `family_name` 做 nameID-1 过滤」的假设被本探针证伪**：若照做，Light 会被误放进基族档位集（300 进入 `Microsoft YaHei` 的候选档）——恰是要防的两链分歧。⇒ 过滤实现改为**直读 nameID 1**（`FT_Get_Sfnt_Name`，读数列证明可行且 ZH 记录可读）。

---

## §1 详设必答收口（初设移交项）

### 1.1 FT name 实读读数（探针 3，全部 face）

| 文件 face | `family_name` | `style_name` | flags | PS | **nameID1-EN**（`FT_Get_Sfnt_Name`） | nameID1-ZH |
|---|---|---|---|---|---|---|
| msyh.ttc f0 | Microsoft YaHei | Regular | 0 | MicrosoftYaHei | Microsoft YaHei | 微软雅黑 |
| msyh.ttc f1 | **Microsoft YaHei UI** | Regular | 0 | MicrosoftYaHeiUI | Microsoft YaHei UI | Microsoft YaHei UI |
| simsun.ttc f0/f1 | SimSun / NSimSun | Regular | 0 | … | SimSun / NSimSun | 宋体 / 新宋体 |
| msyhbd.ttc f0/f1 | Microsoft YaHei / YaHei UI | Bold | **2** | …**-Bold** | Microsoft YaHei / YaHei UI | 微软雅黑 / 同 EN |
| **msyhl.ttc f0** | **Microsoft YaHei（!）** | Light | 0 | MicrosoftYaHeiLight | **Microsoft YaHei Light** | **微软雅黑 Light** |
| msyhl.ttc f1 | Microsoft YaHei UI（!） | Light | 0 | … | Microsoft YaHei UI Light | 同 EN |
| seguuisb（seguisb.ttf） | **Segoe UI（!）** | Semibold | 0 | SegoeUI-Semibold | **Segoe UI Semibold** | — |

### 1.2 过滤判据定案（替换初设 §1.2 第 2 层的实现表述）

- **判据 = nameID1 直读**：`MatchRequest(face) = (nameId1En == request) || (nameId1Zh == request)`——`nameId1En` = platform 3 · nameID 1 · language 0x409；`nameId1Zh` = 同 nameID · language 0x804；记录为 UTF-16BE → UTF-8（复用 Phase 26 `Win32FontSource` 的宽串转换纪律）。两字段在 **face 加载时读取一次**并存入 `FaceEntry`（faces 缓存不重复读）。
- **`face->family_name` / `style_name` / `style_flags` / PS 名 = 诊断信息，不作判据**（family_name 的 nameID16 偏好已被探针 3 证伪；flags 只区分 Bold 无法表达 300/600 档）。
- 跨文件同族名歧义（P3-①）：`msyh.ttc` 与 `msyhbd.ttc` 的 nameID1 **相同**——无碍：候选由 resolve 按样式词定位到**不同文件**，过滤只做「该 face 属不属于请求族」的布尔判定，文件身份在候选层已区分。
- **ZH 请求可达**：`family="微软雅黑"` ⇒ nameID1-ZH = "微软雅黑" ✓（msyh f0 命中；msyhbd f0 同名 ⇒ weight 700 档命中）。`family="微软雅黑 Light"` ⇒ zh1 = "微软雅黑 Light" ✓（独立族直达）。`Segoe UI` 系无 ZH 记录（zh1 空）⇒ EN 判据兜底 ✓。
- 读取失败（无 name 表/无记录）⇒ `nameId1*` 记空串 ⇒ 过滤判据恒 false ⇒ 该候选出局 + 告警（不静默）。

### 1.3 其余移交项状态

K8 十一项定键、兼容机制（默认参数/默认实现虚函数）、组合规则表 = 初设已定案，本稿 §2/§3 落到签名与逐文件粒度；勿重开清单继续有效。

---

## §2 接口与结构定稿

### 2.1 `FontSource.h`（△4）

```cpp
struct FontSourceCandidate {
    std::string filePath;      // 可 FT_New_Face 的路径（编码契约同 ResolveFile = ANSI/UTF-8 窄串）
    std::uint16_t weight;      // 该候选代表的档位（注册表样式词映射；无样式词 = 400）
    int faceIndexHint;         // 复合键匹配项位置（0 起；非复合 = 0）——仅提示，加载时验证
};
// 新虚函数，带默认实现（实现者零改动——Phase 32 S7 / #60 先例）：
virtual std::vector<FontSourceCandidate> ResolveFileCandidates(
    const std::string& family, std::uint16_t weight) const;
// 默认实现：{ ResolveFile(family) 非空 ⇒ 单候选 { path, /*weight=*/400, /*hint=*/0 } }
// ——单候选档位未知记 400 ⇒ 归并时它是唯一候选必被选中 ⇒ 非 Win32/测试源行为与 legacy 完全一致
```
**legacy `ResolveFile(family)` 签名与语义逐字不变**（`FontEngineTests.cpp:178/182/189`、`TextMeasurerTests.cpp:486`、CountingFontSource `:42` 零改动）。

### 2.2 `Win32FontSource`（△5/△6）

- 新增 `ResolveFileCandidates` 覆写：复用注册表全键枚举（提取内部助手 `EnumerateFontKeys(callback)` 供新旧两路共用——旧 `FamilyToFileName` 行为逐字保留）→ 键名剥后缀 → ` & ` 拆项 → 对**每一项**独立判「== family（3）/ family+样式词前缀（2）」→ 命中项产出候选 `{文件, 样式词→weight, 项序}`。
- **样式词→weight 映射表**（定稿）：`Bold→700 · Semibold→600 · Medium→500 · Regular→400 · Semilight→350 · Light→300 · Thin→250 · Black→900`；`Italic/Oblique` 等非 weight 词 = 跳过该项（weight 维非目标）；未知词 = 跳过 + 该候选不计。同键两项各自成候选（`Microsoft YaHei & Microsoft YaHei UI` ⇒ f0@400 + f1@400 两个 hint）。
- `FamilyToFileName`/`ResolveFile`/`DefaultFontFile` **零改动**。

### 2.3 `FontEngine`（△3）

- `FaceId FaceIdFor(const std::string& family, std::uint16_t weight = 400);`（默认参数 = 编译兼容；GL 侧调用点显式传参）。
- 流程：① 空 family ⇒ **legacy 路径原样**（`ResolveFile("") → DefaultFontFile`，不进候选机制——A1 逐位）；② `familyCache.find((family, weight))`；③ 候选为空 ⇒ 告警 + legacy 默认回退 + 入缓存（沿 `:211-215` 不静默契约）；④ 逐唯一路径：`FT_New_Face(path, hint)` → 读 `numFaces` → 在 0..numFaces−1 内**先 hint 后其余**找 `MatchRequest` 命中的 faceIndex（faces 键 = `(路径, faceIndex)`；`FaceEntry` 增 `nameId1En/nameId1Zh`）；命中 ⇒ 存活档 `{candidate.weight, 索引}`；⑤ 存活档集合 → `FontWeightRule::PickNearestTier(tiers, ClampWeight(weight))` → 选定 `(path, faceIndex)` ⇒ `familyCache.emplace((family, weight), id)`；⑥ 全部候选无存活 face ⇒ 告警 + legacy 默认回退 + 入缓存。
- **ClampWeight**：`std::min<std::uint32_t>(weight, 1000u)`（C36-1，两处消费点同式：FontEngine 归并前 + GDI `lfWeight` 前）。
- `measureCache`/`lineHeightCache` 键前置 weight（§2.4）。

### 2.4 缓存键定稿（K8 必改 7 处，tuple 字段序统一「weight 在最前」）

| 位置 | 新键类型 |
|---|---|
| `GDIBackend.h:120` | `std::map<std::tuple<std::uint16_t, float, std::string, int>, HFONT>` |
| `GDITextMeasurer.h:82` | 同上（HFONT） |
| `GDITextMeasurer.h:90` | `std::map<std::tuple<std::uint16_t, std::string, float, std::string, int>, Size>` |
| `FontEngine.cpp:61` measureCache | `std::map<std::tuple<std::uint16_t, std::string, float, std::string, int>, Size>` |
| `FontEngine.cpp:62` lineHeightCache | `std::map<std::tuple<std::uint16_t, float, std::string, int>, float>` |
| `FontEngine.cpp:55` familyCache | `std::map<std::pair<std::string, std::uint16_t>, FaceId>` |
| `FontEngine.cpp:53` faces | `std::map<std::pair<std::string, int>, FaceEntry>` |

### 2.5 `FontWeightRule.h`（新建，内部头，△13）

```cpp
struct WeightTier { std::uint16_t weight; std::size_t candidateIndex; };
// |w-request| 最小者；平手取轻（w 小者）；再平手取 candidateIndex 小者——与 GDI 探针 1 全格一致
std::size_t PickNearestTier(const std::vector<WeightTier>& tiers, std::uint16_t request);
inline std::uint16_t ClampWeight(std::uint32_t w) { return static_cast<std::uint16_t>(std::min<std::uint32_t>(w, 1000u)); }
```
纯函数无 FT 依赖 ⇒ T36-3 真值表零环境。GDI 链不消费（映射器即实现，探针 = 验证样本）。

### 2.6 其余调用点

`GLRenderer.cpp:229` ⇒ `FaceIdFor(font.family, font.weight)`（唯一 GL 解析点，显式传参）；`TextWidget` 两处指纹比较 +`.weight` + 注释键维顺移（wrap `:152-155`：font = 键维 2/3/4、DPI = 5、宽 = 6；preferred `:144`/`:444-452` 同步）；`Font.h` +`std::uint16_t weight = 400;`（`family` 之后[C7560]）+ 契约注释（0–1000 刻度 / 三层归并 / C36-4 合成分歧声明 / clamp 语义）。

---

## §3 逐文件差异 △1..△14

| △ | 文件 | 改动 |
|---|---|---|
| 1 | `ECDI/include/ECDI/Core/Font.h` | +`weight` 字段（family 后[C7560]）+ 契约注释 |
| 2 | `ECDI/src/Render/GDIBackend.h` | `:120` 缓存键类型 +weight（§2.4） |
| 3 | `ECDI/src/Render/GDIBackend.cpp` | `:1372` 键构造；`:1384` `lfWeight = static_cast<WORD>(ClampWeight(font.weight))`（ClampWeight 经 FontWeightRule.h 引入） |
| 4 | `ECDI/src/Render/GDITextMeasurer.h` | `:82`/`:90` 键类型 +weight；注释「必须完整覆盖 Font 语义字段」之下补 weight 实例 |
| 5 | `ECDI/src/Render/GDITextMeasurer.cpp` | `:51` 键构造；`:63` lfWeight 同 △3；`:97` 键构造 |
| 6 | `ECDI/src/Render/FontEngine.h` | `FaceIdFor` 签名 +weight 默认参；`FaceEntry` +`nameId1En/nameId1Zh` |
| 7 | `ECDI/src/Render/FontEngine.cpp` | FaceIdFor 流程重写（§2.3-④⑤⑥）；familyCache/faces/measureCache/lineHeightCache 键（§2.4）；`ReadFaceNameId1` 助手（FT_Get_Sfnt_Name，UTF-16BE→UTF-8，EN 优先 ZH 兜底） |
| 8 | `ECDI/src/Render/FontSource.h` | +`FontSourceCandidate` + `ResolveFileCandidates` 带默认实现（§2.1） |
| 9 | `ECDI/src/Platform/Win32/Win32FontSource.h` | +覆写声明 +`EnumerateFontKeys` 助手声明 |
| 10 | `ECDI/src/Platform/Win32/Win32FontSource.cpp` | 覆写实现 + 样式词映射表 + `EnumerateFontKeys` 提取（FamilyToFileName 改调助手，**行为逐字等价**——T26-6/T69 既有用例回归锚） |
| 11 | `ECDI/src/Render/GLRenderer.cpp` | `:229` `FaceIdFor(font.family, font.weight)` |
| 12 | `ECDI/include/ECDI/Widget/TextWidget.h` | `:144`/`:152-155` 注释键维顺移 |
| 13 | `ECDI/src/Widget/TextWidget.cpp` | `:320-336`/`:444-452` 两处比较 +`.weight` |
| 14 | `ECDI/src/Render/FontWeightRule.h` | **新建**（§2.5；不入 `include/ECDI/` ⇒ CMake 0[GLOB 收录自动成立，沿 Phase 29 先例]） |

**测试文件 0 改动**（兼容机制 = §2.1 默认实现 + §2.3 默认参数——T36-1 承诺）。

---

## §4 契约 C36-1..5（终稿）

1. **C36-1** `Font::weight` = 0–1000 刻度，默认 400；**clamp = 消费边界**（`ClampWeight` 两处：FontEngine 归并前 / GDI `lfWeight` 前；超界值不进任何缓存键——键存原值、消费时 clamp ⇒ 用户改回合法值不残留污染）。
2. **C36-2** 归并 = 候选集（注册表强度匹配）→ **nameID1 直读过虑**（EN/ZH 双判）→ 最近字面平手取轻；**不跨族**；`face->family_name` 不作判据（探针 3 证伪记录在案）。
3. **C36-3** 族名内嵌样式优先于 weight（weight 不降级既有样式——GDI 探针 p36a2 [2] 验证）。
4. **C36-4** 合成分歧 = 已知限制：族内无所选字面档时 GDI 合成（advance +7.6% 实测）、GL 不合成；face 相同、墨不同。
5. **C36-5** 公共 API 净变化 = `Font::weight` **+1 字段** + `FontSource::ResolveFileCandidates` **+1 带默认实现的虚函数**；face 可达性 = 内部实现细节。

---

## §5 测试 T36-1..8（精确断言与装置；490 → ~500）

| # | 装置 | 精确断言 |
|---|---|---|
| T36-1 | 全量回归 | 490 条全绿；`FontEngineTests`/`TextMeasurerTests` **源文件逐字节未改**（git diff 空）；FaceIdFor(family)（单参）对 #69 验收 8 族返回值与改前一致（默认参数路径 = legacy 语义） |
| T36-2 | 真窗口 GDI（沿 `TextMeasurerTests` 夹具形态） | `GDITextMeasurer::MeasureText`：SimSun `weight=400` vs `700` 串宽比 ∈ [1.05, 1.10]（探针读数 183/170 = 1.076 的回归带）；`Consolas`@700 与 @400 串宽相等（等宽族——判据独立于宽度，防误用） |
| T36-3 | `FontWeightRule` 纯函数（零环境） | 真值表：tiers={400,700}：400/500/550→400 档 · 560/600/650/700/900→700 档；tiers={400}：任意请求→0 号；**平手**：tiers={300,400}@350 → 300 档（取轻）；`ClampWeight(65535)=1000` |
| T36-4 | FT 链（真实 Win32FontSource） | `FaceIdFor("Microsoft YaHei UI") != FaceIdFor("Microsoft YaHei")`（face1 vs face0）；`FaceIdFor("Microsoft YaHei UI",700) != FaceIdFor("Microsoft YaHei",700)`（msyhbd f1 vs f0）；`FaceIdFor("NSimSun") != FaceIdFor("SimSun")`；`FaceIdFor("微软雅黑") == FaceIdFor("Microsoft YaHei")`（ZH/EN 同 face） |
| T36-5 | FT 链（兜底） | 构造 hint 失配 ⇒ 扫描命中（复合第二项 hint=1 场景即天然覆盖）；候选全灭（无 nameID1 匹配）⇒ 回退默认 face + 告警日志非空（不静默——断言 Warning 记录，沿 `:211-215` 契约） |
| T36-6 | 缓存键守护（K8） | `MeasureText` 同 `(family,size,dpi)` 变 weight：YaHei UI@400 vs @700 宽度不等（真字面）；重复调用各自命中（宽度复现）；**m_fontCache**（tuple 键）负对照 = 键比较含 weight（直接以 `std::tuple` 判等单测，无幽灵类型） |
| T36-7 | TextWidget 指纹 | SetStyle 变 weight ⇒ wrap 重排（FitText 调用数 +1，沿 T29-4 线性判据装置）；不变 weight ⇒ 命中（FitText 零调用） |
| T36-8 | FT 侧关系断言（真字面格） | `W("Segoe UI",600) == W("Segoe UI",700)`（Semibold 被滤 ⇒ 同落 700 档）且 `!= W("Segoe UI",400)`；`W("Segoe UI",300) == W("Segoe UI",400)`（Light 被滤）；`FaceIdFor("Microsoft YaHei Light") != FaceIdFor("Microsoft YaHei")`（独立族直达）。★ **跨链串宽对照不在库内**（无 WGL 测试环境，T26-9 先例）= A6/harness 库外探针（批三交付） |

**批次**：批一（红线）= △1/2/3/4/5 + T36-1/2；批二（解析链）= △6/7/8/9/10/11/14 + T36-3/4/5/6；批三（收口）= △12/13 + T36-7/8 + harness Release 库交付 + A6 复跑（固定版本指纹 + 字体 + DPI——Phase 31 A5 纪律）。

---

## §6 影响面（终稿）

公共头 **98 → 98** · 公共 API = **+1 字段（`Font::weight`）+ 1 带默认实现的虚函数（`FontSource::ResolveFileCandidates`）** · 用例 **490 → ~500**（T36-1..8 + 真值表展开）· CMake **0** · 内部改动 **14 文件**（§3，测试 0 改动）· 新增内部头 1（`FontWeightRule.h`）· 风险 **低**（口径全部探针钉死；三层规则纯函数化；兼容机制不破既有测试）。

**盯防九条**：① K8 七处必改键逐一核对（漏一 = 静默错）② `FT_Get_Sfnt_Name` UTF-16BE 转换（复用 `WideToUtf8` 前确认字节序——name 记录为大端）③ ZH 记录缺失时 EN 兜底、双缺时过滤出局 + 告警 ④ `GLRenderer.cpp:229` 显式传参（默认参数只保编译）⑤ legacy `ResolveFile`/`FamilyToFileName` 逐字等价（T26-6/T69 回归锚）⑥ GDI 边界 555 语义 = 探针验证样本，GL 归并纯函数与之对齐（T36-3 真值表）⑦ `ClampWeight` 两处消费点同式（C36-1）⑧ familyCache 含回退结果入缓存（v1.5 教训——T26-13 锚）⑨ MSVC 下 `std::min` 括号抑制宏污染（项目既有纪律）。

---

## §8 修订记录

- **v1.0**（2026-10-24）初稿。**探针 3（p36c）完成初设移交的唯一实证项并证伪一处初设假设**：FT `family_name` 优先返回 nameID-16（`msyhl.ttc` face0 = "Microsoft YaHei"/Light；`seguisb.ttf` = "Segoe UI"/Semibold）⇒ nameID-1 过滤**不得用 `family_name`**，改为 `FT_Get_Sfnt_Name` 直读（EN 0x409 优先 / ZH 0x804 兜底，FaceEntry 加载时缓存两字段）——ZH 请求与独立族直达（`微软雅黑 Light`）由此可达。**§2 接口定稿**（`FontSourceCandidate{filePath,weight,faceIndexHint}` · `ResolveFileCandidates` 默认实现语义 · FaceIdFor 六步流程 · 七处 tuple 键字段序「weight 最前」 · `FontWeightRule` 纯函数签名）· **§3 △1..14 逐文件**（FamilyToFileName 零改动 / EnumerateFontKeys 提取逐字等价）· **§4 C36-1..5 终稿**（C36-1 clamp 不进缓存键的细化）· **§5 T36-1..8 精确断言**（SimSun 串宽回归带 [1.05,1.10] · Segoe 600==700 / 300==400 过滤判别式 · 跨链对照归 A6 库外）+ 三批 · **§6 影响面终稿**（头 98→98 / +1 字段 +1 默认虚函数 / 490→~500 / CMake 0 / 风险 低）+ 盯防九条。
