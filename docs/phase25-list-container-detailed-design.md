# Phase 25 · 列表 / 网格容器 —— 详细设计（v1.0 · 实施规格）

> 来源：初设稿 `phase25-list-container-preliminary-design.md` **v1.1 ✅ 已通过**（评审「修 C-VIS 后可进详设」——C-VIS v2 四层结果契约 + 自身 bbox 负区停泊已吸收）
> 状态：**v1.0**（2026-10-02）——🚧 **待评审**
> 定位：**实施规格**——基线 B1–B16（带行号实测）· △1–△7 逐文件改动 · 契约 C1–C9 → 落点 → 测试全映射 · 盯防 8 条（可机检）· 用例正文 T25-1..T25-12 · 三批顺序。

---

## 1. 代码基线（B1–B16，全部 2026-10-02 带行号实测）

| # | 基线 | 证据 |
|---|---|---|
| **B1** | `Layout` 基类唯一纯虚 `virtual void Arrange(Widget& parent) = 0;` | `include/ECDI/Layout/Layout.h:13` |
| **B2** | Arrange 触发链 = **每帧**：`Window::OnPaint` → `m_rootWidget->Arrange()` → `ArrangeInternal()` 递归整树（有 `m_layout` 重排 / 无保持手摆） | `src/Window/Window.cpp:478` · `src/Widget/Widget.cpp:188-208` |
| **B3** | `SetLayout(std::unique_ptr<Layout>)` 在 **Widget 基类 public**；`ScrollContent : public Widget`（内部头）⇒ 布局宿主能力自动可用 | `include/ECDI/Widget/Widget.h:260` · `src/Widget/ScrollContent.h:23` |
| **B4** | `VerticalLayout::Arrange` **全量排列不跳过不可见**；**会 `SetSize`**（虚分派）；跨轴 `SetPosition(padding, y)` | `src/Layout/VerticalLayout.cpp:23-63` |
| **B5** | `HitTest` 子节点递归**无可见性过滤** | `src/Widget/Widget.cpp:139-152` |
| **B6** | `UpdateContentExtent()` **遍历全部子控件**：`w = max(x+w)`、`h = max(y+h)`，负向钳 0；**不是 Σ** | `src/Widget/ScrollView.cpp:59-71` |
| **B7** | `SetContentExtent` 原子副作用链：`ApplyLayout → ClampOffset → SyncBars → Invalidate`（顺序冻结） | `include/ECDI/Widget/ScrollView.h:72-78` |
| **B8** | `Widget::Arrange()` **public**（可显式局部重排） | `src/Widget/Widget.cpp:204` |
| **B9** | 现存 `UpdateContentExtent()` 调用方 = ScrollViewTests 3 处（normal / **empty** / **negative**）+ 生产零调用 | `src/Tests/ScrollViewTests.cpp:408,415,423` |
| **B10** | 几何全 **int**：`SetPosition(int,int)`（`Widget.h:83`）· `virtual SetSize(int,int)`（`:88`）· `GetX/Y/Width/Height`（`:135-141`）· `GetChildCount/GetChildAt`（`:74/266`）· `IsVisible()`（`:146`） | `include/ECDI/Widget/Widget.h` |
| **B11** | 断言包含方式：Layout cpp 直接 `#include "ECDI/Core/ECDIAssert.h"` 用 `FRAMEWORK_ASSERT` | `src/Layout/VerticalLayout.cpp:3,16` |
| **B12** | **ModelProbe `RebuildRows`（`examples/ModelProbe/ModelProbe.cpp:717-767`）**：`:719-720` 全隐藏 → `:734` `row.panel->SetSize(600,28)` → `:737` `SetPosition(0, i×28)` **手算** → `:756` `GetContentView().AddChild(...)` → **`:765-768` `SetContentExtent(600, n×28)` 手算，且自带注释明说不走 `UpdateContentExtent()`**——理由 =「隐藏旧行保留创建时几何，按包围盒推导会把它们算进范围（滚出空白区）」 | `examples/ModelProbe/ModelProbe.cpp:717-768` |
| **B13** | ★★ **B12 的注释 = C-VIS 停泊契约的「应用侧预演」**：那条手算 workaround 的存在理由，恰好被 C-VIS-2/4（隐藏行停泊自身 bbox 负区 → 对 extent 贡献归零）**结构性消除**——迁移后 `UpdateContentExtent()` 取代手算，注释随之删除 | 同上 |
| **B14** | ModelProbeTests 9 用例 = FetchFlow / FetchError 等页面行为断言，**零行几何断言** ⇒ 批三迁移与既有测试无冲突（前置检查通过） | `src/Tests/ModelProbeTests.cpp:226-234` |
| **B15** | LayoutTests 装置模式：直接实例化 `Panel` + `SetLayout(make_unique<...>)` + 显式 `panel.Arrange()` + `GetTestRegistry().Add` 注册；现存 **19 用例** | `src/Tests/LayoutTests.cpp:1-70,857` |
| **B16** | ModelProbeTests 现存 **9 用例**；全套 297 = 各文件 Add 求和 | `src/Tests/ModelProbeTests.cpp` · grep 实测 |

---

## 2. 逐文件改动（△1–△7；新建 4 · 改动 3 · 公共头 +2）

### △1 · 新建 `include/ECDI/Layout/ListLayout.h`（全文）

```cpp
﻿#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{

/// @brief 索引排列布局（Phase 25）：可见子控件按序纵向排列，行几何固定
/// @details 与 VerticalLayout 的分工（初设 §3-②）：**空间分配** vs **索引排列**——
/// 本布局不读 parent 尺寸（挂载点 ScrollContent 的尺寸 = 内容 extent，读之成环）。
/// 契约：
/// - C-VIS（四层）：只排列 IsVisible() 的直接子控件（连续槽位）；**不可见子控件停泊
///   到自身 bbox 负区** `(-w, -h)`（右下角恰落 (0,0) ⇒ 对 UpdateContentExtent 贡献归零、
///   不可命中）；实现策略可替换，行为保证（extent / hit-test）不变。
/// - D9：只调 SetPosition，**不调 SetSize**（child 尺寸归消费者）。
/// - 只处理 parent 的**直接 children**，不递归孙节点（与 Vertical/Horizontal 同族）。
class ListLayout : public Layout{
public:
    /// @param rowHeight 行高（DIP，≥ 1）
    /// @param spacing 行间距（DIP，≥ 0）
    explicit ListLayout(int rowHeight, int spacing = 0);
    void Arrange(Widget& parent) override;
private:
    int m_rowHeight;
    int m_spacing;
};

}
```

### △2 · 新建 `src/Layout/ListLayout.cpp`（全文规格）

```cpp
﻿#include "ECDI/Layout/ListLayout.h"
#include "ECDI/Core/ECDIAssert.h"    // 同 VerticalLayout.cpp:3（B11）

namespace ECDI{

ListLayout::ListLayout(int rowHeight, int spacing)
    : m_rowHeight(rowHeight), m_spacing(spacing)
{
    FRAMEWORK_ASSERT(rowHeight >= 1);
    FRAMEWORK_ASSERT(spacing >= 0);
}

void ListLayout::Arrange(Widget& parent){
    int slot = 0;   // 可见序号（C-VIS-1：连续槽位）
    for (size_t i = 0; i < parent.GetChildCount(); ++i){
        Widget* child = parent.GetChildAt(i);
        if (!child->IsVisible()){
            child->SetPosition(-child->GetWidth(), -child->GetHeight());   // C-VIS-4 停泊
            continue;
        }
        child->SetPosition(0, slot * (m_rowHeight + m_spacing));   // D9：只 SetPosition
        ++slot;
    }
}

}
```

★ 零 `SetSize` / 零 `parent.GetWidth()/GetHeight()` 读取（盯防①④）。

### △3 · 新建 `include/ECDI/Layout/GridLayout.h`（全文）

```cpp
﻿#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{

/// @brief 网格排列布局（Phase 25）：可见子控件按序填充固定 cell 网格，末行左对齐
/// @details ★ cellWidth/cellHeight = 布局槽位的几何间距，**不是 child 尺寸约束**
/// （D9：child 大于 cell = 溢出，由消费者自治，本布局不裁不缩）。
/// 契约同 ListLayout（C-VIS / D9 / 只处理直接 children）。
/// 自适应列宽 = 重启条件（随 desktopnest M2 立项取证——初设 §3-⑤）。
class GridLayout : public Layout{
public:
    /// @param columns 列数（≥ 1）；row = index / columns，col = index % columns
    explicit GridLayout(int columns, int cellWidth, int cellHeight, int spacing = 0);
    void Arrange(Widget& parent) override;
private:
    int m_columns;
    int m_cellWidth;
    int m_cellHeight;
    int m_spacing;
};

}
```

### △4 · 新建 `src/Layout/GridLayout.cpp`（算法规格）

同 △2 骨架；`Arrange` 差异：可见序号 k ⇒ `col = k % m_columns`、`row = k / m_columns` ⇒ `SetPosition(col × (m_cellWidth + m_spacing), row × (m_cellHeight + m_spacing))`；不可见 ⇒ 同款停泊 `(-w, -h)`。断言 `columns ≥ 1 / cellWidth ≥ 1 / cellHeight ≥ 1 / spacing ≥ 0`。

### △5 · `src/Tests/LayoutTests.cpp`（批一/批二承载）

新增 8 用例（T25-1..9, 12 中的 LayoutTests 部分，见 §5）；include 增 `ECDI/Layout/ListLayout.h` / `ECDI/Layout/GridLayout.h`。装置沿 B15 模式：`Panel` + `SetLayout` + 显式 `Arrange()` + 裸 `Widget` 子。

### △6 · `examples/ModelProbe/ModelProbe.cpp`（批三迁移）

1. **组装处（`:355` 附近）**：`auto list = std::make_unique<ScrollView>();` 之后加
   `list->GetContentView().SetLayout(std::make_unique<ListLayout>(static_cast<int>(kRowHeight)));`
   + include `ECDI/Layout/ListLayout.h`。
2. **`RebuildRows` 新建分支（`:737`）**：**删除** `row.panel->SetPosition(0, ...)`（位置归布局——C-VIS-1）。
3. **`RebuildRows` 尾部（`:765-768`）**：`m_scroll->SetContentExtent(600, n×kRowHeight)` 及其注释**整块删除**，替换为两步：
   `m_scroll->GetContentView().Arrange();` + `m_scroll->UpdateContentExtent();`
   ★ B13：被删注释的「隐藏行算进范围」顾虑由 C-VIS-2/4 结构性消除。
4. 复用分支不动（`SetVisible(true)` 即可——Arrange 会重新给位）。

### △7 · `src/Tests/ModelProbeTests.cpp`（批三）

新增 1 用例（T25-10 迁移结构断言）；注册段已有 `RegisterModelProbeTests` ⇒ **RunAllTests 零改动**（LayoutTests 同理——B15 已注册）。

---

## 3. 契约 C1–C9 → 落点 → 测试全映射

| # | 契约 | 落点 | 测试 |
|---|---|---|---|
| **C1** | 排列语义：只排列 `IsVisible()` 直接子控件，连续槽位（C-VIS-1） | △2/△4 | T25-1 |
| **C2** | 停泊：不可见 ⇒ 自身 bbox 负区 `(−w, −h)`（C-VIS-4），效果 = `x+w ≤ 0 ∧ y+h ≤ 0`（C-VIS-2） | △2/△4 | T25-2 |
| **C3** | 停泊后可见区不可命中（C-VIS-3） | △2/△4 | T25-3 |
| **C4** | extent 组合：`Arrange` 后 `UpdateContentExtent` = 可见子包围盒（含非等尺寸真实几何） | △5/△6 | T25-4 / T25-12 |
| **C5** | D9：全路径零 `SetSize`；child 尺寸不受排列影响 | △2/△4 | T25-6 |
| **C6** | 只处理直接 children（行内绝对定位不受影响） | △2/△4 | T25-10 |
| **C7** | 两步模式调用责任（几何/结构变更后消费者执行） | △6 | T25-10 |
| **C8** | Grid 语义：`col/row` 分解、末行左对齐、cell = 槽位非约束 | △4 | T25-5 |
| **C9** | 零回归：Vertical/Horizontal/ScrollView/既有 297 用例逐位不变 | — | T25-9 |

---

## 4. 盯防清单（8 条，全部可机检）

| # | 盯防 | 机检 |
|---|---|---|
| ① | 两个新 cpp 内**零 `SetSize`** | grep |
| ② | 零 `parent.GetWidth()/GetHeight()` 读取（无环保证） | grep |
| ③ | 停泊 = `(−GetWidth(), −GetHeight())`，**非** `(0, −行高)` | 代码审查 |
| ④ | 无递归（Arrange 内不调自身/不遍历孙节点） | 代码审查 |
| ⑤ | `git diff` 范围 = △1–△7 清单内（ScrollView/Vertical/Horizontal/Widget 零 diff） | git |
| ⑥ | 公共头 **92 → 94**（新增恰 2）· CMake 零 diff | git + find |
| ⑦ | RunAllTests.* 零 diff | git |
| ⑧ | ModelProbe `RebuildRows` 内 `i × kRowHeight` / `SetContentExtent` **零命中**（手算消失） | grep |

---

## 5. 用例正文（T25-1..T25-12；★ 自动化 **+9** ⇒ **297 → 306**——对初设 ~308 做口径勘误：T25-9 为检查点、T25-11 人工，均不计入——沿 Phase 19/24 勘误先例）

| # | 输入 | 期望 |
|---|---|---|
| **T25-1** | `ListLayout(28)`；可见子 A/B/C（任意尺寸） | `A.y=0, B.y=28, C.y=56`；`x` 恒 0（A1） |
| **T25-2** | 子 A/B 可见 + H 隐藏（H 尺寸 600×28） | H 不占槽位（B 仍 y=28）；**效果断言**：`H.x + H.w ≤ 0 ∧ H.y + H.h ≤ 0`（C-VIS-2/4，测行为不测坐标） |
| **T25-3** | 同上，内容视图 `HitTest` | 可见区内任意点 `≠ H`；H 原可见位置无命中（C-VIS-3） |
| **T25-4** | 3 可见子（600×28）→ Arrange → `UpdateContentExtent()` | extent = `(600, 84)`；B7 副作用链触发（bars 同步） |
| **T25-5** | `GridLayout(2, 100, 50)` + 5 可见子 | `(0,0) (100,0) (0,50) (100,50) (0,100)`——**末行左对齐**（C8） |
| **T25-6** | List + Grid 各一： Arrange 前后逐 child `GetSize()` | **逐位不变**（C5/D9） |
| **T25-7** | Arrange 后手动 `child.SetPosition(-10,-20)` → `UpdateContentExtent()` | 钳 0 生效（沿 B9 negative 用例语义——既有行为零改动） |
| **T25-8** | 0 子 / 全隐藏 | Arrange no-op；extent = `(0,0)`（C-VIS-2 推论） |
| **T25-9** | 检查点（非用例）：既有 Vertical/Horizontal/ScrollView 用例全绿 | C9 |
| **T25-10** | ModelProbePage：注入 3 模型 → RebuildRows | `row[i].y == i×28` · `height == 28` · `contentExtent == (600, 84)`；过滤 1 个后 extent 随两步更新（A5 结构断言） |
| **T25-11** | 人工（用户侧 @125%）：观感 / 滚动 / 勾选 / 过滤与迁移前一致 | A5 |
| **T25-12** | `ListLayout(28)` + 子 100×20 / 200×50 / 150×28（**非等尺寸**） | `y = 0 / 28 / 56`、size 逐位不变、`UpdateContentExtent = (200, 78)`（评审 §十六——同时锁 C5 + B6 真实几何） |

---

## 6. 影响面

| 项 | 明细 |
|---|---|
| **新增** | 公共头 ×2（`ListLayout.h` / `GridLayout.h`，→ **94**）+ 内部 cpp ×2 |
| **改动** | `LayoutTests.cpp`（+8 用例）· `ModelProbeTests.cpp`（+1）· `ModelProbe.cpp`（迁移） |
| **零改动** | `Layout.h` / `VerticalLayout` / `HorizontalLayout` / `ScrollView.{h,cpp}` / `ScrollContent` / `Widget` / `RunAllTests.*` / CMake |
| **规模口径** | 用例 **297 → 306**（自动化 +9：LayoutTests +8 · ModelProbeTests +1；★ 对初设 ~308 做口径勘误——T25-9 检查点、T25-11 人工不计入，沿 Phase 19/24 先例）· 公共 API **+2 类型**（既有 API 零改动） |

---

## 7. 三批实现顺序

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批一** | △1 + △2 + △5 的 List 部分（T25-1..4, 6(List), 7, 8 = +7 → 304） | 全绿（零回归 + 新用例） |
| **批二** | △3 + △4 + T25-5 + T25-6 Grid 扩展（+2 → 306 的 Grid 部分） | 全绿 |
| **批三** | △6 迁移 + △7（T25-10）+ T25-11 目视 | 四链全绿（306/306）+ 用户目视 + 台账收口 |

## 8. 开放项 / 局限（沿初设 O1–O4 · L1–L2 原样生效）

O1 拉伸策略 · O2 自适应列宽重启条件 · O3 `ScrollView.h:21` 措辞（详设期文档改动项——本阶段实施时一并落地）· **O4 用例数交接清单 = ListLayout 6 + GridLayout 2 + ModelProbe 1 = 9（本节即交接清单）**；L1 停泊协同契约 · L2 键盘导航/双击不做。

## 10. 修订记录

- **v1.0**（2026-10-02）初稿（实施规格）。**基线 B1–B16**（带行号；★ **B12/B13 = 本稿最有价值的发现**：ModelProbe `:765-768` 的手算 extent **自带注释**解释为何不能走 `UpdateContentExtent`——「隐藏旧行保留几何会被算进范围」——该 workaround 恰被 **C-VIS 停泊**结构性消除，迁移 = 删注释 + 删手算 + 两步模式）。**△1–△7**（新建 4：两公共头 + 两 cpp；改动 3：LayoutTests / ModelProbe / ModelProbeTests；**RunAllTests 零改动**——B15 已注册）。**契约 C1–C9 全映射** · **盯防 8 条**（机检：零 SetSize / 零 parent 尺寸读取 / 停泊位 / 无递归 / git 范围 / 头计数 / 手算消失）· **用例正文 T25-1..T25-12**（★ **口径勘误：自动化 +9 ⇒ 297 → 306**——初设 ~308 把检查点与人工误计入，沿 Phase 19/24 先例）· **三批**（7 / 2 / 1+目视）。**待评审。**
