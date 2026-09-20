# Phase 17 · 布局内边距（Layout padding）—— 详细设计

> 状态：**v1.1 待评审**（2026-09-20 · **v1.1 修订**：采纳第三轮评审——**T17-5 口径同步 / A2 与 T17-8 的概念边界 / A1 的「设计目标」标注 / 盯防项补第 7 条**，并新增 **§5.5 需求稿 R9 覆盖项的落地映射**）
> 输入：需求确认 v1.0（`docs/phase17-layout-padding-requirements.md`）· 初步设计 **v1.1**（`docs/phase17-layout-padding-preliminary-design.md`）
> 评审前置：外部初设评审结论「**初步设计：通过，可进入详细设计**」——其要求的 3 项前置修正（**T17-5 判据** / **§3.4 overflow 不变式** / **O1 冻结**）**已在初设 v1.1 全部落实** ⇒ 本稿无阻塞项

---

## 1. 实施总览

### 1.1 改动清单

| # | 文件 | 改动性质 |
|---|---|---|
| 1 | `ECDI/include/ECDI/Layout/VerticalLayout.h` | 签名 +1 形参 · 成员 +1 · 注释 **4** 处（其中 **2 处是失实描述修正**——见 §1.3-4） |
| 2 | `ECDI/src/Layout/VerticalLayout.cpp` | 构造 2 处 · `Arrange` **6 行**（新增 1 · 修改 5）＋注释 3 行 |
| 3 | `ECDI/include/ECDI/Layout/HorizontalLayout.h` | 同 1（对称） |
| 4 | `ECDI/src/Layout/HorizontalLayout.cpp` | 同 2（对称） |
| 5 | `ECDI/src/Tests/LayoutTests.cpp` | **新增 8 个用例**（+85 断言）+ 8 行注册；**既有 11 个用例一字不动** |

**无新增文件**——8 个用例加入既有 `LayoutTests.cpp` ⇒ `RunAllTests.h` / `RunAllTests.cpp` / `CMakeLists.txt` **零改动**（不新增 TU）。

### 1.2 规模锚点

| 量 | 前 | 后 | 说明 |
|---|---|---|---|
| 公共头 | 92 | **92** | 只改既有头的签名与注释，**不新增头** |
| 用例 | 218 | **226**（**设计目标**） | +8（T17-1..T17-8）；**实施后以实测回填**，不强行追设计值（第三轮评审） |
| 断言特征串 | 10 | **11** | 新增条件串 `padding >= 0`（V/H 各一处）——A2 判据随之更新 |
| `LayoutTests.cpp` 断言 | 69 | **154**（Release）/ **146**（Debug） | 设计值 +85 / +77（Debug 下 T17-8 整块不编译）；**落盘后按 skill 条 29③c 实测核对** |
| `LayoutTests.cpp` 行数 | 505 | **≈ 800**（落盘后实测回填） | 新增 8 个函数 + 8 行注册 |
| 改动源文件 | — | **4** | 均为既有文件 |
| `examples/ModelProbe/main.cpp` | — | **本稿不含** | 验收用的 ModelProbe padding 属 **A6——须单独授权**（skill 条 2） |

### 1.3 本稿对初设的七处细化（逐条给理由）

| # | 项 | 初设 | 本稿 | 理由 |
|---|---|---|---|---|
| 1 | Release 钳 0 的**写法** | 只写「Release 侧钳 0」（未定形式） | **mem-init 三元**：`m_padding(padding < 0 ? 0 : padding)`；**断言仍针对形参 `padding`** | 不引入 helper / 不改私有成员布局 / 不触碰 `spacing`（见第 2 条）。断言判形参 ⇒ 钳制**不掩盖**非法输入（Debug 仍拦） |
| 2 | `spacing` 是否一并钳 0 | 未提 | **不钳**（**有意的不对称**） | `spacing` 的 Release 行为是 9.7 的既成事实（`VerticalLayout.cpp:13` 只有断言、无钳制）；改它属**范围外行为变更**。⇒ 记入 §8 **L3** |
| 3 | `cross` 的**计算位置** | 「跨轴尺寸 − 2p」 | **循环外算一次**，作为新增编号 **③**（原 ③ 顺延为 **④**） | 与 `remaining` 同为循环不变量；若放循环内会重复求值，且与「最小 diff」相悖 |
| 4 | ★ **头文件失实描述（初设漏项）** | §2.1 只写「构造注释同步」 | **多出 2 处必须改的失实描述**：`@param fillCrossAxis` 的「所有子跨轴 = 父跨轴，**跨轴坐标恒 0**」在 `padding > 0` 时**失实**；`.cpp:59` 的注释「跨轴坐标 0（契约 4，现状不变）」同样失实 | skill 条 80：**本阶段修正的语义，会让上一阶段写下的描述变成假的**。头文件 `@param` 是跨阶段的承诺 ⇒ 不改 = 公共 API 说谎，且**不触发任何编译错误** |
| 5 | ★ **调用点构成（初设不准）** | 「生产示例 **20**」 | **20 = ModelProbe 9（`ModelProbe.cpp` 8 + `main.cpp` 1）+ `examples/VisualTest` 1 + `src/Demo` 10**；其中 **`src/Demo` 的 10 处不在任何 CMake 目标内** | 本稿 §7 实测枚举：`CMakeLists.txt:42` 把 `/Demo/` 排除出库、`:148`/`:151` 只 `add_subdirectory` ModelProbe 与 VisualTest ⇒ **`VisualTest` 是初设未列出的第 3 个消费者**，且它是 CMake 目标（`visualtest`） |
| 6 | 测试用例数 | 「218 + N，N 归详设」 | **N = 8**，并给出逐用例**精确数值 + 断言清单** | 详设职责；数值已用「目标形态模拟脚本」复算（§5.3） |
| 7 | **跨轴钳 0 的覆盖** | §3.3 定案② 有规则、**无用例** | 并入 **T17-5 第二个块** | 贯彻初设 §4 的表形制：**「有契约必有验证手段」** |

> **与初设口径的对账**：初设 §3.2 写「`Arrange` 改 **4 处**」＝ 主轴起点 / `remaining` / 跨轴尺寸 / 跨轴坐标。本稿 §2.2 把它细化为 **8 个行级改动点**（△1–△8，含构造 2 处与注释 3 处）；**代码行仍只有 6 行**（新增 1 · 修改 5）。两处口径不矛盾，前者按「语义点」计、后者按「行」计。

---

## 2. 逐文件最小 diff 规格

> **阅读约定**：以下代码块中 **标 `△` 的行是本相位要动的行**；**未标 `△` 的行逐字不动**（skill 条 42：规格必须让实施者能直接判断「哪些行要动、哪些绝对不动」）。代码块按仓库源码的 **Tab** 缩进书写。

### 2.1 `ECDI/include/ECDI/Layout/VerticalLayout.h`

```cpp
/// @brief 垂直布局（9.7：stretch + spacing + fillCrossAxis；17：padding——diff 同构约束仅 y→x / height→width）   // △
/// @details 职责：根据子控件 stretch 权重分配主轴（Y）尺寸 + 跨轴（X）可选填充 + spacing 间隙 + 四边内边距 padding。   // △
/// 幂等：每次 Arrange 从头计算，不依赖子控件当前 Position（6.1 契约 1）。
class VerticalLayout : public Layout{

public:

	/// @param spacing      主轴相邻子间隙 px（默认 0 = 现状；>= 0 debug assert——负间距无合理语义）
	/// @param fillCrossAxis 跨轴填充开关（默认 false = 现状；true = 所有子跨轴 = 父跨轴 − 2×padding，跨轴坐标 = padding）   // △
	/// @param padding      四边内边距 px（默认 0 = 现状；>= 0 debug assert / Release 钳 0）；硬 inset——空间不足时内容区退化为 0，坐标仍取 padding，不反向缩减   // 新增
	explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false, int padding = 0);   // △

	void Arrange(Widget& parent) override;

private:

	int m_spacing = 0;
	bool m_fillCrossAxis = false;
	int m_padding = 0;   // 新增

};
```

| △ | 行（现状 `VerticalLayout.h`） | 改法 |
|---|---|---|
| △1 | `:8` `@brief` | 括号内追加 `；17：padding` |
| △2 | `:9` `@details` | 末尾追加 ` + 四边内边距 padding` |
| △3 | `:16` `@param fillCrossAxis` | 「所有子跨轴 = 父跨轴，跨轴坐标恒 0」→「所有子跨轴 = 父跨轴 − 2×padding，跨轴坐标 = padding」（**失实修正**） |
| △4 | `:17` 构造签名 | 追加第 3 形参 `int padding = 0` |
| △5 | `:17` 前 | **新增 1 行** `@param padding` 注释 |
| △6 | `:24` 后 | **新增 1 行** `int m_padding = 0;` |

`HorizontalLayout.h`（`:7`/`:8`/`:15`/`:16`/`:23`）为 **x↔y / width↔height 的逐字镜像**（`主轴（X）` / `跨轴（Y）`），改动点一一对应。

### 2.2 `ECDI/src/Layout/VerticalLayout.cpp`

```cpp
VerticalLayout::VerticalLayout(int spacing, bool fillCrossAxis, int padding)                    // △1
	: m_spacing(spacing), m_fillCrossAxis(fillCrossAxis), m_padding(padding < 0 ? 0 : padding)   // △1
{
	FRAMEWORK_ASSERT(spacing >= 0);
	FRAMEWORK_ASSERT(padding >= 0);   // △2——与 spacing 同型；Release 由上一行三元钳 0（§1.3-1）
}

void VerticalLayout::Arrange(Widget& parent){

	// ① 一次遍历：stretch 总权重 + stretch=0 子当前主轴尺寸和
	const size_t count = parent.GetChildCount();
	if (count == 0) return;

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
			fixedTotal += child->GetHeight();   // stretch=0 的子（主轴保持当前尺寸）
		}
	}

	// ② 剩余空间（F4：负值钳 0；spacing = n−1 个间隙——详设 §2.3；padding = 四边内边距——17 详设 §3.2）   // △3 注释
	const int remaining = (std::max)(0, parent.GetHeight() - 2 * m_padding - fixedTotal   // △3
	                                        - m_spacing * static_cast<int>(count - 1));

	// ③ 跨轴可用尺寸（17：padding 是硬 inset ⇒ 负值钳 0）   // 新增 2 行
	const int cross = (std::max)(0, parent.GetWidth() - 2 * m_padding);   // 新增

	// ④ 分配 + 定位（F2：尺寸一律走 SetSize 虚分派；D2：截断 + 末位吃余数）   // △5 注释（原 ③）
	int y = m_padding;   // △6
	int allocated = 0;
	size_t stretchSeen = 0;
	for (size_t i = 0; i < count; ++i){
		Widget* child = parent.GetChildAt(i);

		if (child->GetStretch() > 0){
			++stretchSeen;
			int height = (stretchSeen == stretchCount)
				? remaining - allocated                                  // 末位吃余数——Σ == remaining
				: remaining * child->GetStretch() / totalStretch;        // 整数除法截断
			allocated += height;
			child->SetSize(m_fillCrossAxis ? cross : child->GetWidth(), height);   // △7a
		}
		else if (m_fillCrossAxis){
			child->SetSize(cross, child->GetHeight());   // △7b——stretch=0 子：主轴不动，跨轴强制填充
		}

		child->SetPosition(m_padding, y);   // △8——跨轴坐标 = padding（契约 4 的推广，17 详设 §3.2）
		y += child->GetHeight() + ((i + 1 < count) ? m_spacing : 0);
	}

}
```

| △ | 现状行号 | 现状 | 改为 |
|---|---|---|---|
| △1 | `:10` / `:11` | `VerticalLayout(int spacing, bool fillCrossAxis)` / `: m_spacing(spacing), m_fillCrossAxis(fillCrossAxis)` | 形参追加 `, int padding` / 初始化列表追加 `, m_padding(padding < 0 ? 0 : padding)` |
| △2 | `:13` 之后 | （无） | **新增 1 行** `FRAMEWORK_ASSERT(padding >= 0);` |
| △3 | `:36` 注释 / `:37` | `// ② 剩余空间（F4：负值钳 0；spacing = n−1 个间隙——详设 §2.3）` / `parent.GetHeight() - fixedTotal` | 注释补 `；padding = 四边内边距——17 详设 §3.2` / 减项前插 `2 * m_padding` |
| △4 | `:39` 之后 | （无） | **新增 2 行**：`// ③ 跨轴可用尺寸…` + `const int cross = (std::max)(0, parent.GetWidth() - 2 * m_padding);` |
| △5 | `:40` | `// ③ 分配 + 定位（…）` | `// ④ 分配 + 定位（…）` |
| △6 | `:41` | `int y = 0;` | `int y = m_padding;` |
| △7 | `:53` / `:56` | `child->SetSize(m_fillCrossAxis ? parent.GetWidth() : child->GetWidth(), height);` / `child->SetSize(parent.GetWidth(), child->GetHeight());` | 两处的 `parent.GetWidth()` → `cross`（语义不变，取值来源改为已钳的 `cross`） |
| △8 | `:59` | `child->SetPosition(0, y);` ＋ 注释「跨轴坐标 0（契约 4，现状不变）」 | `child->SetPosition(m_padding, y);` ＋ 注释「跨轴坐标 = padding（契约 4 的推广，17 详设 §3.2）」 |

### 2.3 `ECDI/src/Layout/HorizontalLayout.cpp`（对称）

| △ | 现状行号 | 现状 | 改为 |
|---|---|---|---|
| △1 | `:10` / `:11` | 同 V | 同 V（形参名一致） |
| △2 | `:13` 之后 | （无） | **新增 1 行** `FRAMEWORK_ASSERT(padding >= 0);` |
| △3 | `:36` / `:37` | `parent.GetWidth() - fixedTotal` | 减项前插 `2 * m_padding` ＋注释同步 |
| △4 | `:39` 之后 | （无） | **新增 2 行**：`const int cross = (std::max)(0, parent.GetHeight() - 2 * m_padding);`（**跨轴是高度**） |
| △5 | `:40` | `// ③ 分配 + 定位` | `// ④ 分配 + 定位` |
| △6 | `:41` | `int x = 0;` | `int x = m_padding;` |
| △7 | `:53` / `:56` | `child->SetSize(width, m_fillCrossAxis ? parent.GetHeight() : child->GetHeight());` / `child->SetSize(child->GetWidth(), parent.GetHeight());` | 两处的 `parent.GetHeight()` → `cross` |
| △8 | `:59` | `child->SetPosition(x, 0);` | `child->SetPosition(x, m_padding);`（**跨轴坐标是第二个实参**） |

> **同构约束检查**：改完后两份 `.cpp` 仍应逐行对照（除 `x↔y` / `width↔height` / `GetWidth↔GetHeight` 的互换）——这是 6.1 / 9.7 的既有纪律，本相位**不破例**。

### 2.4 `ECDI/src/Tests/LayoutTests.cpp`

**落点**：8 个新函数插在 **`TestIdempotentStretch()` 之后、`} // anonymous namespace` 之前**；8 行注册追加在 **`:503`（`Layout.IdempotentStretch`）之后**。

```cpp
void ECDI::Test::RegisterLayoutTests()
{
	... 既有 11 行不动 ...
	GetTestRegistry().Add("Layout.IdempotentStretch", &TestIdempotentStretch);
	GetTestRegistry().Add("Layout.PaddingDefaultZero", &TestPaddingDefaultZero);        // 新增
	GetTestRegistry().Add("Layout.PaddingSingleChild", &TestPaddingSingleChild);        // 新增
	GetTestRegistry().Add("Layout.PaddingCrossAxisWidth", &TestPaddingCrossAxisWidth);  // 新增
	GetTestRegistry().Add("Layout.PaddingWithStretch", &TestPaddingWithStretch);        // 新增
	GetTestRegistry().Add("Layout.PaddingOverflow", &TestPaddingOverflow);              // 新增
	GetTestRegistry().Add("Layout.PaddingNested", &TestPaddingNested);                  // 新增
	GetTestRegistry().Add("Layout.PaddingIdempotent", &TestPaddingIdempotent);          // 新增
	GetTestRegistry().Add("Layout.PaddingNegativeClamped", &TestPaddingNegativeClamped);// 新增
}
```

**风格**：该文件是 **4 空格缩进**（与框架源码的 Tab 不同——既有文件如此，**沿用不改**）；`Panel` + `Widget` 子节点 + `{ }` 分块的组织方式与既有 11 个用例一致。

### 2.5 零改动区（显式声明 + 逐项理由）

| 文件 / 区域 | 改动 | 理由 |
|---|---|---|
| `ECDI/include/ECDI/Layout/Layout.h` | **0** | D2——基类保持**无状态纯接口**（全文 17 行） |
| `ECDI/include/ECDI/Widget/Widget.h` · `src/Widget/Widget.cpp` | **0** | padding 语义全部落在布局实现；`SetSize` / `SetPosition` / `GetAbsolutePosition` 一行不动 |
| `ECDI/include/ECDI/Window/Window.h` · `src/Window/Window.cpp` | **0** | root 尺寸仍 = 客户区全尺寸（`Window.cpp:444`）；留白由 root 的**布局**产生，窗口层无需知道 padding 存在（D5） |
| `Panel` / `CollapsiblePanel` / `ScrollView` / `ScrollBar` / `CaptionBar` | **0** | 它们只是「挂了布局的 Widget」或「被布局排布的 Widget」——不需要知道 padding 存在 |
| 其余 **41 处**既有调用点（§7） | **0** | 第 3 形参**可选**；现有调用最多传 2 个实参，重载决议不受影响 |
| `RunAllTests.h` / `RunAllTests.cpp` / `CMakeLists.txt` | **0** | 不新增 TU（8 个用例进既有文件，`GLOB_RECURSE CONFIGURE_DEPENDS` 天然覆盖） |

---

## 3. 关键行为冻结

### 3.1 `Arrange` 目标形态（V / H 逐项对照）

| 量 | `VerticalLayout` | `HorizontalLayout` |
|---|---|---|
| 主轴 | Y（`GetHeight`） | X（`GetWidth`） |
| 跨轴 | X（`GetWidth`） | Y（`GetHeight`） |
| 跨轴可用尺寸 | `cross = max(0, parent.GetWidth() − 2p)` | `cross = max(0, parent.GetHeight() − 2p)` |
| 主轴剩余空间 | `remaining = max(0, parent.GetHeight() − 2p − fixedTotal − spacing·(n−1))` | `max(0, parent.GetWidth() − 2p − fixedTotal − spacing·(n−1))` |
| 主轴游标初值 | `int y = m_padding;` | `int x = m_padding;` |
| 跨轴尺寸（stretch 子） | `SetSize(m_fillCrossAxis ? cross : child->GetWidth(), height)` | `SetSize(width, m_fillCrossAxis ? cross : child->GetHeight())` |
| 跨轴尺寸（stretch=0 子，仅 fill） | `SetSize(cross, child->GetHeight())` | `SetSize(child->GetWidth(), cross)` |
| 定位 | `SetPosition(m_padding, y)` | `SetPosition(x, m_padding)` |

### 3.2 取值口径（三条定案＋落点）

| # | 定案 | 落点 | 依据 |
|---|---|---|---|
| ① | **`remaining` 单层钳**（`2p` 并入被减项，不拆 `available`／`remaining` 两级） | △3 | 与两层写法**等价**（`fixedTotal ≥ 0`、`spacing ≥ 0` 恒成立）；**单层是最小 diff**（初设 §3.3-①） |
| ② | **`cross` 钳 0** | △4 | `Widget::SetSize` 虽无钳制（`Widget.cpp:210-215`），但负尺寸会让 `HitTest`／绘制进入无意义区间 |
| ③ | **主轴起点与跨轴坐标不钳**（恒 `= padding`，即使 `padding > 主轴/跨轴尺寸`） | △6 / △8 | **硬 inset 语义**——空间不足时内容区退化为 `0`，坐标仍落在 padding 处；**不反向修改 padding**、不中心化、不自动缩减 |

### 3.3 溢出区间与不变式（**精确表述** —— 初设 v1.1 §3.4 的落地）

**两个必须区分的量**：

| 量 | 含义 | 布局是否改写它 |
|---|---|---|
| `fixedTotal` | 所有 `stretch == 0` 子的**主轴尺寸之和** | **否**——对它们只 `SetPosition`（跨轴 `SetSize` 仅在 `fillCrossAxis` 时） |
| `remaining` | 分给 `stretch > 0` 子的**可用主轴空间** | **是**——stretch 子主轴尺寸由此分配（末位吃余数） |

**无溢出区间**（`2p + fixedTotal + spacing·(n−1) ≤ parent主轴`）：

- `Σ stretch子主轴尺寸 == remaining`（末位吃余数保证）
- `fixedTotal + Σ stretch子主轴尺寸 + spacing·(n−1) + 2p == parent主轴`

**溢出区间**（上式不成立）：

- `remaining == 0` ⇒ **所有 stretch 子主轴尺寸 == 0**
- **fixed 子主轴尺寸保持其既有值**（布局不改写它们）——**这正是不能写「Σ 主轴尺寸 == 0」的原因**
- 首子主轴起点仍为 `padding`；**不崩溃**

> **前提澄清（避免归因误导）**：Phase 9.7 的 `remaining = max(0, …)` **本来就**意味着「fixed 子自身已超界时，各项之和允许大于 parent」——这条等式**从始至终只在无溢出区间成立**，**不是** Phase 17 才让它失效。

### 3.4 与既有机制的交互（逐项「不动」）

| 机制 | 是否改动 | 说明 |
|---|---|---|
| `GetContentOffsetX/Y`（Phase 15 滚动） | **不动** | 与 padding 是**两套独立机制**：`GetAbsolutePosition` 逐层累加 `parent->GetX() − parent->GetContentOffsetX()`（`Widget.cpp:377-379`）⇒ 滚动容器内层布局若配 padding，视觉结果 = `滚动位移 + padding`，**天然正交**，一行不改 |
| `HitTest` / `ContainsRect`（Phase 15） | **不动** | 父的命中区域仍是**自身矩形**——padding 是**布局留白，不是命中屏障**。⇒ 记入 §8 **L2** |
| `OnResized → root.Arrange()`（9.7 触发链） | **不动** | resize 后 padding 恒定、内容区随客户区缩放（`Window.cpp:444` 的 root 全尺寸语义不变） |
| `AutoSize()`（9.8） | **不动**，但有一处交互须记明 | 见 §8 **L1**——**本项的真实动机场景不需要改 AutoSize** |
| `CaptionBar`（13） | **不动、不联动** | 需求 O4 / Phase 13 D7 的同型先例 |
| `ScrollView::SetScrollStep` 等断言 | **不动** | 断言特征串只 **+1**（`padding >= 0`），既有 10 条全部保留 |

---

## 4. 契约表（C1–C6 → 实现落点 → 测试）

| # | 契约 | 实现落点 | 验证 |
|---|---|---|---|
| **C1** | **默认 0 = 逐位退化**：`padding = 0` 时 `Arrange` 行为与改动前**逐位相同** | △1 默认值 + △3/△4/△6/△7/△8 在 `p = 0` 时逐位退化（`− 0` / `+ 0` / `cross == parent`） | **T17-1**（守门）＋既有 **11 条 `Layout.*` 用例一字不改仍全绿** |
| **C2** | **padding 是硬 inset**：空间不足时内容区退化为 0，绝不反向修改 padding，不引入中心化 / 自动缩减 | △4（`cross` 钳 0）＋ △6/△8（坐标不钳） | **T17-5**（两块：溢出混排 + 跨轴钳 0） |
| **C3** | **嵌套 = 累加**（每级各自生效），不做继承 / 覆盖 / 级联 | 每级布局各自扣减 ⇒ 自然累加 | **T17-6** |
| **C4** | **与 `spacing` / `stretch` / `fillCrossAxis` 正交**：padding 先扣减，其余分配逻辑逐字不变 | △3 扣减顺序 ＋ △7 只换取值来源 | **T17-3 / T17-4** ＋ 既有用例 |
| **C5** | **幂等**：每次 `Arrange` 从 parent 几何 + 子当前尺寸 + spacing + padding 重算，不读上一次结果 | `ArrangeInternal` 每次递归重算（`Widget.cpp:188-202`）——**结构保证**；padding 是**常量输入** | **T17-7** |
| **C6** | **负值钳 0 + Debug 断言** | △1（`padding < 0 ? 0 : padding`）＋ △2（`FRAMEWORK_ASSERT(padding >= 0)`） | **Release 侧 = T17-8**（钳 0）；**Debug 侧 = A2 结构性验收**（断言特征串 11/11）。⚠️ **A2 不是 T17-8 的一部分**——两者是 C6 的**并列**验证手段，别混读（完整分工见 §5.5） |

**表形制**：沿用 Phase 16 契约表形制——**每条契约必须绑定验证手段**，不允许「有契约、无验证」。

---

## 5. 测试规格（T17-1..T17-8）

### 5.1 硬约束

1. **既有 11 条 `Layout.*` 用例一个字都不改**（初设 §7 / 评审第 8 条）——它们的全绿本身就是 **C1** 的证据（A3 判据）。
2. **期望值必须与目标实现同源**：本稿全部数值由「目标形态模拟脚本」按 §3.1 的语义复算（含整数除法截断 + 末位吃余数），**逐条一致**（§5.3）——skill 条 47。
3. **`Panel` 只作容器**（它有 layout 能力）；需要可命中的叶子时用裸 `Widget`（skill 条 67）——本相位全部断言都是**几何查询**，不涉命中。

### 5.2 逐用例规格

#### T17-1 `Layout.PaddingDefaultZero`（**守门用例** · 14 断言）

目的：证明 `padding = 0` 时**逐位退化**。做法 = 用 `padding = 0` **显式传参**重跑三个既有场景，断言与既有用例**完全相同的期望值**。

| 块 | 场景 | 断言 |
|---|---|---|
| A（对齐 `Layout.SpacingPositions`） | `HorizontalLayout(12, false, 0)` · 父 500×50 · 三子 fixed 100/80/60 | `x == 0 / 112 / 204`（3） |
| B（对齐 `Layout.CrossFill`） | `VerticalLayout(0, true, 0)` · 父 200×100 · 单子 150×30 | 宽 `200` · 高 `30` · `x == 0` · `y == 0`（4） |
| C（对齐 `Layout.StretchBasic`） | `HorizontalLayout(0, false, 0)` · 父 1000×50 · A(100 fixed) / B(s1) / C(s2) | 宽 `100 / 300 / 600`（3）· `x == 0 / 100 / 400`（3）· `Σ == 1000`（1） |

#### T17-2 `Layout.PaddingSingleChild`（8 断言）

| 块 | 场景 | 断言 |
|---|---|---|
| A | `VerticalLayout(0, **false**, 20)` · 父 200×100 · 单子 50×30 | `(x, y) == (20, 20)`（2）· 尺寸仍 `50×30`（2）——证明 `fill=false` 时**不碰跨轴** |
| B | `HorizontalLayout(0, **false**, 20)` · 父 200×100 · 单子 50×30 | 同上（对称面） |

#### T17-3 `Layout.PaddingCrossAxisWidth`（14 断言）

`fillCrossAxis = true` + padding ⇒ 每子跨轴 = `父跨轴 − 2p`（**C4 正交性**：padding 与 fill 共存）。

| 块 | 场景 | 断言 |
|---|---|---|
| A | `VerticalLayout(0, true, 20)` · 父 200×100 · A(50×30 fixed) / B(s1) | A 宽 `160` · B 宽 `160` · A `(20,20)` · B `(20,50)` · B 高 `30` · 等式 `30+30+0+40 == 100`（8） |
| B | `HorizontalLayout(0, true, 20)` · 父 200×100 · A(30×50 fixed) / B(s1) | A 高 `60` · B 高 `60` · A `(20,20)` · B `(50,20)` · B 宽 `130` · 等式 `30+130+0+40 == 200`（6） |

#### T17-4 `Layout.PaddingWithStretch`（**评审示例·黄金数据** · 7 断言）

`HorizontalLayout(10, false, 20)` · 父 **500**×50 · 3 个 `stretch=1`

- `available = 500 − 40 = 460` → `remaining = 460 − 10×2 = 440` → `440/3 = 146 … 2` ⇒ 末位吃余数得 **146 / 146 / 148**
- 断言：宽 `146 / 146 / 148`（3）· `x == 20 / 176 / 332`（3）· 等式 `146+146+148+10×2+20×2 == 500`（1）

#### T17-5 `Layout.PaddingOverflow`（10 断言）

| 块 | 场景 | 断言 |
|---|---|---|
| A（**溢出混排**——把 `fixedTotal` 与 `remaining` 是两个量测死） | `HorizontalLayout(5, false, 20)` · 父 **40**×50 · A `stretch=0` **固定 10×30** / B `stretch=1` | `remaining = max(0, 40 − 40 − 10 − 5) = 0` ⇒ **A 宽 == 10（保持自身尺寸，而非 0）** · **B 宽 == 0** · A `(20,20)` · B `(35,20)`（6） |
| B（**跨轴钳 0 + 坐标不钳**——硬 inset） | `VerticalLayout(0, true, 30)` · 父 **50**×100 · 单子 10×10 fixed | `cross = max(0, 50 − 60) = 0` ⇒ **子宽 == 0（钳 0，非负）** · 子高 == 10（主轴保持）· 子 `(30, 30)`（**x = padding = 30 > 内容区宽 0**——坐标不钳）（4） |

#### T17-6 `Layout.PaddingNested`（**累加，非 max / 非继承** · 9 断言）

```
root  Panel 400×300   VerticalLayout(0, true, 10)
 └─ page  Panel stretch=1   VerticalLayout(0, true, 20)
     └─ child Widget 50×30 (stretch=0)
```

- 断言：page `(x, y) == (10, 10)`（2）· page 尺寸 `380 × 280`（2）· child **相对 page** `(20, 20)`（2）· child **绝对** `(30, 30)`（2——`GetAbsolutePosition()`，证明 **10 + 20 = 30** 而非 `max` / `40` / `20`）· child 宽 `340`（`= 380 − 40`，第二级 padding 生效）（1）

#### T17-7 `Layout.PaddingIdempotent`（15 断言）

`VerticalLayout(6, true, 12)` · 父 300×200 · A fixed(100×40) / B(s1) / C(s2)

- 首轮断言：A `y == 12` · A 宽 `276` · A 高 `40` · B `y == 58` · B 高 `41` · C `y == 105` · C 高 `83`（7）· 等式 `40+41+83+6×2+12×2 == 200`（1）
- 记录 6 个量 → **再 `Arrange()` 一次** → 逐项复比（7）⇒ **C5**（幂等）在「padding + spacing + stretch + fill」全开下成立

#### T17-8 `Layout.PaddingNegativeClamped`（**Release 8 断言 / Debug 0**）

```cpp
void TestPaddingNegativeClamped()
{
#ifdef NDEBUG
	// Release：无断言层 ⇒ 负值必须被钳 0（等价于 padding=0 的几何）
	// （Debug 下负值会在构造期 FRAMEWORK_ASSERT 终止 ⇒ 该分支不参与 Debug 编译；
	//   Debug 侧由 A2 的断言特征串核验覆盖——skill 条 50）
	... 8 条：V(0,false,-10) 与 H(0,false,-10) 各 4 条（位置 (0,0) + 尺寸 50×30 不变）
#endif
}
```

> **为什么必须用 `#ifdef NDEBUG`**：Debug 下 `VerticalLayout(0, false, -10)` 会触发 `FRAMEWORK_ASSERT` ⇒ `HandleAssertFailure` 走 `Logger::Log(Fatal)` + `MessageBoxW` + `assert(false)` ⇒ **测试进程被终止**。⇒ **Debug 分支必须为空**，这是契约设计使然，不是测试偷懒。
>
> **两个分支的覆盖分工**（写进用例注释）：**Debug** = 断言拦截（由 **A2** 的二进制特征串证明断言真的编译进去了）；**Release** = 钳 0（由本用例证明）。
>
> ⚠️ **概念边界**（第三轮评审提出）：**A2 是结构性验收（断言确实存在），不属于 T17-8**。本用例的职责**只有 Release 钳 0**；Debug 侧**没有**（也**不该有**）运行时用例——Debug 下构造负 `padding` 会终止测试进程。完整分工见 **§5.5**。

### 5.3 期望值复算记录（skill 条 47）

本稿全部期望值来自一份**目标形态模拟脚本**（按 §3.1 的语义实现 `Arrange`：单层钳 / `cross` 钳 0 / 坐标不钳 / 整数除法截断 / 末位吃余数），逐条 `assert` 与手算值比对 ⇒ **全部一致**：

```
T17-1 A/C · T17-2 A/B · T17-3 A/B · T17-4 · T17-5 A/B · T17-6 · T17-7 · T17-8
→ 期望值复算一致（含 T17-7 的二次 Arrange 幂等比对）
```

脚本落点 `.workbuddy/tmp/p17_dd_verify.py`（**临时物，交付后清理**）。

### 5.4 断言数汇总（设计值）

| 用例 | 断言 |
|---|---|
| T17-1 | 14 |
| T17-2 | 8 |
| T17-3 | 14 |
| T17-4 | 7 |
| T17-5 | 10 |
| T17-6 | 9 |
| T17-7 | 15 |
| T17-8 | 8（**仅 Release 编译**） |
| **合计** | **+85**（Release）/ **+77**（Debug） |

> ⚠️ **落盘后必须实测**（skill 条 29③c：计数类断言不得推算）——实施时以脚本对 `LayoutTests.cpp` 的 `EXPECT_` 计数，与本表核对；不一致以**实测**为准并回填本表。

---

### 5.5 需求稿 R9 覆盖项的落地映射（**含两处测试口径演进**）

需求稿 §3.4 的 **R9** 列了 7 个必须覆盖的点。逐条映射如下——**不允许出现「列了但没测」的项，也不允许假装测到了**：

| 需求 R9 覆盖项 | 落地 | 说明 |
|---|---|---|
| 默认 0 退化 | **T17-1**（守门） | 三个既有场景显式传 `padding=0` 重跑，期望值与既有用例**逐位相同** |
| 单子 padding 定位 | **T17-2**（块 A/B） | 位置 `(p, p)`；`fill=false` 时**不碰跨轴尺寸** |
| `fillCrossAxis` + padding 的跨轴宽 | **T17-3**（块 A/B） | 每子跨轴 = `父跨轴 − 2p`；V/H 对称两面 |
| stretch 与 padding 共存 | **T17-4** | 黄金数据 `146/146/148` + 等式闭合 |
| **空容器** | **不单设用例** | ① `Arrange` 内 `if (count == 0) return;` 的早退**在任何 padding 计算之前**（在 ② 之前）⇒ padding 根本到不了该路径 ⇒ **结构性保证**，非运行时行为；② 既有 `Layout.VerticalLayout` / `Layout.HorizontalLayout` 的「0 子节点」块已覆盖（`padding=0` 情形）。⇒ **不新增用例，也不假装测过**（如实标注边界） |
| 嵌套累加 | **T17-6** | 绝对坐标 `10 + 20 = 30`（同时否定 `max` / `40` / `20`） |
| 负值钳 0 | **T17-8**（Release）+ **A2**（Debug） | 见下方分工块 |

**两处测试口径演进（相对需求稿 §6 初稿）**：

| 初稿 | 定稿 | 理由 |
|---|---|---|
| T17-5 `PaddingEmptyAndSingle` | **T17-5 `PaddingOverflow`** | 「空容器」被降级为**结构性保证**（上表）；腾出的位置给 **overflow 语义**——`remaining = 0` 时 **fixed 保持 / stretch 归零**，这正是第二轮评审抓出的概念混淆的实测护栏，价值显著更高 |
| T17-8 `PaddingHorizontalSymmetric` | **取消**（对称面并入各用例**块 B**）；腾出位置给**新增** `T17-7 PaddingIdempotent` | 对称面若单设用例只会重复 V 面判据（`diff 同构`是**代码结构**约束，非语义增量）；而**幂等在 padding 多参数场景下的验证**是初稿漏掉的真实缺口（初设 Q3 提出 → 详设 §3.4 冻结为契约 **C5**） |

**C6 的两条验证手段（并列，别混读）**：

```
C6 负值钳 0 + Debug 断言
├── Debug 侧 → FRAMEWORK_ASSERT(padding >= 0)
│              └── 验证 = A2（结构性验收：二进制特征串 11/11，证明断言确实编译进去了）
│                           ⚠️ 不是 T17-8 的一部分
└── Release 侧 → m_padding = padding < 0 ? 0 : padding
               └── 验证 = T17-8（运行时用例：断言几何等价于 padding = 0）
```

**为什么 Debug 侧不能有运行时用例**：Debug 下 `VerticalLayout(0, false, -10)` 会走 `HandleAssertFailure` ⇒ `Logger::Log(Fatal)` + `MessageBoxW` + `assert(false)` ⇒ **测试进程被终止**（连带整个 `ecdi_tests`）。⇒ 该分支**必须**留空（`#ifdef NDEBUG`）——这是契约设计使然，**不是测试偷懒**。

---

## 6. 验收（A1–A7）

| # | 判据 | 手段 / 口径 |
|---|---|---|
| **A1** | 四工具链 **Debug** 构建通过；`ecdi_tests` **226 passed, 0 failed**（⚠️ **226 是设计目标，不是既成事实**——本相位尚未实现。实施后以**实测结果**回填 §1.2 / §5.4；若实测的用例数或断言数与设计值不符，**以实测为准**，不强行追设计值——第三轮评审） | 用户（VS 2026 / CLion）。复跑：`cmake --build <dir> --target ecdi_tests` → 运行 |
| **A2** | **断言特征串 11/11**：新增 `padding >= 0` 必须能在**库目标 obj**（`CMakeFiles/ECDI.dir/Layout/VerticalLayout.cpp.obj` 与 `…HorizontalLayout.cpp.obj`）+ exe 中搜到；既有 10 条不丢 | skill 条 50（**必须是窄串**、**落在库 obj**、宏为**单参数**）。⚠️ Release 恒 0/11 属预期 |
| **A3** | **零回归**：既有 11 条 `Layout.*` 用例**未改动**（`git diff` 显示用例块 0 改动）＋ 全库既有 **218** 条全绿 | `git diff -U0` 逐行核对被删行 |
| **A4** | **结构性判据（跨轴取值唯一入口）**：`VerticalLayout.cpp` 中 `parent.GetWidth()` **仅 1 处**且位于 `cross` 定义行；`HorizontalLayout.cpp` 中 `parent.GetHeight()` **仅 1 处**且位于 `cross` 定义行（**排除注释行**——skill 条 44） | grep + 逐行判定 |
| **A5** | **结构性判据（旧写法零残留）**：`int y = 0` / `int x = 0` / `SetPosition(0, y)` / `SetPosition(x, 0)` 各 **0 处**；`SetPosition(m_padding, y)` / `SetPosition(x, m_padding)` 各 **1 处** | grep |
| **A6** | **ModelProbe 目视**：内容四边不再贴死客户区、鼠标拖选不再越出窗口边界 | ★ **须 `main.cpp` 单独授权**（skill 条 2）。建议 root `padding = 12`（O3）；也可只在 `ModelProbe.cpp:155` 的 page 一级配（**但「全局留白」只有 root 级能做到**） |
| **A7** | **文档同步**（收口时）：`docs/README.md` + 根 `README.md` 的 Phase17 状态与规模锚点 · `roadmap-deferred.md §7.6 #38` → **✅ 已实现（Phase 17）** · `.workbuddy/memory/MEMORY.md` 阶段索引 | 收口清单 |

> **O1 的收尾**（已冻结的断言形态）在 A2 落地：`FRAMEWORK_ASSERT(padding >= 0)` ⇒ 特征串 **10 → 11**。这是本相位**唯一**对「框架级可观测面」的净增。

---

## 7. 影响面与回归清单

### 7.1 调用点实测枚举（2026-09-20 · 排除构建目录）

| 文件 | 行 | 实参形态 | 数量 | 参与构建 |
|---|---|---|---|---|
| `examples/ModelProbe/ModelProbe.cpp` | `:155` `:199` `:240` `:273` `:345` `:365` `:396` `:434` | `V(10,true)` / `H()` ×4 / `H(0,true)` / `V(0,true)` / `H(8,false)` | **8** | ✅ `modelprobe` |
| `examples/ModelProbe/main.cpp` | `:262` | `V(0, true)` | **1** | ✅ `modelprobe`（★ AI 不得自改） |
| `examples/VisualTest/main.cpp` | `:193` | `V(6, true)` | **1** | ✅ `visualtest`（**初设未列**） |
| `src/Demo/Showcase.cpp` | `:83` `:112` `:149` `:205` `:285` `:317` `:360` `:397` `:492` | `V()` ×7 / `H()` ×2 | **9** | ❌ **无目标** |
| `src/Demo/CollapsiblePanelDemo.cpp` | `:20` | `V()` | **1** | ❌ **无目标** |
| `src/Tests/LayoutTests.cpp` | 22 处 `SetLayout` | `V`/`H`，0–2 个实参 | **20** | ✅ `ecdi_tests` |
| `README.md`（文档示例） | `:114` | `V(0, true)` | **1** | — 文档 |
| **合计** | | **全部 ≤ 2 个实参** | **41** | |

**结论**：追加第 3 个**可选**形参对 **41/41** 处**零破坏**；`src/Demo` 的 10 处因**本就不参与构建**而无法编译验证（既有孤儿状态，见 §8 L4）。

### 7.2 回归清单

| 面 | 预期 |
|---|---|
| 既有 218 用例 | **全绿**（`padding` 默认 0 ⇒ 逐位退化，C1） |
| 既有 11 条 `Layout.*` | **全绿且源文件零改动**（A3） |
| 断言特征串 | 10 → **11**（只增不减；既有 10 条必须仍在——A2） |
| 公共头 | 92 → **92**（无新增头） |
| ABI / API | 构造签名**追加可选形参**——对既有**二进制**消费者属 ABI 变化（函数签名含 `int` 参数 ⇒ 符号名变化）。⚠️ 但 v0.x **不做 ABI 稳定承诺**（`ECDIConfigVersion.cmake` 的 `COMPATIBILITY ExactVersion`），且 0.2.0 属可破坏版本 ⇒ 接受 |
| 代码体积 | 每个构造 +1 `int` 成员（8 字节含对齐）——可忽略 |

---

## 8. 已知局限与记账（L1–L5）

| # | 项 | 性质 | 说明 |
|---|---|---|---|
| **L1** | **`AutoSize()` × padding 的交互** | **记账（无消费者）** | `AutoSize()` 只按 `GetPreferredSize()` 设**自身**尺寸，**不感知父布局的 padding**。⇒ 若将来把「窗口尺寸 = root 内容尺寸」这条链接起来（**当前 ModelProbe 没有这么做**——`AutoSize()` 仅用于 `ModelProbe.cpp:718` 的 `m_statLabel` 叶子标签），则 root padding 会**吃掉内容**而不是在外围加留白。**当前不构成问题**；记入 `roadmap-deferred.md`，出现真实消费者时再议。<br>**⇒ 顺带回答需求稿 §1.1 的归因**：用户原始表述里的「AutoSize 需要修改」**其实不需要改 AutoSize**——真实场景是**固定尺寸窗口内的内容贴边**，修复点确在布局层 |
| **L2** | padding **不参与命中** | **有意** | 父的命中区域仍是自身矩形（`ContainsRect`）⇒ padding 区域**照旧可命中父本身**。这是「布局留白 ≠ 命中屏障」的语义选择；若将来需要「padding 区不接收事件」，属**另一套语义**（未立项） |
| **L3** | `spacing` 无 Release 钳 0 | **有意的不对称** | `spacing` 自 9.7 起只有 Debug 断言（`VerticalLayout.cpp:13`）。统一它属**本相位范围外的行为变更**。⇒ 若将来要统一，单独立项 |
| **L4** | `src/Demo/*` 的 10 处调用点**未编译验证** | **既有孤儿状态** | `CMakeLists.txt:42` 把 `/Demo/` 排除出库，`:148`/`:151` 只加 ModelProbe 与 VisualTest ⇒ 该目录**不属于任何 CMake 目标**。本相位对它的影响为「源码文本兼容但未编译验证」——风险为零（追加可选形参），但**如实记账** |
| **L5** | `src/Demo/Showcase.cpp:64` 注释已失实 | **既有账，不在本相位范围** | 原文「垂直间距占位（**VerticalLayout 无间距支持**——透明 Widget 撑高）」——**9.7 落地 `spacing` 后即已失实**。未改（该文件不参与构建，且改动会扩大本相位面）。⇒ 并入「孤儿目标清账」条目 |

---

## 9. 实施顺序（**两批** —— 按依赖切分，每批可独立编译）

| 批 | 内容 | 落盘后判据 |
|---|---|---|
| **A** | 源码 4 文件（`VerticalLayout.h/.cpp` + `HorizontalLayout.h/.cpp`，含 §1.3-4 的失实注释修正） | ① 编译通过；② **既有 218 用例仍全绿**——这是 **C1「逐位退化」的第一手证据**（不必等批 B） |
| **B** | `LayoutTests.cpp`：8 个用例 + 8 行注册 | `ecdi_tests` **226 全绿**；断言实测值与 §5.4 对账 |

**批间不做文档回写**——文档与索引在**验收通过后统一回写**，避免反复改动（与 Phase 16 同做法）。

**实施期盯防清单**：

1. `Arrange` 内**只动 §2.2 表列的 8 个行级改动点**，其余行逐字不动（skill 条 42）。
2. `cross` 必须是**循环外**一次求值（△4），**不要**写进循环体。
3. `(std::max)` **保留括号形式**（`<algorithm>` 的 `std::max`；Windows.h 的 `max` 宏防护）。
4. 头文件注释的 **2 处失实修正**（△3 与 `.cpp:59`）**不得省**——它们是本相位「语义已变、描述不许说谎」的一部分（skill 条 80）。
5. 新用例**沿用 4 空格缩进**（该文件既有风格），不要按框架源码的 Tab 写。
6. 落盘后跑**双工具链 `-fsyntax-only -D_DEBUG`** 静态自查（本会话既有做法），再做 A2/A4/A5 的结构性判据。
7. ★ **`FRAMEWORK_ASSERT` 必须判形参 `padding`，不得改成判成员 `m_padding`**（第三轮评审强调）——`m_padding` 已被三元钳 0，判它就等于**永久掩盖非法输入**（Debug 也拦不住）。同理 `cross` **不得**挪进循环体（A4 以它为结构性判据）。

---

## 10. 修订记录

- **v1.1**（2026-09-20）**采纳第三轮评审（结论「🟢 可以进入实现」）的 3 项修正 + 1 项新增**。① **需求稿口径同步**：§5.5 记录初稿用例表的**两处演进**（`PaddingEmptyAndSingle` → `PaddingOverflow`；`PaddingHorizontalSymmetric` 取消、位置给新增的 `PaddingIdempotent`），并逐条映射需求 R9 的 7 个覆盖项（其中**「空容器」明确降级为结构性保证、不单设用例**）；需求稿本身同步至 **v1.1**。② **A2 与 T17-8 的概念边界**（评审第五、十三节）：§4 的 **C6 行**与 §5.2 的 T17-8 段均改写为「**Release 侧 = T17-8 运行时用例；Debug 侧 = A2 结构性验收**」的并列表述，并显式声明 **A2 不是 T17-8 的一部分**。③ **A1 的 226 标注为「设计目标」**（评审第六节）：§1.2 与 §6-A1 均补「实施后以实测回填、不强行追设计值」。④ **盯防清单新增第 7 条**（评审第十节红线）：`FRAMEWORK_ASSERT` 必须判**形参**，不得改判 `m_padding`。⑤ 评审第十一节的「`cross` 循环外求值」已在原盯防第 2 条覆盖，未重复新增。

- **v1.0**（2026-09-20）**详细设计初稿**。输入 = 需求确认 v1.0 + 初步设计 **v1.1**（外部评审「通过，可进入详细设计」，其 3 项前置修正已在初设 v1.1 落实 ⇒ 无阻塞项）。① **§1.1/§1.2 改动清单与规模锚点**（公共头 92→92 · 用例 218→**226** · 断言特征串 10→**11** · 断言 +85）；② **§1.3 七处细化**——其中 **第 4 条查出初设漏项**（`@param fillCrossAxis` 与 `.cpp:59` 的「跨轴坐标恒 0」在 `padding > 0` 后**失实**，必须改，skill 条 80）、**第 5 条更正调用点构成**（新增 `examples/VisualTest` 这个初设未列出的消费者；`src/Demo` 10 处不参与构建）；③ **§2 逐文件最小 diff**——`△1–△8` 八个行级改动点，**明确标注哪些行动、哪些行逐字不动**，并给出 V/H 同构对照；④ **§3 关键行为冻结**——`cross` 循环外求值、三条取值口径的落点、溢出区间与不变式的**精确表述**（区分 `fixedTotal` 与 `remaining`）、与既有机制（content offset / HitTest / resize 链 / AutoSize / CaptionBar）的**逐项「不动」声明**；⑤ **§4 契约表 C1–C6**（每条绑实现落点与验证手段）；⑥ **§5 测试规格**——逐用例**精确数值 + 断言清单**，全部期望值由**目标形态模拟脚本复算通过**（§5.3，skill 条 47）；**T17-8 明确 `#ifdef NDEBUG` 分支**及其理由（Debug 下负值会终止测试进程）；⑦ **§6 验收 A1–A7**，含两条**结构性判据**（A4 跨轴取值唯一入口 / A5 旧写法零残留）；⑧ **§7 调用点实测枚举 41 处**（含构建归属）；⑨ **§8 局限 L1–L5**——**L1 记明 `AutoSize × padding` 的交互**，并回答「用户的『AutoSize 需要修改』其实不需要改 AutoSize」；⑩ **§9 两批实施顺序 + 6 条盯防清单**。**待评审。**
