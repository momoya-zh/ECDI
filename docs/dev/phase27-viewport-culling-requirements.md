# Phase 27 · 绘制命令构建层的视口剔除（viewport culling）—— 需求确认

> **版本：v1.1**（2026-10-04）｜**状态：✅ 评审通过（可进入初步设计）**
> ★ 外部评审结论：「**Phase 27 Requirements v1.0：方向正确，可以进入 Preliminary Design**」——评级：架构方向 A · 范围控制 A · 风险识别 A-（D3 需完整 OnPaint 盘点）· 验收设计 A；★ **非**「需求完全冻结、无条件通过」：**D3（OnPaint 副作用）升级为初设第一优先级**，评审给定**初设必答六问**（见 §4.1）。★ 评审对边界与验收的修正已吸收（D2 边界接触判据 / A1 整段验证 / A2 确定性契约 / 用例预算 ~+10）。
> **立项来源**：`roadmap-deferred.md` **§7.9 #49**（**顺延至 Phase 27**——用户 2026-10-03 定，下次立项时优先评估）· 性能三方向评估之 **③**
> **前序**：Phase 26（FreeType 文本栈 + GL 渲染后端）已实施收口——三方向中的 ①（GL 执行端）②（测量缓存）已解决，本条是**最后一块**。★ 评审对两 Phase 关系的定调：「**Phase 26 解决『命令已经生成以后怎么更便宜地执行』；Phase 27 解决『既然根本看不见，为什么还要生成命令』**」。

---

## 1. 背景

### 1.1 性能问题的三方向归属（②已由 Phase 26 兑现）

测试项目实测：**大文本 TextBox 滚动 CPU 暴增**。2026-10-03 评估拆成三个独立方向：

| 方向 | 内容 | 归属 | 状态 |
|---|---|---|---|
| ① | **GDI 软件光栅化**（逐命令、无攒批） | Phase 26 **GL 后端** | ✅ 已落地 |
| ② | **逐帧 GDI 文本测量**（`TextWidget` 每帧 `MeasureText`） | Phase 26 **测量缓存**（两条链都补） | ✅ 已落地 |
| **③** | **结构性全量重绘**——视口外子树仍**构建 paint 命令** | **本 Phase（#49）** | 🚧 本稿 |

### 1.2 缺口的精确定义

- `ScrollView::SetContentOffset` → `Invalidate()`（`ScrollView.cpp:113,128`）⇒ 偏移变化 = 整树重绘；
- `Widget::Paint` 的 children 遍历**无可见性 / 视口判定**（`Widget.cpp` Paint 体）⇒ **视口外的行照样构建命令**（`ctx.DrawText` 等）；
- `PushClip` **只影响后端执行、不影响命令构建**——`PaintContext::PushClip` 只是把命令塞进缓冲（`PaintContext.cpp:27`），后端执行时才裁剪。

⇒ **精确表述**：视口外子树的 `OnPaint` 被调用、paint 命令被构建进缓冲，只是**后端执行时被裁剪丢弃**。GL 后端已把执行端做便宜，但**构建端的 CPU（遍历 + 命令构造 + 命令缓冲增长）一分钱没省**。

### 1.3 与相邻条目的边界（三者互补，不重复）

| 条目 | 语义 | 与本条的关系 |
|---|---|---|
| **#21 脏区合帧** | 少发几帧 `WM_PAINT`（局部失效） | 正交——本条优化**单帧内**的构建量 |
| **#33 按需创建控件** | 数据源 → 可视行（虚拟化的前置） | 正交——本条的输入**仍是已存在的子树**（控件一直在，只是不遍历） |
| **#49 本条** | 视口外子树**不构建命令** | — |

### 1.4 ★ 已核实的结构性安全论证（本稿最重要的正面结论）

每个 `Widget::Paint` **先 `PushClip(自身边界)` 再画**（`Widget.cpp:256-259`），后端执行时**子树被祖先边界累积交集裁剪**。由此：

> **若某子树的自身矩形与当前累计裁剪区不相交，则该子树在本帧内不可能改变任何像素** ⇒ 跳过其命令构建在渲染语义上**恒安全**（纯优化，可被像素级等价断言验证）。

★ 这条论证把风险从「渲染等价性」（恒安全）转移到两个真正麻烦的地方——**D4（OnPaint 副作用）与 D2（下钻判据的精确条件）**。

### 1.5 与 Phase 25 C-VIS 的衔接

C-VIS 已让 `IsVisible() == false` 的子控件**跳过绘制**（`Widget::Paint` 入口早退）。本条补的是**可见但在视口外**的子树——长列表滚动场景的主体成本。两者正交叠加：C-VIS 管隐藏项，#49 管视口外可见项。

---

## 2. 现状勘察（K1–K8，全部带行号实测 2026-10-03）

| # | 事实 | 证据 |
|---|---|---|
| **K1** | 偏移变化 ⇒ 整树失效：`ScrollView::SetContentOffset` 末尾 `Invalidate()`（`:128`）；`ScrollView.cpp:113` 同链 | `src/Widget/ScrollView.cpp:113,128` |
| **K2** | `Widget::Paint` **无条件遍历 children**——无视口/相交判定；每 widget 先 `PushClip(自身边界)` | `src/Widget/Widget.cpp:248-276`（`:258` PushClip） |
| **K3** | `PaintContext::PushClip` **只记录命令**——无构建侧裁剪状态 | `src/Render/PaintContext.cpp:27-30` |
| **K4** | PushClip/PopClip 在 `Widget::Paint` 内**严格配对**（不变量 I1：Paint 内无其他 return 路径）⇒ **构建侧维护滚动交集栈在结构上可行** | `src/Widget/Widget.cpp:274` |
| **K5** | 后端执行语义 = 嵌套裁剪交集（GDI `IntersectClipRect` / GL `glScissor`）——像素语义恒安全 | `GDIBackend.cpp` / `GLRenderer.cpp` ApplyScissor |
| **K6** | ★★ **OnPaint 副作用盘点（初勘）**：`TextBox::OnPaint` 起始调 `SyncScrollBar()`（惰性行重算 + 滚动条同步——scrollbar-placement-and-appearance 的字体路径修复点）；其余控件的 OnPaint 基本纯绘制。⇒ **跳过视口外子树 = 该帧不执行其 OnPaint 副作用**——须逐控件盘点（详设） | `src/Widget/TextBox.cpp`（OnPaint 起始 SyncScrollBar） |
| **K7** | `IsVisible()` 早期返回已存在（`Widget::Paint:250-252`）⇒ **隐藏子树已跳过**（C-VIS 副产品）——本条针对**可见但在视口外** | `src/Widget/Widget.cpp:250` |
| **K8** | 命令流结构：`PushClipCommand` / 绘制命令 / `PopClipCommand` 顺序有语义（`RenderCommand.h` 注释「相对顺序有语义，不可重排」）⇒ 剔除必须**整段**跳过（Push + 内容 + Pop 一起） | `include/ECDI/Render/RenderCommand.h` |

---

## 3. 范围

### 3.1 做（倾向）

1. **构建期视口剔除**：`Widget::Paint` 在下钻子树前判定「自身矩形 ∩ 当前构建侧裁剪区」——不相交 ⇒ **跳过整个子树的命令构建**（OnPaint 不调、children 不遍历、Push/Pop 不产生）。
2. **`PaintContext` 维护构建侧裁剪交集栈**：`PushClip`/`PopClip` 本就严格配对（K4）⇒ 顺带维护一个**滚动交集矩形**（纯增量计算），作为下钻判据的来源。
3. **`ScrollView` 滚动路径接入**：`SetContentOffset` 的内容视图重排走剔除后的构建。
4. **验收 = 三重**：① 命令计数断言（视口外子树零命令）；② **像素级等价**（剔除前后整帧 `glReadPixels` 逐位一致——K5 论证的直接兑现）；③ 性能基准（大文本滚动构建耗时对比）。

### 3.2 非目标

| 排除项 | 理由 |
|---|---|
| **脏区合帧 / 局部失效**（#21） | 正交（§1.3）——单帧构建量优化不解决多帧失效策略 |
| **虚拟化 / 按需创建**（#33） | 输入仍是已存在子树（§1.3） |
| **后端执行端优化**（攒批 / instancing） | M-5 范畴——本条只省**构建端** |
| **`UpdateContentExtent` 的可见性过滤** | Phase 25 C-VIS 已定「收益只在 extent、由停泊承担」——本条不改其语义 |
| **OnPaint 副作用的重新设计** | D4 只做**语义边界盘点与定案**（跳过是否可容忍），不重构控件 |

---

## 4. 待决点（倾向已给，待评审）

### D1 ★ **接缝位置**：构建侧裁剪栈放哪

- **(a) `PaintContext` 内部**：`PushClip`/`PopClip` 顺带维护滚动交集矩形；`Widget::Paint` 经 `PaintContext` 查询相交。★ 倾向 (a)——`PushClip` 本就是命令流的裁剪入口（K3/K4），`PaintContext` 是构建侧唯一有完整裁剪信息的位置，**公共 API 零改动**（`PaintContext` 是公共头但新增的是查询方法，既有签名不动）。
- (b) `Widget::Paint` 显式传递裁剪矩形——改 `Paint` 签名 = 动公共虚函数面，**否决**。

### D2 ★ **下钻判据的精确条件**

- 子树矩形 = `Widget` 自身 `m_geometry`（**不含 descendants 越界部分**——K5 论证保证越界部分本来就被祖先裁掉，跳过不改变像素）。
- 判据 = `自身矩形 ∩ 当前累计裁剪 = ∅` ⇒ 跳过。★ **不加安全边距**（几何恒安全论证不允许模糊地带——要么相交要么不相交；评审明确支持：「既然敢做像素级等价测试，就应该把几何边界精确定义，而不是用 padding 掩盖」）；浮点/整数口径 = **int**（与 `m_geometry` 一致）。
- ★★ **边界接触判据（评审补充，v1.1 吸收）**：相交的数学定义 = **交集宽 > 0 且交集高 > 0**（**不是** `>= 0`）——`widget.right == clip.left` 这类**边界接触视为不相交 ⇒ 直接剔除**（接触面积为零，不可能产生像素）。建议落成 `Rect::Intersects` 契约（初设冻结具体落点）。
- ★★ **判定时序（评审补充，v1.1 吸收）**：剔除判定必须发生在当前 Widget **产生任何 paint 命令之前**（含 `PushClip`）——顺序 = ① `IsVisible` → ② 相交判定 → ③ `PushClip` → ④ `OnPaint` → ⑤ children → ⑥ `PopClip`；否则「整个子树不进入 Paint」的优化意义与 A1 的干净性同时丢失。★ 与 Phase 25 C-VIS 的 `IsVisible` 检查**正交且保持该顺序**（评审确认）。
- ⚠️ 待定（初设必答 Q1/Q2）：`ContentOffset` 的处理——`childOrigin = x − GetContentOffsetX()`（Phase 15 接缝）⇒ 子树矩形在**内容坐标系**、裁剪区在**视口坐标系**，判据前须做偏移换算（初设核心数学）。★ **评审定调**：`widgetRect ∩ currentClip` **必须在 `Widget::Paint` 的当前绘制坐标系中统一**（同一坐标系才能相交）；**禁止** `PaintContext` 出现 `IsVisible(rect, contentOffset)` 之类的特判——`ScrollView` 负责把 child 几何 / Paint 坐标处理正确，`PaintContext` 只负责「当前 Paint 坐标系里的 clip」（保持其对 ScrollView / viewport / scrolling 无感知，与 ECDI 分层一致）。

### D3 ★★ **OnPaint 副作用的语义边界**（本 Phase 最大风险点 · **初设第一优先级**）

- **K6 初勘**：`TextBox::OnPaint` 起始的 `SyncScrollBar()` 是已知副作用（惰性行重算 + 滚动条同步）。视口外被跳过的帧不执行它 ⇒ 滚回可视区后的**第一帧**会补执行（Paint 恢复遍历）——「迟一帧同步」是否可容忍，**逐控件盘点后定**。★ 评审初步判定 `SyncScrollBar` 型（同步 UI 到当前状态、无时间敏感逻辑）**可接受**，但「必须用实际源码逐个确认，而不是根据名字猜」。
- ★★ **问题的两级（评审 v1.1 升级）**：
  - **(i) 盘点级（本 Phase 做）**：全库 `OnPaint` override 逐一核查是否含状态推进 / 失效逻辑——搜 `Set*` / `Invalidate*` / `Layout*` / `Update*` / `Sync*` / `Create*` / `Destroy*`，**尤其 `Invalidate`**（`Paint → OnPaint → Invalidate → 下一帧` 若被剔除则状态推进可能永久停摆）。★ **正确的提问不是「副作用是否同步 UI」，而是「是否存在依赖『每次 Paint 都执行』的状态推进或失效逻辑」**。
  - **(ii) 架构级（登记未来项，本 Phase 不做）**：`OnPaint()` 在架构语义上是否本应纯绘制？若是，`SyncScrollBar` 类职责未来应迁往 Update/Layout 相位。★ 评审明确：**Phase 27 不顺手重构**——本 Phase 只盘点 + 判定当前副作用可否容忍；「让 OnPaint 更接近纯绘制」登记为未来 defect / architecture item。
- ⚠️ **底线性约束**：跳过的子树**滚动回来后第一帧必须渲染正确**——这本身就是验收项（A3，评审确认为必须保留：该路径同时验证 culling + offset + clip 栈 + re-entry + 副作用五件事）。

### D4 **开关形态**

- ★ **倾向恒开、无开关（评审 v1.1 降级为倾向、暂不冻结）**：纯几何裁剪层面评审完全赞成恒开；但因 `OnPaint` 目前**不是纯函数**，恒开实际**依赖 D3 盘点成立** ⇒ 等 OnPaint audit 完成且结论为「所有副作用均为绘制前状态同步、不依赖每帧执行」后，方可正式冻结恒开。若盘点发现无法穷尽的副作用，降级路径不变（`Widget` 级 opt-out 虚函数）——留初设决。

### 4.1 ★★ **初设必答六问（评审给定，按重要性排序）**

1. **`PaintContext` 的 clip 采用什么坐标系？**（`widgetRect ∩ currentClip` 必须同一坐标系——见 D2 评审定调）
2. **`ContentOffset` 在进入剔除判定前在哪里完成坐标转换？**（应在 ScrollView 的 child origin / Paint 坐标处理中，不在 `PaintContext`）
3. **Initial clip 是什么？Root 从哪里开始？**（评审倾向 `PaintContext` 初始 clip = client / render target 矩形 ⇒ 整棵树从第一层就有有限 clip；初设把语义写清即可，需求阶段不拍死）
4. **完整盘点所有 `OnPaint` override 的副作用**（D3 (i)——源码逐个核，重点 `Invalidate`）
5. **`Rect::Intersects` 的边界定义与 float → int 的唯一规则**（交集宽高 **> 0**；接触 = 剔除；int 口径唯一换算点）
6. **A2 像素等价测试如何固定时间 / 动画 / caret 等非确定因素**（见 A2 契约）

---

## 5. 验收方向（A1–A6，待初设细化）

| # | 方向 | 类型 |
|---|---|---|
| **A1** | **命令计数（评审 v1.1 加强：整段验证）**：视口外子树的 **`PushClip` / `Draw*` / `PopClip` 整段全部为 0**（`RecordingBackend` 计数或等价观测）——不只 `Draw*` = 0：必须证明「真的没进入子树 Paint」，而不是「进入了但没产生某些 draw command」（K8 命令流顺序语义的兑现） | 自动化 |
| **A2** | **像素级等价 + 确定性契约**：剔除前后整帧渲染逐位一致（K5 恒安全论证的直接兑现——GL `glReadPixels` 对照）。★ **测试输入必须无非确定源（评审 v1.1 补充）**：无动画 / 无时间依赖绘制 / 无随机数 / 无 caret blink 状态跃迁（或固定时间与固定状态）——否则 `glReadPixels !=` 无法区分是 culling 错了还是场景自己变了 | 自动化 |
| **A3** | **滚动回归**：视口外滚动多屏后滚回，首帧渲染正确（无空白/陈旧区域——D3 底线）。★ 评审确认为**必须保留**：同时验证 culling + scroll offset + clip 栈 + re-entry + OnPaint 副作用，能有效抓缓存可见性 / 陈旧 clip / 错误偏移 / 漏失效四类问题 | 自动化 |
| **A4** | **性能**：大文本（数百行）滚动场景的构建耗时对比（实测数字，非感觉） | 基准 |
| **A5** | 零回归：既有全部用例（含 TextBox/ScrollView/GL 后端）全绿 | 自动化 |
| **A6** | 越界绘制语义保持：`ClipsChildren` / 祖先裁剪下子树部分相交时，**相交部分照常构建**（部分可见 ≠ 全跳） | 自动化 |

★ **评审对验收组合的总评**：A1 + A2 + A3 三重组合「非常好」——A2 把 K5 的理论论证变成实际验证（framebuffer 字节对比），A3 是五合一回归探针。

---

## 6. 影响面预算（初估，待初设校准）

| 项 | 预估 |
|---|---|
| 公共头 | **94 → 94**（`PaintContext` 新增查询方法 = 既有公共头追加方法，头文件数不变；API 面待初设核定） |
| 用例 | **319 → ~329**（★ 评审 v1.1 上修：原估 +6 偏乐观——完全可见 / 完全不可见 / **边界接触** / 部分相交 / 嵌套 clip / ScrollView offset / C-VIS 正交 / **滚出再滚回** / 越界绘制若全做自动化 **~+10 不夸张**；★ 评审：「测试多一点反而是这个 Phase 应该接受的」） |
| CMake | **0 改动** |
| 风险 | ★ 中——D3（副作用）是唯一可能反向否决本条的因素（若盘点发现不可容忍的副作用，降级为「仅对纯绘制控件启用」）；★ 评审补充：**本条不解决虚拟化**（创建/保留仍是 2000，只是 Paint 遍历不再深入——与 #33 是两个量级，Phase 27 是低风险的中间优化） |

---

## 7. 修订记录

- **v1.1**（2026-10-04）**外部评审吸收（✅ 通过，可进入初设；D3 升级为初设第一优先级）**。① ★ **结论**：架构方向 A · 范围控制 A · 风险识别 A-（D3 需完整盘点）· 验收设计 A——「可以进入 Preliminary Design」，**非**无条件冻结。② ★★ **新增 §4.1 初设必答六问**（评审给定，按重要性排序）：clip 坐标系 · ContentOffset 转换位置 · initial clip 语义 · OnPaint 全量盘点 · `Rect::Intersects` 边界与 float→int 规则 · A2 确定性契约。③ ★★ **D2 三处吸收**：**边界接触判据**（交集宽高 **> 0**，`right == left` 型接触 = 剔除，落 `Rect::Intersects` 契约）· **判定时序**（剔除必须发生在任何命令产生之前——`IsVisible → 相交判定 → PushClip → OnPaint → children → PopClip`，与 C-VIS 正交且保持顺序）· **坐标系定调**（`widgetRect ∩ currentClip` 必须在同一 Paint 坐标系中；**禁止** `PaintContext` 感知 ContentOffset——转换责任在 ScrollView）。④ ★★ **D3 两级升级**：(i) 盘点级（搜 `Set*/Invalidate*/Layout*/Update*/Sync*/Create*/Destroy*`，**尤其 Invalidate**——「依赖每次 Paint 都执行的状态推进」才是正确提问）；(ii) 架构级（OnPaint 应否纯绘制）**登记未来项，本 Phase 不重构**。⑤ **D4 降级为倾向**：恒开依赖 D3 盘点成立，audit 完成前不冻结。⑥ **A1/A2 加强**：A1 = `PushClip`/`Draw*`/`PopClip` **整段**为 0（不只 Draw 计数）；A2 补**确定性契约**（无动画/时间/随机/caret blink）。⑦ **影响面上修**：用例 ~+6 → **~+10**（319 → ~329）。⑧ **范围补充**：本条不解决虚拟化（创建/保留 2000 不变，Paint 遍历 ~40——与 #33 两个量级，低风险中间优化）。⑨ 评审总评记录：「Phase 26 证明第二个 Renderer 可以接进来；Phase 27 证明 Paint 架构本身足够聪明——从『把功能做全』进入『把执行模型做对』」。
- **v1.0**（2026-10-03）初稿。**输入**：`roadmap-deferred.md` **#49**（顺延至 Phase 27）· 性能三方向评估 · 本次立项核实的 K1–K8（全部带行号；★ **K5 结构性安全论证**——子树不可能画出祖先裁剪之外 ⇒ 剔除恒安全 · ★ **K6 初勘** = TextBox OnPaint 起始的 SyncScrollBar 是已知副作用，D3 语义边界盘点的首个样本）。**内容**：§1.2 缺口精确定义（构建 vs 执行分离）· §1.3 与 #21/#33 边界 · §3.1 做剔除 + 构建侧裁剪栈 · §4 待决点 D1–D4（D3 副作用 = 最大风险点）· §5 三重验收（命令计数 / 像素等价 / 滚动回归 + 性能基准）· 影响面（公共头 94 → 94 · 用例 319 → ~325 · CMake 0）。**待评审。**
