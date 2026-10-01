# TextBox 垂直滚动条（textbox-scrollbar）

> **来源**：`examples/ModelProbe/ModelProbe.cpp:434` 的 JSON 预览区——用户 2026-09-30 反馈「JSON 那个位置似乎还是**老的手写滚动**」。
> **勘察结论**：它**不是手搓**——`TextBox` 自带 Phase 8.5.2 的滚轮滚动（`OnMouseWheel`）+ 光标跟随（`EnsureCaretVisible`）+ `Ctrl+A/C` 复制；**真正缺的是滚动条**（模型列表的 `ScrollView` 自带 `ScrollBar`，TextBox 没有）。
> **定位**：**跨阶段的框架能力扩展**（非新 Phase ⇒ 不用 `phaseN-*` 前缀，同 `window-ownership.md` 先例；阶段由本文件头部与 §8 修订记录跟踪）。
> **状态**：**v1.2（2026-10-01）** —— ★ **已实施**（`TextBox.h` · `TextBox.cpp` · `TextBoxTests.cpp`）：**沙箱 MinGW @ `_DEBUG`（断言启用）283 / 283 全绿**（DPI 120 / 125%），**待四链（MSVC / ClangCL / Clang）复核**。承接 v1.1 的**外部评审（GPT 11 条）** + **本机源码复核（R1–R3）**；★ **实施期修正四处**见 §8 修订记录。

---

## 1. 动机与现状勘察（全部实测，带行号）

| # | 事实 | 证据 |
|---|---|---|
| **K1** | `TextBox` **默认多行** | `ECDI/include/ECDI/Widget/TextBox.h:267` `bool m_singleLine = false;` |
| **K2** | 已有**滚轮垂直滚动** | `ECDI/src/Widget/TextBox.cpp:447-456`（`OnMouseWheel`，8.5.2）；声明在 `TextBox.h:179` |
| **K3** | 已有**光标跟随滚动** | `TextBox.cpp:405-421`（`EnsureCaretVisible`）；★ **全文件 21 处调用**（不含定义行 405）——v1.0 误记为「8 处」，2026-10-01 复核订正 |
| **K4** | 内部偏移是 `float m_scrollOffsetY` | `TextBox.h:311` |
| **K5** | ★★ **无滚动条** | `TextBox.h/.cpp` 全文件 `ScrollBar` **零命中**（复核 2026-10-01：两文件各 0 处） |
| **K6** | ★★ **TextBox 一直是叶子控件**（零子节点） | `AddChild` / `m_children` 在 `TextBox.h/.cpp` **零命中**（复核 2026-10-01：各 0 处） |
| **K7** | 但基类机制**本来就支持**子节点 | `Widget.cpp:268-270`（`Paint` 遍历子节点，且在 `OnPaint` **之后** ⇒ 条画在文本之上）· `Widget.cpp:139-149`（`HitTest` 递归子节点，**逆序** = 后添加者优先） |
| **K8** | `TextBox` **只** override `OnPaint`，未碰 `HitTest` / `Paint` / `Arrange` | `TextBox.h:184`——⇒ **加子节点零机制改动** |
| **K9** | ★★ **不折行**（只按 `\n` 分行） | `TextBox.cpp:336-349`（`RecalculateLines`）；全文件 `折行` / `wrap` **零命中** ⇒ **扣宽度不影响内容高度** ⇒ **可见性判定无循环依赖**（★ 复核确认：`GetMaxScrollOffset()`（`TextBox.cpp:398-404`）只经 `GetTextAreaHeight()`，**只含高度**，完全不看宽度 ✓） |
| **K10** | `ScrollBar` = 纯展示 + 输入层，**偏移权威归容器** | `ScrollBar.h:16-18`（D11）· API：`SetRange` / `SetOffset`（**不回调**）/ `SetOnOffsetChanged`（**用户操作**触发）/ `GetThickness` / `GetOrientation` |
| **K11** | `ScrollView` 的集成模式**可照抄** | `ScrollView.cpp:21-31`（创建 + 挂回调）· `:251-254`（摆位）· `:283-301`（`SyncBars`：可见性 + 范围 + 偏移） |
| **K12** | 文本区宽度**单点可得** | `TextBox.cpp:460-464`（`GetTextAreaWidth()`）——绘制裁切 / 光标定位 / 点击定位 / 拖选**共用同一个入口** |
| **K13** | 唯一多行消费者 = ModelProbe 的 JSON 预览 | `ModelProbe.cpp:434`（600×120 · `SetReadOnly` · **未** `SetSingleLine`——实为 `:435` `SetSize(600,120)` / `:436` `SetReadOnly(true)`）；同文件另 3 个 TextBox 皆单行 |
| **K14** | ★ `Widget::SetVisible` **仅赋值、不 `Invalidate`**、非虚 | `Widget.h:148` `void SetVisible(bool v) { m_state.visible = v; }` ⇒ 每帧下发**无副作用** |
| **K15** | ★★ `ScrollBar::SetRange` **无同值守卫、无条件 `Invalidate()`** | `ScrollBar.cpp:25-32` ⇒ **每帧无条件同步会自激励重绘**（见 §7 C-2）；对照 `SetOffset` **有**同值守卫（`:44-48`） |
| **K16** | ★ `m_children` 是**基类成员**；`ScrollBar::ApplyTheme(const Theme&)` 已存在 | `Widget.h:289` / `ScrollBar.h:70`（⇒ §7 C-3 / C-4 成立） |

**动机**：一个多行只读文本框**没有任何滚动指示**——用户不知道内容有多长、也不知道能滚。而框架里 `ScrollView` 已经提供了滚动条（Phase 15 R2），只是 `TextBox` 没有接。

---

## 2. 范围内（改动清单）

**无新文件 · CMake 0 改动 · Public 头 92 → 92（不新增头，`ScrollBar.h` 已存在）**

| △ | 落点 | 改动 |
|---|---|---|
| **△1** | `ECDI/include/ECDI/Widget/TextBox.h` | +`class ScrollBar;` 前置声明 · +`ScrollBar* m_vBar = nullptr;`（**非拥有缓存指针**——子节点归树；见 §7 C-3）· +`void SyncScrollBar();`（私有）· +`[[nodiscard]] ScrollBar* GetVerticalScrollBar() noexcept;`（公开访问器，**先例 `ScrollView.h:118`**；定位见 **D9**）· +同步守卫缓存（见 §7 C-2） |
| **△2** | `ECDI/src/Widget/TextBox.cpp` 两个构造 | 创建 `ScrollBar(ScrollBar::Orientation::Vertical)` + `AddChild` + `SetOnOffsetChanged([this](int o){ m_scrollOffsetY = static_cast<float>(o); Invalidate(); SyncTextInputCaret(); })`——★ 收尾两句**与 `OnMouseWheel` 同款**（`:454-455`）。★★ **枚举是嵌套声明的**（`ScrollBar.h:32` `enum class Orientation`）⇒ **必须写 `ScrollBar::Orientation::Vertical`**（v1.0 简写为 `Orientation::Vertical`，会编译不过——R2 订正） |
| **△3** | `SetSize`（`TextBox.h:76` 已 override；定义 `TextBox.cpp:156`） | 摆条：`SetSize(GetThickness(), GetHeight())` + `SetPosition(GetWidth() - GetThickness(), 0)`（照抄 `ScrollView.cpp:251-254`）+ `SyncScrollBar()` |
| **△4** | 新增 `SyncScrollBar()` | ① `const bool need = !m_singleLine && GetMaxScrollOffset() > 0.0f;` ② `SetRange(GetMaxScrollOffset() + GetTextAreaHeight(), GetTextAreaHeight())`（★ 先例 `ScrollView.cpp:288` 传的是**内容总高**；v1.0 只写「内容高」——R3 定死公式）③ `SetOffset(static_cast<int>(m_scrollOffsetY))`；★ **三步都包在「值变化守卫」里**（§7 C-2） |
| **△5** | `GetTextAreaWidth()`（`TextBox.cpp:460-464`） | 条**可见**时扣除 `GetThickness()`——★ **单点改动** ⇒ 绘制裁切 / 光标 / 点击 / 拖选自动跟随 |
| **△6** | 同步时机 | ★★ **由「触发点清单」改为「`OnPaint` 起始处的惰性同步」**（先例 = 同文件 `OnPaint` 的 `RecalculateLines()` 惰性重算，`TextBox.cpp:1114`）。**理由**：触发点清单**盖不住字体路径**——`GetLineHeight()`（`:377-384`）实时读 `m_style.font.value`，而 `TextWidget::SetFont`（`TextWidget.cpp:61-68`）→ `SetStyle`（`:81-87`）**只 `Invalidate()`**、且二者**非虚**（TextBox 无从挂钩）⇒ 改字体后条的 range / 可见性会**陈旧**（R1）。★ 惰性同步**一处覆盖全部路径**；前提是 §7 C-2 的值变化守卫 |
| **△7** | `ApplyTheme`（`TextBox.cpp:42-57`） | 尾部补 `if (m_vBar != nullptr) m_vBar->ApplyTheme(theme);`——★ **走条自己的主题入口**，**不在 TextBox 里复制 `ScrollBarStyle` 属性**（§7 C-4 / 外部评审 §6） |
| **△8** | `ECDI/src/Tests/TextBoxTests.cpp` | +**8** 用例（见 §6） |

---

## 3. 关键决策点（含倾向）

| # | 决策 | 倾向 | 理由 |
|---|---|---|---|
| **D1** | 只做**垂直**条 | ✅ | `TextBox` 不折行（K9）⇒ 垂直溢出是唯一需要条的场景；`m_scrollOffsetX` 是**跟手模式**（光标边界推导），不是滚动条语义 |
| **D2** | **仅多行**（`!IsSingleLine()`）显示 | ✅ | 单行高度 = 一行视口 ⇒ 垂直永不溢出，`need` 判据天然覆盖；显式判 `m_singleLine` 只是省一次计算，语义更清楚 |
| **D3** | 可见性 = 内容高 > 视口高 | ✅ | 与 `ScrollView::SyncBars`（`:289`）**同一口径**，两处行为可互相解释 |
| **D4** | 条可见时**文本区让位**（扣厚度） | ✅ | 否则文本被条压住 ⇒ 末列字符永远看不清；★ 扣宽度**不会**反向影响内容高（K9 + `GetMaxScrollOffset` 只看高度）⇒ 无循环 |
| **D5** | 条样式走 `ScrollBarStyle` | ✅ | 主题与能力正交（既有原则）；条的主题**传递给条自己**（△7 / C-4） |
| **D6** | `float` 偏移 ↔ `int` 条偏移，**取整** | ✅ | 条是 UI 层，不需要亚像素；**TextBox 内部 float 精度不变**（`GetScrollOffsetY()` 契约不受影响）。★ **已知代价**：条侧 `int` 与 TextBox 侧 `float` 的 max offset 可差 **≤1px**（`SetRange` 只吃 `int`）——末位 1px 内不保证逐像素对齐 |
| **D7** | 条位置：右侧贴边、全高 | ✅ | 照抄 `ScrollView.cpp:251-254`（同款观感） |
| **D8** | **不**加公开 `SetScrollBarVisible` / `SetScrollBarStyle` | ✅ | 自动判定 + 主题已足够；**第二个消费者出现再加**（YAGNI） |
| **D9** | `GetVerticalScrollBar()` 的定位 = **「访问内部组成控件的只读观察入口」**，**不是第二套滚动控制权** | ✅ | 滚动权威始终是 `m_scrollOffsetY`（第三方不可经条改写状态，条本身也**不回调**自身 `SetOffset`——`ScrollBar.cpp:52`）；该访问器服务于**测试观察**（§6 的 T1–T8 全靠它）、**用户自写验收**与 **API 风格一致**（`ScrollView.h:118` 先例）。★ **否决替代方案**「靠 friend 给测试开后门」——放宽封装比多一个只读访问器更差 |
| **D10** | 同步方式 = **`OnPaint` 起始处惰性同步 + 值变化守卫** | ✅ | 取代 v1.0 的触发点清单（△6）；守卫是**必需**而非可选（§7 C-2：`SetRange` 无条件 `Invalidate`） |

---

## 4. 范围外（YAGNI / 已否决）

| 项 | 处置 | 理由 |
|---|---|---|
| **把 `TextBox` 放进 `ScrollView`**（用户初始设想） | ❌ **否决** | ① **双滚动系统**（TextBox 内部 offset ↔ ScrollView 内容偏移）互相打架；② **光标几何 / IME 候选窗定位错乱**——`GetCaretClientGeometry` 假定自身就是视口；③ 降级成只读展示会**丢失 `Ctrl+A/C` 复制**（而 `ModelProbe.cpp:433` 明确需要）。★ **「列表用 `ScrollView`」与「文本框自持滚动」在语义上本来就该不同** |
| 水平滚动条 | 不做 | 不折行 ⇒ 长行走 `m_scrollOffsetX` 跟手模式（既有）；引条会与跟手模式语义冲突 |
| 折行（soft wrap） | 不做 | **独立能力**（会改变 K9 的前提 ⇒ 需重新论证可见性判定），不在本次范围。★ **本设计对它的依赖见 §7 C-1** |
| 单行 TextBox 的滚动条 | 不做 | 垂直无溢出 |
| 公开的条显隐 / 样式开关 | 不做 | 见 D8 |
| 改 `ScrollBar` 为「`SetRange` 带同值守卫」 | 本次不做 | 那是**条自身的改进**（K15 记录在案），本次用 §7 C-2 的守卫在 TextBox 侧规避；**不为本需求改动共享组件** |

---

## 5. 与既有约束的对齐

| 约束 | 对齐点 |
|---|---|
| **四层渲染不变** | 只用既有 `DrawRect` + `DrawRoundedRect`（`ScrollBar.h:21-22` 已声明**零新增 `RenderCommand`**）——Backend 完全不知道「这里是滚动条」 |
| **组合优于继承** | `ScrollBar` 是独立 `Widget`，`TextBox` 用 `AddChild` **组合**（不继承） |
| **主题与能力正交** | 视觉走 `ScrollBarStyle`，且**由条自己 ApplyTheme**（△7）；能力走 `Widget` 树机制 |
| **公共头不增** | **92 → 92**（`ScrollBar.h` 早已是公共头） |
| **坐标语义** | `ScrollBar` 内部已完成「客户区绝对 → 自身局部」换算（`ScrollBar.h:105-110`）；`TextBox` 只需用**自身原点**摆位（`SetPosition`） |
| **ECDI = 教学型框架** | **复用既有控件、零新概念**——教学成本 = 「TextBox 现在也有滚动条了」一句话 |
| **零回归** | ★ **措辞订正（外部评审 §11）**：默认不溢出状态下 `SetVisible(false)` ⇒ **用户可见行为与现状一致**。★ **不是「逐位等价」**——不可见子节点**仍会被 `Widget::Paint` 遍历调用一次**（子节点自己在 `Widget.cpp:250-251` 才提前返回）、`HitTest` 亦会做一次可见性检查（`:117`）、`ApplyTheme` 多一次调用、`SetSize` 多两次摆位 ⇒ **多出的是不可观测的开销，不是可观测的差异**。原 v1.0 的「逐位等价」说满了 |

---

## 6. 验收与测试

| # | 用例 | 判据 |
|---|---|---|
| **T1** | 多行 + 内容溢出 ⇒ 条可见 | `GetVerticalScrollBar()->IsVisible() == true` |
| **T2** | 多行 + 内容不溢出 ⇒ 条隐藏 | 同上 `== false`；且不可见 ⇒ **不参与命中**（`Widget::HitTest:117` 既有语义） |
| **T3** | 单行 ⇒ 条恒隐藏 | 即便文本很长 |
| **T4** | ★ **条可命中 + 偏移权威归容器**（★ v1.2 实施期重定义——见 §8 **修正 4**） | ① `HitTest` 在条区域**逆序优先命中条本身**（`== GetVerticalScrollBar()`）；② 条自身 `SetOffset(0)` **不改变** `TextBox::GetScrollOffsetY()`（**D9 / K10 的直接验收**——条不是第二控制权）。★ 原「拖条 ⇒ 同步」语义**无法无头观测**（条的 `OnMouseButtonDown/Move` 是 protected，且**不能** `static_cast` 成测试子类——UB）⇒ 该通道降级 **A2 人工验收** |
| **T5** | **滚轮路径**：滚轮滚动 ⇒ 条滑块位置跟随 | `OnMouseWheel` 后 `GetVerticalScrollBar()->GetOffset() == static_cast<int>(GetScrollOffsetY())` |
| **T6** | 条可见时文本区变窄（★ v1.2：改**命令流观测**——见 §8 **修正 1**） | 同尺寸「有 / 无条」两态**相减**：命令流**第 2 个 `PushClip`**（`TextBox::OnPaint` 的文本区）宽度差 == `GetVerticalScrollBar()->GetThickness()`。★ 原稿直测 `GetTextAreaWidth()` 不可行——它是 **private**，`using` 暴露不成立 |
| **T7** | ★★ **光标路径**（新增，外部评审 §5）：`EnsureCaretVisible()` 自动滚动 ⇒ 条同步 | 把光标移到文末（编辑操作或 `SetCaretIndex`）后：`GetScrollOffsetY() > 0` **且** `GetVerticalScrollBar()->GetOffset() == static_cast<int>(GetScrollOffsetY())`。★ 动机：T5 只证明**鼠标**驱动，T7 证明**TextBox 自己改 offset** 也驱动条——**这是组合设计最容易漏掉的一条同步路径** |
| **T8** | ★★ **惰性同步兜底**（△6 / D10；★ v1.2 重定义，原「字体路径」降级 A2——见 §8 **修正 2**） | 先构造「不溢出」（条隐藏）⇒ `SetText(长文本)`（**不走任何显式同步**：实测 `SetText` 只置行缓存失效 + `Invalidate`）⇒ 未绘制前 `IsVisible()` 仍 false ⇒ **仅一次 `Paint`** ⇒ `IsVisible() == true`。★ 无头环境 `GetLineHeight()` 恒走 16px 兜底 ⇒ 字体变化不可观测 |
| **A1** | **既有 275 用例零回归** | 四链（MSVC / ClangCL / Clang / MinGW） |
| **A2** | **ModelProbe JSON 预览观感**（★ v1.2：**兼收两条无法无头观测的路径**——**字体路径** + **条 → TextBox 的用户操作通道**） | 条随内容自动显隐 · 可拖拽翻页 · **改字号后条的范围随之变化**，与模型列表的条**同款** |

**规模预估**：公共头 **92 → 92** · 公共 API **+1**（`GetVerticalScrollBar()`）· 用例 **275 → ≈283**（+8）· CMake **0 改动** · 断言特征串 **11 → 11**。

---

## 7. 实现约束（v1.1 新增——实施前必须遵守）

| # | 约束 | 依据 |
|---|---|---|
| **C-1** | ★★ **本设计依赖 `TextBox` 当前「不自动折行」的既有语义**（K9）。可见性判定之所以不需要「先假定条存在 → 算高度 → 再决定条是否存在」的两阶段布局，正是因为**宽度不反向影响内容高度**。**若未来引入 soft wrap（折行），`SyncScrollBar()` 的可见性判定必须重新设计**（届时 `GetTextAreaWidth()` ← 条可见性 ← `GetMaxScrollOffset()` 会形成真正的环） | 外部评审 §3；K9 + `GetMaxScrollOffset()`（`TextBox.cpp:398-404`）只吃高度 |
| **C-2** | ★★ **`SyncScrollBar()` 必须带「仅值变化才下发」的守卫**（缓存上一次下发的 `(need, contentExtent, viewportExtent, offset)`，四项全同则**直接 return**）。**这不是优化，是正确性要求**：`ScrollBar::SetRange`（`ScrollBar.cpp:25-32`）**无同值守卫且无条件 `Invalidate()`**，而懒性同步发生在 `OnPaint` 内 ⇒ 若无守卫将**每帧请求重绘 = 自激励重绘循环**。★ 对照：`ScrollBar::SetOffset` 自带同值守卫（`:44-48`）、`Widget::SetVisible` 不 `Invalidate`（`Widget.h:148`）⇒ **只有 `SetRange` 这一处需要我们在调用侧兜底** | K15 实测 |
| **C-3** | ★ **`m_vBar` 是「非拥有的缓存指针」**——仅指向 `Widget` 子树中的条，**不参与生命周期管理、不 `delete`、不需要在 `~TextBox()` 里解绑**。析构序天然成立：`m_children` 是**基类成员**（`Widget.h:289`）⇒ `~TextBox()` 体 → TextBox 自身成员 → `Widget::~Widget()` → children（含条）。★ **`~TextBox()` 内不得触碰该回调** | 外部评审 §7 / §10；`Widget.h:289` |
| **C-4** | ★ 条的主题**必须经 `m_vBar->ApplyTheme(theme)` 下传**（`ScrollBar.h:70`），**不得**在 `TextBox` 内复制 `ScrollBarStyle` 的各字段（颜色 / 圆角 / 厚度）。否则 `ScrollBarStyle` 一演变，TextBox 就会长出一套平行主题逻辑 | 外部评审 §6；主题与能力正交 |
| **C-5** | 条的偏移**只由 TextBox 下发**，条的 `SetOnOffsetChanged` **只接受用户操作**（拖拽 / 翻页）——保持 K10 的「偏移权威归容器」 | K10 · `ScrollBar.cpp:52` |

---

## 8. 修订记录

- **v1.2**（2026-10-01）★ **已实施 + 实施期修正四处**。① **产出**：`ECDI/include/ECDI/Widget/TextBox.h`（+25）· `ECDI/src/Widget/TextBox.cpp`（+92 / −1）· `ECDI/src/Tests/TextBoxTests.cpp`（+139）——合计 **3 文件 · +255 / −1**；**公共头 92 → 92** · **公共 API +1**（`GetVerticalScrollBar()`）· **用例 275 → 283**（+8）· CMake **0 改动**。② ★★ **验证**：**沙箱 MinGW @ `_DEBUG`（断言启用）· 283 / 283 全绿**（本机 DPI 120 / 125%）；★ **仍待四链**（MSVC / ClangCL / Clang）复核。③ **实施期修正四处**：
  - **修正 1（T6 观测路径）**：`GetTextAreaWidth()` 是 **真 private** —— `using` 暴露**不成立**（编译器直接报 `is private within this context`；不同于 protected 的 `CaretIndexFromPosition`）。**不改框架可见性**，改为观测**用户可见后果**：命令流**第 2 个 `PushClip`**（文本区）宽度 + 两态相减。★ 反而更强端到端（证明 Clip 真的让位了）。
  - **修正 2（T8 重定义）**：原稿「字体路径」在**无头环境不可观测**——`GetLineHeight()` 在无 Window 时恒走 **16px 兜底**，字体变化不改变内容高。⇒ 改为验证**同一机制**：「**绕开全部显式同步**的变化，靠 `OnPaint` 起始的惰性同步兜住」（`SetText` 实测只置行缓存失效 + `Invalidate`）。**字体路径归 A2 人工验收**。
  - **修正 3（△2 落成形态）**：两构造的条创建抽成私有 **`CreateVerticalScrollBar()`**（避免 10 行重复）；★ **条先于 `ApplyTheme` 创建** ⇒ 主题在构造期即**一次下传**（△7 在构造路径也生效）。
  - **修正 4（T4 语义再收一档）**：条是 TextBox 内部的普通 `ScrollBar`，其鼠标入口 **protected 且测试够不着**（`static_cast` 成测试子类 = UB）⇒ T4 改为两条**可无头观测**的断言（**可命中** + **偏移权威归容器**）。★ **条 → TextBox 的用户操作通道**由 **A2** 兜底（其 λ 与 `OnMouseWheel` 同款三行）。
- **v1.1**（2026-10-01）**吸收外部评审（GPT，11 条）+ 本机源码复核（R1–R3）**。① **K3 计数订正**：`EnsureCaretVisible` 调用点 **8 → 21**（不含定义行 405）。② **R2 订正**：△2 的构造写法必须为 **`ScrollBar::Orientation::Vertical`**（枚举嵌套，`ScrollBar.h:32`）。③ **R3 定死**：△4 的公式 = `SetRange(GetMaxScrollOffset() + GetTextAreaHeight(), GetTextAreaHeight())`（先例 `ScrollView.cpp:288`）；并写明 int/float 取整差 ≤1px（D6）。④ ★★ **R1 改设计**：△6 由「触发点清单」改为 **`OnPaint` 起始处惰性同步**——触发点清单**盖不住字体路径**（`GetLineHeight()` 实时读字体，而 `TextWidget::SetFont`/`SetStyle` 非虚且只 `Invalidate`）；配套 **D10**。⑤ ★★ **新增 §7 实现约束 C-1–C-5**，其中 **C-2 的值变化守卫是正确性要求**——实测 `ScrollBar::SetRange` 无条件 `Invalidate()`，无守卫会**自激励重绘**。⑥ **新增 K14–K16**（三处实测事实）。⑦ **T4 语义订正**（外部评审 §9）：只测 TextBox ↔ 条 的**同步契约**，不绑 `ScrollBar` 内部比例算法。⑧ **新增 T7（光标路径）/ T8（字体路径）**——T8 是 R1 的验收（外部评审 §5 的 T7 与 R1 同族：**同步路径覆盖不全**）。⑨ **§5「逐位等价」措辞订正**（外部评审 §11）：改为「**默认不溢出时用户可见行为与现状一致**」，并注明多出的是**不可观测开销**。⑩ `GetVerticalScrollBar()` 定位写入 **D9**（外部评审 §8）。⑪ 生命周期 / 主题下传写入 **C-3 / C-4**（外部评审 §6/§7/§10）。⇒ **仍待用户确认后实施**。
- **v1.0**（2026-09-30）初稿：现状勘察 **K1–K13**（全部带行号实测）· 范围 **△1–△7** · 决策 **D1–D8**（含倾向与理由）· 范围外（★ 含**否决「换 `ScrollView`」备选方案**的三条理由）· 与既有约束对齐 · 验收 **T1–T6 / A1–A2**。**待评审**。
