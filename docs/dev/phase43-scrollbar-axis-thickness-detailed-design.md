# Phase 43 · 滚动条双轴厚度（#84）· 详细设计稿

> 状态：**v1.1 · 已实施并自检通过**（2026-11-01）。上游 = 初设 **v1.2** + 需求 **v1.3**（两者已在详设期回流，见 §0.2）。实施实测回填见 **§6**。
> 全部行号对 HEAD `292153e` 亲核；基线：用例 **580**、公共头 **101**、设计文档 **212**（落本稿后 **213**）。
> ★ **施工原则**：产品码**全部按「整函数替换」施工**（不做局部手工编辑），本稿给的是可直接抄的完整函数体；测试码给的是完整可编译实现。改完只需对照 §2 的机检表跑一遍。

---

## §0 改动清单总表（施工索引）

### 0.1 文件与预算

| # | 文件 | 落点 | 改动形态 | 行数预算 |
|---|---|---|---|---|
| **1** | `ECDI/include/ECDI/Widget/ScrollView.h` | `:173-174` | 段替换（1 个声明 → 2 个声明 + 注释） | **+8 / −3** |
| **2** | `ECDI/src/Widget/ScrollView.cpp` | `:193-196` | 段替换（1 个定义 → 2 个定义） | **+6 / −4** |
| **3** | 同上 | `:200-222` | **整函数替换** `NeedsVerticalBar` | **+5 / −3** |
| **4** | 同上 | `:224-243` | **整函数替换** `NeedsHorizontalBar` | **+4 / −3** |
| **5** | 同上 | `:248` / `:255` | 两行单行替换 `ViewportWidth` / `ViewportHeight` | **+0 / −0**（同行改） |
| **6** | 同上 | `:261-292` | **整函数替换** `ApplyLayout` | **+6 / −4** |
| **7** | `ECDI/src/Tests/ScrollViewTests.cpp` | 匿名 namespace 内 + `RegisterScrollViewTests()` 尾部 | **4** 个测试函数 + **4** 行注册 | **+96 / −0** |
| — | `CMakeLists.txt` / `RunAllTests.h` / `RunAllTests.cpp` | — | **0 改动**（GLOB + 复用既有注册函数） | 0 |

**合计**：3 文件 · 产品码 **+29 / −17** · 测试 **+96** · 公共头 **101 → 101** · 公共 API **+0** · 用例 **580 → 584** · `FRAMEWORK_ASSERT` 特征串 **57 → 57**（零新增）。

### 0.2 ★ 详设期发现并修正的一处错误（回流记录）

写 §3 测试码时逐格代入算术，发现 **P4 的夹具归属在初设/需求里挂错了**：P4 的算例是 `t_v = 12 / t_h = 4`（`contentW = 190`），却被挂在 **T43-2**（其夹具是反号 `4 / 12`）名下。

- **为什么不只是笔误**：在 `4 / 12` 夹具下，正确实现的 `tW = 200 − BarFootprint(t_v = 4) = 194` ⇒ `190 > 194` 为 **false** ⇒ **横条不出现**，与 `12 / 4` 夹具下的期望**完全相反**（那里 `tW = 186` ⇒ 出现）。照错稿施工会写出一条**必红**的用例。
- **修正**：**P4 归 T43-1**（与 P1 / P3 同夹具 12/4，只换 content extent）；**T43-2 回到单相 P2**（保持 4/12 镜像）。用例数与预算**不变**（仍 4 条 / `+4` / `580 → 584`）。
- **回流**：需求稿 **v1.2 → v1.3**、初设 **v1.1 → v1.2**，两稿判据已同步（单源一致）。

---

## §1 逐文件施工代码

### 1.1 `ScrollView.h`（`:173-174` 段替换）

**BEFORE**

```cpp
	/// @brief 条厚度（读样式——参与 viewport 计算）
	[[nodiscard]] int BarThickness() const noexcept;
```

**AFTER**

```cpp
	/// @brief 竖条厚度（读**竖条自己**的样式；对应条为空时 0）
	/// @details 两轴厚度**各取各的**——不得用「一个厚度代表两条」（实现审计 M5 的原始缺陷形态）。
	/// ⚠️ 改厚度（如 `GetHorizontalScrollBar()->SetStyle(...)`）**不会**触发本容器重排：
	///    调用方须在改后调一次 `SetSize(GetWidth(), GetHeight())`（**同值也会重算**）；
	///    `SetContentExtent` 在 extent 未变时提前返回（`.cpp:85-89`），**救不了**（#84 R84-C′ / 审计 M6）。
	[[nodiscard]] int VerticalBarThickness() const noexcept;

	/// @brief 横条厚度（同上，读**横条自己**的样式）
	[[nodiscard]] int HorizontalBarThickness() const noexcept;
```

★ 三条注释要求缺一不算完成：①「读自己那条」 ② 空指针退化 ③ 改厚度后需 `SetSize`（并写明 `SetContentExtent` 救不了）。
★ **旧名删除、不留 alias**：留 alias 会让补全提示继续命中旧名——本缺陷成因正是「名字里没有轴」；删掉后编译器会把残留调用点全部报出来（用编译器代替 grep 做搬移检查）。

### 1.2 `ScrollView.cpp`（`:193-196` 段替换：定义拆分）

**BEFORE**

```cpp
int ScrollView::BarThickness() const noexcept{

	// 厚度来自样式（主题可改）⇒ 单点可查（ScrollBar::GetThickness）
	return (m_vBar != nullptr) ? m_vBar->GetThickness() : 0;

}
```

**AFTER**

```cpp
int ScrollView::VerticalBarThickness() const noexcept{

	// 厚度来自样式（主题可改）⇒ 单点可查（ScrollBar::GetThickness）
	return (m_vBar != nullptr) ? m_vBar->GetThickness() : 0;

}

int ScrollView::HorizontalBarThickness() const noexcept{

	// 同上——读的是**横条自己的**样式，不是竖条的（#84 S6 / 审计 M5）
	return (m_hBar != nullptr) ? m_hBar->GetThickness() : 0;

}
```

### 1.3 `NeedsVerticalBar()`（`:200-222` 整函数替换）

**AFTER**（★ 标 `+` 的两处是本 Phase 的实质改动）

```cpp
bool ScrollView::NeedsVerticalBar() const noexcept{

	if (!m_barsVisible){

		return false;

	}

+	// ★ #84（S1）：扣除量必须取**横条**厚度——「是否需要竖条」取决于视口**高**，
	//    而视口高被**横条**吃掉。此处曾误用竖条厚度（审计 M5 的第①②面）。
	const int w0 = GetWidth();
	const int h0 = GetHeight();

	// 第一轮：用**未扣除**的视口初判
	const bool nV1 = m_contentH > h0;
	const bool nH1 = m_contentW > w0;

	// 第二轮：按第一轮结论扣除后再判——**单调升级**
	//（viewport 只会变小 ⇒ need 只会 false→true ⇒ 并集即不动点，最多 2 轮收敛）
+	const int tH = h0 - (nH1 ? BarFootprint(HorizontalBarThickness()) : 0);   // v1.6：条占「厚度 + 内缩」

	return nV1 || (m_contentH > tH);

}
```

★ **删掉了原来的 `const int t = BarThickness();`（`:208`）**——拆分后它只服务一处，留着会成为「初始化但未引用」（MSVC `/W4` C4189），而本仓零警告。
★ **`return nV1 || (...)` 的短路结构一字不动**：它是本缺陷能在双轴溢出相里「隐身」的原因（初设 §1.1），也是测试必须带 P3/P4 的原因——**不要顺手把它改成无短路形式**，那是行为变更，不在本 Phase 授权内。

### 1.4 `NeedsHorizontalBar()`（`:224-243` 整函数替换）

**AFTER**

```cpp
bool ScrollView::NeedsHorizontalBar() const noexcept{

	if (!m_barsVisible){

		return false;

	}

+	// ★ #84（S2）：扣除量取**竖条**厚度——「是否需要横条」取决于视口**宽**，
+	//    而视口宽被**竖条**吃掉。本处原本就对，改成显式名字是为了让「轴」在调用点可读。
	const int w0 = GetWidth();
	const int h0 = GetHeight();

	// 两轮判定（同 `NeedsVerticalBar`，详设 §3.3）
	const bool nV1 = m_contentH > h0;
	const bool nH1 = m_contentW > w0;

+	const int tW = w0 - (nV1 ? BarFootprint(VerticalBarThickness()) : 0);   // v1.6：条占「厚度 + 内缩」

	return nH1 || (m_contentW > tW);

}
```

### 1.5 `ViewportWidth()` / `ViewportHeight()`（`:248` / `:255` 单行替换）

```cpp
	// :248（ViewportWidth）——对轴，语义不变，仅换名
	return GetWidth() - (NeedsVerticalBar() ? BarFootprint(VerticalBarThickness()) : 0);
```

```cpp
	// :255（ViewportHeight）——★ 本 Phase 的实质改动（S4）：原来扣的是**竖条**厚度
	return GetHeight() - (NeedsHorizontalBar() ? BarFootprint(HorizontalBarThickness()) : 0);
```

### 1.6 `ApplyLayout()`（`:261-292` 整函数替换）

**AFTER**

```cpp
void ScrollView::ApplyLayout(){

+	// ★ #84：两条**各取自身厚度**（原实现共用一个 `t` ⇒ 横条的 thickness 被整体丢弃，R84-A）
+	//    ⚠️ 若将来让厚度**依赖视口**（如「条随容器变细」），本处的「常量化」前提失效，
+	//       两轮判定的收敛性须重新论证（详设 §3.3）。
+	const int tv = VerticalBarThickness();
+	const int th = HorizontalBarThickness();

	// ① 条几何（**视口空间**——固定在视口，不受偏移）
	//    ★ v1.6：四边**内缩** `kBarInset`（不贴边、不压外层容器的边框环与圆角）；
	//    垂直条贴右、水平条贴底；交叉区仍不做缩角（记账 L2 不变——两条照旧在角落重叠）
+	//    ★ #84：此处**不判可见性**——不可见条仍被排满（可见性只影响绘制与 HitTest，见 C43-5）
	const int s = kBarInset;

	if (m_vBar != nullptr){

		m_vBar->SetSize(tv, (std::max)(0, GetHeight() - s * 2));   // 退化保护：过矮时钳 0
		m_vBar->SetPosition(GetWidth() - tv - s, s);

	}

	if (m_hBar != nullptr){

		m_hBar->SetSize((std::max)(0, GetWidth() - s * 2), th);    // ★ v1.6：同垂直条，四边内缩
		m_hBar->SetPosition(s, GetHeight() - th - s);

	}

	// ② 内容节点几何 = 内容 extent（其子在**内容坐标系**里布局）
	if (m_content != nullptr){

		m_content->SetSize(m_contentW, m_contentH);
		m_content->SetPosition(0, 0);

	}

}
```

---

## §2 机检判据（I43 · 施工后实测口径）

| # | 判据 | 命令 / 形式 | 期望（**实测值**） |
|---|---|---|---|
| **I43-1** | 旧名代码行清零 | `grep -n "BarThickness" ScrollView.h ScrollView.cpp` → `grep -v -e Vertical -e Horizontal` → `grep -v '^[^:]*:[0-9]*:[[:space:]]*//'` | **0 行** |
| **I43-2** | 新名计数（代码行） | 同上口径逐名 `grep -c` | `.h`：`Vertical` **1** / `Horizontal` **1**；`.cpp`：各 **4**（1 定义 + 3 调用）⇒ 合计 **10** |
| **I43-3** | **对轴**（靶心） | 逐函数体 grep：`NeedsVerticalBar` 内 `Horizontal`=1 / `Vertical`=0；`NeedsHorizontalBar` 内 `Vertical`=1 / `Horizontal`=0；`ViewportWidth` 内 `Vertical`=1；`ViewportHeight` 内 `Horizontal`=1；`ApplyLayout` 内各 =1 | **全中** |
| **I43-4** | 助手不读可见性 | 两定义体内 `IsVisible` / `m_barsVisible` | **0 命中** |
| **I43-5** | 公共面 | `git diff --stat` 不含 `ScrollBar.h` / `ScrollBarStyle.h` / `ScrollBar.cpp`；公共头计数 = `find ECDI/include -name "*.h"` 的行数 | **101** |
| **I43-6** | 用例与文档 | `ecdi_tests` / `doc_lint` | **584** 全绿 · `doc_lint` = 基线（71 缺陷全为存量 L01）+ `docs=213` |
| **I43-7** | 同厚等价 | `ScrollViewTests.cpp:242` / `:268` 的 `186.0f` 与 `:586-614` 的 `TestScrollBarInsetAndViewport` **零改动** | 仍绿 |
| **I43-8** | 六站点覆盖矩阵 | 人工对表（详设 §3 每用例的首行注释已标注守护站点） | **6/6 有守** |

★ **机检纪律**：所有「按标识符 grep」的判据**必须排除注释行**——本仓注释是 **TAB + `///`**，口径统一为 `'^[^:]*:[0-9]*:[[:space:]]*//'`（`grep -n` 输出的 `文件:行:` 之后判断）。否则在注释里写「旧名已拆」就会让 I43-1 自伤。

---

## §3 测试源码（完整可编译 · 4 条用例）

放在 `ECDI/src/Tests/ScrollViewTests.cpp` 的匿名 namespace 内（`TestableScrollView` / `kEps` 已有，`:31` / `:42`）。

```cpp
// ── T43-1 双轴厚度：异厚 12 / 4 三相 ─────────────────────────
// 守护站点：P1 = S3 / S4 / S5 / S6 · P3 = S1 · P4 = S2
void TestScrollViewBarThicknessPerAxis(){
	TestableScrollView sv;

	// ★ 顺序固定：样式先于 SetSize（R84-C′：改厚度不触发重排，反序会假红）
	sv.GetHorizontalScrollBar()->SetStyle(ScrollBarStyleOverride{ .thickness = 4 });
	sv.SetSize(200, 100);

	ScrollBar* vBar = sv.GetVerticalScrollBar();
	ScrollBar* hBar = sv.GetHorizontalScrollBar();

	EXPECT_EQ(vBar->GetThickness(), 12);
	EXPECT_EQ(hBar->GetThickness(), 4);

	// P1：双轴溢出 400 × 400
	sv.SetContentExtent(400, 400);

	EXPECT_EQ(vBar->GetWidth(), 12);
	EXPECT_NEAR(vBar->GetAbsolutePosition().x, 186.0f, kEps);   // 200 − 12 − 2
	EXPECT_EQ(hBar->GetHeight(), 4);
	EXPECT_NEAR(hBar->GetAbsolutePosition().y, 94.0f, kEps);    // 100 − 4 − 2
	EXPECT_EQ(sv.GetMaxOffsetX(), 214);                          // 400 − (200 − 12 − 2)
	EXPECT_EQ(sv.GetMaxOffsetY(), 306);                          // 400 − (100 − 4 − 2)

	// P3：只有横条（contentH = 90）——S1 的唯一守护相
	sv.SetContentExtent(400, 90);

	EXPECT_FALSE(vBar->IsVisible());
	EXPECT_EQ(vBar->GetWidth(), 12);          // ★ 不可见 ≠ 尺寸 0（ApplyLayout 不判可见性）
	EXPECT_EQ(hBar->GetHeight(), 4);
	EXPECT_NEAR(hBar->GetAbsolutePosition().y, 94.0f, kEps);
	EXPECT_EQ(sv.GetMaxOffsetX(), 200);       // 200 − 0（竖条不占位）
	EXPECT_EQ(sv.GetMaxOffsetY(), 0);         // 90 < 94 ⇒ 钳 0

	// P4：只有竖条（contentW = 190）——S2 的唯一守护相
	sv.SetContentExtent(190, 400);

	EXPECT_TRUE(hBar->IsVisible());
	EXPECT_EQ(hBar->GetHeight(), 4);
	EXPECT_EQ(sv.GetMaxOffsetX(), 4);         // 190 − (200 − 12 − 2)
	EXPECT_EQ(sv.GetMaxOffsetY(), 306);       // 400 − (100 − 4 − 2)
}

// ── T43-2 反号 4 / 12（镜像）───────────────────────────────
// 守护站点：S5 / S6 —— 与 T43-1 的 P1 互为镜像，防「只改一处刚好绿」
void TestScrollViewBarThicknessAxisSwapped(){
	TestableScrollView sv;

	sv.GetVerticalScrollBar()->SetStyle(ScrollBarStyleOverride{ .thickness = 4 });
	sv.SetSize(200, 100);
	sv.SetContentExtent(400, 400);

	ScrollBar* vBar = sv.GetVerticalScrollBar();
	ScrollBar* hBar = sv.GetHorizontalScrollBar();

	EXPECT_EQ(vBar->GetWidth(), 4);
	EXPECT_NEAR(vBar->GetAbsolutePosition().x, 194.0f, kEps);   // 200 − 4 − 2
	EXPECT_EQ(hBar->GetHeight(), 12);
	EXPECT_NEAR(hBar->GetAbsolutePosition().y, 86.0f, kEps);    // 100 − 12 − 2
	EXPECT_EQ(sv.GetMaxOffsetX(), 206);                          // 400 − (200 − 4 − 2)
	EXPECT_EQ(sv.GetMaxOffsetY(), 314);                          // 400 − (100 − 12 − 2)
}

// ── T43-3 非默认同厚 8 / 8 ──────────────────────────────────
// 判据目的：抓「助手写死 12 / 写死常量」——默认同厚下写死也绿，故必须非默认
void TestScrollViewBarThicknessFromStyle(){
	TestableScrollView sv;

	sv.GetVerticalScrollBar()->SetStyle(ScrollBarStyleOverride{ .thickness = 8 });
	sv.GetHorizontalScrollBar()->SetStyle(ScrollBarStyleOverride{ .thickness = 8 });
	sv.SetSize(200, 100);
	sv.SetContentExtent(400, 400);

	ScrollBar* vBar = sv.GetVerticalScrollBar();
	ScrollBar* hBar = sv.GetHorizontalScrollBar();

	EXPECT_EQ(vBar->GetWidth(), 8);
	EXPECT_NEAR(vBar->GetAbsolutePosition().x, 190.0f, kEps);   // 200 − 8 − 2
	EXPECT_EQ(hBar->GetHeight(), 8);
	EXPECT_NEAR(hBar->GetAbsolutePosition().y, 90.0f, kEps);    // 100 − 8 − 2
	EXPECT_EQ(sv.GetMaxOffsetX(), 210);                          // 400 − (200 − 8 − 2)
	EXPECT_EQ(sv.GetMaxOffsetY(), 310);                          // 400 − (100 − 8 − 2)
}

// ── T43-4 旧行为护栏（入库负对照）───────────────────────────
// 判据目的：把「两轴共用同一个厚度」这一**旧行为**变成一条会红的断言——
//          旧实现下 vBar 宽 == hBar 高（都取竖条厚度），本用例必红。
//          ★ 与 T43-1 的 P1 断言不重复：P1 判「值对不对」，本条判「两轴是否被分开」。
void TestScrollViewBarThicknessAxesAreIndependent(){
	TestableScrollView sv;

	sv.GetHorizontalScrollBar()->SetStyle(ScrollBarStyleOverride{ .thickness = 4 });
	sv.SetSize(200, 100);
	sv.SetContentExtent(400, 400);

	const int vWidth  = sv.GetVerticalScrollBar()->GetWidth();
	const int hHeight = sv.GetHorizontalScrollBar()->GetHeight();

	EXPECT_EQ(vWidth, 12);
	EXPECT_EQ(hHeight, 4);
	EXPECT_TRUE(vWidth != hHeight);   // ★ 旧行为（单轴厚度）下两者恒相等 ⇒ 必红
}
```

**注册（`RegisterScrollViewTests()` 尾部追加 4 行，`:636` 之后）**

```cpp
	GetTestRegistry().Add("ScrollView.BarThicknessPerAxis",         &TestScrollViewBarThicknessPerAxis);
	GetTestRegistry().Add("ScrollView.BarThicknessAxisSwapped",     &TestScrollViewBarThicknessAxisSwapped);
	GetTestRegistry().Add("ScrollView.BarThicknessFromStyle",       &TestScrollViewBarThicknessFromStyle);
	GetTestRegistry().Add("ScrollView.BarThicknessAxesAreIndependent", &TestScrollViewBarThicknessAxesAreIndependent);
```

---

## §4 实施步骤与负对照（施工时照做）

### 4.1 单批施工顺序

1. `ScrollView.h` §1.1 → 编译（此时 `.cpp` 会因旧名消失而报一串错——**这是预期的**，用它列出全部调用点）。
2. `ScrollView.cpp` §1.2 → §1.3 → §1.4 → §1.5 → §1.6（**整函数替换**）。
3. 编译通过 ⇒ 跑 **MinGW Release** `ecdi_tests` ⇒ 应 **580 全绿**（此时新用例还没加，红线 1 已成立）。
4. 加 §3 的**四个**测试函数 + 注册 4 行 ⇒ 再跑 ⇒ **584 / 584**。
5. 逐臂做**施工期负对照**（§4.2，三臂）⇒ 每臂跑完**立即还原**。
6. 还原后终跑 ⇒ **584 / 584**，再交用户四链。

★ **T43-4 是入库的负对照用例（护栏断言）**，三臂负对照（§4.2）是**施工期手工步骤、不入仓**——两者不是一回事，不要混。入库 4 条 ⇒ `580 → 584`，与需求稿 v1.3 的预算一致；`doc_lint` 的 `tests=` 读数应为 **584**。

### 4.2 施工期负对照三臂（不入仓 · 每臂只改一行，跑完立即还原）

| 臂 | 改哪一行 | 预期红项 | 它证明什么 |
|---|---|---|---|
| **① 全回退对轴** | `ScrollView.cpp` 的 `HorizontalBarThickness()` 函数体 → `return VerticalBarThickness();` | T43-1：P1 横条高 `4→12` 红 · `y 94→86` 红 · `maxY 306→314` 红；P3 `IsVisible()` 红 · `maxX 200→214` 红 · `maxY 0→4` 红；P4 横条高红 · `maxY 306→314` 红。T43-2：横条高 `12→4` 红 · `y 86→94` 红 · `maxY 314→306` 红 | 判据整体有牙（不是「改回去也绿」） |
| **② 只回退 S6** | `ApplyLayout()` 里 `m_hBar->SetSize(..., th)` → `tv`，且 `SetPosition(s, GetHeight() - th - s)` → `tv` | T43-1：P1 / P3 / P4 的**横条高与 `y`** 断言红；**`maxX` / `maxY` 全绿**；T43-2 同 | **单处漏改必红**（初设 R1 的兜底从「声称」变「实证」） |
| **③ 只回退 S1** | `NeedsVerticalBar()` 里 `BarFootprint(HorizontalBarThickness())` → `BarFootprint(VerticalBarThickness())` | **只有 T43-1 的 P3 红**（`EXPECT_FALSE(vBar->IsVisible())` + `maxX 200→214`）；P1 / P4 / T43-2 / T43-3 **全绿** | **短路相不可省**（删掉 P3 就只有这一臂能抓到 S1） |

★ **三臂的红 / 绿分布已用独立数值模型逐臂模拟核对**（2026-11-01，脚本按 `NeedsVerticalBar` / `NeedsHorizontalBar` / `Viewport*` / `ApplyLayout` 四条规则重演，不依赖本稿文字）——**不是推演**，是与 §4.2 表逐项对齐后写的：臂① P1 `(hH 12, maxY 314)` · P3 `(needV true, maxX 214, maxY 4)` · P4 `(hH 12, maxY 314)` · P2 `(hH 4, maxY 306)`；臂② 横条高与 `y` 变、`maxOffset` 不变；臂③ 仅 P3 的 `needV`/`maxX` 变。★ 顺带记一个**易写成 186 的坑**：`P3` 在「续条凭空出现」时 `maxX` 是 **214**（`400 − (200 − 12 − 2)`），不是 186——186 是**视口宽**，不是偏移上限。

### 4.3 验证口径

- **自检**：**MinGW Release**（`cmake-build-release-mingw/ecdi_tests.exe`）。★ 必须 Release——本机 MinGW **Debug** 有**既有崩溃** `ImageDecode.Png1x1White`（与本相无关），按 Debug 跑会白耗一轮。
- **验收**：用户侧**四构建链**（MSVC / Clang / ClangCL / MinGW）`584/584`、退出码 0；★ 报「全绿」时须同时记录**断言是否启用**（`FRAMEWORK_ASSERT` 仅 `_DEBUG`）+ **显示器缩放**（本机 1920×1080 @ DPI 120）。
- **A2 人验**：双轴异厚（竖 12 / 横 4）下交叉区观感——横条压在竖条上是否可接受（需求稿 L43-5；不可接受则另行立项 L43-1 缩角）。

---

## §5 影响面与回归清单

| 面 | 证据 | 影响 |
|---|---|---|
| `ScrollViewTests.cpp:242` / `:268` | 硬编码 `186.0f` | 零影响（默认同厚） |
| `ScrollViewTests.cpp:586-614`（v1.6） | `t = vBar->GetThickness()` 供两条 | 零影响（默认同厚），**且它是红线 1 的守门人** |
| `ScrollBar.ThemeAndOverride`（`:556-578`） | `GetThickness()` 12 → 20 | 零影响（不碰 `ScrollBar`） |
| `ClipTests.cpp:695` | `boxRaw->GetVerticalScrollBar()` | `TextBox` 的条，与本类无关 |
| `TextBoxTests.cpp:2004` | `bar->GetThickness()` | `TextBox` 自有第二套摆位，不动（L43-3） |
| 渲染命令流 | `ScrollBar::OnPaint`（`:214-237`）只读自身矩形 | 同厚时逐位不变 |
| 失效管线 | `ApplyLayout` 仍只在既有 4 入口调用（`:47` / `:97` / `:177` / `:333`） | 零新增 `Invalidate` / 零新增布局脏源 |

**回退**：3 文件 `git checkout --`，无数据结构变更、无迁移负担、无 ABI 残留。

**未覆盖（承需求稿）**：L43-8 运行期改厚度无自动用例（R84-C′ 不做）；L43-9 异厚像素观感归 A2。

---

## §6 实施回填表（施工期占位，实现后逐行填实测）

| 项 | 预期 | 实测 | 差异说明 |
|---|---|---|---|
| 产品码行数 | +29 / −17 | **+36 / −14** | 差异 = 注释行（S1/S2/S4/`ApplyLayout` 四处各加 2–3 行说明）+ `VerticalBarThickness` 头注释 5 行；逻辑行与预算一致（2 定义 + 5 调用点） |
| 测试行数 | +96 | **+117** | 差异 = 每个用例的「守护站点 / 判据目的」注释（§3 中未计入预算的部分） |
| 公共头 | 101 → 101 | **101** ✔ | — |
| 用例 | 580 → **584** | **584 passed / 0 failed**（MinGW Release）✔ | — |
| 断言特征串 | 57 → 57 | **57** ✔ | — |
| `doc_lint` | 71 缺陷（全 L01）/ `docs=213` | **71 / 591 findings（全 L01）** · `measured: tests=584 headers=101 docs=213` ✔ | ★ 中途一度 74/594：L13 计数锚点（README ×2 + dev/README ×1）因 `tests` 580→584 变红，已续订回基线 |
| I43-1 | 0 行 | **0 行** ✔ | — |
| I43-2 | .h 1+1 / .cpp 4+4 | **.h 1+1 / .cpp 4+4 = 10** ✔ | — |
| I43-3 | 五函数体对轴全中 | **全中**（`NeedsVerticalBar` V0/H1 · `NeedsHorizontalBar` V1/H0 · `ViewportWidth` V1/H0 · `ViewportHeight` V0/H1 · `ApplyLayout` V1/H1）✔ | — |
| I43-4 | 0 命中 | **0 命中**（两助手只读 `m_vBar`/`m_hBar` + `GetThickness`）✔ | — |
| 四链 | 584/584 × 4 | **MinGW Release 584/584** ✔；MSVC / Clang / ClangCL 待用户侧 | 本机 MinGW Debug 有既有 `ImageDecode.Png1x1White` 崩溃，故用 Release |
| 负对照 ①②③ | 按 §4.2 全红 / 部分红 / 仅 P3 红 | **① 581/3 failed（T43-1/2/4）· ② 581/3 failed（只横条高与 `y`，`maxOffset` 全绿）· ③ 583/1 failed（仅 T43-1 的 P3：`IsVisible` + `maxX`）** ✔ 逐项与预测一致 | 三臂均实测、每臂跑完立即还原 |
| 还原后 | 584 全绿 | **584 passed / 0 failed** ✔；`grep "ARM [123]"` = **0** | — |

---

> 详细设计稿 **v1.1**（2026-11-01）：**已实施并自检通过**。实测 = 产品码 **+36 / −14**（偏差全在注释行）· 测试 **+117** · 用例 **580 → 584** · MinGW **Release 584/584 退出码 0** · 负对照三臂红绿分布与 §4.2 预测**逐项一致** · 机检 I43-1..4 全中 · `doc_lint` 回基线 **71 / 591**（全 L01）。**四链终验（MSVC / Clang / ClangCL / MinGW）待用户侧跑。** 实测明细见 §6。
