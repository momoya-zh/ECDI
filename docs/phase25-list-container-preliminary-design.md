# Phase 25 · 列表 / 网格容器 —— 初步设计（v1.0）

> 来源：需求确认稿 `phase25-list-container-requirements.md` **v1.1 ✅ 已通过**（评审「原则上通过，小修后进初设」；初设必答 5 件见其 §1.5/§6）
> 状态：**v1.0**（2026-10-02）——🚧 **待评审**
> 定位：接口草案 + 组合契约 + 决策定案。本 Phase 无渲染谜题，**取证重心 = 接线机制**（Arrange 触发链 / 不可见子控件 / extent 推导的交互）。

---

## 1. 需求 → 初设映射

| 需求条目 | 本稿落点 |
|---|---|
| **D1 布局层** | §4 两个 `Layout` 派生（`ListLayout` / `GridLayout`），与 `ScrollView` 零耦合 |
| **D2 命名** | §4 类名冻结（`ListLayout` / `GridLayout`——与 `Vertical/HorizontalLayout` 同族） |
| **D3 Grid 参数暂缓** | §3-⑤ 定案：**固定 cell v1** + 自适应列宽登记重启条件（B 消费者未实现，无法对真实需求取证——评审建议的 spike 改为「决策 + 重启条件」，理由见 §3） |
| **D4 extent 归属** | §5 两步模式（`Arrange()` 显式 + `UpdateContentExtent()`）；`Layout::Arrange()` 签名**零改动** |
| **D5 边距** | 归外层 `Panel::padding`；两个新 Layout **无 padding 参数** |
| **D9 排列边界原则** | §3-④ 契约化：只 `SetPosition`、不 `SetSize`；配 T25-6 盯防 |
| **A1–A7** | §7 测试大纲 T25-1..T25-11 |

---

## 2. 代码基线（B1–B10，全部 2026-10-02 带行号实测）

| # | 基线 | 证据 |
|---|---|---|
| **B1** | `Layout` 基类极简：`virtual void Arrange(Widget& parent) = 0;` 唯一纯虚 | `include/ECDI/Layout/Layout.h:13` |
| **B2** | **Arrange 触发链 = 每帧**：`Window::OnPaint` → `m_rootWidget->Arrange()` → `ArrangeInternal()` 递归整树，**有 `m_layout` 的节点重排、无 Layout 的节点保持手摆**（渐进语义——9.7） | `src/Window/Window.cpp:478` · `src/Widget/Widget.cpp:188-208` |
| **B3** | **`SetLayout` 在 `Widget` 基类**（public）：`void SetLayout(std::unique_ptr<Layout>)` ⇒ `ScrollContent : public Widget`（内部头）**自动成为布局宿主**——`GetContentView().SetLayout(...)` 无需任何改动 | `include/ECDI/Widget/Widget.h:260` · `src/Widget/ScrollContent.h:23` |
| **B4** | **`VerticalLayout::Arrange` 不跳过不可见子控件**（全量 `for i < count` 按序分配） | `src/Layout/VerticalLayout.cpp:23-63` |
| **B5** | **`HitTest` 无可见性过滤**（子节点递归命中不查 `IsVisible`）⇒ 隐藏子控件若停在可见区**会被命中** | `src/Widget/Widget.cpp:139-152` |
| **B6** | **`UpdateContentExtent()` 遍历全部子控件**（无可见性过滤）：`extent = (max(x+w), max(y+h))`，负向钳 0 | `src/Widget/ScrollView.cpp:59-71` |
| **B7** | **`SetContentExtent` 原子副作用链**（顺序冻结）：`ApplyLayout → ClampOffset → SyncBars → Invalidate` | `include/ECDI/Widget/ScrollView.h:72-78` |
| **B8** | `Widget::Arrange()` 为 **public**（可显式触发局部重排） | `src/Widget/Widget.cpp:204` |
| **B9** | 现存 `UpdateContentExtent()` 调用方 = **仅 ScrollViewTests 3 处**（normal / empty / **negative**——负向钳 0 已有用例锚）+ 生产零调用 | `src/Tests/ScrollViewTests.cpp:408,415,423` |
| **B10** | 布局几何全 **int**（`SetPosition(int,int)` / `SetSize(int,int)`；`VerticalLayout(int spacing, bool fill, int padding)` 构造同族，`FRAMEWORK_ASSERT` 非负） | `src/Layout/VerticalLayout.cpp:10-16` |

---

## 3. 决策定案（评审 5 件 + 语义新发现）

### 3-① **D4 定案：两步模式**（不改 `Layout::Arrange()` 签名）

```text
数据 / 行集变更后（事件驱动，一次性）：
    contentView.Arrange();          // B8：显式局部重排（public，零接口改动）
    m_scroll.UpdateContentExtent(); // B6：从已摆好的子控件推导 extent（走 B7 副作用链）

无变更的帧：
    Window::OnPaint → Arrange()（B2 每帧自动维持，零成本——纯 SetPosition）
```

⇒ 消费者的两处手算消失：K2（`i × 行高`）由布局接管；K3（`n × 行高`）由 `UpdateContentExtent()` 接管（B6：非 Σ，包围盒右下角）。

### 3-② **ListLayout vs VerticalLayout——为什么新增类而不是增强**（评审必答 ④，正式成文）

1. **概念不同**：Vertical = **空间分配布局**（读 `parent.GetWidth()/GetHeight()` 按 stretch/fill/child size 动态分配）；List = **索引排列布局**（位置 = f(可见序号 × 固定行几何)，**不读 parent 尺寸**）。
2. **循环依赖规避**：挂载点是 `ScrollContent`，而**其尺寸 = 内容 extent**（`ScrollView.h:57`）——若布局读 parent 尺寸（fill 语义），extent → 尺寸 → extent 成环；ListLayout 的固定行几何**天然无环**。
3. **参数集不相容**：Vertical 的 `stretch/fillCrossAxis` 对 List 无意义（合并 = 「某参数在某模式下无效」，违「一个概念一个词」）。
4. **可见性语义不同**：Vertical 全量分配（B4 既有行为，A4 零回归不动它）；List 需要**跳过不可见**（见 3-③）。

### 3-③ **不可见子控件语义（新契约，本稿最重要的新发现）**

行池 + 过滤（K4/K5）与两个既有机制的交互**必然出错**，取证如下：

- 布局若**全量排列**（含隐藏行）：隐藏行占用 index ⇒ 可见行错位（违背过滤语义）；
- 布局若**跳过但原地不动**：隐藏行停留在旧位置，B6 的 extent 遍历**不查可见性** ⇒ 虚增滚动范围；
- 隐藏行若停在可见区：**B5 无可见性过滤的 HitTest 会命中它** ⇒ 幽灵点击。

⇒ **定案（契约 C-VIS）**：ListLayout / GridLayout 只排列 `IsVisible()` 的直接子控件（按 `ChildAt` 顺序取**可见序号**）；**不可见子控件停泊到 (0, −rowHeight)**（Grid：−cellHeight）——三效合一：① 不占可见槽位；② `y + h = 0` ⇒ 对 extent 的贡献经 B6 负向钳 0 **归零**；③ 停泊点在内容坐标系 y < 0，滚动偏移恒 ≥ 0 ⇒ **不可被命中**（B5 的补充防御）。

### 3-④ **D9 契约化**：两个新 Layout **只调 `SetPosition`**，全路径零 `SetSize`（对照 B4：Vertical 会 `SetSize`——又一处本质差异）；child 尺寸归消费者（Row Panel 自治）。T25-6 以 pre/post 断言盯防。

### 3-⑤ **D3 定案：GridLayout = 固定 cell v1** + 自适应列宽 = 重启条件

评审建议的「B 消费者真实布局 spike」**无法执行**——B（desktopnest M2）未实现，无真实需求可取证。诚实处置：

- 本 Phase 落 **`GridLayout(columns, cellWidth, cellHeight, spacing = 0)` 固定 cell 核心**——排列数学（`row = k / columns, col = k % columns`，末行左对齐不居中）在固定/自适应两种语义下**完全相同**；
- **自适应列宽**（按父宽 ÷ 列宽推导列数）= **重启条件**：desktopnest M2 立项时以真实需求定（扩展点 = 列数/列宽的**来源**，排列核心不动）；
- 理由 = 条 95②：不为未实现的消费者发明布局策略（投机抽象）。

### 3-⑥ **其余定案**

| # | 定案 |
|---|---|
| **参数** | `ListLayout(int rowHeight, int spacing = 0)` · `GridLayout(int columns, int cellWidth, int cellHeight, int spacing = 0)`——int 系与 B10 同族；`FRAMEWORK_ASSERT` 行高/单元 ≥ 1、列数 ≥ 1、spacing ≥ 0 |
| **定位公式** | List：可见序号 k ⇒ `(0, k × (rowHeight + spacing))`，跨轴 x 恒 0（D5：边距归 Panel）。Grid：`(col × (cellW + spacing), row × (cellH + spacing))` |
| **空容器** | 0 可见子控件 ⇒ Arrange no-op；extent 经停泊归零 ⇒ `(0, 0)`（空过滤结果 = 不可滚动的空白 ✓） |
| **ScrollView** | 实现**零改动**（D6）；`ScrollView.h:21` 的「不做 ListBox」措辞精确化 = 详设的文档改动项 |
| **ModelProbe 迁移** | 行池结构保留（D8）；`SetPosition`/`SetContentExtent` 手算删除；`GetContentView().SetLayout(make_unique<ListLayout>(28))` + 变更后两步模式（3-①） |

---

## 4. 接口草案（新增 2 公共头；既有公共面零改动）

```cpp
// include/ECDI/Layout/ListLayout.h（新增；include/ECDI/Layout/Layout.h）
namespace ECDI{

/// @brief 索引排列布局（Phase 25）：可见子控件按序纵向排列，行几何固定
/// @details 与 VerticalLayout 的分工（初设 §3-②）：空间分配 vs 索引排列。
/// 契约（D9）：只 SetPosition、不改 child Size；不可见子控件停泊 (0, -rowHeight)
/// （C-VIS——不占槽位 / extent 贡献归零 / 不可命中）。
class ListLayout : public Layout{
public:
    explicit ListLayout(int rowHeight, int spacing = 0);
    void Arrange(Widget& parent) override;
private:
    int m_rowHeight;   ///< ≥ 1（DIP）
    int m_spacing;     ///< ≥ 0
};

}
```

```cpp
// include/ECDI/Layout/GridLayout.h（新增）
namespace ECDI{

/// @brief 网格排列布局（Phase 25）：可见子控件按序填充固定 cell 网格，末行左对齐
/// @details 自适应列宽 = 重启条件（初设 §3-⑤——随 desktopnest M2 立项取证）。
class GridLayout : public Layout{
public:
    GridLayout(int columns, int cellWidth, int cellHeight, int spacing = 0);
    void Arrange(Widget& parent) override;
private:
    int m_columns, m_cellWidth, m_cellHeight, m_spacing;
};

}
```

内部实现 `src/Layout/ListLayout.cpp` / `GridLayout.cpp`（CMake 0 改动——GLOB_RECURSE）。

---

## 5. 影响面

| 项 | 明细 |
|---|---|
| **新增** | 公共头 ×2（`ListLayout.h` / `GridLayout.h`）+ 内部 cpp ×2 |
| **改动** | `src/Tests/LayoutTests.cpp`（T25 承载）· `examples/ModelProbe/ModelProbe.cpp`（迁移，批三） |
| **零改动** | `Layout.h` / `VerticalLayout` / `HorizontalLayout` / `ScrollView` / `ScrollContent` / `Widget` · CMake |
| **规模口径** | 公共头 **92 → 94** · 公共 API **+2 类型**（构造函数；既有 API 零改动）· 用例 **297 → ~308**（估 +11） |

---

## 6. 测试大纲（T25-1..T25-11；承载 = `LayoutTests.cpp` 扩展 + `ModelProbeTests.cpp`）

| # | 用例 | 对应 |
|---|---|---|
| **T25-1** | ListLayout：N 个可见子控件 → `position == (0, i × (rowH + spacing))`，与手算逐位一致（A1） | A1 |
| **T25-2** | 不可见子控件：不占槽位（可见序号连续）+ **停泊位 = (0, −rowHeight)**（C-VIS） | A1 |
| **T25-3** | 停泊子控件不可命中：内容坐标 y<0 处 `HitTest` = null（B5 防御） | A6 |
| **T25-4** | 组合：ListLayout 摆好 → `UpdateContentExtent()` = `(maxX, N × rowH)`；含 spacing 时 = `N × rowH + (N−1) × spacing` | A3 |
| **T25-5** | GridLayout：M 列 × ⌈N/M⌉ 行位置正确；**末行左对齐**（不足列不居中/不越界）（A2） | A2 |
| **T25-6** | **D9 盯防**：Arrange 前后逐 child `GetSize()` 逐位不变（List + Grid 双测） | A6 |
| **T25-7** | 负坐标既有行为：Arrange 后手动 `SetPosition(-10,-20)` → `UpdateContentExtent()` 钳 0（沿 B9 的 negative 用例语义，确认零改动） | A3 |
| **T25-8** | 空容器 / 全隐藏：Arrange no-op；extent = (0,0) | A3 |
| **T25-9** | 零回归：既有 Vertical/Horizontal 用例全绿（不改实现的结构性保证 + 回归锚） | A4 |
| **T25-10** | **ModelProbe 迁移结构断言**：`row[i].position.y == i × 28` · `height == 28` · `contentExtent == n × 28`（评审 §14） | A5 |
| **T25-11** | 目视（用户侧 @125%）：ModelProbe 列表观感 / 滚动 / 勾选 / 过滤与迁移前一致 | A5 |

---

## 7. 实现顺序（三批）

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批一** | `ListLayout.{h,cpp}` + T25-1..4, 6(List), 7, 8, 9 | 既有 297 全绿 + 新用例绿 |
| **批二** | `GridLayout.{h,cpp}` + T25-5, 6(Grid) | 全绿 |
| **批三** | ModelProbe 迁移（K2/K3 手算删除）+ T25-10 + T25-11 目视 | 四链全绿 + 用户目视 |

## 8. 开放项 / 局限

| # | 项 |
|---|---|
| **O1** | 「每行拉伸到父宽」——D9 禁止悄悄发生；若真实需求出现 = 显式可选策略立项（重启条件登记） |
| **O2** | GridLayout 自适应列宽 = 重启条件（desktopnest M2 立项时取证；扩展点 = 列数/列宽来源） |
| **O3** | `ScrollView.h:21` 措辞精确化（「不做**数据控件**语义；布局容器由 ListLayout/GridLayout 承担」）= 详设的文档改动项（公共头注释，非 API） |
| **L1** | 停泊位 `(0, −rowHeight)` 是布局与 ScrollView 钳 0 语义的**协同契约**——单独使用 ListLayout（无 ScrollView）时隐藏子控件在负坐标区（无 viewport 概念则无副作用，如实记录） |
| **L2** | 键盘导航 / 双击 / 当前项概念 = 需求 §2.1 明确无消费者，本 Phase 不设计 |

## 9. 修订记录

- **v1.0**（2026-10-02）初稿。**取证重心 = 接线机制**（B1–B10 带行号）：**B2 Arrange 每帧触发链**（渐进语义——无 Layout 节点保持手摆）· **B3 SetLayout 在 Widget 基类**（ScrollContent 自动成为布局宿主，零改动）· ★★ **B4/B5/B6 三事实联合推出 C-VIS 停泊契约**（Vertical 不跳过不可见 / HitTest 无可见性过滤 / UpdateContentExtent 遍历全量 ⇒ 隐藏行必须「跳过 + 停泊 (0, −rowHeight)」三效合一）· **B9 negative 用例 = A3 既有行为回归锚**。**定案**：D4 两步模式（Arrange 显式 + UpdateContentExtent；不改签名）· ListLayout 不读 parent 尺寸（ScrollContent 尺寸=extent 的循环依赖规避——vs VerticalLayout 正式理由之一）· GridLayout 固定 cell v1 + 自适应列宽重启条件（评审 spike 建议的诚实处置：B 未实现无法取证）· D9 契约化。**接口**：两公共头草案 · 影响面（92 → 94 · API +2 类型 · 用例 297 → ~308 · CMake 0）· **T25-1..T25-11** · 三批 · O1–O3 · L1–L2。**待评审。**
