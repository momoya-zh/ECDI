# TextBox 垂直滚动条（textbox-scrollbar）

> **来源**：`examples/ModelProbe/ModelProbe.cpp:434` 的 JSON 预览区——用户 2026-09-30 反馈「JSON 那个位置似乎还是**老的手写滚动**」。
> **勘察结论**：它**不是手搓**——`TextBox` 自带 Phase 8.5.2 的滚轮滚动（`OnMouseWheel`）+ 光标跟随（`EnsureCaretVisible`）+ `Ctrl+A/C` 复制；**真正缺的是滚动条**（模型列表的 `ScrollView` 自带 `ScrollBar`，TextBox 没有）。
> **定位**：**跨阶段的框架能力扩展**（非新 Phase ⇒ 不用 `phaseN-*` 前缀，同 `window-ownership.md` 先例；阶段由本文件头部与 §7 修订记录跟踪）。
> **状态**：**v1.0 待评审**（2026-09-30）——本稿是**需求 + 设计 + 验收的合并稿**（接口极简、不新增公共头、不引入新概念，故不拆三件套）。

---

## 1. 动机与现状勘察（全部实测，带行号）

| # | 事实 | 证据 |
|---|---|---|
| **K1** | `TextBox` **默认多行** | `ECDI/include/ECDI/Widget/TextBox.h:267` `bool m_singleLine = false;` |
| **K2** | 已有**滚轮垂直滚动** | `ECDI/src/Widget/TextBox.cpp:447-456`（`OnMouseWheel`，8.5.2）；声明在 `TextBox.h:179` |
| **K3** | 已有**光标跟随滚动** | `TextBox.cpp:405-421`（`EnsureCaretVisible`），全文件 8 处调用 |
| **K4** | 内部偏移是 `float m_scrollOffsetY` | `TextBox.h:311` |
| **K5** | ★★ **无滚动条** | `TextBox.h/.cpp` 全文件 `ScrollBar` **零命中** |
| **K6** | ★★ **TextBox 一直是叶子控件**（零子节点） | `AddChild` / `m_children` 在 `TextBox.h/.cpp` **零命中** |
| **K7** | 但基类机制**本来就支持**子节点 | `Widget.cpp:268-270`（`Paint` 遍历子节点，且在 `OnPaint` **之后** ⇒ 条画在文本之上）· `Widget.cpp:139-149`（`HitTest` 递归子节点，**逆序** = 后添加者优先） |
| **K8** | `TextBox` **只** override `OnPaint`，未碰 `HitTest` / `Paint` / `Arrange` | `TextBox.h:184`——⇒ **加子节点零机制改动** |
| **K9** | ★★ **不折行**（只按 `\n` 分行） | `TextBox.cpp:336-349`（`RecalculateLines`）；全文件 `折行` / `wrap` **零命中** ⇒ **扣宽度不影响内容高度** ⇒ **可见性判定无循环依赖** |
| **K10** | `ScrollBar` = 纯展示 + 输入层，**偏移权威归容器** | `ScrollBar.h:16-18`（D11）· API：`SetRange` / `SetOffset`（**不回调**）/ `SetOnOffsetChanged`（**用户操作**触发）/ `GetThickness` / `GetOrientation` |
| **K11** | `ScrollView` 的集成模式**可照抄** | `ScrollView.cpp:22,26,30-31`（创建 + 挂回调）· `:251-254`（摆位）· `:283-301`（`SyncBars`：可见性 + 范围 + 偏移） |
| **K12** | 文本区宽度**单点可得** | `TextBox.cpp:460-464`（`GetTextAreaWidth()`）——绘制裁切 / 光标定位 / 点击定位 / 拖选**共用同一个入口** |
| **K13** | 唯一多行消费者 = ModelProbe 的 JSON 预览 | `ModelProbe.cpp:434`（600×120 · `SetReadOnly` · **未** `SetSingleLine`）；同文件另 3 个 TextBox 皆单行（`:177` BaseURL · `:203` Key · `:283` 搜索） |

**动机**：一个多行只读文本框**没有任何滚动指示**——用户不知道内容有多长、也不知道能滚。而框架里 `ScrollView` 已经提供了滚动条（Phase 15 R2），只是 `TextBox` 没有接。

---

## 2. 范围内（改动清单）

**无新文件 · CMake 0 改动 · Public 头 92 → 92（不新增头，`ScrollBar.h` 已存在）**

| △ | 落点 | 改动 |
|---|---|---|
| **△1** | `ECDI/include/ECDI/Widget/TextBox.h` | +`class ScrollBar;` 前置声明 · +`ScrollBar* m_vBar = nullptr;`（**非拥有**——子节点归树）· +`void SyncScrollBar();`（私有）· +`[[nodiscard]] ScrollBar* GetVerticalScrollBar() noexcept;`（公开访问器，**先例 `ScrollView.h:118`**） |
| **△2** | `ECDI/src/Widget/TextBox.cpp` 两个构造 | 创建 `ScrollBar(Orientation::Vertical)` + `AddChild` + `SetOnOffsetChanged([this](int o){ m_scrollOffsetY = static_cast<float>(o); Invalidate(); SyncTextInputCaret(); })`——★ 收尾两句**与 `OnMouseWheel` 同款**（`:454-455`） |
| **△3** | `SetSize`（`:76` 已 override） | 摆条：`SetSize(GetThickness(), GetHeight())` + `SetPosition(GetWidth() - GetThickness(), 0)`（照抄 `ScrollView.cpp:251-254`）+ `SyncScrollBar()` |
| **△4** | 新增 `SyncScrollBar()` | `const bool need = !m_singleLine && GetMaxScrollOffset() > 0.0f;` ⇒ `SetVisible(need)` · `SetRange(内容高, 视口高)` · `SetOffset(static_cast<int>(m_scrollOffsetY))`（照抄 `ScrollView::SyncBars` 三步） |
| **△5** | `GetTextAreaWidth()`（`:460-464`） | 条**可见**时扣除 `GetThickness()`——★ **单点改动** ⇒ 绘制裁切 / 光标 / 点击 / 拖选自动跟随 |
| **△6** | 调用 `SyncScrollBar()` 的时机 | `RecalculateLines()` 之后 · `OnMouseWheel` 之后 · `EnsureCaretVisible()` 之后 · `SetText`（两个重载）之后 · `ApplyTheme` 之后 |
| **△7** | `ECDI/src/Tests/TextBoxTests.cpp` | +≈6 用例（见 §6） |

---

## 3. 关键决策点（含倾向）

| # | 决策 | 倾向 | 理由 |
|---|---|---|---|
| **D1** | 只做**垂直**条 | ✅ | `TextBox` 不折行（K9）⇒ 垂直溢出是唯一需要条的场景；`m_scrollOffsetX` 是**跟手模式**（光标边界推导），不是滚动条语义 |
| **D2** | **仅多行**（`!IsSingleLine()`）显示 | ✅ | 单行高度 = 一行视口 ⇒ 垂直永不溢出，`need` 判据天然覆盖；显式判 `m_singleLine` 只是省一次计算，语义更清楚 |
| **D3** | 可见性 = 内容高 > 视口高 | ✅ | 与 `ScrollView::SyncBars`（`:289`）**同一口径**，两处行为可互相解释 |
| **D4** | 条可见时**文本区让位**（扣厚度） | ✅ | 否则文本被条压住（滚动条覆盖最后几像素）⇒ 末列字符永远看不清 |
| **D5** | 条样式走 `ScrollBarStyle` | ✅ | 主题与能力正交（既有原则）；`ScrollBar` 已支持 `ApplyTheme` |
| **D6** | `float` 偏移 ↔ `int` 条偏移，**取整** | ✅ | 条是 UI 层，不需要亚像素；**TextBox 内部 float 精度不变**（`GetScrollOffsetY()` 契约不受影响） |
| **D7** | 条位置：右侧贴边、全高 | ✅ | 照抄 `ScrollView.cpp:251-254`（同款观感） |
| **D8** | **不**加公开 `SetScrollBarVisible` / `SetScrollBarStyle` | ✅ | 自动判定 + 主题已足够；**第二个消费者出现再加**（YAGNI） |

---

## 4. 范围外（YAGNI / 已否决）

| 项 | 处置 | 理由 |
|---|---|---|
| **把 `TextBox` 放进 `ScrollView`**（用户初始设想） | ❌ **否决** | ① **双滚动系统**（TextBox 内部 offset ↔ ScrollView 内容偏移）互相打架；② **光标几何 / IME 候选窗定位错乱**——`GetCaretClientGeometry` 假定自身就是视口；③ 降级成只读展示会**丢失 `Ctrl+A/C` 复制**（而 `ModelProbe.cpp:433` 明确需要）。★ **「列表用 `ScrollView`」与「文本框自持滚动」在语义上本来就该不同** |
| 水平滚动条 | 不做 | 不折行 ⇒ 长行走 `m_scrollOffsetX` 跟手模式（既有）；引条会与跟手模式语义冲突 |
| 折行（soft wrap） | 不做 | **独立能力**（会改变 K9 的前提 ⇒ 需重新论证可见性判定），不在本次范围 |
| 单行 TextBox 的滚动条 | 不做 | 垂直无溢出 |
| 公开的条显隐 / 样式开关 | 不做 | 见 D8 |

---

## 5. 与既有约束的对齐

| 约束 | 对齐点 |
|---|---|
| **四层渲染不变** | 只用既有 `DrawRect` + `DrawRoundedRect`（`ScrollBar.h:21-22` 已声明**零新增 `RenderCommand`**）——Backend 完全不知道「这里是滚动条」 |
| **组合优于继承** | `ScrollBar` 是独立 `Widget`，`TextBox` 用 `AddChild` **组合**（不继承） |
| **主题与能力正交** | 视觉走 `ScrollBarStyle`（`TextBox::ApplyTheme` 传递），能力走 `Widget` 树机制 |
| **公共头不增** | **92 → 92**（`ScrollBar.h` 早已是公共头） |
| **坐标语义** | `ScrollBar` 内部已完成「客户区绝对 → 自身局部」换算（`ScrollBar.h:105-110`）；`TextBox` 只需用**自身原点**摆位（`SetPosition`） |
| **ECDI = 教学型框架** | **复用既有控件、零新概念**——教学成本 = 「TextBox 现在也有滚动条了」一句话 |
| **零回归** | `m_vBar` 私有、公共签名不变；默认状态下（不溢出）条不可见 ⇒ **与现状逐位等价** |

---

## 6. 验收与测试

| # | 用例 | 判据 |
|---|---|---|
| **T1** | 多行 + 内容溢出 ⇒ 条可见 | `GetVerticalScrollBar()->IsVisible() == true` |
| **T2** | 多行 + 内容不溢出 ⇒ 条隐藏 | 同上 `== false`；且不可见 ⇒ **不参与命中**（`Widget::HitTest:117` 既有语义） |
| **T3** | 单行 ⇒ 条恒隐藏 | 即便文本很长 |
| **T4** | 拖拽条 ⇒ 偏移跟随 | 拖到轨道中点 ⇒ `GetScrollOffsetY()` ≈ `GetMaxScrollOffset() / 2`（容差由实现定） |
| **T5** | 滚轮滚动 ⇒ 条滑块位置跟随 | `OnMouseWheel` 后 `GetVerticalScrollBar()->GetOffset() == static_cast<int>(GetScrollOffsetY())` |
| **T6** | 条可见时文本区变窄 | `GetTextAreaWidth()` 扣除 `GetVerticalScrollBar()->GetThickness()` |
| **A1** | **既有 275 用例零回归** | 四链（MSVC / ClangCL / Clang / MinGW） |
| **A2** | **ModelProbe JSON 预览观感** | 条随内容自动显隐、可拖拽翻页，与模型列表的条**同款** |

**规模预估**：公共头 **92 → 92** · 公共 API **+1**（`GetVerticalScrollBar()`）· 用例 **275 → ≈281** · CMake **0 改动** · 断言特征串 **11 → 11**。

---

## 7. 修订记录

- **v1.0**（2026-09-30）初稿：现状勘察 **K1–K13**（全部带行号实测）· 范围 **△1–△7** · 决策 **D1–D8**（含倾向与理由）· 范围外（★ 含**否决「换 `ScrollView`」备选方案**的三条理由）· 与既有约束对齐 · 验收 **T1–T6 / A1–A2**。**待评审**。
