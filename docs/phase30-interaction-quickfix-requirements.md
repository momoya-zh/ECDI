# Phase 30 · 文本 / 交互快修小轮 —— 需求确认（v1.0）

> 来源：`roadmap-deferred.md` **#51**（2026-10-05 登记，2026-10-06 用户拍板「排 Phase 29 之后」）——harness 高保真模型评估的 4 项快修打包 + Phase 29 收口后的顺位兑现。
> 状态：**v1.0**（2026-10-07，待评审）
> 定位：**快修小轮**（三处小改 + 一处验证钉住）——非能力型 Phase；单文档走需求 → 初设 → 详设仍按五阶段纪律，但每阶段应短。

---

## 1. 背景

### 1.1 来源与形态

harness 高保真模型仿制评估（2026-10-05，9 项能力缺口 + 2 bug）中 4 项「单点、小改、零架构风险」的打包：① TextBox 光标颜色样式化 · ② Button 文字对齐 · ③ H/V 布局补 C-VIS · ④ CollapsiblePanel 收起可见性同步。Phase 29（#52 文本多行）已全链收口（2026-10-07，五链 349/349），本 Phase 为其顺位兑现。

### 1.2 与相邻条目的边界

- **与 Phase 29 的关系**：Phase 29 的开放项 **O1（wrap 每行对齐，经 `Line.width` 接入）** 显式挂账到本 Phase 的对齐枚举——本 Phase 只落**枚举 + Button 消费**，wrap 每行对齐仍留 O1（单消费者不扩面）。
- **与 Phase 25（C-VIS 契约）的关系**：③ 是把 Phase 25 的 **ListLayout C-VIS 四层结果契约**补齐到老 H/V 布局——语义**照搬不重设计**（防第二套断行/停泊方言）。
- **与 harness 的关系**：harness 侧验证夹具现成（其评估报告逐条带行号）；本 Phase 收口后 harness 可直接复跑核对。

---

## 2. 现状勘察（K1–K6，全部带行号实测 2026-10-07）

- **K1 光标颜色硬编码**：`TextBox.cpp:1351` —— `ctx.DrawRect(Rect{…, m_style.caretWidth.value, lineH}, Color::Black())`——光标**宽**已是样式项（`TextBoxStyle.h:16` `StyleField<float> caretWidth`），**颜色**却写死 `Color::Black()`。`TextBoxStyle`（`TextBoxStyle.h:8-19`）共 9 字段无 `caretColor`；主题默认注入点 = `DefaultTheme.cpp:25-33`（`caretWidth.value = 2.0f` 在 :33）。同文件选区色（`selection`）、组合串下划线色（`composition`）均已样式化——**唯独光标漏网**，属同族补齐非新能力。
- **K2 Button 文字对齐写死**：`Button.cpp:92-99` —— `Button::CalculateTextPosition` 水平**恒居中**（`offsetX = (GetWidth() - textWidth) / 2`）。基类 `TextWidget::CalculateTextPosition`（`TextWidget.cpp:96-105`）默认**左对齐** + 垂直居中（P7 定案）——全库**唯独 Button 覆写并写死**。`ButtonStyle`（`ButtonStyle.h:8-18`）6 字段无对齐项。消费者诉求（harness）：左对齐文本的按钮（如菜单项形态）无路可走。
- **K3 H/V 布局完全无 IsVisible 处理**：`VerticalLayout.cpp:20-27`（第一遍求和：隐藏子的尺寸**照常计入** `fixedTotal`/`totalStretch`）、`:39`（`m_spacing * (count - 1)`——**隐藏子也吃间隙**）、`:44-60`（第二遍分配：隐藏子**照常占槽**）。`HorizontalLayout.cpp:20-27/39/44-60` 同构。后果：`SetVisible(false)` 的子在 H/V 布局下**仍占槽位 + 仍产生间隙**——语义上「隐藏了却留着空洞」。对照 `ListLayout.cpp:17-30`：Phase 25 已落 **C-VIS 四层契约**（不可见子跳过 + 停泊自身 bbox 负区 `(−w, −h)` + 连续槽位）——**同为布局层，行为却不一致**。
- **K4 CollapsiblePanel 收起可见性——框架侧已具备（勘误 #51 登记项）**：`CollapsiblePanel.cpp:191-197` `SetContentVisible` = `m_content->SetVisible(visible)`；调用点全覆盖——构造默认收起（:22）/ 展开开始（:42）/ 无窗口瞬时折叠（:85）/ **折叠动画 onFinished**（:104，动画期间靠父 Clip 裁切——设计如此）。子树隐藏经 `Widget` **入口门控**（`Widget.cpp:114-121` 命中 + `Widget::Paint` 入口）整体生效。`SetContentVisible` 自 **81037ed（2026-09-04，Phase 9.5）** 即存在——早于 harness 评估（2026-10-05）。**结论：harness 报告的 bug2 针对其自研仿制模型，非 ECDI HEAD**（同 viewport-cull 报告教训：外部报告先查版本）。处置见 §3.1（验证钉住，不立项改码）。
- **K5 主题管线的既有形态**（①② 的接入面）：样式字段三件套 = `StyleField<T>`（声明）+ `std::optional<T>`（Override）+ `DefaultTheme` 注入 + `ApplyTheme` 只更新未 Override 属性（D7 契约）——①② 各加一个字段即沿既有四点接线，无新机制。
- **K6 对齐枚举的复用面**：Phase 29 O1（wrap 每行对齐）与本 Phase ② 共享同一枚举；枚举应为**纯值类型、零依赖**（公共头自包含纪律），落位见 D2。

---

## 3. 范围

### 3.1 做（倾向）

1. **① 光标颜色样式化**：`TextBoxStyle` +`StyleField<Color> caretColor`（默认 **Black**——零行为变化）+ Override 字段 + 主题注入 + `TextBox.cpp:1351` 消费（`Color::Black()` → `m_style.caretColor.value`）。
2. **② Button 文字对齐**：新枚举 `TextAlignment`（Left / Center / Right）+ `ButtonStyle` 对齐字段（默认 **Center**——零行为变化）+ Override 字段 + `Button::CalculateTextPosition` 消费（水平按枚举、**垂直恒居中**——三选项共用）。
3. **③ H/V 布局补 C-VIS**：`VerticalLayout` / `HorizontalLayout` 对齐 ListLayout 四层契约——不可见子**跳过 + 停泊** `(−w, −h)`、可见子**连续槽位**、**间隙只算可见**（`visibleCount - 1`）、**末位可见 stretch 子吃余数**、第一遍求和排除隐藏子。
4. **④ CollapsiblePanel 验证钉住**：新增回归用例钉住现有行为（收起 ⇒ 内容子树**不绘制且不可命中**；展开 ⇒ 恢复；重复 Toggle 幂等），**零代码改动**——并把「框架侧已具备」的勘误写回 #51 台账。

### 3.2 非目标

- **wrap 每行对齐**（Phase 29 O1）——本 Phase 只落枚举与 Button 消费，wrap 侧留 O1。
- TextBox 光标**形状/闪烁频率**样式化——只有颜色有消费者证据。
- 垂直对齐（Top/Middle/Bottom）——无消费者证据；P7 垂直居中契约不动。
- ListLayout / GridLayout 的 C-VIS **重设计**——已就位，语义照搬不被本 Phase 触碰。
- CollapsiblePanel 的收起动画行为改动（K4 已具备）。

---

## 4. 待决点（倾向已给，待评审）

- **D1 caretColor 形态**：`TextBoxStyle` +`StyleField<Color>`（`caretWidth` 同族——倾向明确：同族补齐，默认 Black 零行为变化；评审只需核对接线四点齐不齐）。
- **D2 对齐枚举落位**：`TextAlignment` 独立小公共头 `ECDI/Core/TextAlignment.h`（纯值类型零依赖——公共头自包含纪律；**倾向**）vs 塞进 `ButtonStyle.h`（不加文件但 O1 复用时要搬家）。**倾向独立头**：公共头 **94 → 95**，如实记账。
- **D3 H/V C-VIS 语义边界**：**照搬 ListLayout 四层契约**（倾向明确——防第二方言）。评审须裁的细节：① **全可见 ⇒ 结果逐位不变**（零回归红线——spacing 公式在 `visibleCount == count` 时退化为现式）；② **全部隐藏** ⇒ 无间隙无分配、全部停泊（不 panic）；③ **动画中隐藏**（H/V 无动画语义——不在范围）；④ 停泊位 = `(−w, −h)`（C-VIS-4 原文）。
- **D4 CollapsiblePanel 处置**：**验证钉住不立项改码**（倾向明确——K4 勘误成立即应改 #51 台账，把「4 项快修」修正为「3 改 + 1 钉」；评审确认）。

---

## 5. 验收方向（A1–A4，待初设细化）

- **A1 存量全绿**（默认零变化红线）：349 用例全绿——光标默认 Black、Button 默认 Center、**全可见布局结果逐位不变**（③ 的最强回归锚）。
- **A2 行为断言**（命令流 / 坐标级）：① 光标 `DrawRect` 颜色 = `caretColor.value`（默认 Black + override 两态）；② Button 三种对齐的 `DrawText.pos.x` 各就各位（左 = padding 起点 / 右 = 宽 − 文本宽 / 中 = 现状），垂直 y 不变；③ H/V：隐藏子停泊 `(−w, −h)` 且不占槽、可见子连续无洞、间隙数 = 可见数 − 1、末位可见 stretch 吃余数、全隐藏不 panic。
- **A3 C-VIS 语义一致**：H/V 与 ListLayout 对同一组可见性变化的**结果契约**一致（C-VIS-1..3 层面——测行为不测坐标技巧，Phase 25 纪律）。
- **A4 确定性**：无 locale/NLS、无时间依赖；CollapsiblePanel 用例断言幂等（重复 Toggle 无累积效应）。

---

## 6. 影响面预算（初估，待初设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 95**（+`Core/TextAlignment.h`，D2 倾向；若评审裁定塞 `ButtonStyle.h` 则 94 → 94） |
| 公共 API | **+1 类型**（`TextAlignment` 枚举）**+ 4 样式字段**（`caretColor` Style+Override、`textAlignment` Style+Override——与 Phase 29 的虚函数计数口径不同族，如实分开记） |
| 用例 | **349 → ~355**（+6：光标 1 · 对齐 1·2 · H/V C-VIS 2 · CollapsiblePanel 1——初设分解） |
| CMake | **0**（GLOB 自动入库；TextAlignment.h 无 .cpp） |
| 风险 | **低**——三处消费点全带「默认 = 现行为」的零变化红线；唯一行为变化（H/V 隐藏子）有 ListLayout 先例契约背书 |

---

## 7. 修订记录

- **v1.0**（2026-10-07）初稿。**输入**：#51 登记（roadmap v1.87 / audit v1.53，2026-10-05）+ 用户拍板排期（2026-10-06）+ 四项带行号勘察（2026-10-07，K1–K6）。**★ 勘察修正 #51 登记项**：④ CollapsiblePanel **框架侧已具备**（`SetContentVisible` 自 81037ed / 2026-09-04 即全路径同步 `SetVisible`；子树经 Widget 入口门控整体隐藏）——harness bug2 针对其自研仿制模型而非 ECDI HEAD（同 viewport-cull 报告「先查版本」教训），处置 = 验证钉住 + 台账勘误，不改码。范围 = ① caretColor（同族补齐，默认 Black）· ② TextAlignment 枚举 + Button 消费（默认 Center；O1 复用面）· ③ H/V 补 C-VIS（照搬 ListLayout 四层契约；全可见逐位不变红线）· ④ 验证钉住。K1–K6 带行号 · D1–D4 倾向已给 · A1–A4 · 影响面 94→95 / API +1 类型 +4 字段 / 用例 349→~355 / CMake 0 / 风险 低。
