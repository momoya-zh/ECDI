# Phase 25 · 列表 / 网格容器 —— 需求确认

> **版本：v1.1**（2026-10-02）｜**状态：✅ 评审通过——可进入 Preliminary Design**（外部评审结论：「**原则上通过，小修后进入 Preliminary Design**」；★ 初设须钉死 5 件事，处置见 **§1.5**；★ D1「布局层」方向获明确支持）
> **立项来源**（★ 三处登记 = 同一件事的三个视角，须同时对齐）：
> `framework-defect-audit.md` §4 **D-5** · `roadmap-deferred.md` §7.9 **#42**（顺位 **⑤**）· `desktopnest-roadmap.md` §5 **G-7**
> **前序**：Phase 24（`DrawLine` 线段抗锯齿）已全链收口 ⇒ 框架侧**待做区仅余本条**（记账区另有 #44–#48）。

---

## 1. 背景

### 1.1 三处登记（同一件事）

| 账本 | 条号 | 原文要点 |
|---|---|---|
| 审计 | **D-5** | 「**无列表 / 网格容器**（`ListBox` / `ListView` / `TreeView` 均不存在）」· 消费者证据 = `ScrollView.h:21` 自述 + ModelProbe 手工行池 · 定性「缺失能力，**第二个消费者已出现**」 |
| 排期 | **#42** | 「**列表 / 网格容器**」· 顺位 ⑤ · ★ **与 #33（虚拟化）解耦**：「先要『容器』，虚拟化是后一步」· 先例 = `ModelProbe.cpp:715-753` |
| 应用侧 | **G-7** | desktopnest **M2**「框内文件**网格**」需要 · 「**非阻断**（有先例可抄）；但属**第二个消费者**」 |

★ **结论**：这不是"新功能"，是**两个真实消费者已经出现**导致的抽象时机成熟（条 95②「第二个真实消费者出现才抽」）。

### 1.2 两个消费者

| # | 消费者 | 形态 | 状态 |
|---|---|---|---|
| **A** | **ModelProbe 模型列表** | 纵向行列表（勾选框 + id + meta） | ✅ **已实现**（手工版，`ModelProbe.cpp:343-362` 组装 / `:719-767` 行池） |
| **B** | **desktopnest M2「框内文件网格」** | 网格（图标 + 文件名） | ⬜ 未实现（应用侧整体推迟；M2 依赖 G-2，而 G-2 已由 Phase 21 收口） |

★ **A 是"抄得来的先例"，B 是"新形态（网格）"** ⇒ 本 Phase 必须同时考虑**纵向**与**网格**两种排列。

### 1.3 ★ 必须先解释的边界冲突

`ScrollView.h:17-21` **有意划了一条界**（Phase 15 职责四层，原文）：

> 本类**只做** viewport + offset + extent + 滚动交互：
> - **不接管** `Arrange`（偏移不参与布局）；
> - **不提供**样式；
> - **不推导** 内容尺寸以外的语义（**不做 ListBox / 虚拟化 / 框选**——见需求稿 §4）。

⇒ **本 Phase 表面上与之冲突，实则不冲突**，理由三条：

1. **那是"禁止 `ScrollView` 自己变万能容器"**，不是"禁止框架提供列表容器"——`ScrollView` **保持一行不改**是其价值（D1 选「视口偏移」而非「移动子控件」的核心理由，Phase 15 §3.3）。
2. 本条要做的是**另一个正交的容器**，与 `ScrollView` **组合**使用（1.4 与 §3 展开）。
3. ★ **Phase 15 §4 的原话是「不做 ListBox」，而本条要做的是「列表/网格**布局**容器」**——两者范围不同（见 D1）。**本稿必须把这条界线写得比原句更精确**，否则会与 Phase 15 的结论打架。

### 1.4 与「虚拟化」解耦（#33）

- **虚拟化** = 只创建"可见窗口内"的行控件（`Σ items` ≫ `Σ visible`）。
- **本条** = 「给**已经存在**的子控件一个按序排列 + 尺寸推导的容器」。
- ⇒ ★ **不做虚拟化**（#42 原文已钉死）：它需要"按需创建/回收 + 滚动位置 ↔ 数据索引映射"，属**另一条能力**，且**当前两个消费者都不需要**（ModelProbe 行数 = 模型数，量级 ~20；框内文件量级 ~10²）。

### 1.5 ★ 外部评审处置（第一轮，2026-10-02）

> 评审结论：**「Phase 25 Requirements v1.0：原则上通过，小修后进入 Preliminary Design」**。★ 评审对 **D1 选布局层明确支持**，并给出架构分层判词（保持为本 Phase 的界碑）：**「Layout 决定 Widget 怎么摆；数据控件决定数据怎么变成 Widget」**。★ 评审收官清单 = **初设须钉死的 5 件事**（见下表末行）。

| 评审节 | 内容 | 处置 |
|---|---|---|
| §1–§2 | K1–K8 拆解使「List 控件 vs 布局能力」的区别清晰；**不做 ListView** 防止膨胀成 Model/Factory/Selection/Recycling | ✅ **采纳（未改动）** |
| §2 | D1 = 布局层方向正确；本 Phase 输入是**已存在的 Widget 子树**，不是数据集合 | ✅ **采纳（未改动）** |
| §3 | **D3 的 Grid 参数模型暂缓冻结**——cell **固定尺寸 vs 按父宽自适应列宽**是两种核心语义（desktopnest 文件网格更接近后者；候选形态含 `GridLayout(columnWidth, rowHeight, spacing)`）⇒ 需求阶段只定「两个类」，构造参数留初设的 B 消费者 spike | ✅ **采纳** → **D3** 增补（`GridLayout` 参数**不在本阶段拍死**） |
| §4–§5 | **D4 = 最大的技术问题**：「Arrange 后自动提交 extent」若走扩 `Layout::Arrange` 签名的路，会让极简的 `Layout` 抽象知道 ScrollView/ContentExtent ⇒ **污染抽象**；倾向 = 候选 **①**（`Arrange` 摆好后消费者调一次 `UpdateContentExtent()`），职责仍分离（Layout 管摆、ScrollView 管 extent） | ✅ **采纳** → **D4** 定案倾向 ①，**不改 `Layout::Arrange()` 签名** |
| §6 | **§3.1 措辞**：「内容尺寸自动推导」会被读成「Arrange 后 ScrollView 自动知道 extent」，与 D4 实际选择不符 | ✅ **采纳** → §3.1-2 措辞已改（见下） |
| §7 | **A3 补负坐标语义**：正常排列下 min = 0；「负向钳 0」是 `ScrollView::UpdateContentExtent()` 的**既有职责**，**ListLayout 不承担钳位**；验收加特殊 child（`SetPosition(-10,-20)`）验证既有行为不变 | ✅ **采纳** → **A3** 增补 |
| §8 | D5 边距归外层 `Panel::padding`（Phase 17 既有语汇）正确——避免 Panel/List/ScrollView 三层 padding 概念 | ✅ **采纳（未改动）** |
| §9 | D8 保留 Row Panel 正确：行 = 自然坐标空间 / hover 单元；ListLayout 布局 Row，Row 内部自布局 | ✅ **采纳（未改动）** |
| §10–§11 | ★★ **新增边界原则（v1.1 新增 D9）**：ListLayout/GridLayout **只操作直接子控件的 `SetPosition`，不改 child `Size`**（「每行拉伸到父宽」若成需求 = **显式可选策略**，不悄悄发生）；Grid 布局对象 = 直接子控件（Row Panel），**不深入其内部** | ✅ **采纳** → 新增 **D9** |
| §12 | ListLayout vs VerticalLayout 的重叠——支持**新增类**，但初设必须正式写出理由：**VerticalLayout = 空间分配布局（按 stretch/fill/child size 动态分配）；ListLayout = 索引排列布局（index × 固定行几何）**——两者概念不同 | ✅ **采纳** → 列入初设必答 |
| §13 | 不为 DRY 建 `IndexedLayout` 公共抽象——先两个简单类，重复自然出现再抽私有 helper | ✅ **采纳（未改动）** |
| §14 | **A5 增加结构化自动验收**：迁移前后 `row[i].position.y == i × 28` · `height == 28` · `contentExtent == N × 28`（人工目视只补视觉/滚动/勾选）——「看起来差不多但末行多 1px」由自动测试先抓 | ✅ **采纳** → **A5** 增补 |
| §15 | 影响面：公共头可能是 **+2**（ListLayout/GridLayout 各一头）而非 +1 ⇒ 不急着承诺单一数字 | ✅ **采纳** → §3.3 改「**+1 ~ +2**（取决 header grouping）」 |
| ★★ 收官 | **初设须钉死的 5 件事**：① D4 = 消费者调 `UpdateContentExtent()`、不改 `Arrange()` 签名；② ListLayout **只改 Position 不改 Size**；③ Grid cell **固定 vs 自适应列宽**（desktopnest 真实需求 spike）；④ **ListLayout 不复用/增强 VerticalLayout 的理由**正式成文；⑤ **A3/A5 结构化自动测试** | → 全部落 §4/§5，初设逐条收口 |

---

## 2. 现状勘察（全部带行号）

### 2.1 消费者 A：ModelProbe 手工做了什么

| # | 手工事项 | 证据 |
|---|---|---|
| **K1** | **行 = 一个 `Panel` + `CheckBox` + 2×`Label`，全部绝对定位手写坐标** | `ModelProbe.cpp:734-752`（`SetSize(600,28)` · `SetPosition(0, 0)/(32,0)/(420,0)` · 宽 `28/380/170`） |
| **K2** | **行位置手算** `y = i × kRowHeight` | `:737`（`i * kRowHeight`；`kRowHeight = 28.0f`，`:48`） |
| **K3** | **内容尺寸手算并显式提交** | `:767`（`m_scroll->SetContentExtent(600, n * kRowHeight)`） |
| **K4** | **行池复用**：先隐藏旧行，再按需复用或新建 | `:719-733`（`SetVisible(false)` 全隐藏 → `i < m_rows.size()` 则复用并更新文本，否则新建） |
| **K5** | **过滤靠 `SetVisible`** | `:300-303`（`kw.empty() \|\| id.find(kw) != npos`） |
| **K6** | **选择状态由 `CheckBox` 自己持有**，无框架级选择模型 | `:553-554`（`row.cb->SetChecked`）· `:589-591`（全选遍历 `IsChecked`） |
| **K7** | **装饰与滚动分离**：外层 `Panel`（背景/圆角/边框 + `padding=4`）→ 内层 `ScrollView` | `:343-362`（★ 注释说明 `padding=4` 是为了让勾选框躲开圆角弧线） |
| **K8** | 行挂在**内容根**下（不是 `m_list`） | `:756`（`m_scroll->GetContentView().AddChild(...)`） |

★ **无双击 / 无键盘导航 / 无"当前项"概念**（全库 grep `SetOnClick` 只命中按钮，`:229/254/265/315/330/432`）⇒ 本 Phase **不需要**为它们设计 API。

### 2.2 现有能力与边界

| 能力 | 现状 |
|---|---|
| `ScrollView` | viewport + offset + extent + 滚动交互 + 自带滚动条（Phase 15 R1/R2）；**不做**内容语义（`ScrollView.h:21`） |
| 内容尺寸 | **两入口且都显式**：`UpdateContentExtent()`（从子控件二维包围盒推导，D6 不自动挂钩，`ScrollView.h:65-70`）· `SetContentExtent(w,h)`（显式，`:78`） |
| `ScrollContent` | 内容坐标空间的根（**内部头**，`src/Widget/ScrollContent.h`，**不进公共头计数**） |
| `Layout` 基类 | ★ **极简**：`virtual void Arrange(Widget& parent) = 0;`（`Layout/Layout.h:13`）—— 只有一个纯虚 |
| 现有 `Layout` 实现 | `VerticalLayout`（stretch/spacing/fill）· `HorizontalLayout`；`padding`（Phase 17） |
| `Panel` | 可设 `SetLayout`（样式 + 布局宿主） |
| 公共头 | **92**（2026-09-29 复核后未变） |
| 测试用例 | **297**（Phase 24 收口后） |

### 2.3 缺口清单（K1–K8 里「框架本可代劳」的部分）

| 缺口 | 现状 | 可自动化性 |
|---|---|---|
| **G-a 按序摆放** | 消费者手算 `i × 行高`（K2） | ★ 高（纯排列） |
| **G-b 内容尺寸推导** | 消费者手算并调 `SetContentExtent`（K3） | ★ 高（`UpdateContentExtent` 已有，但**要求消费者先摆好位置**——鸡生蛋） |
| **G-c 网格排列** | **完全没有**（消费者 B 需要） | ★ 高 |
| **行池复用**（K4）· **过滤**（K5）· **选择**（K6）· **行内组装**（K1） | 消费者手工 | ⚠️ **低**（牵涉数据绑定与状态，见 D1） |

---

## 3. 范围

### 3.1 做（倾向）

1. **一个"按索引排列子控件"的容器能力**，覆盖**纵向列表**与**网格**两种排列。
2. **布局位置自动推导，并经既有 `UpdateContentExtent()` 从布局结果推导内容尺寸**（v1.1 措辞修正，评审 §6：原「内容尺寸自动推导」会被读成「Arrange 后 ScrollView 自动知道 extent」，与 D4 实际选择不符）——**消除消费者对排列坐标与内容尺寸数值的手工计算**（K2 的 `i × 行高` 与 K3 的 `n × 行高` 两处手算消失）。
3. **与 `ScrollView` 的组合用法**（钢领式文档 + 示例），使消费者 A 的 K2/K3 两处手算消失。
4. **`examples/ModelProbe` 迁移**为其第一个真实消费者（★ 与 Phase 15 迁 `ScrollView` 同款做法：先例即验收）。

### 3.2 ★ 非目标（明确排除，防范围蔓延）

| 排除项 | 理由 |
|---|---|
| **虚拟化**（按需创建/回收） | 与 #33 解耦（#42 原文）；两消费者都不需要（§1.4） |
| **框选**（rubber-band） | `ScrollView.h:21` 明排；无消费者 |
| **`TreeView`**（递归/展开折叠） | 无消费者；`desktopnest` §1 明写「嵌套**不允许**，恒为一层」 |
| **排序 / 列宽拖拽 / 表头** | 无消费者（ModelProbe 的过滤是应用侧搜索，不是列排序） |
| **数据绑定 / 项工厂 / 行池 API** | ⚠️ **见 D1**——倾向**本 Phase 不做**，理由 = 它把容器推向"数据控件"，与虚拟化边界模糊，且**消费者 A 已自证可手工**（K4/K5/K6） |
| **滚动条 / 视口 / 偏移** | 已是 `ScrollView` 的职责，**重复即为坏** |
| **样式（背景/圆角/边框）** | 已是 `Panel` 的职责（K7 的三层拆分保持） |

### 3.3 影响面预算（初估，待初设校准）

| 项 | 预估 |
|---|---|
| 公共头 | **92 → +1 ~ +2**（v1.1 依评审 §15 修正：`ListLayout` / `GridLayout` 若各占一头 = **94**；合一头 = **93**——header grouping 留初设） |
| 公共 API | 待 D1/D3 定；最小情形 = **+0**（只加一个 `Layout` 实现类） |
| 用例 | 297 → **~310**（估 +12，待详设定） |
| CMake | **0 改动**（`GLOB_RECURSE CONFIGURE_DEPENDS` 自动入库） |

---

## 4. 待决点（★ 每条给倾向，待评审）

> ⚠️ 判据（条 95）：先问「**要不要做**」，再问「**怎么做**」；分歧先回到定位表判。

### D1 ★★ **形态：布局层 vs 数据控件层**（本 Phase 最核心）

| 选项 | 内容 | 代价 |
|---|---|---|
| **(a) 布局层** | 新增一个 `Layout` 派生（如 `ListLayout` / `GridLayout`），只管「按序摆放 + 尺寸推导」；行控件由消费者自建 | ★ 小、正交、与 `ScrollView` 零耦合、与虚拟化天然解耦 |
| **(b) 数据控件层** | 新增 `ListView` 控件：绑定 `vector<T>` + 工厂 + 行池 + 选择模型 | ★ 大，且**越界到虚拟化/数据绑定**；消费者 A 的手工部分（K4–K6）**并非不可忍受** |
| **(c) 两者都做** | 先 (a)，(b) 留待第三个消费者 | 分两步 |

★ **倾向 (a)**，理由三条：① 与 `#42` 原文「先要『容器』」一致；② 与「不引入投机抽象」（条 95②）一致——**消费者 B 尚未实现**，其真实需求未知；③ 与 `ScrollView.h:21` 的界线冲突**最小**（(a) 甚至不碰 `ScrollView`）。

### D2 **命名**（条 95① 命名即语义 / 一个概念一个词）

- 若 D1 选 (a)：类名候选 `ListLayout`（纵向）· `GridLayout`（网格）—— ★ **与既有 `VerticalLayout` / `HorizontalLayout` 同族**，命名一致性好。
- ⚠️ **避免** `ListView` / `ListBox`：这两个词在 GUI 语境里**指数据控件**，用作布局类名会造成"一个概念两个词"。
- ★ **倾向**：`ListLayout` + `GridLayout`（两个，各司其职），而非一个带"模式"开关的类。

### D3 **纵向与网格：两个类还是一个类**

- ★ **倾向两个类**（同族 vs 策略）：`ListLayout(行高[, 间距])` 与 `GridLayout(列数, 单元宽, 单元高[, 间距])`——★ 判据 = 「一个概念一个词」+ 两者的**参数集不同**（行高 vs 列数×单元尺寸），合并会引入"某参数在某模式下无效"。
- ★★ **v1.1（评审 §3）：`GridLayout` 的参数模型暂缓冻结**——cell **固定尺寸** vs **按父宽自适应列宽**是两种核心语义（desktopnest 文件网格更接近后者 ⇒ 候选形态含 `GridLayout(columnWidth, rowHeight, spacing)`）⇒ 需求阶段只定「**两个类**」，构造参数由初设的 **B 消费者真实布局 spike** 定（评审 §3）。
- ⚠️ 待评审确认：B 消费者的网格是**等宽列**还是**自适应列宽**（影响 `GridLayout` 参数）。⇒ 已升格为初设必答（§1.5 收官清单 ③）。

### D4 **内容尺寸推导：谁算、何时算**

- 现状：`UpdateContentExtent()` 从**已摆好的**子控件推导（`ScrollView.h:65-70`）⇒ 消费者必须**先摆再算**（K2→K3 的顺序正是如此）。
- ★ **倾向**：新 `Layout` 的 `Arrange` **同时完成摆放与 extent 计算**，并通过既有 `SetContentExtent` 提交给 `ScrollView`。
- ⚠️ **待决**：`Layout::Arrange(Widget& parent)` 的签名**只给 parent**（`Layout/Layout.h:13`）⇒ 布局无法直接知道"我在 `ScrollView` 里"（`parent` 是 `ScrollContent`）。三条路：① 布局只摆放，extent 仍由 `ScrollView::UpdateContentExtent()` 推导（**零签名改动** ✓）；② 扩 `Layout` 接口（★ 动公共头，慎）；③ 消费者显式两步。
- ★ **倾向 ① 定案（v1.1，评审 §4–§5）**：`Arrange` 摆好子控件后，消费者调一次 `UpdateContentExtent()` 即可——**比现状省掉 K2 的手算**，且**零接口改动**。★★ **评审明确支持不为 extent 扩 `Layout` 接口**：那会让极简的 `Layout` 抽象开始知道 ScrollView/ContentExtent ⇒ **污染抽象**；候选 ① 的职责分离保持干净（Layout 管摆、ScrollView 管 extent）。

### D5 **间距 / 边距**

- ★ **倾向复用既有语汇**：间距用构造函数参数（与 `VerticalLayout(spacing, fill)` 同款），边距交给**外层 `Panel` 的 `padding`**（K7 的三层拆分不动）。

### D6 **是否影响 `ScrollView`**

- ★ **倾向两处都不改** `ScrollView` 实现；若需要，**只改文档**（`ScrollView.h:21` 那句「不做 ListBox」的措辞需在初设中精确化为「不做**数据控件**语义；布局容器由 `ListLayout` / `GridLayout` 承担」）。
- ⚠️ 措辞改动 = **公共头注释改动**（非 API），须在详设中登记。

### D7 **`examples/ModelProbe` 是否随本 Phase 迁移**

- ★ **倾向迁移**：与 Phase 15 迁 `ScrollView` 同款（先例即验收）；迁移后 K2/K3 消失，可作为 A 类验收的**结构性判据**。
- ⚠️ 迁移会改 `ModelProbe.cpp`（822 行）——需确认**测试用例不依赖 demo 内部结构**（Phase 15 已有一次同款迁移先例）。

### D8 **是否要把「行」的 `Panel` 也省掉**

- 现状每行是一个 `Panel`（K1）。★ **倾向不省**：行是子控件的**坐标容器 + 可能的点击/hover 单元**，省掉会让布局变成"扁平化子控件"，与既有"控件树"模型冲突（且 Phase 15 的 `ScrollContent` 已确立"坐标系下沉"的先例）。

### D9 **排列的边界原则**（v1.1 新增，评审 §10–§11）

- ★★ **ListLayout / GridLayout 只操作直接子控件的 `SetPosition`，不改 child `Size`**——`ListLayout = arrangement policy`，不是 `child sizing policy`；「每行拉伸到父宽」若成为真实需求，应作为**显式的可选布局策略**进入设计，不得悄悄发生。
- ★ **Grid 布局的对象 = 直接子控件（Row Panel）**，不深入其内部（`GridLayout` 决定 Panel 的 cell，Panel 自己决定 Image/Label 怎么摆）——与 D8 同族：**布局只碰直接子控件；子控件内部尺寸与布局由其自身负责**。
- 初设须把本原则写成契约并配盯防（对 `Layout` 既有语义的最小承诺面）。

---

## 5. 验收方向（A1–A7，待详设细化）

| # | 方向 | 类型 |
|---|---|---|
| **A1** | 纵向容器：N 个子控件按序排列，位置 = `i × 行高`（与手算逐位一致） | 自动化 |
| **A2** | 网格容器：M 列 × ⌈N/M⌉ 行的位置正确，末行不足列时不越界 | 自动化 |
| **A3** | 内容尺寸 = 二维包围盒右下角（与 `ScrollView` D6 定义**同式**，负向钳 0）。★ **v1.1 增补（评审 §7）**：「负向钳 0」是 `ScrollView::UpdateContentExtent()` 的**既有职责**，**ListLayout 自身不承担钳位**（正常排列不产生负坐标，minX = minY = 0）；验收 = 正常排列的结构断言 + **特殊 child（`SetPosition(-10,-20)`）验证既有钳位行为不变** | 自动化 |
| **A4** | **零回归**：既有两个 `Layout`（`Vertical` / `Horizontal`）行为逐位不变 | 自动化 |
| **A5** | `examples/ModelProbe` 迁移后**观感与迁移前一致**（行高 / 位置 / 滚动范围 / 勾选）。★ **v1.1 增补（评审 §14）：增加结构化自动验收**——迁移前后 `row[i].position.y == i × 28` · `row[i].height == 28` · `contentExtent.height == N × 28`（人工目视只补视觉 / 滚动 / 勾选；「看起来差不多但末行多 1px」由自动测试先抓） | 自动化 + 人工（目视） |
| **A6** | 与 `ScrollView` 组合可用：`Panel` + 容器 + `ScrollView` 三层各司其职（装饰 / 排列 / 视口） | 结构性 |
| **A7** | 公共头 / 用例 / CMake 计数与预算一致，且在四链下全绿 | 结构性 |

---

## 6. 下一步

1. ✅ ~~本稿评审~~（**v1.1 已通过**——外部评审「原则上通过，小修后进初设」；处置见 §1.5，**初设须钉死 5 件事**：① D4 = 消费者调 `UpdateContentExtent()`、不改 `Arrange()` 签名；② ListLayout 只改 Position 不改 Size；③ Grid cell 固定 vs 自适应列宽（desktopnest spike）；④ ListLayout 不复用 VerticalLayout 的理由成文；⑤ A3/A5 结构化自动测试）。
2. 评审通过后进**初步设计**（代码基线 B1–Bn 带行号 + 契约 C1–Cn + 探针）。
3. ★ **不在本 Phase 做**的事一律**不写进代码**，需要时记进 `roadmap-deferred.md`（条 95②）。

---

## 7. 修订记录

- **v1.1**（2026-10-02）**评审第一轮处置 —— ✅ 原则上通过，可进入 Preliminary Design**。① **评审结论**：「原则上通过，小修后进入 Preliminary Design」；**D1 选布局层获明确支持**，架构分层判词 = 「Layout 决定 Widget 怎么摆；数据控件决定数据怎么变成 Widget」；逐条处置见 **§1.5**。② ★ **实质修订 4 处**：**§3.1-2 措辞**（「内容尺寸自动推导」→「布局位置自动推导 + 经既有 `UpdateContentExtent()` 推导——消除数值手算」）· **D3**（`GridLayout` 参数模型**暂缓冻结**——固定 vs 自适应列宽留初设 spike）· **D4 定案倾向 ①**（消费者调 `UpdateContentExtent()`，**不改 `Layout::Arrange()` 签名**）· **§3.3 影响面改「公共头 +1 ~ +2」**。③ ★★ **新增 D9（排列边界原则）**：只操作直接子控件 `SetPosition`、不改 child Size；Grid 布局对象 = Row Panel、不深入内部。④ ★ **验收增补**：A3（负坐标语义归 `UpdateContentExtent()`、ListLayout 不钳位 + 特殊 child 断言）· A5（结构化自动验收：位置/行高/extent 逐项断言）。⑤ ★ **初设必答 5 件**（评审收官清单）落 §6。⑥ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-10-02）初稿。**输入**：审计 D-5 · `roadmap-deferred.md` #42（顺位 ⑤）· `desktopnest-roadmap.md` G-7 · 立项勘察（K1–K8 全部带行号）。**内容**：§1 背景（三处登记 / 两消费者 / 与 Phase 15 界线 / 虚拟化解耦）· §2 现状勘察与缺口清单（G-a/b/c）· §3 范围与非目标 · §4 待决点 D1–D8（倾向 = 布局层）· §5 验收 A1–A7 · §6 下一步。**待评审。**
