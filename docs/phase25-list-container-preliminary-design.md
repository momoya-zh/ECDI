# Phase 25 · 列表 / 网格容器 —— 初步设计（v1.0）

> 来源：需求确认稿 `phase25-list-container-requirements.md` **v1.1 ✅ 已通过**（评审「原则上通过，小修后进初设」；初设必答 5 件见其 §1.5/§6）
> 状态：**v1.1**（2026-10-02）——✅ **评审通过（修 C-VIS 后可进详设）**（外部评审结论：「原则上通过，但需要修正 C-VIS 停泊策略后再进入详细设计」；★ **C-VIS v2 = 结果契约与实现策略分离**——v1 的停泊位 `(0, −rowHeight)` 存在两处实质漏洞（child 高度可 ≠ rowHeight 违 D9；**x 方向从未归零**），修复 = 按 child **自身尺寸**停泊 `(−w, −h)` 使右下角恰落 (0,0)；处置见 **§1.1**）
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

### 1.1 ★ 外部评审处置（第一轮，2026-10-02）

> 评审结论：**「初步设计原则上通过，但需要修正 C-VIS 停泊策略后再进入详细设计」**——架构骨架（布局层 / 两步模式 / D9 / 固定 cell / ModelProbe 迁移）**全部获认可**；唯一实质修正 = **C-VIS**。

| 评审节 | 内容 | 处置 |
|---|---|---|
| §一·二 | ★★ **C-VIS v1 停泊位 `(0, −rowHeight)` 两处实质漏洞**：① **D9 不改 child Size ⇒ child 高度可 ≠ rowHeight**（rowH=28、child h=40 ⇒ y+h = 12 > 0，仍贡献 extent——与「y+h = 0」的断言直接矛盾）；② **x 方向从未归零**（x=0 + width=100 ⇒ extent.width 贡献 100）⇒ 「三效合一」实际不成立 | ✅ **采纳** → **C-VIS v2**（§3-③ 重写：结果契约 C-VIS-1..3 与实现策略 C-VIS-4 分离；停泊位改 **`(−child.GetWidth(), −child.GetHeight())`**——右下角恰落 (0,0)，由 `GetSize()` 自保证，不依赖行几何与 child 尺寸的关系） |
| §三 | **修复方案**：按 child 自身尺寸停泊 = 与 D9 自洽（不需要知道 rowHeight 与 child 尺寸是否一致）；契约表述 = 「不可见 child 移动到自身几何范围的左上负区，使其右下角落在 (0,0)」 | ✅ **采纳**（同上） |
| §四 | **T25-2/3 改测「效果」而非「坐标技巧」**：C-VIS 的真保证 = 「隐藏 child 不占布局槽位 / 不扩大 extent / 不进入可命中区域」；停泊算法是实现策略 | ✅ **采纳** → T25-2 改效果断言（`x+w ≤ 0 ∧ y+h ≤ 0`）· T25-3 改行为断言（可见区 HitTest ≠ hiddenChild） |
| §五 | **C-VIS 拆成结果契约 + 实现策略四层**（C-VIS-1 排列语义 / 2 extent / 3 hit-test / 4 当前实现策略）——将来 ScrollView 若自带可见性过滤，可换实现不推翻语义 | ✅ **采纳** → §3-③ 重写为四层结构 |
| §六 | D4 两步模式获认可（不为 extent 让 Layout 横向耦合 ScrollView） | ✅ 采纳（未改动） |
| §七 | **D4 补「调用责任」**：凡可能改变 ScrollContent 子树**几何/结构**的变更（含 child `SetSize`、Label 内容变化等），变更后由**消费者**负责 `Arrange()` + `UpdateContentExtent()`——否则「孩子尺寸变了 / extent 旧了」 | ✅ **采纳** → §5 组合契约增补 |
| §八 | ListLayout 只 SetPosition 获认可；T25-6 双向 size 断言是好防回归点 | ✅ 采纳（未改动） |
| §九 | GridLayout 固定 cell v1 获认可；自适应列宽 = M2 真实需求出现时重新立项（非「未来一定支持」） | ✅ 采纳（未改动） |
| §十 | **cell 参数语义须显式**：`cellWidth/cellHeight` 是**布局槽位的几何间距，不是 child 尺寸约束**（D9 禁止强制 SetSize；child 溢出 cell 目前**不解决**，详设写明即可） | ✅ **采纳** → §4 接口注释增补 |
| §十一 | 空容器 / 全隐藏在新停泊下自然成立（`max(x+w) = 0, max(y+h) = 0` ⇒ extent = (0,0)） | ✅ 确认（T25-8 保留） |
| §十二 | **T25-3 测得太弱**（只测 y<0）→ 改测行为（隐藏 child 的 bbox 在负区 + 可见区无命中） | ✅ 采纳（同 §四） |
| §十三 | **只处理直接 children、不递归孙节点**——正式成文（与 Vertical/Horizontal 同族约束，组合布局的前提） | ✅ **采纳** → §4 接口注释增补 |
| §十四 | ModelProbe 迁移方案无异议 | ✅ 采纳（未改动） |
| §十五 | 测试数 `~308` 到详设须分解为 ListLayout X + GridLayout Y + ModelProbe Z 的实现交接清单 | ✅ 采纳 → 详设待办登记（§8 O4） |
| §十六 | **新增 T25-12「非等尺寸 child」**：rowH=28、children 100×20 / 200×50 / 150×28 ⇒ y = 0/28/56、size 全不变、extent = 200×78——同时锁定「不改 Size」+「extent 用真实 child geometry」，可直接暴露 C-VIS 漏洞 | ✅ **采纳** → **T25-12** 新增 |
| §十七 | **§3-① 删「零成本」**：Arrange 至少遍历 + 判可见性 + 算位 + SetPosition；想表达的是「**无需额外 extent 计算**」，非「运行时成本为零」 | ✅ **采纳** → §3-① 措辞修正 |

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
    Window::OnPaint → Arrange()（B2 每帧自动维持——**无需额外的 extent 计算**，布局仅执行
    既有几何位置更新；「零成本」措辞已删，评审 §十七：遍历 + 判可见性 + SetPosition 的
    运行时成本照常发生）
```

⇒ 消费者的两处手算消失：K2（`i × 行高`）由布局接管；K3（`n × 行高`）由 `UpdateContentExtent()` 接管（B6：非 Σ，包围盒右下角）。

### 3-② **ListLayout vs VerticalLayout——为什么新增类而不是增强**（评审必答 ④，正式成文）

1. **概念不同**：Vertical = **空间分配布局**（读 `parent.GetWidth()/GetHeight()` 按 stretch/fill/child size 动态分配）；List = **索引排列布局**（位置 = f(可见序号 × 固定行几何)，**不读 parent 尺寸**）。
2. **循环依赖规避**：挂载点是 `ScrollContent`，而**其尺寸 = 内容 extent**（`ScrollView.h:57`）——若布局读 parent 尺寸（fill 语义），extent → 尺寸 → extent 成环；ListLayout 的固定行几何**天然无环**。
3. **参数集不相容**：Vertical 的 `stretch/fillCrossAxis` 对 List 无意义（合并 = 「某参数在某模式下无效」，违「一个概念一个词」）。
4. **可见性语义不同**：Vertical 全量分配（B4 既有行为，A4 零回归不动它）；List 需要**跳过不可见**（见 3-③）。

### 3-③ **不可见子控件语义（C-VIS v2，本稿最重要的契约——v1.1 依评审重写）**

行池 + 过滤（K4/K5）与两个既有机制的交互**必然出错**，取证如下：

- 布局若**全量排列**（含隐藏行）：隐藏行占用 index ⇒ 可见行错位（违背过滤语义）；
- 布局若**跳过但原地不动**：隐藏行停留在旧位置，B6 的 extent 遍历**不查可见性** ⇒ 虚增滚动范围；
- 隐藏行若停在可见区：**B5 无可见性过滤的 HitTest 会命中它** ⇒ 幽灵点击。

**C-VIS v1 的停泊位 `(0, −rowHeight)` 存在两处实质漏洞（评审 §一，v1.1 修正）**：
① **D9 不改 child Size ⇒ child 高度可 ≠ rowHeight**（rowH=28、child h=40 ⇒ 停泊后 y+h = 12 > 0，仍贡献 extent——「y+h = 0」的断言与 D9 直接矛盾）；
② **x 方向从未归零**（x=0 + width=100 ⇒ extent.width 贡献 100）——「三效合一」实际不成立。

⇒ **v1.1 定案：结果契约与实现策略分离**——

| 层 | 契约 |
|---|---|
| **C-VIS-1 排列语义** | ListLayout / GridLayout 只为 `IsVisible() == true` 的直接子控件分配**连续布局序号**（visible #0 → slot 0，#1 → slot 1……） |
| **C-VIS-2 extent 结果** | 不可见 child 在 `UpdateContentExtent()` 下**不得增加** `contentExtent.width` / `.height` |
| **C-VIS-3 hit-test 结果** | 不可见 child **不得在 ScrollView 当前内容坐标范围内形成可命中区域** |
| **C-VIS-4 实现策略（当前）** | 停泊到**自身几何范围的左上负区**：`SetPosition(−child.GetWidth(), −child.GetHeight())` ⇒ 右下角恰落 `(0, 0)`，`x+w ≤ 0 ∧ y+h ≤ 0` 由 `GetSize()` 自保证——**不依赖行几何与 child 尺寸的关系**（与 D9 自洽） |

★ 分层的价值：C-VIS-1..3 是**行为保证**（T25 测这个），C-VIS-4 是**实现策略**（可替换）——将来 `ScrollView` 若自带可见性过滤，换实现不推翻 ListLayout 语义。

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
| **cell 参数语义（评审 §十）** | `cellWidth/cellHeight` = **布局槽位的几何间距，不是 child 尺寸约束**——child 大于 cell 时溢出 cell 由消费者自治（D9 禁止强制 SetSize），本 Phase 不解决、详设写明 |
| **只处理直接 children（评审 §十三）** | 两个新 Layout 与 Vertical/Horizontal 同族：**只处理 `parent` 的直接 children，不递归孙节点**——组合布局的前提（Row 内部由 Row 自己的布局/手摆负责） |
| **空容器** | 0 可见子控件 ⇒ Arrange no-op；全部停泊后 `max(x+w) = 0, max(y+h) = 0` ⇒ extent `(0, 0)`（C-VIS-2 的直接推论） |
| **ScrollView** | 实现**零改动**（D6）；`ScrollView.h:21` 的「不做 ListBox」措辞精确化 = 详设的文档改动项 |
| **ModelProbe 迁移** | 行池结构保留（D8）；`SetPosition`/`SetContentExtent` 手算删除；`GetContentView().SetLayout(make_unique<ListLayout>(28))` + 变更后两步模式（3-①） |
| **调用责任（评审 §七）** | **凡可能改变 ScrollContent 子树几何/结构的变更**（加删行、child `SetSize`、内容变化引起 row 尺寸变化等），变更后由**消费者**负责 `Arrange()` + `UpdateContentExtent()`——不执行则滚动范围陈旧（调用契约，非实现 bug） |

---

## 4. 接口草案（新增 2 公共头；既有公共面零改动）

```cpp
// include/ECDI/Layout/ListLayout.h（新增；include/ECDI/Layout/Layout.h）
namespace ECDI{

/// @brief 索引排列布局（Phase 25）：可见子控件按序纵向排列，行几何固定
/// @details 与 VerticalLayout 的分工（初设 §3-②）：空间分配 vs 索引排列。
/// 契约：C-VIS（v1.1 四层——只排列可见子控件；不可见者停泊到自身 bbox 负区，
/// 右下角落 (0,0)）；D9 只 SetPosition 不 SetSize；★ 只处理 parent 的直接
/// children，不递归孙节点（与 Vertical/Horizontal 同族约束）。
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
/// @details ★ cellWidth/cellHeight = 布局槽位的几何间距，**不是 child 尺寸约束**
/// （D9：child 大于 cell = 溢出，由消费者自治）。自适应列宽 = 重启条件（初设 §3-⑤）。
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
| **T25-2** | 不可见子控件：不占槽位（可见序号连续）+ **效果断言**（C-VIS-2：`x + width ≤ 0 ∧ y + height ≤ 0`——测行为不测坐标技巧，v1.1 修正） | A1 |
| **T25-3** | 停泊子控件不可命中（**行为断言**，v1.1 修正）：可见区 `HitTest ≠ hiddenChild`、原可见位置无命中、隐藏 bbox 整体位于内容原点负区 | A6 |
| **T25-4** | 组合：ListLayout 摆好 → `UpdateContentExtent()` = `(maxX, N × rowH)`；含 spacing 时 = `N × rowH + (N−1) × spacing` | A3 |
| **T25-5** | GridLayout：M 列 × ⌈N/M⌉ 行位置正确；**末行左对齐**（不足列不居中/不越界）（A2） | A2 |
| **T25-6** | **D9 盯防**：Arrange 前后逐 child `GetSize()` 逐位不变（List + Grid 双测） | A6 |
| **T25-7** | 负坐标既有行为：Arrange 后手动 `SetPosition(-10,-20)` → `UpdateContentExtent()` 钳 0（沿 B9 的 negative 用例语义，确认零改动） | A3 |
| **T25-8** | 空容器 / 全隐藏：Arrange no-op；全部停泊后 extent = (0,0)（C-VIS-2 的直接推论——新停泊下自动成立，评审 §十一确认） | A3 |
| **T25-9** | 零回归：既有 Vertical/Horizontal 用例全绿（不改实现的结构性保证 + 回归锚） | A4 |
| **T25-10** | **ModelProbe 迁移结构断言**：`row[i].position.y == i × 28` · `height == 28` · `contentExtent == n × 28`（评审 §14） | A5 |
| **T25-11** | 目视（用户侧 @125%）：ModelProbe 列表观感 / 滚动 / 勾选 / 过滤与迁移前一致 | A5 |
| **T25-12** | ★ **非等尺寸 child**（v1.1 新增，评审 §十六）：rowH=28、children 100×20 / 200×50 / 150×28 ⇒ `y = 0 / 28 / 56`、**size 全不变**、`UpdateContentExtent = (200, 78)`——同时锁定 D9（不改 Size）+ B6（extent 用真实 child geometry），可直接暴露 C-VIS v1 的停泊漏洞 | A1 / A3 |

---

## 7. 实现顺序（三批）

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批一** | `ListLayout.{h,cpp}` + T25-1..4, 6(List), 7, 8, 9, 12 | 既有 297 全绿 + 新用例绿 |
| **批二** | `GridLayout.{h,cpp}` + T25-5, 6(Grid) | 全绿 |
| **批三** | ModelProbe 迁移（K2/K3 手算删除）+ T25-10 + T25-11 目视 | 四链全绿 + 用户目视 |

## 8. 开放项 / 局限

| # | 项 |
|---|---|
| **O1** | 「每行拉伸到父宽」——D9 禁止悄悄发生；若真实需求出现 = 显式可选策略立项（重启条件登记） |
| **O2** | GridLayout 自适应列宽 = 重启条件（desktopnest M2 立项时取证；扩展点 = 列数/列宽来源） |
| **O3** | `ScrollView.h:21` 措辞精确化（「不做**数据控件**语义；布局容器由 ListLayout/GridLayout 承担」）= 详设的文档改动项（公共头注释，非 API） |
| **O4** | 用例数 `~308` 到详设须**分解为实现交接清单**（ListLayout X + GridLayout Y + ModelProbe Z，评审 §十五） |
| **L1** | 停泊到自身 bbox 负区是布局与 `UpdateContentExtent()` 包围盒语义的**协同契约**——单独使用 ListLayout（无 ScrollView）时隐藏子控件在负坐标区（无 viewport 概念则无副作用，如实记录） |
| **L2** | 键盘导航 / 双击 / 当前项概念 = 需求 §2.1 明确无消费者，本 Phase 不设计 |

## 9. 修订记录

- **v1.1**（2026-10-02）**评审第一轮处置 —— ✅ 修 C-VIS 后可进入详细设计**。① **评审结论**：「原则上通过，但需要修正 C-VIS 停泊策略后再进入详细设计」；架构骨架（布局层 / 两步模式 / D9 / 固定 cell / ModelProbe 迁移 / 测试分层）全部获认可。② ★★ **C-VIS 修正（唯一实质修正，v1 核心契约有逻辑漏洞）**：**漏洞 ①** = D9 不改 child Size ⇒ child 高度可 ≠ rowHeight，停泊 `(0, −rowHeight)` 后 `y+h` 仍 > 0（与「y+h = 0」断言矛盾）；**漏洞 ②** = x 方向从未归零（`x=0 + width` 仍贡献 extent.width）——「三效合一」实际不成立。**修复** = 停泊位改 **`(−child.GetWidth(), −child.GetHeight())`**（右下角恰落 (0,0)，由 GetSize() 自保证、与 D9 自洽、不依赖行几何）。③ ★★ **C-VIS 重构为四层结果契约**：**C-VIS-1** 排列语义（只给可见子控件连续槽位）/ **C-VIS-2** extent 结果（不增宽高）/ **C-VIS-3** hit-test 结果（可见区不可命中）/ **C-VIS-4** 实现策略（当前 = 自身 bbox 负区停泊；可替换）——**测试测行为（1..3），不测坐标技巧（4）**。④ ★ **测试大纲修订**：T25-2 改效果断言（`x+w ≤ 0 ∧ y+h ≤ 0`）· T25-3 改行为断言 · **新增 T25-12**（非等尺寸 child——同时锁定 D9 与 extent 真实几何）· T25-8 确认新停泊下自动成立。⑤ ★ **增补**：§3-⑥ **调用责任**（几何/结构变更后由消费者执行两步）· **cell 参数 = 槽位间距非尺寸约束** · **只处理直接 children 不递归孙节点** · §3-① 删「零成本」措辞 · §8 新增 **O4**（用例数详设分解）。⑥ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-10-02）初稿。**取证重心 = 接线机制**（B1–B10 带行号）：**B2 Arrange 每帧触发链**（渐进语义——无 Layout 节点保持手摆）· **B3 SetLayout 在 Widget 基类**（ScrollContent 自动成为布局宿主，零改动）· ★★ **B4/B5/B6 三事实联合推出 C-VIS 停泊契约**（Vertical 不跳过不可见 / HitTest 无可见性过滤 / UpdateContentExtent 遍历全量 ⇒ 隐藏行必须「跳过 + 停泊 (0, −rowHeight)」三效合一）· **B9 negative 用例 = A3 既有行为回归锚**。**定案**：D4 两步模式（Arrange 显式 + UpdateContentExtent；不改签名）· ListLayout 不读 parent 尺寸（ScrollContent 尺寸=extent 的循环依赖规避——vs VerticalLayout 正式理由之一）· GridLayout 固定 cell v1 + 自适应列宽重启条件（评审 spike 建议的诚实处置：B 未实现无法取证）· D9 契约化。**接口**：两公共头草案 · 影响面（92 → 94 · API +2 类型 · 用例 297 → ~308 · CMake 0）· **T25-1..T25-11** · 三批 · O1–O3 · L1–L2。**待评审。**
