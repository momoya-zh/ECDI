# Phase 34 后端几何取整口径统一 —— 详细设计

> **状态**：**✅ 已收口（2026-10-19，用户裁定 Phase 34 结束）**——生产实现、9 条自动化测试、详设实施回填、设计文档复评均已完成；更广范围的视觉回归（§5 **T34-5** 要求的 100%/200% 逐字节专项探针——属**审计/探针**类，不进自动化用例）按用户裁定**不在本阶段闭环内**，留待后续轮次承接。
> ★★ **v1.2 仅追加实施回填与修订记录**：`ToPixelRect` / `ToPixelBox` 已用于指定 GDI 矩形消费者；新增 9 条量化测试并登记；MSVC Debug 全套 **474/474**。另有真实窗口 `Renderer → GDIBackend` 的不透明 `DrawRect` 整数 DIP 探针，在 scale 1/2 下与旧截断几何参考帧逐像素零差异；此代表性样本不等于所有消费者或应用场景的视觉验收。
> **输入**：职责确认 `phase34-backend-quantization-requirements.md` **v1.4 ✅ 已冻结**（2026-10-17，四轮外部评审）· 初步设计 `phase34-backend-quantization-preliminary-design.md` **v1.15 待评审**（2026-10-17，十五轮评审）
> **模板**：沿 Phase 33 详设的十一节形态（同族最近先例：既有实现的口径收敛 + 两后端之一 + 零公共头改动）
> **定位**：把初设的决策落成**逐函数、逐表达式的实现规格**与**精确断言**；契约不重开，只在实现层补足初设未定的形态细节与数值。

---

## 1. 代码基线（B1–B10，全部 2026-10-17 带行号实测）

> ★ **行号会漂**（条 114）：本节的行号是 **2026-10-17 的基线**，实施时以**函数名 + 表达式形态**为准。
> 全部实测装置 = `.workbuddy/spike/p34/`（`expr_list.py` 生成 74 条清单 · `audit_source.py` 做双向核验）。
> ★ **复核结果（2026-10-17，详设动笔前重跑）**：74 条清单的「行号 + 表达式 + 取整法 + 函数归属」**四者 74/74 全部命中**，0 处不一致。

### 1.1 规范本体（**本阶段零改动**）

| # | 位置 | 内容 | 本阶段 |
|---|---|---|---|
| **B1** | `src/Render/BackendContract.h:90-93` | `QuantizeEdge(edge)` = `static_cast<int>(std::floor(edge))` | **不动** |
| **B2** | `src/Render/BackendContract.h:101-104` | `SnapVertex(v)` = `static_cast<float>(QuantizeEdge(v))`（GL 侧用） | **不动** |
| **B3** | `src/Render/BackendContract.h:77-81` | 契约原文：「边取 `floor`；尺寸由量化后的两条边相减导出，绝不独立取整」 | **不动** |
| **B4** | `src/Render/BackendContract.h:84-89` | 「已知偏差」段（本阶段要订正的**注释**） | **改注释**（△2） |

### 1.2 ★ 实施单元 × 清单行号区间（B 表，供逐函数定位）

| 单元 | 函数 | 清单条目 | 行号（2026-10-17 基线） | 条数 |
|---|---|---|---|---|
| **U1** | `WriteRoundedRectPremultiplied` | E06–E09 | `:350,351,352,353` | 4 |
| | `DrawRect` | E10–E17 | `:595,596,597,598,631,632,633,634` | 8 |
| | `DrawLine` | E22–E25 | `:836,837,838,839` | 4 |
| | `DrawRoundedRect` | E31–E37 | `:957,958,960,976,977,978,979` | 7 |
| | `DrawRoundedRectLegacy` | E38–E44 | `:1023,1024,1025,1051,1052,1053,1054` | 7 |
| | `DrawRoundedRectOpaqueAA` | E71–E74 | `:1416,1417,1418,1419` | 4 |
| **U2** | `PushClip` | E53–E56 | `:1162,1163,1164,1165` | 4 |
| **U3** | `BlendAlphaSolid` | E01–E05 | `:50,51,120,218` | 5 |
| **U4** | `DrawFocusRect`（bbox 版） | E67–E70 | `:1321,1322,1323,1324` | 4 |
| **U5** | `DrawFocusRect`（float 版） | E57–E66 | `:1204,1205,1206,1207,1214,1215,1303,1304` | 10 |
| **U7** | `DrawImage` | E45–E52 | `:1129,1130,1143,1144,1145,1146` | 8 |
| **U6** | 注释与契约文档 | — | `BackendContract.h:84-89` + 各函数「有意保留」注释 | 0 表达式 |

★ **U6 不计表达式**（它是注释与文档），但**必须与 U1–U5/U7 同批交付**——否则「没改」看起来就是「漏改」。

### 1.3 逐表达式清单（**唯一计数来源**，74 条）

> ★★ **本节不重抄 74 行**——初设 §2.2 已逐条列出，且其与 `expr_list.json` 的**逐字段一致性已机检**（74 行、0 处不符）。
> 详设只固定**口径**与**复算方式**，避免同一份事实两处维护（条 117 的反向用法：**登记处唯一**）。

**计数口径（可复算）**：

| 角色 | 处数 | 本阶段 | 单元分布 |
|---|---|---|---|
| 边 | **38** | **改**（→ `QuantizeEdge`） | U1 20 · U2 4 · U3 2 · U4 2 · U5 6 · U7 4 |
| 矩形尺寸 | **20** | **改形态**（→ 边相减） | U1 12 · U3 2 · U4 2 · U7 4 |
| 非矩形尺寸 | **1** | **不动**（N4，`penWidth`） | — |
| 线宽/半径 | **3** | **不动**（N4） | — |
| 端点 | **12** | **不动**（D6） | — |
| **合计** | **74** | **要改 58** = 38 + 20；**豁免 16** = 1 + 3 + 12 | 单元和 **58** = 32+4+4+4+6+8 |

★ **复算命令**：`python .workbuddy/spike/p34/expr_list.py` —— 它从 `survey.json`（初扫 62 条）+ 内联补丁表（12 条）**一次生成 74 条**，落盘后**回读断言**，再验 5 条等式（74 = 38+20+1+3+12 · 要改 58 · 豁免 16 · 单元 58 · U1 内部 20+12）。

### 1.4 ★★ 初设 §1.4 的一处判断需订正（本稿实测推翻，如实登记）

**初设 §1.4 原文**：

> `BackendContract` 的 `QuantizeEdge` 是纯函数，但**当前无用例直接断言它的四象限行为**。

**实测结论：该断言部分不成立。**

| 项 | 实测（2026-10-17，`ECDI/src/Tests/BackendContractTests.cpp`） |
|---|---|
| 现有量化用例数 | **5 个**：`QuantizeEdgePositiveMatchesFloor` · `QuantizeEdgeNegativeUsesFloor` · `SnapVertexMirrorsQuantizeEdge` · `QuantizeEdgeIntervalConsistent` · `QuantizeEdgeWidthNeverIndepRounded` |
| 调用次数 | `QuantizeEdge` **18 次**，不同输入值 **17 个** |
| 已覆盖 | 负数分数 5 个（`-100.25/-1.5/-0.9/-0.5/-0.1`）· 正数分数 7 个（`0.4/0.5/0.9/1.5/10.5/11.5/19.99`） |
| **缺的 9 个值** | `1.25` · `1.75` · `-1.25` · `-1.75` · `2.0` · `0.999` · `-0.001` · `3.0` · `-3.0` |

★★ **更关键的不是「有没有用例」，而是「期望值的判别力」**——逐条判定：

| 用例 | 实际目的 | 能否区分 floor / trunc / lround |
|---|---|---|
| `PositiveMatchesFloor` | M17 批的**回归锚**（GL 的 `Snap` 改为调共享件，正数行为须一字不变） | 含 `1.5 → 1`，**能**区分 lround（lround 会给 2）；与 trunc 在正数域**同值**，区分不了 trunc |
| `NegativeUsesFloor` | 证明负数用 floor 而非向零截断 | `-0.5 → -1` **能**区分 trunc（trunc 给 0）；但 floor 与 lround 在负数域**同值**，区分不了 lround |
| `SnapVertexMirrorsQuantizeEdge` | 助手间**转发关系** | **不能**（不是口径用例） |
| `IntervalConsistent` | 「尺寸 = 边相减」**不变量** | **不能**（测不变量，非口径） |
| `WidthNeverIndepRounded` | 「独立取整宽度」的反例对照 | 输入全为正，**不能**区分 trunc |

⇒ ★★ **T34-1 仍然必要，但理由必须改写**：不是「无用例」，而是
「**现有用例各自服务于 M17 的回归锚与不变量，没有任何一条构成覆盖三种口径的四象限判别真值表**」。
★ 具体缺口：**同一组输入下同时给出 floor / trunc / lround 三个期望值**，让「选错口径」当场变红。
初设 T34-1 的 12 个值里缺 9 个，且**没有一条把三种口径的期望值并列**——本稿 §5 的 T34-1 按此重写。

★ **这一条的意义**：初设把一个「有 5 个用例」的事实写成了「无用例」。若详设照抄，实施者会以为要**从零建**量化测试，而实际是**补判别力**——两者的用例设计与工作量都不同。

---

## 2. 逐文件改动（△1–△3；**改动 1 个 .cpp + 1 个 .h 的注释** · 新建 0 · CMake 0）

### △1 `src/Render/GDIBackend.cpp`（唯一生产代码改动文件）

#### △1-a 两个文件内助手（**新增**，落点已实测）

**落点**：`GDIBackend.cpp:21` 的 `namespace{` 之后、`L23` 的 `kTextPrefixMinWchars` 之前。

> ★ 条 43（顶层声明必须写明落点）：`ToPixelRect`/`ToPixelBox` 是**文件内**符号，必须落在
> **匿名 namespace 内**（`namespace{` 在 `L21`，实测确认）；放错到 `namespace ECDI` 顶层会变成外部链接符号。

```cpp
/// @brief **普通矩形边界量化**的唯一实现
/// @details 契约 = `BackendContract.h:77-81`（边取 floor、尺寸由边相减）。**普通矩形**的
///          `Rect → RECT` 一律走它。
///          ★★ **选择规则**：① 只需要边界 ⇒ `ToPixelRect(rect)`；
///          ② 同时需要边界与宽高 ⇒ **只调** `ToPixelBox(rect)`，右边/下边由
///          `box.left + box.width` / `box.top + box.height` **导出**；
///          ③ **同一消费者不得同时声明两者**——`ToPixelBox` 内部已复用 `ToPixelRect`，
///          再单独调一次会产生**两份量化结果来源**，正是本阶段要消灭的形态；
///          ④ 带特殊几何变换者（焦点框外扩）**先用本函数取基础边界、再套常量**，
///          **不得**绕开本函数直接对 `Rect` 调 `QuantizeEdge`。
///          ★ 为什么不做成成员：`BlendAlphaSolid` 等在匿名 namespace（自由函数），
///          且它无状态 ⇒ 自由函数最省（不引入新抽象，条 22）。
inline RECT ToPixelRect(const Rect& rect) noexcept
{
    const int left   = BackendContract::QuantizeEdge(rect.x);
    const int top    = BackendContract::QuantizeEdge(rect.y);
    const int right  = BackendContract::QuantizeEdge(rect.x + rect.width);
    const int bottom = BackendContract::QuantizeEdge(rect.y + rect.height);
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

inline PixelBox ToPixelBox(const Rect& rect) noexcept
{
	const RECT rc = ToPixelRect(rect);   // ★ **复用**同一条边计算，不重复实现
	return PixelBox{ rc.left, rc.top,
	                 static_cast<int>(rc.right - rc.left),    // ★ 相减，不是取整
	                 static_cast<int>(rc.bottom - rc.top) };
}
```

★ **命名空间限定**：`BackendContract::QuantizeEdge` 必须**全限定**——`GDIBackend.cpp` 在
`namespace ECDI` 内（`L19`），但**没有** `using namespace BackendContract`（实测确认），
不限定会编译失败。

★ **前置条件（整数域）**：`PixelBox::width/height` 取自 `rc.right - rc.left`，该减法**先在 `LONG` 上做**
再窄化到 `int` ⇒ 须保证**差值本身**在 `int` 范围内（两端各自在 `int` 内**不代表**差值在）。
本阶段**不新增运行时钳制**（条 22 / 条 95⑤），由上游 `ScaleRect` 与布局链保证（记为**设计假设**，非已证明的保证）。

#### △1-b 边级（**38 处**）——替换为 `QuantizeEdge`

| 函数 | 清单 ID | 现行（基线行号） | 目标 | 备注 |
|---|---|---|---|---|
| `DrawRect`（backdrop 分支） | E10–E11 | `:595-596` 逐字段 `static_cast<LONG>` | `const RECT rc = ToPixelRect(rect);` | ★ 该 `rc` 同时喂 `FillRect` 与 `ForceRectAlphaFrosted`（`:614`）⇒ 天然同源 |
| `DrawRect`（主路径） | E14–E17 | `:631-634`（形态已对，方向错） | 同上 | ★ 注意 `:631-634` 是**四个边**（`right`/`bottom` 用 `x+w`），不是尺寸 |
| `DrawRoundedRect` | E34–E35 | `:976-977` | **只调** `const PixelBox box = ToPixelBox(rect);`，边界取 `box.left/top`，右边/下边由 `box.left + box.width` / `box.top + box.height` 导出 | ★ 本函数同时要边界与尺寸 ⇒ 单来源 |
| `DrawRoundedRectLegacy` | E41–E44 | `:1051-1054` | 同上（`box` 由 △1-c 的同一份提供） | ★ **不得**再单独声明 `RECT rc` |
| `DrawRoundedRectOpaqueAA` | E71–E72 | `:1416-1417` | `const PixelBox box = ToPixelBox(rect);` 再 `const LONG x = box.left, y = box.top;` | ★ 见 △1-b2 |
| `WriteRoundedRectPremultiplied` | E06–E07 | `:350-351` | **只调** `const PixelBox box = ToPixelBox(rect);`，放置用 `box.left/top` | ★ 循环上界用 `box.width/height`（△1-c） |
| `DrawLine`（bbox） | E22–E25 | `:836-839` | `bbox.left = static_cast<LONG>(BackendContract::QuantizeEdge(minX));` 等四条 | ★ 输入是 `minX..maxY` 四个 **float 局部量**，不是 `Rect` ⇒ 直接调 `QuantizeEdge` |
| `DrawFocusRect`（float 版） | E57–E62 | `:1204-1207`、`:1214-1215` | `QuantizeEdge`（**配合 △1-d2 的常量语义**） | ★ 六处，**不是**四处的「外扩」 |
| `DrawFocusRect`（bbox 版） | E67–E68 | `:1321-1322` | ★ **指向 △1-d 的正式实现**（基础边界经 `ToPixelRect` 取得，再套 `-2`） | ★ 本表**不给**独立目标式——那会形成绕过统一入口的第二条路径 |
| `BlendAlphaSolid` | E04–E05 | `:218` | `QuantizeEdge`（放置位置） | ★ 与 `:50-51` 的 DIB 尺寸**同源**（△1-c） |
| `PushClip` | E53–E56 | `:1162-1165` | `const RECT rc = ToPixelRect(rect);` | ★ D4：裁剪与绘制同源 |
| `DrawImage` | E45–E46、E49–E50 | `:1129`、`:1143-1144` | **只调** `const PixelBox box = ToPixelBox(dest);` | ★ **两处消费共用同一个 `box`**（见 △1-b3） |

##### △1-b2 ★ `DrawRoundedRectOpaqueAA` 的变量名约束（**不可简化**）

现行 `:1416-1419` 是**四个局部变量** `x/y/w/h`，后续代码大量依赖它们：

- `:1424-1428` 三条带用 `x + w`、`y + h - R`、`x + w - R`
- `:1447-1452` 四角用 `x + w - R`、`y + h - R`

⇒ **必须保留 `x/y/w/h` 四个名字**：

```cpp
const PixelBox box = ToPixelBox(rect);
const LONG x = box.left, y = box.top, w = box.width, h = box.height;
```

★ **只把 `box.width` 内联进那些表达式**会让代码难以阅读，且**改动面扩大**（违反条 42 最小修改面）。

##### △1-b3 ★ `DrawImage` 的两处消费必须共用同一份量化结果

现行 `DrawImage` 把同一个 `dest` **消费两次**：

| 消费点 | 行 | 用途 |
|---|---|---|
| `AlphaBlend` | `:1129-1130` | 图像拉伸的目标位置与尺寸 |
| `FixupSentinelFrosted` | `:1143-1146` | 哨兵修补的矩形 |

★★ **`destRect.right = destRect.left + std::lround(dest.width)`（`:1145`）是本阶段要消灭的「独立尺寸取整」教科书形态。**
**它使 `left + width ≠ right`，哨兵修补会与图像错位 1px。**

**目标式**：

```cpp
const PixelBox box = ToPixelBox(dest);   // ★ 全函数唯一量化来源
// …
AlphaBlend(m_memoryDC,
           static_cast<int>(box.left), static_cast<int>(box.top),
           box.width, box.height,
           dibDC, 0, 0, width, height, blend);
// …
if (m_backdropActual != BackdropMode::None)
{
    RECT destRect{};
    destRect.left   = box.left;
    destRect.top    = box.top;
    destRect.right  = box.left + box.width;    // ★ 导出，不是 left + lround(width)
    destRect.bottom = box.top  + box.height;
    FixupSentinelFrosted(destRect);
}
```

★ **`AlphaBlend` 的形参类型**：`x`/`y` 是 `int`，`PixelBox::left/top` 是 `LONG`。
在本项目目标 Windows ABI 上 `LONG` 与 `int` 同为 32 位有符号 ⇒ **`static_cast<int>` 不损失**，
但**必须显式写**（不依赖隐式转换，也不依赖「值域相同」这一平台相关事实）。

#### △1-c 矩形尺寸级（**20 处**）——**删除独立取整，改由边相减**

| 函数 | 清单 ID | 现行（基线行号） | 目标 |
|---|---|---|---|
| `DrawRect`（backdrop 分支） | E12–E13 | `:597-598` `rc.left + static_cast<LONG>(rect.width)` | `ToPixelRect`（与边**同一份**） |
| `DrawRoundedRect` | E31–E32、E36–E37 | `:957-958`（w/h）、`:978-979`（bbox 右边/下边） | `box.width/height`；边界由 `box.left + box.width` 导出 |
| `DrawRoundedRectLegacy` | E38–E39 | `:1023-1024`（w/h = 尺寸） | `w = box.width`、`h = box.height`（★ `clampedRadius` 的 `min(w,h)/2` 依赖它们，`:1025-1026`） |
| `DrawRoundedRectOpaqueAA` | E73–E74 | `:1418-1419` | `w = box.width`、`h = box.height`（与 △1-b2 同源） |
| `WriteRoundedRectPremultiplied` | E08–E09 | `:352-353` | `box.width/height`（**逐像素循环上界**与放置**同一来源**） |
| `BlendAlphaSolid` | E01–E02 | `:50-51` | `box.width/height`（**DIB 尺寸**）与 `box.left/top`（**放置**）同源 |
| `DrawFocusRect`（bbox 版） | E69–E70 | `:1323-1324` | 尺寸侧**不再独立取整**，右边/下边由 `base.right/bottom` 加常量导出（见 △1-d） |
| `DrawImage` | E47–E48、E51–E52 | `:1130`、`:1145-1146` | `box.width/height`（见 △1-b3） |

★ **为什么「尺寸」不能只是换成 `QuantizeEdge(rect.width)`**：`QuantizeEdge` 返回的是**边**的量化，
而尺寸必须是**两条边之差**。`floor(x + w) - floor(x) ≠ floor(w)` 在分数输入下**普遍不成立**
（例：`x = 1.25, w = 2.75` ⇒ `floor(4.0) - floor(1.25) = 4 - 1 = 3`，而 `floor(2.75) = 2`）。
⇒ 尺寸**只能**由 `box` 导出，这就是 `PixelBox` 存在的全部理由。

#### △1-d `DrawFocusRect` 的 **bbox 版**（`:1321-1324`）——`±2 / +4` 常量语义

**现状**：

```cpp
bbox.left   = static_cast<LONG>(rect.x) - 2;
bbox.top    = static_cast<LONG>(rect.y) - 2;
bbox.right  = bbox.left + static_cast<LONG>(rect.width) + 4;
bbox.bottom = bbox.top  + static_cast<LONG>(rect.height) + 4;
```

**语义**（已核）：焦点框在控件**四周各外扩 2 设备像素**。

**目标式**（★ **顺序不可换**）：

```cpp
const RECT base = ToPixelRect(rect);          // ★ 先走统一入口
const LONG left   = base.left   - 2;          // ★ 局部量用 LONG，与 RECT 成员同类型
const LONG top    = base.top    - 2;
const LONG right  = base.right  + 2;
const LONG bottom = base.bottom + 2;
```

★ **为什么不能写 `QuantizeEdge(rect.x - 2)`**：`-2/+2` 是**设备像素**语义（与外扩像素数有关，
与 DIP 无关）。先减后量化 ⇒ 在分数 DPI 下 `rect.x - 2` 的 frac 被 floor 吸收或进位，
外扩量变成 1 或 3 px。

★ **为什么 `+4` 变 `+2`**：原式是 `bbox.left`（= `x - 2`）**再** `+ 4` ⇒ 净效应 `x + 2`。
等价改写为「右边 + 2」后，`+4` 自然消失。★ 这是**等价变换**（整数输入下），须在用例里用原式对照验证（T34-3a）。

★ **`LONG` 而非 `int`**：`base.left` 是 `LONG`（`RECT` 成员），`base.left - 2` 按 `LONG` 运算。
局部量声明为 `LONG` 可**彻底消除** `LONG → int` 窄化，让类型与运算类型一致。

#### △1-d2 `DrawFocusRect` 的 **float 版**（`:1204-1215`）——**两套独立语义，不可互套**

★★ **最容易被误套的地方**：本函数有**四个**取整结果，语义各不相同：

| 变量 | 现行表达式 | 语义 | 本阶段处置 |
|---|---|---|---|
| `fLeft` / `fTop` | `static_cast<float>(std::lround(rect.x))` / `(rect.y)` | **可见像素起点**（不内缩） | → `QuantizeEdge` |
| `fRight` / `fBottom` | `static_cast<float>(std::lround(rect.x + rect.width) - 1)` | ★ **可见像素终点**——`-1` 是 **GDI 开区间换算**（`:1202-1203` 原文） | `QuantizeEdge(x+w)` 再 **`- 1`**（**保留**） |
| `gRight` / `gBottom` | `static_cast<float>(std::lround(rect.x + rect.width))` | ★ **几何边界**（供弧心/半径，**不内缩**） | `QuantizeEdge(x+w)`（**无 `-1`**） |

★★ **两条铁律**：

1. **`-1` 与焦点框的 `±2` 外扩无关**——前者是**开区间换算**（所有手绘线段都要做），
   后者是**控件外扩装饰**。混为一谈会把焦点框整体缩进 1px。
2. **`fRight` 与 `gRight` 都必须改**，且**改后仍相差 1**：若只改 `fRight` 不改 `gRight`，
   弧心会基于「内缩后的可见边界」计算 ⇒ 复现 `:1209-1213` 记录的**历史缺陷**
   （「四角弧心各内移 1px ⇒ 焦点框内收」）。

★ **`:1303-1304` 的折线端点（E63–E66）不在此列**：它们是**点**（`px/py/x/y`），
不是边，按 **D6 排除**，**保持 `lround` 原样**（只加注释，见 △1-e）。

#### △1-e 豁免处（**16 处不动**，但**必须加注释**说明「有意保留」）

| 位置 | 清单 ID | 处置 |
|---|---|---|
| `DrawText:681-682` | E18–E19 | 保留 `static_cast<LONG>`；注释指向 **D6** |
| `DrawTextImpl:723-724` | E20–E21 | 同上 |
| `DrawLineLegacy:858-859` | E27–E30 | 保留 `lround`；同上（**4 处**：`start.x/start.y/end.x/end.y`） |
| `DrawFocusRect:1303-1304` | E63–E66 | 保留 `lround`；同上（**4 处**：`px/py/x/y`，折线端点） |
| `DrawLineLegacy:850` | E26 | 保留 `lround`；注释指向 **N4**（`penWidth` 线宽，**非矩形尺寸**） |
| `BlendAlphaSolid:120` | E03 | 保留 `lround`；同上（`radius`） |
| `DrawRoundedRect:960` | E33 | 保留 `lround`；同上（`cornerRadius`） |
| `DrawRoundedRectLegacy:1025` | E40 | 保留 `lround`；同上（`cornerRadius`） |

★ **注释是契约的一部分**：A1 的白名单要求「未改的处必须能指回理由」。
**没有注释的「没改」看起来就是「漏改」**——这正是 §2.2 清单里「豁免项」需要 16 条逐一可勾选的原因。

### △2 `src/Render/BackendContract.h`（**注释订正，零代码改动**）

`:84-89` 的「已知偏差」段现有表述与实测不符。**逐句对照**：

| 行 | 现文 | 问题 |
|---|---|---|
| `:84-85` | 「`GDIBackend` 的**多处**边转换仍用 `static_cast<LONG>`」 | 「多处」→ 应为 **22 处边级截断** |
| `:85-86` | 「与本节规则**仅在「负的分数边」上相差 1px**」 | ★ **不完整**：还有 **16 处边级 `std::lround`**，它在**正数 frac ≥ 0.5** 时也与 `floor` 相差 1px |
| `:86-89` | 「**本批不改 GDI 的这四处**…偏差已登记入 `roadmap-deferred.md` #62 行余量」 | ★ 「四处」是**过时口径**（实测 22 截断 + 16 `lround`）；且本阶段**就是要改它** |

**目标注释**（★ 时态必须是「计划消除」——本稿是设计阶段，代码未改）：

```cpp
///          ★★ **历史偏差（Phase 34 计划消除——本稿为设计阶段；实施与回归完成后才可改为完成时态并落真实日期）**：
///          本段曾登记「GDIBackend 多处边转换仍用 static_cast<LONG>，仅在负分数边相差 1px」。实测该描述有两处不准：
///          ① 处数不是「多处」而是 **22 处边级截断**（`survey.json` 复算 = 边级 trunc 22 · 全部 trunc 40）；
///          ② **完全未提**另有 **16 处边级 std::lround**——而 `lround` 与 `floor` 在
///             **职责确认 §9.2 所记录的实测 DPI 档位与输入样本中**，分歧率为 **43.2%–49.8%**
///             （★ 该区间是本次测量结果，不代表所有非整数 DPI 或任意输入分布；
///             统计口径见该节：DIP 整数 0..80 × 五个 scale，含负坐标偏移，共 12960 样本/档）。
///             ★ 它相对截断组（仅负坐标分歧）**高出一个数量级**，这个**量级对比**才是本条要传达的结论。
///          ⇒ Phase 34 起：边级统一 `QuantizeEdge`、尺寸由边相减、端点（D6）与线宽半径（N4）显式保留。
```

★ **「22」与「16」的口径**：22 = 边级**截断**（U1 20 + U4 2）；16 = 边级 **`lround`**（U2 4 + U3 2 + U5 6 + U7 4）。
两者合计 38 = 边级总数，与 §1.3 一致。

### △3 无其它生产代码改动

`GLRenderer.cpp` 已用 `SnapVertex`（**无需动**）· 公共头**零改动**（96 → 96）· CMake **零改动**（`GLOB_RECURSE` 自动收源）。

---

## 3. 实现期必须解决的三处（初设未定，本稿定）

### E34-1 ★★ `DrawLine` 的 bbox 输入**不是 `Rect`**，无法用 `ToPixelRect`

**初设的缺口**：初设 §2.1-b 把 `DrawLine` 的 bbox 与其它函数并列，暗示都走 `ToPixelRect`。
**实测（`:830-839`）**：`DrawLine` 的 bbox 来自**四个独立的 float 局部量**：

```cpp
const float margin = std::clamp(width, 1.0f, 64.0f) + 2.0f;
const float minX = (std::min)(start.x, end.x) - margin;
// … minY / maxX / maxY
```

它们**不构成 `Rect`**（是「两端点 ± margin」的外接框），⇒ **不能**调 `ToPixelRect`。

**本稿定**：`DrawLine` 的四处**直接调 `BackendContract::QuantizeEdge`**：

```cpp
bbox.left   = static_cast<LONG>(BackendContract::QuantizeEdge(minX));
bbox.top    = static_cast<LONG>(BackendContract::QuantizeEdge(minY));
bbox.right  = static_cast<LONG>(BackendContract::QuantizeEdge(maxX));
bbox.bottom = static_cast<LONG>(BackendContract::QuantizeEdge(maxY));
```

★ **这是 `ToPixelRect` 选择规则的唯一合法例外**，必须在代码注释里写明理由
（否则审计会把「直接调 `QuantizeEdge`」当成绕开统一入口，见 T34-4 第 ⑤ 项）。

### E34-2 ★ `BlendAlphaSolid` 是**自由函数**，`ToPixelBox` 必须在它之前定义

**实测**：`BlendAlphaSolid` 在 `:41`（匿名 namespace 内），而 `namespace{` 在 `:21`。
⇒ `ToPixelRect`/`ToPixelBox` 必须插在 **`:21` 与 `:41` 之间**（本稿定 `:22-23` 处，即 `namespace{` 之后、
`kTextPrefixMinWchars` 之前），否则 `BlendAlphaSolid` 看不到它们。

★ **不能**把助手放到文件末尾——C++ 要求**声明先于使用**（匿名 namespace 内无前向声明机制）。

### E34-3 ★ `DrawFocusRect` 的 **bbox 版与 float 版在同一函数内**

**实测**：`DrawFocusRect` 从 `:1186` 起，float 版在 `:1204-1215`，bbox 版在 `:1321-1324`
——**同一个函数体的两端**。

⇒ **U4 与 U5 是同一个函数的两段**，但**分属不同单元**（U4 = bbox 版，U5 = float 版）。
★ **它们可以分开验**（两条独立代码路径、语义无关），但**必须同一次提交**——
因为 `DrawFocusRect` 只有一个函数体，分两次提交会让「函数处于半改状态」。

★ 这与初设 §6.2 的「U5 必须单独」不冲突：**单独**指**回归验证**单独，不是**提交**单独。

---

## 4. 盯防清单（**可机检**，每条给判据）

| # | 盯防项 | 判据（机检命令/形态） |
|---|---|---|
| **①** | 边级不得残留 `static_cast<LONG>(rect.` / `static_cast<LONG>(dest.` | `grep -n 'static_cast<LONG>(\(rect\|dest\)\.' GDIBackend.cpp` ⇒ **0 命中**（E10–E17/E41–E44/E71–E74 等全部消失） |
| **②** | 边级不得残留 `lround(rect.` / `lround(dest.` | 同上正则换成 `lround` ⇒ **0 命中**（E04/E45–E56/E57–E62 消失；★ `lround(pos.` / `lround(start.` / `lround(end.` / `lround(px` 等**豁免项必须仍在**） |
| **③** | 尺寸不得残留独立取整 | `grep -n 'lround(rect\.\(width\|height\))\|static_cast<LONG>(rect\.\(width\|height\))\|lround(dest\.\(width\|height\))'` ⇒ **0 命中** |
| **④** | `ToPixelBox` 内部必须复用 `ToPixelRect` | 读 `ToPixelBox` 函数体：**必须**含 `const RECT rc = ToPixelRect(rect);`；**不得**出现第二份 `QuantizeEdge` 调用 |
| **⑤** | 同一消费者不得同时声明 `RECT` 与 `PixelBox` | 对每个改动函数：**不得**同时出现 `ToPixelRect(` 与 `ToPixelBox(` 两次调用（`ToPixelBox` 内部那次不算） |
| **⑥** | `DrawFocusRect` bbox 版必须经 `ToPixelRect` | 该函数体内 `:1321-1324` 段**不得**出现 `QuantizeEdge(rect.` |
| **⑦** | `DrawFocusRect` float 版的 `-1` 必须仍在 | `:1206-1207` 目标式**必须**形如 `QuantizeEdge(rect.x + rect.width) - 1` |
| **⑧** | `DrawLine` 的例外必须带注释 | `:836-839` 附近**必须**有注释说明「bbox 不是 Rect，故直接调 QuantizeEdge」 |
| **⑨** | 豁免 16 处必须逐一有注释 | 对 E18–E21/E26/E27–E30/E33/E40/E63–E66 各查一次：该行或紧邻上方**必须有** `D6` 或 `N4` 字样 |
| **⑩** | `QuantizeEdge` 必须全限定 | `grep -n 'QuantizeEdge' GDIBackend.cpp` ⇒ 每处都是 `BackendContract::QuantizeEdge` |

★ **①–③ 是「改到位」判据，④–⑩ 是「没走歪」判据**——两类缺一不可：
只查 ①–③ 会漏掉「用了助手但同时又单独量化一次」（初设 §2.1-a 规则③ 明确禁止的形态）。

---

## 5. 用例正文（T34-1..T34-8）

> ★ **测试文件**：新建 `ECDI/src/Tests/BackendQuantizationTests.cpp`（**新建 1 个测试文件**），
> 在 `RunAllTests.h` 声明 + `RunAllTests.cpp` 注册（**两处手工**，非构建系统职责）。
> ★ **CMake 零改动**：`GLOB_RECURSE ... CONFIGURE_DEPENDS` 自动收源。
> ★ **数量**：本阶段 **+9 条**（T34-1a/1b/1c · 2 · 3a · 3b · 4 拆 2 条 · 5）= 需在详设 §7 核算。

### T34-1 ★★ `QuantizeEdge` 四象限**判别**真值表（**三条**，按口径分组）

★★ **本节是 §1.4 的直接产物**：现有用例**不能**区分三种口径，故必须**并列三个期望值**。

**T34-1a · 正数域：`floor` vs `lround` 的分歧（`frac ≥ 0.5`）**

| 输入 | `floor`（**规范**） | `lround` | `trunc` | 判别 |
|---|---|---|---|---|
| `1.5` | **1** | 2 | 1 | ★ 区分 lround |
| `1.75` | **1** | 2 | 1 | ★ 区分 lround |
| `2.5` | **2** | 3 | 2 | ★ 区分 lround |
| `0.5` | **0** | 1 | 0 | ★ 区分 lround（`lround` 远离零 ⇒ 1） |
| `0.999` | **0** | 1 | 0 | ★ 区分 lround |

**T34-1b · 负数域：`floor` vs `trunc` 的分歧（`frac ≠ 0`）**

| 输入 | `floor`（**规范**） | `lround` | `trunc` | 判别 |
|---|---|---|---|---|
| `-0.001` | **-1** | 0 | 0 | ★ 区分 trunc **与** lround |
| `-1.25` | **-2** | -1 | -1 | ★ 区分 trunc **与** lround |
| `-1.75` | **-2** | -2 | -1 | ★ 区分 trunc |
| `-0.5` | **-1** | -1 | 0 | ★ 区分 trunc |

**T34-1c · 整数域与边界（三口径必须一致）**

| 输入 | 三口径一致值 | 用途 |
|---|---|---|
| `0.0` | **0** | 零点 |
| `-0.0` | **0** | ★ 负零（`floor(-0.0) == -0.0` ⇒ 0） |
| `2.0` / `-3.0` | **2** / **-3** | 整边界不偏 |
| `3.0f`（float 字面量） | **3** | 类型一致性 |

★★ **判据强度**：`-0.001 → -1` 与 `1.5 → 1` 是**两条最有判别力**的样本——
前者能同时否掉 `trunc` 与 `lround`，后者能否掉 `lround`。若实施时误用 `static_cast<int>`，
`-0.001` 会给 `0`；误用 `lround`，`1.5` 会给 `2`。**两者都在编译期无警告**，只有这条用例能抓。

### T34-2 ★★ 尺寸 = 边相减（**像素空间**输入；★ 判别力已穷举验证）

★★ **`ToPixelBox` 只对传入的 `Rect` 量化，本身不做 DPI 缩放** ⇒ 输入**一律是像素空间**（`ScaleRect` 之后）。

★★★ **判别条件（本稿穷举确定，2026-10-17）**：

```
「边相减」  = floor(x + w) - floor(x)
「独立取整」= lround(w)
判别点 ⇔ 边相减 != 独立取整
```

★ **穷举结论**（`x, w ∈ [0, 4]`，步长 `0.25`，共 **272 组**）：
**判别点仅 72 组（26.5%）**。⇒ **大部分样本没有判别力**，用例必须**刻意挑选**。

★★ **最干净的判别形态 = `x` 取整数**（此时 `floor(x) = x`，判别条件化简为 `floor(w) != lround(w)`
⇔ **`w` 的小数部分 ≥ 0.5**）。`x` 为整数时穷举得 **40 个判别点**，覆盖全部整数 `x`。

| # | 输入（像素空间） | 边相减 | 独立 `lround(w)` | 期望 `left / width / right` | 判别力 |
|---|---|---|---|---|---|
| ① | `x=0.0, w=1.5` | `floor(1.5)-0 = 1` | **2** | `0 / 1 / 1` | ★★ **判别** |
| ② | `x=0.0, w=0.5` | `floor(0.5)-0 = 0` | **1** | `0 / 0 / 0` | ★★ **判别**（零宽边界） |
| ③ | `x=3.0, w=2.5` | `floor(5.5)-3 = 2` | **3** | `3 / 2 / 5` | ★★ **判别**（非零偏移） |
| ④ | `x=1.25, w=2.75` | `floor(4.0)-1 = 3` | 3 | `1 / 3 / 4` | 同值（**验证没算错**） |
| ⑤ | `x=2.0, w=4.0`（整数） | `6-2 = 4` | 4 | `2 / 4 / 6` | 同值（**零回归**样本） |

★★ **①②③ 是本用例的核心**：只有它们能证明「用了边相减」而不是「用了独立取整」。
★ **④⑤ 保留但降级**——它们验证「结果没算错」「整数输入零回归」，**不能**证明口径正确。

★★ **初设 T34-2 的三组样本全部落在「同值」类**（`x=1.25,w=2.75` · `x=1.5,w=1.5` · `x=2,w=4`）
⇒ **无法区分「边相减」与「独立 `lround`」**。本稿据此重写，并把判别条件写成可复算的式子。

★ **反例提醒**：若把 DIP `x=1,w=1` **直接**传给 `ToPixelBox`，会得 `left=1, right=2, width=1`
——**不是** 2 ⇒ 这就是为什么必须写明「像素空间」。

★ **判据强度自检**：若实施时误写成 `box.width = lround(rect.width)`，则 ①②③ **全部变红**
（① 期望 1 实得 2 · ② 期望 0 实得 1 · ③ 期望 2 实得 3）；而 ④⑤ **仍然全绿**
⇒ **这正是「判别样本」与「对照样本」必须分开的意义**。

### T34-3a ★ `DrawFocusRect` **bbox 版**外扩等价变换

| 项 | 内容 |
|---|---|
| **旧式** | `right = static_cast<LONG>(rect.x) - 2 + static_cast<LONG>(rect.width) + 4` |
| **新式** | `QuantizeEdge(rect.x + rect.width) + 2` |
| **等价前提** | **整数输入**（`rect.x`/`rect.width` 均为整数，`ScaleRect` 之后） |

**归因规则（按「发生变化的表达式与其原始运算路径」判，不是按「哪条边」判）**：

- **① 取整方向差异** = 旧式向零截断与新式 `floor` 对**同一操作数**给出不同结果
- **② 独立尺寸取整差异** = 旧式分别量化位置与尺寸、新式先量化两条边再求差

**实测样本** `x=-1.5, w=3.0`：旧 `left=-3 right=4`、新 `left=-4 right=3`，**左右都变**；
成因 `TR(x)=-1` vs `QE(x)=-2` ⇒ **①类**；右边之差由 `TR(x)+TR(w)+2=4` vs `QE(x+w)+2=3` 产生，
**同样源于 x 的取整方向**，不是独立尺寸取整。

★ **反例判决力**：若把类别简单绑到「哪条边」，这个样本就会被误判成 ②类。

**验证范围**：本测试分成两项，不能混为一项：① 对 `rect.x`/`rect.width` 均为整数且新旧中间运算可表示的样本，验证上述等价关系；② 对负分数等会改变边界的样本，验证变化可按取整方向差异归因。② 是差异归因测试，不是新旧等价测试；不得把整数输入的等价结论外推到分数输入。

**四步**：① 打印四条边旧/新值；② 标出变的边；③ 按上述两类的**运算路径**归属；④ **归不进 ①② = 失败**。
★ 输出须**落文件**作审核证据，不靠人眼「看起来符合」。

### T34-3b `DrawFocusRect` **float 版**端点语义回归（★ **不套 bbox 版的外扩变换**）

**输入约束**：本用例只在边界量化结果及其 `-1` 均处于 `float` 可精确表示范围内时验证浮点变量关系（可用 `|q| ≤ 2^24` 且 `q > INT_MIN` 作为保守约束，其中 `q` 为 `QuantizeEdge` 结果）。超出范围时，`int → float` 可能丢失整数精度，不能要求浮点变量之差仍精确为 1；这不改变生产代码的整数域契约。

| # | 判据 |
|---|---|
| ① | `fRight`/`fBottom` 的 **`-1` 必须仍在**（GDI 开区间换算） |
| ② | `gRight`/`gBottom` **不内缩**（几何边界，供弧心） |
| ③ | ★ 在上述输入约束下，**`gRight - fRight == 1`**（若被误改成同值，弧心内移 ⇒ 复现「焦点框内收」历史缺陷） |
| ④ | `fLeft`/`fTop` **不内缩** |

### T34-4 ★★ 源码审计（**表 A 58 条 + 表 B 16 条**）

**纯函数层**：`PushClip` 与 `DrawRect` 对同一 `Rect` 得到**同一** `RECT`。
★★ **但纯函数测试只证明助手本身正确，不证明调用点真的用了它** ⇒ **另需源码审计验收**。

**表 A · 修改项 58 条**（§2.2 里每个「处置 = 是」的 ID），每条查四项：

| # | 检查项 |
|---|---|
| ⓐ | 原表达式已移除或改写 |
| ⓑ | 新边界来自 `ToPixelRect` / `ToPixelBox` / `QuantizeEdge` |
| ⓒ | 尺寸不再独立取整 |
| ⓓ | 所属函数的特殊语义未被破坏 |

**表 B · 豁免项 16 条**（端点 12 + 线宽半径 3 + `penWidth` 1），每条查两项：

| # | 检查项 |
|---|---|
| ⓐ | 保留理由能指回 **D6**（端点）或 **N4**（线宽半径 + `penWidth`） |
| ⓑ | 原行为**未被改变**（逐 ID 对照改动前的表达式） |

**专项**：

- **U7（`DrawImage`）**：① `AlphaBlend` 位置与尺寸**同出 `PixelBox`**；② `FixupSentinelFrosted` 的
  `destRect` **四边同源**；③ **不得再有** `lround(dest.width/height)`；④ 哨兵修补行为未被破坏
- **`DrawFocusRect` 两版**：bbox 版验 `±2` 外扩（T34-3a）· float 版验 `-1` 仍在且 `gRight - fRight == 1`（T34-3b）
- **不得绕开统一入口**：检查函数体内是否有对 `rect.x/y/width/height` 或 `dest.*` 的**直接** `QuantizeEdge` 调用
  （★ **唯一例外 = `DrawLine` 的 bbox**，见 E34-1，且必须带注释）
- **通用断言**：`box.left + box.width == right` 且 `box.top + box.height == bottom`

### T34-5 100%/200% 零差异（**前提必须同时满足**）

★★ **前提**：探针输入几何 = **整数 DIP 坐标**，且该 DPI 档的 scale 为整数（100% = 1.0 / 200% = 2.0）
⇒ 缩放后仍为整数像素边。

★ **为什么必须写前提**：职责确认 §9.1 的「零分歧率」来自**整数 DIP 样本**；
若实际绘制链允许**分数 DIP 输入**（如 `x=0.6`），则 100% 下也会出现 ② 类差异
⇒ **无条件的「逐字节零差异」与 §4.6 三类模型自相矛盾**。

★ **两种执行方式（择一，本稿定 ①）**：
① **整数 DIP 输入探针** ⇒ 断言**零差异**（验证「整数 DIP + 整数 scale 下三类均不触发」这一数学性质）；
② 真实 UI 场景探针 ⇒ **不要求零差异**，改为「每处差异都能归入 §4.6 三类」。

★ **范围说明**：本稿选择方式①，因此该零差异结论**只适用于整数 DIP 输入与整数 scale**，不证明分数 DIP 输入下零差异，也不替代真实 UI 的分数几何回归。若实际绘制路径允许分数 DIP 输入，应另执行方式②；其差异按 §4.6 三类逐项归因，不得把“整数探针零差异”外推为全部场景零差异。

### T34-6 ★ `ToPixelBox` 的内部复用（**防双来源**）

**判据**：`ToPixelBox` 函数体**必须**含 `const RECT rc = ToPixelRect(rect);`，且**不得**出现第二份
`QuantizeEdge` 调用。★ 这是**结构判据**（读代码即可判），不是行为判据——
若改成「自己再算一遍」，行为可能仍然正确，但**双来源**的隐患立刻成立。

### T34-7 ★ 边界值：`INT_MIN` 附近

**背景**：`DrawFocusRect` float 版的 `-1`（两处）若量化结果为 `INT_MIN`，则执行 `-1` 会发生**有符号整数溢出，属于 C++ 未定义行为**（数学真值 `-2147483649` 不可表示）。特定编译器/构建中观察到的回绕结果不构成语言保证；基于结果大小的朴素溢出检查也不可靠。

**判据**：本用例**不得执行** `QuantizeEdge(rect.x + rect.width) - 1` 且量化结果为 `INT_MIN` 的路径；有符号整数溢出在 C++ 中是**未定义行为**，不可依赖其回绕结果，也不可断言“会回绕但不崩溃”。
用例应验证 `QuantizeEdge` 本身在输入可表示且结果恰为 `INT_MIN` 时返回 `INT_MIN`，并以静态检查确认 `-1` 前置条件（量化结果必须大于 `INT_MIN`）已记录。该路径列为**已知局限 L34-1**：本阶段不新增运行时钳制，但必须明确它是**不受支持的输入条件**，不是已定义行为。
★ 该值**在真实 UI 中不可达**（受窗口尺寸约束）的判断仍是设计假设；若该假设失效，应重新评估是否增加有定义的边界处理。

### T34-8 ★ 豁免项未被顺手改掉

**判据**：对 16 个豁免 ID 逐一**对照改动前的表达式**，断言**逐字符相同**
（`lround(pos.x)` 仍是 `lround(pos.x)`，不是 `QuantizeEdge(pos.x)`）。

---

## 6. 实施顺序（**U1–U7 是唯一实施单位**；三批表**仅是风险分类**）

### 6.1 施工顺序（**按 U 单元**，每个单元内变更同批提交、同批回归）

| 序 | 单元 | 函数 | 要改 | 差异类别 | 风险 |
|---|---|---|---|---|---|
| 1 | **U1** | `DrawRect` · `DrawLine` · `DrawRoundedRect` · `DrawRoundedRectLegacy` · `DrawRoundedRectOpaqueAA` · `WriteRoundedRectPremultiplied` | **32**（边 20 + 尺寸 12） | ① ∪ ② | 中（② 正坐标也触发） |
| 2 | **U4** | `DrawFocusRect`（bbox 版） | **4**（边 2 + 尺寸 2） | ① ∪ ② | 中 |
| 3 | **U2** | `PushClip` | **4**（边） | ③ | 高（43.2%–49.8%） |
| 4 | **U5** | `DrawFocusRect`（float 版） | **6**（边） | ③ | 高 |
| 5 | **U3** | `BlendAlphaSolid` | **4**（边 2 + 尺寸 2） | ② ∪ ③ | ★★ **最高** |
| 6 | **U7** | `DrawImage` | **8**（边 4 + 尺寸 4） | ② ∪ ③ | ★ 高 |
| 7 | **U6** | 注释与契约文档（△1-e + △2） | 0 | 零像素 | 无 |

★ **U1 与 U4 可合批**（同为 ①∪②）；**U3 必须单独**（唯一 ②∪③ 叠加）；**U5 必须单独验**（③ 类分歧率最高）。
★★ **U4 与 U5 是同一函数的两段**（E34-3）⇒ **必须同一次提交**，但**可分开验**。

### 6.2 风险分类（**不是施工顺序**）

| 批 | 内容 | 风险 | 回归 |
|---|---|---|---|
| **批一**（= U1 + U4，共 **36** 处） | 助手 + 边 22 处截断 + 矩形尺寸 14 处 | ①（仅负分数边）+ ②（尺寸，正坐标也触发） | 常规 + 负坐标专项 + **T34-2** |
| **批二**（= U2 + U5，共 **10** 处） | 边级 10 处 `lround` → `QuantizeEdge` | **③**（43.2%–49.8%） | ★ **独立目视回归 + 逐字节 diff** |
| **批三**（= U6） | `BackendContract.h` 注释 + 未改处注释 | 零像素 | 文档核对 |
| ★ **不属任何批** | **U3**（4 处）· **U7**（8 处） | ②∪③ 叠加 | 各自**独立回归** |

★ **批一与批二不可合并**（分歧率差一个数量级）；**批一内部不可拆分**（同函数内边与尺寸必须同批）。

---

## 7. 影响面（实施目标）

| 维度 | 影响 |
|---|---|
| 生产代码文件 | **1 个**：`src/Render/GDIBackend.cpp` |
| 注释文件 | **1 个**：`src/Render/BackendContract.h`（**零代码改动**） |
| 新建测试文件 | **1 个**：`src/Tests/BackendQuantizationTests.cpp` |
| 手工登记 | **2 处**：`RunAllTests.h` 声明 + `RunAllTests.cpp` 注册 |
| 公共 API | **+0** |
| 公共头 | **96 → 96** |
| CMake | **0**（`GLOB_RECURSE ... CONFIGURE_DEPENDS` 自动收源） |
| 表达式改写 | **58 处** = 边 38 + 矩形尺寸 20（★ 口径：「处」= **表达式**，不含注释与新增助手） |
| 豁免 | **16 处** = 端点 12 + 线宽半径 3 + `penWidth` 1 |
| 自动化用例 | **+9 条**（T34-1a/1b/1c · 2 · 3a · 3b · 6 · 7 · 8）★ T34-4/T34-5 属审计与探针，不进自动化 |
| ★ 像素输出 | **会变**（本阶段的目的）⇒ 差异须按职责确认 §4.6 三类逐条归因 |

★ **用例数核算**：当前 `ecdi_tests` 注册 **42 个 `Register*Tests()`** 调用（实测）。
本阶段新增 **1 个注册函数** `RegisterBackendQuantizationTests()`，其内部 `Add` **9 条**。
⇒ 总数 = 现有 + 9。★ **具体基线数须在实施前实测**（`Test summary:` 末行），**不得推算**。

---

## 8. 开放项收口（O34-1..O34-4）

| # | 项 | 状态 |
|---|---|---|
| **O34-1** | `DrawLine` 的 bbox 不构成 `Rect` ⇒ 无法走 `ToPixelRect` | ✅ **本稿定（E34-1）**：直接调 `QuantizeEdge`，并加注释说明理由 |
| **O34-2** | `ToPixelRect` 的落点 | ✅ **本稿定（E34-2）**：`namespace{`（`:21`）之后、`BlendAlphaSolid`（`:41`）之前 |
| **O34-3** | `DrawFocusRect` bbox 版与 float 版同函数、分属 U4/U5 | ✅ **本稿定（E34-3）**：**同一次提交、可分开验** |
| **O34-4** | ★ 初设 §1.4「`QuantizeEdge` 无用例」不成立 | ✅ **本稿订正（§1.4）**：**有 5 个用例**，但**无判别真值表** ⇒ T34-1 理由改写、补判别力 |

---

## 9. 局限（L34-1..L34-3）

| # | 局限 | 说明 |
|---|---|---|
| **L34-1** | `INT_MIN` 附近 `-1` 会回绕 | 数学真值不可表示；**不新增钳制**（条 22）；真实 UI 不可达，属**防御性登记**。判据见 T34-7 |
| **L34-2** | 整数域前置条件是**设计假设**，非已证明的保证 | 由上游 `ScaleRect` 与布局链保证（结构性约束）。若日后出现**无窗口 / 离屏路径**（`roadmap #44` 的 M-1），**须重新评估** |
| **L34-3** | `RecordingBackend` **无法验证**后端量化 | 它记录的是**未量化**的 `Rect`（记录层在量化之前）。这不是本阶段缺陷，而是既有结构 ⇒ 量化抽成**自由纯函数**正是为了让它可测 |

---

## 10. 实施回填（模板，实施后逐批填写）

> ★ 每批完成后在此追加，格式：`批N（日期）：改动文件 · 用例数 · 回归结论 · 偏离记录`

| 批 | 日期 | 改动 | 用例 | 回归 | 偏离 |
|---|---|---|---|---|---|
| U1–U7（同一工作区集成；未拆分提交或独立逐单元回归） | 2026-10-17 · **归档 2026-10-19** | `GDIBackend.cpp`：实现 `ToPixelRect` / `ToPixelBox` 并覆盖 §2 指定消费者；`BackendContract.h` 更新历史注释；新增并登记 `BackendQuantizationTests.cpp` | **新增 9 条自动化测试**；§4 十项源码盯防人工逐项复核（含 16 项豁免注释/行为对照）；`PixelBoxDimensions` 含 162 组整数 DIP × scale {1,2} 几何比较，并执行真实窗口栅格探针 | MSVC Debug `ecdi_tests` 构建成功，全套 **474 passed / 0 failed / 474 total**；真实窗口 200×200 `Renderer → GDIBackend` 不透明 `DrawRect` 样本在 scale 1/2 与旧截断几何参考帧均为 **0 不同像素、0 CLR_INVALID**；`git diff --check` 通过 | 探针只覆盖所列四个不透明 `DrawRect` 矩形，不代表全部消费者或应用级视觉验收；测试文件里的 `TestPixelBox` 是独立契约 oracle，不直接调用 GDIBackend 匿名命名空间助手，调用点正确性由源码审计覆盖。旧 `audit_source.py` 使用改动前固定表达式/基线行号，未作为实施后验证器。无公共 API/CMake 改动；D6/N4 豁免继续保留。 |

★ **实施偏离**（详设与契约冲突时按契约裁决，集中记录于此，条 107）：

| # | 详设原文 | 实施 | 判据 |
|---|---|---|---|
| — | — | — | — |

---

## 11. 修订记录

- **v1.3（2026-10-19）归档**：实现与 9 条用例随 `feat(phase34)` 提交入库；全套 **487/487**（含 Phase 35 的 13 例）通过，退出码 0。状态行由「仍待复评与视觉回归」改为「已收口」，视觉回归以显式范围外记录（用户裁定），不再作为本阶段阻塞项。
- **v1.2（2026-10-17）实施回填**：按 §2/§6 将 GDI 矩形边统一至 `BackendContract::QuantizeEdge`；`ToPixelBox` 由 `ToPixelRect` 派生尺寸，`DrawImage` 的 AlphaBlend 与 sentinel 修补共享一盒，DrawLine bbox 作为唯一非 `Rect` 例外直接量化。新增并注册 9 条自动化测试（含三口径判别、边相减判别样本、焦点框整数/分数公式归因、float 端点差及 `INT_MIN` 量化值安全边界）；其中 `TestPixelBox` 是独立契约 oracle，不直接调用生产匿名助手，生产调用点由源码审计验证；`PixelBoxDimensions` 补充 162 组整数 DIP × scale {1,2} 旧/新几何边界零差异比较；并在真实 200×200 窗口通过 `Renderer → GDIBackend` 绘制四个不透明整数 DIP `DrawRect`，scale 1/2 下分别与按旧截断几何直接 `FillRect` 的参考帧比较，均为 0 个不同像素且无 `CLR_INVALID`（仅代表该路径/样本，不覆盖所有消费者或应用级视觉回归）；MSVC Debug `ecdi_tests` 构建成功，全套 **474 passed / 0 failed / 474 total**，`git diff --check` 通过。源码复核：①–③ 无目标直取整残留；④–⑩ 逐项核对通过，豁免的点位/线宽/半径保留并带 D6/N4 注释。`audit_source.py` 针对改动前表达式与固定基线行号，实施后不适合直接复跑，改以当前 diff 和 §4 判据复核。
- **v1.1**（2026-10-17）**自查补强：T34-2 的样本没有判别力**。★★ **问题**：初稿 T34-2 给了 5 组样本（`x=1.25,w=2.75` · `x=1.25,w=2.25` · `x=1.5,w=1.5` · `x=0.0,w=1.5` · `x=2,w=4`），但**只有第 4 组**能区分「边相减」与「独立 `lround(w)`」——前两组与第 5 组**恰好与旧式同值** ⇒ 它们验证「没算错」，**不能**证明「用对了口径」。★★ **根因**：我写样本时按「看起来能说明问题」挑，**没有写出判别条件再解**。★★★ **修法**：先写出判别式 `floor(x+w) - floor(x) != lround(w)`，再**穷举**（`x,w ∈ [0,4]`，步长 0.25，**272 组**）得判别点 **72 组（26.5%）**；并发现**最干净的形态 = `x` 取整数**（此时条件化简为 `floor(w) != lround(w)` ⇔ `w` 的 frac ≥ 0.5，穷举得 40 个判别点）。⇒ 样本表改为 **①②③ 判别样本 + ④⑤ 对照样本**，并写明「若误写成 `lround(rect.width)`，①②③ 全红而 ④⑤ 仍全绿」。★ **教训（可推广）**：**「对照样本」与「判别样本」必须分开列，且判别样本要先解判别式再挑**——否则一组看起来合理的样本可能**全部没有判决力**，用例全绿却证明不了任何事。★ 本条只改 §5 T34-2，其余章节一字未动（`git diff` 可核）。
- **v1.0**（2026-10-17）初稿。**输入**：需求 **v1.4 ✅ 已冻结** · 初设 **v1.15 待评审**。
  **内容**：§1 基线 B1–B10（★ 详设动笔前**重跑复核 74/74 全命中**）· §2 逐文件 △1–△3
  （**改动 1 个 .cpp + 1 个 .h 注释**，新建 0 生产文件）· §3 三处实现期必须解决（E34-1 `DrawLine`
  bbox 例外 · E34-2 助手落点 · E34-3 同函数两段）· §4 盯防十条（**可机检**）· §5 用例 T34-1..T34-8
  （**+9 条自动化**）· §6 施工顺序（U1–U7）+ 风险分类（三批）· §7 影响面 · §8 开放项 O34-1..4 ·
  §9 局限 L34-1..3 · §10 实施回填模板。
  ★★ **本稿相对初设的三处实质补充**：
  ① ★★★ **§1.4 订正初设的一处判断**——初设写「`QuantizeEdge` 当前无用例直接断言四象限行为」，
     实测**已有 5 个用例**（18 次调用、17 个不同输入值）；但逐条判定后发现它们**都不具口径判别力**
     （正数域 floor == trunc；负数域 floor == lround）⇒ **T34-1 仍必要，但理由改写为「补判别力」而非「从零建」**，
     并据此把 T34-1 拆成 **1a/1b/1c 三组**、**并列三种口径的期望值**。
  ② ★★ **T34-2 补判别样本**——初设的三组样本（`x=1.25,w=2.75` · `x=1.5,w=1.5` · `x=2,w=4`）
     **与旧式同值**，无法证明「边相减 ≠ 独立取整」。本稿补 **`x=0.0, w=1.5`**：
     `floor(1.5) - floor(0) = 1` vs `lround(1.5) = 2` ⇒ **必然分歧**，这才是有判决力的样本。
  ③ ★ **E34-1 新增**——初设把 `DrawLine` 的 bbox 与其它函数并列，暗示走 `ToPixelRect`；
     实测其输入是**四个 float 局部量**（`minX..maxY`），**不构成 `Rect`** ⇒ 本稿定为
     **唯一合法例外**（直接调 `QuantizeEdge` + 必须带注释），并写进盯防第 ⑧ 项。
  ★ **如实登记的边界**：本稿的 74/58/16 对账与源码覆盖核验均为**本机产物**（`expr_list.py` /
  `audit_source.py` 可复跑），**不是外部独立复现**；**三层验收必须继续分开**——
  ① 清单内部复算 · ② 清单 ↔ 源码覆盖 · ③ 实现回归（**尚未开始**）。
