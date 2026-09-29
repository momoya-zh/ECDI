# Phase 23 · 工作线程 → UI 线程投递（worker-to-UI dispatch）——初步设计（v1.0）

> 来源：`docs/phase23-worker-to-ui-dispatch-requirements.md` **v1.1**（外部评审 v1.0 已通过；v1.1 已吸收评审补强，待复评）· 审计 `framework-defect-audit.md` **D-4** · `roadmap-deferred.md` §7.9 顺位 ④。
> 状态：**v1.0 待评审**（2026-09-29）——本稿是接口 / 数据流 / 生命周期的初步设计，不修改源码，不进入详细设计级的逐行实现规格。
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

设计契约：

- `Application` 与 `Win32PlatformApplication` 在同一线程构造；
- `Application::Run()` 必须在该线程调用；
- 工作线程只调用 `PostToUi()`，不调用 `Run()`、`DrainUiDispatch()` 或任何 Win32 UI API；
- owner thread 校验失败的具体诊断 / 返回码列入开放点 O1，不能在实现时无声跨线程运行消息泵。

### 3.2 `PostToUi` 入队与单次唤醒

伪代码只表达状态转移，不是详设的可直接复制实现：

```cpp
bool Win32PlatformApplication::PostToUi(std::function<void()> work){
    if (!work){
        return false;                 // 空工作不入队
    }

    bool needWake = false;
    {
        std::lock_guard lock(m_dispatchMutex);

        if (m_dispatchClosing){
            return false;
        }

        m_dispatchQueue.emplace_back(std::move(work));
        if (!m_dispatchWakePending){
            m_dispatchWakePending = true;
            needWake = true;
        }
    }

    if (!needWake){
        return true;                   // 已有唤醒消息，队列已接收
    }

    if (PostThreadMessageW(m_uiThreadId, kUiDispatchMessage, 0, 0) != FALSE){
        return true;
    }

    // 唤醒失败：不能把工作留在一个永远不会被 drain 的队列里。
    CloseUiDispatch();                    // 同时记录唤醒失败并清空未执行队列
    return false;
}
```

关键点：

1. **先入队、后唤醒**，避免 callback 在消息到达时还不在队列；
2. `m_dispatchWakePending` 只让一批连续提交共享一个唤醒消息，避免每个文件变化都制造一条平台消息；
3. 唤醒失败时进入关闭态并清空未执行队列，不能返回 `true` 后把工作悬在队列里；
4. `PostToUi` 由多个工作线程调用时，锁只保护入队与唤醒标志，不包住平台 API 或 callback 执行；
5. `PostToUi` 不做 UI 线程 fast path，UI 线程调用也经过同一段逻辑。

### 3.3 `Run()` 的消息泵增量

现有 `Run()` 的骨架是每条消息 `TranslateMessage` / `DispatchMessageW`，再执行延迟清理。只增加一个**线程消息分支**：

```cpp
while (GetMessageW(&message, nullptr, 0, 0)){
    if (message.message == kUiDispatchMessage){
        DrainUiDispatch();
    } else {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    PerformDeferredCleanup();
}
```

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

### 3.5 退出与析构顺序

`Application::Exit()` 现有路径是 `m_running = false` → `PlatformApplication::RequestExit()`（`Application.cpp:92-97`）。`Win32PlatformApplication::RequestExit()` 当前直接调用 `PostQuitMessage(0)`（`:48-52`），因此**不能把工作线程直接调用 `Application::Exit()` 当作本阶段安全用法**：它会同时碰到 `m_running` 的跨线程访问与退出消息归属问题。工作线程需要请求退出时，设计用法是 `PostToUi([...] { application.Exit(); })`。本阶段不新增 `Application::CloseDispatch()`：

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
| `ECDI/src/Tests/ApplicationDispatchTests.cpp` | **新建** | T23-1..T23-8，走真实消息泵与真实 `Application` 入口 |
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
| **C8** | `PostToUi` 与消息泵 / Timer / Window Event 分层，不伪造 `TimerEvent` 或 Window Event | `Run()` 私有消息分支，不进 `WindowMessageHandler` | 结构审查；既有测试零回归 |
| **C9** | 唤醒失败不得留下永远不消费的已接受工作 | `PostThreadMessageW` 失败后的关队列清理 | T23-8；结构审查 |
| **C10** | `Run()` 必须在构造 owner thread 执行 | `m_uiThreadId` 记录与 Run 前检查 | T23-2；错误路径留 O1 |

> **口径提醒：**C4 只向消费者承诺单源 FIFO，不把当前 `std::deque` 的 mutex 线性化顺序包装成跨线程业务排序；C7 只承诺 callback 执行边界，不能替调用方保证其捕获对象仍然存活。

---

## 5. 影响面与规模

| 项 | 初步结论 |
|---|---|
| Public 头数量 | **92 → 92**，不新增公共头 |
| Public API | **源码接口集合 +2**：`Application::PostToUi` + `PlatformApplication::PostToUi`；1.0 前不承诺 ABI 兼容 |
| 生产文件 | 5 个：2 个公共头 / 1 个应用实现 / 1 个平台内部头 / 1 个平台实现 |
| 测试文件 | 3 个触点：新建 1 个测试文件，`RunAllTests.h/.cpp` 各登记 1 处 |
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

## 7. 测试方向（T23-1..T23-8）

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
| **T23-6** | callback 捕获业务对象；先把工作入队，再销毁窗口 / 清理测试对象；不让 callback 访问失效对象 | 框架队列不自动访问 Window；测试只验证队列不保存隐式窗口指针，具体用户捕获生命周期由调用方负责 |
| **T23-7** ★★ | A 入队后先让其调用 `Exit()`，B 已在队列或同一批次等待 | A 可以完成；B 不执行；后续新 `PostToUi` 返回 false；消息泵正常返回 |
| **T23-8** ★★ | callback A 抛出 `std::runtime_error`，callback B 记录执行并退出 | 异常不穿出消息循环；B 仍执行；错误日志存在（日志字符串判据留详设）；若唤醒失败分支可注入，则再验证队列清理 |

### 7.3 结构性验收

1. `PostToUi` 的公共声明不包含 HWND / `UINT` / `WPARAM` / `LPARAM`；
2. `Win32PlatformApplication.cpp` 的私有 dispatch 消息只在 `Run()` 消息泵分支消费，不进入 `WindowMessageHandler`；
3. callback 执行区不持有 `m_dispatchMutex`；
4. `RequestExit()` 与析构路径均在清理托盘资源前关闭 dispatch；
5. `TimerEvent` 的 `WM_TIMER` 分支与 `Application::OnTimer` 路径保持原样；
6. 新测试登记完整：`RunAllTests.h` 1 处声明、`RunAllTests.cpp` 1 处调用；
7. 不出现 `WorkerEvent` / `AsyncEvent` / `ThreadPool` / `Future` 等本阶段非目标符号。

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
| **O2** | `PostThreadMessageW` 失败时是否将所有排队工作一次性丢弃、是否保留最后一次错误原因 | 影响可诊断性与队列状态，但不应引入无限重试或全局错误总线 |
| **O3** | callback 异常日志是否需要 `what()` 的 UTF-8 → UTF-16 转换 | 当前 Logger 公共签名是 `std::wstring_view`（`include/ECDI/Core/Logger.h:25-31`）；转换会增加实现面，但不改变核心语义 |
| **O4** | 测试 T23-8 如何稳定观察日志 | `Logger` 走 `OutputDebugStringW`，不能靠 stdout；可复用 DBWIN 监听，也可只断言后续 callback，需在详设选择 |
| **O5** | 是否需要为“提交已接受但未来不会执行”增加返回状态枚举 | 当前 `bool` 已能表达 accepted / rejected；若需要区分 closing / wake failure，应先证明真实消费者有分支需求，再增加类型 |
| **O6** | 第二平台出现后，是否沿用同名 `PostToUi` 语义 | 当前只定公共语义，不预建第二平台；届时必须复核其消息循环 / 调度模型，不照抄 Win32 载体 |

---

## 9. 修订记录

- **v1.0**（2026-09-29）初稿。输入 = 需求稿 **v1.1** · 当前 `PlatformApplication` / `Application` / `Win32PlatformApplication` 消息泵与托盘宿主源码。完成范围映射、接口草案、Win32 唤醒方向、队列 / 非重入 / 退出生命周期模型、契约 C1–C10、8 文件影响面、T23-1..T23-8 测试方向与 O1–O6 开放项。**待评审**；不修改源码、不运行测试。
