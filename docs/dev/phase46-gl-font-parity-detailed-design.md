# Phase 46 GL/FreeType 字体能力对齐（#94）— 详细设计

> **版本**：`v1.2`（2026-11-15，第 6 轮终核 `P0×0 · P1×0 · P2×0` 通过；Batch 0/1/2 已实施收口）
> **上游文档**：[phase46-gl-font-parity-requirements.md](phase46-gl-font-parity-requirements.md)（✅ `v1.5`，`D46-0~5`、`CB-1`/`CB-2` 与 `T46-5/6` 守卫机制全数签字锁定）· [phase46-gl-font-parity-preliminary-design.md](phase46-gl-font-parity-preliminary-design.md)（✅ `v1.2`，放行进详设）
> **状态**：✅ 已实施并验证（Batch 0 -> Batch 1 -> Batch 2 收口，注册测试 `622`，公共头 `102`）
> **规模预算**：源码与测试共 **10** 个内部/测试文件（补列 `Win32FontSource.h`）· 注册测试 **`615 -> 622`**（`+7` 新用例对应 `T46-1, 2, 3, 5, 6, 7, 8`；`T46-4` 为 `+0` 原位契约更新）· `docs/` **`223`** · 公共头文件 **`102`**（不增删公共头文件）

---

## §0 架构不变量、红线与契约破坏台账

### 0.1 三条红线与详设落点

1. **红线 1（不变量与契约透明）**：
   - `GlyphKey{faceId, glyphIndex, pixelSize, hinting}`、`FontEngine` 全部已有渲染/测量/行高公共接口签名保持不变；唯一新增只读测试观测函数为 `FontEngine::LocalizedScanCountForTests() const noexcept` 与 `GLGlyphAtlas::AllocationFailureCount() const noexcept` / `IsFull() const noexcept`。
   - 仅更新 `Font.h`（`:23`、`:27`）与 `FontEngine.h`（`:92`）过时注释；不修改 `Font` 结构体字段或内存布局（`ABI` 零变更）。
   - 英文快路径维持既有精确匹配语义，不引入大小写折叠对既有行为造成静默改变。
2. **红线 2（单出口合成与不变量闭环）**：
   - 全仓 `FT_Load_*` 仅存在 `FontEngine.cpp:176`（`Impl::AdvanceOf`）与 `FontEngine.cpp:740`（`FontEngine::GlyphByKey`）两处。合成逻辑收敛为唯一静态辅助函数 `ApplySynthesis(FT_GlyphSlot, bool synthBold, bool synthItalic)`，保证 `AdvancePx`/`MeasureText`/`FitText` 与光栅化 `GlyphByKey` 严格同源。
   - `FreeTypeTextMeasurer` 仅透传共享 `FontEngine`，测画同源不变量自动成立。
3. **红线 3（零外部依赖与已知容量耦合例外）**：
   - `ftsynth.c`（`sources.ecdi.cmake:42`）与 `ftsnames.c`（`ftbase.c:34`）已编入工程，CMake 与构建脚本零改动。
   - GL 图集 `1024x1024` 单页不淘汰限制已在 `D46-0` 登记为与 Phase 47 的容量耦合已知例外：本 Phase 在 **Batch 0** 先落地 `GLGlyphAtlas` 饱和可观测守卫（`T46-8`），再启用合成状态。

### 0.2 已签字决策与定标常量锚点（`D46-0 ~ D46-5`）

| 门控 | 拍板决策 | 详设常量 / 公式 |
|---|---|---|
| **D46-0** | 图集满可观测守卫前置 | 新增 `GLGlyphAtlas::AllocationFailureCount()` 与 `IsFull()`（曾饱和语义）；`Batch 0` 先合入并通过 `T46-8` |
| **D46-1** | 合成粗体阈值、`ceil` 公式与垂直零偏移 | 触发阈值 `ClampWeight(reqWeight) >= 551 && rawWeight < 551`；`ppem = slot->face->size->metrics.x_ppem`；`xdelta = (65536 + ppem - 1) / ppem`、`ydelta = 0`；维持 `FT_LOAD_TARGET_NORMAL`；五档 `12..32px` 双串 `abs(FT_ratio - GDI_ratio) <= 0.02`（`Consolas` 改验真粗体零漂移） |
| **D46-2** | 虚拟 `FaceEntry` 隔离与显式契约破坏 `CB-1` | 物理句柄唯一由 `rawFaces` 持有（析构唯一入口）；逻辑绑定 `faces[(path, index, synthBold, synthItalic)] -> FaceId`；作废 `I45-italic-fallback`，原位更新 `FontWeightTests.cpp:201, :208` 与 `WidgetTests.cpp:2931` |
| **D46-3** | 合成斜体倾角与控件级右裁剪边界 | `reqItalic && !rawItalic` 触发 `FT_GlyphSlot_Oblique`（零 `advance` 增量）；叠加顺序恒为 **`B -> O`**（先粗后斜）；`20px` 标定集质心右偏 `>= 1.2px`；图集按增大后 `bitmap.width` 完整保存，斜切（`1~4px`）/粗体（`<=1px`）左/右探出均由控件 `PushClip` 边界裁剪 |
| **D46-4** | 中文族名惰性解析与显式契约破坏 `CB-2` | 英文快路径零冷扫 + Miss 时惰性 `platform_id == 3 && name_id == 1` 多语种解析；作废 **`P0-② ZH 边界钉`**（`CB-2` 已补签），原位改 `FontWeightTests.cpp:131-132` 为 `FaceIdFor("微软雅黑") == FaceIdFor("Microsoft YaHei")` 且 `!= 默认` |
| **D46-5** | `FT_Get_Sfnt_Name` 扫描边界与 pimpl 隔离 | `FontSource` 只新增枚举安装字体路径虚函数，`FT_New_Face` 单趟扫描（`face0 + 1..num_faces-1`，SFNT `name_count` 上限 `1024`，`(path, faceIndexHint)` 去重，`n1-only`）收拢在 `FontEngine.cpp` 内部，Zero CMake 改动 |

### 0.3 契约破坏台账（`CB-1` x 3 + `CB-2` x 1，合计 `T46-4: +0`）

| 编号 | 位置 | 旧契约断言 | 新契约预期与修改方案 | 归属批次 |
|---|---|---|---|---|
| **CB-1a** | `FontWeightTests.cpp:201` | `Test45ItalicFaceSelection` 中断言 `EXPECT_EQ(simsunIta, simsunReg)`（`I45-italic-fallback`） | 改为 `EXPECT_NE(simsunIta, simsunReg)`（合成斜体占独立 `FaceId`）并更新 `:196` 注释 | Batch 1 |
| **CB-1b** | `FontWeightTests.cpp:208` | `Test45ItalicFaceSelection` 中复查断言 `EXPECT_EQ(engine->FaceIdFor("SimSun", 400, true), simsunReg)` | 改为 `EXPECT_EQ(engine->FaceIdFor("SimSun", 400, true), simsunIta)`（命中同一合成斜体缓存项） | Batch 1 |
| **CB-1c** | `WidgetTests.cpp:2931` | `Test45StyledSpansTwoChainConsistency` 中断言 `EXPECT_EQ(idCjkReg, idCjkItalic)` | 改为 `EXPECT_NE(idCjkReg, idCjkItalic)`（CJK 回落合成斜体获得独立 `FaceId`） | Batch 1 |
| **CB-2** | `FontWeightTests.cpp:131-132` | `Test36FaceIdPerFamilyAndWeight` 中的 `// P0-② 边界钉：ZH 族名不解析 ⇒ 回退默认 face` 与 `EXPECT_EQ(engine->FaceIdFor("微软雅黑"), engine->FaceIdFor(std::string()))` | 作废 `P0-②` 边界钉并更新注释与断言：`const FaceId zhYahei = engine->FaceIdFor("微软雅黑"); EXPECT_EQ(zhYahei, yahei); EXPECT_NE(zhYahei, engine->FaceIdFor(std::string()));`（即 `FaceIdFor("微软雅黑") == FaceIdFor("Microsoft YaHei") != 默认`；`"宋体" == "SimSun"` 作为 `T46-5` 附加断言，不替代本签字断言） | Batch 2 |

---

## §1 逐文件详细设计（共 10 个内部/测试文件）

### 1.1 `ECDI/src/Render/GLGlyphAtlas.h` & `ECDI/src/Render/GLGlyphAtlas.cpp`（Batch 0）

- **设计目标**：保持 `1024x1024`（`kSize = 1024`）行货架分配与 `GetOrCreate` 负缓存行为不变，增加图集容量触顶可观测守卫。
- **成员与接口变更**（`GLGlyphAtlas.h`）：
  - 在 `GLGlyphAtlas` 私有字段新增：`std::size_t m_allocationFailures = 0;`（沿用已有 `bool m_fullWarned = false;`）。
  - 新增两个公共只读方法：
    ```cpp
    // 自构建起累计因图集空间不足导致的 Allocate 失败次数（不含非法尺寸拒绝）。
    std::size_t AllocationFailureCount() const noexcept { return m_allocationFailures; }

    // 图集是否曾触达饱和阈值（m_fullWarned 语义：首次因空间不足失败置位后保持为 true）。
    bool IsFull() const noexcept { return m_fullWarned; }
    ```
- **实现细节**（`GLGlyphAtlas.cpp`）：
  - **唯一递增落点**：仅在 `GLGlyphAtlas::Allocate`（`GLGlyphAtlas.cpp:72-81`）`if (m_shelfY + h > kSize)` 图集满分支内紧接 `!m_fullWarned` 日志守卫递增一次 **`++m_allocationFailures;`** 并保证 `m_fullWarned = true;`；
  - `GLGlyphAtlas.cpp:60-63`（`w <= 0 || h <= 0 || w > kSize || h > kSize`）属于参数非法拒绝而非图集满，**不递增** `m_allocationFailures` 亦不置 `m_fullWarned`；
  - `GetOrCreate` 仅透传调用 `Allocate` 结果并继续写回负缓存（`GLGlyphAtlas.cpp:174`），不在 `GetOrCreate` 中另设重复计数或额外告警分支，确保每次真实满容失败严格 `+1`。

### 1.2 `ECDI/src/Render/FontEngine.h`（Batch 1/2）

- **注释清理（Batch 2）**：更新 `FontEngine.h:92`，移除 `"ZH 族名不解析（需求非目标 3——注册表键名无中文项）"` 过期说明，改为说明支持合成粗体/斜体与惰性中文族名索引解析。
- **新增测试观测接口（Batch 2）**（紧邻现有 `MeasureTextCacheMissCount()` / `AdvanceMemoMissCount()` / `RasterizeCount()` 观测区）：
  ```cpp
  // 返回惰性本地化族名全库扫描的累计触发次数（英文快路径与空 family 全程 0，首次未命中扫描后恒为 1）。
  std::size_t LocalizedScanCountForTests() const noexcept;
  ```
- 既有 `FaceIdFor`、`GlyphIndex`、`AdvancePx`、`MeasureText`、`FitText`、`LineHeight`、`Ascent`、`Glyph`、`GlyphByKey` 签名与默认值零变更。

### 1.3 `ECDI/src/Render/FontEngine.cpp`（Batch 1/2）

#### 1.3.1 头文件引入、双层对象模型与 `facesById` 1-based 索引约定

- 在 `FontEngine.cpp` 顶部增加 `#include FT_SYNTHESIS_H`（与已有 `FT_SFNT_NAMES_H` 同在 `.cpp` 内部使用，遵守 `:9` pimpl 隔离）。
- 双层结构定义：
  ```cpp
  struct RawFaceKey {
      std::string filePath;
      int faceIndex = 0;
      auto operator<=>(const RawFaceKey&) const = default;
  };

  struct RawFaceEntry {
      FT_Face face = nullptr;          // 唯一所有权：仅由 ~FontEngine() 遍历 rawFaces 调用 FT_Done_Face
      long numFaces = 1;
      std::string nameId1En;
      std::vector<std::string> nameId1All;
      bool nameId1AllLoaded = false;
      std::uint16_t os2Weight = 400;
      bool italic = false;
  };

  struct BoundFaceKey {
      std::string filePath;
      int faceIndex = 0;
      bool synthBold = false;
      bool synthItalic = false;
      auto operator<=>(const BoundFaceKey&) const = default;
  };

  struct BoundFaceEntry {
      FT_Face face = nullptr;          // 非拥有指针，寿命由 rawFaces 保证
      bool synthBold = false;
      bool synthItalic = false;
  };
  ```
- `Impl` 成员与 **1-based `facesById` 向量约定**：
  - `std::map<RawFaceKey, RawFaceEntry> rawFaces;`
  - `std::map<BoundFaceKey, FaceId> faces;`
  - `std::vector<BoundFaceEntry> facesById;` — **构造/初始化时预先填入 `slot 0` 空哨兵（`facesById.emplace_back();`，保证 `facesById[0].face == nullptr` 对应 `0` 恒无效约定、`nextFaceId = 1` 每次顺序 `push_back` 后严格满足 `facesById.size() == nextFaceId`，下标即 `FaceId`）**；
  - 新增边界安全反查与兼容接口：
    ```cpp
    const BoundFaceEntry* BoundFaceOf(FaceId id) const noexcept {
        return (id > 0 && static_cast<std::size_t>(id) < facesById.size())
            ? &facesById[static_cast<std::size_t>(id)]
            : nullptr;
    }
    FT_Face FaceOf(FaceId id) const noexcept {
        const BoundFaceEntry* b = BoundFaceOf(id);
        return b ? b->face : nullptr;
    }
    ```
  - `std::unordered_map<std::string, std::vector<FontSourceCandidate>> localizedIndex;`
  - `bool localizedIndexBuilt = false;`
  - `std::size_t localizedScanCount = 0;`

#### 1.3.2 合成判定与唯一出口 `ApplySynthesis`

```cpp
inline bool NeedSynthBold(std::uint16_t rawWeight, std::uint16_t reqWeight) noexcept {
    return ClampWeight(reqWeight) >= 551 && rawWeight < 551;
}

inline bool NeedSynthItalic(bool rawItalic, bool reqItalic) noexcept {
    return reqItalic && !rawItalic;
}

void ApplySynthesis(FT_GlyphSlot slot, bool synthBold, bool synthItalic) noexcept {
    if (!slot || !slot->face || !slot->face->size) {
        return;
    }
    // 严格按 B -> O 顺序：先水平加粗再斜切（oR 更紧凑）。
    if (synthBold) {
        const FT_Pos ppem = slot->face->size->metrics.x_ppem;
        if (ppem > 0) {
            const FT_Pos xdelta = (65536 + ppem - 1) / ppem;
            FT_GlyphSlot_AdjustWeight(slot, xdelta, 0);
        }
    }
    if (synthItalic) {
        FT_GlyphSlot_Oblique(slot);
    }
}
```

- **两个调用点（全仓仅此两处）**：
  1. `Impl::AdvanceOf(...)`（`FontEngine.cpp:176` `FT_Load_Char` 之后、`:185`/`:192` `advance.x >> 6` 之前）：
     - 查找 `const BoundFaceEntry* bound = BoundFaceOf(faceId);`
     - `FT_Load_Char(face, ...)` 成功后立即执行 `ApplySynthesis(face->glyph, bound ? bound->synthBold : false, bound ? bound->synthItalic : false);`
     - **不变量声明**：后续重构严禁在 `AdvanceOf` 中引入任何绕过 `ApplySynthesis` 的 `advance` 读取快路径，确保 `AdvanceOf` 与 `GlyphByKey` 逐像素同源。
  2. `FontEngine::GlyphByKey(...)`（`FontEngine.cpp:740` `FT_Load_Glyph` 之后、`:745` `FT_Render_Glyph` 与 `:756` `advance.x >> 6` 之前）：
     - 查找 `const BoundFaceEntry* bound = impl.BoundFaceOf(key.faceId);`（为空则 `return false;`）
     - `FT_Load_Glyph(bound->face, ...)` 成功后立即执行 `ApplySynthesis(bound->face->glyph, bound->synthBold, bound->synthItalic);`
     - 随后再调用 `FT_Render_Glyph` 及读取 `advance.x >> 6`、bitmap 宽高与 `bitmap_left`/`bitmap_top`。
- `LineHeight` / `Ascent` 仍仅读取 `face->size->metrics`（`ydelta = 0` 且 `Oblique` 不改变垂直度量），与 `GDI` 一致。

#### 1.3.3 `EnsureRawFace`、`BindFace`、本地化族名过滤判据与双池回落保形

- **大小写策略（全程统一精确 UTF-8 字节匹配，不进行 ASCII 小写折叠）**：
  - 英文快路径（`ResolveFileCandidates` -> `nameId1En == family`）与本地化路径（`localizedIndex[utf8Name]` 键值及 `nameId1All` 元素匹配）均采用 **精确 `std::string` 相等比较 (`==`)**。由此严格遵守红线 1（例如 `"SIMSUN"` 仍按 Miss 处理，不会因隐式小写折叠突然命中 `"SimSun"` 改变既有行为），同时 Windows SFNT `platform_id == 3 && name_id == 1` 本身同时提供标准中文/英文族名正写。
- **`EnsureRawFace(path, index)` 与惰性 `EnsureNameId1All(RawFaceEntry&)`**：
  - `EnsureRawFace(path, index)` 按 `(path, static_cast<int>(index))` 唯一打开 `FT_Face`，冷打开仅调用既有 `ReadNameId1En(face)` 并读 `OS/2`，设 `nameId1AllLoaded = false`，英文快路径 **零额外 SFNT 全表开销**；
  - 新增 `EnsureNameId1All(RawFaceEntry& entry)`：
    ```cpp
    void EnsureNameId1All(RawFaceEntry& entry) {
        if (entry.nameId1AllLoaded || !entry.face) {
            return;
        }
        entry.nameId1AllLoaded = true;
        const FT_UInt count = (std::min)(FT_Get_Sfnt_Name_Count(entry.face), 1024u);
        for (FT_UInt i = 0; i < count; ++i) {
            FT_SfntName n{};
            if (FT_Get_Sfnt_Name(entry.face, i, &n) != 0) {
                continue;
            }
            if (n.platform_id == 3 && n.name_id == 1) {
                std::string s = Utf16BEToUtf8(n.string, n.string_len);
                if (!s.empty() &&
                    std::find(entry.nameId1All.begin(), entry.nameId1All.end(), s) == entry.nameId1All.end()) {
                    entry.nameId1All.push_back(std::move(s));
                }
            }
        }
    }
    ```
- **`BindFace(path, index, raw, reqWeight, reqItalic)`**：
  ```cpp
  const bool synthBold = NeedSynthBold(raw.os2Weight, reqWeight);
  const bool synthItalic = NeedSynthItalic(raw.italic, reqItalic);
  ```
  查/插 `faces[{path, index, synthBold, synthItalic}]`；未命中则分配 `id = nextFaceId++` 并 `facesById.push_back({raw.face, synthBold, synthItalic});`。
- **`FaceIdFor(family, weight, italic)` 解析与过滤判据**：
  1. 命中 `familyCache` 或空 `family` -> 走既有缓存或 `fallbackToDefault(weight, italic)`（先 `EnsureRawFace(defaultPath, 0)`，再 `BindFace(..., weight, italic)` 保留合成需求）。
  2. 先查英文快路径：`candidates = impl.source->ResolveFileCandidates(family); bool usedLocalizedIndex = false;`；
     - 仅当 `candidates.empty()` 时触发 `impl.EnsureLocalizedIndex();` 并查 `localizedIndex.find(family)`，若命中则赋给 `candidates` 并置 **`usedLocalizedIndex = true;`**。
  3. 遍历 `PathGroup` 各 `(g, idx)` 加载 `RawFaceEntry* raw = impl.EnsureRawFace(g.path, idx);`，**完整族名过滤判据如下**：
     - 快路径（`usedLocalizedIndex == false`）：
       `const bool nameMatch = g.direct || (raw->nameId1En == family);`（**不触发 `EnsureNameId1All`**，英文快路径零增量开销）。
     - 本地化路径（`usedLocalizedIndex == true`）：
       若 `!(g.direct || raw->nameId1En == family)`，则调用 **`impl.EnsureNameId1All(*raw)`** 后判 `std::find(raw->nameId1All.begin(), raw->nameId1All.end(), family) != raw->nameId1All.end()`；据此确保请求 `"宋体"` 只通过 `simsun.ttc#0` 而精确排除 `#1` `"新宋体"`，请求 `"微软雅黑"` 只通过 `msyh.ttc#0` / `msyhbd.ttc#0` 而精确排除 `#1` `"Microsoft YaHei UI"` 与 `msyhl.ttc#0` `"微软雅黑 Light"`。
  4. **单趟收集斜体/常规双池 & 回落保形**：
     - 所有 `nameMatch == true` 的面按 `raw->italic` 分别加入：
       - 若 `raw->italic`：入 `italicTiers` / `italicLoc`；
       - 若 `!raw->italic`：入 `regularTiers` / `regularLoc`。
     - 若 `italic == true` 且 `italicTiers` 非空：从 `italicTiers` 经 `PickNearestTier` 选面 -> `BindFace(..., weight, /*reqItalic=*/true)`（此时 `raw->italic == true` 故 `synthItalic == false`，只视字重判 `NeedSynthBold`）；
     - 若 `italic == true` 且 `italicTiers` 为空但 `regularTiers` 非空：从 `regularTiers` 经 `PickNearestTier` 选常规物理面 -> `BindFace(..., weight, /*reqItalic=*/true)`（此时 `raw->italic == false` 故 **`synthItalic == true`**，如 `"宋体" @700, true` 得 `synthBold=true, synthItalic=true`，`"微软雅黑" @700, true` 得 `synthBold=false, synthItalic=true`）；
     - 若 `italic == false` 且 `regularTiers` 非空：从 `regularTiers` 选面 -> `BindFace(..., weight, false)`；
     - 若对应物理候选皆空：写 Warning 日志并调用 `fallbackToDefault(weight, italic)`（保留 `(weight, italic)` 合成标记）。

#### 1.3.4 惰性本地化族名索引 `EnsureLocalizedIndex`

- 仅当 `FaceIdFor` 英文快路径未命中（且 `family` 非空）时调用；进程内至多执行一次（`if (localizedIndexBuilt || !source) return; localizedIndexBuilt = true; ++localizedScanCount;`）。
- 单趟遍历 `source->EnumerateInstalledFiles()` 返回路径列表：
  - 先用 `FT_New_Face(library, path.c_str(), 0, &face0)` 打开读取 `numFaces` 与 `face0` 的 SFNT 记录，立即 `FT_Done_Face(face0)`；
  - 续用 `FT_New_Face(library, path.c_str(), idx, &subFace)` 扫 `1..numFaces-1`，读取后立即 `FT_Done_Face(subFace)`（常驻句柄为 0）。
- `RecordLocalizedNames(FT_Face face, const std::string& path, int faceIndex)`：
  - `const FT_UInt count = (std::min)(FT_Get_Sfnt_Name_Count(face), 1024u);`
  - 仅收集 `s.platform_id == 3 && s.name_id == 1`，以 `Utf16BEToUtf8(s.string, s.string_len)` 精确 `utf8Name` 为键插入 `localizedIndex[utf8Name]`（**不做 ASCII 小写折叠**），插入前对同键下的 `(filePath, faceIndexHint)` 去重。

### 1.4 `ECDI/src/Render/FontSource.h` · `ECDI/src/Platform/Win32/Win32FontSource.h` & `Win32FontSource.cpp`（Batch 2）

- **`FontSource.h`**：
  - 在 `FontSource` 中新增带默认空实现的虚方法：
    ```cpp
    // 枚举系统已安装字体文件路径（供 FontEngine 惰性构建本地化族名索引）。
    virtual std::vector<std::string> EnumerateInstalledFiles() const { return {}; }
    ```
  - 保留 `ResolveFile`、`ResolveFileCandidates` 与 `FontSourceCandidate` 定义零破坏。
- **`Win32FontSource.h`**（`Win32FontSource.h:34-49`，内部平台头，公共头文件 `102` 不变）：
  - 在 `class Win32FontSource : public FontSource` 公共接口区（紧随 `ResolveFileCandidates` `:43` 之后）新增声明：
    ```cpp
    std::vector<std::string> EnumerateInstalledFiles() const override;
    ```
- **`Win32FontSource.cpp`**：
  - **保持既有 `ResolveFileCandidates`（`:291-359`，含 `:351`）逐字不变**，不做未声明的英文快路径行为扩张。
  - 实现 `Win32FontSource::EnumerateInstalledFiles() const override`：
    1. 获取 `const std::wstring dir = FontsDirectoryWide();`
    2. 调用既有 `ForEachFontRegistryKey([&](const std::string& /*keyU8*/, const std::string& fileU8) { ... });`（与 `Win32FontSource.cpp:167-168` `std::function<void(const std::string&, const std::string&)>` 签名完全匹配）；
    3. `const std::wstring wideFile = UTF8ToWide(fileU8);` 若为空跳过；若 `wideFile.find(L':') != std::wstring::npos || wideFile.rfind(L"\\\\", 0) == 0` 视为绝对路径（覆盖实测 `KingsoftSymbol.ttf`），否则构造 `dir + L"\\" + wideFile`；
    4. `FileExistsWide(resolved)` 校验通过后，沿用与 `:314` / `:356` 完全一致的 **`WideToAnsi(resolved)`** 转码（使 `rawFaces` 物理路径与英文快路径、探针、`FT_New_Face` 参数处于同一编码键域，杜绝非 ASCII 路径打不开及同文件双键双句柄；若 `WideToAnsi` 返回空串则跳过）；按路径集合去重后返回。

### 1.5 `ECDI/include/ECDI/Core/Font.h`（Batch 2）

- 仅更新注释，零字段与 ABI 变更（`float size = 14.0f; std::string family; std::uint16_t weight = 400; bool italic = false;`）：
  - `:23`：改为说明 GDI 与 GL/FreeType 均在 `weight >= 551` 且无更重物理档时合成水平加粗（保留 `551..699` 真粗体族沿用 `PickNearestTier` 等既有分档语义）。
  - `:27`：改为说明无真斜体面时 GL/FreeType 自动按 `B -> O` 合成斜切并在回落链保留合成标记（右探出由控件 `PushClip` 裁剪）。

### 1.6 `ECDI/src/Tests/FontWeightTests.cpp` & `ECDI/src/Tests/WidgetTests.cpp`（Batch 0/1/2）

- **契约改动（`T46-4`，+0 用例，4 处原位改造）**：
  - `FontWeightTests.cpp:201` -> `EXPECT_NE(simsunIta, simsunReg)`（`CB-1a`）
  - `FontWeightTests.cpp:208` -> `EXPECT_EQ(engine->FaceIdFor("SimSun", 400, true), simsunIta)`（`CB-1b`）
  - `WidgetTests.cpp:2931` -> `EXPECT_NE(idCjkReg, idCjkItalic)`（`CB-1c`）
  - `FontWeightTests.cpp:131-132` -> 删去 `P0-②` 旧注释与断言，改为：`const FaceId zhYahei = engine->FaceIdFor("微软雅黑"); EXPECT_EQ(zhYahei, yahei); EXPECT_NE(zhYahei, engine->FaceIdFor(std::string()));`（`CB-2`）
- **新增 7 个注册测试用例（`615 -> 622`，与 `T46-1, 2, 3, 5, 6, 7, 8` 一一对应）**：

| # | 注册测试名 | 覆盖 `T46-*` | 核心签字断言清单 | 归属批次 |
|---|---|---|---|---|
| 1 | `FontWeight.GlyphAtlasFullObservability` | `T46-8` | 先传入非法尺寸（如 `0`、`kSize + 1`）验证 `AllocationFailureCount() == 0 && !IsFull()`；随后用大字形填满 `1024x1024` 再继续请求 `Allocate`：断言 `slot.valid == false`、`IsFull() == true`、`AllocationFailureCount() == N` 与满容拒绝次数精确一致、不越界崩溃 | Batch 0 |
| 2 | `FontWeight.SyntheticBoldWidthRatioParity` | `T46-1` | ① `SimSun`、`SimHei`、`Microsoft YaHei` 在 `12/16/20/24/32px` 对纯 CJK `"测试文本中文"` 与混合 `"测试文本 test 123"` 两相各自校验 `abs(FT_ratio - GDI_ratio) <= 0.02` 且 `xstr >= 64` 无吞宽；② `Consolas` 校验真粗体 `400/700` 输出与改前基准零漂移；③ `FreeTypeTextMeasurer::MeasureText` 与逐码点 `Glyph::advance` 之和完全相等；④ 验证 `w=550` 不合成与 `w=551` 合成阈值边界 | Batch 1 |
| 3 | `FontWeight.SyntheticObliqueSlantGeometry` | `T46-2` | `20px` 下 `SimSun`/`Microsoft YaHei`（`{'测','国','H','W'}`）与 `Tahoma`（`{'H','W','A','I'}`）请求 `italic=true` 顶部质心右偏 **`>= 1.2px`**；同字集常规态 `italic=false` **`abs(dx) <= 0.4px`**；合成斜切 **`MeasureText` 宽度增量 `== 0`**、`LineHeight`/`Ascent` 零变化；真斜体族 `Segoe UI` / `Consolas` 在 `italic=true` 时不二次倾斜 | Batch 1 |
| 4 | `FontWeight.SynthesisIsolationAndCacheParity` | `T46-3` | `SimSun` 四态 `(400,F)` / `(700,F)` / `(400,T)` / `(700,T)` 产出 **4 个互异 `FaceId`**；共享同一物理 `FT_Face` 句柄作用域析构安全不重复释放；`(700, T)` 总墨量 **`>` `(400, T)`** 且具斜切质心右偏 `>= 1.2px`（验证 `B -> O`）；四态交替读写六组缓存（`familyCache` / `measureCache` / `ascentCache` / `lineHeightCache` / `advanceCache` / `glyphCache`）零投毒 | Batch 1 |
| 5 | `FontWeight.ChineseFamilyEndToEndResolution` | `T46-5` | 中文系统（或对应字体文件存在）时**禁止静默跳过**；`TestRunner`（`TestFramework.h:26-30`）无独立 Skip 态，仅在非中文 CI 缺对应字体文件时按**方案 ⓐ（文件存在性分段守卫 + `Logger::Warning` 留痕 + 提前 return，记为 PASS 但实跑汇总须明确标注「守卫性空转」）**放行：`simsun.ttc`、`msyh.ttc`、`simhei.ttf`（及 `simkai.ttf`/`simfang.ttf`/`Deng.ttf`）存在时硬断言 `"宋体" == "SimSun"`、`"新宋体" == "NSimSun"`（`simsun.ttc#1`）、`"微软雅黑" == "Microsoft YaHei"`、`"黑体" == "SimHei"`、`"楷体" == "KaiTi"`、`"仿宋" == "FangSong"`、`"等线" == "DengXian"` 不仅 `FaceId` 匹配，且 `MeasureText` 与 `GlyphByKey` 位图（宽高、`bearing`、`advance`、`a8`）**逐位等价**；另验证 `"微软雅黑 UI"` 在 `n1-only` 下预期 miss 回退默认 | Batch 2 |
| 6 | `FontWeight.ChineseFamilySynthesisAndFallback` | `T46-6` | `"微软雅黑" @700, italic=true` 选中真粗体 `msyhbd.ttc#0` + 合成斜切；`"宋体" @700, italic=true` 合成粗+斜且与三态互异；未知中文名 `"不存在字体XYZ"`（`@400,F` 与 `@700,T`）安全告警回落默认 `SimSun` 不崩溃且 `@700,T` 在回落面上保留粗斜合成态（仅在非中文 CI 缺失 `msyh.ttc`/`simsun.ttc` 时同 `T46-5` 方案 ⓐ 走文件存在性守卫性提前返回 + `Logger::Warning` 留痕；中文 Windows 上必须实跑全部断言） | Batch 2 |
| 7 | `FontWeight.EnglishFastPathZeroScanAndParity` | `T46-7` | 通过 `LocalizedScanCountForTests()` 验证查询 `"consola.ttf"`、`"Consolas"`、`"SimSun"`、`""` 全程 **`scanCount == 0`**（零冷扫描）；验证英文 `@400` 常规 `MeasureText`/`LineHeight`/`Ascent`/`AdvancePx`/`GlyphByKey` 输出与原契约零漂移、`"SIMSUN"` 等大小写不匹配输入按精确契约不触发隐式小写改道；随后触发中文/miss 查找一次后 `scanCount` 变为 `1` 且多次重复/miss 保持为 `1` | Batch 2 |

---

## §2 分批实施计划与验收闸门

1. **Batch 0（图集饱和守卫前置，测试 `615 -> 616`）**：
   - 修改 `GLGlyphAtlas.h` / `GLGlyphAtlas.cpp` 与 `FontWeightTests.cpp`（注册 `FontWeight.GlyphAtlasFullObservability`）。
   - **闸门**：MinGW 构建零警告通过；`ECDI_Tests.exe` 全部 **616** 用例通过。
2. **Batch 1（合成粗体/斜体核心 + `CB-1`，测试 `616 -> 619`）**：
   - 修改 `FontEngine.cpp`（双层 `rawFaces`/`faces`/`facesById` 1-based、唯一出口 `ApplySynthesis` 两点注入、单趟双池选面与回落保形）、`FontWeightTests.cpp`（`CB-1a`/`CB-1b` + `T46-1`/`T46-2`/`T46-3` 共 3 条测试）、`WidgetTests.cpp`（`CB-1c`）。（此批不依赖 `LocalizedScanCountForTests()`；**Batch 1 interim**：`EnsureLocalizedIndex` 尚未接入，`candidates.empty()` 暂维持既有 `Warning` 告警 + `fallbackToDefault(weight, italic)` 行为不变，待 `Batch 2` 接通本地化索引。）
   - **闸门**：MinGW 构建零警告通过；`ECDI_Tests.exe` 全部 **619** 用例（含 `Test45*` 与 `Test36*`）通过。
3. **Batch 2（中文族名惰性索引 + `CB-2` + 注释同步，测试 `619 -> 622`）**：
   - 修改 `FontSource.h`、`Win32FontSource.h`、`Win32FontSource.cpp`、`FontEngine.h`（`:92` 注释 + `LocalizedScanCountForTests()`）、`FontEngine.cpp`（`EnsureNameId1All` + `EnsureLocalizedIndex`）、`Font.h`（注释）、`FontWeightTests.cpp`（`CB-2` + `T46-5`/`T46-6`/`T46-7` 共 3 条测试）。
   - **终闸**：本机（中文 Windows 存在全部测试字体文件）MinGW 与 Clang 双链构建零警告通过且 `ECDI_Tests.exe` **622** 用例**全绿且 `0` 守卫触发**；非中文 CI 缺字体文件时，仅允许 `T46-5`/`T46-6` 触发 **§3 方案 ⓐ（文件存在性守卫性提前返回 + `Logger::Warning` 留痕）**、其余全绿，且实跑汇总必须如实披露触发「守卫性空转」的字体与分支；同步最终指标并运行 `python tools/doc_lint.py` 达成 `0 defect, 0 style`。

---

## §3 边界、非目标与风险控制

- **非中文 CI 文件缺失守卫机制钉死（方案 ⓐ，零测试框架改动）**：
  - 鉴于 `ECDI/src/Tests/TestFramework.h:26-30` 的 `TestResult` 仅有 `passed` 布尔与 `Passed/Failed` 两计数、全测试目录不存在独立 Skip 状态，为严格维持 10 文件预算不修改 `TestFramework.h/.cpp`，选用 **方案 ⓐ（文件存在性分段守卫 + `Logger::Warning` 留痕 + 提前 `return`）**：
    - 在 `T46-5` 与 `T46-6` 中，通过字体目录检测目标 `.ttc`/`.ttf` 文件存在性；仅当目标字体物理文件确实不在磁盘上（非中文 CI）时，输出 `Logger::Log(LogLevel::Warning, ...)` 明确标出缺失字体与跳过分支并提前 `return`（禁止无日志静默 `return`；中文 Windows 上若文件存在但解析失败必须 hard fail）；
    - `TestRunner` 将该分支计为 `PASS`；实跑汇总报告必须如实核对日志，单独披露触发「守卫性空转」的字体与分支数（本机中文 Windows 为 `0` 守卫触发）。
- **已知非目标继承**：不在此 Phase 中做图集多页扩容或 LRU 淘汰（转交 Phase 47 `#95`）；不扫 `HKCU` 用户私有字体目录；不接管 `DirectWrite`；不修改 `Win32FontSource::ResolveFileCandidates` 英文快路径既有行为。
- **`n1-only` 与 `"微软雅黑 UI"` 边界说明**：Windows 下 `msyh.ttc#1` 在 `0x0804` 与 `0x0409` 的 `nameID-1` 均为 `"Microsoft YaHei UI"`（无中文 `nameID-1` 记录），故在 `n1-only` 策略下请求 `"微软雅黑 UI"` 按预期 miss 并告警回退；而 `"微软雅黑 Light"`（`msyhl.ttc#0`）与 `"等线 Light"`（`Dengl.ttf#0`）在 `0x0804` 的 `nameID-1` 本身即为对应中文名，可经 `n1` 正常直达并与父族隔离。
- **性能与冷启动护栏**：普通英文/已有显式文件名查询不触发 `EnumerateInstalledFiles()`（`scanCount == 0`），冷启动增量 `0 ms`；首次中文族名回查触发单趟扫描实测约 `96~103 ms` 且进程生命周期内至多执行 1 次。

---

## §4 修订记录

| 版本 | 日期 | 变更说明 |
|---|---|---|
| `v1.0` | 2026-11-15 | 初版定稿：落地 Batch 0/1/2 拆分、9 文件详设规约、4 处 `CB-1/CB-2` 改造点与 `+7 -> 622` 用例映射。 |
| `v1.1` | 2026-11-15 | 吸收第 4 轮评审 `P1×3 + P2×8`：① `CB-2` 按签字文本重写为 `FaceIdFor("微软雅黑") == FaceIdFor("Microsoft YaHei") != 默认`；② 补齐 `EnsureNameId1All` 触发时机、`usedLocalizedIndex` 族名过滤判据、精确 UTF-8 大小写策略与单趟双池回落；③ 对齐 `T46-4: +0` 与 `616/619/622` 用例映射，补全 `T46-1/2/3/5/7/8` 全数签字断言；④ 对调校正 §0.2 `D46-2`/`D46-3`，统一 `WideToAnsi` 与 `ForEachFontRegistryKey` 签名，移除 `ResolveFileCandidates` 范围外修改，钉死 `GLGlyphAtlas::Allocate` 单点计数与 `facesById` 1-based `slot 0` 约定，清理两处措辞并补终闸 Clang 双链。 |
| `v1.2` | 2026-11-15 | 吸收第 5 轮评审 `P1-A + P2-A/B`：① 钉死非中文 CI 文件缺失守卫方案 ⓐ（文件存在性分段守卫 + `Logger::Warning` 留痕 + 提前 `return`，零框架改动，实跑汇总披露「守卫性空转」）；② 详设与需求（`v1.5`）同步补列 `Win32FontSource.h` 第 10 文件（公共头 `102` 不变）；③ `T46-6` 补签同款文件存在性守卫；④ 补充 `Batch 1 interim` `candidates.empty()` 暂保持既有回落行为说明。 |
