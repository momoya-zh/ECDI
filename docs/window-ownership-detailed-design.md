# Window 所有权与生命周期契约 —— 详细设计 v1.2

> 阶段：详细设计（五阶段法 ③）
> 前置：[`window-ownership.md`](window-ownership.md) **初设 v1.1**（外部评审「通过，可进入详细设计」）
> 状态：**✅ 已实施并验收（2026-09-12）**——`ecdi_tests` **183 passed / 0 failed / 183 total**；**A1–A4 + A6 全部通过**（含**四工具链** MinGW / MSVC / Clang / ClangCL 实测运行、无断言错误）；A1 编译期契约与 A4 静态检查已实证
> 定位：初设 v1.1 已完成契约论证与决策（三层归属 / 三条链 / 5 条不变量 / B1–B5），**本文档不含架构讨论**——只做「编译级契约 + 逐文件精确改动 + 验收执行步骤」。
> 一句话：把 B1–B5 落成**可直接照抄的 diff 级规格**，并给出比初设更强的两处实证（编译期契约测试 / `make_unique` 陷阱）。

---

## 1. 范围与决策引用

**本文档实施初设 §2.3 的 5 条不变量**，全部决策取自初设 §3，不再重新论证：

| 编号 | 决策（初设 §3 定稿） | 本文落点 |
|---|---|---|
| B1 | 构造器 `public` → `private` + `friend class Application` | §2.1 |
| B1-t | `Create` 内改 `std::unique_ptr<Window>(new Window(*this, …))` | §2.4 |
| B2 | 形参 / 成员 `Application*` → `Application&` | §2.1 / §2.2 |
| B3 | 断言与容错并存（b3a）+ **把设计意图写进注释** | §2.4 |
| B4 | 删除 `Application::friend class Window;` | §2.3 |
| B5 | **b5b**：`~Window()` 保持 `public` + 契约条款 | §2.1 / §4 |
| — | `PlatformWindowHost` 旧注释**改写** | §2.5 |

**范围外**（初设 §1.4 / §7 已定）：Detached 窗口、`m_running` / `Run`-`Exit` 语义、`~Application` 残留窗口隐患、自定义 deleter。

---

## 2. 精确改动（逐文件）

> 改动总计 **5 个生产文件 ≈ 35 行**（含注释）+ **1 个测试文件 ≈ 12 行**。

### 2.1 `ECDI/include/ECDI/Window/Window.h`

**改动 1｜类注释：追加所有权契约段**（接在现有「禁止拷贝和移动」之后）

```cpp
/// 禁止拷贝和移动（Widget 树节点地址稳定 + PlatformWindow 生命周期绑定）。
///
/// ── 所有权与生命周期契约（`docs/window-ownership.md` 初设 v1.1 §4.1 / §4.2）────────
/// Window 对象由 Application 持有并负责生命周期管理；**调用者不得对 Window\* 执行 delete**。
/// Window::Release() 仅负责释放**平台窗口资源**，不负责销毁 Window 对象。
/// 创建：Application::Create()（**当前唯一实际构造入口**——本类构造器为 private，
/// 构造权限经 `friend class Application` 授予 Application）。
```

**改动 2｜访问权限布局：构造器从 `public` 移入 `private` + 加 `friend`**

修改前（现状）

```cpp
class Window : public PlatformWindowHost {
	public:

		/// @param app         所属 Application
		/// @param title       窗口标题
		/// @param width       窗口总宽度（含边框和标题栏）
		/// @param height      窗口总高度（含边框和标题栏）
		/// @param services    渲染服务（7.1.4：默认 GDIBackend+GDITextMeasurer——平台默认工厂；
		///                   测试/未来可注入其他后端）
		/// 7.1.5：窗口类参数移除（窗口系统资源归平台层 WindowClass::Instance()——Window 不再认识平台对象）
		Window(Application* app,const std::string&title,int width,int height,
		       RenderServices services = CreateDefaultRenderServices());

		// 禁止拷贝（保留 public——deleted 函数在 public 区诊断更清晰）
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		// 禁止移动
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		~Window()noexcept;
```

修改后

```cpp
class Window : public PlatformWindowHost {
	public:

		// 禁止拷贝 / 禁止移动（留在 public —— deleted 函数在 public 区诊断信息更清晰）
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		/// @brief 析构（**保持 public** —— B5b：对象所有权由 Application 承担，
		/// 用契约而非访问控制禁止外部 delete；私有化会使 unique_ptr 的 default_delete 编译失败）
		~Window()noexcept;

		// …（其余 public API 不变）…

	private:

		// ── 构造权限（B1：所有权契约的编译期基础）────────────────────────────
		/// @param app      所属 Application（B2：**引用**——「无主窗口」在语法上不存在）
		/// @param title    窗口标题
		/// @param width    窗口总宽度（含边框和标题栏）
		/// @param height   窗口总高度（含边框和标题栏）
		/// @param services 渲染服务（默认 GDIBackend+GDITextMeasurer；测试/未来可注入其他后端）
		/// @pre **框架外部不可直接构造**——构造权限授予 Application（`friend class Application`）；
		///      `Application::Create()` 是**当前唯一实际构造入口**（构造即登记，两者不可分离）。
		/// @details 构造权限的粒度说明：`friend class Application` 授予的是**整个 Application 类**，
		///          不是单个成员函数——因此准确表述是「框架外无法构造 + Application 拥有构造权限」，
		///          而非「只有 Create() 能构造」。
		Window(Application& app,const std::string&title,int width,int height,
		       RenderServices services = CreateDefaultRenderServices());

		friend class Application;   ///< 构造权限（B1）

		// …（原 private 区其余内容不变）…

		Application& m_application;	///< 所属 Application（B2：引用；构造器初始化列表注入）
```

⚠️ **注意三处连带影响**：

| 项 | 说明 |
|---|---|
| `m_application` 类型 | `Application* … = nullptr` → `Application&`。**引用成员必须由构造器初始化列表初始化**（现有代码已如此：`Window.cpp:43 : m_application(app)`），**不可**保留默认成员初始化器 |
| `Window.h` 前置声明 | `class Application;`（现有 `Window.h:24`）足以声明引用成员——**无需**改成 include |
| `~Window` 注释中的 `\*` | 类注释里写 `Window\*` 是为避免被 Doxygen 解析成粗体起始符，与现有注释风格一致（若嫌别扭可写 `` `Window*` ``） |

### 2.2 `ECDI/src/Window/Window.cpp`

**唯一改动**（引用化后的调用点，全库仅此 1 处）：

| 位置 | 修改前 | 修改后 |
|---|---|---|
| `Window.cpp:413`（`Window::OnEvent`） | `m_application->OnEvent(event);` | `m_application.OnEvent(event);` |
| `Window.cpp:43`（构造初始化列表） | `: m_application(app)` | **不变**（引用成员同语法） |

### 2.3 `ECDI/include/ECDI/Application/Application.h`（B4）

删除 `Application.h:42`：

```cpp
	friend class Window;
```

**删除依据**：`Window` 对 `Application` 的唯一访问是 `m_application.OnEvent(event)`，而 `EventRouter::OnEvent` 本就是 `public`（`EventRouter.h:33`）⇒ 该友元**当前无用途**。

⚠️ **同批复核结论（B4 附带的"双向访问关系重查"）**：

| 声明 | 位置 | 处置 |
|---|---|---|
| `friend class Window;` | `Application.h:42` | **删除**（本次） |
| `friend class Window;` | `Widget.h:237` | **不动**——`SetWindow` 私有化的配套授权，用途明确 |
| `friend class Window;` | `TextBox.h:23` | **不动**——Window 作为框架协调者访问 protected 状态机 |
| `friend class Application;` | `Window.h`（本次新增） | B1 的构造权限 |

**方向性小结**：`Application → Window` 的友元（构造权限）**新增**；`Window → Application` 的友元（反向访问）**删除**——二者不是对称关系，`Window` 不再需要任何反向授权。

### 2.4 `ECDI/src/Application/Application.cpp`

**改动 1｜`Create()` 中的 **Window 构造语句**（**最小修改面**——其余行保持现状）

修改前（`Application.cpp:45-50`）

```cpp
	m_windows.emplace_back(std::make_unique<Window>(
		this,
		title,
		width,
		height
	));
```

修改后

```cpp
	// ⚠️ B1-t：必须写作 unique_ptr(new Window(...))，**不可**用 std::make_unique<Window>(...)。
	//    原因：构造器为 private（B1），而 make_unique 的函数体**不是 Application 的成员**——
	//    friend 授权只在"访问发生处"生效，make_unique 内部会因无权访问而编译失败。
	//    此处 new 表达式就在本成员函数体内，friend 生效；所有权**立即**交给 unique_ptr（非裸指针）。
	m_windows.emplace_back(std::unique_ptr<Window>(new Window(*this, title, width, height)));
```

⚠️ **不要全文替换 `Create()`**：`Window& window = *m_windows.back();` / `WindowCreatedEvent event(&window);` /
`OnEvent(event);` / `return window;` **全部保持现状**——本次只动上面这一条语句。


**改动 2｜`OnWindowDestroyed` 的断言 + 容错注释（B3 / §4.4 契约文本）**

```cpp
void Application::OnWindowDestroyed(const WindowDestroyedEvent& event) {

	// 将已销毁的 Window 从活跃列表移到延迟销毁列表
	auto it = std::find_if(
		m_windows.begin(),
		m_windows.end(),
		[&](const auto& window) { return window.get() == event.GetWindow(); });

	// 容器成员关系是**内部不变量**（B1/B2 之后，框架外已无法产生未登记窗口）。
	// Debug 构建以断言暴露框架自身的缺陷；Release 构建保留下面的运行时分支，
	// 避免不变量一旦被违反时"无效迭代器解引用"进一步演化为未定义行为。
	FRAMEWORK_ASSERT(it != m_windows.end());

	if (it == m_windows.end()) {

		return;

	}

	m_deferredDestroy.emplace_back(std::move(*it));

	m_windows.erase(it);

	// 所有窗口都关闭了，退出消息循环
	if (m_windows.empty()) {

		Exit();

	}

}
```

> 说明：两段并存**不是**"两头下注"——职责分层（断言 = 暴露不变量违反；分支 = 阻止 UB 扩散），且**理由已写在代码里**（初设 §4.4 要求）。

### 2.5 `ECDI/include/ECDI/Platform/PlatformWindowHost.h`

**注释改写**（`PlatformWindowHost.h:15`，**改写而非删除**——保留 7.1 设计意图的演进痕迹）

修改前

```cpp
/// 接口收敛说明（YAGNI，2026-08-15）：
/// - 无 OnDestroyed：WM_DESTROY 后 Win32PlatformWindow 内部置空句柄，框架层无需动作
```

修改后

```cpp
/// 接口收敛说明（YAGNI，2026-08-15；2026-09-12 按 window-ownership.md §4.5 改写）：
/// - 无 OnDestroyed：**PlatformWindowHost 不负责 Window 对象的生命周期**。
///   Win32 平台层在 WM_DESTROY 后清空 HWND 并置空句柄，同时经既有事件通道上报
///   WindowDestroyedEvent；Window 对象的延迟销毁与注册表维护由 Application 负责。
///   （原文"框架层无需动作"写于 7.1——当时 Application 侧尚无窗口注册表语义，已过期。）
```

### 2.6 `ECDI/src/Tests/WindowChromeTests.cpp`

**改动 1｜新增编译期契约测试**（`#include <type_traits>` 一并补上）

```cpp
// ── 编译期契约（window-ownership.md §6.1）：Window 不得在框架外构造 ──────────
// 依据：std::is_constructible 在**中立上下文**求值 ⇒ 构造器为 private 时结果为 false。
// 实证（详设 §3）：MinGW g++ 16.1 与 clang++（LLVM）均返回 false。
// ⚠️ 该 trait 在中立上下文求值 —— 写在 Application 内部同样返回 false，
//    因此它**只能做负向断言**；友元权限的正向证据 = Application::Create 里
//    `new Window(*this, …)` 能编译（天然成立）。
static_assert(
	!std::is_constructible_v<Window, Application&, std::string, int, int>,
	"Window 不得在框架外构造——请使用 Application::Create()");

// 补充：拷贝/移动已 deleted，一并有编译期回归
static_assert(!std::is_copy_constructible_v<Window>, "Window 禁止拷贝");
static_assert(!std::is_move_constructible_v<Window>, "Window 禁止移动");
```

**改动 2｜`TestWindow::window` 显式标注 non-owning（§6.4 文档纪律）**

```cpp
	/// ⚠️ **非拥有**指针（non-owning reference）——本对象**只是观察者 / 触发器**，
	/// 不承担 Window 对象的所有权：所有权始终归 Application（`unique_ptr` 唯一持有）。
	/// ⇒ 绝不可改用 `unique_ptr<Window>` 持有（会造成双重所有权 → 二次析构）。
	/// 推荐消费者模型请见 `Application::Create` 的返回值用法：`Window& w = app.Create(...)`。
	Window* window = nullptr;
```

---

## 3. 编译期契约测试方案（§2.6 改动 1 的依据）

### 3.1 实证（已完成）

用等价探针（私有构造器 + `friend`）在**两个**工具链上验证 `std::is_constructible_v` 是否尊重访问控制：

| 工具链 | 编译 | 中性上下文求值 | **友元类内部求值** |
|---|---|---|---|
| MinGW g++ 16.1（`D:\Environment\CPP\mingw64`） | ✅ | `false` | **`false`** |
| clang++（LLVM，`D:\Environment\CPP\LLVM`） | ✅ | `false` | **`false`** |

### 3.2 结论

1. **trait 尊重访问控制** ⇒ 负向断言（`!is_constructible_v<…>`）**可用**；
2. **trait 在中立上下文求值** ⇒ 即便写在 `Application` 内也返回 `false` ⇒ **只能负向断言**，**不能**正向证明"友元可构造"；
3. 友元权限的正向证据 = `Application::Create` 内 `new Window(*this, …)` **能编译**（编译即证明，无需 trait）。

### 3.3 待四工具链确认 + 兜底

| 项 | 内容 |
|---|---|
| 待确认 | **MSVC `cl.exe` / ClangCL** 未实测（行为由标准规定，但按"先实证再定稿"纪律在验收 A2 确认） |
| 兜底（若某工具链 trait 不尊重访问控制） | 改用**负向编译探针**：一个 `EXCLUDE_FROM_ALL` 独立小目标，内含 `Window w(app, "t", 100, 100);`，**预期编译失败**；只手动跑（**不入** `ecdi_tests`，避免构建被破坏） |
| 人工一次性配套 | 在非 `Application` 上下文写 `Window w(app, "t", 100, 100);` 确认编译失败，结论以注释固化（`§2.6` 已含该注释） |

**为什么不引入更复杂的 trait 魔改**：本验收项的**真实目标是"框架外部不能构造 Window"**——该目标由**编译器对 private 构造器的访问控制**直接保证（不依赖任何 trait）；`static_assert` 只是**回归锚**（防止日后有人把构造器改回 `public`）。故不为"必须 static_assert"而增加模板复杂度。

---

## 4. 注释文本落点清单（契约原文 → 代码位置）

| 契约原文（初设 §4） | 落点 |
|---|---|
| §4.1 Window 对象所有权（"不得 delete"） | `Window.h` 类注释（§2.1 改动 1） |
| §4.2 `Release()` ≠ `delete Window` | `Window.h` 类注释 + `Window::Release()` 声明注释（补一句"仅释放平台资源，不销毁对象"） |
| §4.3 对象回收依赖消息泵 | `Application::ProcessDeferredDestroy()` 注释（首次表达该前提） |
| §4.4 断言与容错的双层含义 | `Application::OnWindowDestroyed`（§2.4 改动 2） |
| §4.5 `PlatformWindowHost` 注释改写 | `PlatformWindowHost.h`（§2.5） |

---

## 5. 实施顺序（6 步）

| 步 | 动作 | 验证 |
|---|---|---|
| 1 | `Window.h`：类注释 + 构造器私有化 + `friend` + `m_application` 改引用 | 编译**预期失败**（`Create` 仍用 `make_unique`）——先看到 B1-t 的失败，确认陷阱真实 |
| 2 | `Application.cpp`：改 `std::unique_ptr<Window>(new Window(*this, …))` | 编译通过 |
| 3 | `Window.cpp`：`m_application.OnEvent` | 编译通过 |
| 4 | `Application.h` 删 friend；`PlatformWindowHost.h` 注释改写；`Application.cpp` 断言注释；`ProcessDeferredDestroy` 注释 | 编译通过 |
| 5 | `WindowChromeTests.cpp`：`static_assert` ×3 + non-owning 标注 | 编译通过（`static_assert` 全部成立） |
| 6 | 全量测试 + 静态检查 | 见 §6 |

> **第 1 步的"预期失败"是刻意的**——它是 B1-t 陷阱的现场证据（不是流程噪声）。

---

## 6. 验收清单

| 编号 | 项 | 通过判据 |
|---|---|---|
| **A1** | **编译期契约**（B 的第一验收项） | 三个 `static_assert` 全部成立；另在非 `Application` 上下文人工写 `Window w(app, "t", 100, 100);` → **编译失败** |
| **A2** | 四工具链构建 | MSVC / Clang / ClangCL / MinGW 全部通过（并确认各工具链下 `is_constructible_v` 行为与 §3.1 一致） |
| **A3** | 运行期（带断言） | MinGW + `-DCMAKE_CXX_FLAGS=-D_DEBUG` → `ecdi_tests` 全绿（当前 **183**） |
| **A4** | 静态检查 | `grep -rn "new Window"` → **恰好 1 处**（`Application.cpp`）；`grep -rn "make_unique<Window>"` → **0 处** |
| ~~A5~~ | **已裁决：不采纳** | `Release()` 幂等**已有源码证据**（`Win32PlatformWindow.cpp:111` 早退 + `:337` 置空）且**非 B 新增行为** ⇒ 不为 183 → 184 的数字扩张 B 范围。若需回归，另作 Phase 12 行为回归项 |
| **A6** | 用户侧验证 | **MSVC Debug 运行 `ecdi_tests`** → 不再触发 `Application.cpp:92` 断言 |

---

## 7. 风险与回退

| # | 风险 | 处置 |
|---|---|---|
| R1 | 某工具链 `is_constructible_v` 不尊重访问控制 ⇒ `static_assert` 编译失败 | 兜底见 §3.3（负向编译探针）；`static_assert` 是回归锚，**不是**主保证——主保证是编译器的访问控制本身 |
| R2 | 引用成员化遗漏某处 `m_application->` | 全库 `grep -rn "m_application"` 仅 `Window.cpp:43/413` 两处（改动前已核实） |
| R3 | 私有构造器影响未来派生 | 全库 `: public Window` **0 命中**（已核实）；若未来需要派生，届时一并设计（记录于本文档，不阻塞） |
| R4 | `make_unique` 写法被"顺手改回" | §2.4 注释写明原因 + A4 静态检查（`make_unique<Window>` 必须 0 处） |
| **回退** | 任一步失败 | 改动集中在 5 个生产文件、无 API 交互耦合 ⇒ 逐文件 `git checkout` 即可回退；**无数据/协议迁移** |
| **附带发现（未改）** | `ECDI/ECDI开发规范.md:143` 写「…并丢失 `m_application` 指针」——B2 后 `m_application` 已是**引用**，措辞过期。**不在本次授权范围**（6 文件之外），待用户裁决 |

---

## 8. 修订记录

- v1.2（2026-09-12）**实施落地（3 处实测偏差 + 验收结果）——✅ 已实施**：
  - **实施偏差 ①（规格未指明落点，已修正）**：§2.6 的 `static_assert` 未写明**放置位置** —— 首次被插在 `#include` 块中间，而 `Window` / `std::is_constructible_v` 当时尚未可见（`using namespace ECDI;` 在其后）。**已移至 `using namespace ECDI;` 之后、`namespace {` 之前的顶层作用域**。⇒ 规格应写明落点（本版补记）。
  - **实施偏差 ②（连带过期注释）**：`WindowChromeTests.cpp` 的 `TestWindow` 构造体内原写「直接构造 Window（构造器是 public）会绕过登记」——B1 后该表述**已失效**。已改为「直接构造 Window 会绕过登记」，并补注「B1 起『直接构造』已**不可编译**——构造器 private + `friend class Application`；下文保留因果链，作为这条约束的由来记录」。
  - **实施偏差 ③（排版等价，非语义）**：规格代码块中的注释用 ` —— `（带空格），实施按项目既有排版采用 `——`（无空格），语义完全一致。
  - **实施细节（排版 / 依赖直连，无设计变化）**：`Application.h` 删 `friend class Window;` 时连同其后空行一并删除；`Window.h` 的 `friend class Application;` 后补一空行（与该 `private` 区其它分组一致）；测试文件补 `#include <string>`（`static_assert` 直用 `std::string`，不依赖传递包含）。
  - **验收结果**：

    | 编号 | 结果 |
    |---|---|
    | **A1** 编译期契约 | ✅ `static_assert` ×3 全部成立（构建通过即证明）；**人工反例已实证** —— 非 `Application` 上下文 `ECDI::Window w(app, "t", 100, 100);` → `error: 'ECDI::Window::Window(ECDI::Application&, const std::string&, int, int, ECDI::RenderServices)' is private within this context`（MinGW g++ 16.1） |
| **A2** 四工具链 | ✅ **全部通过（2026-09-12，用户实测）**——MinGW / **MSVC** / **Clang** / **ClangCL** 均构建并运行 `ecdi_tests`，**无断言错误**（含 `_DEBUG` 断言层生效的构建） |
    | **A3** 运行期（带断言） | ✅ `ecdi_tests` **183 passed, 0 failed, 183 total**，exit 0 |
    | **A4** 静态检查 | ✅ **代码行**：`new Window` **1 处**（`Application.cpp:49`）、`make_unique<Window>` **0 处**。⚠️ **判据精确化**：另有 **3 处注释命中**（`Application.cpp:45` 两个 token、`WindowChromeTests.cpp:32` 一处）——它们**有意**记录被禁写法，属文档性内容 ⇒ A4 应写作「**代码行**中 1 处 / 0 处（排除注释）」 |
| **A6** 用户侧验证 | ✅ **已确认（2026-09-12，用户实测）**——MSVC Debug 运行 `ecdi_tests`：**正常通过、无断言报错**（`Application.cpp:92` 断言不再触发）。同时意味着 **MSVC 工具链编译通过**（A2 的 MSVC 项完成） |

  - **B1-t 陷阱的现场实证（真实仓库，非等价探针）**：把 `Application.cpp` 的构造语句临时换回 `std::make_unique<Window>(...)` 后重建 → **编译失败，且错误点位于标准库内部**：`.../bits/unique_ptr.h:1086:30: error: 'ECDI::Window::Window(ECDI::Application&, const std::string&, int, int, ECDI::RenderServices)' …`（随后对 `Application.cpp` 做**字节级还原**并重建通过）——**证明访问检查发生在 `make_unique` 函数体内**，与设计判断一致。
  - **实现日期取证**：本文档 §8 本条目（v1.2）+ `docs/README.md` 索引行（✅ 已实施）。
  - **未做**：改动未提交（停在工作区）；未动 `examples/` 与其它测试文件。
- v1.1（2026-09-12）**外部评审「通过，允许进入实施」——3 处文字级修正**（架构决策 / 影响面 / 代码落点 / 测试策略均未变）：
  - **① §2.1 `@pre` 收紧**：「**只能由 Application 构造**」与 B1 的措辞纪律冲突（`friend class Application` 授予**整个类**，非单个成员函数）⇒ 改为「**框架外部不可直接构造**——构造权限授予 Application；`Application::Create()` 是当前唯一实际构造入口」。
  - **② §2.4 改动 1 收窄**：原写「`Create` 全文」有被误改为"全文替换"的风险 ⇒ 改为「**`Create()` 中的 Window 构造语句**」并给出**前后对照**，明确其余行（`*m_windows.back()` / `WindowCreatedEvent` / `OnEvent` / `return`）**全部保持现状**——遵循**最小修改面**原则。
  - **③ A5 裁决 = 不采纳**：`Release()` 幂等已有源码证据（`Win32PlatformWindow.cpp:111` 早退 + `:337` 置空句柄）且**非 B 新增行为**；B 的核心验收是 5 条不变量，不为 `183 → 184` 的数字扩张范围。**最终验收 = A1–A4 + A6**。
  - **评审确认保留不动**：§2 逐文件落点、§3 编译期契约测试方案（含"设计阶段证明规则 / 实施阶段证明环境实际行为"的分层）、§4 注释落点清单、§5 实施 6 步（**第 1 步刻意暴露编译失败**获肯定——把 C++ 访问控制坑变成可观察的工程证据）、§6 其余验收项、§7 风险与回退。
  - **注**：§3.1 的双工具链实证表**保留**（证明"不能拿 `is_constructible` 证明 friend 权限"这一设计判断的来由），但**不再扩展实验**——已达"知道为什么这么设计"的程度。
- v1.0（2026-09-12）**详细设计初稿**：
  - **前置**：初设 `window-ownership.md` **v1.1** 外部评审「通过，可进入详细设计」；本版实施其 5 条不变量与 B1–B5 全部决策，**不含架构讨论**。
  - **精确到 diff**：`Window.h` 访问权限布局（前/后对照）+ `friend` 粒度说明；`Window.cpp` 唯一调用点；`Application.h` 删友元（附**双向访问关系复核表**）；`Application.cpp` 两处（`Create` 全文含 B1-t 陷阱说明 + `OnWindowDestroyed` 断言/容错注释）；`PlatformWindowHost.h` 注释改写；测试文件 2 处（`static_assert` ×3 + non-owning 标注）。
  - **两处本版新增实证**（比初设更强）：① `is_constructible_v` 在 **MinGW g++ 16.1 与 clang++** 下均确认**尊重访问控制**，且**在中立上下文求值**（友元类内部同样为 `false`）⇒ 只能负向断言；② 由此确定 `static_assert` 的**角色是回归锚而非主保证**，主保证是编译器访问控制本身 —— 并把"不为 trait 魔改"写成显式设计取舍。
  - **注释文本落点清单**（§4）：把初设 §4 的四条契约原文与 §4.5 注释改写逐一映射到具体文件，避免实施时遗漏。
  - **实施顺序**（§5）刻意在**第 1 步暴露编译失败**——作为 B1-t 陷阱的现场证据。
  - **自查修正（内部一致性）**：§2 开头与 §7 回退行原写「**4** 个生产文件」，而 §2.1–§2.5 实列 **5** 个（`Window.h` / `Window.cpp` / `Application.h` / `Application.cpp` / `PlatformWindowHost.h`）⇒ 统一为 **5 个生产文件 ≈ 35 行**。
  - **未做**：未改动任何框架代码；A5（`Release` 幂等用例）与"是否立项派生支持"留待裁决。
