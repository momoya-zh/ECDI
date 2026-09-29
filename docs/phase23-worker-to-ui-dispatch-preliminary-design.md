# Phase 23 · 工作线程 → UI 线程投递（worker-to-UI dispatch）——初步设计（v1.2）

> 来源：`docs/phase23-worker-to-ui-dispatch-requirements.md` **v1.1**（外部评审 v1.0 已通过；v1.1 已吸收评审补强，待复评）· 审计 `framework-defect-audit.md` **D-4** · `roadmap-deferred.md` §7.9 顺位 ④。
> 状态：**v1.2 待复评**（2026-09-29）——★ v1.2 吸收第二轮设计评审：**采纳其 P0 并发竞态（「唤醒失败 + 并发提交」）的定性、但修正其修法**（把唤醒移入临界区 + 失败**就地**回滚，而非新增第 4 个状态，见 §1.5 / §3.2 / §3.4）；v1.1 已吸收首轮 5 条补强（见 §1.4）；本稿是接口 / 数据流 / 生命周期的初步设计，不修改源码，不进入详细设计级的逐行实现规格。
> 本稿的核心结论：**新增一个最小的 `Application::PostToUi(std::function<void()>)` 公共入口；平台接缝由 `PlatformApplication` 承担；Win32 实现用 UI 线程消息队列唤醒，队列和唤醒消息均留在平台实现内部；callback 永不 inline 执行。**
> 设计输入复核：当前 `Win32PlatformApplication::Run()` 的消息泵为 `GetMessageW` → `TranslateMessage` → `DispatchMessageW`（`src/Platform/Win32/Win32PlatformApplication.cpp:26-46`）；`PlatformApplication` 已有 `std::function` sink 与应用级能力扩展惯例（`include/ECDI/Platform/PlatformApplication.h:19-28,30-34,70-100`）；生产代码现有 `PostMessageW` 仅服务托盘菜单 / Desktop 跟随两个具体功能。

---

## 1. 范围映射：需求 → 设计域

### 1.1 R / D → 本稿落点

| 需求 / 决策 | 初步设计落点 | 说明 |
|---|---|---|
| **R1 / A1** UI 线程执行 | `Win32PlatformApplication::Run()` 处理私有线程消息；`DrainUiDispatch()` 只在 `Run()` 所在线程调用 | 不新建 dispatcher thread；执行线程 = 构造 `Win32PlatformApplication` 的 owner thread = `Run()` 线程 |
| **R2 / R9 / R10 / D0 / D7** 异步、无 inline | `Application::PostToUi` 只转发；平台层入队并唤醒；callback 只在后续 drain 执行 | 即使调用者已经在 UI 线程，也走同一入队路径；禁止 `if (IsUiThread()) callback()` fast path |
| **R3 / D1** 应用级接缝 | `PlatformApplication::PostToUi` + `Application::PostToUi` | 不把投递绑定到 `PlatformWindow`，不暴露 HWND |
| **R4** 载荷所有权 | `std::function<void()>` **按值进入 API、移动进平台队列** | 队列拥有待执行工作；调用方不得传引用捕获的栈对象作为隐式生命周期协议 |
| **R5 / D5** 默认不绑定窗口 | callback 本身不携带 `Window*` 参数，平台队列不保存窗口指针 | 若消费者未来需要窗口上下文，另立稳定 token / 失效判定，不在本阶段偷偷加入 |
| **R6 / D4** 顺序 | 单一 `std::deque` + mutex 保护；同一投递源的入队顺序保持 | 不对外承诺多个工作线程之间的业务顺序；实现内部按 mutex 临界区的入队顺序消费 |
| **R7 / D6** 生命周期 | `RequestExit()` 先关闭 dispatch，再 `PostQuitMessage`；平台析构先关闭 dispatch，再清理托盘资源 | 已排队但未取出的工作丢弃；正在执行的单个 callback 不被强行中断，但其后续批次停止 |
| **R8 / A8** 可测 | 新增真实 `Application` + `std::thread` 测试文件，走消息泵，不注入伪造 Event | 测试记录三个线程 ID、入队返回值、执行顺序、重入状态与关闭状态 |
| **Q6** callback 异常 | 平台 drain 对每个 callback 单独 `try/catch`；记录 Error 后继续后续工作 | 只处理 callback 执行异常；`std::function` 入队时的分配异常不伪装成可恢复的业务结果，详设再冻结 |
| **Q9 / A9** 非重入 | drain 先把当前队列搬到本地批次，再逐项执行；callback 中的新提交只产生后续唤醒 | 当前 callback 返回前，新提交的 callback 不会递归进入 |

### 1.2 选 `PostThreadMessageW`，不复用窗口 HWND

初步设计选择 **Win32 UI 线程消息队列 + `PostThreadMessageW`** 作为唤醒载体，理由按优先级排列：

1. **不依赖任何窗口生命周期**：G-3 的消费者可能没有窗口，也可能在多个窗口之间共享结果；
2. **不增加隐藏宿主**：当前托盘宿主是懒创建、带 Shell 生命周期契约的具体功能资源（`Win32PlatformApplication.cpp:96-142`），不应为了 G-3 强制创建或把普通 dispatch 绑到托盘；
3. **不复用现有私有消息**：托盘 `WM_NULL` 与 Desktop 跟随消息各有自己的状态机，不能把它们扩成公共队列；
4. **消息线程自然归属**：`GetMessageW(nullptr, ...)` 已经接收线程消息（现有消息泵 `:28-46`），只需在 `TranslateMessage` / `DispatchMessageW` 前识别一个平台内部保留的 dispatch 消息；
5. **公共层零 Win32**：消息号、线程 ID、`PostThreadMessageW` 全部留在 `src/Platform/Win32`。

> 这不是在需求阶段把某个消息号写成公共契约。这里只冻结「第一版采用线程消息唤醒，不创建新的隐藏窗口」这一实现方向；消息值、失败日志与测试观察方式留到详细设计按实际源码位置钉死。

### 1.3 为什么不把队列放进 `PlatformApplication` 公共基类

队列的**语义**由 `PlatformApplication` 接缝承载，但第一版的**存储**放在 `Win32PlatformApplication`：

- 公共基类目前只保存平台无关的 `std::function` sink（`SetDeferredCleanup` / `SetTrayEventSink`），不需要因一个 Win32 唤醒实现把 `<mutex>` / `<deque>` 和线程队列状态带进公共头；
- 当前唯一平台实现是 Win32；X11 / Wayland 仍是 YAGNI 接口，不为不存在的实现者预建共享队列；
- 若未来第二个平台出现，`PlatformApplication::PostToUi` 仍是同一语义接缝，但它可以选择自己的唤醒机制与队列存储。

这与「平台接口表达能力、平台实现持有资源」的既有分工一致，不是新增一个 `Dispatcher` 对象层。

### 1.4 本稿评审处置（2026-09-29 · v1.0 → v1.1）

> 设计评审结论：**v1.0 方向与质量通过**——`PostToUi` 最小入口 + `PlatformApplication` 应用级接缝 + `PostThreadMessageW` 唤醒 + 平台内部队列，均符合账本纪律（YAGNI · 每个 Win32 API 唯一归属 · 应用级接缝 = `PlatformApplication → Application`）；本稿的源码引用自检**全部命中、无腐坏**。采纳 5 条补强进入 v1.1，**无架构性推翻**。

| # | 评审发现 | 处置 |
|---|---|---|
| **R-①** ★★ | §3.3 的 `Run()` 分支判据**只比消息号、未比 `hwnd`**；本仓库已占用 `WM_APP + 1`（托盘回调，`Win32PlatformApplication.cpp:20`）与 `WM_APP + 2`（Desktop 跟随，`Win32PlatformWindow.cpp:73`），且测试会**向窗口**投递 `WM_APP + 2`（`DesktopLayerTests.cpp:532`）⇒ 消息号一旦碰撞，该分支会把窗口消息**静默吞掉**（不再走 `DispatchMessageW`） | ✅ **采纳**——判据改为 `message.hwnd == nullptr && message.message == kUiDispatchMessage`（线程消息的 `hwnd` **恒为 NULL**）；§3.3 补**消息号占位登记纪律**；§4 的 **C8** 与 §7.3 的**结构验收项 8** 同步 |
| **R-②** ★ | §5 影响面**缺用例锚点**（项目惯例以用例数作规模锚点） | ✅ **采纳**——§5 新增「用例」行：**264 → 273**（T23-1..T23-9 = 9 条） |
| **R-③** ★ | 需求 **A6**（真实消费者形态载荷）在 T23 表内**无对应用例** ⇒ **A→C→T 可追溯性缺口**，一条验收项无人认领 | ✅ **采纳**——新增 **T23-9**（「异步解码完成通知」样式的小载荷） |
| **R-④** | `PostToUi` 返回 `false` 存在**三义合流**（空工作 / 关闭中 / 唤醒失败），与 **O5** 同源 | ✅ **采纳**——§8 的 **O5** 补实证；详设须给出 Debug 断言或明确区分 |
| **R-⑤** | 「退出路径是单一漏斗」的实测证据未写入初设 | ✅ **采纳**——§3.5 补：生产 `PostQuitMessage` **仅 1 处** + 隐式退出亦经 `Application::Exit()` |

### 1.5 第二轮评审处置（2026-09-29 · v1.1 → v1.2）

> 评审结论：**初设整体通过**，但指出一处 **P0 并发竞态**（「唤醒失败 + 并发提交」），要求在详细设计前修掉。★ 本稿**采纳其问题定性、修正其修法**——评审建议的「新增第 4 个状态 `WakeFailed`」**拦不住已 `return true` 的在途提交者**（见 §3.2 的交错表），故真正修法是**收紧原子粒度**：把 `PostThreadMessageW` 移入同一临界区、失败**就地**回滚。**无架构性推翻。**

| # | 评审发现 | 处置 |
|---|---|---|
| **P-①** ★★ | **P0 并发竞态**：§3.2 伪代码在**解锁之后**才调 `PostThreadMessageW`，失败再调 `CloseUiDispatch()` 清队列 ⇒ **Worker A 的唤醒失败会清掉 Worker B 已 `return true` 的工作**（B 在 A 解锁后、A 失败前入队，看到 `wakePending == true` ⇒ 不唤醒、直接返回 `true`） | ✅ **采纳问题、修正修法**——**不是**加第 4 状态：`WakeFailed` 只能约束**失败记录之后**的提交者，而**已经返回的 `true` 撤不回来**；★ 且 `Closing` **已足以表达失败态**（失败即关闭），另立状态属**语义重复**。**真修法 = 把 `PostThreadMessageW` 移入同一临界区**——「入队 → 发唤醒 → 失败回滚」**同一把锁内**完成（§3.2 重写 + §3.4 增状态转换表）；★ 失败时**就地**写 `m_dispatchClosing = true` + 清队列，**不得**调用 `CloseUiDispatch()`（它会**二次加锁** ⇒ 自死锁） |
| **P-②** | §3.2 关键点 4 把**性质相反**的两件事并列（「锁不包住平台 API 或 callback 执行」） | ✅ **采纳**——**拆成两句**：`callback` = **用户代码** ⇒ **必须出锁**（用户反向 `PostToUi` 会二次加锁）；`PostThreadMessageW` = **内核快调用、不执行用户代码** ⇒ **必须在锁内**（出锁就是 P-① 的 bug） |
| **P-③** | 「`Run()` 必须在**构造**线程执行」被当成既有事实陈述 | ✅ **采纳**——§3.1 明确这是**本阶段新增的前置条件**（由 R1 / R10 与 T23-1「`Run()` 前提交也须有效」共同**蕴含**），并补**影响面实测**：全库构造点与 `Run()` 调用点**全部同线程** ⇒ **零既有用法受影响** |
| **P-④** | T23-6 的期望句「测试只验证队列不保存隐式窗口指针」**不可执行**（无法从外部观测"队列里有没有指针"） | ✅ **采纳**——T23-6 重定位为**生命周期解耦**用例（窗口销毁后 callback 仍执行、框架不代调用方访问捕获对象）；「队列不保存 `Window*`」改由 **§7.3 结构审查**承担 |
| **P-⑤** | O2 只问「是否全部丢弃 / 是否保留错误原因」，**未含状态的原子粒度** | ✅ **采纳**——O2 补「**状态转移的原子粒度**」（入队与唤醒是否必须同一临界区；失败进入 `closing` 后是否允许重开） |
| **P-⑥** | O5 建议**现在就**把 `bool` 升级为状态枚举 | ⛔ **不采纳（维持）**——**有分支需求的消费者尚不存在**（条 22「第二个真实消费者才抽象」）；本阶段处置 = 详设给 **Debug 断言**或**明确区分**，§8 O5 **原文维持**、仅补与 §3.2 的交叉引用 |

★ **本稿补记两条审查外发现**（第二轮评审未提，自查所得）：

1. **P-① 修复路径本阶段无自动化覆盖**——「唤醒失败 ⇒ 就地回滚」需要一个**可注入的唤醒失败**才能被测，而 T23-8 的该分支是**条件式**（「若可注入」）。⇒ 详设必须**显式选择**：复用既有 `ForTests` 缝先例（`SetShellSeamsForTests` / `SetDragFinishForTests`）注入失败，或**如实标注「仅代码审查」**（条 40 的表述纪律——不得让"没测到"看起来像"测到了"）。
2. **构造期新增线程亲和副作用**——`GetCurrentThreadId()` + `PeekMessageW(PM_NOREMOVE)` 使**构造线程**成为消息队列的 owner（并**创建**了该线程的消息队列）。这不是"加了两个调用"，而是**给构造期引入了一项新的线程归属语义**；已并入 P-③ 的影响面实测（全库构造点与 `Run()` 同线程 ⇒ 零破坏）。

---

## 2. 头文件草案（接口完整增量，未进入逐行详设）

### 2.1 `include/ECDI/Application/Application.h`

**改动 1：**标准库 include 区新增 `<functional>`。

**改动 2：**在 `Application::Create()` 之后、`Exit()` 之前增加一个应用级公共入口；其余 public / protected / private 声明保持现状。

```cpp
#include <functional>

// ... namespace ECDI{

class Application : public EventRouter{

public:

    // 既有构造 / 析构 / Run / Create 保持现状

    /// @brief 将工作异步提交到本 Application 的 UI 消息线程
    /// @param work 待执行工作；按值接收，平台队列取得其所有权
    /// @return true = 已接收入队；false = 已进入关闭阶段或唤醒失败
    /// @details 无论调用者是否已经在 UI 线程，均不会在本调用栈内执行 work。
    ///          不绑定 Window，不暴露 HWND 或 Win32 消息类型。
    bool PostToUi(std::function<void()> work);

    /// @brief 请求退出消息循环
    void Exit();

};
```

> `PostToUi` 的 callback 参数允许捕获应用层自己的模型 / 状态，但**不提供**框架层自动保活。调用方若捕获 `Application*`、`Window*` 或其它对象，仍须自行保证其在 callback 执行时有效；本阶段不造 `WeakHandle`。

### 2.2 `include/ECDI/Platform/PlatformApplication.h`

该文件已有 `<functional>`（当前 `:6`），只追加一个纯虚能力；不新增平台消息类型、不新增接缝类。

```cpp
class PlatformApplication{
public:
    virtual ~PlatformApplication() = default;

    // 既有 Run / RequestExit 保持现有顺序与语义

    /// @brief 异步提交工作到平台 UI 消息线程
    /// @return true = 已入队；false = 平台已关闭或唤醒失败
    /// @details 平台实现负责线程安全入队与唤醒；不得在调用栈内执行 work。
    virtual bool PostToUi(std::function<void()> work) = 0;

    // 既有托盘 / DPI 能力保持现状
};
```

**命名冻结：**使用 `PostToUi`，不使用 `InvokeOnUiThread`（暗示同步）、`Dispatch`（容易与现有 EventRouter / Renderer 语义撞名）、`IUiDispatcher`（投机抽象 + `I` 前缀）。

### 2.3 `src/Platform/Win32/Win32PlatformApplication.h`

这是内部实现头，可以包含 Win32 类型与队列实现成员；公共头不带这些成员。

```cpp
#include <deque>
#include <functional>
#include <mutex>

class Win32PlatformApplication final : public PlatformApplication{
public:
    Win32PlatformApplication();
    ~Win32PlatformApplication() override;

    int Run() override;
    void RequestExit() override;
    bool PostToUi(std::function<void()> work) override;

    // 既有 DeclareDpiAwareness / Tray 能力与测试观察保持现状

private:
    void DrainUiDispatch();
    void CloseUiDispatch() noexcept;

    DWORD m_uiThreadId = 0;              ///< 构造线程；Run 必须在此线程执行
    std::mutex m_dispatchMutex;         ///< 保护队列、关闭态、唤醒态
    std::deque<std::function<void()>> m_dispatchQueue; ///< UI 待执行工作
    bool m_dispatchClosing = false;     ///< true 后拒绝新工作
    bool m_dispatchWakePending = false; ///< 已有一个唤醒消息或正在 drain

    // 既有托盘成员保持原位置与声明顺序
};
```

**成员落点纪律：**新增 dispatch 成员作为一组放在既有平台状态成员之前；不把 `m_uiThreadId` 混进托盘状态机，不改变既有托盘析构顺序。

---

## 3. 实现分解

### 3.1 owner thread 与消息队列初始化

`Win32PlatformApplication::Win32PlatformApplication()` 不再 `= default`，在构造线程记录 `GetCurrentThreadId()`，并用 `PeekMessageW(..., PM_NOREMOVE)` 确保该线程已经拥有消息队列。

★ **v1.2 定性：`Run()` 必须在「构造 `Win32PlatformApplication` 的同一线程」执行——这是本阶段新增的前置条件，不是既有事实的重述。**

| 维度 | 说明 |
|---|---|
| **为什么新增** | 由需求 **R1 / R10**（执行线程 = UI 消息线程）与 **T23-1**（`Run()` **之前**提交也须有效）共同**蕴含**：唤醒载体是 `PostThreadMessageW(threadId, ...)`，它把消息投给**线程自己的消息队列** ⇒ **队列归属哪个线程，`GetMessageW` 就只能在哪个线程取到** |
| **与既有口径的差别** | 此前**没有**这一约束——构造与 `Run()` 分属两个线程**不会被任何检查发现**（消息队列尚未被框架使用，`Run()` 的 `GetMessageW(nullptr, ...)` 也不校验线程）。本阶段起由 `m_uiThreadId` 记录，并在 `Run()` 入口校验 |
| **影响面（实测，2026-09-29）** | 全库的构造点与 `Run()` 调用点**全部同线程**：`examples/MinimalApp/main.cpp:9,16` · `examples/VisualTest/main.cpp:189,209` · `examples/ModelProbe/main.cpp:198,344`；测试侧 `Application app;`（`WindowChromeTests.cpp` ×7 · `WindowBackgroundTests.cpp:54`）与直接构造的 `Win32PlatformApplication app;`（`TrayTests.cpp` ×5）**均不调用 `Run()`** ⇒ 新前置条件对它们**空成立** ⇒ **零既有用法受影响** |
| **为什么不"自动兼容跨线程"** | 让跨线程也成立需要**结构性代价更大**的方案（隐藏宿主窗口 + 该窗口的消息泵，或把队列搬到共享存储 + 另一套唤醒），二者都会把「一个 UI 线程」的模型复杂化，而当前**没有第二个消费者**（YAGNI） |
| **构造期的连带副作用** | `GetCurrentThreadId()` + `PeekMessageW(PM_NOREMOVE)` 会**创建**构造线程的消息队列 ⇒ 「构造即确立线程归属」。这不是"顺手多两个调用"，而是给构造期**新增一项线程亲和语义**（见 §1.5 补记 ②） |

设计契约：

- `Application` 与 `Win32PlatformApplication` 在同一线程构造（★ 由 `Application::Application()` 的成员初始化列表 `m_platformApplication(std::make_unique<Win32PlatformApplication>())` 结构性保证，`Application.cpp:29-30`）；
- `Application::Run()` 必须在该线程调用；
- 工作线程只调用 `PostToUi()`，不调用 `Run()`、`DrainUiDispatch()` 或任何 Win32 UI API；
- owner thread 校验失败的具体诊断 / 返回码列入开放点 O1，不能在实现时无声跨线程运行消息泵。

### 3.2 `PostToUi` 入队与单次唤醒

★★ **v1.2 修正的竞态（P-①）：原写法把「发唤醒」放在锁外，失败时清队列会波及已返回 `true` 的在途提交者。**

v1.1 的伪代码是「入队 → 解锁 → 发唤醒 → 失败则 `CloseUiDispatch()` 清队列」。它存在这条交错：

| 时刻 | Worker A | Worker B | 共享状态 |
|---|---|---|---|
| **t0** | 入队 `a`，见 `wakePending == false` ⇒ 置 `true`，**解锁** | — | `queue = [a]` · `wakePending = true` |
| **t1** | （尚未调用 `PostThreadMessageW`） | 入队 `b`，见 `wakePending == true` ⇒ **不唤醒**，`return true` | `queue = [a, b]` |
| **t2** | `PostThreadMessageW` **失败** ⇒ 清空队列 | — | `queue = []` ⇐ ★ **`b` 已被 `true` 承诺过，却被丢弃** |

⇒ **B 拿到的 `true` 无法撤回。** 任何"再补一个状态"的方案（如第 4 态 `WakeFailed`）只能约束 **t2 之后**的提交者，**在途者照旧被丢** —— 这违反 **C1**（`true` = 已入队并拥有）。

★ **正解不是扩展状态机，而是收紧原子粒度**：把「入队 → 发唤醒 → 失败回滚」收进**同一个临界区**，于是 **t1 不再存在**（B 要么在 A 完成全部动作**之前**拿锁——那 B 会**自己**发唤醒；要么**之后**——那 B 看到 `closing` 直接拿 `false`）。

伪代码只表达状态转移，不是详设的可直接复制实现：

```cpp
bool Win32PlatformApplication::PostToUi(std::function<void()> work){
    if (!work){
        return false;                 // 空工作不入队（且不进临界区）
    }

    std::lock_guard lock(m_dispatchMutex);   // ★ 入队 / 唤醒 / 失败回滚：同一临界区

    if (m_dispatchClosing){
        return false;                 // 含"上次唤醒失败后已就地关闭"的情形
    }

    m_dispatchQueue.emplace_back(std::move(work));

    if (m_dispatchWakePending){
        return true;                  // 已有在途唤醒消息：入队即视为完成
    }

    // ★★ 唤醒在锁内发出（v1.2 修 P-①）：内核快调用、不执行用户代码 ⇒ 可持锁。
    if (PostThreadMessageW(m_uiThreadId, kUiDispatchMessage, 0, 0) == FALSE){
        // 唤醒失败 = 这批工作永远不会被 drain ⇒ 就地回滚本临界区的全部后果。
        // ⚠️ 不得调用 CloseUiDispatch()——它会二次加锁 ⇒ 自死锁。
        m_dispatchWakePending = false;
        m_dispatchQueue.clear();
        m_dispatchClosing = true;
        return false;
    }

    m_dispatchWakePending = true;
    return true;
}
```

关键点：

1. **先入队、后唤醒**，避免 callback 在消息到达时还不在队列；
2. `m_dispatchWakePending` 只让一批连续提交共享一个唤醒消息，避免每个文件变化都制造一条平台消息；
3. ★★ **入队与唤醒在同一临界区内**（v1.2 修 P-①）——`PostThreadMessageW` 失败时**就地**清队列 + 置关闭态，使「清队列」不可能波及**已 `return true`** 的提交者：因为**不存在**"在锁外、失败发生前"能入队的第三个提交者（交错表的 t1 时刻已被消除）；
4. ★★ **两条纪律的性质相反，不可并列成一条**（v1.2 拆分 P-②）：
   - **`PostThreadMessageW` 必须在锁内**——它是**内核快调用、不执行用户代码**；放到锁外才是 bug（即 3 的竞态）；
   - **callback 执行必须在锁外**——它是**用户代码**：用户在自己的 callback 里反向 `PostToUi` 会**二次加锁**，锁内执行即**自死锁**（见 §3.4 步骤 4 → 5：先解锁、再逐项执行）；
5. `PostToUi` 不做 UI 线程 fast path，UI 线程调用也经过同一段逻辑（R10 / D7）；
6. 返回 `false` 承载**三义**（空工作 / 已关闭 / 唤醒失败），本稿**不引入状态枚举**——见 **O5**。

### 3.3 `Run()` 的消息泵增量

现有 `Run()` 的骨架是每条消息 `TranslateMessage` / `DispatchMessageW`，再执行延迟清理。只增加一个**线程消息分支**：

```cpp
while (GetMessageW(&message, nullptr, 0, 0)){
    if (message.hwnd == nullptr && message.message == kUiDispatchMessage){
        DrainUiDispatch();
    } else {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    PerformDeferredCleanup();
}
```

★★ **判据必须同时判别 `hwnd`**（v1.1 补 R-①）：线程消息的 `hwnd` **恒为 NULL**，故 `message.hwnd == nullptr` 是**精确且零成本**的第一判别字段。仅比消息号**不安全**——本仓库已占用 `WM_APP + 1`（托盘回调，`Win32PlatformApplication.cpp:20`）与 `WM_APP + 2`（Desktop 跟随，`Win32PlatformWindow.cpp:73`），且 `DesktopLayerTests.cpp:532` 会**向窗口**投递 `WM_APP + 2` ⇒ 一旦新消息号与任一**窗口消息**号碰撞，本分支会把窗口消息**静默吞掉**（不再走 `DispatchMessageW`），且极难定位。

★ **消息号取值与占位登记**：具体取值留详细设计钉死；**取值纪律** = 沿用既有的**注释式占位登记**（先例 = `Win32PlatformWindow.cpp:71`，其注释显式记「`WM_APP + 1` 已被 … 占用，仍避开以免同号两义」），须避开 `WM_APP + 1` / `WM_APP + 2`，候选 **`WM_APP + 3`**。

不得把 dispatch 消息送入 `WindowMessageHandler`：它不是窗口事件，也不应伪造 `Window*` 来源；也不得把 callback 包成 `TimerEvent`，否则会错误进入焦点控件派发链（`Application.cpp:184-189`）。

### 3.4 drain 的批次快照与非重入

`DrainUiDispatch()` 不持锁执行 callback；处理方式是：

1. 加锁检查关闭态；
2. 将共享队列 `swap` 到局部 `std::deque`；
3. 清除 `m_dispatchWakePending`；
4. 解锁；
5. 逐项执行局部批次；
6. 每项执行前检查关闭态，若已关闭则丢弃该项及其余局部项；
7. callback 内新提交的工作进入共享队列，产生**后续**唤醒，不插入当前 callback 调用栈。

该结构同时满足：

- 当前 callback 不被新 callback 递归打断；
- callback A 提交 B 时，B 不会在 A 返回前执行；
- callback A 调用 `Application::Exit()` 时，A 可以完成当前函数体，B 及之后的未执行工作被关闭态丢弃；
- 队列锁不跨越用户代码，避免用户 callback 反向提交造成自死锁。

★ **状态转换表（v1.2 新增）**：`m_dispatchClosing` / `m_dispatchWakePending` 两个 bool 加队列内容即构成**全部状态**；下表穷举可达转换（`Q` = 队列内容，`+w` = 追加工作）。

| # | 起始（`closing` / `wakePending`） | 事件 | 结果（`closing` / `wakePending` / 队列） | 返回 | 落点 |
|---|---|---|---|---|---|
| **S1** | `false` / `false` | `PostToUi(work)`（非空）+ 唤醒**成功** | `false` / `true` / `Q + w` | `true` | §3.2 主路径 |
| **S2** | `false` / `true` | `PostToUi(work)`（非空） | `false` / `true` / `Q + w` | `true` | §3.2 早退（已有在途唤醒） |
| **S3** | 任意 | `PostToUi(空 function)` | **不变** | `false` | §3.2 首段（**不进临界区**） |
| **S4** | `true` / 任意 | `PostToUi(任意)` | **不变** | `false` | §3.2 关闭分支 |
| **S5** ★★ | `false` / `false` | `PostToUi(work)`（非空）+ 唤醒**失败** | `true` / `false` / **清空** | `false` | §3.2 失败回滚（★ **与 S1 同一临界区**） |
| **S6** | `false` / `true` | `DrainUiDispatch()` | `false` / `false` / **整体 swap 出** | — | §3.4 批次快照（锁内；解锁后逐项执行） |
| **S7** | 任意 / 任意 | `CloseUiDispatch()`（`RequestExit` / 析构） | `true` / `false` / **清空** | — | §3.5；**幂等** |

**读表要点：**

1. ★★ **S5 与 S1 共用一个临界区** ⇒ 「S1 已返回 `true`、S5 随后把它清掉」这条交错**在结构上不存在**——这正是 v1.2 的全部意义（对照 §3.2 的交错表）；
2. **S5 与 S7 的效果同形**（都进入 `closing` 并清队列）⇒ **无需第 4 个状态**：「关闭态」**同时就是**「唤醒失败态」（P-① 修正其修法的依据）；
3. S6 **必须**先把队列 swap 出来**再解锁**——否则就退化成「锁内执行 callback」，即 §3.2 关键点 4 明令禁止的形态；
4. **S5 之后没有「重开」转换**——`closing` 一旦为真，本生命周期内不再回落（`CloseUiDispatch` 也幂等）。若详设认为需要重开能力，属 **O2 ②**。

### 3.5 退出与析构顺序

`Application::Exit()` 现有路径是 `m_running = false` → `PlatformApplication::RequestExit()`（`Application.cpp:92-97`）。`Win32PlatformApplication::RequestExit()` 当前直接调用 `PostQuitMessage(0)`（`:48-52`），因此**不能把工作线程直接调用 `Application::Exit()` 当作本阶段安全用法**：它会同时碰到 `m_running` 的跨线程访问与退出消息归属问题。工作线程需要请求退出时，设计用法是 `PostToUi([...] { application.Exit(); })`。

★ **退出路径是单一漏斗（v1.1 补 R-⑤ 实测证据）**：全库生产代码 `PostQuitMessage` **仅 1 处**（`Win32PlatformApplication.cpp:50`），且**隐式退出**（最后一个窗口关闭）也经 `Application::Exit()`（`Application.cpp:147-148`，由 `OnWindowDestroyed` 调用）⇒ 「在 `RequestExit()` 内关闭队列即可覆盖**全部**退出路径」**成立**，无需在每个退出点各加一次关闭。

本阶段不新增 `Application::CloseDispatch()`：

- `Win32PlatformApplication::RequestExit()` 先调用 `CloseUiDispatch()`，再调用既有 `PostQuitMessage(0)`；
- `CloseUiDispatch()` 加锁设置 `m_dispatchClosing = true`、清空共享队列、清除 `m_dispatchWakePending`；
- `Run()` 当前正在执行的 callback 不被异步强杀；callback 返回后不再执行后续已关闭工作；
- `Win32PlatformApplication::~Win32PlatformApplication()` 最先调用 `CloseUiDispatch()`，再按现有顺序执行托盘 `NIM_DELETE` → 图标销毁 → `DestroyWindow` → 类析构；
- `Application::~Application()` 仍先清空托盘 sink（`Application.cpp:50-56`），不把 `Application` 捕获清理责任伪装成队列能力。

### 3.6 callback 异常边界

初步设计冻结以下**边界性质**：异常不得穿出 `DrainUiDispatch()`，更不能穿过 `DispatchMessageW` / Win32 消息循环。

建议实现形态：

```cpp
try{
    work();
} catch (const std::exception&){
    Logger::Log(LogLevel::Error, L"ECDI UI dispatch callback threw std::exception");
} catch (...){
    Logger::Log(LogLevel::Error, L"ECDI UI dispatch callback threw an unknown exception");
}
```

- 每个 callback 单独捕获，单个失败不阻断后续已排队工作；
- 不新增异常类型、不把异常变成 Framework Event；
- 是否把 `std::exception::what()` 转为 UTF-8 / UTF-16、是否附带序号，留详细设计；
- `std::function` 入队期间的分配异常不是 callback 执行异常，初步设计不承诺吞掉，详设必须明确 `PostToUi` 的异常契约。

### 3.7 预计改动文件（实现前清单）

| 文件 | 变更性质 | 设计落点 |
|---|---|---|
| `ECDI/include/ECDI/Application/Application.h` | 修改 | 声明 `Application::PostToUi`，追加 `<functional>` |
| `ECDI/src/Application/Application.cpp` | 修改 | 一行薄转发；不把队列逻辑放进 `Application` |
| `ECDI/include/ECDI/Platform/PlatformApplication.h` | 修改 | 新增 `PostToUi` 纯虚能力与契约注释 |
| `ECDI/src/Platform/Win32/Win32PlatformApplication.h` | 修改 | override、队列成员、互斥量、owner thread 状态 |
| `ECDI/src/Platform/Win32/Win32PlatformApplication.cpp` | 修改 | owner thread / `PostThreadMessageW` / Run 分支 / drain / close / 异常边界 |
| `ECDI/src/Tests/ApplicationDispatchTests.cpp` | **新建** | T23-1..T23-9，走真实消息泵与真实 `Application` 入口 |
| `ECDI/src/Tests/RunAllTests.h` | 修改 | 声明 `RegisterApplicationDispatchTests()` |
| `ECDI/src/Tests/RunAllTests.cpp` | 修改 | 注册新测试文件 |

**构建系统：**当前 CMake 使用 `GLOB_RECURSE ... CONFIGURE_DEPENDS`，不预期修改 `CMakeLists.txt`；测试注册是手工接线，不能因 CMake 自动发现新 `.cpp` 而漏登记。

---

## 4. 契约（C1–C10）

| # | 契约 | 实现落点 | 测试方向 |
|---|---|---|---|
| **C1** | `PostToUi` 成功返回只表示工作已入队并拥有，不表示已执行 | `PostToUi` 入队后返回 | T23-1 / T23-2 |
| **C2** | callback 只在 owner UI 消息线程执行 | `Run()` 线程消息分支 + `DrainUiDispatch` | T23-2 |
| **C3** | callback 永不在提交调用栈内 inline 执行 | 不设 UI thread fast path；批次快照 | T23-4 / T23-5 |
| **C4** | 同一投递源 FIFO；跨源顺序不形成公共语义 | 单一 deque 的入队临界区 | T23-3；不测试跨源固定顺序 |
| **C5** | 队列不持有 `Window*` / `Application*` 的自动保活 | callback 只作为用户载荷保存 | T23-6；代码审查 |
| **C6** | 关闭态拒绝新工作并丢弃未执行工作 | `RequestExit` / 析构前 `CloseUiDispatch` | T23-7 |
| **C7** | 正在执行的 callback 不被强制终止；其异常隔离在 dispatch 边界 | `try/catch` 包围单项执行 | T23-7 / T23-8 |
| **C8** | `PostToUi` 与消息泵 / Timer / Window Event 分层，不伪造 `TimerEvent` 或 Window Event | `Run()` 私有消息分支（★ **同时判别 `hwnd`**，见结构验收项 8），不进 `WindowMessageHandler` | 结构审查；既有测试零回归 |
| **C9** | 唤醒失败不得留下永远不消费的已接受工作 | ★ **与入队同一临界区**内的失败回滚（§3.2 · §3.4 **S5**）：就地清队列 + 置关闭态，**不得**调用会二次加锁的 `CloseUiDispatch()` | T23-8（★ 该分支**条件式**——需可注入的唤醒失败，见 §1.5 补记 ①）；结构审查 |
| **C10** | `Run()` 必须在构造 owner thread 执行 | `m_uiThreadId` 记录与 Run 前检查 | T23-2；错误路径留 O1 |

> **口径提醒：**C4 只向消费者承诺单源 FIFO，不把当前 `std::deque` 的 mutex 线性化顺序包装成跨线程业务排序；C7 只承诺 callback 执行边界，不能替调用方保证其捕获对象仍然存活。
> ★ **v1.2：C1 与 C9 必须合读**——`true` 是**不可撤回**的承诺（工作已入队并被队列拥有），因此「失败时清队列」**只允许**发生在与**入队同一临界区**内（§3.2）；任何"事后清理"都会丢弃**已承诺**的工作。

---

## 5. 影响面与规模

| 项 | 初步结论 |
|---|---|
| Public 头数量 | **92 → 92**，不新增公共头 |
| Public API | **源码接口集合 +2**：`Application::PostToUi` + `PlatformApplication::PostToUi`；1.0 前不承诺 ABI 兼容 |
| 生产文件 | 5 个：2 个公共头 / 1 个应用实现 / 1 个平台内部头 / 1 个平台实现 |
| 测试文件 | 3 个触点：新建 1 个测试文件，`RunAllTests.h/.cpp` 各登记 1 处 |
| 用例 | **264 → 273**：新增 **T23-1..T23-9**（9 条） |
| 新增 Event | **0**；不新增 `WorkerEvent` / `AsyncEvent` |
| 新增平台对象 | **0**；不新增 dispatcher 类、不新增托盘宿主、不绑定 Window |
| 构建配置 | 预期 **0**；CMake 自动发现源文件，测试登记手工完成 |
| 运行时资源 | 1 个 mutex、1 个队列、1 个 owner thread ID、1 个私有线程消息唤醒标志；无线程池、无定时器 |
| 主要风险 | 关闭竞态、唤醒失败后的队列处置、callback 异常、用户捕获对象生命周期 |

### 5.1 与既有约束对齐

| 约束 | 对齐 |
|---|---|
| 平台能力两条接缝 | G-3 是应用级能力，走 `PlatformApplication → Application`；不挂到 `PlatformWindow` |
| 每个 Win32 API 唯一归属 | `GetCurrentThreadId` / `PeekMessageW` / `PostThreadMessageW` / `PostQuitMessage` 只在 Win32 平台实现出现 |
| Event 只表示已发生事实 | dispatch callback 不伪造 Event；消费者需要具体事实时另按 Event 规则设计 |
| 资源类禁拷贝禁移动 | 队列不拥有 Window / Widget；不改变其地址稳定和 HWND 绑定契约 |
| YAGNI | 一个应用级入口 + 一个 Win32 内部队列；不引入线程池、Future、Token 或跨平台总线 |
| 零回归 | `TimerEvent`、动画、光标闪烁、托盘、Desktop 跟随的既有路径不改；dispatch 消息不进窗口翻译器 |

---

## 6. 实现前核查清单（V1–V6）

| # | 待定项 | 当前倾向 | 进入详细设计前必须回答 |
|---|---|---|---|
| **V1** ★★ | `Run()` owner thread 不匹配时的处理 | Debug `FRAMEWORK_ASSERT` + Release 返回 `-1` 并记 Error | 现有 `Run()` 返回码是否已有 `-1` 约定；是否需要单独的错误常量 |
| **V2** ★★ | `PeekMessageW` 的队列创建时机 | 构造期创建，允许 Run 前工作线程入队 | 构造线程与 Run 线程约束的公共注释、失败诊断与测试方式 |
| **V3** ★★ | `PostThreadMessageW` 唤醒失败后是否永久关闭 | **是**：清空队列、拒绝后续提交，避免“返回成功但永不执行” | 失败日志内容、是否需要保留失败原因枚举；不得引入重试循环 |
| **V4** | 单次 drain 的批次边界 | `swap` 到局部队列，callback 内新工作进入下一批 | 关闭态在当前批次中间发生时的逐项检查位置 |
| **V5** ★ | callback 异常日志 | 捕获并继续；先用固定 ASCII / 宽日志，不承诺传播 `what()` | Logger 的宽字符串转换是否值得进入本阶段；测试是否只断言后续 callback 仍执行 |
| **V6** | `PostToUi` 参数分配异常 | 不在平台边界吞掉，允许提交方收到标准异常 | 是否要在公共 API 文档写 `std::bad_alloc` 可能传播；不要把“无异常”凭空加入契约 |

---

## 7. 测试方向（T23-1..T23-9）

### 7.1 测试承载与通用装置

新建 `ECDI/src/Tests/ApplicationDispatchTests.cpp`，直接创建 `Application`，不新建 `PlatformApplication` 替身，不手工调用 `DrainUiDispatch`，不伪造 `WM_*` 消息。

通用记录字段：

- `std::thread::id submitThread`；
- `std::thread::id executeThread`；
- 测试主线程记录为 `uiThread`；
- `std::atomic<bool>` 只做跨线程信号，不用 `Sleep` 充当同步；
- callback 最后调用 `Application::Exit()`，让真实消息泵有确定的退出点。

### 7.2 用例表

| 用例 | 输入 / 场景 | 期望 |
|---|---|---|
| **T23-1** | 在 `Run()` 前由工作线程提交一个 callback，工作线程 join 后再进入 `Run()` | `PostToUi` 返回 true；callback 在之后的 UI 消息线程执行；执行完成后正常退出 |
| **T23-2** ★★ | 工作线程在 UI `Run()` 已运行时提交 callback | `submitThread != executeThread` 且 `executeThread == uiThread`；不产生第三个 dispatcher thread |
| **T23-3** | 同一个工作线程依次提交 A、B、C；A/B/C 记录序号 | UI 消费顺序严格 A → B → C；不对两个不同工作线程之间的交错顺序作断言 |
| **T23-4** ★★ | callback A 在 UI 线程执行期间调用 `PostToUi(B)`，A 在返回前记录 `aFinished=false` | B 不在 A 的调用栈中执行；A 返回后 B 才执行；验证 C3 / R9 |
| **T23-5** ★★ | UI 线程直接调用 `PostToUi(A)`，调用返回后再泵消息 | `PostToUi` 返回时 A 尚未执行；A 只在后续消息阶段执行；验证 D7 / R10 |
| **T23-6** ★ | **生命周期解耦**：工作线程提交一个 callback，其载荷是**值语义**的小对象；随后在 UI 线程销毁窗口 / 析构相关测试对象，再让消息泵 drain | ① callback **仍然执行**（队列不因窗口销毁而停摆）；② `executeThread == uiThread`；③ **框架不代调用方访问其捕获对象**——载荷按值存在于 `std::function` 内 ⇒ **可断言值正确**。★「队列**不保存** `Window*` / `Application*`」**无法从外部观测**，改由 **§7.3 结构审查**承担（v1.2 修正 P-④：原期望句「测试只验证队列不保存隐式窗口指针」不可执行） |
| **T23-7** ★★ | A 入队后先让其调用 `Exit()`，B 已在队列或同一批次等待 | A 可以完成；B 不执行；后续新 `PostToUi` 返回 false；消息泵正常返回 |
| **T23-8** ★★ | callback A 抛出 `std::runtime_error`，callback B 记录执行并退出 | 异常不穿出消息循环；B 仍执行；错误日志存在（日志字符串判据留详设）；若唤醒失败分支可注入，则再验证队列清理 |
| **T23-9** ★ | 以「异步解码完成通知」样式的**载荷**（小结构体 + 结果码，模拟消费者形态）由工作线程提交；公共头不含该消费者类型 | 载荷按值随 `std::function` 移动入队；UI 线程读到有效载荷；★ 对应需求 **A6**（真实消费者形态可接入），补上 v1.0 的 **A→T 缺口** |

### 7.3 结构性验收

1. `PostToUi` 的公共声明不包含 HWND / `UINT` / `WPARAM` / `LPARAM`；
2. `Win32PlatformApplication.cpp` 的私有 dispatch 消息只在 `Run()` 消息泵分支消费，不进入 `WindowMessageHandler`；
3. callback 执行区不持有 `m_dispatchMutex`；
4. `RequestExit()` 与析构路径均在清理托盘资源前关闭 dispatch；
5. `TimerEvent` 的 `WM_TIMER` 分支与 `Application::OnTimer` 路径保持原样；
6. 新测试登记完整：`RunAllTests.h` 1 处声明、`RunAllTests.cpp` 1 处调用；
7. 不出现 `WorkerEvent` / `AsyncEvent` / `ThreadPool` / `Future` 等本阶段非目标符号；
8. ★ `Run()` 的 dispatch 分支**同时判别消息号与 `hwnd`**（`message.hwnd == nullptr`）——线程消息的判别**不依赖消息号唯一性**（v1.1 补）。
9. ★★ **P-① 的可机检形态**（v1.2 补）：① `PostThreadMessageW` 的调用点**位于 `m_dispatchMutex` 临界区内**（不得在解锁之后）；② 唤醒失败分支**不出现 `CloseUiDispatch()` 调用**（它自己加锁 ⇒ 自死锁）。两条合起来把「失败回滚与入队同临界区」变成**结构性判据**。
10. ★ **队列类型层面无窗口亲和**（v1.2 补，承接 T23-6）：`m_dispatchQueue` 的元素类型是 `std::function<void()>`，**不含 `Window*` / `Application*` / `HWND`**；整条 dispatch 路径**不读取任何窗口对象**。

### 7.4 验证纪律

实施后由用户在 VS / CLion 运行四工具链；报告必须逐项写明：

- MSVC / ClangCL / Clang / MinGW 的通过数；
- `_DEBUG` / `/MDd` 断言是否启用；
- 本机显示器 DPI（虽本阶段主要是线程测试，但沿用项目全量测试纪律）；
- 若出现异步 flaky，先检查测试同步和消息泵时序，不用重复运行掩盖未定性。

---

## 8. 开放决策点（O1–O6，详细设计前）

| # | 问题 | 为什么不能在本稿继续猜 |
|---|---|---|
| **O1** | `Run()` 错线程的 Release 行为到底是返回 `-1`、记录后返回其它码，还是仅保留前置条件 | 这是既有 `Run()` 返回码语义的扩展，不能凭空造错误值 |
| **O2** ★ | ① `PostThreadMessageW` 失败时是否将所有排队工作一次性丢弃、是否保留最后一次错误原因；② ★★ **状态转移的原子粒度**（v1.2 补 P-⑤）：**入队与唤醒是否必须同一临界区**（本稿已定案「**是**」——§3.2 / §3.4 **S5**）· **失败进入 `closing` 后是否允许重开**（本稿倾向「**否**」） | 影响可诊断性与队列状态，但不应引入无限重试或全局错误总线；★ ② 的"重开"若最终要开，须先证明有真实消费者需要它（YAGNI）；★ **「不允许重开」的理由**：重开只会给"已拿到 `false` 的提交者"制造*第二次机会*的错觉 |
| **O3** | callback 异常日志是否需要 `what()` 的 UTF-8 → UTF-16 转换 | 当前 Logger 公共签名是 `std::wstring_view`（`include/ECDI/Core/Logger.h:25-31`）；转换会增加实现面，但不改变核心语义 |
| **O4** | 测试 T23-8 如何稳定观察日志 | `Logger` 走 `OutputDebugStringW`，不能靠 stdout；可复用 DBWIN 监听，也可只断言后续 callback，需在详设选择 |
| **O5** | 是否需要为"提交已接受但未来不会执行"增加返回状态枚举 | ★ **v1.1 补实证（R-④）**：当前 `bool` 实际承载**三义**——**空工作**（§3.2 伪代码首段）· **`m_dispatchClosing`**（关闭中）· **唤醒失败**——**共用同一个 `false`** ⇒ 调用方无法区分「本来就没工作」「正在关闭」「系统唤醒失败」。详设至少给出 **Debug 断言或明确区分**；是否升级为小型状态枚举，仍应先证明真实消费者有分支需求（不无端造 `Promise` / 结果类型）。★★ **v1.2（P-⑥）：维持不引入枚举**——`PostThreadMessageW` 已移入临界区（§3.2）后**三义仍共用 `false`**；本轮评审建议"现在就上枚举"**不采纳**，理由 = 有分支需求的消费者尚不存在（§1.5 P-⑥） |
| **O6** | 第二平台出现后，是否沿用同名 `PostToUi` 语义 | 当前只定公共语义，不预建第二平台；届时必须复核其消息循环 / 调度模型，不照抄 Win32 载体 |

---

## 9. 修订记录

- **v1.2**（2026-09-29）**第二轮设计评审吸收 —— P0 并发竞态修正（★ 采纳问题、修正修法）**。① ★★ **§3.2 重写**：`PostThreadMessageW` **移入临界区**，失败时**就地**清队列 + 置 `m_dispatchClosing`（**不得**调用会二次加锁的 `CloseUiDispatch()`）；新增**交错实证表**（t0/t1/t2）说明为何「入队与唤醒分属两个临界区」会让 Worker B 已 `return true` 的工作被 A 的失败清掉。★ **不采纳**评审建议的第 4 状态 `WakeFailed`——已返回的 `true` **撤不回**，且它与 `Closing` **语义重复**。② ★★ **§3.4 补状态转换表 S1–S7**（含 `wakePending` 列与「唤醒失败」行 **S5**），并写明 **S5 与 S1 同一临界区**、**S5 与 S7 效果同形** ⇒ 无需第 4 状态（`Closing` 即失败态）。③ ★ **§3.1 owner-thread 定性改为「本阶段新增的前置条件」**（非既有事实重述）+ **影响面实测**（全库构造点与 `Run()` 调用点**全部同线程** ⇒ 零既有用法受影响）；关键点纪律 4 **拆为性质相反的两句**（`PostThreadMessageW` 必须在锁内 / callback 必须在锁外）。④ ★ **T23-6 重定位为「生命周期解耦」用例**（原期望句不可执行），「队列不保存窗口指针」改由 **§7.3 结构审查**承担。⑤ ★ **O2 补「状态转移的原子粒度」**（含"是否允许重开"）；**O5 维持不引入状态枚举**（P-⑥ 明确记为不采纳）。⑥ ★ **§4 C9 落点精确化为「与入队同一临界区」** + §4 口径提醒补「**`true` 不可撤回**」；**§7.3 增结构验收项 9 / 10**。⑦ ★ **补记两条审查外发现**（§1.5）：P-① 修复路径**本阶段无自动化覆盖**（详设须选择注入缝或如实标注"仅代码审查"）· 构造期新增**线程亲和副作用**已并入 ③ 的影响面实测。★ 其余（`PostThreadMessageW` 方向 · 队列存储归属 · 契约 C1–C10 骨架 · 8 文件影响面 · O1 / O3 / O4 / O6）**维持不变**；**无源码改动**。
- **v1.1**（2026-09-29）**设计评审意见吸收**（评审结论：方向与质量通过，采纳 5 条补强；逐条处置见 §1.4）。① ★★ **`Run()` 分支判据补 `hwnd` 判别**（`message.hwnd == nullptr`）——仅比消息号会与既有 `WM_APP + 1` / `WM_APP + 2` 碰撞并**静默吞掉窗口消息**；§3.3 同步补**消息号占位登记纪律**，§4 的 **C8** 与 §7.3 增**结构验收项 8**。② ★ **新增 T23-9**（真实消费者形态载荷）补齐需求 **A6** 的 A→T 缺口；§5 增**用例锚点 264 → 273**。③ ★ **§3.5 补退出漏斗实测证据**（生产 `PostQuitMessage` 仅 1 处 + 隐式退出亦经 `Application::Exit()` ⇒ 单点关闭即覆盖全部退出路径）。④ ★ **O5 补三义合流实证**（空工作 / 关闭中 / 唤醒失败共用 `false`）。★ 其余（`PostThreadMessageW` 方向 · 队列存储归属 · 契约 C1–C10 · 8 文件影响面 · O1–O6）**维持不变**。
- **v1.0**（2026-09-29）初稿。输入 = 需求稿 **v1.1** · 当前 `PlatformApplication` / `Application` / `Win32PlatformApplication` 消息泵与托盘宿主源码。完成范围映射、接口草案、Win32 唤醒方向、队列 / 非重入 / 退出生命周期模型、契约 C1–C10、8 文件影响面、T23-1..T23-8 测试方向与 O1–O6 开放项。**待评审**；不修改源码、不运行测试。
