# Phase 20.1 · 渲染层 DPI 缩放（render DPI scaling）—— 详细设计（v1.0）

> 来源：`docs/phase20.1-render-dpi-scaling-preliminary-design.md`（**v1.1 ✅ 已通过**，2026-09-24）
> 状态：**v1.1**（2026-09-24）——✅ **评审通过**（外部评审结论：「**通过，可以进入实现**」，**无阻塞性架构问题**）· 本版按其 **2 处「实现前建议修改」** 逐条处置（见 **§1.6**）；★ 评审认可本稿**可直接作为实现 checklist**；★★ **v1.2 = 批一（`Renderer.{h,cpp}`）实施期的 §5 判据口径修正**（见 **§11**）
> 一句话：把「**框架内部(DIP) → 渲染层(物理)**」这条边落到 **`Renderer::Execute` 的入口**，形式 = 「**取几何 → 乘 `scale` → 传后端**」，**命令缓冲一字不动**

---

## 1. 设计输入与基线

### 1.1 已冻结（需求 v1.2 + 初设 v1.1，本稿不再讨论）

| 项 | 已冻结的内容 |
|---|---|
| **落点与形态** | `Renderer::Execute` 入口 · **A2**（执行期**瞬时转换**，**不碰 `CommandBuffer`**） |
| **注入形态** | **`Execute(const CommandBuffer&, float scale = 1.0f)`**（初设 §2.2；★ **第一轮评审已确认保留**） |
| **折算实现形态** | 三个**文件级 helper**（`ScaleRect` / `ScalePoint` / `ScaleLength`），**不加 `operator*`** |
| **折算范围** | **只折几何**；`font.size` / `text` / `color` / `image` **一律不动** |
| **取整口径** | 折算**只乘不取整**（C-8）；后端**既有**取整口径不统一**不在本阶段处理**（O1 记账） |
| **单位不变式** | 需求稿 **§3.1 U1–U5** + 本稿 **C-7**（输入 DIP / 输出物理） |
| **测试落点** | **扩展既有 `src/Tests/RendererTests.cpp`**（既有文件 ⇒ `RunAllTests.*` 零改动） |

### 1.2 详设新增基线（B12–B15，2026-09-24 逐条实测）

| # | 事实 | 位置 |
|---|---|---|
| **B12** | 8 个私有 `ExecuteCommand` 声明**逐一列出**；★ 其中 `PopClipCommand` 的 `cmd` 形参**命名但未被使用**（`Renderer.h:28` 声明 + `Renderer.cpp:58-62` 定义）——**既有先例** | `Renderer.h:22-29` |
| **B13** | `Renderer.cpp` 只 include `Renderer.h` + `RenderingBackend.h`；`Rect` / `Point` **经这两条链已可得**，且 helper **不做取整** ⇒ **不需要新增任何 `#include`**（**连 `<cmath>` 也不需要**） | `Renderer.cpp:1-3` |
| **B14** | 测试宏集 = `EXPECT_TRUE` / `EXPECT_FALSE` / `EXPECT_EQ` / `EXPECT_NE` / **`EXPECT_NEAR(lhs, rhs, eps)`**（★ 后者**强制 double 语义**） | `TestFramework.h:70-95` |
| **B15** | `RendererTests.cpp` **已有** `constexpr float kEpsilon = 0.001f`（`:26`）与 7 条注册（`:308-314`）；且**已 include** `Renderer.h` / `RecordingBackend.h` / `Point.h` / `Color.h` / `Font.h` / `Image.h` ⇒ ★ **新用例零 include 改动、零容差新定义** | `RendererTests.cpp:9-26` · `:306-315` |

**⇒ B13 + B15 合起来意味着：本子阶段的代码改动**没有**任何「依赖引入」成本**。

### 1.3 ★ 对初设的一处修正 + 三处细化（实现前必须接受）

#### ★ 修正 A：初设 §9-③ 的示例**不成立**（本稿据此改测试取值策略）

初设 §9-③ 写「`scale = 1.5f` 下**部分积在二进制中不可精确表示**」——★ **实测证伪**（脚本复算，float32 语义）：

| scale | 整数值 × scale 是否精确 | 证据 |
|---|---|---|
| `1.0f` / `1.5f` / `1.25f` / `2.0f` | ★ **全部精确** | `1.5 = 3/2` 可精确表示；`n × 1.5` 是半整数，**同样精确**（`3 × 1.5 = 4.5`，二进制 `100.1`） |
| `1.2f` | ❌ **不精确** | `3 × 1.2f = 3.6000001`（而实数解 `3.5999999`）· `100 × 1.2f = 120.0000076` |
| `1.1f` | ❌ **不精确** | `3 × 1.1f = 3.3000002` |

⇒ **本稿仍用 `EXPECT_NEAR` + 既有 `kEpsilon`**，但**理由不同**：不是"因为不精确"，而是**沿该文件既有的浮点断言惯例**（`RendererTests.cpp:26` 的 `kEpsilon`）。
⇒ ★★ **本条修正带来一个更好的测试设计**（见 §6.2-T20.1-3）：既然 `1.5f` 的乘积**精确**，就可以**刻意挑奇数输入**，让期望值落在**半整数**上（`1 → 1.5`）——**半整数能分辨「乘法」与「乘法后取整」**（`lround(1.5) = 2 ≠ 1.5`）。**若用偶数输入，期望值全是整数，这个测试就抓不到"偷偷取整"**。

#### 细化 B：helper **不写 `static`**

初设 §3.2 草图写的是 `static Rect ScaleRect(...)`。★ **匿名 namespace 内的函数已有内部链接** ⇒ `static` **冗余**（`-Wunused-function` 家族之外无收益）。本稿**不写 `static`**（见 §2.2 定稿）。

#### 细化 C：`PopClipCommand` 重载的未用形参写法

`PopClipCommand` **无几何字段**，但 `std::visit` 的 lambda **统一**调用 `ExecuteCommand(cmd, scale)` ⇒ 签名**必须**带 `float scale`。★ 该重载**两个形参都不使用**——**沿 B12 的既有先例**（`cmd` 本就未被使用），**不引入 `[[maybe_unused]]`**（既有代码从未用过该属性，为一致性起见不新开风格）。

#### 细化 D：`ScaleLength` 的**调用点清单**（= C-8 的 grep 判据点）

| 调用点 | 折算的量 | 数量 |
|---|---|---|
| `DrawLineCommand` | `width` | 1 |
| `DrawRoundedRectCommand` | `cornerRadius` | 1 |
| `DrawFocusRectCommand` | `cornerRadius` | 1 |
| **合计** | | **3**（**恰好 3 处**——多一处就说明有人误用了它） |

★ **明令禁止**：用 `ScaleLength` 折算 `Font::size`（那是 Q5 的**双重缩放**，见 §2.5 盯防清单 ①）。

### 1.4 初设 §9 七件事 → 本稿答案索引

| 初设 §9 | 事项 | 本稿落点 |
|---|---|---|
| **①** | 8 个 `ExecuteCommand` 重载的逐条改动规格 | **§2.1 + §2.2**（含 `PopClip` 写法 ⇒ §1.3 细化 C） |
| **②** | **默认实参的落点规则** | **§2.5 盯防清单 ②**（**只写声明、定义处不得重复**） |
| **③** | 测试期望值的逐点算准 | **§6.2**（★ 并用脚本复算，见 §1.3 修正 A） |
| **④** | A4 的 grep 判据具体式样 | **§5**（C-8 / C-11 两行） |
| **⑤** | Phase 20 文档的回填清单 | **§10 收口清单 1–2** |
| **⑥** ★ 评审指定 | **`scale` 的语义冻结** | **§3.1 + §3.2**（★ 含「认识 `scale` ≠ 认识 DPI」） |
| **⑦** ★ 评审指定 | **`scale` 的合法范围**（finite + positive，**不加运行时检查**） | **§3.3**（★ 含接口注释**逐字文本**，§3.4） |

### 1.5 ★ 上游（初设）评审意见的承接（已全部落地）

| 初设评审条目 | 要求 | 本稿处置 |
|---|---|---|
| **§5 + §17**（必冻结 ①） | 冻结 `scale` 的语义（本次 `Execute` 的执行上下文参数；Renderer 不保存；不依赖 `BeginFrame` 顺序；且**不认识 DPI**） | ✅ **§3.1 / §3.2**，并**逐字给出接口注释**（§3.4） |
| **§16**（必冻结 ②） | 明确 `scale` 合法范围 = **finite + positive**；★ **倾向不加运行时检查** | ✅ **§3.3**（**采纳"不加运行时检查"**，契约写在注释里） |
| **§9**（必冻结 ③） | **收紧「必然零回归」表述**（数学恒等成立，但不得外推成"247 个测试必然不变"） | ✅ **§4**（三层表述，**冻结**；★ 表内标"须由四工具链实测确认"） |
| **§11** | `≤1px` 不作无条件定理 | ✅ 已在初设 v1.1 §2.4 收紧；本稿 **§10 收口清单 3** 沿用同一措辞（★ 记账项 **O1 定义在初设 §6**） |
| **§14** | T20.1-3 顺带保护**源 `Image` 不被修改** | ✅ **§6.2-T20.1-3** 增加该断言 |
| **§6 / §7 / §8 / §10 / §12 / §13** | 确认保留：A2 · 三个 helper · Q5 不折字号 · Q7 不动 · `scale` 每帧取 · 五用例分工 | ✅ **原样保留，本稿未改** |

### 1.6 ★ 本稿（详设）第一轮评审的处置（2026-09-24）

> 评审结论：**「通过，可以进入实现」**，★ **无阻塞性架构问题**；附 **2 处「实现前建议修改」**（本版**均已落地**）+ 确认保留 12 项既有决定。

| 评审条目 | 内容 | 处置 |
|---|---|---|
| **§1–§8 · §11–§15** | 确认保留：`scale` 的语义链（**执行上下文参数 / 不保存 / 不认识 DPI**）· 三个 helper 而非 `operator*` · **8 重载映射表「可直接作为实现 checklist」** · `DrawText` 只折 `pos` ⇒ **不会 ×2.25** · `PushClip` 与绘制**同一个 `ScaleRect`**（★「100% 下看不出来、150% 才错位」的经典 DPI bug）· T20.1-3 的**半整数**技巧（★「同时验证**有没有缩放** + **有没有提前取整**」）· §1.3 修正 A 的方向 · **§4 三层零回归论证「已可以冻结」** · **不加 `assert`** · `PopClip` 不写 `[[maybe_unused]]` · **三批实现顺序**（★「**先验证局部能力，再接入真实路径**」）· AI 静态 / 用户实测的**验收分工** · §10 收口路径 | ✅ **采纳**（原样保留） |
| **§9** ★★ | ⚠️ **ABI 兼容性表述不清**（本稿最该改的一处）：默认实参**不是重载**、**也不是运行时包装**——它只是**调用点由编译器补上 `1.0f`**。⇒ 「**旧头文件 + 新库**」/「**旧目标文件 + 新库**」**不因默认值而 ABI 兼容**；新增形参**改变了函数签名**（符号变了）。须明确：**源码兼容保留 · ABI 不承诺** | ★ **采纳（实质修正）** ⇒ **§8 公共 API 行改写 + 新增 §8.1 兼容性口径表** |
| **§10** ★ | ⚠️ **Image 断言的因果不成立**：`Image` 是**值类型** ⇒ `auto image = cmd.image;` **复制之后** `width` / `height` / `stride` / `pixels` **全都相同**，本稿原写的目的（「防复制」）**推不出来**——该断言只能证明「**内容未被修改**」 | ★ **采纳（表述修正）** ⇒ **§6.2-T20.1-3 目的句改写** |
| **§13 / §14** | 三批顺序「比一次性全改更适合本阶段」；★ 确认 AI **不把「四工具链 + 252 全绿」当作已发生的事实**——它仍是**待实现后的验收标准** | ✅ **采纳**（本稿 §9 的「责任人」列即此分工） |
| **§15** | Phase 20 → 20.1 的**收口路径闭环**（不能让文档停留成「Phase 20 = DPI done · Phase 20.1 = 神秘附加功能」） | ✅ **采纳**（§10 收口清单 1–3） |

★ **本稿另修正一处评审未提的交叉引用错误**：§1.5 表原写「本稿 **§8-O1**」——★ **O1 定义在初设 §6，不在本稿 §8**（本稿 §8 是「影响面」）⇒ 已改为指向「本稿 §10 收口清单 3」。

---

## 2. ★ 逐文件行级改动（△1–△4）

### 2.1 `include/ECDI/Render/Renderer.h`（△1）

**① 类注释补定位声明**（在 `- BeginFrame/EndFrame 直接转发…` 之后**追加一行**）：

```cpp
		/// - ★ Phase 20.1：`Execute` 携带**坐标缩放因子** `scale`，把**框架几何(DIP)** 折算为
		///   **物理像素**后交给后端——这是本框架**唯一**的「框架内部 → 渲染层」换算边。
		///   ⚠️ `Renderer` **不保存** `scale`（它是**本次 Execute 调用的执行上下文参数**，
		///   不是帧状态）⇒ **不存在 `BeginFrame` → `Execute` 的顺序依赖**；
		///   ⚠️ `Renderer` **只知道 `scale`、不知道 DPI**（`scale` 恒为 `float`，不依赖 Windows）。
```

**② `Execute` 声明**（★ **默认实参只写在这里**）：

```cpp
		/// @brief 执行一批命令（★ Phase 20.1：`scale` = 本次执行的 DIP → 物理 几何缩放因子）
		/// @param commands 命令缓冲（几何恒为 **DIP**；★ 本函数**不修改**它——A2 / 契约 C-10）
		/// @param scale 坐标缩放因子（**finite + positive**；★ 语义 = **本次执行**的上下文档位，
		///        而非帧状态）。默认 `1.0f` = 恒等 ⇒ 既有调用点零改动 + 100% DPI 下逐位零回归。
		void Execute(const CommandBuffer& commands, float scale = 1.0f);
```

**③ 8 个私有重载**——★ **只加 `float scale`，注释原样不动**：

```cpp
		void ExecuteCommand(const DrawRectCommand& cmd, float scale);   // 决策 9：重载集，未来加命令只加重载
		void ExecuteCommand(const DrawTextCommand& cmd, float scale);   // D5：文本命令转发（穷尽性由 std::visit 保证）
		void ExecuteCommand(const DrawLineCommand& cmd, float scale);          // Phase 8
		void ExecuteCommand(const DrawRoundedRectCommand& cmd, float scale);  // Phase 8
		void ExecuteCommand(const DrawImageCommand& cmd, float scale);        // Phase 8
		void ExecuteCommand(const PushClipCommand& cmd, float scale);         // Phase 8（状态命令）
		void ExecuteCommand(const PopClipCommand& cmd, float scale);          // Phase 8（状态命令）
		void ExecuteCommand(const DrawFocusRectCommand& cmd, float scale);    // Phase 8
```

**★ 不动的**：`BeginFrame` 的声明**与它的注释**（`///< Phase 18：透明转发（背景色是决策层输入，Renderer 不持有）`）**一字不改**——这正是初设 §2.2 选 `Execute` 形态的**首要理由**。

### 2.2 `src/Render/Renderer.cpp`（△2）

**① 文件头**——**零 `#include` 改动**（**B13**）。

**② 匿名 namespace 新增三个 helper**（★ 定稿，含 §1.3 细化 B 的"不写 `static`"）：

```cpp
namespace ECDI {

	// ★★ Phase 20.1：本框架**唯一**的「框架内部(DIP) → 渲染层(物理)」几何折算实现点（契约 C-11）。
	// 纯乘法、**不取整**（契约 C-8——量化留给绘图 API：GDI 的最后一步）。
	// ⚠️ 匿名 namespace 已给内部链接，**不另写 `static`**；唯一消费者 = 本文件 ⇒ 不建新头。
	// ⚠️ 不给 Core 的 `Rect` / `Point` 加 `operator*`——那会把「DIP → 物理」降格为几何对象的
	//    普通数学操作，极易在 Widget / Layout 里被误用（Phase 20 极力避免的正是这个）。
	namespace {

		Rect ScaleRect(const Rect& rect, float scale) noexcept
		{
			return Rect{
				rect.x * scale,
				rect.y * scale,
				rect.width * scale,
				rect.height * scale
			};
		}

		Point ScalePoint(const Point& point, float scale) noexcept
		{
			return Point{ point.x * scale, point.y * scale };
		}

		/// @brief 标量折算（`width` / `cornerRadius`）
		/// @note ★ **不得**用它折算 `Font::size`——字号由后端按窗口 DPI 换算（Phase 20 批五），
		///       在 Renderer 再折一次即**双重缩放**（Q5 / C-8）。调用点**恰好 3 处**（§1.3 细化 D）
		float ScaleLength(float length, float scale) noexcept
		{
			return length * scale;
		}

	}
```

**③ `Execute` 绑定 `scale`**（★ **定义处不写默认实参**）：

```cpp
	// ★ Phase 20.1：`scale` **只在此处进入流程**，逐层**显式传参**（Renderer 不存成员）
	void Renderer::Execute(const CommandBuffer& commands, float scale)
	{
		for (const auto& command : commands)
		{
			std::visit([this, scale](const auto& cmd) { ExecuteCommand(cmd, scale); }, command);
		}
	}
```

> ★ **`commands` 是 `const CommandBuffer&`、循环体是 `const auto&`** ⇒ **A2 / C-10 由类型系统保证**（想改缓冲根本写不出来）——这是"不修改 `CommandBuffer`"的**第一道防线**，T20.1-5 是第二道。

**④ 8 个重载**（逐条折算规格，**与需求稿 §4 折算量清单逐条对齐**）：

| 重载 | 折算（★ 均为"取几何 → helper → 传后端"） | 不动的 |
|---|---|---|
| `DrawRectCommand` | `ScaleRect(cmd.rect, scale)` | `color` |
| `DrawTextCommand` | ★ **仅 `ScalePoint(cmd.pos, scale)`** | ★ **`font` 原样** · `text` · `color` |
| `DrawLineCommand` | `ScalePoint(start)` · `ScalePoint(end)` · ★ **`ScaleLength(width)`** | `color` |
| `DrawRoundedRectCommand` | `ScaleRect(rect)` · ★ **`ScaleLength(cornerRadius)`** | `color` |
| `DrawImageCommand` | ★ **仅 `ScaleRect(cmd.dest, scale)`** | ★ **`image` 原样**（仍按引用传，**无拷贝**） |
| `PushClipCommand` | `ScaleRect(cmd.rect, scale)` | — |
| `PopClipCommand` | —（**两个形参均不使用** ⇒ §1.3 细化 C） | — |
| `DrawFocusRectCommand` | `ScaleRect(rect)` · `ScaleLength(cornerRadius)` | `color` |

**★ `PushClip` 与绘制必须用同一个 `ScaleRect`**——否则 **clip 与 fill 会不同尺度**（那正是 `WM_NCHITTEST` 那条老问题的同型）。

### 2.3 `src/Window/Window.cpp`（△3）

**`PaintFrame` 的 `:136` 单行**：

```cpp
		m_renderer.Execute(m_commands, GetDpiScale());   // ★ Phase 20.1：每帧取窗口 DPI 缩放
```

**★ 帧编排五步顺序一字不动**（`clear → Root.Paint → BeginFrame → Execute → EndFrame`，Phase 18 详设 §6 第 1 条已冻结）；本改动**只加一个实参**。

★ `GetDpiScale()` 是 `const noexcept` 成员（**B9**）、`PaintFrame` 非 const ⇒ **可直接调用**；平台窗口未就绪时它返回 `1.0f` ⇒ 恒等退化（与本阶段之前的行为一致）。
★ **每帧取**（不缓存）⇒ 跨屏 `WM_DPICHANGED` 后**下一帧自动跟随**，无需任何新通路（O2）。

### 2.4 `src/Tests/RendererTests.cpp`（△4）

**★ 零 include 改动、零容差新定义**（**B15**）；新增 **5 个测试函数**（匿名 namespace 内）+ **5 条注册**（`RegisterRendererTests()` 内）。

**注册（追加在既有 7 条之后）**：

```cpp
    GetTestRegistry().Add("Renderer.ScaleIdentity",            &TestRendererScaleIdentity);
    GetTestRegistry().Add("Renderer.ScaleRectAndClip",         &TestRendererScaleRectAndClip);
    GetTestRegistry().Add("Renderer.ScaleAllGeometryFields",   &TestRendererScaleAllGeometryFields);
    GetTestRegistry().Add("Renderer.ScaleTextPositionOnly",    &TestRendererScaleTextPositionOnly);
    GetTestRegistry().Add("Renderer.ScaleLeavesBufferIntact",  &TestRendererScaleLeavesBufferIntact);
```

**完整输入 / 期望**见 **§6.2**。

### 2.5 ★ 实现盯防清单（本子阶段最易写错的 6 条）

| # | 陷阱 | 判据 / 后果 |
|---|---|---|
| **①** | ★★ **折了 `font.size`** | 字体**双重缩放**（Renderer ×1.5 与后端 ×1.5 ⇒ 实际 **×2.25**）⇒ **T20.1-4 专抓**；`ScaleLength` 的调用点必须**恰好 3 处**（§1.3 细化 D） |
| **②** | ★★ **默认实参写了两遍** | 声明与定义**都写** `= 1.0f` ⇒ **编译错误**（C++ 禁止重定义默认实参）。★ **只写在 `Renderer.h`** |
| **③** | **漏折 `width` / `cornerRadius`** | 只折 `Rect` / `Point` 而漏标量 ⇒ 线宽与圆角**不随 DPI 缩放**（最典型的漏法）⇒ **T20.1-3 专抓**（奇数输入 ⇒ 半整数期望值，见 §6.2） |
| **④** | **漏折 `PushClip`** | clip 是 DIP 而 fill 是物理 ⇒ **裁剪区域错位**（且 100% DPI 下看不出来） |
| **⑤** | **顺手改了 `CommandBuffer`** | 破坏 A2（多后端共用缓冲会**语义污染**）；★ **类型系统已挡一道**（`const CommandBuffer&` + `const auto&`）⇒ **T20.1-5 是第二道** |
| **⑥** | **`PopClip` 重载里写了未用形参的处理** | 该重载**两个形参都不使用**是**既有先例**（**B12**）⇒ **不要**引入 `[[maybe_unused]]`、**不要**为它写 `(void)scale;`——保持与其他 7 个重载**同形** |

---

## 3. `scale` 的完整语义与合法范围（★ 评审指定 ⑥ / ⑦）

### 3.1 语义（★ 冻结）

> **`scale` 是本次 `Execute` 调用的执行上下文参数。**
> 它表示「**当前 `CommandBuffer` 对应的 DIP → physical 几何缩放因子**」。
> **`Renderer` 不保存该值**，也**不要求 `BeginFrame` / `Execute` 之间建立任何状态依赖**。

★ **为什么必须写死这一条**（评审 §5 指出的**术语错位**）：本子阶段的文档里 `scale` 曾被称作「**帧级**坐标变换」，而接口形态是 `Execute` 的**形参** ⇒ 读者会疑惑「`scale` 到底是 `Renderer` 的状态，还是 `Execute` 的参数」。**答案 = 后者**。若将来真要做成帧级状态（`BeginFrame(background, scale)`），那是**另一次决定**（初设 §2.2 已给出等价的 ~3 行切换路径）。

### 3.2 ★ 「认识 `scale` ≠ 认识 `DPI`」（★ 冻结）

```text
Renderer 看到：  float 1.25f          ← 它不知道这是什么
                 ↓
              × geometry
                 ↓
Backend 收到：   物理几何
```

`Renderer` **不知道** `1.25f` 是 **120 DPI**、**125% 缩放**、还是将来某个 **UI transform**。
⇒ ★ **`Renderer` 仍然零 Win32 依赖**（无 `GetDpiForWindow`、无 `DpiConversion.h`、无 `Windows.h`）——**这是"平台无关性不破"的可检判据**（§5）。

### 3.3 合法范围 = **finite + positive**（★ 冻结；**不加运行时检查**）

| 项 | 决定 |
|---|---|
| **契约** | `scale` 必须是**有限正数**（`std::isfinite(scale) && scale > 0.0f`）——**由调用方保证** |
| **运行时检查** | ★ **不加**（无 `assert`、无钳制、无 fallback） |
| **理由** | ① 唯一的生产调用点是 `Window.cpp:136`，来源是 `GetDpiScale()`（恒返回正有限值，未就绪时 `1.0f`）；② 沿 ECDI「**不为理论上可能的错误调用加防御**」的克制路线；③ `Renderer` 虽在 `include/ECDI/` 下（公共头），但**应用层从不直接接触它**（`Window` 私有持有 `m_renderer`）——与 `WM_NCHITTEST` 那类"公共 API 直收 int"的情形**不同** |

★ **本稿同时明确"不做"的替代方案**（记录理由，防后人"顺手加"）：`assert(std::isfinite(scale))` **会引入新的行为契约**（Debug 下终止），而 `FRAMEWORK_ASSERT` 的终止语义在本项目里是**给"不变量被破坏"用的**，`scale` 的合法性属**调用方前置条件**，不是框架不变量 ⇒ **不适用**。

### 3.4 接口注释的最终文本（★ 逐字，落地时照抄）

```cpp
		/// @param scale 坐标缩放因子（**finite + positive**；★ 语义 = **本次 Execute 调用**的
		///        上下文参数，**不是** `Renderer` 的状态、也不要求 `BeginFrame` 先被调用）。
		///        默认 `1.0f` = 恒等（既有调用点零改动 + 100% DPI 下逐位零回归）。
		///        ⚠️ 本类**只做** `geometry * scale`，**不认识 DPI**——1.25 是 120 DPI 还是别的
		///        变换，本类不关心、也无法知道（平台无关性的落点）。
```

---

## 4. ★ 「100% 零回归」的准确论证（★ 评审指定 ③ —— **冻结，不得回退成初设 v1.0 的写法**）

### 4.1 数学部分（成立，且是强保证）

`scale == 1.0f` 时 `v * 1.0f == v` 在 IEEE-754 下**精确成立**（bit-exact：乘 1.0 不引入舍入、无 FMA 参与）⇒ **三个 helper 的输出与原字段逐位相同**。

### 4.2 不得外推的部分（★ 评审 §9 的批评点）

**不得**由"单值/字段的数学恒等"推出「**整个软件行为必然恒等**」。`Renderer` 的路径上除了乘法还有：**helper 调用 → 构造临时 `Rect` / `Point` → 参数传递 → 后端**——这些环节的正确性是**工程事实**，不是数学推论。

### 4.3 本稿的三层表述（★ 冻结；初设 v1.1 §2.5 同款）

| 层 | 结论 | 强度 |
|---|---|---|
| **单值** | `v * 1.0f == v`（bit-exact） | ★ **数学恒等（强）** |
| **几何字段** | 三个 helper 的输出与原字段**逐位同值** | ★ **数学恒等（强）** |
| **整条链** | 既有 **247** 个用例**应**保持通过 · 目视行为**应**不变 | ⚠️ **工程判断**——**须由四工具链实测确认**（§9-A2） |

★ **仍不需要 `if (scale == 1.0f)` 特判**：乘法本身即恒等，加分支只会引入**未被测试覆盖的路径**——**这一条不受本次收紧影响**。

---

## 5. 契约 C-7–C-11 的验证映射

| 契约 | 内容 | 验证方式 |
|---|---|---|
| **C-7** | 输入几何恒 **DIP**、输出给 Backend 恒 **物理** | 代码结构（`Execute` 之后的几何必过 helper）+ **T20.1-2 / T20.1-3** |
| **C-8** | 折算**只做乘法、不取整 / 不提前量化** | ★ **grep**：`Renderer.cpp` 的 helper 内**无** `lround` / 无 `static_cast<int>`；★ **T20.1-3** 用**半整数**期望值——**若实现偷偷取整，该用例必失败** |
| **C-9** | `scale` **必须带默认值 `1.0f`** | 声明处**有**、定义处**无**（**盯防 ②**）；**T20.1-1** 用默认实参调用 |
| **C-10** | **折算不修改 `CommandBuffer`** | ① 类型系统（`const CommandBuffer&` + `const auto&`）② **T20.1-5** 断言源缓冲**逐位不变** |
| **C-11** | **唯一的折算实现点** = `ScaleRect` / `ScalePoint` / `ScaleLength` | ★ **grep 判据**（见下） |

**★ A4 / C-11 的 grep 判据式样**（★ 必须区分**代码行与注释**；★★ **v1.2 按批一实测修正**——原稿把「处」误写成「行」，且未提示「排除注释行 / 排除 helper 定义行」）：

```text
① helper 定义各 1 处（合计 3）：
   grep -nE '^\t\t(Rect|Point|float) Scale(Rect|Point|Length)\(' src/Render/Renderer.cpp   -> 3

② 乘法的唯一落点：形如 <expr> * scale 的**代码行**只应出现在三个 helper 内。
   ★ 实测口径须区分「处」与「行」：**7 处，但只落在 6 行**
     （ScaleRect 4 + ScalePoint 2【同一行 point.x * scale, point.y * scale】+ ScaleLength 1）
     - grep -c  '\* scale'         -> 6（行数）
     - grep -o '\* scale' | wc -l  -> 7（处数）  ★ 本稿原写「7 行」有误

③ 折算调用点（★ 须**排除 helper 定义行**——定义签名里也含同名标识符）：
   ScaleRect **5** · ScalePoint **3** · ScaleLength **3**（合计 11）
   ★ ScaleLength 必须**恰好 3**——多一处即说明有人误用了它（§1.3 细化 D）

④ 反向排除（★ 实测踩到）：判据**必须剔除注释行**——
   本文件注释里**故意**出现 `[[maybe_unused]]` 与 `(void)scale`（作为「**不做**」的说明，盯防 ⑥），
   若不过滤注释，这两条「应 0」的判据会**假报**。

⑤ 越界检查：src/Render/ 之外**不得**出现 ScaleRect / ScalePoint / ScaleLength 标识符
```

---

## 6. 测试实现规格（T20.1-1..T20.1-5）

### 6.1 承载方式

| 项 | 值 |
|---|---|
| **文件** | **扩展既有** `src/Tests/RendererTests.cpp` ⇒ ★ **`RunAllTests.h` / `RunAllTests.cpp` 零改动** |
| **装置** | 既有 `RecordingBackend`（**B7**：它记录的**正是后端收到的几何**）+ 既有 `kEpsilon`（**B15**） |
| **include** | ★ **零改动**（B15 已备齐 `Renderer.h` / `RecordingBackend.h` / `Point.h` / `Color.h` / `Font.h` / `Image.h`） |
| **无头性** | ★ **零窗口、零 GDI**（不碰 `GDIBackend` / `Win32RenderContext`） |

### 6.2 用例清单（★ 期望值已用脚本按 float32 语义复算）

#### **T20.1-1 `Renderer.ScaleIdentity`** —— G2 / C-2 / C-9 的**纯函数锚**

| 步 | 输入 | 断言 |
|---|---|---|
| ① | `Execute(cmds)`（**走默认实参**） | `draws[0].rect` = `{1, 2, 3, 4}`（与输入**同值**） |
| ② | 同一 `cmds` 再 `Execute(cmds, 1.0f)`（**显式**） | 与 ① **逐位相同**；`draws.size() == 2`（两次执行两次记录） |

★ ①用默认实参、②用显式实参 ⇒ **同时验 C-9（默认值存在）与 C-2（`dpi == 96` 恒等）**。

#### **T20.1-2 `Renderer.ScaleRectAndClip`** —— A1 / A4

输入（`scale = 2.0f`）：`DrawRectCommand{ Rect{1, 2, 3, 4} }` · `PushClipCommand{ Rect{5, 6, 7, 8} }`

| 期望 | 值 |
|---|---|
| `draws[0].rect` | `{2, 4, 6, 8}` |
| `clipOps[0].rect` | `{10, 12, 14, 16}`（★ **与 fill 用同一 helper** 的证据） |
| `clipOps[0].isPush` | `true` |

#### **T20.1-3 `Renderer.ScaleAllGeometryFields`** —— ★★ **A7（含 `width` / `cornerRadius`）+ 评审 §14 的 image 断言**

输入（★ **`scale = 1.5f`**，输入**刻意取奇数**，从而让期望值落在**半整数**上——见 §1.3 修正 A）：

| 命令 | 输入 | 期望 |
|---|---|---|
| `DrawLineCommand` | `start{1, 2}` · `end{3, 4}` · `width = 2.0f` | `start{1.5, 3}` · `end{4.5, 6}` · **`width = 3.0f`** |
| `DrawRoundedRectCommand` | `rect{2, 4, 6, 8}` · `cornerRadius = 4.0f` | `rect{3, 6, 9, 12}` · **`cornerRadius = 6.0f`** |
| `DrawImageCommand` | `dest{8, 16, 32, 48}` + `Image{2×1, stride 8}` | `dest{12, 24, 48, 72}` · ★★ **`image` 内容不变**（见下） |
| `DrawFocusRectCommand` | `rect{1, 1, 2, 2}` · `cornerRadius = 3.0f` | `rect{1.5, 1.5, 3, 3}` · `cornerRadius = 4.5f` |

★★ **image 断言（评审 §14 建议 —— 本稿落实）**：`Image` 用**既有 `TestImageValueSemantic` 同款的非对称像素**（如 `{0,0,255,255, 0,255,0,255}`），断言后端收到的 `imageCalls[0].image` 的 `width` / `height` / `stride` / **`pixels` 逐字节**与输入一致。
⇒ ★★ **本断言能证明什么、不能证明什么（★ v1.1 按评审 §10 修正）**：

| | 结论 | 理由 |
|---|---|---|
| ✅ **能证明** | **`image` 的内容 / 尺寸 / `stride` 未被 DPI 折算改动** | 即「**几何折算了，而 payload 一动不动**」——这正是本子阶段要钉住的边界 |
| ❌ **不能证明** | 「**没有发生复制**」 | `Image` 是**值类型** ⇒ 即便有人写 `auto image = cmd.image;`，**复制后**的 `width` / `height` / `stride` / `pixels` **与此完全相同** ⇒ 本断言**照样通过** |

⇒ **目的句（准确版，替换 v1.0 的写法）**：**保护 `image` 的内容 / 尺寸 / `stride` 不发生任何 DPI 转换或修改；`Renderer` 不对 `image` payload 做任何处理。**
★ **若将来要建立 zero-copy（真正的「不复制」）契约**，须**另立一个可观察的验证机制**（**内容比较观察不到复制**）——不在本子阶段范围。

★ **为什么期望值故意是半整数**：`{1.5, 3}` / `{4.5, 6}` / `{4.5f}` 这类值**能被 `lround` 改变**（`lround(1.5) = 2`）⇒ **本用例同时是 C-8（不得取整）的回归锚**。若把输入全取偶数（`2 → 3.0`），期望值全是整数，**这个用例就抓不到"偷偷取整"**。

#### **T20.1-4 `Renderer.ScaleTextPositionOnly`** —— ★★ **Q5 / G3（最危险的回归点）**

输入（`scale = 2.0f`）：`DrawTextCommand{ pos{5, 7}, text = "AB", font.size = 14.0f }`

| 期望 | 值 |
|---|---|
| `textDraws[0].pos` | `{10, 14}` |
| ★★ **`textDraws[0].font.size`** | **`14.0f`（未变）** |
| `textDraws[0].text` | `"AB"`（未变） |

★ 这条是**评审 §8 点名的"核心回归测试之一"**：它钉住「**几何 → Renderer 折** · **字号 → 后端折**」的**责任边界**。

#### **T20.1-5 `Renderer.ScaleLeavesBufferIntact`** —— ★★ **A2 / A6 / C-10 的**唯一**证据**

输入（`scale = 2.0f`）：缓冲按序含 `PushClip{Rect{0,0,100,100}}` → `DrawRect{Rect{10,20,30,40}}` → `PopClip`

| # | 断言 | 意义 |
|---|---|---|
| ① | 命令**数量 = 3**、**类型序列不变**（`PushClip` → `DrawRect` → `PopClip`）、**顺序不变** | **A6**（C-5：只改数值，不改顺序 / 类型 / 数量） |
| ② | ★★ **`commands` 内的几何逐位不变**：`std::get<PushClipCommand>(commands[0]).rect` 仍 = `{0,0,100,100}`；`std::get<DrawRectCommand>(commands[1]).rect` 仍 = `{10,20,30,40}` | ★★ **A2 / C-10**——这是"折算**不修改** `CommandBuffer`"的**唯一机器证据** |
| ③ | 同时后端收到的**已是物理**：`draws[0].rect` = `{20,40,60,80}` | 与 ② 并列 ⇒ **证明"折算了、但没改缓冲"** |

★ ② 与 ③ **必须同时断言**：只有 ② 无法区分"折算没发生"，只有 ③ 无法区分"折算发生了但没有污染缓冲"。

### 6.3 用例数口径（★ 防 Phase 19 的"把 T 编号当用例数"重演）

| 项 | 值 |
|---|---|
| **新增注册条目** | **5**（T20.1-1..T20.1-5，**一个场景 = 一条注册**） |
| **用例数** | **247 → 252** |
| **承载文件** | `src/Tests/RendererTests.cpp`（**既有**）⇒ **`RunAllTests.h/.cpp` 零改动** |

---

## 7. 实现顺序与检查点（★ 先让能力可验证，再让行为生效）

| 批 | 改动 | 检查点 |
|---|---|---|
| **批一** | `Renderer.h`（△1）+ `Renderer.cpp`（△2） | ★ **既有 247 全绿**——`scale` 默认 `1.0f` ⇒ **零破坏可观测**（此时尚无人调用新形参） |
| **批二** | `RendererTests.cpp`（△4，5 用例 + 5 注册） | ★ **252 全绿**——能力**已被验证** |
| **批三** | `Window.cpp`（△3，注入 `GetDpiScale()`） | ★ **四工具链** + **目视**：**100% 下与改前一致** & **125% / 150% 下内容充满窗口**（R1 修复生效） |

★ **批一先做、批二紧跟、批三最后**——批三一旦出问题，能立刻判定问题在**注入点**（`Window.cpp`）而非**折算本身**（批二已证）。

---

## 8. 影响面（汇总）

| 面 | 值 |
|---|---|
| **代码文件** | **4**（`Renderer.h` · `Renderer.cpp` · `Window.cpp` · `RendererTests.cpp`） |
| **文档文件** | **1**（需求稿 v1.2 勘误，已在初设轮完成） |
| **公共头** | **92 → 92**（`Renderer.h` 已有；★ **无新增头**） |
| **公共 API** | ★ **0 新增 / 1 处已有接口的形参扩展**（`Execute` 加 `float scale`）。★ **源码兼容：保持**（3 个既有调用点零改动——**B2/B3**）；★★ **ABI / 二进制兼容：不承诺**（★ v1.1 按评审 §9 修正，逐维口径见 **§8.1**） |
| **新增用例** | **5**（**247 → 252**），全部**无头** |
| **新增 `#include`** | **0**（**B13 / B15**） |
| **`RunAllTests.*` / `CMakeLists.txt` / `main.cpp`** | **零改动** |
| **后端（GDI / Recording）** | **零改动**——它们**看到的单位变了**（DIP → 物理），但接口与实现都不动（U4） |
| **`main.cpp`** | ★ 即便需要也**须单独授权**（本阶段**不需要**） |

### 8.1 ★★ 兼容性口径（★ v1.1 新增，防误读——评审 §9）

| 维度 | 结论 | 依据 |
|---|---|---|
| **源码兼容** | ✅ **保持** | 默认实参 ⇒ 原调用 `Execute(commands)` **继续编译**（编译器在调用点补 `1.0f`） |
| **ABI / 二进制兼容** | ❌ **不承诺** | 函数签名由 `(const CommandBuffer&)` 变为 `(const CommandBuffer&, float)` ⇒ **符号变了**。★ **默认参数不参与符号、也不生成重载** ⇒ 「**旧头文件 + 新库**」/「**旧目标文件 + 新库**」**不因默认值而兼容** |
| **与本项目版本策略的关系** | 一致 | ECDI 处于 **0.x** 阶段（**无 ABI 稳定性承诺**）⇒ 本改动**不构成额外约束**。★ 另注：本项目为**静态库**形态 ⇒ **使用方恒须与新库一起重编译**（这是一贯的构建口径，非本子阶段引入） |

---

## 9. 验收（A1–A7 的落地）

| 需求 | 落地方式 | 责任人 |
|---|---|---|
| **A1**（150% 内容充满窗口） | **人工目视**（主屏 125% / 跨屏 150%）——★ 与 Phase 20 的 A5/A6 **合并做一次** | 用户 |
| **A2**（100% 逐位零回归） | **既有 247 全绿** + **T20.1-1** + **§4.3 三层表述**（整链层**须实测**）+ **四工具链** | 用户 |
| **A3**（缩放/移动/最大化同源同尺度） | 人工目视（命中侧 Phase 20 已折，本稿只补**绘制侧**） | 用户 |
| **A4**（落点唯一可 grep） | **§5 的三条判据式样** | AI（静态）+ 用户复核 |
| **A5**（四工具链 + 测试全绿） | ★ **报绿须写明断言是否启用**（`_DEBUG` 口径） | 用户 |
| **A6**（顺序/类型/数量不变） | **T20.1-5 ①** | AI（静态） |
| **A7**（覆盖每种几何字段） | **T20.1-3**（`Rect` / `Point` / `width` / `cornerRadius` + `image` 不变） | AI（静态） |

---

## 10. 文档收口清单（实现后执行）

| # | 动作 |
|---|---|
| **1** | ★ **Phase 20 初设 §2.1 规则 3** 补「**第二条换算边**」——补记「框架内部(DIP) → 渲染层(物理)」这条边落在 `Renderer::Execute`（需求稿 §8.2 的明确要求） |
| **2** | ★ **Phase 20 详设 §14 的 R1 标 ✅**，并指向本子阶段 |
| **3** | ★ **`roadmap-deferred.md`**：Phase 20.1 收口；★ **新增记账项** = **后端几何取整口径不统一**（**O1**，`DrawRect` 截断 vs `PushClip`/`DrawLine` 用 `lround`，既有 ≤1px 量级近似） |
| **4** | 两个 README 的**规模锚点**（用例 **247 → 252**；`docs/` 计数） |
| **5** | **`.workbuddy/memory/`**：Phase 20.1 细节下沉 `archive/`（`MEMORY.md` 只留一行） |

---

## 11. 修订记录

- **v1.2**（2026-09-24）**批一（`Renderer.{h,cpp}`）实施期的就地修正 —— §5 的 A4 / C-11 grep 判据口径**。
  ★ 批一落地后**按 §5 判据自检**，发现**判据本身有两处不可用**（★ **不是实现偏离，是判据偏离实测**）：
  ① ★ **「处」与「行」被混为一谈**：原写「`* scale` 合计 **7 行**」，实测是「**7 处、落在 6 行**」
     （`ScalePoint` 那行含两次：`point.x * scale, point.y * scale`）⇒ 按 `grep -c`（行数）得 **6**、
     按 `grep -o` 配 `wc -l`（**处数**）才得 **7**。判据已按两种口径分别写明。
  ② ★ **未提示两类必须排除的行**：(a) **注释行**——本文件注释里**故意**出现 `[[maybe_unused]]` 与 `(void)scale`
     （作为「**不做**」的说明，盯防 ⑥），不过滤会让两条「应 0」的判据**假报**；
     (b) **helper 定义行**——定义签名（`Rect ScaleRect(`）里也含同名标识符，会与「调用点计数」混淆。
  ③ ★ 另补 **③ 折算调用点的实测值**（ScaleRect **5** · ScalePoint **3** · ScaleLength **3**，合计 11）与 **④ 越界检查**。
  ④ ★ **本修正只动 §5 的判据文本，不改任何设计决定**（△1–△4 · C-7–C-11 · 三批顺序**全部不变**）。
  ⑤ 头部 v1.1 → **v1.2**。
- **v1.1**（2026-09-24）**本稿第一轮评审处置 —— ★ 两处表述修正 + 一处交叉引用修正**。
  ① **评审结论**：「**通过，可以进入实现**」，**无阻塞性架构问题**；逐条处置见 **§1.6**。
  ② ★★ **ABI 兼容性口径修正（评审 §9，本稿最该改的一处）**：默认实参**不是重载 / 也不是运行时包装**，只是**调用点补 `1.0f`** ⇒ 须明确 **源码兼容保持 · ABI 不承诺**。⇒ **§8 公共 API 行改写** + **新增 §8.1 兼容性口径表**（含「与本项目 0.x 版本策略一致」「静态库形态下使用方恒须重编译」）。
  ③ ★ **Image 断言的因果修正（评审 §10）**：`Image` 是**值类型** ⇒ 「断言内容一致 ⇒ 没有复制」**推不出来**。⇒ **§6.2-T20.1-3 目的句改写**为「**保护内容 / 尺寸 / `stride` 不被 DPI 转换或修改**」，并**显式标注能证明 / 不能证明**；「zero-copy」若要成立须**另立可观察机制**。
  ④ ★ **本稿自查的交叉引用修正**：§1.5 表原写「本稿 **§8-O1**」，而 **O1 定义在初设 §6**（本稿 §8 是「影响面」）⇒ 改为指向「本稿 §10 收口清单 3」。
  ⑤ **评审确认保留的 12 项**（未改动）：`scale` 语义链 · 三个 helper · 8 重载映射表 · `DrawText` 只折 `pos` · `PushClip` 同 helper · T20.1-3 半整数技巧 · 修正 A 的方向 · **§4 三层论证「已可冻结」** · 不加 `assert` · `PopClip` 不写 `[[maybe_unused]]` · **三批顺序** · **AI / 用户的验收分工**。
  ⑥ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-09-24）初稿。**输入**：初设 **v1.1**（评审通过：3 项「详设必冻结」+ 2 项文档修正 + 3 项建议）· 需求稿 v1.2 · B2/B3/B7/B9/B11 沿用初设。**内容**：**B12–B15 详设新增基线**（★ B13「**零 include 成本**」· B15「零容差新定义」）· ★ **§1.3 对初设的一处修正 + 三处细化**（★ **修正 A：实测证伪「`1.5f` 不可精确表示」**——`1.5 = 3/2` 与半整数皆精确；真正的非精确在 `1.2f` / `1.1f`；★ 并由此得出**半整数期望值才能抓"取整"**的测试设计）· △1–△4 逐文件行级改动（含完整声明 / 定义定稿与逐重载折算表）· ★ **§2.5 实现盯防清单 6 条** · ★ **§3 `scale` 语义 + 合法范围冻结（评审 ⑥/⑦）**，含**接口注释逐字文本**与**"为何不加 `assert`"** 的记录 · ★ **§4「100% 零回归」三层表述（评审 ③，冻结）** · §5 契约 C-7–C-11 验证映射 + **A4 的三条 grep 判据式样** · **§6 五个用例的完整输入 / 期望（脚本按 float32 复算）** · §7 三批实现顺序 · §8 影响面 · §9 验收 A1–A7 落地 · §10 收口清单。**待评审。**
