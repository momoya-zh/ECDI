# Phase 24 · DrawLine 线段抗锯齿（line coverage AA）—— 初步设计（v1.1）

> 来源：需求确认稿 `phase24-line-antialiasing-requirements.md` **v1.1 ✅ 已通过**（2026-10-01 外部评审「通过，可以进入 Preliminary Design」）
> 状态：**v1.1**（2026-10-01）——✅ **评审通过**（外部评审结论：「**Phase 24 Preliminary Design v1.0：基本通过，可进入 Detailed Design**」；★ 附 **3 处进入详设前的修正**——**① 必修**：T24-2 面积容差不得由单像素误差直接推出 · **② 强烈建议**：T24-8 拆「几何层 / 端到端」两层 · **③ 建议**：`PatchSurface` 矩形化须有直接回归覆盖——**本版已全部吸收**（处置见 §1.1）；★ 另有 D3 措辞、坐标域契约、`#48` 定性三处澄清一并落盘）
> 评审给定的优先级链 = **Q2 → D3 → D0 → D2 → Q3 → Q4**，且 **Q2 与 D3 耦合决定**——本稿按该链**先探针取证、后定案**（§2 探针 ⇒ §3 定案），并形成评审要求的完整链：**线段几何模型 → 端帽模型 → 像素 coverage → 接缝策略 → 预乘合成 → PatchSurface → GDI AlphaBlend**（§2 → §4 → §5）。

---

## 1. 需求 → 初设映射（本稿要闭合什么）

| 需求条目 | 本稿落点 |
|---|---|
| **R1** 斜线 AA、签名不变 | §5 `DrawLine` 内部分支（公共签名零改动） |
| **R2** 复用两层架构 | §5.1 `LineCoverage`（生成，落 `src/Render/` 同目录）+ §5.2 `CoverageRaster::RasterizeMask`（合成）+ 既有 `PatchSurface`/`AlphaBlend`（装配） |
| **R3** AA 开关管辖 | §5.3 分支序：`!m_antiAliasing` ⇒ legacy 逐位现状 |
| **R4** 零回归护栏 | §3-D1 显式分流 + §5.3 分支谓词 + §7 T24-8 |
| **R5** 接缝连续 | §4（本稿最重的一节——圆帽并集 + 残差量化冻结） |
| **R6** 资源纪律 | §5.4 `PatchSurface` 矩形化复用（不新建每帧 DIB） |
| **R7** width 输入契约 | §3-D4 定案：覆盖度层只消费物理像素 + **宽度整数化同 legacy 口径**（`max(1, lround)`） |
| **D0–D4 / Q1–Q4** | §2 探针 → §3 全部定案 |

### 1.1 ★ 外部评审处置（第一轮，2026-10-01）

> 评审结论：**「Phase 24 Preliminary Design v1.0：基本通过，可进入 Detailed Design」**；★ 评审同时认可：探针先行的方法论、S=8 / 圆帽 / 显式分流 / 直算不缓存 / `DrawPolyline` 延后 / 三批实现等全部主要定案，以及「B-4 → D1」是全稿最关键的取舍（不为统一数学模型牺牲轴对齐像素兼容性）。

| 评审节 | 级别 | 内容 | 处置 |
|---|---|---|---|
| §四（D3 措辞） | 建议 | 「GDI w≥2 几何笔的端帽 = 圆帽」是对 GDI 内部实现下定义——应改为「**当前 GDI 输出表现出圆帽端点特征**」（兼容对象是视觉行为，不是内部 primitive） | ✅ **采纳** → §2.1 **A-4** 与 §3 **D3** 措辞已改 |
| §六（lround 位置） | ★ **详设输入** | **取整坐标域必须画死**：`RenderCommand`（DIP）→ Renderer DPI 折算 → **物理 float** → `DrawLine` → `IsAxisAlignedAfterRound`（`lround` 发生在后端、作用于**物理 float**；绝不在 DIP 域取整） | ✅ **采纳** → 新增 **§5.5 交给详设的输入契约**（含坐标域链条图） |
| §九（PatchSurface 回归） | 建议 | 矩形化改的是 **Phase 8.6 稳定共享设施** ⇒ 必须有**直接回归覆盖**：Corner 路径矩形化后输出不变 + W≠H stride 正确（现 T24-7 只覆盖 `RasterizeMask`，盖不住 PatchSurface 本身） | ✅ **采纳** → 新增 **T24-11**（大→小半径交叉序列 = 历史 stride 缺陷触发形态 + W≠H 斜线无行错位 + 角路径输出对照）；★ 顺带兑现技术债表「跨半径角补丁测试缺口」一项 |
| §十（T24-8 拆层） | ★★ **必修** | T24-8 在**生成层**测 over-blend，证明不了端到端（真实 `PatchSurface` + premultiplied + `AlphaBlend` 不是同一条路径）⇒ 拆两层：**T24-8a 几何层**（并集 vs 求和）+ **T24-8b 端到端**（真实 `DrawLine` ×2 → 读回 bitmap 断言 C-SEAM——复用既有 L2 真窗口像素读回装置，`AntiAliasingTests.cpp:266-342` 先例） | ✅ **采纳** → **T24-8 拆分为 8a / 8b**（§7） |
| §十一（T24-2 容差） | ★★ **必修** | 「B-1 单像素误差 ≤1/255」**推不出**「整体面积误差 ≤1%」（单像素误差与全几何面积误差不是同一统计量）⇒ 容差来源重定义：**对固定测试几何先建高采样参照（S=128），实测 S=8 的面积误差后冻结阈值** | ✅ **采纳** → **T24-2** 容差定义已改（§7）；★ 探针程序补一组 S=128 参照数据随详设执行 |
| §十四（#48 定性） | 建议 | `#48` 须明确写成「**当前阶段已知的非零残差，不是 Phase 24 缺陷**」——防止日后把 C-SEAM 的 28/255 误读为本阶段 bug | ✅ **采纳** → §4 与 **L1** 已补定性 |
| §十二（bbox 原点） | ★ **详设输入** | `GenerateLineCoverage` 的 **grid (0,0) 对应哪个物理像素**、bbox 计算、采样点坐标三者必须详设画出（最易「整体偏半像素」处） | ✅ **采纳** → 收入 **§5.5**（与坐标域契约同节交付详设） |
| §一~三 · §五 · §七~八 · §十三 · §十五 | — | 架构链 · S=8 · 圆帽 · 显式分流 · D0 · D2 · 公共 API +0 · 三批顺序 · 双层防线（退化） | ✅ **采纳（未改动）** |

---

## 2. 探针取证（P-A / P-B + Q1 / Q4 静态盘点）

> 程序落 `.workbuddy/spike/lineaa/`（gitignore 覆盖，不进仓库；沿 Phase 16 spike 惯例）：`probe_a_gdi_line.cpp`（GDI 像素真相，g++ -lgdi32）、`probe_b_coverage.cpp`（覆盖度数学原型，纯 std）。以下事实全部为 2026-10-01 本机实测输出。

### 2.1 P-A：GDI `DrawLine` 像素真相（10 个用例的 ASCII 像素网格）

| # | 事实 | 证据（探针输出） |
|---|---|---|
| **A-1** | **w=1 水平线精确吸附单行**：`(10,10)-(25,10)` 只画 **row 10**；垂直同理单列 | Case 1/4 网格 |
| **A-2** | **w=1 为半开区间 `[start, end)`**：`(10,3)-(20,3)` 画 cols 10..19（**终点 col 20 不画**） | Case 5 网格 |
| **A-3** | **w≥2 的足迹 quirky、不可用简单带模型重现**：w=2 画 rows 9-10 且 **row 9 两端各内缩 1px**（row 10 全长）；w=3 对称 ±1 | Case 2/3 网格 |
| **A-4** | **GDI w≥2 输出表现出圆帽端点特征**（w=2/3 斜线两端可见 taper——按**可观察视觉行为**兼容，**不据此断言 GDI 内部 primitive 形态**；v1.1 措辞订正） | Case 3/8 网格 |
| **A-5** | 斜线 w=1 = 单像素楼梯（**无 AA**），与 8.6 前的圆角同病 | Case 6 网格 |
| **A-6** | ★★ **现状勾的接缝本来就有 1px 缺口**：CheckBox 勾（s=14, bw=1）在肘点内侧 **(6,10) 为空像素**（两段楼梯从公共端点即刻分开） | Case 7 网格 |
| **A-7** | **退化线段不画任何像素**（`(4,4)-(4,4)` w=1 → 0 像素） | Case 9 网格 |
| **A-8** | 分数坐标 `y=10.4` → `lround` = 10 后按 row 10 画（**后端取整先于绘制**；AA 路径收到的是取整前的 float） | Case 10 网格 |

### 2.2 P-B：覆盖度数学原型（超采样 vs 解析 / 接缝量化 / 耗时）

| # | 事实 | 证据（探针输出） |
|---|---|---|
| **B-1** | ★★ **超采样 S=4 即收敛到量化步长之下**：对角像素解析参照 0.43984，S=4 误差 0.60/255 · S=8 误差 0.60/255 · S=16 误差 0.40/255；**S=2 误差 48/255 不可用** | 收敛表 |
| **B-2** | ★★ **接缝残差（圆帽）**：勾两段 stadium 带 over-blend vs 真并集——**max 27.0/255，共 3 个像素 >1/255**（全部在肘点邻域）；并集几何**填补现状缺口**（肘点 (6,10)：GDI 现状 0 → 圆帽并集 **0.875**） | B1 三网格 + 偏差统计 |
| **B-3** | butt 帽对照：残差略小（15.2/255 / 3 像素）**但缺口残留**（(6,10) 仅 0.5）⇒ butt 被 R5「无缺口」否决 | B1b 网格 + 统计 |
| **B-4** | ★★ **居中带在整数轴线上 = 0.5/0.5 分裂**（w=1 线 y=10 → rows 9/10 各 0.5），而 GDI 真相 = row 10 单行 ⇒ **统一覆盖度路径无法重现轴线现状** | B2 输出 |
| **B-5** | **耗时**（本机 MinGW -O2）：S=8 每像素 **315ns** ⇒ 短段（~22 触及像素）**≈ 7µs**；典型帧（10 段）**≈ 0.07ms**；S=16 每像素 1165ns（3.7×，无收益） | B4 计时 |

### 2.3 Q1 全库盘点：`a < 1` 的 `DrawLine` 调用方 = **0**

| 调用方 | 颜色来源 | alpha |
|---|---|---|
| `CheckBox.cpp:99,101` | `m_style.checkmark.value` → `DefaultTheme.cpp:64` = `Color::Black()` | **255** |
| `CaptionButton.cpp:141-181` | `kGlyph`(220,222,226,**255**) / `kGlyphOnRed`(255,255,255,**255**)（`CaptionButton.cpp:22-23`） | **255** |
| （半透明色 `kHoverBg` a=26 / `kPressedBg` a=45 走 **`DrawRect`**，不经 DrawLine） | `CaptionButton.cpp:24-25` | — |
| 测试（`RendererTests.cpp:75,134,375`） | `Color::Blue()` / `Black()` | **255** |
| examples（ModelProbe / MinimalApp） | **零 `DrawLine` 调用**（grep 实证） | — |

### 2.4 Q4 每帧调用量（静态盘点）

CaptionBar 每帧 = min(1) + max/restore(4 或 6) + close(2) = **7~9 段**；每个选中 CheckBox = **2 段**；典型场景 **≤ ~20 段/帧**，段长 ≤ ~20px。⇒ 与 B-5 的 0.07ms/10 段相乘，**量级判定：直算完全可行**（详见 §3-D2）。

---

## 3. 决策定案（评审优先级链执行结果）

| # | 定案 | 证据 | 否决项及理由 |
|---|---|---|---|
| **Q2** ★ | **超采样 S=8**（默认值；参数化契约同 `GenerateCornerMask`：`samples <= 0` → 空网格，奇数合法） | B-1（S=8 误差 ≤0.6/255，已在 1/255 量化步长之下）· B-5（0.07ms/帧）· **与 `CornerMaskCache` 默认 S=8 同源**（单一精度口径） | **解析法否决**：收益 < 量化步长，却新增一套独立数学面（多边形裁剪 + 面积公式）——8.6 选超采样的「对任何形状同一套代码」理由对线段依旧成立 |
| **D3** ★ | **圆帽（stadium：点到线段距离 ≤ w/2）**；**A1 的理论面积参照几何 = stadium 解析面积**（矩形 `L×w` + 圆盘 `π w²/4`） | A-4（GDI w≥2 输出**表现出**圆帽端点特征——视觉语言兼容，v1.1 措辞订正）· A-6 + B-2（圆帽并集**填补现状缺口**）· B-3（butt 残留缺口） | **butt 帽否决**（R5「无缺口」直接违背）· **方帽否决**（无 GDI 现状先例，肘点更尖） |
| **D1** | **显式分流**：`lround` 后共线（`lround(sy)==lround(ey) ‖ lround(sx)==lround(ex)`）⇒ 走 legacy（a≥1 逐位现状）；其余走覆盖度路径。谓词提为 `LineCoverage.h` 内**纯函数**（`IsAxisAlignedAfterRound`，`ResolveTarget` 先例） | B-4（居中带轴线 0.5/0.5 ≠ GDI 单行）· A-3（w≥2 足迹 quirky，逆向建模脆弱） | **统一路径否决**：要重现 A-2/A-3 的 cosmetic/几何笔差异 = 逆向 GDI pen 内部规则，脆弱且无收益 |
| **D0** | **补齐（是）**：`a<1` 一律走覆盖度路径（预乘公式一般化，见 §5.2 契约 C-2） | §2.3 盘点 = **0 个 `a<1` 调用方** ⇒ 纯增益、无可观测现状行为可变（评审 §6 的拍板前置已满足） | — |
| **D2** | **直算不缓存**（每段现算现用） | B-5 + §2.4（≤0.15ms/帧，无缓存必要）· 线段键空间无限（自由度 = 4 坐标 × 宽度）⇒ 缓存无界增长风险实、收益虚 | **照抄 `CornerMaskCache` 否决**：该缓存的价值前提是「键空间小」（半径一个维度）——线段不满足（需求 D2 的警惕被探针坐实） |
| **D4** | **宽度整数化同 legacy 口径**：覆盖度路径取 `effW = max(1, lround(width))`（与 `GDIBackend.cpp:407` 同式）；**输入坐标不取整**（float 直入——AA 的意义所在） | R7 + 需求 D4 冻结意图 | 亚像素宽度仍记账（**N3 / #46** 相邻，本阶段不动） |
| **Q3** | 几何层 + 合成层**全无头可测**（纯函数，零 GDI）；端到端（AlphaBlend 上屏）= **人工目视**（8.6 A 系列 + Phase 22 A3 先例） | K1/K10 · `CornerCoverageMask.h:22` 同款声明 | — |
| **Q4** | 已答（§2.4）——量级结论 = 直算可行 | 同上 | — |

---

## 4. 接缝策略（R5 的初设处置——本稿最重的一节）

1. **「无缺口」= 结构性满足**：圆帽带的并集几何天然填补肘点（B-2：现状 GDI 缺口像素 0 → 圆帽并集 0.875）。这不是巧合——**现状缺口恰是「两段各自硬边楼梯」的产物**，覆盖度化本身即消除之。
2. **「无重叠加深」→ 量化冻结**：两段独立 blend 在肘点邻域存在 over-blend 残差（**实测 max 27.0/255、3 像素**）。本稿将 R5 的「不得重叠加深」精确化为可执行判据：
   > **C-SEAM**：任何共端点同类线段对接处，over-blend 与真并集的逐像素偏差 **≤ 28/255 且受影响像素 ≤ 4**（探针实测峰值 + 1/255 余量）。T24-9 以生成层用例固化该阈值。
   ★ 相对现状（GDI 整像素缺口）是**净改善**；★ 该精确化属对需求 R5 的判据细化（非放松方向相反的改写），**随本稿评审一并确认**。
3. **根治方案记账**：真正零残差需要把折线**作为一条路径求并集、一次 blend**——即新增 `DrawPolyline` 原语。它已有 **2 个真实消费者**（勾 + 关闭 X，均过「第二消费者」门槛），但属**新公共原语**（违背需求 R1 的冻结，扩大 RenderCommand variant / Renderer / 双后端触面）⇒ **本阶段不做**，登记为 follow-up 记账建议（**#48**，须用户拍板后立项）。★★ **定性（评审 §十四）**：C-SEAM 阈值内的接缝残差是**当前阶段已知的非零残差，不是 Phase 24 缺陷**——防日后误读为本阶段 bug。

### 4.1 交给详设的输入契约（评审 §六 / §十二，v1.1 新增）

详设必须把以下两组定义**画死**（实现期最易错的两处）：

1. **取整坐标域**——`lround` 发生在哪条边上：

```text
RenderCommand 坐标（DIP 域，float）
        ↓  Renderer DPI 折算（Phase 20.1：ScalePoint / ScaleLength）
物理像素 float          ← ★ 取整前的值，AA 路径的输入（R7）
        ↓  DrawLine 收到
IsAxisAlignedAfterRound（lround 作用于物理 float——后端内部）
        ↓ true                    ↓ false
   legacy（内部照旧取整）      覆盖度路径（float 直入，不取整）
```

★ **绝不在 DIP 域取整**（否则 DPI > 1 时两个不同 DIP 坐标会塌缩，且与 Phase 20 的「换算只在平台边界」契约冲突）。

2. **覆盖度网格的坐标三元组**——`GenerateLineCoverage` 的 bbox 计算、grid 原点、采样点坐标必须一一对应：

```text
band 几何范围（stadium 外接框，含两端圆帽）
   bboxX = floor(min(x0,x1) − effW/2) … bboxX + grid.width − 1（对 y 同理）
grid(i, j) 对应物理像素方块 [bboxX+i, bboxX+i+1) × [bboxY+j, bboxY+j+1)
采样点 = 该方块内 (i + (sx+0.5)/S, j + (sy+0.5)/S)
BlendPatch(target, bboxX, bboxY, grid.width, grid.height)  ← 原点必须同源
```

★ 三者共享同一个 `bboxX/bboxY`（**单点出参**，不各自计算）——「整体偏半像素」的根因就是三处独立取整。

---

## 5. 接口草案（全部内部层；公共 API +0）

### 5.1 覆盖度生成——新文件 `src/Render/LineCoverage.{h,cpp}`

```cpp
/// 线段覆盖度网格（行主序，stride = width——与 CornerCoverageMask 同形）
struct LineCoverageGrid {
    int width = 0;   ///< <= 0 表示空网格
    int height = 0;
    std::vector<std::uint8_t> coverage;   ///< width × height
    bool Empty() const noexcept;
};

/// 生成线段覆盖度（**stadium 带模型：点到线段距离 <= effW/2，圆帽**）
/// @param x0..y1 物理像素坐标（**float 直入，不取整**——R7 / D4）
/// @param width  线宽；内部 effW = max(1, lround(width))（D4：同 legacy 口径）
/// @param samples 超采样倍率（默认 8；<= 0 → 空网格——同 GenerateCornerMask 契约）
LineCoverageGrid GenerateLineCoverage(float x0, float y0, float x1, float y1,
                                      float width, int samples = 8);

/// 分流谓词（纯函数，供 T24-8 无头测试——ResolveTarget 先例）
/// lround 后共线即 true（含退化：round 后重合 ⇒ legacy 画零像素，A-7）
bool IsAxisAlignedAfterRound(float x0, float y0, float x1, float y1);
```

实现要点：遍历带触及像素（**沿带步进 + 邻域测试**，非 bbox 全扫——成本模型见 B-5）；每像素 S×S 采样点做 stadium 内外判定。纯几何零 GDI。

### 5.2 像素合成——`CoverageRaster` 追加一个函数

```cpp
/// 覆盖度网格 → 预乘 BGRA（**无角变换**的一般化版本；Corner 版保留不动）
/// 契约 C-2（一般化预乘）：A = round(cov × a × 255)；RGB = round(colorByte × cov × a)；
/// 不变量 RGB ≤ A 恒成立（沿既有注释口径）；a == 1 时退化为 RasterizeCornerPatch 同族公式
void RasterizeMask(std::uint8_t* dest, int stride,
                   const LineCoverageGrid& grid, const Color& color);
```

### 5.3 `GDIBackend::DrawLine` 分支（入口序即语义序）

```
DrawLine(start, end, width, color):
  1. if (!m_antiAliasing)                  → legacy（逐位现状，含 a<1 丢 alpha 现状）   // R3
  2. if (IsAxisAlignedAfterRound(...))     → legacy（a≥1 逐位现状，R4；A-2/A-3 的quirky足迹原样保留）  // D1
  3. else                                  → 覆盖度路径：
       grid = GenerateLineCoverage(start, end, width, S);   // S 取 m_cornerMaskCache.GetSamples()——O1：单一 AA 精度旋钮
       if (grid.Empty()) return;                            // 防御（A-7：退化本不会到达此处）
       m_patchSurface.Ensure(grid.width, grid.height);      // §5.4
       RasterizeMask(patchBits, patchStride, grid, color);
       BlendPatch(target, bboxX, bboxY, grid.width, grid.height);
```

### 5.4 `PatchSurface` 矩形化（R6 落点）

现状 `Ensure(requiredSize)` 只支持**正方形**（size×size）——线段 bbox 是长方形，500px 斜线会强迫 500×500（1MB）且**只增不减**。改动：`Ensure(w, h)` 按**宽、高两维各自只增不减**；`BlendPatch(target, x, y, w, h)`；角路径调用处传 `(R, R)` **行为逐位不变**。行宽改用 `width * 4`（原 `size * 4` 的 stride 陷阱注释同步迁移）。★ 本改动触及圆角路径的**共享设施** ⇒ 批三零回归验证（既有 AA 用例全绿为硬判据）。

---

## 6. 影响面

| 类别 | 明细 |
|---|---|
| **新增** | `src/Render/LineCoverage.h` + `LineCoverage.cpp`（估 ~150 行）；`src/Tests/LineCoverageTests.cpp`（估 ~300 行） |
| **改动** | `src/Render/CoverageRaster.{h,cpp}`（+`RasterizeMask` ~30 行）· `src/Render/GDIBackend.{h,cpp}`（DrawLine 分支 ~50 行 + PatchSurface 矩形化 ~20 行机械改动）· `src/Tests/RunAllTests.cpp`（注册 +1 行） |
| **零改动** | `RenderCommand.h` / `PaintContext` / `Renderer` / `RecordingBackend`（签名不变——RecordingBackend 依旧原样记录）· 全部公共头（**92 → 92 / API +0**）· **CMake 0 改动**（`GLOB_RECURSE CONFIGURE_DEPENDS` 自动收编新 .cpp，`CMakeLists.txt:39,107` 实证）· Widget / examples |
| **规模口径** | 用例 **286 → 286+12 ≈ 298**（v1.0 估 296 ⇒ v1.1 修订购 12：T24-8 拆 8a/8b、新增 T24-11——评审 ②③）· 新 .cpp × 2 自动进构建 |

---

## 7. 测试大纲（T24-1..T24-10，全部无头除注明外）

| # | 用例 | 对应验收 |
|---|---|---|
| **T24-1** | 带模型语义：斜线覆盖度横截**单调**、带内趋满、带外为零；**锚点**（端点圆帽区/带边）逐点核对 | A1 |
| **T24-2** | **面积守恒**（A1 补注的参照几何）：总覆盖度和 ≈ `L×w + π(w/2)²`。★ **容差定义（v1.1 依评审 §11 重写）**：**不得**由「单像素误差 ≤1/255」（B-1）直接推出面积容差——两者不是同一统计量。定法 = **对固定测试几何先以 S=128 生成参照**，实测 S=8 的面积误差后**冻结阈值**（探针程序补 S=128 参照组随详设执行） | A1 |
| **T24-3** | 确定性：同输入两次生成逐位相同（沿 `TestMaskDeterminism` 形态） | A1 |
| **T24-4** | `samples` 契约：`<= 0` → Empty；奇数合法（沿 `TestMaskInvalidArgs` / `TestCacheSetSamplesClears` 形态） | A1 |
| **T24-5** | 退化输入 → Empty（`GenerateLineCoverage` 直接防御，不依赖分流） | A1 |
| **T24-6** | 分流谓词：`IsAxisAlignedAfterRound` 对整数轴线/退化/分数轴线 → true；斜线 → false | A6 |
| **T24-7** | `RasterizeMask` 预乘不变量：逐像素 `RGB ≤ A`；`a=1` 输出与 `RasterizeCornerPatch` 同公式（对照断言） | A4 |
| **T24-8a** | **接缝残差·几何层**（v1.1 拆分，评审 §十）：勾两段的并集覆盖度 vs 两带求和（over-blend 数学模拟）——偏差 ≤ 28/255 且 ≤ 4 像素 | A3-② |
| **T24-8b** | ★★ **接缝残差·端到端**（v1.1 新增）：**真实路径**连画两段（`GDIBackend::DrawLine` ×2 → `PatchSurface` + premultiplied + `AlphaBlend`）→ 窗口像素读回，对勾肘点邻域断言 C-SEAM（★ 复用既有 L2 装置：每次独立 backend + `BeginFrame`/`EndFrame` + `GetDC` 读回，`AntiAliasingTests.cpp:266-342` 先例）——生成层模拟（8a）证明不了端到端 | A3-② |
| **T24-9** | 宽度整数化：`width = 0/0.4/2.5` → effW = 1/1/3（D4 口径；防「覆盖度路径悄悄引入亚像素宽度」） | A4 |
| **T24-10** | 端到端**人工目视**（用户侧 @125%）：①勾/关闭 X 边缘平滑且**接缝无缺口无可见加深**；②AA 关闭逐位现状；③与圆角 AA 品质对照 | A3 |
| **T24-11** | ★ **`PatchSurface` 矩形化直接回归**（v1.1 新增，评审 §九③）：① **同一 backend 大半径 → 小半径交叉序列**（历史 stride 缺陷的触发形态——顺带兑现技术债表「跨半径角补丁测试缺口」）；② **W≠H 补丁 stride 正确性**（画斜线断言无行错位）；③ 矩形化后**角路径输出与改造前对照**（逐位不变判据） | A2 / A5 |

---

## 8. 实现顺序（三批）

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批一** | `LineCoverage.{h,cpp}` + `LineCoverageTests.cpp`（T24-1..6, 8a, 9） | 构建 + 既有 **286 全绿** + 新用例全绿（零后端接线） |
| **批二** | `CoverageRaster::RasterizeMask` + T24-7 | 全绿 |
| **批三** | `GDIBackend::DrawLine` 分支 + `PatchSurface` 矩形化 + `BlendPatch(w,h)` + **T24-8b / T24-11**（端到端与矩形化回归——均在真实 backend 上） | 四链全绿（★ 角路径零回归 = 硬判据）+ T24-10 人工目视 |

---

## 9. 开放项（O1–O4，均已带倾向）

| # | 项 | 倾向 |
|---|---|---|
| **O1** | S 来源 | **复用 `m_cornerMaskCache.GetSamples()`**——圆角与线段共用**一个** AA 精度旋钮（`SetSamples` 清空角缓存的语义不受影响；线段无缓存故无清空问题） |
| **O2** | PatchSurface 面积诊断 | 沿 `kDiagnosticThreshold` 模式：累计面积超阈值记一次 Warning（防超长线段一次性驻留无界增长无感知） |
| **O3** | 触及像素遍历的实现形态（沿带步进 vs bbox+距离预过滤） | 详设以 B-5 成本模型实测对比后定（两者量级相同，属实现自由） |
| **O4** | `#48`（`DrawPolyline` 原语）登记时机 | 随本阶段收口一并登记（含 R-1：出现「接缝零残差」硬需求 / R-2：第三个折线消费者）——**立项须用户拍板** |

## 10. 局限（L1–L4）

| # | 局限 |
|---|---|
| **L1** | 接缝 over-blend 残差 ≤ 28/255 / ≤ 4 像素（C-SEAM 冻结）——★★ **定性：这是当前阶段已知的非零残差，不是 Phase 24 缺陷**（v1.1 依评审 §十四）；根治 = `DrawPolyline`（#48） |
| **L2** | 分数轴对齐线段的亚像素信息被 legacy 丢弃（`y=10.4` → row 10，现状即如此；D1 分流优先保 R4 逐位） |
| **L3** | `a<1` + 轴对齐 + AA 开 → 覆盖度路径（带 AA 边缘）；同几何 `a≥1` → legacy 硬边——**alpha 语义一致性优先于几何路径一致**（D0 拍板的直接推论，盘点证实无现存观测点） |
| **L4** | `PatchSurface` 两维各自只增不减 ⇒ 超长线段一次性内存驻留（O2 诊断告警兜底；现状生产无超长线段调用方） |

## 11. 修订记录

- **v1.1**（2026-10-01）**评审第一轮处置 —— ✅ 基本通过，可进入 Detailed Design；3 处修正全部吸收**。① **评审结论**：「**Phase 24 Preliminary Design v1.0：基本通过，可进入 Detailed Design**」——全部主要定案（S=8 / 圆帽 / 显式分流 / D0 / D2 / `DrawPolyline` 延后 / 三批）获认可；逐条处置见 **§1.1**。② ★★ **必修 2 处**：**T24-2 容差重定义**（B-1 单像素误差推不出面积容差 ⇒ 对固定几何先建 **S=128 参照**再实测冻结）· **T24-8 拆 8a/8b**（生成层模拟证明不了端到端 ⇒ 8b 用**真实 backend 连画两段 + 像素读回**断言 C-SEAM，复用 `AntiAliasingTests.cpp:266-342` 的 L2 装置）。③ ★ **建议 1 处**：**新增 T24-11**（`PatchSurface` 矩形化直接回归——大→小半径交叉序列〔历史 stride 缺陷触发形态〕+ W≠H stride + 角路径输出对照；★ 顺带兑现技术债表「跨半径角补丁测试缺口」）。④ ★ **澄清 3 处**：**A-4/D3 措辞**（「GDI w≥2 输出**表现出**圆帽端点特征」——不给 GDI 内部下定义）· **§5.5 输入契约**（取整坐标域链条画死：**lround 只发生在后端物理 float 上**，绝不在 DIP 域；bbox / grid 原点 / 采样点三元组**单点出参**）· **#48 定性**（C-SEAM 残差 = **当前阶段已知非零残差，非 Phase 24 缺陷**）。⑤ ★ **规模修订**：用例 286+10 ≈ 296 → **286+12 ≈ 298**；批次内容同步（8b / 11 归批三）。⑥ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-10-01）初稿。**方法**：按评审优先级链 **Q2 → D3 → D0 → D2 → Q3 → Q4 先探针后定案**——P-A（GDI 线像素真相 8 条事实：**A-2 半开区间** · **A-3 w≥2 足迹 quirky** · **A-4 圆帽语言** · **A-6 现状勾接缝有 1px 缺口**）+ P-B（覆盖度原型 5 条：**B-1 S=8 收敛** · **B-2 接缝残差 27/255** · **B-4 轴线 0.5/0.5 ⇒ 必须分流** · **B-5 0.07ms/帧**）+ Q1 全库盘点（**a<1 调用方 = 0**）+ Q4 静态盘点（≤20 段/帧）。**定案**：Q2 超采样 S=8 · D3 圆帽（stadium，理论面积参照几何随之冻结）· D1 显式分流（谓词纯函数化）· D0 补齐 · D2 直算不缓存 · D4 宽度整数化同 legacy。**接缝**：R5 客观化为 C-SEAM（≤28/255 / ≤4 像素，T24-8 固化）+ `DrawPolyline` 根治方案记账建议（#48 待拍板）。**接口**：`LineCoverage` 生成层（新文件）+ `RasterizeMask` 合成层 + `DrawLine` 三分支 + `PatchSurface` 矩形化；公共 API +0 · CMake 0 改动 · 用例 286 → ~296。**待评审。**
