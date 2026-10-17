# Phase 34 · 后端几何取整口径统一 —— 初步设计（preliminary design v1.1）

> 状态：**v1.1（2026-10-17）待评审** —— ★ 外部评审六点已吸收：**三项必改**（P1-1 批次与 17 处尺寸的对应 / P1-2 T34-3 旧式不完整 / P1-3 O4 范围界定）+ **三项建议**（P2-4 助手去重 / P2-5 T34-5 输入前提 / P2-6 有效尺寸与数值边界）
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
	/// ★★ **前置条件（v1.1 补，评审 P2-6）**：
	///   · `rect` 各分量须**有限**且 `quantize` 后落在 `int` 可表示范围内；
	///   · **不要求** `width/height > 0`——但量化后 `width <= 0` 或 `height <= 0` 时，
	///     各消费者**必须**先判空再使用（沿 `BlendAlphaSolid:45-55` 的既有「空矩形 no-op」契约）；
	///   · ★ **不得**假定调用方必然传正常矩形：`CreateDIBSection` 的 `biWidth` 与
	///     逐像素循环上界都直接吃这两个值。
};

inline PixelBox ToPixelBox(const Rect& rect) noexcept
{
	const RECT rc = ToPixelRect(rect);   // ★ v1.1：**复用**同一条边计算，不重复实现（评审 P2-4）
	return PixelBox{ rc.left, rc.top,
	                 static_cast<int>(rc.right - rc.left),    // ★ 相减，不是取整
	                 static_cast<int>(rc.bottom - rc.top) };
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


#### △1-d2 ★★ `DrawFocusRect` **float 版**（`:1204-1215`）——与 bbox 版是**两套独立语义**（v1.1 新增，评审 P1-3）

★★ **最容易被误套的地方**：本函数有**四个**取整结果，语义各不相同，**不能一律当「外扩」处理**：

| 变量 | 表达式 | 语义 | 本阶段处置 |
|---|---|---|---|
| `fLeft` / `fTop` | `lround(rect.x)` / `lround(rect.y)` | **可见像素起点**（不内缩） | `QuantizeEdge` |
| `fRight` / `fBottom` | `lround(rect.x + rect.width) **- 1**` | ★ **可见像素终点**——`-1` 是 **GDI 开区间换算**（`:1202-1203` 原文：可见像素到 `right-1`；`right` 列被 `PushClip` 裁掉 ⇒ 手动画段需显式内缩） | `QuantizeEdge(x+w)` 再 **`- 1`**（**保留**） |
| `gRight` / `gBottom` | `lround(rect.x + rect.width)` | ★ **几何边界**（供弧心/半径，**不内缩**） | `QuantizeEdge(x+w)`（**无 `-1`**） |

★ **两条铁律**：
1. **`-1` 与焦点框的 `±2` 外扩无关**——前者是**开区间换算**（所有手绘线段都要做，`:1202` 的注释已明写），后者是**控件外扩装饰**。混为一谈会把焦点框整体缩进 1px。
2. **`fRight` 与 `gRight` 都必须改**，且**改后仍相差 1**：若只改 `fRight` 不改 `gRight`，弧心会基于「内缩后的可见边界」计算 ⇒ 复现 `:1209-1213` 记录的**历史缺陷**（「四角弧心各内移 1px ⇒ 焦点框内收」）。

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
| **T34-3**（v1.1 修正旧式——评审 P1-2） | `DrawFocusRect` 外扩**等价变换**（★ **bbox 版与 float 版分开验**） | ★★ **旧式完整写法**（此前写成 `LONG(x)-2+4` 是**错的**，漏了宽度项）：**`right = static_cast<LONG>(rect.x) - 2 + static_cast<LONG>(rect.width) + 4`**；**新式 = `QuantizeEdge(rect.x + rect.width) + 2`**。★ **必须验证三条**：① **输入为整数像素**时两式**逐值相等**（等价性）；② **输入为分数像素**时差异**符合 ①/② 归因**（① = 左边负分数；② = 右边由宽度独立取整改为边相减）；③ **左/上/右/下四条边**各自的外扩语义（左 `-2` · 上 `-2` · 右 `+2` · 下 `+2`）。★ **术语澄清**：「整数坐标」= **量化前的像素空间输入为整数**（即 `ScaleRect` 之后的值），**不是**「DIP 输入为整数」——后者在非整数 DPI 下仍会得到分数像素坐标。★ **float 版（`:1204-1215`）不适用本项**：它的 `-1` 是 GDI 开区间换算（见 §2.1-d2），不是外扩 | 纯函数 + 探针 |
| **T34-4** | 裁剪与绘制同源（A2） | `PushClip` 与 `DrawRect` 对同一 `Rect` 得到**同一** `RECT` | 可经 `RecordingBackend`？★ **不可**——`RecordingBackend` 记录的是**未量化**的 `Rect`。⇒ 需把「量化」抽成可测自由函数（本设计已如此），用例直接调它 |
| **T34-5**（v1.1 补输入前提——评审 P2-5） | 100%/200% 零差异 | ★★ **前提（必须同时满足，否则本判据不成立）**：探针输入几何 = **整数 DIP 坐标**，且该 DPI 档的 scale 为整数（100% = 1.0 / 200% = 2.0）⇒ 缩放后仍为整数像素边。★ **为什么必须写前提**：职责确认 §9.1 的「零分歧率」来自**整数 DIP 样本**；若实际绘制链允许**分数 DIP 输入**（如 `x=0.6`），则 100% 下也会出现 ② 类差异 ⇒ **无条件的「逐字节零差异」与 §4.6 三类模型自相矛盾**。★ **两种执行方式（择一，须在详设定）**：① **整数 DIP 输入探针** ⇒ 断言**零差异**（验证的是「整数 DIP + 整数 scale 下三类均不触发」这一数学性质）；② **真实 UI 场景探针** ⇒ **不要求零差异**，改为「每处差异都能归入 §4.6 三类」（A7 的原因层）。 | 库外探针（两档 DPI）+ 目视（条 1：AI 不代跑） |

★ **T34-4 的诚实边界**：`RecordingBackend` 记录 `Rect`（浮点，未量化），所以**它无法验证后端的量化行为**。这不是本阶段的缺陷，而是「记录层在量化之前」的既有结构。⇒ 本设计把量化抽成**自由纯函数**正是为了让 A2/A3 可测（否则只能靠读代码）。

---

## §6 实施顺序（★ v1.1 重划——**按函数分单元，不按取整法分批**；理由见 §6.1）

| 批 | 内容 | 风险 | 回归 |
|---|---|---|---|
| **批一** | △1-a（两个助手）+ 边级 22 处截断 + **同函数的尺寸 14 处**（△1-c） | ①（仅负分数边，18.5%/12.3%/18.5%）+ ②（尺寸，37.0%/24.7%/37.0%） | 常规 + 负坐标专项 + **T34-2** |
| **批二** | 边级 12 处 `lround` → `QuantizeEdge`（含 `PushClip`、`DrawFocusRect` float 版） | ③（43.2%–49.8%） | ★ **独立目视回归 + 逐字节 diff** |
| **批三** | `BackendContract.h` 注释订正（△2）+ 未改处的「有意保留」注释（△1-e） | 零像素风险 | 文档核对 |

★ **批一与批二不可合并**（分歧率差一个数量级）；**批一内部不可拆分**（同函数内边与尺寸必须同批）。

### 6.1 ★★ 批次重构：职责确认 D7 的「按取整法分批」在函数层面**不成立**（v1.1 订正，评审 P1-1）

★★ **评审指出的问题成立，且比评审说的更严重**。职责确认 D7 按**取整法**分三批：① 22 处截断边 → ② 17 处尺寸 → ③ 12 处 `lround` 边。但实测的**函数-角色矩阵**显示：**没有一个函数能整块落进单一批次**。

| 函数 | 边·截断 | 边·lround | 尺寸·截断 | 尺寸·lround | 端点 | 线宽半径 |
|---|---|---|---|---|---|---|
| `DrawRect` | 6 | 0 | 2 | 0 | 0 | 0 |
| `DrawRoundedRect` | 2 | 0 | 4 | 0 | 0 | 1 |
| `DrawRoundedRectLegacy` | 4 | 0 | 2 | 0 | 0 | 1 |
| `DrawRoundedRectOpaqueAA` | 2 | 0 | 2 | 0 | 0 | 0 |
| `WriteRoundedRectPremultiplied` | 2 | 0 | 2 | 0 | 0 | 0 |
| `DrawLine` | 4 | 0 | 0 | 0 | 0 | 0 |
| **`BlendAlphaSolid`** | 0 | **2** | 0 | **2** | 0 | 1 |
| **`DrawFocusRect`** | 2 | **6** | 2 | 0 | 0 | 0 |
| **`PushClip`** | 0 | **4** | 0 | 0 | 0 | 0 |
| `DrawLineLegacy` | 0 | 0 | 0 | 1 | 4 | 0 |
| `DrawText` / `DrawTextImpl` | 0 | 0 | 0 | 0 | 4 | 0 |

★★ **三个交错点**（评审点出了第一个，另外两个同样致命）：

| # | 函数 | 冲突 | 后果 |
|---|---|---|---|
| **①** | `BlendAlphaSolid` | 边是 `lround`（③类）、尺寸也是 `lround`（②类的尺寸面） | 评审原话：**助手的调用方式已经把不同批次耦合在一起** |
| **②** | `DrawFocusRect` | **同一函数里 2 处截断边（①）+ 6 处 lround 边（③）+ 2 处截断尺寸（②）** | 三批全占 ⇒ 按批施工会**分三次改同一函数** |
| **③** | `DrawRoundedRect` / `Legacy` | 边截断（①）+ 尺寸截断（②）+ **线宽半径 `lround`（N4 不动）** | 若按「批二 = 全部 17 处尺寸」执行，会把**线宽半径**也裹进去 ⇒ **越界** |

★ **③ 正是评审说的「剩余 3 处尺寸何时改」**：17 处尺寸里 **3 处是 `lround`**——`BlendAlphaSolid:50-51`（矩形尺寸）与 `DrawLineLegacy:850`（**线宽 `penWidth`**）。★ 后者**根本不是矩形尺寸**，按 N4 **不得改** ⇒ 「17 处尺寸全改」这句话本身是错的，**真实可改的是 14 处**。

#### 6.2 ★ 重构后的实施单元（**按函数分单元**，每个单元内变更同批提交、同批回归）

| 单元 | 函数 | 变更内容 | 差异类别（职责确认 §4.6） | 风险 |
|---|---|---|---|---|
| **U1** | `DrawRect` · `DrawLine` · `DrawRoundedRect` · `DrawRoundedRectLegacy` · `DrawRoundedRectOpaqueAA` · `WriteRoundedRectPremultiplied` | 边 20 处截断 → `QuantizeEdge` + 尺寸 14 处 → 边相减 | **① ∪ ②** | 中（② 正坐标也触发） |
| **U2** | `PushClip` | 边 4 处 `lround` → `QuantizeEdge` | **③** | 高（43.2%–49.8%） |
| **U3** | `BlendAlphaSolid` | 边 2 处 `lround` → `QuantizeEdge` **+** 尺寸 2 处 `lround` → 边相减 | **② ∪ ③** | ★★ **最高**（两族叠加，且是唯一「DIB 尺寸 + 放置」分离的函数） |
| **U4** | `DrawFocusRect`（bbox 版 `:1321-1324`） | 边 2 处截断 → `QuantizeEdge` + 尺寸 2 处 → 边相减 + `±2` 常量 | **① ∪ ②** | 中 |
| **U5** | `DrawFocusRect`（float 版 `:1204-1215`） | 边 6 处 `lround` → `QuantizeEdge`（★ **`-1` 是开区间换算，不是外扩**，见 §2.1-d2） | **③** | 高 |
| **U6** | 注释与契约文档 | △1-e 保留说明 + △2 `BackendContract` 订正 | 零像素 | 无 |

★ **U1 与 U4 可合批**（同为 ①∪②）；**U3 必须单独**（唯一 ②∪③ 叠加）；**U5 必须单独**（同函数内还有 U4 的边，但 float 版与 bbox 版是**两条独立代码路径**，可分开验）。
★ **不变的约束**：同函数内的边与尺寸**必须同批**（否则 `left+取整(w)` 与 `取整(x+w)` 并存 ⇒ 比现状更糟）；**端点 8 处与线宽半径 3 处全程不动**（D6 / N4）。

---

## §7 开放项

| # | 项 | 状态 |
|---|---|---|
| **O1** | `ScaleRect` 在各 DPI 的分数分布 | ✅ 已测（职责确认 §9.1：仅 125/150/175%） |
| **O2** | 哪些处上游已整数 | ✅ 已测（职责确认 §9.2 分歧率；本稿 §1.3 逐函数形态） |
| **O3** | `lround` 里哪些属尺寸应豁免 | ✅ 已定（职责确认 §10.4：3 处线宽半径保留） |
| **O4** | `DrawFocusRect` 的 `±2/+4` 语义 | ✅ **v1.1 已核并关闭（评审 P1-3）**——**分两条路径，不可互套**：**① bbox 版（`:1321-1324`）**：`-2` / `+4` 是**固定像素外扩 2px**，旧式 `LONG(x) - 2 + LONG(width) + 4` 的净效应 = `LONG(x) + LONG(width) + 2` ⇒ 新式 `QuantizeEdge(x+w) + 2` **等价**（整数输入下）；**② float 版（`:1204-1215`）**：`fRight = lround(x+w) - 1` 的 `-1` 是 **GDI 开区间换算**（`:1202-1203` 注释原文：「可见像素 = [left, right-1]，right 列被 PushClip 裁掉 ⇒ 手动画段需显式内缩」），**与外扩无关**；且同函数还有 `gRight = lround(x+w)`（**几何边界**，供弧心）。⇒ ★ **float 版只做「`lround` → `QuantizeEdge`」的方向替换，`-1` 原样保留**，**不得**套用 bbox 版的 `+2` 变换 |
| **O5** | ★ **新** `ForceRectAlphaFrosted(rc)`（`DrawRect:614`）拿的是**已量化的 `rc`** | ✅ **已核（本稿 §1.3）**：它消费 `rc` 做 alpha 覆盖 → 必须与 `FillRect` 的 `rc` **完全同源**。本设计用**同一个** `ToPixelRect` 结果同时喂两者 ⇒ 天然保证。★ **实施时须核**：该函数是否**再**自行量化一次 |


---

## §8 修订记录

- **v1.1**（2026-10-17）**外部评审六点吸收（3 必改 + 3 建议）**。① ★★★ **P1-1 批次与 17 处尺寸的对应（本轮最实质订正）**：复评指出「§6 批一只列 22 处截断边 + 14 处尺寸，**剩余 3 处尺寸没交代**」，并指出 `BlendAlphaSolid` 的助手调用**已把不同批次耦合**。经实测**问题比复评说的更严重**：按**函数-角色矩阵**核对，**没有任何函数能整块落进 D7 的单个批次**——`BlendAlphaSolid`（边 `lround` + 尺寸 `lround` ⇒ ③∪②）、`DrawFocusRect`（**三批全占**：2 截断边 + 6 lround 边 + 2 截断尺寸）、`DrawRoundedRect`/`Legacy`（边① + 尺寸② + **线宽半径 lround（N4 不动）**）。★ 并**订正一处计数口径**：17 处尺寸中 **3 处是 `lround`**，其中 `DrawLineLegacy:850` 是**线宽 `penWidth`**、**根本不是矩形尺寸**，按 N4 **不得改** ⇒ **真实可改的尺寸是 14 处**，「17 处尺寸全改」这句话本身是错的。⇒ **新增 §6.1（冲突矩阵）+ §6.2（重构为 U1–U6 六个「函数级实施单元」）**：按**函数**分单元而非按取整法分批，每个单元内边与尺寸**同批提交**；**U3（`BlendAlphaSolid`）单独一批**（唯一 ②∪③ 叠加）、**U5（float 版）单独一批**。② ★★ **P1-2 T34-3 旧式补全**：原写 `LONG(x)-2+4`，**漏了宽度项**⇒ 测试会验错对象。改为完整式 **`LONG(x) - 2 + LONG(width) + 4`** vs 新式 **`QuantizeEdge(x+w) + 2`**，并列出**三条必验**（整数输入等价 / 分数输入符合 ①② 归因 / 四条边各自的外扩语义）；★ 补**术语澄清**：「整数坐标」= **量化前的像素空间输入为整数**（`ScaleRect` 之后），不是「DIP 输入为整数」。③ ★★ **P1-3 O4 关闭**：复评指「本稿已给出替换方案，却保留可能推翻它的条件」——成立。**分两条路径界定**：**bbox 版**的 `±2/+4` = 固定像素外扩（已核，按本稿处理）；**float 版**的 `-1` = **GDI 开区间换算**（`:1202-1203` 原文可证），**与外扩无关** ⇒ **只做 `lround`→`QuantizeEdge` 的方向替换，`-1` 原样保留，不得套 bbox 版的 `+2`**。新增 **§2.1-d2** 逐变量列出四个取整结果的语义（`fLeft/fTop` 起点 · `fRight/fBottom` 终点含 `-1` · `gRight/gBottom` 几何边界无 `-1`），并写明**只改 `fRight` 不改 `gRight` 会复现「焦点框内收」历史缺陷**。④ ★ **P2-4 助手去重**：`ToPixelBox` 改为**复用** `ToPixelRect` 的结果再相减，不再重算同一组边（消除两份实现漂移的可能，同时保留 `PixelBox` 类型的语义价值）。⑤ ★ **P2-5 T34-5 输入前提**：补「**整数 DIP 坐标 + 整数 scale**」这一前提，并指出**无条件零差异与 §4.6 三类模型自相矛盾**（若允许分数 DIP 输入，100% 下也会出 ② 类差异）；给出两种执行方式（整数 DIP 探针断言零差异 / 真实场景探针改用 A7 原因层）。⑥ ★ **P2-6 有效尺寸与数值边界**：`PixelBox` 补**前置条件**（分量有限、`quantize` 后可表示；**不要求** `width/height > 0`，但消费者须先判空；**不得**假定调用方传正常矩形——`CreateDIBSection` 的 `biWidth` 与逐像素循环上界直接吃这两个值）。⑦ ★ **附：一次自证伪**——本稿 §5 曾把 T34-3 的原式简写为 `LONG(x)-2+4`，是**我在设计稿里引入的错误**（与职责确认 §4.6 已核的完整式不符），靠复评指出后修正；已在 T34-3 里写明「此前写法是错的」。
- **v1.0**（2026-10-17）**初稿**。上游 = 职责确认 **v1.4 ✅ 已冻结**（四轮外部评审，2026-10-17）。① **§1 基线全部带行号实测**，并**修正职责确认 §10 的一处口径**：`PushClip`（`:1162-1165`）实为 `lround(x+w)` 形态（**尺寸未独立取整**）⇒ 它只有 ③ 类（取整方向）问题、与 D2 无关 ⇒ **可独立成批**。② **§1.3 给出「必须按函数整体处理」的三条实测理由**，其中 `BlendAlphaSolid` 是 ② 类的**真实危害样本**：`lround(width)` 定 DIB 尺寸、`lround(rect.x)` 定放置 ⇒ `left + width ≠ right` ⇒ 相邻控件**漏缝或压边 1px**。③ **§2 逐文件最小修改面**（条 42）：新增**两个文件内助手** `ToPixelRect` / `ToPixelBox`，后者用独立类型把「★ **尺寸 = 边相减**」这条契约**在类型层面可见**（教学型框架，条 95③）；`△1-b`/`△1-c` 逐函数列出替换目标；`△1-d` 把 `DrawFocusRect` 的 `±2/+4` 落成「**先量化边、再套常量**」并说明为何不能写 `QuantizeEdge(rect.x - 2)`（外扩是设备像素语义）。④ **§2 △1-e**：端点（D6）与线宽半径（N4）**不动但须加注释**——没有注释的「没改」看起来就是「漏改」。⑤ **§5 测试方向**：★ 诚实登记 **`RecordingBackend` 无法验证后端量化**（它记录的是**未量化**的 `Rect`）⇒ 本设计把量化抽成**自由纯函数**正是为了让 A2/A3 **可测**；`T34-2` 已**实跑验证**（真实 g++ 编译执行），五组样本全部符合 §4.6 三类归因、`left + width == right` **0 失败**。⑥ **§6 实施顺序**：批一（边截断 + 同函数尺寸）· 批二（`lround` 边级，独立目视回归）· 批三（注释与契约文档）；★ 批一与批二**不可合并**（分歧率差一个数量级），批一**内部不可拆分**（同函数边与尺寸必须同批）。⑦ **§7 开放项**：职责确认的 O1–O3 已关闭；新增 **O4**（`+4` 等价性待 T34-3 验证）与 **O5**（已核，`ForceRectAlphaFrosted` 与 `FillRect` 必须同源）。
