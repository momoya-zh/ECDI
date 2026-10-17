# Phase 34 · 后端几何取整口径统一 —— 初步设计（preliminary design v1.4）

> 状态：**v1.4（2026-10-17）待评审** —— ★ 第五轮评审四项 P1 已吸收：**批次表与 U 单元覆盖不一致** · **AA 分支变量来源缺失** · **T34-2 坐标空间未统一** · **`int`→`LONG` 类型表述不准确**；三项 P2（注释时态 / 审计可勾选 / 归因可判定）同步
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

### 1.2 GDI 侧的 62 处（职责确认 §10 已分类；此处只列**本阶段要动的 50 处**）

| 角色 | 处数 | 本阶段 | 说明 |
|---|---|---|---|
| 边 | **34** | **改**（22 截断 → 12 lround） | 目标 `QuantizeEdge` |
| 尺寸（**矩形**） | **16** | **改形态**（改为边相减） | 目标：删除独立取整。★ **v1.2 订正**：原记 17 处，其中 `DrawLineLegacy:850` 的 `width` 实为 **`penWidth`（线宽）**、**不是矩形尺寸** ⇒ 归入「线宽/半径」保留类（N4） |
| **非矩形尺寸**（**不改**） | **1** | **不动**（N4） | `DrawLineLegacy:850` 的 `width` = **`penWidth` 线宽**，**不是矩形尺寸** ⇒ 归保留类。★ **v1.3 补**：此前本表漏列此类别，导致四类合计 61 与总数 62 差 1（评审 P2-1 指出） |
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
	LONG left;          ///< 量化后的**左边界**（= `QuantizeEdge(rect.x)`）
	LONG top;           ///< 量化后的**上边界**（= `QuantizeEdge(rect.y)`）
	int  width;         ///< ★★ **由右边界与左边界相减导出**，**不是**对 `rect.width` 取整
	int  height;        ///< ★★ 同上（下边界 − 上边界）
};

/// @par 前置条件（★ v1.2 补全——评审 P1-3：**必须覆盖中间值与最终结果**，不只是输入）
///   ① 输入分量 `rect.x/y/width/height` **有限**；
///   ② ★ **中间和有限**：`rect.x + rect.width` 与 `rect.y + rect.height` 也须有限
///      （两个有限浮点数相加**仍可能溢出到 ±inf**，此时 `floor` 的结果未指定）；
///   ③ ★★ **按实际运算类型逐项约束（v1.3 订正——评审 P1-3）**——不能只说「可表示于 LONG」：
///      · `QuantizeEdge` 的返回类型是 **`int`**（`BackendContract.h:90`）⇒ **`floor` 后的值须可表示于 `int`**；
///      · `PixelBox::left/top` 是 **`LONG`**——★ **v1.4 订正（评审 P1-4）**：在本项目支持的全部 Windows ABI 上，**`int` 与 `LONG` 均为 32 位有符号整数**，故 `int` 的全部可表示值**均可由 `LONG` 表示**；★ 但这是**平台相关事实**（`LONG` 在别的 ABI 上可能是 64 位），**不是普遍的类型规则**，不得据此推断可移植性；
///      · `PixelBox::width/height` 是 **`int`**，取自 `rc.right - rc.left`——★ 该减法**先在 `LONG` 上做**（`RECT` 成员为 `LONG`），**再窄化到 `int`** ⇒ 须保证**差值本身**在 `int` 范围内，而不只是两端在 `int` 内；
///      · `DrawFocusRect` 的 `QuantizeEdge(...) - 2` **在 `int` 上运算** ⇒ `edge - 2` 不得下溢、`edge + 2` 不得上溢；
///      以上四类**逐项**成立，才算「所有实际中间表达式与最终结果均可由其运算类型表示」。
///   ④ ★ **差值可表示**：`right - left` / `bottom - top` 须可表示于 `int`
///      （两条边各自可表示 **不代表**其差值可表示——`LONG` 的减法结果可能超出 `int`）；
///   ⑤ ★ **外扩后仍可表示**：消费者若再做固定像素偏移（如 `DrawFocusRect` 的 `±2`），
///      偏移后的边界仍须可表示；
///   ⑥ ★ **不满足时的归属（本阶段不新增防御层）**：由**既有上游契约**保证——
///      `Rect` 的一切来源经 `Renderer::ScaleRect` 与布局链产出，其量级受窗口尺寸约束；
///      本阶段**只登记这条依赖**，不在此处加运行时钳制（沿条 22 YAGNI 与条 95⑤）。★ **依赖的具体出处（v1.3 补）**：`RD-2`/`Renderer::ScaleRect` 的入参 `Rect` 来自布局链，其量级被 `Window` 的客户区尺寸约束（`Win32PlatformWindow` 的 `GetClientRect` 路径）；★ 但这是**结构性约束、非运行时断言** ⇒ 记为「设计假设」而非「已证明保证」。
///      若日后出现无窗口/离屏路径（`roadmap #44` 的 M-1），该契约须随之重估。

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
| `DrawRoundedRectOpaqueAA` | `:1416-1419`（x/y/w/h **四个局部变量**） | ★★ **v1.4 补（评审 P1-2）**：本函数**同时要边界与尺寸** ⇒ `const PixelBox box = ToPixelBox(rect);` 再 `const LONG x = box.left, y = box.top, w = box.width, h = box.height;`。★ **必须保留 `x/y/w/h` 这四个名字**——后续三条带与四角用了 `x + w`、`y + h - R`、`x + w - R`（`:1424-1428`、`:1447-1452`），只换成 `box.width` 而不建局部量会让那些表达式失去 `w` |
| `WriteRoundedRectPremultiplied` | `:350-351` | `const PixelBox box = ToPixelBox(rect);`（配合 △1-c） |
| `DrawLine`（bbox） | `:836-839`（`minX..maxY`） | `QuantizeEdge(minX)` 等 |
| `DrawFocusRect`（float 版） | `:1204-1207`、`:1214-1215`（`lround`） | `QuantizeEdge`（配合 △1-d 的常量语义） |
| `DrawFocusRect`（bbox 版） | `:1321-1322`（`- 2`） | `QuantizeEdge(rect.x) - 2` |
| `BlendAlphaSolid` | `:218`（`lround(rect.x/y)`） | `QuantizeEdge` |
| `PushClip` | `:1162-1165`（`lround`） | `QuantizeEdge`（★ D4：裁剪与绘制同源） |

#### △1-c 矩形尺寸级（**16 处**）——**删除独立取整，改由边相减**（★ v1.3：原记 17 处，其中 1 处是线宽，见 §2.1-e）

| 函数 | 现行 | 目标 |
|---|---|---|
| `DrawRect`（backdrop 分支） | `:597-598` `left + static_cast<LONG>(width)` | `ToPixelRect`（一并对齐） |
| `DrawRoundedRect` | `:957-958`（w/h）、`:976-979`（bbox） | `const PixelBox box = ToPixelBox(rect);` → 尺寸 `box.width/height`；边界 `box.left/top` 与 `box.left + box.width` / `box.top + box.height`。★ **w/h 与四条边同源** |
| `DrawRoundedRectLegacy` | **`:1023-1024`**（`w`/`h` = 尺寸） | `const PixelBox box = ToPixelBox(rect);` → `w = box.width`、`h = box.height`（★ `clampedRadius` 的 `min(w, h) / 2` 依赖它们，`:1025-1026`）。★ **v1.3 订正**：原把 `:1053-1054` 也列在此格，但那两行是**边**（`rect.x + rect.width` 形态）⇒ 归 **△1-b**，不在本表 |
| `DrawRoundedRectOpaqueAA` | `:1418-1419` | 取自上方同一个 `box`（`box.width`/`box.height`）——★ 与 △1-b 的 `x/y/w/h` **同源**，不得重新量化 |
| `WriteRoundedRectPremultiplied` | `:350-353` | `const PixelBox box = ToPixelBox(rect);` → 放置用 `box.left/top`、**逐像素循环上界**用 `box.width/height`，**同一来源** |
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


### 2.2 ★★ 逐表达式清单（v1.3 新增——**唯一计数来源**，评审 P1-1）

> ★★ **本节存在的理由**：前几版正文与汇总表多次互相矛盾（17/14/16/18、21/22、51/50），根因是**汇总数字靠手算**。本节由 `.workbuddy/spike/p34/expr_list.py` 从 `survey.json` **机器导出**（62 条，落 `expr_list.json`），**正文所有处数都必须能由它复算**。

**分类与处置汇总（可由下行清单逐条数出）**：

| 角色 | 处数 | 处置 |
|---|---|---|
| 边 | **34** | **改**（→ `QuantizeEdge`） |
| 矩形尺寸 | **16** | **改**（→ 边相减） |
| 非矩形尺寸 | **1** | 不改（`penWidth`，N4） |
| 线宽/半径 | **3** | 不改（N4） |
| 端点 | **8** | 不改（D6） |
| **合计** | **62** | **要改 50** = 34 + 16 ★ **复算式：要改 50 = 边 34 + 矩形尺寸 16** |

**实施单元覆盖（要改的 50 处）**：U1 **32**（边 20 + 矩形尺寸 12）· U2 **4**（边）· U3 **4**（边 2 + 矩形尺寸 2）· U4 **4**（边 2 + 矩形尺寸 2）· U5 **6**（边）· 合计 **50** ✅

**逐条清单**（ID 由脚本生成，行号为 2026-10-17 基线；实施时以「函数 + 表达式形态」为准）：

| ID | 行 | 函数 | 角色 | 取整法 | 处置 | 单元 |
|---|---|---|---|---|---|---|
| E01 | `L50` | `BlendAlphaSolid` | 矩形尺寸 | `lround` | 是 | U3 |
| E02 | `L51` | `BlendAlphaSolid` | 矩形尺寸 | `lround` | 是 | U3 |
| E03 | `L120` | `BlendAlphaSolid` | 线宽/半径 | `lround` | 否（N4 保留） | U3 |
| E04 | `L218` | `BlendAlphaSolid` | 边 | `lround` | 是 | U3 |
| E05 | `L218` | `BlendAlphaSolid` | 边 | `lround` | 是 | U3 |
| E06 | `L350` | `WriteRoundedRectPremultiplied` | 边 | `trunc` | 是 | U1 |
| E07 | `L351` | `WriteRoundedRectPremultiplied` | 边 | `trunc` | 是 | U1 |
| E08 | `L352` | `WriteRoundedRectPremultiplied` | 矩形尺寸 | `trunc` | 是 | U1 |
| E09 | `L353` | `WriteRoundedRectPremultiplied` | 矩形尺寸 | `trunc` | 是 | U1 |
| E10 | `L595` | `DrawRect` | 边 | `trunc` | 是 | U1 |
| E11 | `L596` | `DrawRect` | 边 | `trunc` | 是 | U1 |
| E12 | `L597` | `DrawRect` | 矩形尺寸 | `trunc` | 是 | U1 |
| E13 | `L598` | `DrawRect` | 矩形尺寸 | `trunc` | 是 | U1 |
| E14 | `L631` | `DrawRect` | 边 | `trunc` | 是 | U1 |
| E15 | `L632` | `DrawRect` | 边 | `trunc` | 是 | U1 |
| E16 | `L633` | `DrawRect` | 边 | `trunc` | 是 | U1 |
| E17 | `L634` | `DrawRect` | 边 | `trunc` | 是 | U1 |
| E18 | `L681` | `DrawText` | 端点 | `trunc` | 否（D6 排除） | 保留 |
| E19 | `L682` | `DrawText` | 端点 | `trunc` | 否（D6 排除） | 保留 |
| E20 | `L723` | `DrawTextImpl` | 端点 | `trunc` | 否（D6 排除） | 保留 |
| E21 | `L724` | `DrawTextImpl` | 端点 | `trunc` | 否（D6 排除） | 保留 |
| E22 | `L836` | `DrawLine` | 边 | `trunc` | 是 | U1 |
| E23 | `L837` | `DrawLine` | 边 | `trunc` | 是 | U1 |
| E24 | `L838` | `DrawLine` | 边 | `trunc` | 是 | U1 |
| E25 | `L839` | `DrawLine` | 边 | `trunc` | 是 | U1 |
| E26 | `L850` | `DrawLineLegacy` | 非矩形尺寸 | `lround` | 否（线宽 penWidth，N4） | 保留 |
| E27 | `L858` | `DrawLineLegacy` | 端点 | `lround` | 否（D6 排除） | 保留 |
| E28 | `L858` | `DrawLineLegacy` | 端点 | `lround` | 否（D6 排除） | 保留 |
| E29 | `L859` | `DrawLineLegacy` | 端点 | `lround` | 否（D6 排除） | 保留 |
| E30 | `L859` | `DrawLineLegacy` | 端点 | `lround` | 否（D6 排除） | 保留 |
| E31 | `L957` | `DrawRoundedRect` | 矩形尺寸 | `trunc` | 是 | U1 |
| E32 | `L958` | `DrawRoundedRect` | 矩形尺寸 | `trunc` | 是 | U1 |
| E33 | `L960` | `DrawRoundedRect` | 线宽/半径 | `lround` | 否（N4 保留） | U1 |
| E34 | `L976` | `DrawRoundedRect` | 边 | `trunc` | 是 | U1 |
| E35 | `L977` | `DrawRoundedRect` | 边 | `trunc` | 是 | U1 |
| E36 | `L978` | `DrawRoundedRect` | 矩形尺寸 | `trunc` | 是 | U1 |
| E37 | `L979` | `DrawRoundedRect` | 矩形尺寸 | `trunc` | 是 | U1 |
| E38 | `L1023` | `DrawRoundedRectLegacy` | 矩形尺寸 | `trunc` | 是 | U1 |
| E39 | `L1024` | `DrawRoundedRectLegacy` | 矩形尺寸 | `trunc` | 是 | U1 |
| E40 | `L1025` | `DrawRoundedRectLegacy` | 线宽/半径 | `lround` | 否（N4 保留） | U1 |
| E41 | `L1051` | `DrawRoundedRectLegacy` | 边 | `trunc` | 是 | U1 |
| E42 | `L1052` | `DrawRoundedRectLegacy` | 边 | `trunc` | 是 | U1 |
| E43 | `L1053` | `DrawRoundedRectLegacy` | 边 | `trunc` | 是 | U1 |
| E44 | `L1054` | `DrawRoundedRectLegacy` | 边 | `trunc` | 是 | U1 |
| E45 | `L1162` | `PushClip` | 边 | `lround` | 是 | U2 |
| E46 | `L1163` | `PushClip` | 边 | `lround` | 是 | U2 |
| E47 | `L1164` | `PushClip` | 边 | `lround` | 是 | U2 |
| E48 | `L1165` | `PushClip` | 边 | `lround` | 是 | U2 |
| E49 | `L1204` | `DrawFocusRect` | 边 | `lround` | 是 | U5 |
| E50 | `L1205` | `DrawFocusRect` | 边 | `lround` | 是 | U5 |
| E51 | `L1206` | `DrawFocusRect` | 边 | `lround` | 是 | U5 |
| E52 | `L1207` | `DrawFocusRect` | 边 | `lround` | 是 | U5 |
| E53 | `L1214` | `DrawFocusRect` | 边 | `lround` | 是 | U5 |
| E54 | `L1215` | `DrawFocusRect` | 边 | `lround` | 是 | U5 |
| E55 | `L1321` | `DrawFocusRect` | 边 | `trunc` | 是 | U4 |
| E56 | `L1322` | `DrawFocusRect` | 边 | `trunc` | 是 | U4 |
| E57 | `L1323` | `DrawFocusRect` | 矩形尺寸 | `trunc` | 是 | U4 |
| E58 | `L1324` | `DrawFocusRect` | 矩形尺寸 | `trunc` | 是 | U4 |
| E59 | `L1416` | `DrawRoundedRectOpaqueAA` | 边 | `trunc` | 是 | U1 |
| E60 | `L1417` | `DrawRoundedRectOpaqueAA` | 边 | `trunc` | 是 | U1 |
| E61 | `L1418` | `DrawRoundedRectOpaqueAA` | 矩形尺寸 | `trunc` | 是 | U1 |
| E62 | `L1419` | `DrawRoundedRectOpaqueAA` | 矩形尺寸 | `trunc` | 是 | U1 |

★ **验收口径（评审要求）**：每个待改表达式**恰好出现一次**；每个豁免表达式**有明确分类**；所有小计**能从清单复算**。以上三条已由 `expr_list.py` 的小计复算段落机器验证（62 = 34+16+1+3+8；要改 50 = 34+16；单元和 50）。

### △2 `src/Render/BackendContract.h`（**注释订正，零代码改动**）

`:84-89` 的「已知偏差」段现有表述与实测不符（职责确认 §9.4 已记）：说「多处用截断」但没提 **12 处 `lround`**（后者才是主要风险）。改为：

```cpp
///          ★★ **历史偏差（**Phase 34 计划消除**——★ v1.4：本稿为**设计阶段**，实施与回归完成后才可改为完成时态并落真实日期）**：本段曾登记「GDIBackend 多处边转换
///          仍用 static_cast<LONG>，仅在负分数边相差 1px」。实测（职责确认 §9.3）该描述有两处不准：
① 处数不是「多处」而是 **22 处边级截断**（★ v1.3 订正：原写 21；`survey.json` 复算 = 边级 trunc **22** · 全部 trunc **40**，见 §2.2 清单）；
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
`GDIBackend.cpp` +约 40 行（助手 2 个 + **50 处表达式改写** = 边 34 + 矩形尺寸 16；★ **口径**：「处」= **表达式**,不含注释与新增助手——三者分开计，见 §2.2）
| 用例 | **+N**（§5）——`QuantizeEdge` 真值表 + 等价变换对照 |
| CMake | **0** |
| ★ 像素输出 | **会变**（本阶段的目的）⇒ 差异须按职责确认 §4.6 三类逐条归因（A4/A7） |

---

## §5 测试方向

| # | 用例 | 判据 | 类型 |
|---|---|---|---|
| **T34-1**（v1.2 补具体值——评审 P2-6） | `QuantizeEdge` 真值表（**逐值断言，不靠读者构造**） | **输入 → 期望**：`1.25`→`1` · `1.75`→`1` · `-1.25`→`-2` · `-1.75`→`-2` · `0.5`→`0` · `-0.5`→`-1` · `2.0`→`2` · `-0.0`→`0` · `0.999`→`0` · `-0.001`→`-1` · `3.0f`→`3` · `-3.0f`→`-3`。★ **为什么必须列值**：「负数 frac 小于 0.5」这句话有歧义——指的是**有符号小数部分**还是**绝对值小数部分**？`-1.25` 的 frac 若按有符号看是 `-0.25`（<0.5），按绝对值看是 `0.25`（<0.5），两种读法结论恰好相同；但 `-1.75` 按有符号看 `-0.75`（<0.5，会误以为 floor = `-1`）、按绝对值看 `0.75`（≥0.5）——★ **两种读法给出相反的预期** ⇒ 只有列具体值才能消除歧义（这正是职责确认 条 119③ 的「判据的判别字段完备性」）。 | 纯函数，无窗口 |
| **T34-2**（v1.4 统一坐标空间——评审 P1-3） | **尺寸 = 边相减**（★ 职责确认 ② 类） | ★★ **`ToPixelBox` 只对传入的 `Rect` 量化，本身不做 DPI 缩放** ⇒ 本项的输入**一律是像素空间**（`ScaleRect` 之后），**不得**把 DIP 值与 DPI 档位混写进同一组期望。三组样本（**均为像素空间输入**）：① `x=1.25, w=2.75` ⇒ `left=1, width=3, right=4`（**不是** `1+2=3`）；② `x=1.5, w=1.5`（= 150% 下 DIP `x=1,w=1` 缩放后的值）⇒ `left=1, width=2, right=3`；③ `x=2, w=4`（整数）⇒ `left=2, width=4, right=6`（**与旧式相同**）。★ **反例提醒（评审指出）**：若把 DIP `x=1,w=1` **直接**传给 `ToPixelBox`，会得 `left=1, right=2, width=1`——**不是** 2 ⇒ 这就是为什么必须写明「像素空间」。★ 三组已实跑（`verify_pre11.py`）。 | 纯函数（★ 不需要窗口——这是把 ② 类**变成可断言**的关键） |
| **T34-3a**（v1.2 拆分——评审 P2-4） | `DrawFocusRect` **bbox 版**外扩**等价变换** | ★ **旧式完整写法**（v1.1 曾错写为 `LONG(x)-2+4`，**漏了宽度项**）：**`right = static_cast<LONG>(rect.x) - 2 + static_cast<LONG>(rect.width) + 4`**；**新式 = `QuantizeEdge(rect.x + rect.width) + 2`**。★ **三条判据**：① **输入为整数像素**时两式**逐值相等**；② **分数像素**时差异符合 ①② 归因；③ **左/上/右/下四条边**各自的外扩语义（左 −2 · 上 −2 · 右 +2 · 下 +2）。★ **术语**：「整数坐标」= **量化前的像素空间输入为整数**（`ScaleRect` 之后），不是「DIP 输入为整数」。★ **已实跑**：4/4 整数输入等价、4/4 分数输入方向正确、四边 8/18/42/62 全对（`verify_pre11.py`） | 纯函数 + 探针　★★ **v1.3 补具体样本（评审 P2-3）**——下表 `x/width` 均为**像素空间**（`ScaleRect` 之后），不是 DIP：`x=10,w=20`（旧 32 / 新 32，同）· `x=1.25,w=2.75`（旧 5 / 新 6，**②类**）· `x=1.5,w=1.5`（旧 4 / 新 5，**②类**）· `x=-1.5,w=3.0`（旧 4 / 新 3，**①类**）· `x=1.75,w=1.75`（旧 4 / 新 5，**②类**）· `x=2,w=4`（旧 8 / 新 8，**整数不变**）。★ 以上六组已**实跑**（`verify_pre11.py`），整数组等价、分数组方向与归因一致　★★ **v1.4 把「符合归因」变成可判定（评审 P2-3）**——每组分四步记录**旧式/新式的四条边**：① 打印 `left/top/right/bottom` 旧值与新值；② 标出**哪条边变**；③ 判定该边变化属于**①边取整方向**（左/上边因负分数变）还是 **②尺寸独立取整改边相减**（右边因宽度不再独立取整变）；④ **归不进 ①② 的差异 = 失败**。★ 输出须落文件作为审核证据，不靠人眼「看起来符合」 |
| **T34-3b**（v1.2 新增） | `DrawFocusRect` **float 版**端点语义回归（★ **不套 bbox 版的外扩变换**） | ① `fRight`/`fBottom` 的 **`-1` 必须仍在**（GDI 开区间换算，见 §2.1-d2）；② `gRight`/`gBottom` **不内缩**（几何边界，供弧心）；③ ★ **`gRight - fRight == 1`** 恒成立（若被人误改成同一值，弧心会内移 ⇒ 复现「焦点框内收」历史缺陷）；④ `fLeft`/`fTop` **不内缩**。 | 纯函数 + 目视 |
| **T34-4**（v1.2 补源码审计——评审 P2-5） | 裁剪与绘制同源（A2） | **纯函数层**：`PushClip` 与 `DrawRect` 对同一 `Rect` 得到**同一** `RECT`（直接调 `ToPixelRect`）。★★ **但纯函数测试只证明助手本身正确，不证明调用点真的用了它** ⇒ **另需源码审计验收**：① `DrawRect` 的实际绘制边界**来自** `ToPixelRect` 的结果；② `PushClip` 的实际裁剪边界**来自** `ToPixelRect`；③ **不存在旁路**的 `lround(rect.` / `static_cast<LONG>(rect.` 或独立尺寸取整（A1 静态扫描为辅助门槛）。★ 这是**审查项**，不新增测试基础设施 | 纯函数 + **源码审计**　★★ **v1.3 补：审计分两类（评审 P2-4）**——**①共享边界路径**：`DrawRect` 与 `PushClip` 的边界须来自 `ToPixelRect`；**②特殊消费者路径**：`BlendAlphaSolid` 须用 `ToPixelBox`（DIB 尺寸与放置同源）、`WriteRoundedRectPremultiplied` 的逐像素循环上界须取自 `box.width/height`、`DrawFocusRect` 两条路径各自的边界语义（bbox 版 `±2` 外扩 / float 版 `-1` 开区间）须与 §2.1-d / §2.1-d2 一致。★ 审计范围 = **实际改动面**（U1–U5 覆盖的 9 个函数），不是只有 2 个　★★ **v1.4 改为逐 ID 可勾选（评审 P2-2）**——§2.2 清单里**每个「处置 = 是」的 ID**（共 50 个）须逐条勾选五项：① 原表达式已移除或改写；② 新边界来自 `ToPixelRect`/`ToPixelBox`/`QuantizeEdge`；③ 尺寸不再独立取整；④ 豁免的 12 个 ID（端点 8 + 线宽半径 3 + penWidth 1）**仍保持原规则**且能指回 D6/N4；⑤ 特殊消费者未再次独立量化。★ 目的：防止「总数对了但某调用点漏改」或「豁免项被顺手改掉」 |
| **T34-5**（v1.1 补输入前提——评审 P2-5） | 100%/200% 零差异 | ★★ **前提（必须同时满足，否则本判据不成立）**：探针输入几何 = **整数 DIP 坐标**，且该 DPI 档的 scale 为整数（100% = 1.0 / 200% = 2.0）⇒ 缩放后仍为整数像素边。★ **为什么必须写前提**：职责确认 §9.1 的「零分歧率」来自**整数 DIP 样本**；若实际绘制链允许**分数 DIP 输入**（如 `x=0.6`），则 100% 下也会出现 ② 类差异 ⇒ **无条件的「逐字节零差异」与 §4.6 三类模型自相矛盾**。★ **两种执行方式（择一，须在详设定）**：① **整数 DIP 输入探针** ⇒ 断言**零差异**（验证的是「整数 DIP + 整数 scale 下三类均不触发」这一数学性质）；② **真实 UI 场景探针** ⇒ **不要求零差异**，改为「每处差异都能归入 §4.6 三类」（A7 的原因层）。 | 库外探针（两档 DPI）+ 目视（条 1：AI 不代跑） |

★ **T34-4 的诚实边界**：`RecordingBackend` 记录 `Rect`（浮点，未量化），所以**它无法验证后端的量化行为**。这不是本阶段的缺陷，而是「记录层在量化之前」的既有结构。⇒ 本设计把量化抽成**自由纯函数**正是为了让 A2/A3 可测（否则只能靠读代码）。

---

## §6 实施顺序（★ v1.4：**U1–U6 是唯一实施单位**；下方三批表**仅是风险分类**，不构成实施顺序）

| 批 | 内容 | 风险 | 回归 |
|---|---|---|---|
| **批一**（= **U1 + U4**，共 **36** 处） | △1-a（两个助手）+ **边 22 处截断**（U1 20 + **U4 2**）+ **矩形尺寸 14 处**（U1 12 + **U4 2**） | ①（仅负分数边，18.5%/12.3%/18.5%）+ ②（尺寸，37.0%/24.7%/37.0%） | 常规 + 负坐标专项 + **T34-2** |
| **批二**（= **U2 + U5**，共 **10** 处） | 边级 **10 处** `lround` → `QuantizeEdge`（`PushClip` 4 + `DrawFocusRect` float 版 6） | **③**（43.2%–49.8%） | ★ **独立目视回归 + 逐字节 diff** |
| **批三** | `BackendContract.h` 注释订正（△2）+ 未改处的「有意保留」注释（△1-e） | 零像素风险 | 文档核对 |

★★ **v1.4 关键澄清（评审 P1-1）**：上表是**风险分类**，**不是实施顺序**——**施工一律按 §6.2 的 U1–U6 逐单元进行**，因为只有 U 单元才保证「同函数内边与尺寸同批」。★ **三批与 U 的映射**：批一 = **U1 + U4**（36 = 32 + 4）· 批二 = **U2 + U5**（10 = 4 + 6）· **U3（`BlendAlphaSolid`，4 处）不属任何批**——它是唯一同时跨 ②③ 两类的单元，**必须单独一批**。★ **批一与批二不可合并**（分歧率差一个数量级）；**批一内部不可拆分**。★ 这正是 v1.3 漏掉 U4 那 2 处尺寸的根因：把批一当成了「U1 的执行清单」。

### 6.1 ★★ 批次重构：职责确认 D7 的「按取整法分批」在函数层面**不成立**（v1.1 订正，评审 P1-1）

★★ **评审指出的问题成立，且比评审说的更严重**。职责确认 D7 按**取整法**分三批：① 22 处截断边 → ② 17 处尺寸（★ **实测应为 16 处矩形尺寸**，见 §6.2 对账）→ ③ 12 处 `lround` 边。但实测的**函数-角色矩阵**显示：**没有一个函数能整块落进单一批次**。

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
| **③** | `DrawRoundedRect` / `Legacy` | 边截断（①）+ 尺寸截断（②）+ **线宽半径 `lround`（N4 不动）** | 若按「批二 = **全部矩形尺寸（实数 16 处）**执行，会把**线宽半径**也裹进去 ⇒ **越界**（★ 实数 16 处矩形尺寸 + 1 处线宽）|

★ **③ 正是评审说的「剩余 3 处尺寸何时改」**：「17 处尺寸」里 **3 处是 `lround`**、**其中 1 处是线宽**（⇒ 矩形尺寸实数 **16**）——`BlendAlphaSolid:50-51`（矩形尺寸）与 `DrawLineLegacy:850`（**线宽 `penWidth`**）。★ 后者**根本不是矩形尺寸**，按 N4 **不得改** ⇒ 「17 处尺寸全改」这句话本身是错的，**真实可改的是 14 处**。

#### 6.2 ★ 重构后的实施单元（**按函数分单元**，每个单元内变更同批提交、同批回归）

| 单元 | 函数 | 变更内容 | 差异类别（职责确认 §4.6） | 风险 |
|---|---|---|---|---|
| **U1** | `DrawRect` · `DrawLine` · `DrawRoundedRect` · `DrawRoundedRectLegacy` · `DrawRoundedRectOpaqueAA` · `WriteRoundedRectPremultiplied` | 边 20 处截断 → `QuantizeEdge` + **矩形尺寸 12 处** → 边相减（`DrawRect` 2 · `DrawRoundedRect` 4 · `Legacy` 2 · `OpaqueAA` 2 · `WriteRoundedRectPremultiplied` 2） | **① ∪ ②** | 中（② 正坐标也触发） |
| **U2** | `PushClip` | 边 4 处 `lround` → `QuantizeEdge` | **③** | 高（43.2%–49.8%） |
| **U3** | `BlendAlphaSolid` | 边 2 处 `lround` → `QuantizeEdge` **+** **矩形尺寸 2 处** `lround` → 边相减 | **② ∪ ③** | ★★ **最高**（两族叠加，且是唯一「DIB 尺寸 + 放置」分离的函数） |
| **U4** | `DrawFocusRect`（bbox 版 `:1321-1324`） | 边 2 处截断 → `QuantizeEdge` + 尺寸 2 处 → 边相减 + `±2` 常量 | **① ∪ ②** | 中 |
| **U5** | `DrawFocusRect`（float 版 `:1204-1215`） | 边 6 处 `lround` → `QuantizeEdge`（★ **`-1` 是开区间换算，不是外扩**，见 §2.1-d2） | **③** | 高 |
| **U6** | 注释与契约文档 | △1-e 保留说明 + △2 `BackendContract` 订正 | 零像素 | 无 |

★ **U1 与 U4 可合批**（同为 ①∪②）；**U3 必须单独**（唯一 ②∪③ 叠加）；**U5 必须单独**（同函数内还有 U4 的边，但 float 版与 bbox 版是**两条独立代码路径**，可分开验）。

★★ **计数对账（v1.2 新增——评审 P1-2 要求「以逐表达式清单为唯一计数来源」）**：

| 类别 | 处数 | 构成 | 出处 |
|---|---|---|---|
| **矩形尺寸**（本阶段改） | **16** | U1 **12** + U3 **2** + U4 **2** | `survey.json` 中 `role=尺寸` **且表达式含 `rect.`** |
| 非矩形尺寸（**不改**） | **1** | `DrawLineLegacy:850` 的 `width`（= `penWidth` 线宽） | 同上，表达式**不含** `rect.` |
| 线宽/半径（**不改**，N4） | **3** | `BlendAlphaSolid:120` `radius` · `DrawRoundedRect:960` `cornerRadius` · `DrawRoundedRectLegacy:1025` `cornerRadius` | `role=线宽/半径` |
| 端点（**不改**，D6） | **8** | `DrawText` 2 · `DrawTextImpl` 2 · `DrawLineLegacy` 4 | `role=端点` |
| **合计** | **28** = 尺寸17 + 线宽半径3 + 端点8 | — | 62 处中剩余 34 处为**边级** |

★ **v1.1 的两处数字错误**（如实记录）：§6.2 曾写「U1 14 处 + U3 2 处 + U4 2 处 = 18」，既**大于** 17、又与「可改 14 处」自相矛盾。根因 = 我在两处各自**手算**而没有回到 `survey.json` 逐条数。⇒ 本条起：**任何处数都必须能由 `recount.py` 复现**（`counts.json` 为机器输出）。
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

- **v1.4**（2026-10-17）**第五轮评审吸收——四项 P1 + 三项 P2**。① ★★★ **P1-1 批次表与 U 单元覆盖冲突（本轮核心）**：复评指出 §6 的「批一」写「22 处截断边 + 12 处矩形尺寸」，但 22 处截断边**含 U4 的 2 处**，而 12 处尺寸**不含 U4 的 2 处** ⇒ **U4 的尺寸被漏掉**，违反 §6 自己的「同函数内边与尺寸必须同批」。★ 根因：**把「批一」当成了「U1 的执行清单」**。已订正：批一 = **U1 + U4（36 = 32 + 4）**、批二 = **U2 + U5（10 = 4 + 6）**，**U3 不属任何批**（唯一跨 ②③ 两类，须单独）；并明确 **§6 三批表只是「风险分类」，施工一律按 U1–U6**。② ★★ **P1-2 AA 分支变量来源缺失**：△1-b 写 `const RECT bbox = ToPixelRect(rect)`，但 △1-c 要求从 `box.width/height` 取尺寸——**`RECT` 没有 `width` 成员**，且 `box` 不知从何而来。实核 `:1416-1419` 后订正：本函数需要 `PixelBox`，且**必须保留 `x/y/w/h` 四个局部名**——因为后续三条带与四角用了 `x + w`、`y + h - R`、`x + w - R`（`:1424-1428`/`:1447-1452`）。★ 并**同族核查**另外三个函数（`DrawRoundedRect` / `Legacy` / `WriteRoundedRectPremultiplied`）的变量来源，逐一写明「w/h 与四条边同源」。③ ★★ **P1-3 T34-2 坐标空间未统一**：复评指出「150% 的 `x=1,w=1` ⇒ width=2」**不成立**——`ToPixelBox` **不做 DPI 缩放**，直接传 DIP `x=1,w=1` 会得 `width=1`。已改为三组**像素空间**样本，并把 DIP↔像素的对应关系写成注释（`x=1.5,w=1.5` = 150% 下 DIP `1,1`），且**把反例写进判据**防止误读。④ ★ **P1-4 `int`→`LONG` 表述**：原写「是扩展，恒安全」——评审判定不准确：在 Windows ABI 上二者**同为 32 位**，故「可表示值相同」，但这是**平台相关事实**而非普遍类型规则。已改。⑤ ★ **P2-1 注释时态**：`BackendContract.h` 草案原写「Phase 34 **已消除**」，但本稿是设计阶段、代码未改 ⇒ 改为「**计划消除**」，并注明须待实施与回归完成后才可改完成时态。⑥ ★ **P2-2 审计改为逐 ID 可勾选**：§2.2 清单里 50 个「处置=是」的 ID 各勾五项，12 个豁免 ID 另需证明「仍保持原规则」——防止「总数对了但某调用点漏改」。⑦ ★ **P2-3 归因可判定**：T34-3a 由「差异符合 ①② 归因」细化为四步可执行判据（打印四边旧/新值 → 标出变的边 → 归属类别 → 归不进即失败），输出落文件作证据。
- **v1.3**（2026-10-17）**第四轮评审吸收——把「数字必须可由机器清单复现」贯彻到每一处**。① ★★★ **P1-1 逐表达式清单（本轮核心）**：复评指出 §2 的 △1-c 清单按行号逐组数出 **18 处**、与 §6.2 的 **16 处**冲突。实核**根因在正文**：`DrawRoundedRectLegacy` 那一格把 **`:1053-1054`（边，`rect.x + rect.width` 形态）**与 **`:1023-1024`（尺寸，`w`/`h`）** 混列在一起 ⇒ 按行号数会把边算进尺寸。已**剔除边**（归 △1-b），尺寸行只留 `:1023-1024`。★ 并按复评要求新增 **§2.2 逐表达式清单**（62 条、ID `E01`–`E62`，由 `expr_list.py` 从 `survey.json` **机器导出**，落 `expr_list.json`），含「行/函数/角色/取整法/处置/单元」六字段；**正文所有小计均可从它复算**：62 = 34+16+1+3+8，要改 50 = 34+16，单元和 U1 32+U2 4+U3 4+U4 4+U5 6 = 50。② ★★ **P1-2 `BackendContract` 注释的 21 → 22**：复评指出 △2 草案写「21 处边级截断」与本稿矩阵（U1 20 + U4 2 = 22）冲突。机器复算确认**边级 trunc = 22**、**全部 trunc = 40**（含尺寸 16 + 端点 2）⇒ **「合计 40」本就正确，错的只有 21**。已改为 22 并注明复算出处。③ ★★ **P1-3 数值条件改按实际运算类型**：复评正确指出「可表示于 `LONG`」不足以描述实际运算——`QuantizeEdge` 返回 **`int`**；`PixelBox::left/top` 是 `LONG`（`int`→`LONG` 扩展恒安全）；**`width/height` 是 `int` 而 `rc.right - rc.left` 先在 `LONG` 上算再窄化** ⇒ 须保证**差值本身**在 `int` 内；`DrawFocusRect` 的 `±2` **在 `int` 上运算** ⇒ `edge±2` 不得溢出。四类**逐项**列出。★ 并按要求把「由上游契约保证」**指到具体出处**（布局链 → `ScaleRect` → `Win32PlatformWindow` 的 `GetClientRect`），同时**如实降级**为「**结构性约束、非运行时断言** ⇒ 记为设计假设而非已证明保证」。④ ★ **P2-1 §1.2 补「非矩形尺寸 1 处」**：复评指出四类（34+16+8+3）合计 **61 ≠ 62**，缺的正是 `DrawLineLegacy:850` 的 `penWidth`——它既不是矩形尺寸、也不在那 3 处线宽/半径里。已补该行，合计回 62。⑤ ★ **P2-2 §4 的 51 → 50**：复评指出与 §1.2 的「要动 50 处」冲突。已改，并**明确口径**：「处」= **表达式**；助手 2 个与注释补充**分开计**，不混用同一术语。⑥ ★ **P2-3 T34-3a 具体样本**：补六组「像素空间」样本（`x/width` 均为 `ScaleRect` 之后的值），并注明整数组等价、分数组归因，避免实施者自行猜哪些差异算合理。⑦ ★ **P2-4 T34-4 审计分两类**：**共享边界路径**（`DrawRect`/`PushClip` 来自 `ToPixelRect`）与**特殊消费者路径**（`BlendAlphaSolid` 用 `ToPixelBox`、逐像素循环上界、焦点框两版语义）；审计范围 = **实际改动面 9 个函数**，不是只有 2 个。
- **v1.2**（2026-10-17）**第三轮评审六点吸收（3 必改 + 3 建议）**。① ★★★ **P1-1 `PixelBox` 缺字段（本版最硬的错）**：v1.1 把「前置条件」注释块**插进了结构体内部**，把 `int width; int height;` 两个字段**挤掉** ⇒ 初始化器给了 4 个值、结构体只声明 2 个成员 ⇒ **按原样无法编译**。已恢复完整四字段定义（`LONG left/top` + `int width/height`），并**用编译器实证**：从本稿抽出的 `ToPixelRect`/`ToPixelBox` 草案 `g++ -std=c++20` **编译通过**，`T34-1` 真值表 **12/12**、`T34-2` `left=1,width=3,right=4` **PASS**（`verify_compile.py`）。② ★★★ **P1-2 尺寸计数互斥（17 / 14 / 16 / 18 四个数打架）**：复评指出算术矛盾**成立**，且根因是我在两处各自**手算**、没有回到 `survey.json` 逐条数。机器复核（`recount.py` → `counts.json`）：`role=尺寸` 的 **17 条**里，`DrawLineLegacy:850` 的表达式是 `width`（= **`penWidth` 线宽**）而**不含 `rect.`** ⇒ **真实矩形尺寸 = 16 处**（U1 **12** + U3 **2** + U4 **2**，合计 **16** ✅）。⇒ 全文当前态统一为 **16**，并新增 **§6.2 计数对账表**（矩形尺寸 16 / 非矩形尺寸 1 / 线宽半径 3 / 端点 8，合计 28 + 边级 34 = 62）；§1.2 标题「要动的 51 处」随之订正为 **50 处**。★ **教训**：任何处数必须能由脚本复现，不许手算。③ ★★ **P1-3 数值边界覆盖不全**：v1.1 只约束输入，未覆盖中间值与最终结果。补 **6 条前置条件**：① 输入有限；② ★ **中间和有限**（两个有限浮点数相加仍可能溢出到 `±inf`，此时 `floor` 未指定）；③ 四条边可表示于 `LONG`；④ ★ **差值可表示于 `int`**（两边界各自可表示**不代表**差值可表示）；⑤ ★ **外扩后仍可表示**（`DrawFocusRect` 的 `±2`）；⑥ **不满足时的归属**——由既有上游契约保证（经 `ScaleRect`/布局链，量级受窗口约束），**本阶段不新增运行时钳制**（条 22 / 条 95⑤），但登记「若日后出现离屏路径（#44 M-1）须重估」。④ ★ **P2-4 T34-3 拆分**：拆为 **T34-3a**（bbox 版外扩等价性，三条判据）与 **T34-3b**（float 版端点语义回归：`-1` 仍在、`gRight` 不内缩、★ **`gRight - fRight == 1` 恒成立**——若被误改成同值就会复现「焦点框内收」历史缺陷）。⑤ ★ **P2-5 T34-4 补源码审计**：纯函数测试只证明助手正确、**不证明调用点用了它** ⇒ 增补审查项（`DrawRect`/`PushClip` 的边界**来自** `ToPixelRect`、**无旁路**的 `lround`/`static_cast<LONG>`/独立尺寸取整）。⑥ ★ **P2-6 T34-1 列具体值**：给出 12 个「输入 → 期望」，并说明**为什么必须列值**——「负数 frac<0.5」有歧义（有符号小数部分 vs 绝对值小数部分），`-1.75` 两种读法给出**相反预期**。
- **v1.1**（2026-10-17）**外部评审六点吸收（3 必改 + 3 建议）**。① ★★★ **P1-1 批次与 17 处尺寸的对应（本轮最实质订正）**：复评指出「§6 批一只列 22 处截断边 + 14 处尺寸，**剩余 3 处尺寸没交代**」，并指出 `BlendAlphaSolid` 的助手调用**已把不同批次耦合**。经实测**问题比复评说的更严重**：按**函数-角色矩阵**核对，**没有任何函数能整块落进 D7 的单个批次**——`BlendAlphaSolid`（边 `lround` + 尺寸 `lround` ⇒ ③∪②）、`DrawFocusRect`（**三批全占**：2 截断边 + 6 lround 边 + 2 截断尺寸）、`DrawRoundedRect`/`Legacy`（边① + 尺寸② + **线宽半径 lround（N4 不动）**）。★ 并**订正一处计数口径**：17 处尺寸中 **3 处是 `lround`**，其中 `DrawLineLegacy:850` 是**线宽 `penWidth`**、**根本不是矩形尺寸**，按 N4 **不得改** ⇒ **真实可改的尺寸是 14 处**，「17 处尺寸全改」这句话本身是错的。⇒ **新增 §6.1（冲突矩阵）+ §6.2（重构为 U1–U6 六个「函数级实施单元」）**：按**函数**分单元而非按取整法分批，每个单元内边与尺寸**同批提交**；**U3（`BlendAlphaSolid`）单独一批**（唯一 ②∪③ 叠加）、**U5（float 版）单独一批**。② ★★ **P1-2 T34-3 旧式补全**：原写 `LONG(x)-2+4`，**漏了宽度项**⇒ 测试会验错对象。改为完整式 **`LONG(x) - 2 + LONG(width) + 4`** vs 新式 **`QuantizeEdge(x+w) + 2`**，并列出**三条必验**（整数输入等价 / 分数输入符合 ①② 归因 / 四条边各自的外扩语义）；★ 补**术语澄清**：「整数坐标」= **量化前的像素空间输入为整数**（`ScaleRect` 之后），不是「DIP 输入为整数」。③ ★★ **P1-3 O4 关闭**：复评指「本稿已给出替换方案，却保留可能推翻它的条件」——成立。**分两条路径界定**：**bbox 版**的 `±2/+4` = 固定像素外扩（已核，按本稿处理）；**float 版**的 `-1` = **GDI 开区间换算**（`:1202-1203` 原文可证），**与外扩无关** ⇒ **只做 `lround`→`QuantizeEdge` 的方向替换，`-1` 原样保留，不得套 bbox 版的 `+2`**。新增 **§2.1-d2** 逐变量列出四个取整结果的语义（`fLeft/fTop` 起点 · `fRight/fBottom` 终点含 `-1` · `gRight/gBottom` 几何边界无 `-1`），并写明**只改 `fRight` 不改 `gRight` 会复现「焦点框内收」历史缺陷**。④ ★ **P2-4 助手去重**：`ToPixelBox` 改为**复用** `ToPixelRect` 的结果再相减，不再重算同一组边（消除两份实现漂移的可能，同时保留 `PixelBox` 类型的语义价值）。⑤ ★ **P2-5 T34-5 输入前提**：补「**整数 DIP 坐标 + 整数 scale**」这一前提，并指出**无条件零差异与 §4.6 三类模型自相矛盾**（若允许分数 DIP 输入，100% 下也会出 ② 类差异）；给出两种执行方式（整数 DIP 探针断言零差异 / 真实场景探针改用 A7 原因层）。⑥ ★ **P2-6 有效尺寸与数值边界**：`PixelBox` 补**前置条件**（分量有限、`quantize` 后可表示；**不要求** `width/height > 0`，但消费者须先判空；**不得**假定调用方传正常矩形——`CreateDIBSection` 的 `biWidth` 与逐像素循环上界直接吃这两个值）。⑦ ★ **附：一次自证伪**——本稿 §5 曾把 T34-3 的原式简写为 `LONG(x)-2+4`，是**我在设计稿里引入的错误**（与职责确认 §4.6 已核的完整式不符），靠复评指出后修正；已在 T34-3 里写明「此前写法是错的」。
- **v1.0**（2026-10-17）**初稿**。上游 = 职责确认 **v1.4 ✅ 已冻结**（四轮外部评审，2026-10-17）。① **§1 基线全部带行号实测**，并**修正职责确认 §10 的一处口径**：`PushClip`（`:1162-1165`）实为 `lround(x+w)` 形态（**尺寸未独立取整**）⇒ 它只有 ③ 类（取整方向）问题、与 D2 无关 ⇒ **可独立成批**。② **§1.3 给出「必须按函数整体处理」的三条实测理由**，其中 `BlendAlphaSolid` 是 ② 类的**真实危害样本**：`lround(width)` 定 DIB 尺寸、`lround(rect.x)` 定放置 ⇒ `left + width ≠ right` ⇒ 相邻控件**漏缝或压边 1px**。③ **§2 逐文件最小修改面**（条 42）：新增**两个文件内助手** `ToPixelRect` / `ToPixelBox`，后者用独立类型把「★ **尺寸 = 边相减**」这条契约**在类型层面可见**（教学型框架，条 95③）；`△1-b`/`△1-c` 逐函数列出替换目标；`△1-d` 把 `DrawFocusRect` 的 `±2/+4` 落成「**先量化边、再套常量**」并说明为何不能写 `QuantizeEdge(rect.x - 2)`（外扩是设备像素语义）。④ **§2 △1-e**：端点（D6）与线宽半径（N4）**不动但须加注释**——没有注释的「没改」看起来就是「漏改」。⑤ **§5 测试方向**：★ 诚实登记 **`RecordingBackend` 无法验证后端量化**（它记录的是**未量化**的 `Rect`）⇒ 本设计把量化抽成**自由纯函数**正是为了让 A2/A3 **可测**；`T34-2` 已**实跑验证**（真实 g++ 编译执行），五组样本全部符合 §4.6 三类归因、`left + width == right` **0 失败**。⑥ **§6 实施顺序**：批一（边截断 + 同函数尺寸）· 批二（`lround` 边级，独立目视回归）· 批三（注释与契约文档）；★ 批一与批二**不可合并**（分歧率差一个数量级），批一**内部不可拆分**（同函数边与尺寸必须同批）。⑦ **§7 开放项**：职责确认的 O1–O3 已关闭；新增 **O4**（`+4` 等价性待 T34-3 验证）与 **O5**（已核，`ForceRectAlphaFrosted` 与 `FillRect` 必须同源）。
