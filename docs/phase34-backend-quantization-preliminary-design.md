# Phase 34 · 后端几何取整口径统一 —— 初步设计（preliminary design v1.0）

> 状态：**v1.0（2026-10-17）待评审**
> 上游：职责确认 `phase34-backend-quantization-requirements.md` **v1.4 ✅ 已冻结（2026-10-17）**（四轮外部评审）
> 阶段类型：**机制级定线**（非新能力）· 公共 API **+0** · 公共头 **96 → 96** · CMake **0**

---

## §1 基线（带行号实测，2026-10-17；行号会漂，实施时以**函数名 + 形态**为准）

### 1.1 规范本体（已存在，本阶段**零改动预期**）

| 位置 | 内容 |
|---|---|
| `src/Render/BackendContract.h:90-93` | `QuantizeEdge(edge)` = `static_cast<int>(std::floor(edge))` |
| `src/Render/BackendContract.h:101-104` | `SnapVertex(v)` = `static_cast<float>(QuantizeEdge(v))`（GL 侧用） |
| `src/Render/BackendContract.h:77-81` | 契约原文：「边取 `floor`；尺寸由量化后的两条边相减导出，绝不独立取整」 |

### 1.2 GDI 侧的 62 处（职责确认 §10 已分类；此处只列**本阶段要动的 51 处**）

| 角色 | 处数 | 本阶段 | 说明 |
|---|---|---|---|
| 边 | **34** | **改**（22 截断 → 12 lround） | 目标 `QuantizeEdge` |
| 尺寸 | **17** | **改形态**（改为边相减） | 目标：删除独立取整 |
| 文本位置/线段端点 | 8 | **不动**（D6 = A） | 保留原规则 |
| 线宽/半径 | 3 | **不动**（N4） | Phase 8 契约 |

### 1.3 ★ 三个必须按函数整体处理的原因（实测，非推断）

| 函数 | 实测形态 | 为什么不能只改一半 |
|---|---|---|
| `DrawRect` | `:595-598`（backdrop 分支）用 `left + 取整(width)`；`:631-634`（主路径）用 `取整(x+w)` | **同一函数内两种口径并存** ⇒ 只改边不改尺寸，坏的那半仍是错的 |
| `BlendAlphaSolid` | `:50-51` 取 `lround(width/height)` 当 **DIB 尺寸**；`:218` 取 `lround(rect.x/y)` 当**放置位置** | ★★ **两者独立取整的直接后果**：`lround(x) + lround(w) ≠ floor(x+w)` ⇒ 相邻控件**漏缝或压边 1px**。这是 ② 类危害的**真实样本** |
| `WriteRoundedRectPremultiplied` | `:350-353` 取边与宽高各一次；`:352-353` 的 `w/h` 用于逐像素循环上界 | 循环上界与放置坐标必须同源，否则**最后一列/行画错像素** |

★ **`PushClip` 是个例外（好消息）**：`:1162-1165` 已经是 `取整(x + w)` 形态（**尺寸未独立取整**）⇒ 它**只有取整方向**问题（`lround`→`floor`），与 D2 无关 ⇒ **可独立成批**。

### 1.4 现有测试对取整口径的覆盖（实测）

`BackendContract` 的 `QuantizeEdge` 是纯函数，但**当前无用例直接断言它的四象限行为**。GDI 像素输出无自动化（属探针/目视）。⇒ 本阶段需补**真值表用例**（见 §5）。

---

## §2 逐文件改动规格

### △1 `src/Render/GDIBackend.cpp`（唯一生产代码改动文件）

#### △1-a 引入取整助手（文件内匿名 namespace）

**新增**（约 12 行，紧跟 `BackendContract.h` 的 include 之后）：

```cpp
/// @brief 矩形 → RECT（★ Phase 34：**边取 QuantizeEdge、尺寸由边相减**的唯一实现）
/// @details 契约 = `BackendContract.h:77-81`。**任何**把 `Rect` 变成 `RECT` 的地方都必须走它，
///          否则又会出现「边一个口径、尺寸另一个口径」（本阶段要消灭的正是这个）。
///          ★ 为什么不做成成员：`BlendAlphaSolid` 等在匿名 namespace（自由函数），
///          且它无状态 ⇒ 自由函数最省（不引入新抽象，条 22）。
inline RECT ToPixelRect(const Rect& rect) noexcept
{
    const int left   = QuantizeEdge(rect.x);
    const int top    = QuantizeEdge(rect.y);
    const int right  = QuantizeEdge(rect.x + rect.width);
    const int bottom = QuantizeEdge(rect.y + rect.height);
    RECT rc{};
    rc.left   = static_cast<LONG>(left);
    rc.top    = static_cast<LONG>(top);
    rc.right  = static_cast<LONG>(right);
    rc.bottom = static_cast<LONG>(bottom);
    return rc;
}

/// @brief 同上，但返回**量化后的尺寸**（给「需要 (w,h) 而不是 (right,bottom)」的消费者）
/// @details ★★ 存在的理由（本阶段最容易被写错的一处）：`BlendAlphaSolid` 需要
///          `CreateDIBSection` 的 `biWidth/biHeight`，那是**尺寸**不是边。
///          若继续用 `lround(rect.width)`，则 DIB 宽度与放置位置 `QuantizeEdge(rect.x)`
///          仍会各自取整 ⇒ `left + width ≠ right`，相邻块漏缝/压边。
///          ⇒ **尺寸必须由量化后的两条边相减**得出，这是契约原文的直译。
struct PixelBox
{
    LONG left, top;
    int  width, height;      ///< ★ **已由边相减导出**，不是对 `rect.width/height` 取整
};

inline PixelBox ToPixelBox(const Rect& rect) noexcept
{
    const int left   = QuantizeEdge(rect.x);
    const int top    = QuantizeEdge(rect.y);
    const int right  = QuantizeEdge(rect.x + rect.width);
    const int bottom = QuantizeEdge(rect.y + rect.height);
    return PixelBox{ static_cast<LONG>(left), static_cast<LONG>(top),
                     right - left, bottom - top };   // ★ 相减，不是取整
}
```

★ **为什么不复用 `ToPixelRect` 再算宽高**：`rc.right - rc.left` 也正确，但会让读者以为「宽高是 `RECT` 的副产品」；单独的类型让「**尺寸 = 边相减**」这条契约在**类型层面**可见（教学型框架，条 95③：理由与结论同等重要）。

#### △1-b 边级（34 处）——替换为 `QuantizeEdge`

| 函数 | 现行 | 目标 |
|---|---|---|
| `DrawRect`（backdrop 分支） | `:595-598` 逐字段 `static_cast<LONG>` | `const RECT rc = ToPixelRect(rect);` |
| `DrawRect`（主路径） | `:631-634` 同上（形态已对，方向错） | 同上 |
| `DrawRoundedRect` | `:976-977` | `const RECT bbox = ToPixelRect(rect);` |
| `DrawRoundedRectLegacy` | `:1051-1054` | 同上 |
| `DrawRoundedRectOpaqueAA` | `:1416-1417` | 同上（配合 △1-c） |
| `WriteRoundedRectPremultiplied` | `:350-351` | `const PixelBox box = ToPixelBox(rect);`（配合 △1-c） |
| `DrawLine`（bbox） | `:836-839`（`minX..maxY`） | `QuantizeEdge(minX)` 等 |
| `DrawFocusRect`（float 版） | `:1204-1207`、`:1214-1215`（`lround`） | `QuantizeEdge`（配合 △1-d 的常量语义） |
| `DrawFocusRect`（bbox 版） | `:1321-1322`（`- 2`） | `QuantizeEdge(rect.x) - 2` |
| `BlendAlphaSolid` | `:218`（`lround(rect.x/y)`） | `QuantizeEdge` |
| `PushClip` | `:1162-1165`（`lround`） | `QuantizeEdge`（★ D4：裁剪与绘制同源） |

#### △1-c 尺寸级（17 处）——**删除独立取整，改由边相减**

| 函数 | 现行 | 目标 |
|---|---|---|
| `DrawRect`（backdrop 分支） | `:597-598` `left + static_cast<LONG>(width)` | `ToPixelRect`（一并对齐） |
| `DrawRoundedRect` | `:978-979`、`:957-958` | `bbox = ToPixelRect`；`w/h` 从 `bbox` 相减 |
| `DrawRoundedRectLegacy` | `:1053-1054`、`:1023-1024` | 同上 |
| `DrawRoundedRectOpaqueAA` | `:1418-1419` | 从 `box.width/height` 取 |
| `WriteRoundedRectPremultiplied` | `:352-353` | 从 `box.width/height` 取 |
| `BlendAlphaSolid` | `:50-51` | ★ `const PixelBox box = ToPixelBox(rect);` → `box.width/height`（**DIB 尺寸**）与 `box.left/top`（**放置**）同源 |
| `DrawFocusRect`（bbox 版） | `:1323-1324` `left + 取整(width) + 4` | `QuantizeEdge(rect.x + rect.width) + 2`（★ 常量语义见 △1-d） |

#### △1-d `DrawFocusRect` 的 `±2 / +4` 常量语义（职责确认 §4.6 已核，此处落规格）

**现状**：`:1321-1322` = `static_cast<LONG>(rect.x) - 2`；`:1323-1324` = `bbox.left + static_cast<LONG>(rect.width) + 4`。

**语义**（已核）：焦点框在控件**四周各外扩 2 设备像素**。

**目标式**（★ 顺序不可换）：

```cpp
const int left   = QuantizeEdge(rect.x) - 2;
const int top    = QuantizeEdge(rect.y) - 2;
const int right  = QuantizeEdge(rect.x + rect.width)  + 2;   // ★ 不是 left + 取整(width) + 4
const int bottom = QuantizeEdge(rect.y + rect.height) + 2;
```

★ **为什么不能写 `QuantizeEdge(rect.x - 2)`**：`-2/+2` 是**设备像素**语义（与外扩像素数有关，与 DIP 无关）。先减后量化 ⇒ 在分数 DPI 下 `rect.x - 2` 的 frac 被 floor 吸收或进位，外扩量变成 1 或 3 px。
★ **为什么 `+4` 变 `+2`**：原式是 `bbbox.left`（= `x - 2`）**再** `+ 4` ⇒ 净效应 `x + 2`。等价改写为「右边 + 2」后，`+4` 自然消失。★ **这是等价变换，不是改语义**——须在用例里用原式对照验证（见 §5 T34-3）。

#### △1-e 端点与线宽（**不动**，但须加注释说明「有意保留」）

| 位置 | 处置 |
|---|---|
| `DrawText:681-682`、`DrawTextImpl:723-724` | 保留 `static_cast<LONG>`；加注释指向职责确认 **D6 = A** |
| `DrawLineLegacy:858-859` | 保留 `lround`；同上 |
| `BlendAlphaSolid:120`、`DrawRoundedRect:960`、`DrawRoundedRectLegacy:1025` | 保留 `lround`；加注释指向 **N4**（Phase 8 契约：宽度 `lround` + 下限 1px） |

★ **注释是契约的一部分**：A1 的白名单要求「未改的处必须能指回理由」。没有注释的「没改」看起来就是「漏改」。

### △2 `src/Render/BackendContract.h`（**注释订正，零代码改动**）

`:84-89` 的「已知偏差」段现有表述与实测不符（职责确认 §9.4 已记）：说「多处用截断」但没提 **12 处 `lround`**（后者才是主要风险）。改为：

```cpp
///          ★★ **历史偏差（Phase 34 已消除，2026-10-17）**：本段曾登记「GDIBackend 多处边转换
///          仍用 static_cast<LONG>，仅在负分数边相差 1px」。实测（职责确认 §9.3）该描述有两处不准：
///          ① 处数不是「多处」而是 **21 处边级截断**（合计 40 处截断，含尺寸/端点）；
///          ② **完全未提**另有 **12 处边级 std::lround**——而 `lround` 与 `floor` 在
///             非整数 DPI 下分歧 **43.2%–49.8%**，是截断组（仅负坐标）的**一个数量级以上**。
///          ⇒ Phase 34 起：边级统一 `QuantizeEdge`、尺寸由边相减、端点（D6）与线宽半径（N4）显式保留。
```

### △3 无其它生产代码改动

`GLRenderer.cpp` 已用 `SnapVertex`（无需动）· 公共头零改动 · CMake 零改动。

---

## §3 接口草案（公共面）

**零新增**。本阶段全部在 `src/`（内部）与 `BackendContract.h`（内部头）内。

★ 唯一的「接口」是文件内新增的 `ToPixelRect` / `ToPixelBox` / `PixelBox`——**匿名 namespace 内**，不进任何头文件 ⇒ 外部不可见、不构成 API 面。

---

## §4 影响面

| 维度 | 影响 |
|---|---|
| 公共 API | **+0** |
| 公共头 | **96 → 96** |
| 内部件 | `GDIBackend.cpp` +约 40 行（助手 2 个 + 51 处改写）· `BackendContract.h` 注释 |
| 用例 | **+N**（§5）——`QuantizeEdge` 真值表 + 等价变换对照 |
| CMake | **0** |
| ★ 像素输出 | **会变**（本阶段的目的）⇒ 差异须按职责确认 §4.6 三类逐条归因（A4/A7） |

---

## §5 测试方向

| # | 用例 | 判据 | 类型 |
|---|---|---|---|
| **T34-1** | `QuantizeEdge` 真值表（**四象限**） | 正数 `frac<0.5` / `frac≥0.5` / 负数 `frac<0.5` / `frac≥0.5` + 整数 + `±0.5` 边界 + `0.0` + 负零 | 纯函数，无窗口 |
| **T34-2** | **尺寸 = 边相减**（★ 职责确认 ② 类） | 对 `x=1.25,w=2.75` ⇒ `ToPixelBox` 给出 `left=1, width=3`（right=4）而**不是** `1+2=3`；对 `150%` 的 `x=1,w=1` ⇒ `left=1,width=2`（right=3） | 纯函数（★ 不需要窗口——这是把 ② 类**变成可断言**的关键）　★★ **设计稿落盘后已实跑验证**（`verify_design_math.py`，真实 g++ 编译执行，非推算）：`x=1.25,w=2.75` ⇒ 旧右 3 / **新右 4**（②类）· `x=1.5,w=1.5`（150%，**全正**）⇒ 旧 2 / **新 3**（②类）· `x=-1.5,w=3.0` ⇒ 旧 2 / **新 1**（①类负分数边）· `x=2,w=4`（整数）⇒ **不变** · `x=1.75,w=1.75`（175%）⇒ 旧 2 / **新 3**（②类）；**`left + width == right` 全部自洽（0 失败）** |
| **T34-3** | `DrawFocusRect` 外扩**等价变换** | 新式（`QuantizeEdge(x+w)+2`）与原式（`LONG(x)-2+4`）在整数坐标下**逐值相等**；在分数坐标下差异**符合 §4.6 ①/② 归因** | 纯函数 + 探针 |
| **T34-4** | 裁剪与绘制同源（A2） | `PushClip` 与 `DrawRect` 对同一 `Rect` 得到**同一** `RECT` | 可经 `RecordingBackend`？★ **不可**——`RecordingBackend` 记录的是**未量化**的 `Rect`。⇒ 需把「量化」抽成可测自由函数（本设计已如此），用例直接调它 |
| **T34-5** | 100%/200% 零差异（★ 职责确认 §4.6 的强判据） | 库外探针在两档 DPI 下逐字节 diff **必须为空**（三类差异在这两档实测为 0%） | 库外探针 + 目视 |

★ **T34-4 的诚实边界**：`RecordingBackend` 记录 `Rect`（浮点，未量化），所以**它无法验证后端的量化行为**。这不是本阶段的缺陷，而是「记录层在量化之前」的既有结构。⇒ 本设计把量化抽成**自由纯函数**正是为了让 A2/A3 可测（否则只能靠读代码）。

---

## §6 实施顺序（对应职责确认 D7 的三批）

| 批 | 内容 | 风险 | 回归 |
|---|---|---|---|
| **批一** | △1-a（两个助手）+ 边级 22 处截断 + **同函数的尺寸 14 处**（△1-c） | ①（仅负分数边，18.5%/12.3%/18.5%）+ ②（尺寸，37.0%/24.7%/37.0%） | 常规 + 负坐标专项 + **T34-2** |
| **批二** | 边级 12 处 `lround` → `QuantizeEdge`（含 `PushClip`、`DrawFocusRect` float 版） | ③（43.2%–49.8%） | ★ **独立目视回归 + 逐字节 diff** |
| **批三** | `BackendContract.h` 注释订正（△2）+ 未改处的「有意保留」注释（△1-e） | 零像素风险 | 文档核对 |

★ **批一与批二不可合并**（分歧率差一个数量级，合并后无法归因）；**批一内部不可拆分**（同函数的边与尺寸必须同批，否则两种口径并存）。

---

## §7 开放项

| # | 项 | 状态 |
|---|---|---|
| **O1** | `ScaleRect` 在各 DPI 的分数分布 | ✅ 已测（职责确认 §9.1：仅 125/150/175%） |
| **O2** | 哪些处上游已整数 | ✅ 已测（职责确认 §9.2 分歧率；本稿 §1.3 逐函数形态） |
| **O3** | `lround` 里哪些属尺寸应豁免 | ✅ 已定（职责确认 §10.4：3 处线宽半径保留） |
| **O4** | ★ **新** `DrawFocusRect` 的 `+4` 在原式里是否恒等于「右扩 2」 | ⏳ **须实施时用 T34-3 验证**——若某分支（如 float 版的 `-1`）语义不同，则不能照搬本稿 △1-d |
| **O5** | ★ **新** `ForceRectAlphaFrosted(rc)`（`DrawRect:614`）拿的是**已量化的 `rc`** | ✅ **已核（本稿 §1.3）**：它消费 `rc` 做 alpha 覆盖 → 必须与 `FillRect` 的 `rc` **完全同源**。本设计用**同一个** `ToPixelRect` 结果同时喂两者 ⇒ 天然保证。★ **实施时须核**：该函数是否**再**自行量化一次 |


---

## §8 修订记录

- **v1.0**（2026-10-17）**初稿**。上游 = 职责确认 **v1.4 ✅ 已冻结**（四轮外部评审，2026-10-17）。① **§1 基线全部带行号实测**，并**修正职责确认 §10 的一处口径**：`PushClip`（`:1162-1165`）实为 `lround(x+w)` 形态（**尺寸未独立取整**）⇒ 它只有 ③ 类（取整方向）问题、与 D2 无关 ⇒ **可独立成批**。② **§1.3 给出「必须按函数整体处理」的三条实测理由**，其中 `BlendAlphaSolid` 是 ② 类的**真实危害样本**：`lround(width)` 定 DIB 尺寸、`lround(rect.x)` 定放置 ⇒ `left + width ≠ right` ⇒ 相邻控件**漏缝或压边 1px**。③ **§2 逐文件最小修改面**（条 42）：新增**两个文件内助手** `ToPixelRect` / `ToPixelBox`，后者用独立类型把「★ **尺寸 = 边相减**」这条契约**在类型层面可见**（教学型框架，条 95③）；`△1-b`/`△1-c` 逐函数列出替换目标；`△1-d` 把 `DrawFocusRect` 的 `±2/+4` 落成「**先量化边、再套常量**」并说明为何不能写 `QuantizeEdge(rect.x - 2)`（外扩是设备像素语义）。④ **§2 △1-e**：端点（D6）与线宽半径（N4）**不动但须加注释**——没有注释的「没改」看起来就是「漏改」。⑤ **§5 测试方向**：★ 诚实登记 **`RecordingBackend` 无法验证后端量化**（它记录的是**未量化**的 `Rect`）⇒ 本设计把量化抽成**自由纯函数**正是为了让 A2/A3 **可测**；`T34-2` 已**实跑验证**（真实 g++ 编译执行），五组样本全部符合 §4.6 三类归因、`left + width == right` **0 失败**。⑥ **§6 实施顺序**：批一（边截断 + 同函数尺寸）· 批二（`lround` 边级，独立目视回归）· 批三（注释与契约文档）；★ 批一与批二**不可合并**（分歧率差一个数量级），批一**内部不可拆分**（同函数边与尺寸必须同批）。⑦ **§7 开放项**：职责确认的 O1–O3 已关闭；新增 **O4**（`+4` 等价性待 T34-3 验证）与 **O5**（已核，`ForceRectAlphaFrosted` 与 `FillRect` 必须同源）。
