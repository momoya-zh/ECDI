# Phase 25 · 列表 / 网格容器 —— 详细设计（v1.2 · 实施规格）

> 来源：初设稿 `phase25-list-container-preliminary-design.md` **v1.1 ✅ 已通过**（评审「修 C-VIS 后可进详设」——C-VIS v2 四层结果契约 + 自身 bbox 负区停泊已吸收）
> 状态：**v1.2**（2026-10-02）——✅ **第二轮外部评审通过（可进入 Implementation）**（外部评审结论：「**Phase 25 Detailed Design v1.1：通过，可进入 Implementation**」，并明言「**没有必要再开 v1.2**」＝ 无需新一轮**设计**，非指版本号；★ **v1.2 = 5 处文档精度修正**——T25-12 的 extent 算术 / §6 用例计数与 `ScrollView.h` / T25-6 描述，**核心方案一字未改**；详见 §10 修订记录 v1.2）
> 定位：**实施规格**——基线 B1–B16（带行号实测）· △1–△8 逐文件改动 · 契约 C1–C9 → 落点 → 测试全映射 · 盯防 8 条（可机检）· 用例正文 T25-1..T25-12 · 三批顺序。

---

## 1. 代码基线（B1–B16，全部 2026-10-02 带行号实测）

| # | 基线 | 证据 |
|---|---|---|
| **B1** | `Layout` 基类唯一纯虚 `virtual void Arrange(Widget& parent) = 0;` | `include/ECDI/Layout/Layout.h:13` |
| **B2** | Arrange 触发链 = **每帧**：`Window::OnPaint` → `m_rootWidget->Arrange()` → `ArrangeInternal()` 递归整树（有 `m_layout` 重排 / 无保持手摆） | `src/Window/Window.cpp:478` · `src/Widget/Widget.cpp:188-208` |
| **B3** | `SetLayout(std::unique_ptr<Layout>)` 在 **Widget 基类 public**；`ScrollContent : public Widget`（内部头）⇒ 布局宿主能力自动可用 | `include/ECDI/Widget/Widget.h:260` · `src/Widget/ScrollContent.h:23` |
| **B4** | `VerticalLayout::Arrange` **全量排列不跳过不可见**；**会 `SetSize`**（虚分派）；跨轴 `SetPosition(padding, y)` | `src/Layout/VerticalLayout.cpp:23-63` |
| **B5** | ★★ **`HitTest` 的可见性门控在「入口」而非「子节点循环」**：入口 `if (!IsVisible()) return nullptr;` ⇒ 不可见节点**及其整棵子树**都不可命中；子节点逆序循环（`:137-153`）只是**不再额外过滤**（由每个 child 的入口自查） | `src/Widget/Widget.cpp:114-121`（入口门控）· `:137-153`（子节点循环） |
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

## 2. 逐文件改动（△1–△8；新建 4 · 改动 4 · 公共头 +2）

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
///   到自身 bbox 负区** `(-w, -h)`（右下角恰落 (0,0) ⇒ 对 UpdateContentExtent 贡献归零）；
///   ★ 停泊**只服务 extent**——不可命中性由 `Widget::HitTest` 入口的可见性门控保证（B5），与本布局无关；
///   实现策略可替换，行为保证（extent）不变。
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

新增 **9** 用例（**T25-1..8 + T25-12**，见 §5；★ v1.1 口径修正——原文「8 用例 / T25-1..9」误把检查点 T25-9 计入）；include 增 `ECDI/Layout/ListLayout.h` / `ECDI/Layout/GridLayout.h`。装置沿 B15 模式：`Panel` + `SetLayout` + 显式 `Arrange()` + 裸 `Widget` 子。

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

### △8 · `include/ECDI/Widget/ScrollView.h:21`（注释措辞修正 · 原 O3 落地）

原文：
```cpp
/// - **不推导** 内容尺寸以外的语义（不做 ListBox/虚拟化/框选——见需求稿 §4）。
```
⇒ 改为：
```cpp
/// - **不推导** 内容尺寸以外的语义（不做 ListBox/虚拟化/框选——见需求稿 §4）；★ 需要「按序摆放」时
///   用 `Layout/ListLayout` / `Layout/GridLayout`（Phase 25）与本控件**组合**，**不在本类内建**。
```

★ **理由**：本 Phase 之后「框架已有列表容器」是事实，原句会让读者误以为框架不做列表 —— **注释会静默变假**（沿条 96）。★ **仅改注释**，零代码 / 零 API / 零行为影响 ⇒ 盯防⑤ 的 diff 范围含 △8。

## 3. 契约 C1–C9 → 落点 → 测试全映射

| # | 契约 | 落点 | 测试 |
|---|---|---|---|
| **C1** | 排列语义：只排列 `IsVisible()` 直接子控件，连续槽位（C-VIS-1） | △2/△4 | T25-1 |
| **C2** | 停泊：不可见 ⇒ 自身 bbox 负区 `(−w, −h)`（C-VIS-4），效果 = `x+w ≤ 0 ∧ y+h ≤ 0`（C-VIS-2） | △2/△4 | T25-2 |
| **C3** | ★ **不可命中性来自既有门控、非本布局**：`Widget::HitTest` 入口的 `IsVisible()` 已保证不可见节点（及其子树）不可命中 ⇒ 停泊**不负责命中**，本布局**不额外承担**命中语义（v1.1 修正） | 依赖既有行为（B5） | T25-3（★ 改为「隐藏↔可见往返幂等」） |
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
| ⑤ | `git diff` 范围 = **△1–△8** 清单内（★ △8 = `ScrollView.h:21` **注释行**；`ScrollView.cpp` / `VerticalLayout` / `HorizontalLayout` / `Widget` **零 diff**） | git |
| ⑥ | 公共头 **92 → 94**（新增恰 2）· CMake 零 diff | git + find |
| ⑦ | RunAllTests.* 零 diff | git |
| ⑧ | ModelProbe `RebuildRows` 内 `i × kRowHeight` / `SetContentExtent` **零命中**（手算消失） | grep |

---

## 5. 用例正文（T25-1..T25-12；★ 自动化 **+10** ⇒ **297 → 307**——**唯一口径**：LayoutTests **+9**（T25-1..8 + T25-12）· ModelProbeTests **+1**（T25-10）；T25-9 为检查点、T25-11 人工，**均不计入**）

| # | 输入 | 期望 |
|---|---|---|
| **T25-1** | `ListLayout(28)`；可见子 A/B/C（任意尺寸） | `A.y=0, B.y=28, C.y=56`；`x` 恒 0（A1） |
| **T25-2** | 子 A/B 可见 + H 隐藏（H 尺寸 600×28） | H 不占槽位（B 仍 y=28）；**效果断言**：`H.x + H.w ≤ 0 ∧ H.y + H.h ≤ 0`（C-VIS-2/4，测行为不测坐标） |
| **T25-3** | `ListLayout(28)` + A/B/C 可见 → 隐藏 B → `Arrange` → **恢复 B** → `Arrange` | 隐藏后 C 上移到 `y=28`；恢复后 B 回 `y=28`、C 回 `y=56` ⇒ **隐藏↔可见往返幂等**（C-VIS-1 连续槽位）★ 替代原「不可命中」断言——那条由 B5 入口门控**恒成立**、无区分度（v1.1 修正） |
| **T25-4** | 3 可见子（600×28）→ Arrange → `UpdateContentExtent()` | extent = `(600, 84)`；B7 副作用链触发（bars 同步） |
| **T25-5** | `GridLayout(2, 100, 50)` + 5 可见子 | `(0,0) (100,0) (0,50) (100,50) (0,100)`——**末行左对齐**（C8） |
| **T25-6** | List + Grid 各一： Arrange 前后逐 child `GetSize()` | **逐位不变**（C5/D9） |
| **T25-7** | Arrange 后手动 `child.SetPosition(-10,-20)` → `UpdateContentExtent()` | 钳 0 生效（沿 B9 negative 用例语义——既有行为零改动） |
| **T25-8** | 0 子 / 全隐藏 | Arrange no-op；extent = `(0,0)`（C-VIS-2 推论） |
| **T25-9** | 检查点（非用例）：既有 Vertical/Horizontal/ScrollView 用例全绿 | C9 |
| **T25-10** | ModelProbePage：注入 3 模型 → RebuildRows | `row[i].y == i×28` · `height == 28` · `contentExtent == (600, 84)`；过滤 1 个后 extent 随两步更新（A5 结构断言） |
| **T25-11** | 人工（用户侧 @125%）：观感 / 滚动 / 勾选 / 过滤与迁移前一致 | A5 |
| **T25-12** | `ListLayout(28)` + 子 100×20 / 200×50 / 150×28（**非等尺寸**） | `y = 0 / 28 / 56`、size 逐位不变、`UpdateContentExtent = (200, 84)`（评审 §十六——同时锁 C5 + B6 真实几何） |

---

## 6. 影响面

| 项 | 明细 |
|---|---|
| **新增** | 公共头 ×2（`ListLayout.h` / `GridLayout.h`，→ **94**）+ 内部 cpp ×2 |
| **改动** | `LayoutTests.cpp`（**+9** 用例）· `ModelProbeTests.cpp`（**+1**）· `ModelProbe.cpp`（迁移）· `ScrollView.h`（仅 △8 注释行） |
| **零改动** | `Layout.h` / `VerticalLayout` / `HorizontalLayout` / `ScrollView.cpp`（★ `ScrollView.h` **有 △8 注释行** ⇒ 不在此列） / `ScrollContent` / `Widget` / `RunAllTests.*` / CMake |
| **规模口径** | 用例 **297 → 307**（自动化 **+10**：LayoutTests **+9**（T25-1..8 + T25-12）· ModelProbeTests **+1**（T25-10））· 公共 API **+2 类型**（既有 API 零改动） |

---

## 7. 三批实现顺序

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批一** | △1 + △2 + △5 的 List 部分（T25-1..4, 6, 7, 8, 12 = **+8** → 305） | 全绿（零回归 + 新用例） |
| **批二** | △3 + △4 + T25-5 + T25-6 的 Grid 扩展（**+1** → 306）；★ **T25-6 = 单一共享测试**：批一实现 **List 分支**，批二在**同一测试内追加 Grid 分支**（**不新增测试编号**、不重复计数） | 全绿 |
| **批三** | △6 迁移 + △7（T25-10 · **+1** → 307）+ **△8**（`ScrollView.h:21` 注释）+ T25-11 目视 | 四链全绿（**307**/307）+ 用户目视 + 台账收口 |

## 8. 开放项 / 局限（沿初设 O1–O4 · L1–L2 原样生效）

O1 拉伸策略 · O2 自适应列宽重启条件 · ~~O3 `ScrollView.h:21` 措辞~~（★ **v1.1 已落地 = △8**，见 §2） · **O4 用例数交接清单 = LayoutTests 9（List 7：T25-1/2/3/4/7/8/12 · Grid 1：T25-5 · 共用 1：T25-6）+ ModelProbeTests 1（T25-10）= 10**（本节即交接清单，v1.1 口径修正）；L1 停泊协同契约 · L2 键盘导航/双击不做。

## 10. 修订记录

- **v1.2**（2026-10-02）**第二轮外部评审处置 —— ✅ 通过，可进入 Implementation**。① **评审结论**：「**Phase 25 Detailed Design v1.1：通过，可进入 Implementation**」（C-VIS 彻底闭环 · 契约映射完整 · `(-w,-h)` 规格正确 · T25-12 有价值 · 三批顺序合理 · 盯防 8 条认可；「**没有必要再开 v1.2**」＝ 无需新一轮**设计**，**非**指版本号）。② **修正 5 处文档精度问题**（均不涉及设计变更）：ⓐ ★★★ **§5 T25-12 的 `UpdateContentExtent` 期望值算术修正**：`(200, 78)` → **`(200, 84)`**——child3（150×28 @ y=56）的 `y + h = 84` > child2 的 78（对照 **T25-4 / T25-10** 的 `(600, 84)` **均正确**，唯 T25-12 少算 child 3）；★ **外部评审亦原样引用了 78**（未验算），本版据 B6 定义（`h = max(y+h)`）重算。ⓑ **§6 影响面「改动」列**：`LayoutTests.cpp（+8 用例）` → **`+9`**（残留；△5 / §5 / §7 / §8 早已统一为 9）。ⓒ **§6「改动」列补 `ScrollView.h`**（△8 注释行）—— 原文只列 3 项，与 §2「**改动 4**」不符。ⓓ **§6「零改动」列**：`ScrollView.{h,cpp}` → **`ScrollView.cpp`**（`ScrollView.h` 有 △8 注释行 ⇒ 不属零改动；与盯防⑤ 口径对齐）。ⓔ **§7 T25-6 描述明确化**：改为「**单一共享测试**——批一实现 List 分支，批二在**同一测试内追加** Grid 分支，**不新增测试编号**」（原「批一写 List 版、批二扩 Grid 版」易被误读为新增测试编号）。★ **核心设计（`ListLayout` / `GridLayout` / C-VIS / `ScrollView` / `Layout` 基类 / `Widget`）一处未改** —— 本版**只修正文档精度**。

- **v1.1**（2026-10-02）**评审处置（Miao）—— 修正 3 处准确性问题（核心方案全部维持）**。① ★★★ **B5 基线修正**：原记「`HitTest` 子节点递归**无可见性过滤**」⇒ 实测**门控在入口**（`Widget.cpp:114-121`：`if (!IsVisible()) return nullptr;` ⇒ 不可见节点**及其整棵子树**均不可命中），子节点逆序循环（`:137-153`）只是**不再额外过滤**、由每个 child 的入口自查。★ **推论纠正**：**停泊的唯一收益是 extent**（`UpdateContentExtent` 实测**不过滤**可见性，`ScrollView.cpp:59-71` 只做 `max(x+w)`），**与命中无关**。② ★★ **C-VIS-3 重定义**：「停泊后可见区不可命中」⇒ **「不可命中性来自既有 `Widget::HitTest` 入口门控，本布局不额外承担命中语义」**（**编号保留** ⇒ 零条号漂移）；△1 / △3 的类注释同步（删除「不可命中」这一**错误归因**）。③ ★★ **T25-3 重塑（去平凡断言）**：原「`HitTest` 不可命中」由 B5 入口门控**恒成立 ⇒ 无区分度**（沿 GL spike 的教训「**平凡断言比没有断言更危险**」）⇒ 改为 **「隐藏↔可见往返幂等」**（测 C-VIS-1 连续槽位）。④ ★★ **用例计数收敛为唯一口径**：△5「+8」/ §5「+9」/ §7「+7+2=9」三处打架，且 ★ **T25-12 漏归批** ⇒ 统一为 **LayoutTests +9（T25-1..8 + T25-12）· ModelProbeTests +1（T25-10）· 合计 +10 ⇒ 297 → 307**；批次 = 批一 **+8 → 305** · 批二 **+1 → 306** · 批三 **+1 → 307**；★ **T25-6 定为单用例**（批一 List 版、批二扩 Grid 版，**不重复计数**）。⑤ ★ **O3 与盯防⑤ 的矛盾消除**：O3「`ScrollView.h:21` 措辞本阶段一并落地」 vs 盯防⑤「`ScrollView` 零 diff」⇒ **定为落地**（新增 **△8**，仅改注释行；理由 = 该注释在本 Phase 后会**静默变假**，沿条 96），盯防⑤ 范围相应含 △8。⑥ ★ **未改动**：核心方案（`ListLayout` / `GridLayout` 布局层 · C-VIS 停泊消除 extent workaround）**全部维持**；△1–△7 的**代码规格一字未改**。
- **v1.0**（2026-10-02）初稿（实施规格）。**基线 B1–B16**（带行号；★ **B12/B13 = 本稿最有价值的发现**：ModelProbe `:765-768` 的手算 extent **自带注释**解释为何不能走 `UpdateContentExtent`——「隐藏旧行保留几何会被算进范围」——该 workaround 恰被 **C-VIS 停泊**结构性消除，迁移 = 删注释 + 删手算 + 两步模式）。**△1–△7**（新建 4：两公共头 + 两 cpp；改动 3：LayoutTests / ModelProbe / ModelProbeTests；**RunAllTests 零改动**——B15 已注册）。**契约 C1–C9 全映射** · **盯防 8 条**（机检：零 SetSize / 零 parent 尺寸读取 / 停泊位 / 无递归 / git 范围 / 头计数 / 手算消失）· **用例正文 T25-1..T25-12**（★ **口径勘误：自动化 +9 ⇒ 297 → 306**——初设 ~308 把检查点与人工误计入，沿 Phase 19/24 先例）· **三批**（7 / 2 / 1+目视）。**待评审。**
