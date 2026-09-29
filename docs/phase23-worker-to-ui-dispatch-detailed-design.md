# Phase 23 · 工作线程 → UI 线程投递（worker-to-UI dispatch）—— 详细设计（v1.0）

> 来源：初步设计 `docs/phase23-worker-to-ui-dispatch-preliminary-design.md` **v1.2** · 需求确认 `docs/phase23-worker-to-ui-dispatch-requirements.md` **v1.1** · 审计 `framework-defect-audit.md` **D-4**（= `desktopnest-roadmap.md` **G-3** = `roadmap-deferred.md` §7.9 顺位 ④）。
> 状态：**v1.0 待评审**（2026-09-29）——本稿是**实施规格**：把初设的接口 / 数据流 / 生命周期落成**逐文件、逐行**的改动清单（△1–△8），闭合初设留下的 **O1–O6** 与 **V1–V6**，并给出可直接照做的测试装置与用例正文。
> 本稿**不修改任何源码 / 测试代码**，也不运行测试；实施由用户在自己的工具链完成（四链验证纪律见 §5.3）。
> 用例锚点：**264 → 275**（★ 相对初设 v1.2 的 **273** 有 **+2** —— 见 §1.3 **D-1** 的如实报告）· Public 头 **92 → 92** · 设计文档 **147 → 148**。

---

## 1. 设计输入与实施总览

### 1.1 已定项（初设结论，本稿直接采用，不再论证）

| # | 定项 | 出处 |
|---|---|---|
| 1 | 新增**唯一**公共入口 `Application::PostToUi(std::function<void()>)`（+ 平台接缝 `PlatformApplication::PostToUi` 纯虚） | 初设 §2.1 / §2.2 |
| 2 | 队列与唤醒**全部留在** `Win32PlatformApplication` 内部；公共基类不带 `<mutex>` / `<deque>` | 初设 §1.3 |
| 3 | 唤醒载体 = **`PostThreadMessageW`（线程消息）**，不建隐藏宿主窗口、不复用托盘 `WM_NULL` / Desktop 跟随消息 | 初设 §1.2 |
| 4 | **入队 → 发唤醒 → 失败回滚** 在**同一临界区**内（★ P0 竞态的修法）；callback **永在锁外**执行 | 初设 §3.2 / §3.4 |
| 5 | callback **永不 inline**（UI 线程调用亦入队） | 初设 §1.1 / R9 / R10 / D7 |
| 6 | `Run()` 的 dispatch 分支**同时判别消息号与 `hwnd == nullptr`** | 初设 §3.3 |
| 7 | `RequestExit()` 与析构**先关闭 dispatch** 再清理既有资源；退出路径是**单一漏斗** | 初设 §3.5 |
| 8 | 逐 callback `try/catch`，异常不穿出消息泵 | 初设 §3.6 |
| 9 | `Application::Exit()` **不做**跨线程改造（N9）；工作线程退出须 `PostToUi([...]{ Exit(); })` | 需求 K15 / N9 |

### 1.2 ★★ 实施总览（**8 文件 = 生产 5 + 测试 3**，新建 1）

| # | 文件 | 性质 | 内容概要 |
|---|---|---|---|
| △1 | `ECDI/include/ECDI/Application/Application.h` | 改 | `+1 include`（`<functional>`）· `+1` 公共声明 `PostToUi` |
| △2 | `ECDI/src/Application/Application.cpp` | 改 | `+1 include`（`<utility>`）· `+1` 薄转发定义 |
| △3 | `ECDI/include/ECDI/Platform/PlatformApplication.h` | 改 | `+1` 纯虚 `PostToUi` |
| △4 | `ECDI/src/Platform/Win32/Win32PlatformApplication.h` | 改 | `+4 include` · `+1 override` · `+1` 测试缝（含 `using`）· `+3` 私有成员函数 · `+6` 成员 |
| △5 | `ECDI/src/Platform/Win32/Win32PlatformApplication.cpp` | 改 | 消息号登记 · 构造 · `Run` · `RequestExit` · `PostToUi` · `DrainUiDispatch` · `CloseUiDispatch` · 适配器 · 析构 |
| △6 | `ECDI/src/Tests/ApplicationDispatchTests.cpp` | **新建** | T23-1..T23-11（11 条）+ 通用装置 |
| △7 | `ECDI/src/Tests/RunAllTests.h` | 改 | `+1` 注册声明 |
| △8 | `ECDI/src/Tests/RunAllTests.cpp` | 改 | `+1` 注册调用 |

**构建系统：** `0` 改动。`CMakeLists.txt:107-109` 用 `GLOB_RECURSE ... CONFIGURE_DEPENDS` 收 `ECDI/src/Tests/*.cpp` ⇒ △6 自动入库；`CMakeLists.txt:42` 把 `/Tests/` 排除出框架库 ⇒ 新增测试文件不会进 `ECDI` 静态库。★ 但**测试注册是手工接线**（△7 / △8），CMake 的自动发现**不能**代替它。

### 1.3 ★★ 对初设的修正 / 细化（★ 如实报告，共 6 处）

| # | 初设写法 | 本稿修正 / 细化 | 依据 |
|---|---|---|---|
| **D-1** ★★ | 用例 **264 → 273**（T23-1..T23-9 = **9 条**） | **264 → 275（11 条）**：新增 **T23-10**（★ **C9 唤醒失败回滚**——初设 §1.5 补记 ① **自己指出**「P-① 修复路径本阶段无自动化覆盖」，本稿用**注入缝**把它变成可测）+ **T23-11**（★ **C10 新增前置条件**的拒绝分支，**零既有覆盖**）。同时把初设 §4 里 **C10 的测试映射由「T23-2」订正为「T23-11」** | 初设 §1.5 补记 ①；本稿 §4 / §5 |
| **D-2** ★★ | 成员草案写 `bool m_dispatchClosing = false;` | **必须改为 `std::atomic<bool>`**：初设 §3.4 步骤 6（**S6**）要求 drain **逐项**检查关闭态，而该检查发生在**锁外**（callback 执行区）；同一时刻**工作线程**的唤醒失败回滚（S5）会**写**这个标志 ⇒ 锁外读写即**数据竞争**（UB）。`m_dispatchWakePending` **保持普通 `bool`**（它只在锁内被访问） | 本稿 §2 △4 / §4.3 机检 6 |
| **D-3** ★ | 无测试缝；T23-8 的唤醒失败分支是「若可注入」的**条件式** | **新增测试缝** `SetPostThreadMessageSeamForTests`（**仅内部头**，先例 `SetShellSeamsForTests`）⇒ D-1 的 T23-10 成为**确定性单线程用例**。★ 不改公共 API（seam 不出实现层，先例形态见 `Win32PlatformApplication.h:48-74`）。★★ **复核发现（2026-09-29 实测）**：本仓库**4 处源码注释 + 2 处详设**均写「**条 51**：seam 不出实现层」，而 skill 当前 **条 51 = 提交前审计** —— seam 纪律实际落在 **条 91 的衍生纪律**（①措辞即先例 · ②「框架能力 vs 测试设施」判据）⇒ ★ **条号引用已漂移**（skill 插入新条目后编号整体后移）。本稿按**当前条号**理解；★ **源码注释的订正属超范围**（未获授权）⇒ 记为**待清账项**（建议随本阶段实施批次一并订正 4 处注释 + 2 处详设引用） | 本稿 §2 △4 |
| **D-4** | V6 留作待定（「是否要在公共 API 文档写 `std::bad_alloc` 可能传播」） | **定案：写**。`std::function` 入队的分配异常**不吞、不转成 `false`**——资源耗尽**不得伪装**成业务拒绝 | 本稿 §1.5 V6 |
| **D-5** ★ | 初设未评估测试装置的**跨用例污染** | ★★ **补出装置级缺陷**：本组用例全部在**同一个（测试主）线程**上 `Run()` ⇒ **每线程消息队列在用例之间是共享的**。若某个用例投出 `WM_QUIT` 而未被消费（例如「预投一次终止 QUIT」+「`Exit()` 又投一次」= 两次），**残留的 `WM_QUIT` 会让下一个用例的 `Run()` 立即返回** ⇒ 后续用例**假失败**。⇒ 装置定案：**① 每个 `Run()` 恰好消费一次终止 `WM_QUIT`；② 每个用例收尾清空线程消息队列**（有界） | 本稿 §2 △6 |
| **D-6** | §3.2 关键点 4 已拆为「平台快调用在锁内 / 用户 callback 在锁外」 | **再细一步**：唤醒失败分支里的 **`Logger::Log`** 也**留在锁内**并显式声明理由——实测 `Logger::Log` = 拼串 + `OutputDebugStringW`（`Core/Logger.cpp:35-51`），**不执行任何用户代码**，与 `PostThreadMessageW` **同类**（内核/调试调用）；锁外记录反而要手工提前解锁，得不偿失 | 本稿 §2 △5 |

> ★ **未改动项**（初设结论原样采用）：`PostThreadMessageW` 方向 · 队列存储归属（平台实现内）· 契约 C1–C10 骨架 · **8 文件**影响面（本稿未增删文件）· **O6**（第二平台）维持开放。

### 1.4 ★ 代码基线 B1–B14（本稿新增，全部带行号 · 2026-09-29 实测）

| # | 事实 | 位置 |
|---|---|---|
| **B1** | 现有消息泵全文 = `GetMessageW(&message, nullptr, 0, 0)` → `TranslateMessage` → `DispatchMessageW` → `PerformDeferredCleanup()`；退出时 `return static_cast<int>(message.wParam)`。★ `hwnd == nullptr` ⇒ `GetMessageW` **同时**取窗口消息与**线程消息** | `Win32PlatformApplication.cpp:28-46` |
| **B2** | 托盘回调消息在**匿名 namespace** 的常量区：`constexpr UINT kTrayCallbackMessage = WM_APP + 1;` ⇒ 本稿 `kUiDispatchMessage` 的**同款落点** | `Win32PlatformApplication.cpp:17-22`（`:20`） |
| **B3** | ★ **注释式占位登记的先例**：`WM_APP + 2` 的定义处显式写「`WM_APP + 1` 已被 … 占用（虽属不同窗口，仍避开以免同号两义）」 | `Win32PlatformWindow.cpp:67-73`（`:71-72` 注释 · `:73` 定义） |
| **B4** | 现有构造是 `= default`（声明在头、定义在 cpp —— pimpl 约束，条见头注释） | `Win32PlatformApplication.cpp:24` |
| **B5** | `RequestExit()` 全文 = `PostQuitMessage(0);` ⇒ **生产代码唯一**的 `PostQuitMessage` 调用点 | `Win32PlatformApplication.cpp:48-52` |
| **B6** | 析构四步（`NIM_DELETE` → `DestroyIcon` → `DestroyWindow` → 类成员析构） | `Win32PlatformApplication.cpp:585-619` |
| **B7** | ★ 测试缝区的既有形制：`using NotifyShellFn = ...` **先于**首个使用点（注释写明「GCC 对成员函数形参不做延迟名字查找」）· 观测访问器统一 `...ForTests` 后缀 | `Win32PlatformApplication.h:48-74`（`using` 在 `:53-54`） |
| **B8** | 应用级接缝现状：`:21` `SetDeferredCleanup`（基类持 `std::function` sink 的既有模式）· `:25` `Run` / `:28` `RequestExit` 纯虚 · `:70-76` 托盘 sink · `:92-100` `PerformDeferredCleanup`。★ `<functional>` 已在 `:6` | `include/ECDI/Platform/PlatformApplication.h` |
| **B9** | `Application` 侧：成员初始化列表 `m_platformApplication(std::make_unique<Win32PlatformApplication>())`（**构造同线程**的结构性保证）· `Run()` 纯转发 · `Exit()` = `m_running=false` + `RequestExit()` · 隐式退出（最后窗口关闭）亦经 `Exit()` | `Application.cpp:29-30` · `:58-63` · `:92-97` · `:147-149` |
| **B10** | ★ **O1 的口径先例**：生命周期前置条件被违反时，框架既有处置 = **`Logger::Log(Warning/Error, ...)` + `return`**，**不加 `FRAMEWORK_ASSERT`**（`SetWindowLayer` 的 Show 后拒绝 · `Minimize` 的 Show 前拒绝） | `Win32PlatformWindow.cpp:1038-1048` · `:1352-1357` |
| **B11** | ★ `Logger::Log` 全文 = 拼串 + `OutputDebugStringW`，**无任何可注入 sink** ⇒ 「断言日志存在」不可自动化 | `Core/Logger.cpp:35-51` |
| **B12** | `FRAMEWORK_ASSERT` 仅 `_DEBUG` 生效（`#ifdef _DEBUG`）⇒ 若 O1 走断言，**Release 将无任何守卫** | `Core/ECDIAssert.h:22` |
| **B13** | 测试装置基线：`EXPECT_*` 五宏（**无跨线程保护**——`TestContext*` 是唯一全局，失败记录追加进 `TestResult::failures`）· 注册声明表末尾 `:37` · 注册调用表末尾 `:35` · `ecdi_tests` 源 = `Tests/*.cpp` GLOB（`:107-109`）+ `/Tests/` 被排除出库（`:42`） | `TestFramework.h:70-95` · `RunAllTests.h:37` · `RunAllTests.cpp:35` · `CMakeLists.txt:42,107-119` |
| **B14** | 可复用先例装置：直接 include **内部平台头** + 直接构造 `Win32PlatformApplication`（本稿 T23-10 同款）· `<thread>` 用例的唯一先例 + **有界 deadline** 轮询 · 手动泵 `PumpMessages` + ★「单轮必须有上限」的**挂死教训**（无上限 `PM_REMOVE` 内层循环永不退出）· 新测试文件的注册函数形制 | `TrayTests.cpp:9,52` · `ChildProcessTests.cpp:8,23-29` · `DesktopLayerTests.cpp:48-60,81-93` · `PreshowGeometryTests.cpp:147-152` |

### 1.5 初设开放项的闭合（O1–O6 / V1–V6）

| # | 问题 | ★ 本稿定案 | 理由 |
|---|---|---|---|
| **O1** | `Run()` 错线程的 Release 行为 | **不加 `FRAMEWORK_ASSERT`**；`Run()` 入口 `if (m_uiThreadId != GetCurrentThreadId())` → **`Logger::Log(Error, ...)` + `return 0`** | ① 与既有前置条件处置**同形**（B10）；② 加断言则 **Release 无守卫**（B12）且用例会在 Debug 下**中止整个套件**、无法自动化；③ **不造错误码**——实测 `Run()` 现有唯一 `return` 是 `static_cast<int>(message.wParam)`，框架**没有** `-1` 约定（B1）。选 `Error` 而非 `Warning` 的理由：`SetWindowLayer` 是「忽略一个配置项」，这里是「**请求的操作完全无法执行**」 |
| **O2** | ① 失败是否保留错误原因 / ② 原子粒度与「重开」 | **① 不保留**（只记固定文本日志）；**② 入队与唤醒同一临界区（已定案）· 关闭后禁止重开** | ① 需要额外错误状态而**无消费者**（YAGNI）；② 重开只会给「已拿到 `false` 的提交者」制造**第二次机会的错觉** |
| **O3** | 异常日志是否转换 `what()` | **不转换**：只用固定宽字符串 | `what()` 的**窄字符编码未定义**（可能 GBK、也可能 UTF-8），转 UTF-16 可能产出乱码；框架边界只需知道「某 callback 抛了」，不需要文本 |
| **O4** | T23-8 如何稳定观察日志 | **不观察**：判据改为**可观测后果**（B 仍执行 + 异常未穿出 `Run()`）；日志存在性 = **仅代码审查 + 运行期人工可见**（VS 输出窗口 / DebugView） | `Logger` **无可注入 sink**（B11）；★ 沿条 40 的表述纪律——**不让「没测到」看起来像「测到了」** |
| **O5** | 是否引入返回状态枚举 | **维持 `bool`**，三义（空工作 / 已关闭 / 唤醒失败）**如实记录**、不伪装 | 有分支需求的消费者**尚不存在**（条 22「第二个真实消费者才抽象」）；本稿只把它写进公共头注释与 §8 L1 |
| **O6** | 第二平台是否沿用同名 `PostToUi` | **维持开放**（本阶段不动） | 当前唯一平台实现是 Win32；届时须复核其消息循环模型，**不照抄**线程消息载体 |
| **V1** | `Run()` 返回码约定 | 见 **O1**（`0`，与「立即收到 `WM_QUIT`」同形） | B1 实测：无既有错误码 |
| **V2** | 消息队列创建时机 | **构造期**（`GetCurrentThreadId()` + `PeekMessageW(PM_NOREMOVE)`）——**不是可选优化而是必要条件** | `PostThreadMessageW` **要求目标线程已有消息队列**；不建则 T23-1（`Run()` 前提交）**根本不可能成立** |
| **V3** | 唤醒失败后是否永久关闭 | **是**（清队列 + 置 `closing` + 拒绝后续） | 构造期已保证队列存在 ⇒ 失败只可能意味着**线程已退出/正在退出**，属**终态**；不引入重试循环 |
| **V4** | 单次 drain 的批次边界 | `swap` 到局部队列 → 解锁 → 逐项执行，**每项前查关闭态**（锁外读 `std::atomic<bool>`，见 D-2） | 关闭态可在**批次中途**由 A 的 `Exit()` 产生（T23-7 的判据） |
| **V5** | 异常日志内容 | 固定文本（见 **O3**） | — |
| **V6** | `PostToUi` 参数分配异常 | **不吞**；公共头注释**写明** `std::bad_alloc` 可能传播（D-4） | 资源耗尽不得伪装成业务拒绝；`std::function` 的复制/移动异常与「提交被拒绝」是**两类事实** |

---

## 2. 逐文件改动（△1–△8）

> 每处均给出**落点**（既有行号）+ **增量全文**；未提及的代码**保持现状**（条 42 最小修改面）。

### △1 `ECDI/include/ECDI/Application/Application.h`

**改动 1：**标准库 include 区（`:6-8`）按字母序新增一行。

```cpp
#include <functional>
#include <memory>
#include <string>
#include <vector>
```

**改动 2：** public 区，`Create` 声明（`:70-71` 结束）之后、`Exit` 声明（`:73` 注释起）之前插入。

```cpp
	/// @brief 把工作异步提交到本 Application 的 UI 消息线程（Phase 23 D-1）
	/// @param work 待执行工作；**按值接收**，平台队列取得其所有权
	/// @return true = 已接收入队（★ **不表示已执行**）；false = 空工作 / 已进入关闭阶段 / 平台唤醒失败
	/// @details 无论调用者是否已经在 UI 线程，**都不会在本调用栈内执行 work**（R9 / R10 / D7）。
	///          不绑定 Window、不暴露 HWND 或任何 Win32 类型（R3 / D1）。
	///          调用方若在 work 中捕获 `Application*` / `Window*` / 其它对象，须**自行保证**
	///          其在 callback 执行时仍然有效——框架不提供自动保活（R5 / D5）。
	/// @note 可能抛出 `std::bad_alloc`（入队分配失败）——框架**不吞、不转成 `false`**（V6）。
	bool PostToUi(std::function<void()> work);
```

### △2 `ECDI/src/Application/Application.cpp`

**改动 1：**标准库 include 区（当前仅 `:25` 的 `<algorithm>`）新增一行。

```cpp
#include <algorithm>
#include <utility>
```

**改动 2：** `Create` 定义（`:66-90`）之后、`Exit` 定义（`:92`）之前插入。

```cpp
bool Application::PostToUi(std::function<void()> work){

	// 7.1.5 / Phase 23：本函数**只做转发**——队列、唤醒、关闭态全部在平台实现内部。
	//   ★ std::move：形参已是按值副本，若此处传左值会**二次拷贝** std::function（可能再分配）。
	//   ★ 不 catch：std::bad_alloc 属资源耗尽，不属于"提交被拒绝"（V6 定案）。
	return m_platformApplication->PostToUi(std::move(work));

}
```

### △3 `ECDI/include/ECDI/Platform/PlatformApplication.h`

**改动：**在 `RequestExit()`（`:28`）之后、Phase 14 托盘区注释（`:30`）之前插入一个纯虚能力（沿用本文件 `:30-34` 已写明的**应用级能力惯例三步**第 ① 步）。

```cpp
	/// @brief 异步提交工作到平台 UI 消息线程（Phase 23 D-1——★ 应用级能力惯例第 ① 步）
	/// @param work 待执行工作（按值；实现者取得其所有权）
	/// @return true = 已入队；false = 空工作 / 平台已关闭 / 唤醒失败
	/// @details 实现者负责**线程安全入队 + 唤醒**；★ **不得在调用栈内执行 work**（R9 / D7）。
	///          本能力只解决「工作怎样到达 UI 线程」，**不表达任何 Framework Event**（N6）。
	/// @note 本能力**不属于**「窗口级能力」（不挂 `PlatformWindow`）：工作线程可能没有窗口、
	///       也可能服务多个窗口；且它必须在窗口生命周期之外成立（R5 / D5）。
	virtual bool PostToUi(std::function<void()> work) = 0;
```

> ★ **纯虚实现者盘点（条 33，2026-09-29 实测）**：全库 `PlatformApplication` 的**唯一**派生类是 `Win32PlatformApplication`（`Win32PlatformApplication.h:23`）；测试侧**没有**平台替身（`TrayTests.cpp:52` 等直接构造真实现）。⇒ 本新增纯虚**只影响 1 个实现者**，零替身同步。

### △4 `ECDI/src/Platform/Win32/Win32PlatformApplication.h`

**改动 1：** include 区（`memory` / `string` 在 `:10-11`）新增四个标准库头。

```cpp
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
```

**改动 2：** public 能力区，`RequestExit()`（`:38`）之后插入一个 override。

```cpp
	bool PostToUi(std::function<void()> work) override;
```

**改动 3：** public 测试缝区（`:48-74`）**末尾**追加（★ `using` 必须写在**首个使用点之前**——B7 / 条 62①）。

```cpp
	/// @brief UI 唤醒投递缝（★ 仅内部头——seam 不出实现层，skill 条 91 衍生纪律；不进公共 API）
	/// @details 供 T23-10 注入「唤醒失败」，把初设 §3.2 的**失败回滚分支（S5）**变成可测
	///          （初设 §1.5 补记 ① 自己指出该分支本无自动化覆盖）。
	///          ★ `using` 必须先于首个使用点声明——GCC 对成员函数形参不做延迟名字查找（B7）。
	using PostThreadMessageFn = BOOL (*)(DWORD idThread, UINT msg, WPARAM wParam, LPARAM lParam);

	void SetPostThreadMessageSeamForTests(PostThreadMessageFn post){ m_postThreadMessage = post; }
```

**改动 4：** private 方法区（`:76-95`）末尾追加。

```cpp
	/// @brief 取出当前待执行工作并逐项执行（★ 只由 `Run()` 的 dispatch 分支调用）
	void DrainUiDispatch();

	/// @brief 关闭投递通路（幂等；`RequestExit` 与析构在清理既有资源**之前**调用）
	void CloseUiDispatch() noexcept;

	/// @brief 唤醒投递缝适配器（默认真实 API = `PostThreadMessageW`）
	static BOOL PostThreadMessageAdapter(DWORD idThread, UINT msg, WPARAM wParam, LPARAM lParam);
```

**改动 5：** 成员区**之前**（即现有 `m_trayHostHwnd`（`:97`）之前）插入一组。

```cpp
	// ── Phase 23：UI 投递通路（★ 作为**一组**置于既有托盘状态之前；不改既有成员声明顺序）──
	DWORD m_uiThreadId = 0;                    ///< 构造线程（owner）——`Run()` 必须在此线程执行
	std::mutex m_dispatchMutex;                ///< 保护队列与唤醒标志（★ **不保护** callback 执行）
	std::deque<std::function<void()>> m_dispatchQueue;   ///< UI 待执行工作
	std::atomic<bool> m_dispatchClosing{false};///< ★ 必须原子：drain 在**锁外**逐项读它（见详设 D-2）
	bool m_dispatchWakePending = false;        ///< 已有在途唤醒消息或正在 drain（★ 仅锁内访问）

	PostThreadMessageFn m_postThreadMessage = &PostThreadMessageAdapter;   ///< 唤醒缝（默认真实 API）
```

### △5 `ECDI/src/Platform/Win32/Win32PlatformApplication.cpp`

#### 改动 1：匿名 namespace 常量区（`:17-22`）追加消息号登记

```cpp
/// @brief UI 投递的唤醒消息（★ **线程消息**——其 `MSG::hwnd` 恒为 NULL）
/// @details ★ 注释式占位登记（先例 `Win32PlatformWindow.cpp:71-72`）：
///          `WM_APP + 1` = 托盘回调（本文件 `:20`）· `WM_APP + 2` = Desktop 跟随延后一拍
///          （`Win32PlatformWindow.cpp:73`）⇒ 本项目取 **`WM_APP + 3`**。
///          严谨性说明：线程消息与窗口消息**类别不同**（前者 `hwnd` 恒 NULL），
///          数值相同也不会互相吞掉；此处仍显式避让，以免**同号两义**妨碍排查。
///          ⚠️ 测试会向**窗口**投 `WM_APP + 2`（`DesktopLayerTests.cpp:532`）——这正是
///          `Run()` 分支必须**同时判别 `hwnd`** 的实证理由（详设 §2 △5 改动 3）。
constexpr UINT kUiDispatchMessage = WM_APP + 3;
```

#### 改动 2：构造函数（替换 `:24` 的 `= default`）

```cpp
Win32PlatformApplication::Win32PlatformApplication(){

	// ★ Phase 23：记录 owner thread，并**建立本线程的消息队列**。
	//   为什么必须：`PostThreadMessageW` 要求目标线程**已有消息队列**（否则直接失败）
	//   ⇒ 不建队列，「Run() 之前提交」（T23-1）在结构上不可能成立（详设 V2）。
	//   ★ `PM_NOREMOVE`：只窥探、不移除、**不派发**任何消息（构造期不得触发回调——条 108）。
	m_uiThreadId = GetCurrentThreadId();

	MSG message{};

	PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);

}
```

#### 改动 3：`Run()`（`:28-46` 替换）

```cpp
int Win32PlatformApplication::Run(){

	// ★ Phase 23 · C10：owner-thread 前置条件（本阶段**新增**的约束，非既有事实重述）。
	//   为什么在框架侧显式闭合、而不是放任跨线程：唤醒是 `PostThreadMessageW(m_uiThreadId, ...)`
	//   ⇒ 队列归属**构造线程**；换线程泵消息会**永远取不到**投递工作（静默失败）。
	//   处置口径 = 与既有生命周期前置条件**同形**（`Win32PlatformWindow.cpp:1038-1048` 的
	//   `SetWindowLayer` 用 Warning + return）——★ 不加 FRAMEWORK_ASSERT：断言只在 _DEBUG 生效
	//   （`Core/ECDIAssert.h:22`）⇒ Release 无守卫，且会让本项在前置条件下中止整个测试套件。
	//   返回值沿用 0（与"立即收到 WM_QUIT"同形）：框架**没有** Run 错误码约定，不凭空造（详设 O1 / L4）。
	if (m_uiThreadId != GetCurrentThreadId()){

		Logger::Log(LogLevel::Error,
			L"Run() called on a non-owner thread - the UI dispatch queue belongs to the constructing thread");

		return 0;

	}

	// 标准 Win32 消息循环：GetMessage 返回 0 时退出（收到 WM_QUIT）
	MSG message{};

	while (GetMessageW(&message, nullptr, 0, 0)){

		// ★ Phase 23：私有 dispatch 线程消息分支。
		//   判据**必须同时判别 `hwnd`**——线程消息的 hwnd 恒为 NULL（`GetMessageW(nullptr,...)`
		//   会同时取出窗口消息与线程消息）；只比消息号一旦与窗口消息同号，
		//   该分支会把窗口消息**静默吞掉**（不再走 DispatchMessageW），且极难定位（初设 R-①）。
		if (message.hwnd == nullptr && message.message == kUiDispatchMessage){

			DrainUiDispatch();

		} else {

			TranslateMessage(&message);

			DispatchMessageW(&message);

		}

		// 每条消息后的延迟清理时机（资源生命周期管理——框架经 SetDeferredCleanup 注册）
		PerformDeferredCleanup();

	}

	return static_cast<int>(message.wParam);

}
```

> ★ **不把 dispatch 消息送进 `WindowMessageHandler`**：它不是窗口事件，也不应伪造 `Window*` 来源；**不把它包成 `TimerEvent`**：那会错误进入焦点控件派发链（`Application.cpp:184-193`）。

#### 改动 4：`RequestExit()`（`:48-52` 替换）

```cpp
void Win32PlatformApplication::RequestExit(){

	// ★ Phase 23：**先关闭投递通路，再请求退出**。
	//   退出路径是**单一漏斗**（生产代码 `PostQuitMessage` 仅此 1 处，`:50`；隐式退出亦经
	//   `Application::Exit()`——`Application.cpp:147-149`）⇒ 单点关闭即覆盖全部退出路径。
	CloseUiDispatch();

	PostQuitMessage(0);

}
```

#### 改动 5：新增 `PostToUi` / `DrainUiDispatch` / `CloseUiDispatch` / 适配器

> 落点：紧接 `RequestExit()` 之后（即 `DeclareDpiAwareness` 段（`:54-94`）之前），与 `Run/RequestExit` 同段。

```cpp
// ── Phase 23：UI 投递通路（入队 / 唤醒 / 批次 drain / 关闭）────────────────────

bool Win32PlatformApplication::PostToUi(std::function<void()> work){

	if (!work){

		return false;   // 空工作不入队（★ 不进临界区、不触发唤醒——状态表 S3）

	}

	// ★★ 入队 / 唤醒 / 失败回滚：**同一临界区**（初设 §3.2 的 P0 竞态修法）。
	//   为什么必须同锁：若"发唤醒"在锁外，则「A 解锁 → B 入队并拿到 true（见 wakePending=true
	//   ⇒ 不唤醒）→ A 的唤醒失败并清队列」这条交错会**丢掉 B 已被 true 承诺的工作**；
	//   已返回的 true **撤不回**，任何"事后补救状态"都拦不住在途提交者。
	std::lock_guard lock(m_dispatchMutex);

	if (m_dispatchClosing){

		return false;   // 含"上次唤醒失败后已就地关闭"的情形（状态表 S4）

	}

	m_dispatchQueue.emplace_back(std::move(work));

	if (m_dispatchWakePending){

		return true;    // 已有在途唤醒消息：入队即视为完成（状态表 S2）

	}

	// ★ 唤醒**在锁内**发出：`PostThreadMessageW` 是内核快调用、**不执行用户代码** ⇒ 可持锁。
	//   （反例见上：放到锁外即初设 §3.2 交错表的 bug。）
	if (m_postThreadMessage(m_uiThreadId, kUiDispatchMessage, 0, 0) == FALSE){

		// 唤醒失败 ⇒ 这批工作永远不会被 drain ⇒ **就地回滚本临界区的全部后果**。
		// ⚠️ 不得调用 CloseUiDispatch()——它自己会加锁 ⇒ **自死锁**。
		// ⚠️ 日志留在锁内是**有意**的：`Logger::Log` = 拼串 + `OutputDebugStringW`
		//    （`Core/Logger.cpp:35-51`），不执行用户代码，与 PostThreadMessageW 同类。
		m_dispatchWakePending = false;

		m_dispatchQueue.clear();

		m_dispatchClosing = true;   // ★「关闭态」同时就是「唤醒失败态」⇒ 无需第 4 个状态

		Logger::Log(LogLevel::Error,
			L"UI dispatch wake failed - the queue is closed and the pending work is dropped");

		return false;   // 状态表 S5（★ 与 S1 共用同一临界区）

	}

	m_dispatchWakePending = true;

	return true;   // 状态表 S1：true = 已入队并被队列拥有（**不表示已执行**）

}

void Win32PlatformApplication::DrainUiDispatch(){

	// ★ 批次快照：先把当前全部待执行工作搬出共享队列，**再解锁**。
	std::deque<std::function<void()>> batch;

	{

		std::lock_guard lock(m_dispatchMutex);

		batch.swap(m_dispatchQueue);

		m_dispatchWakePending = false;   // 允许后续提交再次唤醒（drain 之后的新提交）

	}

	// ★ 解锁后逐项执行——callback 是**用户代码**：用户在 callback 里反向 `PostToUi` 会
	//   二次加锁 ⇒ 锁内执行即**自死锁**（初设 §3.2 关键点 4）。
	for (std::function<void()>& work : batch){

		// ★ 逐项查关闭态（状态表 S6 的收尾语义）：关闭可能在**本批次中途**由上一项产生
		//   （T23-7：A 调 `Exit()` ⇒ B 及其余项丢弃）。★ 本读取在**锁外** ⇒ 该标志必须是
		//   `std::atomic<bool>`（工作线程的失败回滚会写它——详设 D-2）。
		if (m_dispatchClosing){

			break;

		}

		try{

			work();

		} catch (const std::exception&){

			Logger::Log(LogLevel::Error, L"ECDI UI dispatch callback threw std::exception");

		} catch (...){

			Logger::Log(LogLevel::Error, L"ECDI UI dispatch callback threw an unknown exception");

		}

	}

	// batch 在此析构：剩余工作的**用户析构函数**也在锁外执行（同上，用户代码不入锁）。

}

void Win32PlatformApplication::CloseUiDispatch() noexcept{

	// 幂等：可被 `RequestExit()` 与析构重复调用（后者是"析构前先断通路"的硬要求）。
	std::lock_guard lock(m_dispatchMutex);

	m_dispatchClosing = true;

	m_dispatchQueue.clear();

	m_dispatchWakePending = false;

}

BOOL Win32PlatformApplication::PostThreadMessageAdapter(DWORD idThread, UINT msg, WPARAM wParam, LPARAM lParam){

	return PostThreadMessageW(idThread, msg, wParam, lParam);

}
```

#### 改动 6：析构（`:585` 起，**首行**插入）

```cpp
Win32PlatformApplication::~Win32PlatformApplication(){

	// ★ Phase 23：**先关闭投递通路**（拒绝新工作 + 丢弃未执行工作），再做既有资源清理。
	//   理由：此后任何工作线程的提交都拿不到 true；已排队但未执行的工作不会在托盘/窗口
	//   资源清理过程中被 drain（`Run()` 已不在，且析构后对象即将消失）。
	CloseUiDispatch();

	// 2.1 幽灵图标防线（R1 硬要求）：以下是既有四步，**一字不改**
	...
```

> ★ 既有四步（`NIM_DELETE` → `DestroyIcon` → `DestroyWindow` → 类成员析构）**零改动**；`CloseUiDispatch` 只加在最前。

### △6 新建 `ECDI/src/Tests/ApplicationDispatchTests.cpp`

> 文件需 **UTF-8 BOM + CRLF**（全仓库文本文件一致）；`#include` 后无空格（项目风格）。全文规格如下。

```cpp
#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "ECDI/Application/Application.h"
#include "Platform/Win32/Win32PlatformApplication.h"   // 唤醒注入缝（内部头——seam 不出实现层）

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

using namespace ECDI;   // ★ 本项目测试文件惯例（TrayTests.cpp:16 同款）

namespace {

// ══════════════════════════════════════════════════════════════════
// Phase 23：工作线程 → UI 线程投递 —— T23-1..T23-11
// ★ 本组**必须**用真实 `Application` + 真实消息泵：被测行为是「跨线程投递 + 泵内 drain」，
//   任何替身都到达不了 `Win32PlatformApplication::Run()` 的 dispatch 分支（C2 / C8）。
// ══════════════════════════════════════════════════════════════════

/// @brief 最小 Application（本组只测投递通路，不观测事件）
struct TestApp : public Application {};

// ── 装置 ①：确定终止 ────────────────────────────────────────────────
// ★★ 为什么需要它：`Run()` 阻塞在 `GetMessageW`，若实现漏了唤醒，用例会**挂死整个套件**
//   （不是红字，是挂起）。补投一条 `WM_QUIT` 使「泵退出」**不依赖被测的唤醒是否成功**。
/// @details 与 `PostToUi` 的唤醒消息**同属本线程队列且 FIFO** ⇒ 只要在**最后一次提交之后**
///          补投，泵就会先 drain 完所有工作、再取到 WM_QUIT 退出（顺序由队列保证，非时序假设）。
///          ⚠️ `PeekMessageW` 的队列创建已在 `Win32PlatformApplication` 构造期完成，
///          但本函数在**任何** `Run()` 之前调用仍需队列存在 ⇒ 本组用例都持有 `TestApp`（构造即建队列）。
void PostQuitToThisThread(){

	::PostThreadMessageW(::GetCurrentThreadId(), WM_QUIT, 0, 0);

}

// ── 装置 ②：残留消息清理（★ 防跨用例污染）───────────────────────────
// ★★ 为什么需要它：本组全部在**同一个测试主线程**上 `Run()` ⇒ **消息队列在用例之间共享**。
//   多投的 `WM_QUIT`（例如「预投终止 QUIT」+「`Exit()` 又投一次」）若未被消费，
//   **下一个用例的 `Run()` 会立即返回** ⇒ 后续用例假失败。
//   ⚠️ 单轮**必须有上限**——无上限的 `PM_REMOVE` 内层循环会因窗口持续重投 `WM_PAINT` 而
//   **永不退出**（`DesktopLayerTests.cpp:81-93` 的挂死教训，本 helper 沿用其 256 上限）。
void DrainStrayMessages(){

	MSG message{};

	int burst = 0;

	while (burst++ < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)){

		// 不 Translate / 不 Dispatch：这里只做「清场」（到这一步测试对象已全部销毁）

	}

}

/// @brief 用例收尾（★ 每个用例最后一行调用——先清场，再让下个用例拿到干净队列）
struct TestScopeCleanup{

	~TestScopeCleanup(){ DrainStrayMessages(); }

};

// ── T23-1：`Run()` 之前由工作线程提交（C1 / C2 / A1 / A2）─────────────
void Test23SubmitBeforeRun(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> executed{0};

	std::atomic<bool> accepted{false};

	std::thread::id submitThread{};

	std::thread::id executeThread{};

	std::thread worker([&]{

		submitThread = std::this_thread::get_id();

		// ★ 工作线程内**不做断言**（`TestContext` 无跨线程保护——B13）；只记录，主线程断言
		accepted.store(app.PostToUi([&]{

			executeThread = std::this_thread::get_id();

			executed.fetch_add(1, std::memory_order_relaxed);

		}));

	});

	worker.join();

	PostQuitToThisThread();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(accepted.load());                        // C1 / A2

	EXPECT_EQ(executed.load(), 1);

	EXPECT_NE(submitThread, executeThread);              // A1：提交线程 ≠ 执行线程

	EXPECT_EQ(executeThread, std::this_thread::get_id()); // A1：执行线程 = UI 消息线程

}

// ── T23-2 ★★：`Run()` 运行期间由工作线程提交（C2 / A1）─────────────────
// ★★ 设计要点：「提交确实发生在 Run 运行期间」由**结构**保证——工作线程在**UI callback G
//   内部**派生（此刻 UI 线程必然在 `Run()` 的 drain 栈上）⇒ 无需 `Sleep`、无需自旋等待。
void Test23SubmitWhileRunning(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> executed{0};

	std::atomic<bool> accepted{false};

	std::thread::id submitThread{};

	std::thread::id executeThread{};

	const std::thread::id uiThread = std::this_thread::get_id();

	const bool gateAccepted = app.PostToUi([&]{

		// G：在 drain 内执行。此处派生工作线程 ⇒ 其提交时刻**晚于** Run 进入泵
		std::thread worker([&]{

			submitThread = std::this_thread::get_id();

			accepted.store(app.PostToUi([&]{

				executeThread = std::this_thread::get_id();

				executed.fetch_add(1, std::memory_order_relaxed);

			}));

			// ★ 提交**完成之后**再补投终止 QUIT ⇒ 队列序 = [唤醒 C, WM_QUIT]
			PostQuitToThisThread();

		});

		worker.join();   // G 内 join：本线程被占用期间 worker 完成提交（不需要 UI 线程参与）

	});

	EXPECT_TRUE(gateAccepted);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(accepted.load());

	EXPECT_EQ(executed.load(), 1);

	EXPECT_NE(submitThread, uiThread);

	EXPECT_EQ(executeThread, uiThread);

}

// ── T23-3：单源 FIFO（C4 / A4）──────────────────────────────────────
void Test23SingleSourceFifo(){

	TestApp app;

	TestScopeCleanup cleanup;

	char order[3] = { 0, 0, 0 };

	std::atomic<bool> allAccepted{false};

	std::thread worker([&]{

		bool ok = app.PostToUi([&]{ order[0] = 'A'; });

		ok = app.PostToUi([&]{ order[1] = 'B'; }) && ok;

		ok = app.PostToUi([&]{ order[2] = 'C'; }) && ok;

		allAccepted.store(ok);

		PostQuitToThisThread();

	});

	worker.join();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(allAccepted.load());

	EXPECT_TRUE(order[0] == 'A');

	EXPECT_TRUE(order[1] == 'B');

	EXPECT_TRUE(order[2] == 'C');   // ★ 只断言**单源**顺序；不对多线程交错作任何断言（C4 口径）

}

// ── T23-4 ★★：callback 内提交**不重入**（C3 / R9 / A9）─────────────────
void Test23NoReentrancyFromCallback(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<bool> aFinished{false};

	std::atomic<bool> bPosted{false};

	std::atomic<bool> bSawAFinished{false};

	std::atomic<int> bExecuted{0};

	const bool aAccepted = app.PostToUi([&]{

		bPosted.store(app.PostToUi([&]{

			// ★ 判据：B 执行时 A 是否已执行完**末尾语句**。
			//   若实现递归消费（bug），B 会在 `PostToUi(B)` 返回前跑 ⇒ aFinished == false ⇒ 响亮失败。
			bSawAFinished.store(aFinished.load());

			bExecuted.fetch_add(1, std::memory_order_relaxed);

		}));

		PostQuitToThisThread();   // 队列序 = [唤醒 B, WM_QUIT]（B 的唤醒先入队）

		aFinished.store(true);

	});

	EXPECT_TRUE(aAccepted);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(bPosted.load());

	EXPECT_EQ(bExecuted.load(), 1);      // B 最终确实执行了（正对照）

	EXPECT_TRUE(bSawAFinished.load());   // ★ 但只能在 A 返回之后

}

// ── T23-5 ★★：UI 线程提交同样是异步（D7 / R10 / A10）───────────────────
void Test23UiThreadSubmitIsAlsoAsync(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> executed{0};

	const bool accepted = app.PostToUi([&]{ executed.fetch_add(1, std::memory_order_relaxed); });

	// ★★ 核心判据：提交调用返回时，callback **尚未执行**（不存在 UI 线程 fast path）
	EXPECT_TRUE(accepted);

	EXPECT_EQ(executed.load(), 0);

	PostQuitToThisThread();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_EQ(executed.load(), 1);   // 只在后续消息泵阶段执行

}

// ── T23-6 ★：队列不因窗口销毁而停摆（C5 / A5 · 生命周期解耦）────────────
// ★ 修正（初设 v1.2 P-④）：原期望句「测试只验证队列不保存隐式窗口指针」**不可执行**
//   （无法从外部观测"队列里有没有指针"）⇒ 本用例改验**可观测的生命周期解耦**：
//   ① 窗口销毁后队列仍工作；② 值语义载荷在 UI 线程读到仍有效。
//   「队列元素类型不含 Window*/Application*」改由**结构审查**承担（§4.3 机检 5）。
void Test23QueueSurvivesWindowDestroyed(){

	TestApp app;

	TestScopeCleanup cleanup;

	// ⚠️ **关键前提**：关闭"最后窗口关闭即退出"。否则 `Release()` ⇒ OnWindowDestroyed ⇒
	//    `Exit()` ⇒ `CloseUiDispatch()` 会（正确地）清空队列，本用例的载荷就被丢弃了
	//    ——那是 C6 的正常语义，不是缺陷；本用例要验的是"**关闭之前**队列不依赖窗口"。
	app.SetQuitOnLastWindowClosed(false);

	Window& window = app.Create("ECDI_DispatchLifecycle", 320, 240);

	window.Release();   // 只销毁 HWND（UI 线程）；Window 对象进入延迟销毁列表
	// ⚠️ 此后**不得再触碰 window 引用**——`ProcessDeferredDestroy()` 会在泵内释放该对象

	std::atomic<bool> payloadValid{false};

	{

		std::string payload = "decode-done:0x2A";   // ★ 局部：提交后立即出作用域

		const bool accepted = app.PostToUi([&payloadValid, payload]{

			// payload 是**副本**（值语义）⇒ 原对象已亡，队列里的副本必须仍然有效
			payloadValid.store(payload == "decode-done:0x2A");

		});

		EXPECT_TRUE(accepted);

	}

	PostQuitToThisThread();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(payloadValid.load());

}

// ── T23-7 ★★：`Exit()` 关闭后丢弃同批未执行工作（C6 / D6① / A5 / A10）───
void Test23ExitDropsPendingWork(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> aExecuted{0};

	std::atomic<int> bExecuted{0};

	const bool aAccepted = app.PostToUi([&]{

		aExecuted.fetch_add(1, std::memory_order_relaxed);

		app.Exit();   // ★ 关闭 dispatch（CloseUiDispatch）+ PostQuitMessage（终止由它自带）

	});

	const bool bAccepted = app.PostToUi([&]{ bExecuted.fetch_add(1, std::memory_order_relaxed); });

	EXPECT_TRUE(aAccepted);

	EXPECT_TRUE(bAccepted);              // ★ 提交时二者都被接受（true 只承诺"已入队"）

	EXPECT_EQ(app.Run(), 0);

	EXPECT_EQ(aExecuted.load(), 1);      // 正在执行的不被强行中断

	EXPECT_EQ(bExecuted.load(), 0);      // ★ 同批剩余项被关闭态丢弃（逐项检查的判据）

	EXPECT_FALSE(app.PostToUi([]{ }));   // 关闭后拒绝新工作

}

// ── T23-8 ★★：callback 异常隔离（C7 / O4）────────────────────────────
void Test23CallbackExceptionIsolated(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> aExecuted{0};

	std::atomic<int> bExecuted{0};

	std::atomic<bool> allAccepted{false};

	std::thread worker([&]{

		bool ok = app.PostToUi([&]{

			aExecuted.fetch_add(1, std::memory_order_relaxed);

			throw std::runtime_error("intentional test exception");

		});

		ok = app.PostToUi([&]{ bExecuted.fetch_add(1, std::memory_order_relaxed); }) && ok;

		allAccepted.store(ok);

		PostQuitToThisThread();

	});

	worker.join();

	// ★ 若异常穿出 drain ⇒ 穿出 Run() ⇒ 穿出本用例 ⇒ Runner 记 "unhandled exception" FAIL。
	//   故本行返回 0 即证明**异常已被隔离在 dispatch 边界内**（C7）。
	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(allAccepted.load());

	EXPECT_EQ(aExecuted.load(), 1);

	EXPECT_EQ(bExecuted.load(), 1);   // ★ 单个 callback 失败不阻断后续（C7）

	// ★ O4 定案：**不断言日志**（`Logger` 无可注入 sink——B11）；
	//   日志存在性 = 仅代码审查 + 运行期人工可见（VS 输出窗口 / DebugView）。

}

// ── T23-9 ★：真实消费者形态载荷（A6）────────────────────────────────
// ★ 载荷类型定义在**测试 TU 内部** ⇒ 结构上不可能进公共头（§4.3 机检 5 同步核）
struct DecodeDonePayload{

	int         resultCode;

	int         width;

	int         height;

	std::string sourcePath;

};

void Test23ConsumerShapedPayload(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<bool> matched{false};

	std::atomic<bool> accepted{false};

	std::thread worker([&]{

		const DecodeDonePayload payload{ 0, 64, 64, "C:/icons/sample.ico" };   // 模拟"解码完成"

		accepted.store(app.PostToUi([&matched, payload]{   // 值语义：载荷随 std::function 移动入队

			matched.store(payload.resultCode == 0 && payload.width == 64
				&& payload.height == 64 && payload.sourcePath == "C:/icons/sample.ico");

		}));

		PostQuitToThisThread();

	});

	worker.join();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(accepted.load());

	EXPECT_TRUE(matched.load());

}

// ── T23-10 ★★：唤醒失败就地回滚 + 拒绝路径（C9 / C1 / S3–S5）────────────
// ★ 本用例直接构造 **Win32PlatformApplication**（不经 `Application`、**不泵消息**）
//   ——先例 `TrayTests.cpp:52`；因为被测行为发生在「入队与唤醒」这一步，与消息泵无关。
// ★ 正对照（条 81）：**真实唤醒**路径返回 true 由 T23-1 承担（那里用的是真 API）。
int  g_postThreadMessageCalls  = 0;

BOOL g_postThreadMessageResult = TRUE;

BOOL FakePostThreadMessage(DWORD, UINT, WPARAM, LPARAM){

	++g_postThreadMessageCalls;

	return g_postThreadMessageResult ? TRUE : FALSE;

}

void Test23WakeFailureRollsBack(){

	Win32PlatformApplication app;   // ★ 构造即建立本线程消息队列（V2）

	app.SetPostThreadMessageSeamForTests(&FakePostThreadMessage);

	int executed = 0;

	// ① 空工作 ⇒ false（S3）：★ 不进临界区、**不触发唤醒**
	g_postThreadMessageCalls = 0;

	g_postThreadMessageResult = TRUE;

	EXPECT_FALSE(app.PostToUi(std::function<void()>{}));

	EXPECT_EQ(g_postThreadMessageCalls, 0);

	// ② 唤醒失败 ⇒ false + **就地回滚**（S5）：队列被清空 + 进入关闭态
	//    ★ 正对照：先证明缝真的被调用过（否则"回滚"可能只是"压根没走到唤醒"）
	g_postThreadMessageResult = FALSE;

	EXPECT_FALSE(app.PostToUi([&executed]{ ++executed; }));

	EXPECT_EQ(g_postThreadMessageCalls, 1);

	EXPECT_EQ(executed, 0);          // 被回滚的工作**没有执行**

	// ③ 失败即关闭 ⇒ 后续提交一律 false（S4），且**不再触发唤醒**
	g_postThreadMessageCalls = 0;

	EXPECT_FALSE(app.PostToUi([&executed]{ ++executed; }));

	EXPECT_EQ(g_postThreadMessageCalls, 0);

	EXPECT_EQ(executed, 0);

}

// ── T23-11 ★★：`Run()` 的 owner-thread 前置条件（C10 / O1 / A5）──────────
void Test23RunOnOwnerThreadOnly(){

	TestApp app;   // owner = 本测试线程

	TestScopeCleanup cleanup;

	const std::thread::id owner = std::this_thread::get_id();

	// ① 非 owner 线程调用：被拒绝、**立即返回**（不进入跨线程消息泵——那会静默失效）
	int foreignResult = -1;

	std::thread foreign([&]{ foreignResult = app.Run(); });

	foreign.join();

	EXPECT_EQ(foreignResult, 0);   // ★ 沿用 0：框架无 Run 错误码约定（O1 / L4）

	// ② 正对照：owner 线程上 `Run()` **正常工作**（判据不是"永远拒绝"）
	std::atomic<int> executed{0};

	EXPECT_TRUE(app.PostToUi([&]{ executed.fetch_add(1, std::memory_order_relaxed); }));

	PostQuitToThisThread();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_EQ(executed.load(), 1);

	EXPECT_EQ(owner, std::this_thread::get_id());   // 本用例确实在 owner 线程上运行

}

}   // anonymous namespace

void ECDI::Test::RegisterApplicationDispatchTests(){

	GetTestRegistry().Add("ApplicationDispatch.SubmitBeforeRun",         &Test23SubmitBeforeRun);

	GetTestRegistry().Add("ApplicationDispatch.SubmitWhileRunning",      &Test23SubmitWhileRunning);

	GetTestRegistry().Add("ApplicationDispatch.SingleSourceFifo",        &Test23SingleSourceFifo);

	GetTestRegistry().Add("ApplicationDispatch.NoReentrancyFromCallback",&Test23NoReentrancyFromCallback);

	GetTestRegistry().Add("ApplicationDispatch.UiThreadSubmitIsAsync",   &Test23UiThreadSubmitIsAlsoAsync);

	GetTestRegistry().Add("ApplicationDispatch.QueueSurvivesWindowGone", &Test23QueueSurvivesWindowDestroyed);

	GetTestRegistry().Add("ApplicationDispatch.ExitDropsPendingWork",    &Test23ExitDropsPendingWork);

	GetTestRegistry().Add("ApplicationDispatch.ExceptionIsolated",       &Test23CallbackExceptionIsolated);

	GetTestRegistry().Add("ApplicationDispatch.ConsumerShapedPayload",   &Test23ConsumerShapedPayload);

	GetTestRegistry().Add("ApplicationDispatch.WakeFailureRollsBack",    &Test23WakeFailureRollsBack);

	GetTestRegistry().Add("ApplicationDispatch.RunOwnerThreadOnly",      &Test23RunOnOwnerThreadOnly);

}
```

**装置纪律（本组用例共同遵守，实施时逐条对照）：**

1. **工作线程内不做断言**——`TestContext` 是唯一全局、`TestResult::failures` 是 `std::vector`（B13），跨线程断言才安全的前提是"无并发访问"；本组统一改为**原子记录 + 主线程断言**。
2. **每个用例恰好一次终止 `WM_QUIT`**——要么显式补投，要么由被测的 `Exit()` 产生（T23-7）；**不重复投**。
3. **每个用例收尾 `DrainStrayMessages()`**（`TestScopeCleanup` 的析构）——清掉任何残留（多投的 QUIT、未消费的唤醒消息）。
4. **`Run()` 之前必须校验提交返回 `true`**（T23-1..T23-9 已内建）——若提交就失败，用例应在 `EXPECT` 响亮失败后自然结束，而不是进泵等一个永不到来的唤醒。
5. **不注入伪造 `WM_*`**、不手工调 `DrainUiDispatch`、不新建 `PlatformApplication` 替身（初设 §7.1）。

### △7 `ECDI/src/Tests/RunAllTests.h`

在 `:37` 之后追加一行声明（保持文件末尾 `} // namespace` 结构）：

```cpp
void RegisterApplicationDispatchTests();   ///< Phase 23：工作线程 → UI 线程投递（T23-1..T23-11）
```

### △8 `ECDI/src/Tests/RunAllTests.cpp`

在 `:35` 的 `RegisterPreshowGeometryTests();` 之后追加一行调用：

```cpp
    RegisterApplicationDispatchTests();   // Phase 23：工作线程 → UI 线程投递（T23-1..T23-11）
```

> ★ **登记与被登记的测试文件必须同批**（Phase 21 的教训）：△6 与 △7/△8 若拆批，会出现「文件在库里但注册函数不存在」的链接错误。

---

## 3. 契约 C1–C10 → 实现落点 → 测试

| # | 契约（初设 §4 原样） | 实现落点（本稿） | 覆盖 |
|---|---|---|---|
| **C1** | 成功返回只表示已入队并拥有，不表示已执行 | △5 `PostToUi` 尾 `return true`；★ 失败回滚**同临界区** | T23-1 / T23-7 / T23-10 |
| **C2** | callback 只在 owner UI 消息线程执行 | △5 `Run()` dispatch 分支 + `DrainUiDispatch()` | T23-1 / T23-2 |
| **C3** | 永不在提交调用栈内 inline 执行 | 无 fast path + 批次快照（△5 `DrainUiDispatch`） | T23-4 / T23-5 |
| **C4** | 同一投递源 FIFO；跨源不构成公共语义 | 单一 `m_dispatchQueue` + 单临界区入队 | T23-3 |
| **C5** | 队列不自动保活 `Window*` / `Application*` | 队列元素类型 = `std::function<void()>`；不读取任何窗口对象 | T23-6 + §4.3 机检 5 |
| **C6** | 关闭态拒绝新工作并丢弃未执行工作 | `CloseUiDispatch()`（`RequestExit` / 析构）+ `DrainUiDispatch` 逐项检查 | T23-7 |
| **C7** | 执行中的 callback 不被强杀；异常隔离在边界 | 逐项 `try/catch`；关闭检查在**每项之前** | T23-7 / T23-8 |
| **C8** | 与消息泵 / Timer / Window Event 分层 | △5 `Run()` 私有分支 + ★ **`hwnd == nullptr` 判别**；不进 `WindowMessageHandler` | §4.3 机检 2 / 3 + 既有 264 用例零回归 |
| **C9** | 唤醒失败不留永远不消费的已接受工作 | △5 `PostToUi` 失败分支：**就地**清队列 + 置 `closing`（★ **不得**调 `CloseUiDispatch()`） | **T23-10** + §4.3 机检 1/2 |
| **C10** | `Run()` 必须在构造 owner thread 执行 | △5 `Run()` 入口前置条件（`Error` + `return 0`） | **T23-11** + §4.3 机检 8 |

> ★ **口径提醒（承初设 §4）**：C1 与 C9 **必须合读**——`true` 是**不可撤回**的承诺，所以「清队列」只允许发生在**与入队同一临界区**内。C4 只承诺单源 FIFO，不把 `std::deque` 的互斥量线性化顺序包装成跨线程业务排序；C7 只承诺执行边界，**不替调用方**保证其捕获对象仍然存活。

---

## 4. 关键行为冻结与盯防清单

### 4.1 全链调用序列（工作线程提交 → UI 线程执行）

```
[工作线程]  Application::PostToUi(work)
              └─ △2 薄转发（std::move，不 catch）
                   └─ Win32PlatformApplication::PostToUi(work)
                        ├─ 空工作？                        ⇒ return false            （S3）
                        ├─ lock(m_dispatchMutex)
                        ├─ closing？                       ⇒ return false            （S4）
                        ├─ queue.emplace_back(work)
                        ├─ wakePending？                   ⇒ return true             （S2）
                        ├─ m_postThreadMessage(ui, kUiDispatchMessage, 0, 0)
                        │    ├─ 失败 ⇒ wakePending=false · queue.clear() · closing=true
                        │    │          · Log(Error)     ⇒ return false            （S5）
                        │    └─ 成功 ⇒ wakePending=true  ⇒ return true             （S1）
                        └─ unlock

[UI 线程]  Run()  → GetMessageW 取到 (hwnd==NULL, message==kUiDispatchMessage)
              └─ DrainUiDispatch()
                   ├─ lock → batch.swap(queue) → wakePending=false → unlock          （S6 前半）
                   ├─ for each work in batch:
                   │    ├─ closing？ ⇒ break（丢弃本项及其余）                        （S6 后半）
                   │    └─ try { work(); } catch(…) { Log(Error); }
                   └─ batch 析构（用户析构函数在锁外）

[退出]     Exit()/最后窗口关闭 → RequestExit() → CloseUiDispatch() → PostQuitMessage(0)  （S7）
[析构]     ~Win32PlatformApplication() → CloseUiDispatch() → 既有四步清理
```

### 4.2 状态机 S1–S7 → 代码行（初设 §3.4 表的落地对照）

| # | 状态转换 | △5 中的落点 | 返回 |
|---|---|---|---|
| **S1** | `closing=false` / `wakePending=false` + 唤醒成功 | `PostToUi` 尾两行 | `true` |
| **S2** | `wakePending=true` 时继续入队 | `PostToUi` 的 `if (m_dispatchWakePending) return true;` | `true` |
| **S3** | 空 `std::function` | `PostToUi` 首段（**锁外**） | `false` |
| **S4** | `closing=true` 时提交 | `PostToUi` 的 `if (m_dispatchClosing) return false;` | `false` |
| **S5** ★★ | 唤醒失败 ⇒ `closing=true` + 清队列 | `PostToUi` 失败分支（★ **与 S1 同一临界区**） | `false` |
| **S6** | drain 快照 + 逐项检查 | `DrainUiDispatch` 前半（锁内 swap）/ 后半（锁外逐项） | — |
| **S7** | `CloseUiDispatch()`（幂等） | `RequestExit()` · 析构 · （★ **不**在失败分支调用） | — |

### 4.3 ★★ 盯防清单（实施后**逐条机检**，全部可机械判定）

| # | 判据 | 检查方式 |
|---|---|---|
| **1** ★★ | `m_postThreadMessage(` 在 `Win32PlatformApplication.cpp` 出现 **恰好 1 次**，且该行位于 `std::lock_guard lock(m_dispatchMutex);` **之后**、`PostToUi` 闭括号**之前**（= 唤醒在临界区内） | `grep -n "m_postThreadMessage("` 数行号并比对上下文 |
| **2** ★★ | `CloseUiDispatch()` 在该文件出现 **恰好 3 次**（1 处定义 + `RequestExit` + 析构）⇒ 唤醒失败分支**没有**第 4 处调用（自死锁防线） | `grep -c "CloseUiDispatch()"` |
| **3** | `kUiDispatchMessage` 出现 **恰好 2 次**（匿名 namespace 定义 + `Run()` 分支），定义处带 `WM_APP + 1` / `+ 2` 的占位登记注释 | `grep -n "kUiDispatchMessage"` |
| **4** | `std::lock_guard` 出现 **恰好 3 次**（`PostToUi` / `DrainUiDispatch` / `CloseUiDispatch`）；★ callback 执行区（`for` + `try`）**不在**任何 `lock_guard` 作用域内 | `grep -n "lock_guard"` + 人工看花括号范围 |
| **5** | 公共面零 Win32：`Application.h` / `PlatformApplication.h` 内 `HWND` / `UINT` / `WPARAM` / `LPARAM` / `Windows.h` **零命中**；`PostToUi` 签名只用 `std::function<void()>` | `grep -nE "HWND\|WPARAM\|LPARAM\|Windows.h"` 两文件 |
| **6** ★ | `m_dispatchClosing` 声明为 **`std::atomic<bool>`**；`m_dispatchWakePending` 为普通 `bool`（D-2） | `grep -n "atomic<bool> m_dispatchClosing\|bool m_dispatchWakePending"` |
| **7** | 测试登记三件事齐备：`RunAllTests.h` +1 声明 · `RunAllTests.cpp` +1 调用 · 新建文件存在；且 `GetTestRegistry().Add(` 全库求和 = **275** | `grep -c "GetTestRegistry().Add(" ECDI/src/Tests/*.cpp` 求和 |
| **8** | `Application::Exit()` **未加**线程守卫（N9 不得越范围）；`Run()` 的错线程分支**不含** `FRAMEWORK_ASSERT` | `grep -n "FRAMEWORK_ASSERT" Win32PlatformApplication.cpp` ⇒ 期望 **0 命中** |
| **9** | 零回归：`git diff --name-only` **不含** `Win32PlatformWindow.cpp` / `WindowMessageHandler.cpp` / `AnimationManager.*`；`TimerEvent` 路径零改动 | `git diff --name-only` |
| **10** | `PostThreadMessageW` 在**生产代码**中只出现 **1 次**（△5 的适配器内部）；测试侧的出现属于装置 | `grep -rn "PostThreadMessageW" ECDI/src --include=*.cpp` |

---

## 5. 验收（A1–A10 → 本稿用例）

### 5.1 A → T 映射（★ 无孤儿验收项、无孤儿用例）

| 验收 | 判据 | 用例 |
|---|---|---|
| **A1** 线程归属 | 提交线程 ≠ 执行线程，且执行线程 = 消息泵线程 | T23-1 / T23-2 |
| **A2** 异步不阻塞 | 提交在 UI 未消费时即返回；返回时未执行 | T23-1 / T23-5 |
| **A3** 公共面不泄漏平台细节 | 公共头无 HWND / Win32 类型；应用不需 `PostMessageW` | §4.3 机检 5（结构审查） |
| **A4** 顺序契约 | 单源 A→B→C | T23-3 |
| **A5** 生命周期安全 | `Run()` 前 / 中 / `Exit()` 后 / 窗口销毁 / 析构 | T23-1 / T23-6 / T23-7 / T23-11 |
| **A6** 真实消费者可接入 | 消费者形态小载荷走通 | T23-9 |
| **A7** 既有 Timer 零回归 | 现有 264 用例保持通过 + dispatch 消息不进窗口翻译器 | 全量既有套件 + §4.3 机检 9 |
| **A8** 四工具链可测 | MSVC / ClangCL / Clang / MinGW 均构建并运行新增用例 | 用户侧执行（§5.3） |
| **A9** 无同步重入 | A 内提交 B，B 不在 A 返回前执行 | T23-4 |
| **A10** UI 线程调用语义一致 | 提交返回时未执行；关闭后拒绝 | T23-5 / T23-7 |

### 5.2 用例锚点

**264 → 275**（新增 **11** 个注册条目：`ApplicationDispatch.*`）。★ 相对初设 v1.2 的 **273** 为 **+2**，原因见 §1.3 **D-1**（C9 与 C10 各补 1 条）。

### 5.3 验证纪律（由用户在 VS / CLion 执行，AI 不代跑）

报告必须逐项写明：① **四链通过数**（MSVC / ClangCL / Clang / MinGW，`ecdi_tests`）；② **断言是否启用**（MSVC 系查 `/MDd`、GNU 系查 `-D_DEBUG`）；③ **本机显示器缩放**（本组以线程 / 消息为主，仍沿用项目全量纪律）。★ 若出现 **flaky**，**先查测试同步与消息泵时序**（本组无 `Sleep`，出现不稳定即指向实现或装置缺陷），不得用重复运行掩盖。

---

## 6. 影响面

| 项 | 结论 |
|---|---|
| Public 头 | **92 → 92**（不新增公共头） |
| Public API | **+2**（`Application::PostToUi` · `PlatformApplication::PostToUi`）；1.0 前不承诺 ABI 兼容 |
| 生产文件 | **5**（2 公共头 + 1 应用实现 + 1 平台内部头 + 1 平台实现） |
| 测试文件 | **3**（新建 1 + 登记 2）；★ 无 CMake 改动 |
| 用例 | **264 → 275**（+11） |
| 断言特征串 | **11 → 11**（★ 不新增 `FRAMEWORK_ASSERT`——O1 定案） |
| 设计文档 | **147 → 148** |
| 新增 Event / 平台对象 / 构建配置 | **0 / 0 / 0** |
| 运行时资源 | 1 `mutex` + 1 `deque` + 1 `atomic<bool>` + 1 `bool` + 1 `DWORD` + 1 函数指针；**无线程、无定时器、无第二队列** |
| 主要风险 | 关闭竞态（已由同临界区消解）· 唤醒失败处置 · callback 异常 · 用户捕获对象生命周期（**框架不承担**） |

---

## 7. 实施顺序（**单批**）

**△1–△8 必须同批**：△3 的纯虚与 △4 的 override 缺一即**编译失败**；△1/△2 与 △3/△4/△5 是同一接口的两端；△6/△7/△8 是「测试承载 + 登记」，**拆批即链接失败**（Phase 21 教训）。

建议顺序（同一批内）：△3 → △4 → △5 → △1 → △2 → △6 → △7 → △8，随后按 §5.3 在四链验证。

**实施后回写**（收口时）：`docs/README.md` 的 §Phase23 段与「当前」行 · 根 `README.md` 的 Status 表 **23** 行 · 审计 `framework-defect-audit.md`（D-4 状态 + 修订记录）· `roadmap-deferred.md`（顺位 ④ + 修订记录）· `desktopnest-roadmap.md`（G-3）· 本文件 §7.1 实施记录。

---

## 8. 局限（L1–L6，如实记录）

| # | 局限 | 说明 / 处置 |
|---|---|---|
| **L1** ★★ | **唤醒失败的并发交错无法自动化** | T23-10 只能验「失败 ⇒ 拒绝 + 不留下已接受工作」的**单线程**形态（含缝被调用过的正对照）；「Worker B 在 A 失败前已 `return true`」这条**时序交错**由 §4.3 机检 **1 / 2**（结构判据：同一临界区、失败分支不二次加锁）承担，**不由运行时用例承担**。★ 承初设 §1.5 补记 ① 的诚实口径 |
| **L2** | 「队列已清空」不可从外部观测 | 关闭态下 `DrainUiDispatch` 本就丢弃 ⇒ 「清空」与「未清空但关闭」**行为不可区分**。故 T23-10 的判据 = **返回值 + 未执行 + 后续被拒**，而非队列长度 |
| **L3** ★ | `Application::Exit()` 仍**非线程安全**（N9 明确排除） | 工作线程必须 `PostToUi([...]{ Exit(); })`。★ **建议记账**（`roadmap-deferred.md`：`m_running` 的跨线程写）——本稿**不改账本**，留待实施收口时回写 |
| **L4** | `Run()` 无错误码约定 ⇒ 错线程返回 `0`（与正常退出同形） | 只能靠 `Error` 日志区分（`Win32PlatformApplication.cpp` 的 `Run()` 入口）。若将来需要外部可判的失败信号，须先有消费者（YAGNI） |
| **L5** | T23-8 不断言日志 | `Logger` 无可注入 sink（B11）；日志存在性 = 代码审查 + 人工可见（O4） |
| **L6** | 测试装置与实现存在一处**耦合点** | 装置依赖「唤醒消息 = **线程消息**、与 `WM_QUIT` **同队列且 FIFO**」；若实现改回**窗口消息**唤醒（初设 §3.3 已禁），本装置（T23-1..T23-9 / T23-11）须同步修改。如实记录，防"装置随实现漂移却无人知" |

---

## 9. 修订记录

- **v1.0**（2026-09-29）**详细设计初稿**。输入 = 需求 **v1.1** · 初设 **v1.2**（P0 竞态已修）。产出：**逐文件改动 △1–△8**（★ 8 文件 = 生产 5 + 测试 3，新建 1；**文件数与初设一致**）· **代码基线 B1–B14**（全部带行号实测）· **契约 C1–C10 → 落点 → 测试** 全映射 · **状态机 S1–S7 → 代码行对照** · **盯防清单 10 条**（全部可机检）· **用例正文 T23-1..T23-11**（含装置规则 5 条）· **A1–A10 → T 全映射** · 影响面 · 实施顺序 · **局限 L1–L6**。★ **对初设的 6 处修正 / 细化（§1.3）**：**D-1** 用例 273 → **275**（补 C9 的回滚覆盖与 C10 的拒绝覆盖，C10 的测试映射由 T23-2 订正为 T23-11）· **D-2** ★★ `m_dispatchClosing` 必须为 **`std::atomic<bool>`**（drain 在**锁外**逐项读它，而工作线程会写它）· **D-3** 新增**内部测试缝** `SetPostThreadMessageSeamForTests`（先例 `SetShellSeamsForTests`，不改公共 API）· **D-4** V6 定案（`std::bad_alloc` 可能传播，写进公共头注释）· **D-5** ★★ 补出**装置级缺陷**：每线程消息队列**跨用例共享** ⇒ 「每个 `Run()` 恰好一次终止 `WM_QUIT`」+「用例收尾清残留」，否则残留 `WM_QUIT` 会让**下一个**用例假失败 · **D-6** 失败分支的 `Logger::Log` **留在锁内**并声明理由（实测 `Core/Logger.cpp:35-51` 不执行用户代码）。★ **闭合初设 O1–O6 与 V1–V6**（§1.5），其中 **O1** 定案「**不加断言**、`Error` + `return 0`」（口径先例 = `Win32PlatformWindow.cpp:1038-1048`）。★ **不修改任何源码 / 测试代码，不运行测试**。
