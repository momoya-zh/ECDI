# Phase 27 · 绘制命令构建层的视口剔除 —— 详细设计（v1.1 · 实施规格）

> 来源：初设稿 `phase27-viewport-culling-preliminary-design.md` **v1.1 ✅ 评审 PASS**（外部评审 2026-10-04：「**PASS，可以进入 Detailed Design**」，13 项分项全过；并给定**详设盯防五件事**——初设 §1.1）
> 状态：**v1.1**（2026-10-04）——**✅ 评审 PASS → Implementation**（外部评审 2026-10-04：15 项分项全过、「可以直接开工」、总体风险 低～中可控；无硬伤，三条非阻塞建议已吸收——见 §9）
> 定位：**实施规格**——把初设的几何/语义冻结落成逐文件改动与精确测试场景。★ 初设评审的五个盯防点在 **§1 逐题钉死**（本稿核心）；★ 范围锁不变（需求 §3.2：不做虚拟化 / 脏区 / 后端执行端 / OnPaint 纯绘制重构）。

---

## 1. 详设五必答（初设评审盯防五件事逐题钉死——本稿核心）

### D27-A ★ clip stack 的精确数据结构与 initial clip 生命周期（盯防 1）

**数据结构（`PaintContext` 私有成员，冻结）**：

```cpp
private:
    std::vector<Rect> m_cullStack;   ///< 构建侧**累计裁剪交集**栈（★ 镜像栈——C27-1。★ 命名注记：名虽为 cull，实存的是「initial ∩ … ∩ rect」的累计交集——评审 §13；现阶段名字可接受，不改设计）
    std::size_t       m_cullBase = 0;///< 种子边界：**仅守卫本镜像栈**——`m_cullStack` 中属于 initial clip 的条目数，构建侧 `pop_back` 不得弹出（≠ 命令 `PopClip` 发射的合法下限——评审 §12）
    bool              m_cullingEnabled = true; ///< 构造期定，**不可变**（D27-C）
```

**构造（两个，签名冻结）**：

```cpp
PaintContext(CommandBuffer& commands, TextMeasurer& measurer);
// 既有双参：无种子（m_cullBase=0，栈空 = 无界）、剔除开 —— 既有全部调用点零改动

PaintContext(CommandBuffer& commands, TextMeasurer& measurer,
             const Rect& initialClip, bool enableCulling = true);
// 新增四参：种子 = initialClip（拷贝入栈，m_cullBase=1）
//   Window::PaintFrame 传 client 矩形；A2 基线传 enableCulling=false；T27-12 验证两态
```

**生命周期**：`initialClip` **按值拷贝**进栈——`PaintContext` 本就是每帧栈上对象（初设 B3，`Window.cpp:136` 先例），无悬垂面；`CommandBuffer&` / `TextMeasurer&` 的生存期约束与既有契约相同（帧内有效），本 Phase 不新增约束。

**三个方法的精确行为（冻结）**：

| 方法 | 行为 |
|---|---|
| `PushClip(rect)` | ① 构建侧：`next = 栈空 ? rect : Intersection(栈顶, rect)`；`m_cullStack.push_back(next)`。② 命令：照旧 `emplace_back(PushClipCommand{rect})`（**原 rect 原样进命令**，命令流零变化）。①② 同函数内固定先栈后命令 |
| `PopClip()` | ① 构建侧：`if (m_cullStack.size() > m_cullBase) m_cullStack.pop_back()`（★ 守卫**仅作用于本镜像栈**：保证种子永不被弹出——即使上层 `PopClip` 多调用一次，镜像栈与命令栈也同步失衡而非崩溃，与既有「栈空跳过」防御同级）。② 命令：**照旧发射 `emplace_back(PopClipCommand{})`，不受 `m_cullBase` 门控**（★ 评审 §12 维护提醒：`m_cullBase` **不是**命令 `PopClip` 的合法下限——实现时注释必须写明「守卫只保护构建侧镜像栈」，防止后人把 `> m_cullBase` 比较连带套到命令发射上）。①② 顺序固定：先栈后命令 |
| `IsRectVisible(rect) const` | `!m_cullingEnabled` ⇒ `true`；栈空（无种子且无 push——仅双参构造的首层前出现）⇒ `true`（无界）；否则 `Intersects(栈顶, rect)` |

**`Intersection` 求交**：`PaintContext.cpp` 私有静态 helper（返回交集矩形，可为零面积）；公共面只有 `Intersects`（bool，初设 Q5 冻结的严格 >0 语义）——**不求交矩形不进公共 API**（无消费者，YAGNI）。

**镜像不变量（C27-1，安全性的最终表述）**：构建栈顶 ≡ 执行端有效裁剪。执行端 = 同一批 rect 按 lround 半开量化后求交（`GDIBackend.cpp:638-653` / `GLRenderer.cpp:361-382,685-699` 实测）；剔除安全性 = 初设 §1-Q5 证明（lround 单调 + 半开 ⇒ 构建侧空交集 ⇒ 执行侧必空），本稿**不重复证明、原样冻结为契约**。

### D27-B ★ `self` 的构造方式：单一表达式，culling rect ≡ PushClip rect（盯防 2）

冻结为 `Widget::Paint` 内**同一个具名局部变量**（不复制第二份几何公式）：

```cpp
const Rect self{ static_cast<float>(x), static_cast<float>(y),
                 m_geometry.width, m_geometry.height };  // ← 判据与 PushClip 消费同一份值
if (!ctx.IsRectVisible(self)) return;                    // 判据
ctx.PushClip(self);                                      // 推入同一份
```

★ 机检盯防（§5 ⑦）：`Widget.cpp` 中 `IsRectVisible` 与 `PushClip` 的实参**必须是同一标识符 `self`**——出现第二个构造表达式即违规（Phase 26 §11.9「同一几何量多处消费必须同一量化规则」教训的结构化落地）。

### D27-C ★★ `SetCullingEnabled` 的 API 边界：**定案 = 方案 B 变体**（盯防 3，评审踩刹车点）

**比较（初设 O3 两方案）**：

| | 方案 A：公共 `SetCullingEnabled(bool)` | **方案 B 变体：构造期开关（定案）** |
|---|---|---|
| A2 对照 | 同一 ctx 运行期切换 | 构造两个 PaintContext（每帧栈上构造本就是既有生命周期） |
| 兼容性 | **永久契约**——语义一公布即不可收回 | 构造参数同样是签名面……但**开关语义冻结在「帧开始前定死」**，无运行期状态漂移 |
| 将来真出现消费者 | 已背契约 | **再加 setter 是纯增量**（向后兼容零成本）；现在加了将来收不回 |
| 语义风险 | 帧中途切换 = 未定义地带（栈已建、判定时有时无） | **不存在**——不可变成员 |
| 需求符合度 | 「测试能关」≠「用户能关」（评审原话） | 精确满足前者 |

**定案**：**不设 `SetCullingEnabled` 公共方法**。开关收敛为四参构造的 `enableCulling`（默认 `true`），构造期不可变；`IsRectVisible` 保留公共（正常 PaintContext 能力，与开关性质不同——评审的区分采纳）。★ **公共 API 由此从初设的 +3 收敛为 +2**（`Intersects` / `IsRectVisible`）+ 1 四参构造重载。★ D4 的未来降级路径不受影响（Widget 级 opt-out 是另一维度；全局逃生门若真出现，届时加公共 setter 是纯增量）。

### D27-D ★ T27-5/6/7 精确测试树（盯防 4——subtree pruning 的可执行定义）

**T27-5（嵌套子树剪枝）**——双参构造（无界根），靠 Root 的 PushClip 制造有限裁剪：

```text
Root Panel  (0,0,200,200)                     ← 无界 ⇒ 保留；PushClip [0,0,200,200]
└─ Parent Panel (150,50,100,100)              ← 交 Root = [150..200]×[50..150]（50×100 >0）⇒ 保留
   ├─ ChildA Panel (local 60,60, 20,20)       ← 绝对 (210,110)：∩ [150..200]×[50..150] = ∅ ⇒ ★ 整段剔除
   │  └─ Deep Label (local 5,5)               ← 随 ChildA 一起消失（连遍历都不进入）
   └─ ChildB Panel (local 30,30, 20,20)       ← 绝对 (180,80)：∩ >0 ⇒ 保留
```

断言：`PushClipCommand` 数 = 3（Root/Parent/ChildB）· `DrawRectCommand` 中**不存在** ChildA 的标志色 · Deep Label 的 `DrawTextCommand` 不存在 · ChildA 分支的 `PopClip` 也不存在（C27-2 整段语义）。

**T27-6（ScrollView offset）**——`ScrollView(0,0,200,100)`，行 180×20 × 6（content y = 0,20,…,100），`SetContentOffset(0,80)`：

```text
row_i 绝对 y = i*20 − 80
row0 [−80,−60] row1 [−60,−40] row2 [−40,−20] row3 [−20, 0]  ← 与视口 [0..100] 交集高 = 0 ⇒ 全部剔除
row4 [0, 20]  row5 [20, 40]                                  ← 保留（可见）
```

★ **row3 是内嵌的边界接触断言**（`[−20, 0]` 接触型 = 交集高恰为 0 ⇒ 剔除——Q5 语义在真实滚动场景的复现）。断言：可见行的 `DrawRectCommand`（行背景标志色）恰好 2 个，且 y 为 0 / 20。

**T27-7（滚出滚回 + 副作用补执行）**——同 T27-6 装置，行 4 换成 `TextBox(180×20)`（含多行文本触发 `SyncScrollBar`）：① 滚出（offset 推到 TextBox 视口外）→ Paint → 断言 TextBox 分支整段 0 且滚动条可见性**未同步**（惰性账未还）；② 滚回 → Paint → 断言首帧：TextBox 分支命令恢复 + `bar->IsVisible()` 翻转（账已补）+ 无空白像素语义（D3 底线）。★ 双断言同时锚定「延迟执行 ≠ 丢失」与 A3。

### D27-E ★★ 存量命令流测试对账清单（盯防 5——实测盘点，2026-10-04）

**方法**：子代理全量审计 8 个测试文件中所有检查命令流 / 涂画控件树的用例（`PushClipCommand` / `Draw*Command` / `commands.size()` 模式 + 布局越界分析）。

**结论：⇒ 存量测试零改动。** 依据三条：

1. **全部 ~25 条 paint 路径都用双参构造**（无界初始 clip）⇒ 顶层控件永不触发剔除；嵌套层只受「父子几何」影响。
2. **唯一的父子越界 paint 用例 = `Clip.ChildOverflow`（ClipTests.cpp:214）**：Panel(10,20,100,50) 内 Button pos(5,5) size(200,20) → 绝对 (15,25,200,20)，与父裁剪交集 95×20 **部分相交 >0** ⇒ 按冻结判据**照常保留**，其断言（2 次 PushClip、x=10/15）不变。★ 该用例升格为**金丝雀**：若未来有人把判据收紧为「完全包含」或引入 epsilon，它第一个失败——这正是我们想要的守卫。
3. **两个「子树完全越界但不涂画」的用例**（`ScrollView.ClipsChildrenRejectsOutside`（行在 y=300、视口 200）、`Panel.HitTestRegression`（子 (200,200) 在 100×100 Panel 内））均为 **HitTest-only**，不 Paint ⇒ 不受影响；且它们钉住的「越界子树仍可命中」语义**本 Phase 不触碰**（剔除只改 Paint 路径，`HitTest` 零改动——初设 D27-8 / C27-2 的另一半）。

**对账明细表（详设存档，实施批二复核用）**：

| 文件 | 涂画 + 命令断言用例 | 越界子树？ | 判定 |
|---|---|---|---|
| ClipTests | DepthSequence · RectAbsolute · TextBoxFullLine · SelectionNoClamp · **ChildOverflow（金丝雀）** · InvisibleNoPush | 仅 ChildOverflow（部分相交） | 全部不变 |
| WidgetTests | PanelPaint · LabelPaint · ButtonPaint · PanelSetStyle · PanelShapeRounded · PanelShapeBorderRing（全部单根无子） | 无 | 不变 |
| TextBoxTests | EchoMasked · ShapeRounded · ShapeBorderRing · ScrollBarNarrowsTextArea（`NthClipWidth` helper）· ScrollBarLazySyncOnPaint（**OnPaint 副作用断言**——根级无界 ⇒ 安全） | 无 | 不变 |
| ScrollViewTests | OffsetDoesNotAffectOwnClip · ContentIsOffset · NotAffectedByContentOffset（**相对索引断言最脆**）· ThumbGeometry；ClipsChildrenRejectsOutside / Panel.HitTestRegression（**HitTest-only**） | 越界者不涂画 | 不变 |
| CaptionBarTests | GlyphCommandCount（精确总数 3/4/6/8）· BarCombination（子全在界内） | 无 | 不变 |
| CheckBoxTests | PaintUnchecked/Checked · Radio PaintUnchecked/Checked（单根无子） | 无 | 不变 |
| ProgressBarTests | PaintTwoLayers · PaintZeroProgress · SetStyle · ResizeFillGeometry（★ 零宽 fill 断言——命令级零面积，与控件级剔除正交） | 无 | 不变 |
| RendererTests | 全部无 `Widget::Paint`（手工构造命令流） | — | 不变 |

---

## 2. 逐文件改动

| # | 文件 | 改动 |
|---|---|---|
| △1 | `include/ECDI/Core/Rect.h` | + `inline bool Intersects(const Rect&, const Rect&)`（初设 Q5 代码原样；**公共 API +1**） |
| △2 | `include/ECDI/Render/PaintContext.h` | + 四参构造（`const Rect& initialClip`, `bool enableCulling = true`）· + `IsRectVisible(const Rect&) const` · + 私有 `m_cullStack` / `m_cullBase` / `m_cullingEnabled`（D27-A/C；**公共 API +1**）· ★ **不设** `SetCullingEnabled`（D27-C 定案） |
| △3 | `src/Render/PaintContext.cpp` | 两构造实现 · `PushClip`/`PopClip` 栈维护（D27-A 表格行为）· `IsRectVisible` · 私有 `Intersection` helper |
| △4 | `src/Widget/Widget.cpp` | `Paint` 入口插入剔除判定（D27-B 的 `self` 单表达式；位于 `IsVisible` 之后、`PushClip` 之前） |
| △5 | `src/Window/Window.cpp` | `PaintFrame` 改四参构造：`initialClip = Rect{0, 0, clientSize.width, clientSize.height}`——client 尺寸**每帧**经 `m_platformWindow->GetClientSize()` 取（沿 GL 后端 per-frame 自省先例，resize 天然跟随） |
| △6 | `src/Tests/ClipTests.cpp`（或新建 `CullingTests.cpp`） | T27-1..T27-9 / T27-11 / T27-12（ PaintContext 级 + Widget 级；`CullingTests.cpp` 新文件则 CMake +1——**定案：并入既有测试文件不新建**，CMake 保持 0） |
| △7 | `tools/` 无头 GL 探针（`.workbuddy` 先例，不入仓库） | T27-10 A2 像素等价（构造期 `enableCulling=false/true` 两帧 `glReadPixels` 逐字节比对） |

---

## 3. 契约（冻结）

| # | 契约（沿初设，标注 v1.0 修订） |
|---|---|
| C27-1 | 镜像不变量：构建栈顶 ≡ 执行端有效裁剪（初设 §1-Q5 证明冻结） |
| C27-2 | 剔除 = `PushClip`/`OnPaint`/children/`PopClip` **整段**不发生、零命令；`HitTest` 零改动 |
| C27-3 | 判据 = 交集宽 > 0 且高 > 0（float 同源；接触 = 剔除；无边距） |
| C27-4 | 顺序 = `IsVisible → 剔除 → PushClip → OnPaint → children → PopClip`（固定） |
| C27-5 | initial clip：`Window` 每帧注入 client 矩形（按值拷贝、不发命令）；双参构造 = 无界 |
| C27-6 | **（v1.0 修订：开关收敛进构造）** `enableCulling=false` 构造 ⇒ 命令流与 Phase 26 之前**逐位一致**（构建栈照常维护——镜像不变量不受开关影响）；不可变成员，无运行期切换语义 |
| C27-7 | OnPaint 副作用白名单 = 初设 §1-Q4 表（2 处幂等同步型）；新增副作用须复核 |
| C27-8 | ★ 新增：**种子守卫**——`PopClip` 永不弹出 `m_cullBase` 以下条目（D27-A）；`self` ≡ `PushClip` 实参（D27-B 单表达式） |

---

## 4. 盯防（可机检，沿初设 7 条 + 修订）

| # | 项 | 判据 |
|---|---|---|
| ① | 判定在 PushClip 之前 | `Widget.cpp` 行序：`IsRectVisible` < `PushClip` |
| ② | 栈维护与命令发射同函数 | `PaintContext::PushClip` 内两步共存、顺序固定 |
| ③ | `Intersects` 严格大于 | `Rect.h` 无 `>=` 判交 |
| ④ | `PaintContext` 零滚动感知 | `grep -c "ScrollView\|ContentOffset\|viewport" PaintContext.*` = 0 |
| ⑤ | 副作用白名单不膨胀 | 新 OnPaint override 含 `Set*/Invalidate*/…` 须复核 C27-7 |
| ⑥ | 执行端量化不回退 | 两后端 `lround` 保持；**禁止 ±1 闭合修正**（初设 Q5 反例） |
| ⑦ | `self` 单表达式 | `Widget.cpp` 中 `IsRectVisible(self)` 与 `PushClip(self)` 同一标识符（D27-B） |
| ⑧ | ★ 种子守卫（评审 §12 收紧） | `PaintContext.cpp` `PopClip` 含 `m_cullBase` 比较**且仅门控 `pop_back`**——`PopClipCommand` 的 `emplace_back` 在守卫之外（命令发射与 Phase 26 逐位一致）；实现注释须写明守卫的作用域 |
| ⑨ | ★ 新增：无公共开关 | `grep -c "SetCullingEnabled" include/ src/` = 0（D27-C 定案的防回退哨兵） |

---

## 5. 用例正文（T27-1..T27-12；319 → ~331，+12）

| # | 场景（精确装置） | 断言核心 |
|---|---|---|
| T27-1 | 初设 T27-5 树的 Root 单独涂画（全可见） | 命令流与剔除前逐位一致 |
| T27-2 | D27-D 树（ChildA 视口外） | A 分支 `PushClip`/`Draw*`/`PopClip` 整段 = 0 |
| T27-3 | 边界接触：widget `[100,150]` vs clip `[150,250]`（x 向）+ y 向对称各一 | 剔除 |
| T27-4 | 部分相交（95×20，同 ChildOverflow 几何） | 整段照常构建 |
| T27-5 | D27-D 精确测试树 | subtree pruning（A 连同 Deep 整段消失、B 保留） |
| T27-6 | D27-D ScrollView 装置（6 行 offset 80，含 row3 边界接触） | 可见行背景恰好 2 个（y=0/20）；row0..3 整段 0 |
| T27-7 | D27-D 滚出滚回装置（TextBox 行） | 出 = 整段 0 + 滚动条未同步；回 = 首帧恢复 + `IsVisible` 翻转 |
| T27-8 | C-VIS 正交：隐藏子停泊 `(-w,-h)` + 可见视口外子并存 | 隐藏子行为与 Phase 25 逐位同；视口外子被新判据剔除 |
| T27-9 | 退化矩形（0 宽 / 0 高 widget 各一） | 剔除且不进 OnPaint |
| T27-10 | △7 无头 GL：同一树 `enableCulling` false/true 两帧 | `glReadPixels` 逐字节一致（Q6 确定性契约） |
| T27-11 | **（v1.0 修订：构造期开关）** 同一树 `enableCulling=false` 构造 | 命令流与既有基线逐位一致（C27-6） |
| T27-12 | 双参（无界）vs 四参（client 种子）各涂一棵超界根子树 | 前者不剔除、后者剔除超界部分 |

**批二验收含 D27-E 对账复核**：8 文件对账表逐条重跑（319 存量全绿即对账通过）。

---

## 6. 影响面

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94** |
| 公共 API | **+2**（`Intersects` / `IsRectVisible`——★ 较初设 −1：`SetCullingEnabled` 收敛进构造，D27-C）+ 1 四参构造重载 |
| 用例 | **319 → ~331**（+12） |
| CMake | **0 改动**（T27 用例并入既有测试文件，△6 定案） |
| 风险 | **低～中（可控性高）**——存量对账实测零改动（D27-E）后进一步收敛 |

---

## 7. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | △1 + △2 + △3（PaintContext 级全量）+ T27-3/4/11/12（PaintContext 级用例） | 全链编译 + 存量 319 全绿（零行为变化） |
| 批二 | △4 + △5（Widget/Window 接入）+ T27-1/2/5/6/7/8/9 + **D27-E 对账复核** | 331 全绿（MinGW + Clang） |
| 批三 | △7 + T27-10（A2 像素等价） | 探针逐字节一致 + 用户 MSVC |
| 批四 | A4 性能基准（大文本构建耗时对比；记录 O1 指标集六项、核心三项）+ 文档回填收口 | 读数入详设 §8.1 |

---

## 8. 开放项

| # | 项 |
|---|---|
| O1 | A4 基准参数表（批四冻结，沿 D26-5 口径：固定树 / 固定行数）。**指标集（评审 §14 建议冻结）**：① 树规模 · ② 可见节点数 · ③ 被剔除节点数 · ④ RenderCommand 数 · ⑤ Paint 遍历节点数 · ⑥ 构建耗时——**核心 = ⑥⑤④**（本 Phase 目标 = 减少构建侧工作量，**非** GPU 速度；总帧时间为次要参考，不作核心指标） |
| O2 | ~~存量对账清单~~ → **✅ 已完成（D27-E，零改动 + 金丝雀升格）** |
| O4 | 未来架构候选：OnPaint 纯绘制化（编号待用户拍板，本 Phase 不做） |

---

## 9. 修订记录

- **v1.0**（2026-10-04）初稿。**输入**：初设 v1.1（评审 PASS）+ 初设评审盯防五件事 + 子代理对账审计（8 文件 / ~25 条 paint 路径全量盘点）。**§1 五必答**：D27-A（栈结构 / 种子守卫 / 三方法行为表）· D27-B（`self` 单表达式冻结）· **D27-C（`SetCullingEnabled` 定案 = 方案 B 变体——开关收敛进构造期、不可变；公共 API +3 → +2；「将来加 setter 是纯增量，现在加是永久契约」）** · D27-D（T27-5/6/7 精确测试树含几何坐标与 row3 边界接触内嵌断言）· **D27-E（对账实测：存量零改动；`Clip.ChildOverflow` 升格金丝雀；两个 HitTest-only 越界树不受影响）**。逐文件 △1–△7 · 契约 C27-1..C27-8 · 盯防 9 条（+种子守卫 + 无公共开关哨兵）· 用例 T27-1..T27-12 · 四批 · O2 关闭。
- **v1.1**（2026-10-04）评审吸收。**结论：「Phase 27 Detailed Design：PASS → Implementation，可以直接开工」**——15 项分项全过（几何 / clip stack / initial clip / ScrollView 坐标 / `self` 单源 / `SetCullingEnabled` 边界「较初设更好」/ OnPaint 副作用 / subtree pruning / HitTest 隔离 / 存量测试影响 / GL 像素等价 / 测试场景 / 文件范围 / 批次 / 风险 低～中可控；唯一 🟡 = O1 基准未冻结，不阻塞）。**三条非阻塞建议已吸收**：① §12——`m_cullBase` **仅守卫构建侧镜像栈**，不是命令 `PopClip` 发射的合法下限（D27-A 行为表 ② 与盯防 ⑧ 收紧：`> m_cullBase` 比较只门控 `pop_back`，命令 `emplace_back` 在守卫之外；实现注释必须写明作用域）；② §13——`m_cullStack` 实存**累计裁剪交集**（D27-A 成员声明加命名注记，现阶段名字可接受、不改设计）；③ §14——O1 冻结**指标集六项**（树规模 / 可见节点数 / 被剔除节点数 / RenderCommand 数 / Paint 遍历节点数 / 构建耗时），**核心 = 构建耗时 · 遍历节点数 · RenderCommand 数**（本 Phase 目标 = 减少构建侧工作量而非 GPU 速度），总帧时间降为次要参考。公共 API（+2 +1 重载）/ 用例（319→~331）/ 批次 / 文件范围 / CMake 全部不变。
