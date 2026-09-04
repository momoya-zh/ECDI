﻿# Phase 9.7 自适应布局 初步设计（v1.0）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-01
> 状态：待评审（用户 / GPT）
> 前置：phase9.7-adaptive-layout-requirements.md v1.0（GPT 评审通过——三决策定死 + 初设五要点）
> 定位（GPT 定调）：让 ECDI **第一次具备真正意义上的窗口尺寸自适应能力**——不是完整布局系统

---

## 1. 概述

给 Box Layout（VerticalLayout / HorizontalLayout）补上自适应三件套：**stretch 权重**（Widget 属性）、**spacing**（Layout 配置）、**窗口 resize 触发链**（OnResized → Arrange）。语义模型 = Qt Box 最小集：固定尺寸 + 剩余空间按 stretch 比例分配 + spacing + H/V 嵌套。

**验收载体 = ModelProbe**（Qt 版行为基准）：窗口 800×600 拖到 1200×800，BaseURL/Key 输入框跟随变宽、按钮保持固定尺寸、Models 列表与 Preview 吃剩余高度。不另造布局 demo。

---

## 2. 冻结决策（职责确认 + GPT 评审，实现不得弱化）

| # | 决策 | 内容 |
|---|------|------|
| F1 | **stretch 属于 Widget** | `Widget::SetStretch(int)` / `GetStretch()`，默认 0。语义 = "父 Layout 分配剩余空间时按此权重参与"；`stretch=0` ≠ 固定尺寸，= **不参与分配**（固定尺寸由 `SetSize` 表达）。尺寸来源二分：显式 SetSize 优先 / 分配兜底 |
| F2 | **分配走 `SetSize()` 虚分派** | Layout 一律 `child->SetSize(w, h)`，禁直写 geometry——Layout 与 Widget 间的架构边界（TextBox override 的 EnsureCaretVisible / CollapsiblePanel 收起态语义全保留） |
| F3 | **spacing 属于 Layout，构造参数注入** | `VerticalLayout(int spacing = 0)` / `HorizontalLayout(int spacing = 0)`——布局配置非运行时状态，不设 setter（未来动态 UI 需求出现再加，不可逆性为零） |
| F4 | **负剩余空间** | `remaining = max(0, 可用 − 固定子尺寸和 − spacing×(n−1))`——stretch 控件按 0 分配，超出部分由 R1 Clip 裁掉。**不引入压缩/shrink 算法**（minSize/shrink 挂账，需求出现另设计） |
| F5 | **取整规则** | **前 N−1 个 stretch 子按计算值取整，最后一个吃余数**——保证 `Σ 分配值 == remaining`（100 分 1+1+1 → 33/33/34，无 1px 凭空消失） |
| F6 | **根容器无特例** | **不给 RootWidget 写任何特殊布局代码**——`RootWidget + VerticalLayout + 页面 stretch=1` 自然解决吃满窗口（已核实：RootWidget 是普通 Widget，Window 构造 `make_unique<Widget>`，SetLayout 直接可用） |
| F7 | **向后兼容硬契约** | 全子 stretch=0 + spacing=0（+决策 D1 选默认 false 的 cross 选项）→ V/H 行为与现状**逐字节一致**（只排位置、不碰尺寸）——LayoutTests 既有断言（只查 Position）零回归 |

---

## 3. 核心算法

### 3.1 主轴分配（GPT 冻结模型）

```text
主轴可用空间 available        （H = parent width；V = parent height）
  ↓ 减 Σ 固定子尺寸           （stretch=0 的子，主轴取其当前尺寸）
  ↓ 减 spacing × (n−1)
  ↓
remaining = max(0, …)         （F4：负数钳 0）
  ↓
totalStretch = Σ childStretch （仅 stretch>0 的子）
  ↓
每个 stretch 子：allocated_i = remaining × stretch_i / totalStretch
  ↓ 取整：前 N−1 个四舍五入（或截断——见 D2），最后一个 = remaining − Σ 前面
```

**例**（自洽修订版——GPT 原例"固定 100+100 / spacing 10×2 / stretch 1+2 → 260/520"在 4 子 3 间隙下不自洽（1000−200−30=770≠780）；此处换 spacing=0 使数据自洽，spacing 由独立用例覆盖）：宽 1000、固定 100、stretch 1+2、spacing 0 →
remaining = 1000 − 100 = 900 → stretch=1 得 **300**、stretch=2 得 **600**（Σ==1000 恒等）。

### 3.2 Arrange 伪码（HorizontalLayout 版——Vertical diff 同构，见 §5）

```cpp
void HorizontalLayout::Arrange(Widget& parent){
    // ① 固定子主轴尺寸求和 + stretch 总权重（一次遍历）
    int fixedTotal = 0; int totalStretch = 0;
    for (每个 child){
        if (child->GetStretch() > 0) totalStretch += child->GetStretch();
        else                         fixedTotal  += child->GetWidth();   // 显式尺寸
    }
    const int count = parent.GetChildCount();
    const int remaining = (std::max)(0, parent.GetWidth() - fixedTotal - m_spacing * (count - 1));   // F4

    // ② 分配：stretch 子按权重（F5 最后一个吃余数），固定子保持原尺寸
    int x = 0; int stretchAllocated = 0; size_t stretchSeen = 0; size_t stretchCount = 数出;
    for (size_t i = 0; i < count; ++i){
        child = parent.GetChildAt(i);
        if (child->GetStretch() > 0){
            ++stretchSeen;
            int w = (stretchSeen == stretchCount)
                ? remaining - stretchAllocated                                    // F5：最后吃余数
                : remaining * child->GetStretch() / totalStretch;
            stretchAllocated += w;
            child->SetSize(w, child->GetHeight());                            // F2：虚分派；cross 轴见 D1
        }
        child->SetPosition(x + (i ? m_spacing : 0)... );                      // spacing 间隔插位
        x = child 主轴末端 + spacing;
    }
}
```

（精确代码归详细设计——本节锁定**遍历两遍、固定子不 SetSize、spacing 只作用于主轴间隙**三个要点。）

### 3.3 跨轴行为——本设计唯一重大开放决策（D1）

GPT 验收图里"行随窗口变宽"隐含了**跨轴填充**：V 布局的子（行面板，固定高）宽度必须跟随父宽，否则窗口变宽后行不跟着宽、内层 H 分配无从谈起。但跨轴填充默认开启会**破坏 F7 兼容承诺**（现有子的跨轴尺寸会被改写）。

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| A. 跨轴永不碰 | 布局只管主轴（现状推广） | 最简、F7 完整保留 | **ModelProbe 验收不成立**——行不随窗口变宽 |
| B. 跨轴恒填充 | 所有子跨轴 = 父跨轴（Qt 默认行为） | Qt 心智一致 | 破坏 F7；存量 demo（ModelProbe 手写 600 宽）Arrange 后视觉即变 |
| C. **跨轴填充 = Layout 构造开关**（推荐） | `VerticalLayout(int spacing = 0, bool fillCrossAxis = false)`；开启时所有子跨轴 = 父跨轴、跨轴坐标 0 | F7 完整保留（默认 false）+ ModelProbe 显式 opt-in（改造 demo 本就是 9.7 验收的一部分） | 多一个参数；跨轴填充是"每布局"而非"每子"粒度（v0.1 够用——子级差异需求未出现） |

**倾向 C**。理由：兼容承诺是需求冻结决策，不可为省一个参数而破；ModelProbe 全部布局 opt-in 一次即可；Qt 默认填充的心智差异记录在文档（ECDI 默认不填，opt-in 才填）。**待用户/GPT 拍板后进详细设计。**

### 3.4 取整方向（D2，小决策）

前 N−1 个：四舍五入 vs 截断。倾向**截断**（floor——与整数权重乘除天然一致，无 <cmath> 依赖；最后一个吃余数自动补齐）。差异仅在半像素场景，两方案 Σ 恒等。待详设定稿。

---

## 4. 触发链

```text
WM_SIZE → Window::OnResized(w, h)
    ↓
m_rootWidget->SetSize(w, h)            （现有）
m_rootWidget->Arrange()                （新增——F6：ArrangeInternal 渐进语义，
    ↓                                   有 Layout 的节点重排 / 无 Layout 的保持手摆，递归下钻）
Invalidate()                           （新增——重排后全树重绘；resize 场景本就是全客户区失效，代价可接受）
```

- **启动路径不变**：main.cpp 首次 `Arrange()` 手动调用保留（构造期 root SetSize 不走 OnResized——Window.cpp:73 注释的既有顺序约束）；Show 后若 WM_SIZE 到达，Arrange 幂等无害。
- **重排频率**：拖拽中每 WM_SIZE 全树重排——几十控件规模无感（Qt 同为同步重排）；脏区优化记账。
- **动画交互**：CollapsiblePanel 冻结点 6（动画中外部 SetSize 不支持）不变——resize 触发的 SetSize 同属该边界，折叠动画中拖窗口的视觉退化可接受（动画 200ms 窗口极短）。

---

## 5. LinearLayout 抽象重启评估（需求决策 8 的回答）

R2 关闭时留的重启条件"第三布局立项时重启"——**本次不触发**：9.7 是增强既有两个布局，不是立项第三布局。结论：

- **维持 V/H diff 同构**（6.1 设计契约：仅 y→x / height→width 镜像），分配逻辑在两个 cpp 内镜像重复（约 25 行）；
- 不提取公共实现——6.1 评审确立的"抽象后代码不减反增、第三用例未出现"结论今天仍然成立；
- 真出现第三布局（Grid/Flow）时，提取点已经清晰（§3.1 纯计算段），届时一次到位。

---

## 6. 接口草案

```cpp
// Widget.h（新增三行——属性同 SetShowFocusRect 先例）
void SetStretch(int stretch) noexcept{ m_stretch = stretch; }        ///< 剩余空间分配权重（0 = 不参与）
[[nodiscard]] int GetStretch() const noexcept{ return m_stretch; }
int m_stretch = 0;                                                   ///< 默认 0——不参与分配（F1）

// VerticalLayout.h / HorizontalLayout.h（F3 + D1）
explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false);
explicit HorizontalLayout(int spacing = 0, bool fillCrossAxis = false);
int m_spacing;        bool m_fillCrossAxis;
```

**不新增**：Layout 基类不改（仍是 `Arrange(Widget&)` 单方法）；无 SetSpacing/SetFillCrossAxis setter（F3）；无 minSize/maxSize/alignment（挂账）。

---

## 7. 测试计划（方向）

| 组 | 用例 | 断言 |
|---|------|------|
| 基线回归 | 既有 LayoutTests 全部 | 全 stretch=0 + spacing=0 → 期望逐字节不变（F7） |
| 分配 | `Layout.StretchBasic` | 1000 宽 / 100+100 固定 / spacing 10 / stretch 1+2 → 260/520（GPT 例逐值断言） |
| 取整 | `Layout.StretchRemainder` | remaining 100 / stretch 1+1+1 → 33/33/34（F5：Σ == remaining） |
| 负剩余 | `Layout.StretchNegative` | 300 宽 / 固定 350 → stretch 子尺寸 0、位置正确、不越界（F4） |
| spacing | `Layout.SpacingPositions` | 三个固定子 + spacing 12 → x = 0 / w+12 / 2w+24 |
| 跨轴（D1 定后） | `Layout.CrossFill` | fillCrossAxis=true → 子跨轴 = 父跨轴、坐标 0；false → 不碰 |
| 触发链 | `Layout.ResizeRearrange`（若可无窗口模拟） | RootWidget 场景：SetSize 后 Arrange → stretch 子跟随 |
| 幂等 | 追加断言 | Arrange 两次结果一致（既有契约延续） |
| F2 分派 | `Layout.SetSizeDispatch` | stretch 子用 override SetSize 的控件（Testable 派生计数）→ 分配经虚函数到达 |

---

## 8. 影响面（预判）

| 区 | 文件 | 说明 |
|---|------|------|
| Widget | `Widget.h/.cpp` | +m_stretch/SetStretch/GetStretch（三行） |
| Layout | `VerticalLayout.h/.cpp`、`HorizontalLayout.h/.cpp` | 构造参数 + Arrange 分配逻辑（diff 同构） |
| Window | `Window.cpp` | OnResized + Arrange + Invalidate（两行） |
| 测试 | `LayoutTests.cpp` + 新增用例 | 基线断言核实 + 新增分配用例 |
| demo | `src/Demo/ModelProbe.cpp` | 布局改造（spacing/stretch/fillCrossAxis 接线 + 删透明 Spacer Widget）——实现阶段授权 |
| main.cpp | **单独授权**（skill 条 2） | 窗口 resize 行为本身无需改码；仅 RootWidget 设 Layout 一行（或经 demo 内页面自适配）——归实现阶段清单 |
| 文档 | `docs/phase9.7-*-preliminary-design.md`（本文）+ 详细设计（新） | — |

**不改动**：Animation/Theme/控件本体、CollapsiblePanel（其 SetSize override 语义经 F2 保留）、ProgressBar。

---

## 9. 待评审决策点汇总

> **2026-09-01 GPT 评审已全部定死**（详设 phase9.7-adaptive-layout-detailed-design.md v1.0 落地）：**D1=C**（跨轴构造开关默认 false——且 fill=true 时为强制填充语义）/ **D2=截断**（末位吃余数，废除"或四舍五入"措辞）/ **D3=仅必要布局改造**（ModelProbe 是验收工具非 UI 重构机会）/ **D4=main.cpp 显式设置**（Window 不替用户决定 RootWidget 布局）；另冻结：Arrange 纯布局不 Invalidate（归 Window 层）、"stretch=0 的子"措辞纪律（不称固定子）、嵌套布局链测试必须覆盖（NestedComposite）。

1. ~~**D1 跨轴行为**~~ → **C**
2. ~~**D2 取整方向**~~ → **截断**
3. ~~**ModelProbe 布局改造幅度**~~ → **仅必要改造**
4. ~~**RootWidget 设 Layout 的落点**~~ → **main.cpp**

---

## 10. 修订记录

- v1.1（2026-09-01）§3.1 算术勘误：GPT 原例"260/520"在 4 子 3 间隙下不自洽（1000−200−30=770≠780）→ 改 spacing=0 / 单固定 100 / stretch 1+2 → remaining=900 → 300/600（Σ==1000 自洽）；§9 决策点全部冻结为定稿（详设 phase9.7-adaptive-layout-detailed-design.md v1.0 落地）。
- v1.0（2026-09-01）初步设计初稿：冻结决策 F1-F7（吸收 GPT 评审五要点——分配模型/负剩余钳 0/末位吃余数/根容器无特例/spacing 构造参数）；核心算法 + 伪码；**新增 D1 跨轴行为决策点**（GPT 验收图隐含跨轴填充，与 F7 兼容承诺冲突——三方案对比，倾向 C 构造开关）；触发链 + 动画边界；LinearLayout 抽象不重启论证（维持 diff 同构）；测试方向 + 影响面 + ModelProbe 验收定位。
