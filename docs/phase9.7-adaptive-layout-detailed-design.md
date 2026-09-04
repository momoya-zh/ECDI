# Phase 9.7 自适应布局 详细设计（v1.2）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-01（v1.2 修订 2026-09-02）
> 状态：**已实现（2026-09-02 确认）**——v1.1b 评审通过后实现落地：SetStretch/spacing/fillCrossAxis/触发链全部实现，测试 141 全绿（9.7 用例并入 LayoutTests）；ModelProbe 消费验证（keyBox/list SetStretch(1)——拖窗输入框跟随变宽、列表吃剩余空间）；main.cpp 根容器 VerticalLayout(0,true)+page SetStretch(1)；LinearLayout 未提取（9.5 R2 重启评估结论）
> 前置：phase9.7-adaptive-layout-preliminary-design.md v1.0（评审通过）/ requirements v1.0（三决策 + F1-F7）

## 1. 目标与范围

给 Box Layout（VerticalLayout / HorizontalLayout）补齐窗口自适应能力：**stretch 权重分配**（Widget 属性）+ **spacing**（Layout 配置）+ **跨轴填充开关**（Layout 配置）+ **resize 触发链**（Window 层）。验收载体 = ModelProbe（仅必要布局改造，D3）。

本设计冻结 GPT 评审定死的全部语义，实现不得弱化。

## 2. 硬契约（冻结语义）

### 2.1 stretch 语义与尺寸来源二分（9.7 最核心语义）

| stretch 值 | 主轴尺寸来源 | 跨轴尺寸来源 |
|---|---|---|
| `= 0`（默认） | **保持当前尺寸**（不称"固定子"——stretch=0 ≠ Fixed policy，只是不参与分配） | fillCrossAxis=true → 父跨轴；false → 保持当前尺寸 |
| `> 0` | **完全由 Layout 分配**（`SetSize` 前的主轴尺寸值被覆盖，不再有意义） | 同上 |

- `stretch=0` 不是 Fixed policy——措辞纪律：全文用"**stretch=0 的子（主轴保持当前尺寸）**"，禁用"固定子/Fixed"称呼（GPT 评审 #5——防语义绕回）。
- **stretch ≥ 0 约束**：`SetStretch()` 要求 `stretch >= 0`（debug assert）；负值不具有合理权重语义（GPT 评审 #6）；`stretch <= 0` 均视为不参与分配，与 `=0` 行为一致。
- 一个子只有一个父 → `m_stretch` 单字段按所在布局的主轴解读（V = 高、H = 宽），无跨轴权重（需求冻结）。

### 2.2 分配算法（D2 = 截断定稿）

```text
available  = 父主轴尺寸                       （H = GetWidth()；V = GetHeight()）
fixedTotal = Σ stretch=0 子的当前主轴尺寸
remaining  = max(0, available − fixedTotal − spacing×(n−1))      ← 负剩余钳 0（F4，无 shrink）
对每个 stretch>0 的子（按children顺序第 k 个，共 m 个）：
    allocated_k = remaining × stretch_k / totalStretch        ← 整数除法截断（前 m−1 个）
    最后一个（k == m）：allocated = remaining − Σ allocated_(1..m−1)  ← 吃余数，Σ 恒等 remaining
```

- **统一措辞**：截断、末位吃余数（初设 F5 的"或四舍五入"措辞按 GPT 评审 #4 废除——本文为唯一真相）。
- `m == 0`（全 stretch=0）：remaining 不分配（尾部留空）——与现状一致。
- 无浮点、无 <cmath>。

### 2.3 spacing 语义（含 GPT 算术勘误）

- **间隙数 = n − 1**（Qt Box 惯例 + requirements F4 契约）：相邻子之间一个 spacing，首尾无。
- ⚠️ **勘误记录**：GPT 验收例"TextBox = 790"按 n×spacing（3×10=30）计算——与 (n−1) 契约矛盾；按契约复算 = **800**（1000 − 100 − 80 − 10×2）。本文及测试以 **800** 为准。
- 构造参数注入（F3），无 setter；spacing 只作用于主轴间隙，不影响跨轴。
- **spacing ≥ 0 约束**：`VerticalLayout`/`HorizontalLayout` 构造参数 `spacing` 要求 `>= 0`（debug assert）；负间距无合理语义（会减少主轴剩余空间并导致控件重叠——GPT 评审 #5）；超出范围属非法输入，框架开发者 API 不做运行时静默纠正。

### 2.4 跨轴填充（D1 = C 定稿）

```cpp
explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false);
explicit HorizontalLayout(int spacing = 0, bool fillCrossAxis = false);
```

`fillCrossAxis == true` 时的精确语义（GPT 评审 #2 冻结——**是强制 fill，不是"允许 fill"**）：

```text
VerticalLayout（主轴 Y / 跨轴 X）:
    child->SetSize(fillCrossAxis ? parentWidth  : w, h)
    child->SetPosition(0, y)                        ← 跨轴坐标 0（与现状一致，见下）

HorizontalLayout（主轴 X / 跨轴 Y）:
    child->SetSize(w, fillCrossAxis ? parentHeight : h)
    child->SetPosition(x, 0)                        ← 跨轴坐标 0（现状契约 4 本就如此）
```

- **跨轴坐标无需新增逻辑**：现行 V/H 布局跨轴坐标已恒 0（VerticalLayout.cpp:19 / HorizontalLayout.cpp:19 契约 4）——fill 只改**跨轴尺寸**。
- **粒度 = 每布局**（非每子）：v0.1 够用（子级跨轴差异需求未出现，挂账）。
- fill=true 时 stretch=0 的子跨轴也被强制改写——这是 opt-in 的明确代价，文档明示。

### 2.5 触发链与 Arrange 纯度（GPT 评审 #9）

```text
WM_SIZE → Window::OnResized(w, h):
    m_rootWidget->SetSize(w, h);   // 现有
    m_rootWidget->Arrange();       // 新增
    Invalidate();                  // 新增——Window 层负责
```

- **Arrange 只负责布局计算，绝不 Invalidate**——它是"按当前几何重算布局"的纯操作（测试/初始化/离屏计算复用）；"改变了 UI 要重绘"归调用方（Window 层）。
- **嵌套顺序保证**：`ArrangeInternal` 先 `m_layout->Arrange(*this)` 后递归子（Widget.cpp:170-183 既有顺序）——父布局先定子尺寸，子布局再在新鲜尺寸内分配，**天然形成布局链**，零改动。
- 启动路径不变：main.cpp 首次 `Arrange()` 保留（构造期 root SetSize 不走 OnResized——既有顺序约束）。
- 动画边界：CollapsiblePanel 冻结点 6 不变（resize 触发的 SetSize 同属"动画中外部改尺寸"边界，200ms 窗口内视觉退化可接受）。

### 2.6 向后兼容（F7 完整表述）

全部子 `stretch=0` + `spacing=0` + `fillCrossAxis=false`（三者全为默认值）→ **几何行为与现状一致**：只排主轴位置、不调任何子 `SetSize()`、主轴位置计算结果与现状相同；既有 LayoutTests（只断言 Position）零回归。"逐字节"仅在内部代码路径层面等价——接口契约以输出几何为准（GPT 评审 F7 措辞收敛）。

### 2.7 diff 同构维持（LinearLayout 抽象不重启）

分配逻辑在 V/H 两个 cpp 内镜像重复（~25 行）——6.1"diff 同构"契约延续；提取点已记录（§2.2 纯计算段），第三布局立项时一次到位。本次不建公共头/基类。

## 3. 接口定义（精确签名）

```cpp
// Widget.h（新增——属性三件套，同 SetShowFocusRect 先例）
public:
	/// @brief 剩余空间分配权重（9.7——0 = 不参与分配，保持当前尺寸；>0 = 主轴尺寸由父 Layout 分配）
	/// @pre stretch >= 0（debug assert；负值无合理权重语义——GPT 评审 #6）
	void SetStretch(int stretch) noexcept{ m_stretch = stretch; }

	/// @brief 分配权重只读查询
	[[nodiscard]] int GetStretch() const noexcept{ return m_stretch; }

private:
	int m_stretch = 0;   ///< 默认 0——不参与剩余空间分配（向后兼容锚点）

// VerticalLayout.h / HorizontalLayout.h（对称，diff 同构）
public:
	/// @brief @param spacing      主轴相邻子间隙 px（默认 0 = 现状；>= 0 debug assert——负间距无合理语义）
	///        @param fillCrossAxis 跨轴填充开关（默认 false = 现状；true = 所有子跨轴 = 父跨轴尺寸，
	///                             跨轴坐标恒 0——详见详设 §2.4；opt-in 有跨轴改写代价）
	explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false);

private:
	int m_spacing = 0;
	bool m_fillCrossAxis = false;
```

**不新增**：Layout 基类不动（`Arrange(Widget&)` 单方法）；无 setter；无 min/max/alignment/sizeHint（挂账）。

## 4. 实现细节

### 4.1 HorizontalLayout::Arrange（精确实现——Vertical 按 §2.4 镜像）

```cpp
void HorizontalLayout::Arrange(Widget& parent){

	// ① 一次遍历：stretch 总权重 + stretch=0 子当前主轴尺寸和（措辞纪律：非"固定子"——§2.1）
	const size_t count = parent.GetChildCount();
	if (count == 0) return;                                // ① 提前返回——count−1 下溢防护（GPT 评审 #3）

	int fixedTotal = 0;
	int totalStretch = 0;
	size_t stretchCount = 0;
	for (size_t i = 0; i < count; ++i){
		const Widget* child = parent.GetChildAt(i);
		if (child->GetStretch() > 0){
			totalStretch += child->GetStretch();
			++stretchCount;
		}
		else{
			fixedTotal += child->GetWidth();          // stretch=0：主轴保持当前尺寸
		}
	}

	// ② 剩余空间（F4：负值钳 0；spacing = n−1 个间隙——§2.3）
	const int remaining = (std::max)(0, parent.GetWidth() - fixedTotal
	                                       - m_spacing * static_cast<int>(count - 1));

	// ③ 分配 + 定位（F2：尺寸一律走 SetSize 虚分派；D2：截断 + 末位吃余数）
	int x = 0;
	int allocated = 0;
	size_t stretchSeen = 0;
	for (size_t i = 0; i < count; ++i){
		Widget* child = parent.GetChildAt(i);

		if (child->GetStretch() > 0){
			++stretchSeen;
			int width = (stretchSeen == stretchCount)
				? remaining - allocated                                  // 末位吃余数——Σ == remaining
				: remaining * child->GetStretch() / totalStretch;        // 整数除法截断
			allocated += width;
			child->SetSize(width, m_fillCrossAxis ? parent.GetHeight() : child->GetHeight());
		}
		else if (m_fillCrossAxis){
			child->SetSize(child->GetWidth(), parent.GetHeight());       // stretch=0 子：主轴不动，跨轴强制填充
		}

		child->SetPosition(x, 0);                                        // 跨轴坐标 0（契约 4，现状不变）
		x += child->GetWidth() + ((i + 1 < count) ? m_spacing : 0);      // 间隙只在子间（§2.3）
	}

}
```

VerticalLayout 镜像：`GetWidth→GetHeight`、`SetSize(w, h)→SetSize(h 轴对调)`、`SetPosition(0, y)`、`fixedTotal += GetHeight()`。

**实现要点**（含 GPT 评审 #2/#3）：
- x 累加使用 `child->GetWidth()`（SetSize 后的真实尺寸），遵守"Layout 提出尺寸、Widget 虚分派决定最终几何，下一定位以最终几何为准"——与 TextBox/CollapsiblePanel override 语义统一。
- `count == 0` 时 ① 已提前返回，`count − 1` 不可达——size_t 下溢已物理排除。
- `totalStretch == 0` 时 ③ stretch 分支不可达（stretchCount == 0）——无除零。
- x 累加用**定位后的实际宽度** `child->GetWidth()`（stretch 子经 SetSize 后已更新；stretch=0 子为原值）——避免二次计算漂移。
- `count == 0` 时循环体不执行（remaining 计算含 `count−1` 下溢防护：size_t 0−1 → 先判空提前返回，详细代码归实现自查项）。
- `totalStretch == 0` 时 ③ 的 stretch 分支不可达（stretchCount == 0）——无除零。

### 4.2 Window::OnResized（精确改动——两行）

```cpp
void Window::OnResized(int width, int height){
	if (m_rootWidget){
		m_rootWidget->SetSize(width, height);
		m_rootWidget->Arrange();   // 9.7：重排布局链（纯布局——Invalidate 归本层，§2.5）
		Invalidate();              // 9.7：重排后请求重绘
	}
}
```

### 4.3 Widget.h 改动（三行，§3）——无 .cpp 改动（内联属性）

## 5. 测试设计（Phase7.2，全无窗口——Layout 纯几何）

### 5.1 基线回归

既有 LayoutTests 全部原样通过（F7：默认构造三参数全默认 → 旧行为逐字节一致）。**既有用例零改动**。

### 5.2 新增用例

> **数据自洽勘误**（GPT 评审 ①）：初设/详设 GPT 原例"固定 100+100 / spacing 10×2 / stretch 1+2 → 260/520"在 4 子 3 间隙下不自洽（1000−200−30=770≠780）——本文统一换 spacing=0 使数字自洽；spacing 由用例 4 独立覆盖。

| # | 用例 | 断言 |
|---|------|------|
| 1 | `Layout.StretchBasic` | 父宽 1000 / spacing 0 / 子 A(stretch=0, w=100) / B(stretch=1) / C(stretch=2) → remaining = 900 → **B=300, C=600**（Σ==1000）；x = 0 / 100 / 400；B/C 主轴经 SetSize 虚分派（F2） |
| 2 | `Layout.StretchRemainder` | remaining 100 / stretch 1+1+1 → **33 / 33 / 34**（Σ == remaining——D2 截断 + 末位吃余数） |
| 3 | `Layout.StretchNegative` | 300 宽 / stretch=0 子合计 350 → **stretch 子主轴 = 0**；不产生负尺寸；排列顺序与 spacing 规则正确；超出父边界部分由 R1 Clip 处理（不做 shrink——F4 契约直接验证） |
| 4 | `Layout.SpacingPositions` | 三子 100/80/60 + spacing 12 → x = 0 / 112 / 204（间隙仅在子间，首尾无） |
| 5 | `Layout.CrossFill` | fillCrossAxis=true：stretch=0 子跨轴 = 父跨轴且主轴不动；false：完全不碰尺寸（§2.4 双分支） |
| 6 | `Layout.SetSizeDispatch` | stretch>0 子用 SetSize override 的 Testable 派生（计数器）→ Arrange 后计数 == 1（F2：分配经虚分派） |
| 7 | `Layout.NoStretchNoTouch` | 全 stretch=0 + spacing=0 + fill=false → Arrange 前后所有子 GetSize 逐字节相等（F7 直接验证） |
| 8 | **`Layout.NestedComposite`**（GPT 验收测试——一例覆盖全能力） | 见 §5.3 期望值表 |
| 9 | 幂等 | 上述每例 Arrange 两次，第二次结果与第一次逐字节一致（既有契约延续） |

### 5.3 NestedComposite 期望值表（嵌套布局链——9.7 核心价值验证）

```text
Root 1000×600，VerticalLayout(spacing=10, fillCrossAxis=true)
  ├─ Page    stretch=1，内部 HorizontalLayout(spacing=10, fillCrossAxis=true)
  │            ├─ Label    stretch=0，SetSize(100, h)
  │            ├─ TextBox stretch=1
  │            └─ Button   stretch=0，SetSize(80, h)
  └─ Footer  stretch=0，SetSize(w, 40)
```

| 阶段 | 断言 | 依据 |
|---|------|------|
| Root V 分配 | remaining = 600 − 40 − 10×1 = 550 → **Page 高 550**；Footer 高 40；两者宽 1000（跨轴填充）；Page y=0、Footer y=560 | §2.2/§2.4 |
| Page H 分配（ArrangeInternal 递归自动发生——§2.5 顺序保证） | remaining = 1000 − 180 − 10×2 = **800** → **TextBox 宽 800**；Label 100 / Button 80 主轴不动；三者高 550（跨轴填充）；x = 0 / 110 / 920 | §2.2/§2.3 勘误/§2.4 |
| Σ 恒等 | 100 + 800 + 80 + 10×2 = 1000；550 + 40 + 10 = 600 | D2 |

> ⚠️ GPT 评审例的 790 系按 n×spacing 误算——按 (n−1) 契约（§2.3 勘误）修正为 800。本表为测试唯一真相。

## 6. 文件影响清单（实现阶段原子授权预览）

| # | 文件 | 动作 |
|---|------|------|
| 1 | `ECDI/include/ECDI/Widget/Widget.h` | 修改（+m_stretch/SetStretch/GetStretch 三行，§3） |
| 2 | `ECDI/include/ECDI/Layout/VerticalLayout.h` | 修改（构造参数 ×2 + 成员） |
| 3 | `ECDI/include/ECDI/Layout/HorizontalLayout.h` | 修改（同上，diff 同构） |
| 4 | `ECDI/src/Layout/VerticalLayout.cpp` | 修改（Arrange 分配逻辑，§4.1 镜像） |
| 5 | `ECDI/src/Layout/HorizontalLayout.cpp` | 修改（Arrange 分配逻辑，§4.1） |
| 6 | `ECDI/src/Window/Window.cpp` | 修改（OnResized +2 行，§4.2） |
| 7 | `ECDI/src/Tests/LayoutTests.cpp` | 修改（+9 用例；既有用例零改动） |
| 8 | `ECDI/src/Demo/ModelProbe.cpp` | 修改（**仅必要布局改造**——D3：透明 Spacer→spacing / 手写 600 宽→stretch+fillCrossAxis / Models·Preview→stretch=1；**禁顺手美化**——换色/字体/padding/层次/业务逻辑一律不动） |
| 9 | `ECDI/main.cpp` | 修改（**单独授权**——D4：RootWidget `SetLayout(VLayout(...))` + 页面 `SetStretch(1)`，一行级） |
| 10 | `docs/phase9.7-adaptive-layout-detailed-design.md` | 本文档 |
| 11 | `ECDI/ECDI.vcxproj` | 无新文件——**零改动**（纯修改既有文件） |

**不改动**：Animation/Theme/ProgressBar/TextBox/Button 等控件本体、CollapsiblePanel（SetSize override 语义经 F2 保留）、RecordingBackend/Render 层。

## 7. 已知限制（v0.1 冻结）

1. **minSize / maxSize / shrink 不做**——stretch 子可缩到 0（Clip 裁掉），窗口过窄的压缩策略挂账。
2. **跨轴粒度 = 每布局**——子级跨轴差异（如 Qt 的 Fixed 方向 policy）挂账。
3. **alignment 不做**——跨轴恒 0 起点（fill）或保持（不 fill），无居中/尾部对齐。
4. **resize 重排性能**——每 WM_SIZE 全树 Arrange + 全区重绘，几十控件规模无感；脏区记账。
5. **动画中 resize**——CollapsiblePanel 冻结点 6 边界内，视觉退化可接受。
6. **sizeHint / 内容自适应**——挂账，独立立项 9.8 AutoSize（能力已具备，见 requirements"与 9.8 的关系"）。

## 8. 评审请求

请评审：① §2.3 spacing 勘误（790→800，(n−1) 契约）② §4.1 精确实现（x 累加用定位后实际宽度 / count==0 防护）③ §5.3 NestedComposite 期望值表（嵌套布局链逐值）④ §6 影响清单（ModelProbe 仅必要改造边界 + main.cpp 单独授权项）。

## 9. 修订记录

- v1.2（2026-09-02）**实现落地状态同步**：v1.1b 评审通过后实现完成——Widget::SetStretch(int)/GetStretch（默认 0 向后兼容锚点）+ V/H 构造参数 spacing（默认 0）+ 跨轴 fillCrossAxis 开关 + OnResized→Arrange 触发链；契约 10 修订生效（分配走 SetSize 虚分派）；测试 141 全绿（stretch/spacing 用例并入 LayoutTests）；ModelProbe 消费（keyBox SetStretch(1) H 拉伸 / list SetStretch(1) V 吃剩余空间）；main.cpp 根容器 VerticalLayout(0,true) + page SetStretch(1)（D4 落地）；**LinearLayout 未提取**（9.5 R2 重启评估结论：共享逻辑未达类层次——预判的纯函数级别未出现）。

- v1.1（2026-09-01）GPT 评审通过（整体通过）；**§5.2 test 1 数据修正**：原 GPT 原例"260/520"在 4 子 3 间隙下不自洽（1000−200−30=770≠780）——改 spacing=0 / 单固定 100 / stretch 1+2 → remaining=900 → **300/600**（Σ==1000，数字自洽）；勘误说明 §5.2 头 + §3.1 同步。
- v1.1b（2026-09-01）GPT 微修订三合入（不升 v1.2——GPT："不要因为边角问题又开v1.2"）：① **StretchNegative 断言修正**：删"位置合法不越界"（不成立——stretch=0 子可超出父，Clip 裁，F4 直接验证）→ 改"stretch 子主轴=0、不产生负尺寸、排列正确、超出由 Clip 处理"；② **spacing ≥ 0 / stretch ≥ 0 debug assert 约束**（§2.1/§2.3/§3 签名注释同步）；③ **§4.1 count==0 提前 return** 物理排除 size_t 下溢 + x 累加注释措辞优化（GPT 评审 #2：Layout 提出尺寸、Widget 决定最终几何、定位以最终几何为准——"避免漂移"废除）；④ **F7 措辞收敛**：删"逐字节"→"几何行为与现状一致；不调用 SetSize；主轴位置与现状相同"（GPT 评审 #7——接口契约以输出几何为准）。
- v1.0（2026-09-01）详细设计初稿：初设 v1.0 GPT 评审十项决策落成精确签名/实现（D1=C 跨轴构造开关强制 fill / D2=截断+末位吃余数 / D3=仅必要改造 / D4=main.cpp）；**spacing 勘误**（GPT 验收例 790 按 n×spacing 误算——按 (n−1) 契约修正 800，§2.3/§5.3）；措辞纪律（"stretch=0 的子"替代"固定子"）；**尺寸来源二分表**（§2.1——9.7 最核心语义）；Arrange 纯度（不 Invalidate，Window 层负责）；嵌套顺序保证论证（ArrangeInternal 既有顺序零改动）；NestedComposite 验收测试（一例覆盖 stretch/spacing/cross/嵌套/递归全能力）。
