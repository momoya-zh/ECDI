# Phase 46 初步设计稿 · GL 字体能力对齐（合成粗体 / 合成斜体 + 中文字体族名解析，#94）

> **状态**：**v1.2（2026-11-15，吸收 Qwen4 二轮评审 `P1-A ⓑ` + `P2×5` 且用户已补签 `CB-2`；上游 = [phase46-gl-font-parity-requirements.md](phase46-gl-font-parity-requirements.md) v1.4 全数闸门已签字拍板，放行进详设）**
> **源码基线**：HEAD `36bb02d`（`tests=615` · Public 头 `102` · 设计文档 `221 → 222` · 递归 `docs/dev/ 220 → 221`）
> **继承红线**：需求稿 **v1.4** §0 三条全文有效——① 既有英文 / 常规 / 真粗真斜体路径零回归（含全数已签 `D46-2` `CB-1` 与 `D46-4` `CB-2` 两项显式契约破坏豁免登记）；② `FreeTypeTextMeasurer` 与 `GLRenderer` 无条件测画同源、`ApplySynthesis` 单点出口、`xstr >= 64` 整像素加粗下界；③ 零新第三方依赖、`third_party/` 与 `CMakeLists.txt` 零改动。

---

## §1 事实探针（`O46-1`～`O46-3`）实测归档与设计定标

本节读数均由本机（Windows, `DPI=96`, HEAD `36bb02d` + FreeType 2.14.3）实跑探针 `.workbuddy/spike/p46/o46_probe.cpp`、`o46_supp.cpp` 与 `o46_3_bench.cpp` 实测并归档至 `.workbuddy/spike/p46/o46_readout.txt`，作为初设与详设唯一参数依据。

### 1.1 `O46-1`：GDI 加粗阈值、五档双串比率对齐与 Hinting 对照

1. **精确触发阈值（`20px` 全梯扫描）**：
   - `SimSun`、`SimHei`、`Microsoft YaHei`、`Consolas` 在 **`w <= 550`** 全走常规宽；**`w >= 551`**（实测 `551 / 560 / 575 / 600`）全部触发 GDI 合成加粗（`SimSun` 纯 CJK `120→126 (+6)`、混合串 `170→183 (+13)`、单字 `'测'` `20→21 (+1)`）。
   - `Microsoft YaHei` 与 `Consolas` 在 `w = 700, 900` 切换至真 Bold 面（`msyhbd.ttc`、`consolab.ttf`）。
   - **选定合成粗体判据**：**`synthBold = (ClampWeight(requestedWeight) >= 551) && (chosenRawWeight < 551)`**——对 `SimSun`/`SimHei` 等无 Bold 面族在 `>= 551` 时精确激活合成（对齐 GDI `w >= 551` 边界）；对已有 `>= 551` 真档位的 `Segoe UI`/`Consolas`/`Microsoft YaHei`，仍沿用 Phase 36 `PickNearestTier`（`FontWeightTests.cpp:98` 钉死 `560→700`：`YaHei @ 560/600` GL 选 `700` 真粗体面不二次合成，而 GDI 在 `551..600` 选 `400`+合成得 `175 vs 166`，该 `551..699` 既有档位选择差异不在 Phase 46 改写）。

2. **五档（`12 / 16 / 20 / 24 / 32px`）`ceil(65536 / x_ppem)`、`ydelta = 0` 双串实测**：
   - 以 `xdelta = (65536 + ppem - 1) / ppem`（`ppem = face->size->metrics.x_ppem`）、`ydelta = 0` 驱动 `FT_GlyphSlot_AdjustWeight`：

| 字体与类型 | 串型 | 12px | 16px | 20px | 24px | 32px | 最大 `abs(FT_ratio - GDI_ratio)` |
|---|---|---|---|---|---|---|---|
| `SimSun`（合成） | 纯 CJK 6 字 | `72→78 (+6)` | `96→102 (+6)` | `120→126 (+6)` | `144→150 (+6)` | `192→198 (+6)` | **`0.0000`**（五档与 GDI 逐像素一致） |
| `SimSun`（合成） | 混合 13 字 | `102→115 (+13)` | `136→149 (+13)` | `170→183 (+13)` | `204→217 (+13)` | `272→285 (+13)` | **`0.0000`**（五档与 GDI 逐像素一致） |
| `SimHei`（合成） | 纯 CJK / 混合 | `+6 / +13` | `+6 / +13` | `+6 / +13` | `+6 / +13` | `+6 / +13` | **`0.0000`** |
| `Microsoft YaHei`（真粗体） | 纯 CJK / 混合 | `+0 / +2` | `+0 / +6` | `+0 / +4` | `+0 / +6` | `+0 / +9` | **`0.0000`** |
| `Consolas`（真粗体，缺 CJK） | 纯 CJK / 混合 | CJK `FT 42→42` vs `GDI 84→90` | CJK `54→54` vs `108→114` | CJK `66→66` vs `132→138` | CJK `78→78` vs `156→162` | CJK `108→108` vs `216→222` | CJK `0.0278..0.0714` / 混合 `0.0131..0.0336`（**非合成问题**，系既有 `GDI 跨字体 CJK fallback` vs `GL .notdef` 分歧；`T46-1` 仅对完整覆盖测试串字族做比率校验，`Consolas` 改验真粗体面改前/后逐位零漂移） |

   - **结论**：GDI 合成加粗在 `12..32px` 全区间为**逐字恒定 `+1px`（无论 CJK 还是拉丁）**，无高字号阶跃；因此 **`kPx = 1`**（即 **`xdelta = (65536 + ppem - 1) / ppem`**、`ydelta = 0`）无需按字号分段表即可在涵盖串的字族五档实现 **`0.0000` 偏差**，稳妥落入 `<= 0.02` 门槛内。

3. **Hinting A/B 对照（`SimSun @ 12 / 14 / 16px`）**：
   - 保持 `FT_LOAD_TARGET_NORMAL`（先 hint 再 `AdjustWeight`）：`'测'` @12px `h=11, bearY=10, adv=13, sharp255=35`，与常规体 `h/bearY`（`11/10`）完全对齐；`'A'` @12px `h=8, bearY=8, sharp255=12, gray=28`。
   - 改用 `FT_LOAD_NO_HINTING`：`'A'` @12px 竖向与常规体脱齐（`h=8→9, bearY=8→9`）且锐度降半（`sharp255=12→5, gray=28→35`），`'测'` @14px 亦漂移至 `h=14, bearY=12`。
   - **拍板**：**维持 `FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP` 不变**（签字项 `D46-1 ⓐ`），在非目标记录合成态小字号微调约束。

### 1.2 `O46-2`：合成斜体倾角定标与「粗 + 斜」叠加顺序

1. **斜体倾角质心实测（`FT_GlyphSlot_Oblique`, `20px`）**：
   - 在**标定字形集**上测量上、下半部质心差 `dx = topCx - botCx`（排除 `'f'`/`'/'` 等天然非对称字形，该两字常规态 `dx` 已分别为 `+0.75px` 与 `+3.49px`）：
     - **`SimSun` / `Microsoft YaHei` 标定集 `{'测', '国', 'H', 'W'}`**：`REG` 均在 `[-0.14, +0.28] px`；`OBL @ 20px` 实测 `SimSun` `'测'=+1.99px`、`'国'=+1.74px`、`'H'=+1.47px`、`'W'=+1.28px`，`YaHei` `'测'=+1.73px`、`'国'=+1.95px`、`'H'=+1.34px`、`'W'=+1.49px`。
     - **纯拉丁 `Tahoma` 标定集 `{'H', 'W', 'A', 'I'}`**（`Tahoma` 缺 CJK 字形，`O46-2` 补跑实测）：`REG` 均在 `[-0.06, 0.00] px`；`OBL @ 20px` 实测 `'H'=+1.37px`、`'W'=+1.46px`、`'A'=+1.49px`、`'I'=+1.95px`。
   - **定标修正**：需求初版草案 `T46-2` 原估计 `>= 2.0px` 会误杀 `SimSun`（`'测'=1.99, 'W'=1.28px`）与 `Tahoma`（`1.37..1.95px`）；按二轮评审 **`P1-A ⓑ`** 统一将 **`T46-2` `20px` 完整标定集（`{'测', '国', 'H', 'W'}` 及 `Tahoma` `{'H', 'W', 'A', 'I'}`）斜切质心右偏门槛定标为 `>= 1.2px`**（`SimSun`/`YaHei` 实测 `1.28～1.99px`，最小裕量改善为 `0.08px`；同一标定集常规态 `abs(dx) <= 0.4px`）。
2. **水平左右探出定量**：`16 / 20px` 下实测斜切右探出 `oR = max(0, bitmap_left + width - advance)` 为 **`1～4 px`**（汉字 `1～3px`、`'W'/'f'/'/'` 最高 `4px`）；纯合成粗体 `o46_supp` 实测 `SimSun` `oR = 0px`、`YaHei 'W'` `oR = 1px`，且 `YaHei '测'` `bitmap_left = -1`（`@16/20px`），故**合成粗体左/右探出均 `<= 1px`**。`GLGlyphAtlas` 按增大后 `bitmap.width` 完整储存，斜切与粗体左/右探出同由控件 `PushClip` 边界裁剪。
3. **「粗 + 斜」叠加顺序对比（`AdjustWeight → Oblique` vs `Oblique → AdjustWeight`）**：
   - 两种顺序总灰度和差异 **~1%**（如 `SimSun '测' @20px`: `44072 vs 43562`，差 `1.16%`）；
   - **`AdjustWeight → Oblique`（先粗后斜）** 的外框更紧凑、`oR` 不额外外扩（`YaHei '国' @20px`: `w=22, oR=2` vs 先斜后粗 `w=24, oR=3`；`Consolas '测' @20px`: `w=14, oR=2` vs `w=15, oR=3`）。**选定顺序：先 `AdjustWeight(xdelta, 0)` 后 `Oblique`**。

### 1.3 `O46-3`：Win32 注册表全量冷扫耗时与 `nameID-1` 细节

1. **注册表特征**：本机 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts` 共 **499 条**（499 唯一存在文件、555 face、25,232 条 SFNT name 记录），其中 **1 条为绝对路径**（`C:\ProgramData\Kingsoft\office6\KingsoftSymbol\KingsoftSymbol.ttf`）。`Win32FontSource::EnumerateInstalledFiles()` 须区分绝对路径与相对 `Fonts` 目录文件名（注：`ForEachFontRegistryKey` 沿用只读 `HKLM`，Windows `HKCU` 每用户安装字体不入索引，记入已知边界）。
2. **SANITY UPPER BOUND 发现**：系统字体中最大 SFNT name 记录数达 **452**（`LinLibertine_R_G.ttf` 等多族），若沿用 `ReadNameId1En` 的 `256` 上限会截断尾部记录 ⇒ **本地化扫描上限提升至 `1024`**（覆盖全部已测字体）。
3. **耗时优化与实测归档（`o46_readout.txt`）**：
   - 初探 `-1` 双开法（`1054` 次打开）冷盘耗时 `1402.78 ms`（对照纯二进制 `90.07 ms`，`o46_readout.txt:147`）；
   - 改为**单趟直开 `face_index = 0` 续扫 `1..num_faces-1`** 后，独立复跑并归档至 `o46_readout.txt:215-217` 实测 **`FT_1pass = 102.97 ms`（Pass 1）/ `96.23 ms`（Pass 2）**（同 run 纯二进制 `63.79 / 55.94 ms`）。单趟冷扫 **~96～103 ms**（较双开提速 >13×），完美落地 **`D46-5 ⓒ`**（Zero `CMake` 改动 + `FontEngine` 内部 `FT_*` pimpl 隔离）。
4. **`nameID-1` 命中去重与 `nameID-16` 隔离说明**：
   - 注：`o46_probe.cpp` 初测时同时收集 `nameID-1 + nameID-16` 且未对多语言重复路径去重，因此归档打印 `微软雅黑 hits=3`（含 `msyhl.ttc#0` 的 `nameID-16`）与 `宋体 hits=2`（`simsun.ttc#0` 多语记录重复）；正式设计按 **仅采集 `platform_id == 3 && name_id == 1` 全语种（UTF-16BE → UTF-8）+ 对每个键下的 `(filePath, faceIndexHint)` 显式去重**：由此 `"微软雅黑"` 精准命中 `msyh.ttc#0`（`400`）与 `msyhbd.ttc#0`（`700`）、排除 `msyhl.ttc#0`（`nameID-1 = "微软雅黑 Light"` 归独立族名），完整保持 Phase 36 轻量独立族隔离契约，同时全命中 `"宋体"`/`"新宋体"`/`"黑体"`/`"楷体"`/`"仿宋"`/`"等线"`/`"等线 Light"`/`"微软雅黑 Light"`。

---

## §2 架构设计与跨模块变更方案

### 2.1 `FontEngine::Impl` 双层对象模型（物理句柄池 × 虚拟样式条目）

为守住 `FontEngine.cpp:9` 的 `FT_*` pimpl 隔离与「多个 `FaceId` 共享同一 `FT_Face` 不重复加载、不重复释放」双不变量，将现行单一 `faces` 表拆分为物理与虚拟两层：

```
  Physical key: (path, index)
        │
        ▼
  rawFaces : map<(string, int), RawFace>
    ├─ FT_Face face          // 唯一所有权；~FontEngine() 仅遍历 rawFaces 调 FT_Done_Face
    ├─ long numFaces
    ├─ string nameId1En             // 首中早退读得（英文快路径 0 额外开销，见 §2.2）
    ├─ vector<string> nameId1All    // 惰性按需读取（仅当 EN 过滤未命中需复核本地化族名时填充，上限 1024）
    ├─ bool nameId1AllLoaded = false
    ├─ uint16_t os2Weight
    └─ bool italic

  Virtual key: (path, index, synthBold, synthItalic)
        │
        ▼
  faces : map<tuple<string, int, bool, bool>, FaceId>
        │
        ▼  (单调递增、永不复用)
  facesById : map<FaceId, BoundFace>
    └─ BoundFace { FT_Face face; bool synthBold; bool synthItalic; }
```

- **句柄零双开与零双重释放**：`EnsureRawFace(path, index)` 按 `(path, index)` 唯一打开一次 `FT_Face`；`EnsureVirtualFace(path, index, synthBold, synthItalic)` 复用同一 `FT_Face` 分配独立 `FaceId` 并写入 `facesById`。`~FontEngine()` **仅遍历 `rawFaces` 调 `FT_Done_Face`**，结构性消除重复析构风险。
- **`FaceOf` 与 `BoundFaceOf`**：既有 `FaceOf(id)` 保持返回 `FT_Face`（兼容所有既有测量/行高/ascent 查询）；新增 `BoundFaceOf(id)` 返回 `const BoundFace*` 供 `AdvanceOf` 与 `GlyphByKey` 读取 `synthBold`、`synthItalic`。

### 2.2 单一出口合成管线（`ApplySynthesis`）与 `FaceIdFor` 选面规则

1. **单一出口 `ApplySynthesis(FT_GlyphSlot slot, bool synthBold, bool synthItalic)`**：
   - 依赖 `<ft2build.h>` + `#include FT_SYNTHESIS_H`（仅引入在 `FontEngine.cpp` 内部，遵守 pimpl）。
   - 执行顺序与参数（由 `O46-1`、`O46-2` 定标）：
     1. 若 `synthBold && slot->face && slot->face->size`：取 `const FT_Pos ppem = slot->face->size->metrics.x_ppem;`；若 `ppem > 0`，`FT_GlyphSlot_AdjustWeight(slot, (65536 + ppem - 1) / ppem, 0)`（强制 `xstr >= 64`、`ydelta = 0`）。
     2. 若 `synthItalic`：调用 `FT_GlyphSlot_Oblique(slot)`（零 advance 修改）。
   - **全仓仅两处调用点**：
     - **`Impl::AdvanceOf`**（`FontEngine.cpp:176-177`）：`FT_Load_Char(...) == 0` 之后立即调用 `ApplySynthesis(face->glyph, bf->synthBold, bf->synthItalic)`，随后写入 `advanceCache`（key 为带虚拟 `faceId` 的 `GlyphKey`）并输出 `advance.x >> 6`。
     - **`FontEngine::GlyphByKey`**（`FontEngine.cpp:740-745`）：`FT_Load_Glyph(...) == 0` 之后、**`FT_Render_Glyph` 之前**立即调用 `ApplySynthesis(face->glyph, bf->synthBold, bf->synthItalic)`，确保栅格位图与 `bitmap.advance = advance.x >> 6` 与测量链逐字同源。
   - 由于 `GlyphKey`、`advanceCache`、`glyphCache`、`lineHeightCache`、`ascentCache`、`familyCache` 均已由 `FaceId` 或 `(family, weight, italic)` 分档，`LineHeight` / `Ascent` 对 `(synthBold, synthItalic)` 自动复用物理 `face->size->metrics`（`ydelta = 0` 且 `Oblique` 不改 `ascender`/`height`），各下游接口与缓存键**零签名修改**。

2. **合成触发判定与 `FaceIdFor` 三段式流程**：
   - `NeedSynthBold(uint16_t rawWeight, uint16_t requestedWeight) = (ClampWeight(requestedWeight) >= 551) && (rawWeight < 551)`
   - `NeedSynthItalic(bool rawItalic, bool requestedItalic) = requestedItalic && !rawItalic`
   - `FaceIdFor(family, weight, italic)` 执行路径：
     1. **`familyCache` 命中**：直接返回缓存 `FaceId`（含失败回退缓存，防刷屏）。
     2. **空 `family`**：走 `fallbackToDefault(weight, italic)`——打开默认 `SimSun` (`face 0`)，按 `NeedSynthBold` / `NeedSynthItalic` 绑定对应虚拟 `FaceId`（`scanCount == 0`）。
     3. **快路径候选（`source->ResolveFileCandidates(family)`）**：沿 Phase 36/45 `PathGroup` 分组加载 `RawFace`，优先用零增量开销的 `raw.nameId1En == family` 匹配（`g.direct` 豁免过滤；**不读 `nameId1All`**，保持英文快路径零 SFNT 全扫描开销；仅当本地化族名经由 `EnsureLocalizedIndex` 复用候选筛选时才按需惰性填充 `raw.nameId1All`）。筛选斜体匹配面（`italic ? raw.italic : !raw.italic`）→ 若得候选池直接 `PickNearestTier` 并按 `NeedSynthBold` 绑定虚拟 `FaceId`（`scanCount == 0`）；若 `tiers.empty() && italic`，按相同逻辑在常规池（`!raw.italic`）选最接近档位并叠加 `NeedSynthItalic(false, true)` 与 `NeedSynthBold`。
     4. **英文快路径全灭 ⇒ 惰性中文索引路径（`EnsureLocalizedIndex`）**：
        - 若 `!impl.localizedIndexBuilt`：置 `localizedIndexBuilt = true`、`++localizedScanCount`，调用 `impl.source->EnumerateInstalledFiles()`，单趟对每个文件以 `FT_New_Face(library, path.c_str(), 0, &face0)` 打开并继扫 `1..numFaces-1`，读取所有 `platform_id == 3 && name_id == 1` 记录（上限 `1024`）按 `(filePath, faceIndex)` 去重后写入 `localizedIndex[utf8Name] -> vector<FontSourceCandidate>`，读完即 `FT_Done_Face`（索引建立期**不常驻 555 个 `FT_Face` 句柄**，内存占用接近零）。
        - 用 `localizedIndex` 命中的候选集执行与第 3 步完全统一的斜体过滤 / 常规池回落 + `PickNearestTier` + `NeedSynth*` 合成。
     5. **仍未命中**：写 Warning 日志并调用 `fallbackToDefault(weight, italic)`（结果写入 `familyCache`）。

### 2.3 `FontSource`、`Win32FontSource` 与 `GLGlyphAtlas` 改动设计

1. **`FontSource.h` / `Win32FontSource.cpp`**：
   - `FontSource` 新增带默认空实现的虚函数 `virtual std::vector<std::string> EnumerateInstalledFiles() const { return {}; }`，既有非 Win32 / 测试替身（如 `CountingFontSource`）**零改动编译**。
   - `Win32FontSource::EnumerateInstalledFiles()` 复用 `ForEachFontRegistryKey`，兼容相对文件名（拼 `FontsDirectoryWide()`）与绝对路径（如驱动器盘符/UNC，命中 `O46-3` 中的 `KingsoftSymbol.ttf`），过滤不存在文件与无法转换 `CP_ACP` 的路径后去重返回。
2. **`GLGlyphAtlas.h` / `.cpp`（`D46-0 ⓐ` 容量可观测守卫）**：
   - 不触碰 `Allocate` 的 shelf 分配几何、`kSize = 1024` 与 1px 间隙逻辑；
   - 新增成员 `std::size_t m_allocFailureCount = 0;`，在 `Allocate` 因图集满（`m_shelfY + h > kSize`）返回 `Empty()` 时递增 `++m_allocFailureCount`；新增只读观测缝 `AllocationFailureCount() const noexcept` 与 `IsFull() const noexcept`（返回 `m_fullWarned`，语义为「曾触达饱和阈值」，不排除大字形失败后极小字形仍可能填入既有 shelf 残隙——`T46-8` 以同大字形 `Allocate` 失败次数 + `IsFull()` 断言）。

### 2.4 显式契约破坏清单（两项登记，对应 4 处存量断言）

| # | 契约标识与现行测试位置（**全部实核**） | 原契约 | Phase 46 新契约与处置 |
|---|---|---|---|
| **CB-1** | `I45-italic-fallback`（共 **3 处**：`FontWeightTests.cpp:201`、`FontWeightTests.cpp:208`、`WidgetTests.cpp:2931`） | 无真斜体族 `italic=true` 回落常规 `FaceId`（`EXPECT_EQ(simsunIta, simsunReg)` / 缓存复查 `simsunReg` / `EXPECT_EQ(idCjkReg, idCjkItalic)`） | 按 `D46-2 ⓐ` 作废：改写 `FontWeightTests.cpp:201` 为 `EXPECT_NE(simsunIta, simsunReg)`、`:208` 为 `EXPECT_EQ(..., simsunIta)`、`WidgetTests.cpp:2931` 为 `EXPECT_NE(idCjkReg, idCjkItalic)` 并更新注释 |
| **CB-2** | `P0-② ZH 边界钉`（**1 处**：`FontWeightTests.cpp:131-132`，`Test36FaceIdPerFamilyAndWeight`） | 中文族名不解析：`EXPECT_EQ(engine->FaceIdFor("微软雅黑"), engine->FaceIdFor(std::string()))` | 收编 `#69`（需求 v1.4 `D46-4 ⓐ` **已由用户补签确认**）：更新该断言为 **`FaceIdFor("微软雅黑") == FaceIdFor("Microsoft YaHei")`**（且 `!= 默认 SimSun`），由 `T46-5` 全面覆盖常见中文字体族 |

---

## §3 改动预算、风险边界与验收映射

| 维度 | 规划 | 说明 |
|---|---|---|
| **改动源码清单** | `FontSource.h` · `Win32FontSource.cpp` · `FontEngine.h`（含 `:92` 过时注释修正） · `FontEngine.cpp` · `GLGlyphAtlas.h` · `GLGlyphAtlas.cpp` · `Font.h`（仅注释） · `FontWeightTests.cpp` · `WidgetTests.cpp`（仅 `:2931` `CB-1`） | `ECDI/include/` 零代码变更；`GLRenderer.cpp` 0 改动；`CMakeLists.txt` / `third_party/` 0 改动 |
| **公共头文件 / API** | `102 → 102`（+0）/ `+0 API` | 内部观测缝 `FontEngine::LocalizedScanCountForTests()` 与 `GLGlyphAtlas::AllocationFailureCount()` / `IsFull()` 均在内部头 |
| **测试增量与编排** | **净增 `+7`（`615 → 622`）** | 严格对齐需求稿口径：① **`T46-4`** 为原位更新 `CB-1`（3 处）+ `CB-2`（1 处）[**+0**]；② 新增 **7 条**注册用例：**`T46-1`**（涵盖串的 `SimSun`/`SimHei`/`YaHei` 五档双串比率 `<= 0.02` + `Consolas` 真粗体零漂移 + 测画同源）、**`T46-2`**（`O46-2` 完整标定集 `>= 1.2px` @20px + 零宽增 + 真斜体不二次斜切）、**`T46-3`**（四态 `FaceId` 隔离 + 共享句柄安全释放 + 六组缓存防投毒 + `B->O` 叠加）、**`T46-5`**（`"宋体"`/`"新宋体"`/`"微软雅黑"`/`"黑体"`/`"楷体"`/`"仿宋"`/`"等线"` 逐位等价英文名）、**`T46-6`**（中文名+粗斜与未知回落告警）、**`T46-7`**（英文/直查/默认 `LocalizedScanCountForTests() == 0`）、**`T46-8`**（`GLGlyphAtlas` 饱和 `AllocationFailureCount()` / `IsFull()` 守卫） |
| **已知架构边界** | ① 图集 4× 槽位在 1024² 饱和后跳过缺字形绘制，留待 Phase 47（`#95`）扩容淘汰；② `FT_LOAD_TARGET_NORMAL` 下小字号微调不保证与 GDI 点阵对齐等价；③ 左/右探出越界（斜切右探出 `1～4px` + 粗体左/右探出 `<= 1px`）由控件 `PushClip` 边界裁剪；④ `Consolas` 等缺 CJK 字体在 `GDI CJK fallback vs GL .notdef` 的既有分歧、`Microsoft YaHei @ 551..699` 档 GL `PickNearestTier(700)` vs GDI 合成（`175 vs 166`）既有分歧、Windows `HKCU` 每用户字体（仅扫 `HKLM`）均维持现状 | 仅作范围限定 |

---

## §4 修订记录

- **v1.0（2026-11-15）**：结合用户签字拍板（`D46-0~4: ⓐ`, `D46-5: ⓒ`）与 `O46-1`～`O46-3` 实测读数完成初设定稿。
- **v1.1（2026-11-15）**：吸收 Qwen4 初设评审（`P1-1..3` + `P2×10`）：`CB-1` 补齐 `FontWeightTests.cpp:208` 与 `WidgetTests.cpp:2931` 并将 `WidgetTests.cpp`/`FontEngine.h:92` 纳入改动清单；`CB-2` 同步上游需求 **v1.3** 待补签状态；`T46-1` 限定涵盖串字族并补充 `Consolas`/斜体标定集/`B->O` 精度/`(path,faceIdx)` 去重/惰性 `nameId1All`/`551..699`/`HKCU`/递归 `docs/dev/` 计数口径等全数细节。
- **v1.2（2026-11-15）**：吸收 Qwen4 二轮评审（`P1-A ⓑ` + `P2×5`）并确认用户补签 `CB-2`：`T46-2` 门槛统一至 `>= 1.2px` 对齐全集 `{'测', '国', 'H', 'W'}` + `Tahoma` `{'H', 'W', 'A', 'I'}`；冷扫只引 `o46_readout.txt:215-217` 实档值（96～103 ms）；补记粗体左探出 `<= 1px` 边界，**全数闸门闭环放行进详设**。
