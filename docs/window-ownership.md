# Window 所有权与生命周期契约（初步设计 v1.1）

> 阶段：初步设计（五阶段法 ② —— 需求确认已由《B 方案 + 两轮外部评审》承担）
> 来源：Phase 12 WindowChrome 实施后缺陷（见 `phase12-windowchrome-detailed-design.md` §9 v1.5）
> 状态：**✅ 已实施（2026-09-12）**——实施规格见 [`window-ownership-detailed-design.md`](window-ownership-detailed-design.md) **v1.2**；`ecdi_tests` **183 passed / 0 failed**（MinGW + `-D_DEBUG`）；A1 编译期契约与 A4 静态检查已实证，MSVC / Clang / ClangCL 待用户确认
> 定位：**本项目的 Window 所有权 / 生命周期正式契约文档**。不属任何 Phase，故未采用 `phaseN-<module>-<type>.md` 命名；阶段由本头部与 §8 跟踪，后续相关变更在本文档内演进（不另开文档）。
> 文件布局（v1.1 调整）：**契约主体**（§1–§8：三层归属 / 三条链 / 5 条不变量 / 决策 / 契约条款原文 / 验收）在本文档；**实施规格**另立 [`window-ownership-detailed-design.md`](window-ownership-detailed-design.md)（逐文件精确改动 + 验收执行步骤），两者互为索引——原「后续变更在本文档内演进（不另开文档）」的措辞随之修订。
> 一句话：把「窗口归属」从**运行期约定**升级为**类型与访问权限层面的契约**——**创建权 / 对象所有权 / 平台资源销毁**三者分离、各自闭合。

---

## 1. 起源与目标

### 1.1 触发事件

| 时点 | 事实 |
|---|---|
| Phase 12 实施后 | 用户运行 MSVC 构建的 `ecdi_tests` → `Application.cpp:92` 断言 `Expression: it != m_windows.end()`（`OnWindowDestroyed`） |
| 直接根因 | 测试替身 `TestWindow` 用 `std::make_unique<Window>(&app, …)` **直接构造**窗口，绕过 `Application::Create()` ⇒ 窗口从未登记进 `m_windows` |
| 销毁链（**同步重入**） | `~TestWindow → ~Window → Release() → DestroyWindow()`（返回前 `WM_DESTROY` 已处理完）`→ WindowDestroyedEvent → Window::OnEvent → Application::OnWindowDestroyed` → 在册中找不到 → 断言 |
| 为何只在 MSVC 暴露 | `FRAMEWORK_ASSERT` 仅在 `#ifdef _DEBUG` 下存在（`Core/ECDIAssert.h:22`）；CMake 的 MinGW 构建不含 `_DEBUG` ⇒ 断言层被编译为 `((void)0)`，"全绿"实为**断言未执行** |
| **A（止血）——已关闭** | `TestWindow` 改持**非拥有** `Window* = &app.Create(...)`，析构只 `Release()`。带 `-D_DEBUG` 的 MinGW 构建 → `ecdi_tests` **183 passed / 0 failed / 退出码 0**（断言真正生效，且无其它被掩盖的缺陷）。回写于 Phase 12 详设 **v1.5** |
| **B（本文档）** | 修掉「**允许消费者写出这种代码**」的 API 根源——A 解决"当前消费者出错"，B 解决"消费者能这么出错" |

### 1.2 问题陈述：两个互斥的所有权世界观，各取一半

| | 世界观 ① | 世界观 ② |
|---|---|---|
| 主张 | 窗口归 `Application` 所有 | 窗口可脱离 `Application` 存在 |
| 相应机制 | 构造器应 `private` + `friend Application`；断言可保留 | `public` 构造器合法；`Application` 必须容忍未登记窗口（断言应删） |
| 自洽性 | ✅ | ✅ |

**ECDI 现状 = ① 的 public 构造器 + ② 应容忍的断言 ⇒ 两个都不成立**：

| 症状 | 锚点 |
|---|---|
| 构造时声明归属，归属关系却直到销毁才被检验 | `Window.h:46`（构造器 `public` 且收 `Application*`，**可为 `nullptr`**）vs `Application.cpp:45`（登记只在此） |
| 断言与它自己的容错分支互相矛盾（两头下注） | `Application.cpp:92` 断言「绝不可能」+ `:94` 又 `return` 兜底 |
| 销毁路径暗依赖消息循环，而公开契约未声明 | `ProcessDeferredDestroy()` 唯一调用点 = `Win32PlatformApplication.cpp:19`（每条消息后） |
| `PlatformWindowHost` 注释与事件系统结论相反 | `PlatformWindowHost.h:15`「框架层无需动作」vs `WindowMessageHandler.cpp:65`（仍翻译 `WM_DESTROY`） |
| 最直观的修法本身是陷阱（双重所有权 + 析构期所有权迁移） | 见 §2.4 与 B1-t |

### 1.3 目标

> **让 Window 的创建权与所有权关系，在类型与访问权限层面闭合。**

保证级别（**表述纪律**——不得写成「只有 `Create()` 能构造」）：

1. **框架外无法构造** `Window`（编译期拒绝）；
2. **`Application` 拥有 `Window` 的构造权限**——`friend class Application` 授权的是**整个 `Application` 类**，不是单个成员函数；
3. **`Application::Create()` 是当前唯一实际构造入口**；
4. `Window` 必须关联一个 `Application`——「无主窗口」在语法上不存在。

### 1.4 非目标（防范围膨胀）

| 非目标 | 理由 |
|---|---|
| Detached / 无归属窗口 | 当前无消费者。**不承诺** `CreateDetached` 一类 API；若未来出现需求，**重新设计生命周期模型**（不提前决定） |
| `m_running` / `Run`-`Exit` 语义 | 属 Application 生命周期，**独立记账**（§7） |
| `~Application` 残留活窗口 | 潜在析构序隐患，**独立记账**（§7）；不纳入本次范围 |
| 删除断言的容错分支 | 保留（b3a 双层防御，§3 / §4.4） |

---

## 2. 生命周期契约

### 2.1 三层归属（**相互独立**）

| 层 | 持有者 | 释放点 | 现成机制 |
|---|---|---|---|
| **Window 对象** | `Application::m_windows`（`unique_ptr<Window>`） | `ProcessDeferredDestroy()`（有 `Run()`）/ `~Application`（无 `Run()`） | 唯一 `unique_ptr` 持有 ⇒ **无双重 delete** |
| **平台窗口（HWND）** | `Window::m_platformWindow`（`unique_ptr<PlatformWindow>`） | `Window::Release()` → `Win32PlatformWindow::Release()` → `DestroyWindow` | `m_hwnd == nullptr` 早退 ⇒ **幂等** |
| **注册表成员** | `Application`（`m_windows` ↔ `m_deferredDestroy`） | `Application::OnWindowDestroyed` 转移 | 转移后由延迟清理真正 `delete` |

### 2.2 三条链（闭环）

> 以下三条链为**自足描述**——读者无需再翻源码（每条给出关键锚点行号）。

**① 创建**
```cpp
Application::Create(title, w, h)
    ↓
new Window(*this, …)                     // 私有构造器——框架内唯一实际构造点（B1）
    ↓
std::unique_ptr<Window>(…)               // 唯一持有者（B1-t：make_unique 不可用）
    ↓
m_windows.emplace_back(…)                // 登记 = 所有权确立
    ↓
派发 WindowCreatedEvent
```

**② HWND 销毁（只销毁资源，不动对象）**
```cpp
Window::Release()                        // 幂等：m_hwnd == nullptr 时直接返回 true
    ↓
m_platformWindow->Release()              // Win32PlatformWindow::Release
    ↓
DestroyWindow(m_hwnd)                    // 同步——返回前 WM_DESTROY 已处理完
    ↓
m_hwnd = nullptr                         // Win32PlatformWindow.cpp:337（幂等前提）
    ↓
WindowDestroyedEvent                     // WindowMessageHandler.cpp:62-70
    ↓
Window::OnEvent → Application::OnEvent   // Window.cpp:409
    ↓
Application::OnWindowDestroyed           // Application.cpp:80
```

**③ Window 对象回收**
```cpp
Application::OnWindowDestroyed
    ↓
m_windows → m_deferredDestroy            // 不立即 delete（避开"在窗口自己的回调里删窗口"）
    ↓
[有 Run()] PerformDeferredCleanup()      // Win32PlatformApplication.cpp:19（每条消息后）
    ↓
Application::ProcessDeferredDestroy()    // Application.cpp:69
    ↓
m_deferredDestroy.clear() → delete Window
    ↓
~Window() → Release()（句柄已空 → 无操作）
```

无 `Run()` 分支不并入主链（它是**当前实现路径**而非永久语义保证，见 §4.3）：清理从不发生 → 延后到 `~Application` 的成员析构。
### 2.3 不变量（5 条）

| # | 不变量 | 机制 |
|---|---|---|
| 1 | Window 必须关联一个 `Application` | `Application&`（B2） |
| 2 | Window 只能经 `Application` 的受控创建路径产生 | `private` 构造器 + `friend class Application`（B1） |
| 3 | Window 对象的所有权归 `Application` | `unique_ptr<Window>`（唯一持有者） |
| 4 | **Window 对象生命周期与 HWND 资源生命周期相互独立**：`Release()` 只负责平台资源销毁，`Application` 负责 Window 对象回收 | `Release()` 幂等 + 注册表转移（§2.2 ②③） |
| 5 | 调用者**不得**对 `Window*` 执行 `delete` | 契约条款（§4.1）——`~Window()` 保持 `public`，**不做**访问控制（B5b，理由见 §3） |

### 2.4 源码取证锚点（本文档全部结论以真实源码为锚）

| 结论 | 锚点 |
|---|---|
| `Release()` 只销毁 HWND | `Window.cpp:108-118` → `Win32PlatformWindow.cpp:109-119` |
| `Release()` 幂等 | `Win32PlatformWindow.cpp:111`（早退）+ `:337`（`WM_DESTROY` 置空句柄） |
| HWND 销毁事件的唯一翻译点 | `WindowMessageHandler.cpp:62-70` |
| 注册表转移 | `Application.cpp:80-110` |
| 实际 `delete` 时机 | `Win32PlatformApplication.cpp:19` → `Application.cpp:69-72` |
| 成员析构序（决定"无 `Run()`"时的回收点） | `Application.h:116/118/120/122` |
| 事件可拿到 `Window*`（B5 论据） | `Event.h:24`（`GetWindow()`）——回调里 `delete e.GetWindow()` **可编译** |
| `Window` 无派生类（B1 前提） | 全库 grep `: public Window` → **0 命中** |
| `Window` 构造点唯一（B1/B2 前提） | 全库 grep `new Window` / `make_unique<Window>` → 仅 `Application.cpp:45` |

---

## 3. 决策

| 编号 | 决策 | 内容 | 关键陷阱 / 前提 |
|---|---|---|---|
| **B1** | 构造器可见性 | `public` → **`private`** + `friend class Application` | 前提已核实：**全库零 `Window` 派生类** ⇒ 不阻断既有派生。**措辞纪律**：不得写「只有 `Create()` 能构造」 |
| **B1-t** | `Create` 内的构造写法 | `std::make_unique<Window>` → **`std::unique_ptr<Window>(new Window(*this, …))`** | ⚠️ `make_unique` 的**函数体不是 `Application` 的成员** ⇒ 访问检查失败（friend 只在"访问发生处"生效）。`new` 立即交给 `unique_ptr`，所有权明确，**不是**裸指针 |
| **B2** | 归属关系类型化 | 形参 `Application*` → **`Application&`**；成员同步改引用 | 消灭 `Window(nullptr, …)` 这一状态。`m_application` 全库仅 2 处使用（`Window.cpp:43` / `:413`） |
| **B3** | 断言与容错**并存**（**b3a**） | 保留 `FRAMEWORK_ASSERT` + 保留 `if (it == m_windows.end()) return;`，**并把设计意图写进注释** | 二者是**两层**：断言 = `_DEBUG` 下暴露不变量违反；容错 = Release 下避免"无效迭代器解引用"演化为 UB。**必须写清理由**，否则会重演"两头下注"的观感（问题从来不是两者并存，而是没有说明为什么并存） |
| **B4** | 清理反向友元 | 删除 `Application::friend class Window;`（`Application.h:42`） | `Window` 只调 `m_application.OnEvent()`，而 `EventRouter::OnEvent` 本就 `public` ⇒ 该友元**无用途**。同批**重查** Application ↔ Window 双向访问关系（`Widget.h:237` / `TextBox.h:23` 的同名友元属另一用途，**不动**） |
| **B5** | 析构权限（**b5b**） | **保持 `~Window()` `public`**，用**契约条款**（§4.1）禁止外部 `delete` | ⚠️ 私有化析构会让 `std::unique_ptr<Window>` 的 **`std::default_delete` 编译失败**（与 `make_unique` 同类陷阱：访问检查发生在 `default_delete::operator()` 内部）⇒ 必须自定义 deleter（嵌套 `Window::Deleter`）+ 改 `m_windows` / `m_deferredDestroy` 的**元素类型**与全部转移代码。**收益/代价比不划算**：`delete event.GetWindow()` 是**明显绕过契约**的用法，与"看起来完全正常"的 `Window(app, …)` 性质不同——前者用契约约束，后者用类型消灭 |
| — | Detached 窗口 | **不设计** | 见 §1.4 |
| — | `PlatformWindowHost` 旧注释 | **改写**（保留 7.1 设计意图的演进痕迹） | §4.5 |

---

## 4. 契约条款（正式措辞）

> 以下文本建议以注释形式落入 `Window.h` 类注释 / `PlatformWindowHost.h`，作为**对外可引用的契约原文**。

### 4.1 Window 对象所有权

> `Window` 对象由 `Application` 持有并负责生命周期管理；**调用者不得对 `Window*` 执行 `delete`**。
> `Window::Release()` 仅负责释放**平台窗口资源**，不负责销毁 `Window` 对象。

### 4.2 `Release()` ≠ `delete Window`

`Release()` 的语义边界（锚点见 §2.4）：**只销毁 HWND**；**不**从 `m_windows` 移除（移除由 `OnWindowDestroyed` 负责）；**幂等**（重复调用返回 `true` 且无副作用）。

### 4.3 对象回收依赖消息泵（**新增强制条款**）

> `Application` 对已销毁窗口对象的回收时机**依赖消息循环**：
> - 有 `Run()`：每条消息处理后（`PerformDeferredCleanup` → `ProcessDeferredDestroy`）；
> - 无 `Run()`：延后到 `~Application` 的成员析构。
>
> **消费者不得假设「窗口关闭后其 `Window` 对象立即析构」。**

这是当前架构**已有但从未表达**的前提——本条款把它显式化（也是 A 阶段能成立的依据：测试里 `TestWindow` 只触发 `Release()`，对象回收交给 `Application`）。
>
> ⚠️ **措辞纪律**：「无 `Run()` → 延后到 `~Application`」是**当前实现的回收路径**，**不得**包装成"设计永久保证某个固定顺序"——§7 恰好已登记 `~Application` 残留窗口的独立隐患，两者必须保持区分（前者是事实陈述，后者是待调查项）。

### 4.4 断言与容错的双层含义（b3a 注释文本）

> 容器成员关系是**内部不变量**。Debug 构建以断言暴露框架自身的缺陷；Release 构建保留运行时分支，避免不变量被违反时"无效迭代器解引用"进一步演化为未定义行为。

### 4.5 `PlatformWindowHost` 注释改写（对照）

**原文（已过期）**

> 无 `OnDestroyed`：WM_DESTROY 后 Win32PlatformWindow 内部置空句柄，框架层无需动作

**新文（拟）**

> 无 `OnDestroyed`：`PlatformWindowHost` **不负责 `Window` 对象的生命周期**。Win32 平台层在 `WM_DESTROY` 后清空 HWND 并置空句柄，同时经既有事件通道上报 `WindowDestroyedEvent`；`Window` 对象的延迟销毁与注册表维护由 `Application` 负责。

（**改写而非删除**——原句记录了 7.1 时代的设计意图，演进痕迹保留。）

---

## 5. 影响面（初设级：文件与改动性质）

| 文件 | 改动性质 |
|---|---|
| `ECDI/include/ECDI/Window/Window.h` | 构造器移入 `private`；加 `friend class Application;`；`Application*` → `Application&`（形参 + 成员）；类注释补 §4.1 契约原文 |
| `ECDI/src/Window/Window.cpp` | 引用化后的 1 处调用点（`m_application.OnEvent`） |
| `ECDI/src/Application/Application.cpp` | `Create` 内构造写法（B1-t）+ §4.4 注释 |
| `ECDI/include/ECDI/Application/Application.h` | （B4）删除 `friend class Window;` |
| `ECDI/include/ECDI/Platform/PlatformWindowHost.h` | （§4.5）注释改写 |
| `ECDI/src/Tests/WindowChromeTests.cpp` | 追加**编译期契约测试**（§6.1）；`TestWindow::window` 显式标注 **non-owning reference**（§6.4） |
| `docs/` | 本文档 + `docs/README.md` 索引 + Phase 12 详设 v1.5 的回指 |

**零改动**：`PlatformWindow` 系列、全部控件、`examples/`（ModelProbe / VisualTest / MinimalApp）、其它全部测试文件。

**规模估计**：**5 个生产文件 ≈ 35 行**（含注释——`Window.h` / `Window.cpp` / `Application.h` / `Application.cpp` / `PlatformWindowHost.h`）；1 个测试文件 ≈ 12 行。

---

## 6. 验收

### 6.1 编译期契约测试（**必做**——B 的第一验收项）

```cpp
// 依据：std::is_constructible 在**中立上下文**求值 ⇒ 构造器为 private 时结果为 false
static_assert(
    !std::is_constructible_v<Window, Application&, const char*, int, int>,
    "Window 不得在框架外构造——请使用 Application::Create()");
```

**实证（2026-09-12，两个工具链已验）**——用等价探针（私有构造器 + `friend`）验证：

| 工具链 | 编译 | 中性上下文 `is_constructible_v` | **友元类内部** |
|---|---|---|---|
| MinGW g++ 16.1 | ✅ | `0`（false） | **`0`（false）** |
| clang++（LLVM） | ✅ | `0`（false） | **`0`（false）** |

⇒ 两个结论：
1. **trait 尊重访问控制** ⇒ 负向断言方案可用；
2. ⚠️ **trait 在中立上下文求值**——即使写在 `Application` 内部也返回 `false`。因此它**只能做负向断言**，**不能**用来正向证明"友元可构造"。友元权限的正向证据 = `Application::Create` 里那行 `new Window(*this, …)` **能编译**（天然成立）。

**待四工具链确认**：MSVC `cl.exe`（含 ClangCL）尚未实测——它同样是标准要求的行为，但按本项目纪律（**先实证再定稿**），在实施验收 A2 中一并确认。

**兜底方案（若某工具链的 trait 不尊重访问控制）**：改用**负向编译探针**——一个 `EXCLUDE_FROM_ALL` 的独立小目标，内含 `Window w(app, "t", 100, 100);`，**预期编译失败**；只手动跑（不入 `ecdi_tests`，避免构建被破坏）。

**配套（人工、一次性）**：在非 `Application` 上下文写 `Window w(app, "t", 100, 100);` 确认**编译失败**，结论以注释固化于测试文件。
### 6.2 运行期

- MinGW + `-DCMAKE_CXX_FLAGS=-D_DEBUG` → `ecdi_tests` 全绿（当前 **183**；若采纳 §6.5 则 184）
- **MSVC Debug（用户在 VS / CLion 运行）→ 不再触发 `Application.cpp:92` 断言**

### 6.3 静态检查

B1-t 落地后，`Window` 的构造形态**只有一种**：

```text
grep -rn "new Window"        → 恰好 1 处（Application.cpp::Create 内）
grep -rn "make_unique<Window>" → 0 处
```

（原先笼统写"`new Window\|make_unique<Window>` 只剩 1 处"——与 B1-t 的最终代码形态不对齐，此处按实际形态精确化。0 处的意义：**`make_unique` 这一形态在该改造后已不可能出现**。）

### 6.4 文档纪律：不得把 A 的测试替身当作"推荐用法示例"

`TestWindow` 持有 `Window*` 是**测试观察者**（**non-owning reference**）——因为它需要观察 `Application` 所拥有的对象。**推荐消费者模型仍是**：

```cpp
Window& window = application.Create("Title", 800, 600);
```

测试代码注释与文档都必须标明 `non-owning`，否则会重演 `unique_ptr<Window>`（历史重演）。

### 6.5 可选新增用例（待裁决）

`WindowChrome.ReleaseIsIdempotent`：连续两次 `window->Release()` → 不崩、第二次返回 `true`。把 §4.2 的"幂等"从文字变成可回归断言（用例数 183 → 184）。

---

## 7. 开放项与独立记账

| 项 | 状态 | 说明 |
|---|---|---|
| **`~Application` 残留活窗口 → 潜在析构序隐患** | **独立记账（Deferred）** | **分析**：`Application` 成员声明序 `m_platformApplication → m_windows → m_deferredDestroy → m_running` ⇒ 析构逆序使 `m_deferredDestroy` **先于** `m_windows` 销毁；若此时仍有活窗口，`~Window → Release → DestroyWindow → WM_DESTROY → OnWindowDestroyed` 会向**已结束生命周期的容器**写入，并在 `m_windows` 自身析构过程中 `erase`。**实测**（临时目录独立探针；N=1/2/3/5 窗口 × O0/`-O2`）：**无可观测异常**。⚠️ **结论表述纪律**：记录为「**潜在 Application 析构序隐患**」，**不得**写成"已确认存在 UB"——UB 的判据是"是否访问了已结束生命周期的对象"，不是"跑几次没崩" |
| **`m_running` / `Run`-`Exit` 语义** | **独立记账** | `m_running` 初值 `true`（`Application.h:122`）⇒ `Exit()` 在 `Run()` 之前也生效（真发 `PostQuitMessage`）；其语义实为"`Exit()` 未被调用过"。属 Application 生命周期，与本文档范围分离 |
| **文档命名** | 待确认 | 本文档不属任何 Phase ⇒ 未用 `phaseN-*` 前缀，阶段由头部/§8 跟踪。若希望改为 `phase-deferred-window-ownership.md` 一类，请指明 |
| **详设** | 下一步 | 生命周期闭环已定义死 ⇒ 详设以机械落地为主（精确 diff + 陷阱清单 + 验收执行步骤） |

---

## 8. 修订记录

- v1.2（2026-09-12）**实现落地状态同步（补记）**：原头部记「初设 v1.1 外部评审通过（可进入详细设计）」，实际 B1–B5 **已于 2026-09-12 实施并验证通过**——`ecdi_tests` **183 passed / 0 failed / 183 total**（MinGW g++ 16.1 + `-DCMAKE_CXX_FLAGS=-D_DEBUG`，断言层真正生效）；编译期契约测试（`static_assert` ×3）与静态检查（`new Window` 代码 **1 处** / `make_unique<Window>` 代码 **0 处**）均通过；人工反例（框架外构造 `Window`）**实测编译失败**。实施细节与 3 处实测偏差见详设 **v1.2** §8。
- v1.1（2026-09-12）**外部评审「通过，可进入详细设计」——进详设前 4 项检查已处理**：
  - **① §2.2 三条链**：评审反馈"代码块为空"——**与文件实际内容不符**（原文三块共 14 行内容俱在，疑为粘贴进评审时丢失）。但按其**真实诉求**（"以后看文档的人不用再翻源码"）**照做强化**：改为线性 `↓` 全链形式，补全中间步骤（`unique_ptr` 持有 / `m_hwnd = nullptr` / `ProcessDeferredDestroy` → `~Window`），并逐条标注源码锚点行号。
  - **② §6.1 `static_assert` 可行性与实现方式**：评审提示"C++ 没有自然方式检测构造器不可访问"，要求在详设决定。**已实证**（2026-09-12，等价探针 = 私有构造器 + `friend`）：**MinGW g++ 16.1 与 clang++（LLVM）编译通过** ⇒ `std::is_constructible_v` **尊重访问控制**，负向断言方案可用。**同时发现关键性质**：trait **在中立上下文求值**——写在友元类**内部**同样返回 `false` ⇒ 它**只能做负向断言**，不能正向证明友元权限（正向证据 = `Create` 内那行 `new Window` 能编译）。MSVC/ClangCL **待四工具链确认**（验收 A2），并给出兜底方案（`EXCLUDE_FROM_ALL` 负向编译探针）。
  - **③ §6.3 grep 表述**：原"`new Window\|make_unique<Window>` 只剩 1 处"与 B1-t 的最终形态不对齐 ⇒ 精确化为 `new Window` **恰好 1 处**、`make_unique<Window>` **0 处**。
  - **④ §4.3 措辞纪律**：明确「无 `Run()` → 延后到 `~Application`」是**当前实现路径**，**不得**包装成"设计永久保证某固定顺序"——与 §7 登记的 `~Application` 残留窗口隐患保持区分（事实陈述 vs 待调查项）。
  - **顺带**：文件布局调整——契约主体留在本文档，实施规格另立 `window-ownership-detailed-design.md`（原「不另开文档」措辞随之修订）。
  - **自查修正（本文档 v1.1 内部一致性）**：§5「规模估计」原写「**4** 个生产文件 ≈ 12–15 行」，而 §5 表格实列 **5** 个生产文件（`Window.h` / `Window.cpp` / `Application.h` / `Application.cpp` / `PlatformWindowHost.h`）⇒ 已统一为 **5 个生产文件 ≈ 35 行**（含注释）。
- v1.0（2026-09-12）**初步设计初稿**：
  - **入口**：Phase 12 WindowChrome 实施后，用户运行 MSVC 构建的 `ecdi_tests` 触发 `Application.cpp:92` 断言（`it != m_windows.end()`）。定位为测试替身直接构造 `Window` 绕过登记；**该路径此前从未被触发**（全库构造点仅 2 处，另一处即 `Create` 自身）。
  - **A 阶段（止血）已关闭**：`TestWindow` 改持非拥有 `Window*`，经 `Create()` 取窗口，析构只 `Release()`；**MinGW + `-D_DEBUG` → 183 passed / 0 failed / 退出码 0**，并确认无其它被断言层掩盖的缺陷。已回写 Phase 12 详设 **v1.5**。
  - **B 阶段（本文档）决策来源**：B 方案 → 两轮外部评审（均「通过立项」）→ 三项裁决落定：**① 独立文档 `docs/window-ownership.md`**（不并入 `roadmap-deferred.md`，不假装属某个 Phase）；**② B5b**（析构保持 `public` + 生命周期契约，否决 b5a 私有化析构）；**③ `~Application` 残留窗口独立记账**，不进 B，且结论按"潜在析构序隐患"记录（不写成"已确认 UB"）。
  - **评审采纳要点**：B1 措辞收紧（`friend` 授权整个类，不得写"只有 `Create()` 能构造"）；B3 定 b3a 并要求把设计意图写进注释；B4 同批并重查双向访问关系；**Detached 不承诺**（删除 `CreateDetached` 提法）；**`m_running` 剥离**；**Host 旧注释改写而非删除**；**B 需自己的编译期契约测试**（§6.1）；§6.4 文档纪律（测试替身不得当作推荐用法示例）；不变量第 4 条精确化（§2.3）。
  - **我方补充核查（评审未提）**：`Release()` 语义与幂等性取证（§2.4）；`~Application` 残留窗口路径分析 + 独立探针实测（§7）；B5 的 `default_delete` 编译期陷阱；B1-t 的 `make_unique` 访问检查陷阱。
