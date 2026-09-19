# Phase 16 桌面驻留层（`WindowLayer::Desktop`）详细设计（v1.5）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-19
> 状态：**✅ 评审通过 · 可进实现**（2026-09-19 外部详细设计评审：「**设计上通过，可以进入实现**」「没有发现需要推翻架构或重新做初设级别修改的问题」）——评审另给 **7 条实现级检查项**（非阻塞），已落成 **§9.1 盯防清单**。初设 v1.3 已「评审通过 · O1 已实测关闭」（`SWP_FRAMECHANGED` **不需要**，§6.1 三组 × 3 轮 + v5a 三点量具）。 ★ **v1.2：实施前核对补正 D-6**——`wcscmp` 的包含来源（显式补入 `<cwchar>`；详见 §1.3 D-6 与 §2.4.1）。 ★ **v1.4：三批已实施**（批 A `02f0e34` 纯新增 / 批 B `829bb6d` 行为切换 / 批 C 测试）——**待验收 A1–A8**（四工具链构建与测试运行由用户执行）。
> 上游：[phase16-desktop-layer-requirements.md](phase16-desktop-layer-requirements.md) **v1.4**（D0–D11 已定 · §8.1 两条新约束 · §8.2 八问）· [phase16-desktop-layer-preliminary-design.md](phase16-desktop-layer-preliminary-design.md) **v1.3**（B1–B11 基线 · C1–C12 契约 · O1–O5）
> 相关：phase12-windowchrome-detailed-design.md（**逐文件最小 diff 规格**先例）· phase15-scrollview-detailed-design.md（§1 实施总览 / §5 测试规格 / §6 验收的排版先例）· window-ownership-detailed-design.md（访问权限与生命周期）
> 一句话：**公共 API 头数 92 → 92（净增 0）**，全部改动收敛在 `Win32PlatformWindow` 内部 + 1 个新测试文件。本阶段是项目**首个纯实现层 Phase**。

---

## 1. 实施总览

### 1.1 改动清单

| # | 文件 | 动作 | 规模 | 备注 |
|---|---|---|---|---|
| 1 | `ECDI/include/ECDI/Window/WindowLayer.h` | 修改 | **仅注释**（`Desktop` 档 8 行） | §2.1 —— 原「spike 未通过 ⇒ 退化为 Bottom」已失实 |
| 2 | `ECDI/include/ECDI/Platform/PlatformWindow.h` | 修改 | **仅注释**（`SetWindowLayer` 的 `@details` 3 行） | §2.2 —— ★ **初设漏项**，见 §1.3 D-1 |
| 3 | `ECDI/src/Platform/Win32/Win32PlatformWindow.h` | 修改 | +2 成员 · **+12 方法**（**10** 个类外定义：`ResolveTarget` / `TargetInsertAfter` / `FindDesktopWindow` / `IsDirectlyAboveDesktop` / `ReinsertAboveDesktop` / `ApplyDesktopStyle` / `SyncDesktopHook` / `SyncDesktopHookOff` / `OnForegroundChanged` / `DesktopForegroundProc`；**2** 个内联：观测缝 setter `SetDesktopHookObserverForTests` / 判据 `IsDesktopLayer`）· +1 `using` | §2.3（**v1.3 更正**：原「+9 方法 +1 缝 setter」把内联判据漏计、又把 setter 重复计数） |
| 4 | `ECDI/src/Platform/Win32/Win32PlatformWindow.cpp` | 修改 | **+2** 标准库 include（`<cwchar>` + `<unordered_map>`） · +2 匿名 namespace 实体 · **3 处既有代码改造** · +9 新方法体 | §2.4 |
| 5 | `ECDI/src/Tests/DesktopLayerTests.cpp` | **新建** | T16-1..T16-7（**实测 435 行 / 47 条 `EXPECT_*`**——v1.4 回写，估算偏低见 §2.5） | §2.5 |
| 6 | `ECDI/src/Tests/RunAllTests.h` | 修改 | +1 声明 | §2.6 |
| 7 | `ECDI/src/Tests/RunAllTests.cpp` | 修改 | +1 调用 | §2.6 |
| — | `CMakeLists.txt` | **零改动** | — | `GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库（新 `.cpp` 无需登记） |
| — | `ECDI/ECDI.vcxproj` | **不存在** | — | VS 工程由 `cmake --preset vs2026` 生成（skill 条 66） |

### 1.2 三个规模锚点

| 量 | 前 | 后 | 说明 |
|---|---|---|---|
| **Public 头** | 92 | **92** | 净增 0——本阶段无新头、无新 public 类型 |
| **测试用例** | 210 | **217** | +7（T16-1..T16-7）；`ecdi_tests` 注册总数（实测基线 210，21 个文件） |
| **断言特征串** | 10 | **10** | ★ **零新增断言**——见 §1.3 D-5 |

### 1.3 本稿对初设的**六**处修正（逐条给理由）

| # | 初设 | 本稿 | 理由 |
|---|---|---|---|
| **D-1** | §2.1 与 §5：修改 Public 头 **1**（仅 `WindowLayer.h`） | **2**（+`PlatformWindow.h`，**仍均仅注释**） | `PlatformWindow.h:93-94` 的 `@details` 原文是「**Bottom/Desktop 档持续维护普通窗口层底部位置**」——对 Desktop 档**已失实**（改后它维护的是「紧贴桌面窗口正上方」）。本阶段的核心恰是修正这条语义 ⇒ 注释必须同步，否则公共头在说谎（skill 条 61④ 的「描述类行」纪律） |
| **D-2** | §7 T16-5：「`SendMessageW` 直发 + 观察 z 序邻居」 | **取「真实 `SetWindowPos` + 观察 z 序邻居」**，不伪造 `WINDOWPOS` | 伪造 `WINDOWPOS` 传给 `DefWindowProc` 后它**可能真的应用**该次位置变更——「发给系统不会有事」是**未证实的平台行为假设**（skill 条 31：契约不得建立在未证实的平台行为上）。改用真实 `SetWindowPos` 后，判据与 T16-1 同构（同一 helper），且正对照天然存在 |
| **D-3** | §7：「是否新建文件**归详设**」 | **新建 `DesktopLayerTests.cpp`** | 本阶段测试的最佳样本是**直接构造 `Win32PlatformWindow`**（先例 `DropFilesTests.cpp:133` `Win32PlatformWindow window(host, "DropTest", 200, 200);`）——可**直接**调用 `GetHwndForTests()` / `SetDesktopHookObserverForTests()`，无需经 `Window`→`PlatformWindow&`→`static_cast` 三跳；也避免把 Phase 12 的 `WindowChromeTests.cpp` 变成跨阶段杂糅文件（与 Phase 14/15「每阶段一测试文件」一致） |
| **D-4** | §2.2：「成员（private 区，**`m_windowLayer` 之后**）」 | **private 成员区末尾新增 Phase 16 分组** | `m_windowLayer` 之后是 Phase 12 的 `bool m_shown` / `m_chromeConfigured` / `m_lastWindowState` 三件套，插在其中会割裂既有分组。本项目排版惯例是**每阶段一组**（`Phase 12：…` / `Phase 13：…`）⇒ 追加到成员区末尾 |
| **D-5** | （未提及） | **零新增 `FRAMEWORK_ASSERT`**，并**显式声明** | 本阶段无「可断言且真会触发」的不变量：`ResolveTarget` 是纯分支；`ApplyDesktopStyle` / `ReinsertAboveDesktop` / `SyncDesktopHook` 的每个前置条件都由**显式守卫**返回（守卫即契约，断言只会成为死代码）。⇒ A2 特征串**保持 10 条**（与 Phase 15 同集），不新增登记（skill 条 50⑧④） |
| **D-6** | （未提及；原写「真正的新增只有标准库**一行**」） | **两行**：`<cwchar>` + `<unordered_map>`；调用写**裸** `wcscmp` | 原规格的 `wcscmp` **依赖 `<Windows.h>` 的传递包含**，而全库既无 `wcscmp` 先例、也无任何 `.cpp` include `<cwchar>`。**实测**：MinGW g++（`-c`，exit 0 零警告）与 clang++（`-fsyntax-only`，0 error 0 warning）**不加它都能编过**——机制上可用；但「靠展开链」与**本文件自己的既有认知**相悖（`:15-17` 的 `DrawText` 防护注释正是因为「`dwmapi.h / windowsx.h` 展开链会带进 `Windows.h`」而存在），且该块的 4 个既有标准库 include **逐一核对均被使用**（口径 = 「**列你所用的**」）⇒ **显式补入**：成本 1 行，收益是 TU 自洽。⚠️ 同时修正 §2.4.6 的一处笔误（原称「`wcscmp` 需要 `<cstring>`」——**不成立**） |

---

## 2. 逐文件最小 diff 规格

> 纪律（skill 条 42/43）：**只列真正要动的行** + 前后对照；未列出的行**一律保持现状**；顶层/成员级新增必须写明**落点**。

### 2.1 `ECDI/include/ECDI/Window/WindowLayer.h`（仅注释）

**替换 `:24-32`（`Desktop` 档的文档块）**：

```cpp
	/// @brief 桌面驻留层：被应用窗口覆盖，且 **Win+D 后仍保持可见**
	/// @details **实现已落地**（Phase 16，2026-09-19）——Win32 实现 = 窗口以
	/// `GetWindow(桌面窗口, GW_HWNDPREV)` 为目标位置持续维护「紧贴桌面窗口正上方」，
	/// 并移除 `WS_MINIMIZEBOX`（「显示桌面」只最小化**可最小化**窗口）。
	/// **API 承诺与平台能力解耦**：本枚举只描述层级语义，与之无关的平台手段
	/// （`WS_POPUP` / `WorkerW` 挂载 / `SetParent`）一律不进入公共契约。
	/// ★ 语义状态 ≠ 实现路径（详设 D-DESK-1）：平台实现无论经哪条路径执行，
	/// 本档位的请求语义恒为 Desktop（未来若新增查询 API，必须如实返回 Desktop）。
	Desktop
```

**未动**：`Normal` / `Bottom` 两个档位的文档块（`:13-22`）——逐字保持。

### 2.2 `ECDI/include/ECDI/Platform/PlatformWindow.h`（仅注释 —— ★ 初设漏项）

**替换 `:93-95`（`SetWindowLayer` 的 `@details`）**：

```cpp
	/// @details **Bottom** 档持续维护普通窗口层底部位置；**Desktop** 档持续维护
	/// 「紧贴桌面窗口正上方」（Win+D 后仍可见）——两者均由 Win32 实现经
	/// `WM_WINDOWPOSCHANGING` 维护。切回 Normal 时停止维护（不主动改变当前 z 序——
	/// 交系统自然演化）。
```

**未动**：`:91-92`（`@brief` / `@param`）、`:96`（签名）——逐字保持。

### 2.3 `ECDI/src/Platform/Win32/Win32PlatformWindow.h`

#### 落点 ①：include（无新增）

本文件已有 `<Windows.h>`（`:11`，含 `#undef DrawText` 防护 `:12-14`）与 `<string>`（`:16`）。新增代码只用 `HWINEVENTHOOK` / `HWND` / `LONG` / `DWORD` / `CALLBACK`（均在 `Windows.h`）⇒ **零 include 改动**。

> ⚠️ `<unordered_map>` **不进本头**——它只出现在 `.cpp` 的匿名 namespace（§2.4.2）⇒ 零传播（O4 的确认项）。

#### 落点 ②：public 区——测试缝与纯函数（**插在 `SetDragFinishForTests`（`:79`，其后 `:80` 为空行）之后、`WindowProc` 的文档注释（`:81-82`）之前**）

```cpp
	/// @brief 桌面钩子安装/卸除的观测缝（Phase 16 D9——**函数指针**形态，复刻 `DragFinishFn` 先例）
	/// @details `SetWinEventHook` 依赖**真实桌面环境**，自动化只能断言「装 / 卸被调用了」，
	/// 故以观测缝计数，不去 mock 系统 API（skill 条 51：seam 不出实现层、保 `final`）。
	/// ⚠️ 声明必须位于首个使用点（`m_hookObserver` 成员）之前——与 `DragFinishFn` 同一教训：
	/// GCC 对成员函数形参不做延迟名字查找（放在 private 区会让 MinGW 报 has not been declared）。
	using HookObserverFn = void (*)(bool installed);

	/// @param fn 观测回调（`true` = 已装 / `false` = 已卸）；`nullptr` = 取消观测
	void SetDesktopHookObserverForTests(HookObserverFn fn){ m_hookObserver = fn; }

	/// @brief 把「档位 + 桌面句柄」映射为目标 z 序位置（**纯函数**——O3 定为 C 的落点）
	/// @param layer   当前档位
	/// @param desktop 桌面窗口句柄（`Desktop` 档调用方须传入；其余档位忽略）
	/// @return `HWND_BOTTOM` = Bottom 档 ·「桌面窗口的上一位」= Desktop 档 ·
	///         **`nullptr` = 本次不修改**（Normal 档 / 桌面句柄无效 / 桌面已处 z 序最顶）
	/// @details ⚠️ **`nullptr` 是「跳过」哨兵，不是「插到最底」**——调用方必须显式判空，
	/// 绝不可把它直接交给 `SetWindowPos`（那会落到 `HWND_BOTTOM`，把 Desktop 档
	/// **意外降级**成 Bottom——K8 / 契约 C2 的核心）。
	/// @note **public static 的唯一目的是可被自动化测试直接覆盖**（C2 是本阶段最危险的
	/// 路径）。测试已 include 本内部头（`DropFilesTests.cpp:13` 先例）⇒ 零新文件、零测试缝；
	/// 且它不读实例状态 ⇒ 无副作用、可纯逻辑断言（T16-7）。
	static HWND ResolveTarget(WindowLayer layer, HWND desktop);
```

> 📌 **为什么 `ResolveTarget` 放 public、其余新增方法一律 private**：C2「桌面不可用 ⇒ 不修改（**绝不**降级 `HWND_BOTTOM`）」写错即是**静默降级**，属本阶段最高风险路径；而它**不需要窗口、不需要 explorer** 就能验证。这是 O3 由「倾向 C」**定为 C** 的全部理由（初设 §3.1 ①）。

#### 落点 ③：private 区——方法声明（**插在 `ApplyDwmEnhancements`（`:103`）之后、`m_host`（`:105`）之前**）

```cpp
	// ── Phase 16：桌面驻留层（R1–R4）────────────────────────────────────────

	/// @brief 目标 z 序位置（D8——按档位分流的**唯一**入口）
	/// @details 只做两件事：**判断是否要查桌面句柄**（仅 Desktop 档）+ 交给 `ResolveTarget`。
	/// 语义与返回值同 `ResolveTarget`（`nullptr` = 本次不修改）。
	HWND TargetInsertAfter() const;

	/// @brief 定位桌面窗口（D6 / D7——**不缓存**，每次即时重查）
	/// @return 桌面窗口句柄；`nullptr` = 不可用（explorer 重建窗口期 / 非交互式会话）
	static HWND FindDesktopWindow();

	/// @brief 本窗口是否已「紧贴桌面窗口正上方」（D8 判据——§8.1② 的前置检查）
	/// @details ⚠️ **三态**（契约 C11）：`true` = 已在位 / `false` = 未在位 /
	/// **`true` = 不可判定**——不可判时返回 `true` 的语义是「**禁止无依据的 z-order 操作**」，
	/// **不是**「z 序已满足 C1」。名称保留（改成 `…OrCannotDetermine` 只会让调用点更难读）。
	bool IsDirectlyAboveDesktop() const;

	/// @brief 把窗口重新插到桌面窗口正上方（内部**先判在位**，已在位则什么都不做）
	void ReinsertAboveDesktop();

	/// @brief 应用 / 撤销 Desktop 档所需的样式位（D1 A′ / D3）
	/// @param desktop `true` = 移除 `WS_MINIMIZEBOX`；`false` = 补回
	/// @details **幂等**（目标值与现值相同则不动手——避免多余的 `WM_STYLECHANGING/CHANGED`
	/// 往返）且**可逆**（配置期内允许 `Desktop → Normal` 转移）。
	/// ⚠️ **不需要 `SWP_FRAMECHANGED`**（O1 已实测关闭，初设 §6.1）：`WS_MINIMIZEBOX`
	/// 不参与非客户区几何计算 ⇒ 改这一位没有需要系统重算的对象。
	void ApplyDesktopStyle(bool desktop);

	/// @brief 同步钩子与当前档位（D11 四态表的**唯一**执行点）
	/// @details 幂等：Desktop 档且未装 ⇒ 装；非 Desktop 档且已装 ⇒ 卸；其余不动。
	void SyncDesktopHook();

	/// @brief 强制卸除钩子（`Release()` / 析构路径专用——**只减不增**，不读档位）
	void SyncDesktopHookOff();

	/// @brief 前台事件回调的转发落点（在**注册线程**上执行——契约 C8）
	/// @param foreground 成为前台的窗口（可能不是本窗口）
	void OnForegroundChanged(HWND foreground);

	/// @brief 静态窗口过程式回调（`SetWinEventHook` **无 user-data 参数** ⇒ 经 hook 反查实例）
	/// @details 签名与 `WINEVENTPROC` 一致。**既有先例**：`&Win32PlatformWindow::WindowProc`
	/// 以同款「`static … CALLBACK` 成员 → Win32 回调指针」形态传进 `WNDCLASSW.lpfnWndProc`
	/// （`Win32WindowClass.cpp:28`）⇒ 四工具链均已有实证（O5 的判据之一）。
	static void CALLBACK DesktopForegroundProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
		LONG idObject, LONG idChild, DWORD thread, DWORD time);

	/// @brief 当前是否处于 Desktop 档（判据集中——避免各处重复比较枚举）
	bool IsDesktopLayer() const noexcept{ return m_windowLayer == WindowLayer::Desktop; }
```

#### 落点 ④：private 成员（**插在 `m_imeResultPendingChars`（`:122-123`）之后、类闭合 `};`（`:125`）之前**）

```cpp
	// ── Phase 16：桌面驻留层状态 ─────────────────────────────────────────

	HWINEVENTHOOK m_desktopHook = nullptr;	///< 桌面驻留维护钩子（D11——**仅 Desktop 档持有**，其余档恒 `nullptr`）
	HookObserverFn m_hookObserver = nullptr;	///< 钩子观测缝（测试用——生产恒 `nullptr`，见 public 区的 setter）

```

> ⚠️ **`HookObserverFn` 的声明顺序**：`using` 在 public 区（落点 ②），成员在 private 区 —— 类内 **public 先于 private** 出现，且**成员函数体内引用后置成员是合法的**（完整类上下文）⇒ 无 GCC 顺序问题。这一点与 `DragFinishFn` 的处境**不同**（那个别名被**形参类型**引用，必须在首个使用点之前）。

### 2.4 `ECDI/src/Platform/Win32/Win32PlatformWindow.cpp`

#### 2.4.1 include：**+2 行**（**标准库段** —— 项目头段零新增）

**❌ 不加这一行**（考虑过并否决——D-5 已定**零新增断言**，故 `ECDIAssert.h` 不需要；此处显式记录，以免实施者以为漏了）：

```cpp
#include "ECDI/Core/ECDIAssert.h"
```

**✅ 真正新增的标准库两行**（字典序插入既有 4 行之间）：

```cpp
#include <cstring>                     // 既有（:19）
#include <cwchar>                      // ★ Phase 16 新增：IsDesktopClassWindow 的裸 wcscmp（D-6 · §2.4.2）
#include <string>                      // 既有（:20）
#include <system_error>                // 既有（:21）
#include <unordered_map>               // ★ Phase 16 新增：hook → 实例 反查表（仅 .cpp——不进任何头）
#include <vector>                      // 既有（:22）
```

> ⚠️ **顺序纪律**：`<cstring>` **`<cwchar>`** `<string>` `<system_error>` **`<unordered_map>`** `<vector>` —— 字典序
> （`cstring < cwchar`：第 2 字符 `s` < `w`）。Windows 平台头（`<Windows.h>` 等，`:9-13`）在**标准库之前**、**项目头之后**（本文件既有顺序，不动）。

> ⚠️ **为什么 `<cwchar>` 必须显式加（D-6——**实测结论**，不是保守估计）**：`wcscmp` 归 `<cwchar>`，**不属于** `<cstring>`。
> 实测两个工具链在不加它时**都能编过**（`<Windows.h>` 的展开链把它带了进来）：
>
> | 工具链 | 命令 | 结果 |
> |---|---|---|
> | **MinGW g++** | `-std=c++20 -Wall -Wextra -c` | **exit 0，零警告** |
> | **clang++** | `-std=c++20 -Wall -Wextra -fsyntax-only` | **0 error / 0 warning** |
>
> 仍然显式加，理由两条：① 该块既有的 4 个标准库 include **逐一核对均被使用**（`memcpy`×1 / `std::string`×11 /
> `std::system_error`×1 / `std::vector`×1）⇒ 这个块的口径是「**列你所用的**」；② **本文件自己就记着「头展开链不稳定」**——
> `:15-17` 的 `DrawText` 防护注释明写 `dwmapi.h / windowsx.h` 的展开链也可能带进 `Windows.h` ⇒ 依赖展开链与这条既有认知相悖。
>
> **调用写法**：**裸 `wcscmp`**（不带 `std::`）——全库 c-函数一律裸调用（`memcpy` 2 处 · `wcslen` 2 处），只有 `std::abs` 带前缀（那是 `<cmath>` 重载解析的需要）。
> **为什么不用 `lstrcmpW`**：它是 locale 敏感的 `CompareString` 语义；类名比较要的是**序数**比较。


#### 2.4.2 匿名 namespace：+2 实体（**插在 `ClipboardGuard`（`:38-47`）之后、闭括号 `}`（`:49`）之前**）

```cpp
/// @brief hook → 实例 映射（Phase 16 D5：每窗口一个钩子 ⇒ 回调必须能反查 owner）
/// @details `SetWinEventHook` 的回调签名固定且**无 user-data 参数**，而框架须支持多窗口
/// ⇒ 以**回调首参（hook 自身）**为键反查实例。**替代方案（进程级窗口列表 + 广播）已否决**：
/// 那要求回调遍历所有窗口并各自判断「是否该动」，把「谁的维护」变成全局语义。
/// ⚠️ **线程安全**：`WINEVENT_OUTOFCONTEXT` 的回调在**注册钩子的那个线程**的消息循环中执行
/// ⇒ 与 Install / Uninstall 天然同线程 ⇒ **无需加锁**（写入契约 C8）。
/// ⚠️ 函数局部 `static` 的析构发生在进程退出——此时任何存活窗口自身即属泄漏，无实际风险。
std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*>& HookOwners(){

	static std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*> owners;

	return owners;

}

/// @brief 桌面类窗口判定（钩子过滤与 z 序判据共用同一份类名集合）
/// @details `Progman` = 桌面本体；`WorkerW` = 壁纸/`SHELLDLL_DefView` 宿主（部分 Windows
/// 版本「显示桌面」抬升的是它而非 `Progman`）⇒ 两者都算「桌面层被抬升」。
/// ⚠️ 类名缓冲 32 宽字符足够（两名称最长 7），`GetClassNameW` 返回值 0 视为不匹配。
bool IsDesktopClassWindow(HWND hwnd){

	wchar_t buf[32]{};

	if (GetClassNameW(hwnd, buf, 32) == 0){ return false; }

	// 序数比较（`<cwchar>` 已在 include 段显式补入——D-6 / §2.4.1）。
	// ⚠️ 不用 lstrcmpW：那是 locale 敏感的 CompareString 语义，类名比较要的是序数。
	return wcscmp(buf, L"Progman") == 0 || wcscmp(buf, L"WorkerW") == 0;

}
```

#### 2.4.3 `Release()` —— 修 B4 的判空陷阱

**替换 `:187-197`**：

```cpp
bool Win32PlatformWindow::Release() noexcept {

	// ★ Phase 16（契约 C12）：**脱钩必须在 hwnd 判空之前** —— 旧实现
	//   `if (m_hwnd==nullptr) return true;` 会让「hwnd 已空、钩子仍在」的路径
	//   **跳过全部清理** ⇒ 回调打到已析构对象（UB）。**钩子与 hwnd 是相互独立的资源**，
	//   不可用同一个判空条件代表两者（与 Phase B 的 Window 所有权教训同族）。
	SyncDesktopHookOff();

	if (m_hwnd==nullptr) {

		return true;

	}

	return DestroyWindow(m_hwnd) != FALSE;

}
```

> **幂等性保持**：`SyncDesktopHookOff()` 首行即 `if (m_desktopHook == nullptr) return;` ⇒ 重复 `Release()`（`~` 又调一次）无副作用、**不重复通知观测缝**。

#### 2.4.4 `WM_WINDOWPOSCHANGING` —— 按档位分流（D8，本阶段的核心 diff）

**替换 `:404-424`**：

```cpp
	// ── Phase 12 R10 / Phase 16 D8：持续维护层级档位对应的 z 序位置 ─────────
	case WM_WINDOWPOSCHANGING: {

		// ⚠️ 必须是「持续维护」而非一次性 SetWindowPos（其他程序会把我们顶下来）；
		// ⚠️ 只改 hwndInsertAfter，不碰 x/y/cx/cy/flags（否则会干扰最大化/还原几何）；
		// ⚠️ 契约边界：不承诺阻止第三方 SetWindowPos 造成的瞬时 z 序变化（Windows z 序是动态的）
		// ★ Phase 16（D8）：判据由「!= Normal ⇒ 一律 HWND_BOTTOM」改为**按档位分流**——
		//   Normal 不修改 / Bottom 保持既有语义（逐位等价）/ Desktop 贴桌面窗口正上方。
		auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);

		if ((wp->flags & SWP_NOZORDER) == 0){

			const HWND target = TargetInsertAfter();

			// ★ K8 / C2：target == nullptr 表示**本次不修改**（Normal 档 / 桌面句柄无效 /
			//   桌面已处 z 序最顶）——**绝不可**把它交给 SetWindowPos（那会落到
			//   HWND_BOTTOM，把 Desktop 档意外降级成 Bottom）。保持原值是唯一安全选择。
			if (target != nullptr){

				wp->hwndInsertAfter = target;

			}

		}

		break;   // 走 DefWindowProc（几何变更仍由系统处理）

	}
```

**等价性核对表（零回归的结构保证——`Bottom` / `Normal` 逐位等价）**：

| 档位 | 旧行为（`:410-418`） | 新行为 | 等价？ |
|---|---|---|---|
| `Normal` | 判据 `!= Normal` 不成立 ⇒ 不读 `wp`、不改 | 读 `wp` → `TargetInsertAfter()` 返回 `nullptr` ⇒ 不改 | ✅ **逐位等价**（多一次只读的 `lParam` 解引用，语义中性） |
| `Bottom` | `wp->hwndInsertAfter = HWND_BOTTOM` | `ResolveTarget` 在 `layer == Bottom` 分支直接返回 `HWND_BOTTOM` ⇒ 同 | ✅ **逐位等价** |
| `Desktop` | ⚠️ 同 `Bottom`（**错误**——R1 从未实现） | 桌面窗口的上一位（或跳过） | **本阶段修正对象** |

> ⚠️ **`Bottom` 档不会因此变慢**：`TargetInsertAfter()` 只在 Desktop 档才调 `FindDesktopWindow()`
> （§2.4.6 的三元表达式）⇒ **Bottom 档零新增系统调用**（`ResolveTarget` 在 Bottom 分支提前返回）。

#### 2.4.5 `SetWindowLayer` —— 样式位 + 钩子 + 分流派发

**替换 `:842-887`**：

```cpp
void Win32PlatformWindow::SetWindowLayer(WindowLayer layer){

	// 配置期契约：与 chrome 三件套同一生命周期。
	// API 签名不因此锁死——未来若出现运行期切层需求，只需松开此判据（零签名变更）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetWindowLayer ignored after Show() - layer is config-time only");

		return;

	}

	if (m_windowLayer == layer){

		return;   // 幂等

	}

	m_windowLayer = layer;   // 语义状态恒记录用户请求（D-DESK-1：不随实现路径降级）

	// ── Phase 16 D1(A′) / D3：Desktop 档移除 WS_MINIMIZEBOX ────────────────
	// 真因（需求稿 §8 P0 实测）：「显示桌面」只最小化**可最小化**窗口 ⇒ 差异是**一位**，
	// 不是整个窗口形态 ⇒ 保留 WS_OVERLAPPEDWINDOW 的其余系统红利（Alt+Space /
	// Aero Snap / 最大化动画），仅让 Desktop 档放弃「可最小化」这一条。
	ApplyDesktopStyle(IsDesktopLayer());

	// ── Phase 16 D11：钩子生命周期（Set ⇒ 装 / 离档 ⇒ 卸；Hide / Show **不参与**）──
	SyncDesktopHook();

	if (IsDesktopLayer()){

		// K2 修正：原文案写 "spike pending"（该 spike 已于 2026-09-15 结项）——
		// 本阶段即为落地，不再降级执行。
		Logger::Log(LogLevel::Info,
			L"WindowChrome: WindowLayer::Desktop enabled - window is wedged above the desktop window");

	}

	if (m_hwnd == nullptr){

		return;   // 无窗口：仅记录状态（Show 后由 WM_WINDOWPOSCHANGING 自然生效）

	}

	// 切到 Bottom/Desktop：立即派发一次（后续由 WM_WINDOWPOSCHANGING 持续维护）；
	// 切回 Normal：target == nullptr ⇒ **不主动改变当前 z 序**（交系统自然演化——
	// 避免「突然跳到最前」的反直觉效果）。
	const HWND target = TargetInsertAfter();

	if (target != nullptr){

		SetWindowPos(m_hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}
```

**与旧实现的差异（只有三处，其余行保持现状）**：

| # | 变化 | 说明 |
|---|---|---|
| ① | 删掉 `:863-870` 的 `LogLevel::Warning`（"not yet validated (spike pending) - executed as Bottom"） | 换为 `:312-317` 的 `LogLevel::Info` 启用提示（**文案语义反转**：从「未验证 + 降级」到「已启用 + 生效」） |
| ② | 新增 `ApplyDesktopStyle(...)` 与 `SyncDesktopHook()` 两次调用 | 落在 `m_windowLayer = layer;` 之后、`m_hwnd` 判空**之前**（样式与钩子是**配置期**资源，与 hwnd 判空无关——见 §3.3 时序） |
| ③ | `:880-885` 的 `if (m_windowLayer != Normal) SetWindowPos(… HWND_BOTTOM …)` → `TargetInsertAfter()` + 判空 | Desktop 档不再被压到 `HWND_BOTTOM`（**R1 的兑现**） |

#### 2.4.6 九个新方法体（**追加在 `SetWindowLayer` 定义之后（原 `:887` 之后），按头文件声明顺序**）

```cpp
// ══════════════════════════════════════════════════════════════════════════
// Phase 16：桌面驻留层（R1–R4）
// ══════════════════════════════════════════════════════════════════════════

HWND Win32PlatformWindow::ResolveTarget(WindowLayer layer, HWND desktop){

	// ── Normal 档：不参与 z 序维护 ⇒ 返回「跳过」哨兵 ──
	if (layer == WindowLayer::Normal){

		return nullptr;

	}

	// ── Bottom 档：既有语义（Phase 12 已实现）——逐位不变 ──
	if (layer == WindowLayer::Bottom){

		return HWND_BOTTOM;

	}

	// ── Desktop 档：紧贴桌面窗口正上方 ──
	if (desktop == nullptr || !IsWindow(desktop)){

		// ★ K8 / C2：桌面窗口不可用（explorer 重建窗口期 / 非交互式会话）⇒ **跳过本次修改**。
		//   绝不可 fallback 到 HWND_BOTTOM —— 那会把 Desktop 档意外降级成 Bottom，
		//   是「比不重插更糟」的位置（desktop_spike.cpp:592-594 已显式防御同一陷阱）。
		return nullptr;

	}

	// 桌面窗口的上一位 =「紧贴着它的那个位置」。
	// ⚠️ 返回 nullptr（桌面已在 z 序最顶）同样是**跳过** —— 此时「紧贴其上」不可能，
	//    而任何替代位置（HWND_BOTTOM / HWND_TOP）都违反契约。
	return GetWindow(desktop, GW_HWNDPREV);

}

HWND Win32PlatformWindow::TargetInsertAfter() const{

	// ⚠️ **只让 Desktop 档去查桌面句柄**——Normal / Bottom 不看它。
	//    这让 WM_WINDOWPOSCHANGING 在 **Bottom 档下不产生任何 GetShellWindow() 调用**
	//    （零新增成本：Bottom 是既有档位，不应因本阶段变慢）。
	const HWND desktop = IsDesktopLayer() ? FindDesktopWindow() : nullptr;

	return ResolveTarget(m_windowLayer, desktop);

}

HWND Win32PlatformWindow::FindDesktopWindow(){

	// D7：实测 `GetShellWindow()` ≡ `FindWindowW(L"Progman")`（四组独立运行一致）⇒ 取
	// **官方 API**：语义直述（「shell 的桌面窗口」）、无按类名枚举的成本。
	// ⚠️ 返回值仍须 `IsWindow` 校验——explorer 重建窗口期可能短暂失效（K8）；
	//    调用方（ResolveTarget / IsDirectlyAboveDesktop）一律按「不可用 ⇒ 跳过」处理。
	// ★ D6：**不缓存** ⇒ 不需要「缓存 + 有效性检测 + 重定位」状态机——explorer 重建
	//   `Progman` 之后，下一次查询**天然**拿到新句柄。代价实测 0.8 次/秒（可忽略）。
	return GetShellWindow();

}

bool Win32PlatformWindow::IsDirectlyAboveDesktop() const{

	// ★ 契约 C11（三态）：true = 已在位 / false = 未在位 / **true = 不可判定**。
	//   不可判时返回 true 的含义是「**禁止无依据的 z-order 操作**」，
	//   **不是**「z 序已满足 C1」。
	if (m_hwnd == nullptr || !IsWindow(m_hwnd)){ return true; }   // 不可判 ⇒ 抑制动作

	const HWND desktop = FindDesktopWindow();

	if (desktop == nullptr || !IsWindow(desktop)){ return false; }   // 桌面不可用 ⇒ 未在位

	return GetWindow(desktop, GW_HWNDPREV) == m_hwnd;

}

void Win32PlatformWindow::ReinsertAboveDesktop(){

	if (!IsDesktopLayer() || m_hwnd == nullptr || !IsWindow(m_hwnd)){

		return;

	}

	// ★ 需求稿 §8.1②（本阶段**正式设计输入**）：先判在位，已在位则什么都不做。
	//   路线 E 的原始实现是**无条件重插**——实测那是 Win+D 瞬间「闪烁一下」的来源之一
	//   （用户目视确认）。在位检查把抖动降到最小，且零额外成本。
	if (IsDirectlyAboveDesktop()){

		return;

	}

	const HWND target = TargetInsertAfter();

	if (target != nullptr){

		SetWindowPos(m_hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}

void Win32PlatformWindow::OnForegroundChanged(HWND foreground){

	// 只有「桌面层被抬升」才需要跟随（Win+D / Show Desktop 的机制即此——spike §7.1）。
	if (!IsDesktopClassWindow(foreground)){

		return;

	}

	ReinsertAboveDesktop();

}

void CALLBACK Win32PlatformWindow::DesktopForegroundProc(HWINEVENTHOOK hook, DWORD event,
	HWND hwnd, LONG idObject, LONG /*idChild*/, DWORD /*thread*/, DWORD /*time*/){

	// 三重过滤：事件类型 / 窗口句柄 / 对象粒度（只关心窗口级前台变更）
	if (event != EVENT_SYSTEM_FOREGROUND || hwnd == nullptr || idObject != OBJID_WINDOW){

		return;

	}

	const auto it = HookOwners().find(hook);

	if (it == HookOwners().end()){

		// 已卸除（回调与 UnhookWinEvent 之间的窗口期——见 SyncDesktopHookOff 的注销顺序）
		return;

	}

	it->second->OnForegroundChanged(hwnd);

}

void Win32PlatformWindow::ApplyDesktopStyle(bool desktop){

	if (m_hwnd == nullptr){

		return;   // 无窗口无从改样式（构造期已创建 HWND，此处仅防御）

	}

	const LONG_PTR current = GetWindowLongPtrW(m_hwnd, GWL_STYLE);

	const LONG_PTR wanted = desktop
		? (current & ~static_cast<LONG_PTR>(WS_MINIMIZEBOX))
		: (current |  static_cast<LONG_PTR>(WS_MINIMIZEBOX));

	if (wanted == current){

		return;   // 幂等：已是目标值则不动手（避免多余的 WM_STYLECHANGING/CHANGED 往返）

	}

	// ⚠️ **不需要 SWP_FRAMECHANGED**（O1 已实测关闭，初设 §6.1）：只改样式位，
	//    非客户区几何逐位不变（v5a 三点量具 A = B = C = 0 × 0）。
	//    Borderless 档的 SetChromeMode 已在配置期派发过一次（:800-804），
	//    那次重算发生在本调用**之前**，且不因改这一位失效 ⇒ 既不必要、也不应重复派发。
	SetWindowLongPtrW(m_hwnd, GWL_STYLE, wanted);

}

void Win32PlatformWindow::SyncDesktopHook(){

	const bool want = (IsDesktopLayer() && m_hwnd != nullptr);

	if (want && m_desktopHook == nullptr){

		// hInstance 可传 nullptr（D5 / K9）：WINEVENT_OUTOFCONTEXT 的回调在**本进程内**
		// 由系统在注册线程的消息循环中调用，不要求回调位于 DLL ⇒ 无需模块句柄。
		m_desktopHook = SetWinEventHook(
			EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
			nullptr, &Win32PlatformWindow::DesktopForegroundProc,
			0, 0, WINEVENT_OUTOFCONTEXT);

		if (m_desktopHook != nullptr){

			HookOwners()[m_desktopHook] = this;

			if (m_hookObserver != nullptr){ m_hookObserver(true); }

		}

	}
	else if (!want && m_desktopHook != nullptr){

		SyncDesktopHookOff();

	}

}

void Win32PlatformWindow::SyncDesktopHookOff(){

	if (m_desktopHook == nullptr){ return; }   // 幂等（Release 与析构会各调一次）

	// ★ 注销顺序（契约 C6 / C12）：**先从映射表移除，再 UnhookWinEvent** ——
	//   反序会让「回调已取出 owner、而对象正在析构」成为可能（UAF）。
	//   移除后即使回调窗口期内仍被触发，`HookOwners().find()` 也会返回 end() ⇒ 安全返回。
	HookOwners().erase(m_desktopHook);

	UnhookWinEvent(m_desktopHook);

	m_desktopHook = nullptr;

	if (m_hookObserver != nullptr){ m_hookObserver(false); }

}
```

> 📌 **两处实现注意**：
> ① `wcscmp` 归 **`<cwchar>`**（**不是** `<cstring>`——这是 v1.0 的笔误，v1.2 已正）⇒ 已在 §2.4.1 **显式补入** `#include <cwchar>`（D-6）。
> ② `EVENT_SYSTEM_FOREGROUND` / `OBJID_WINDOW` / `WINEVENT_OUTOFCONTEXT` / `GetShellWindow` / `UnhookWinEvent` / `HWINEVENTHOOK` 全部来自 `<Windows.h>`（本文件 `:9` 已含，且 `:15-17` 有 `DrawText` 防护）✅。

### 2.5 新建 `ECDI/src/Tests/DesktopLayerTests.cpp`

**结构（实测 **435 行 / 47 条 `EXPECT_*`**——立项估「约 250 行」未计项目「每语句间空行」排版惯例，偏差 +74%）：include 段 → `LayerHost` → 3 个 helper → 7 个用例 → `RegisterDesktopLayerTests()`。

#### 2.5.1 include 段（照抄 `DropFilesTests.cpp` 的骨架）

```cpp
#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "Platform/Win32/Win32PlatformWindow.h"
#include "ECDI/Window/WindowLayer.h"

using namespace ECDI;
```

> ⚠️ **顺序与既有测试文件逐字一致**（`DropFilesTests.cpp:1-16` 即为模板）：测试基础设施头
> （`RunAllTests.h` / `TestFramework.h`）→ **`<Windows.h>`** → `#undef DrawText` → 平台 / 框架头 → 标准库。
> ⚠️ `#undef DrawText` **必须紧跟 `<Windows.h>` 且在任何 ECDI 头之前**（规范 10：`DrawTextW` 宏会污染
> `RenderingBackend` 一类接口声明——**传递 include 也算**）。
> ⚠️ skill 条 8 的「项目头 → `<Windows.h>` → 标准库」是**框架源文件**的顺序；测试文件因先包含测试基础设施头，
> 其既有排版如上——**跟随本目录先例，不引入第三种顺序**。

#### 2.5.2 `LayerHost`——最小 Host 替身（**照抄 `DropFilesTests.cpp:28-73` 的 9 个 override，全部空实现**）

```cpp
namespace{

/// @brief 最小 Host 替身（`PlatformWindowHost` 的 9 个纯虚全部空实现）
/// @details 本组用例**不经 `Application` / `Window`**——直接栈上构造 `Win32PlatformWindow`
/// 即可（`DropFilesTests.cpp:133` 先例），从而直呼内部方法与观测缝，免去三层 static_cast。
struct LayerHost final : public PlatformWindowHost{

	void OnPaint() override{}

	void OnResized(int, int) override{}

	void OnExitSizeMove() override{}

	Window* GetWindow() const noexcept override{ return nullptr; }

	void OnEvent(const Event&) override{}

	void OnIMEComposition() override{}

	void OnIMECompositionUpdate(const std::string&) override{}

	void OnIMECompositionCommit(const std::string&) override{}

	bool IsClientInteractiveAt(int, int) const noexcept override{ return false; }

};
```

> ⚠️ `PlatformWindowHost` 的纯虚共 **9** 个（实测 `PlatformWindowHost.h`：`OnPaint` / `OnResized` /
> `OnExitSizeMove` / `GetWindow` / `OnEvent` / `OnIMEComposition` / `OnIMECompositionUpdate` /
> `OnIMECompositionCommit` / `IsClientInteractiveAt`）——**漏一个即编译不过**（无默认实现）。

#### 2.5.3 三个 helper

```cpp
/// @brief 手动消息泵（与 `WindowChromeTests.cpp:99` 同款）
void PumpMessages(int maxCount){

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i){

		TranslateMessage(&msg);

		DispatchMessageW(&msg);

	}

}

/// @brief z 序判据：`upper` 是否位于 `lower` **之上**
/// @details `GetWindow(h, GW_HWNDNEXT)` = 给定窗口**下方**的窗口 ⇒ 自 `upper` 沿链向下走，
/// 遇到 `lower` 即为真。⚠️ 加迭代上限：防御异常 z 序造成死循环（正常链长远小于该值）。
bool IsAboveInZOrder(HWND upper, HWND lower){

	for (int guard = 0; guard < 4096; ++guard){

		upper = GetWindow(upper, GW_HWNDNEXT);

		if (upper == nullptr){ return false; }

		if (upper == lower){ return true; }

	}

	return false;

}

/// @brief 钩子观测缝的计数落点（生产恒 `nullptr`——见 `SetDesktopHookObserverForTests`）
int g_hookOn  = 0;

int g_hookOff = 0;

void CountingHookObserver(bool installed){ installed ? ++g_hookOn : ++g_hookOff; }
```

#### 2.5.4 七个用例的断言规格

| # | 用例名 | 关键代码 / 断言 |
|---|---|---|
| **T16-1** | `DesktopLayer.BottomLayerRegression` | 建 `ref`（Normal）+ `bot`（Bottom）：`ref.Show()` → `bot.SetWindowLayer(Bottom)` → `bot.Show()` → `PumpMessages(64)`；**正对照** `EXPECT_TRUE(IsWindowVisible(ref…))` + `IsWindowVisible(bot…)`；**断言** `EXPECT_TRUE(IsAboveInZOrder(ref.GetHwndForTests(), bot.GetHwndForTests()))`（★ `bot` 后显示却被压到 `ref` 之下 ⇒ 维护生效） |
| **T16-2** | `DesktopLayer.StyleStripsMinimizeBox` | `before = GetWindowLongPtrW(hwnd, GWL_STYLE)`；**前提** `EXPECT_TRUE((before & WS_MINIMIZEBOX) != 0)`；`SetWindowLayer(Desktop)` 后：`(after & WS_MINIMIZEBOX) == 0` 且 `(after & ~WS_MINIMIZEBOX) == (before & ~WS_MINIMIZEBOX)`（其余位逐位保持）+ 逐条 `WS_CAPTION` / `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MAXIMIZEBOX` 均非 0 |
| **T16-3** | `DesktopLayer.StyleRoundTripAndIdempotent` | `original = style`；`Desktop` ⇒ 位清零；`Normal` ⇒ **`style == original`（逐位复原）**；连续两次 `Desktop` ⇒ 两次读值相等（幂等）；再 `Normal` ⇒ `style == original` |
| **T16-4** | `DesktopLayer.HookLifecycle` | **两窗口**（见下 §2.5.5 的 A / B 分工）；逐次断言 `g_hookOn` / `g_hookOff` **精确计数**（不是"至少一次"） |
| **T16-5** | `DesktopLayer.NormalLayerNotMaintained` | 三窗口 `ref` / `normal` / `bottom`；`ref.Show()` → **正对照** `bottom.SetWindowLayer(Bottom); bottom.Show()` ⇒ `IsAboveInZOrder(ref, bottom)` 为真（**证明维护机制在工作**）→ 被测 `normal.Show()` ⇒ `IsAboveInZOrder(normal, ref)` 为真（**Normal 不受维护、后显示者在上**，契约 C3） |
| **T16-6** | `DesktopLayer.ConfigTimeContract` | `Set(Desktop)` → **`Show()` + 泵消息** → **然后**记 `styled` / `onBefore` / `offBefore`（★ 基线在 Show **之后**——`GWL_STYLE` 整值含**系统动态位** `WS_VISIBLE`，由 `ShowWindow` 写入；Show 前取基线 + 整值比较 = **假失败**，v1.5 实测）→ 运行期 `Set(Normal)` + `Set(Bottom)`；断言两个独立观测代理均未变：**`(style & ~kDynamic) == (styled & ~kDynamic)`**（`kDynamic = WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE`——系统自管位，不归本契约；若被拒路径错跑 `ApplyDesktopStyle(false)`，`WS_MINIMIZEBOX` 被补回 ⇒ 掩码比较照样抓到）∧ `g_hookOn == onBefore` ∧ `g_hookOff == offBefore`（契约 C10） |
| **T16-7** | `DesktopLayer.ResolveTargetTruthTable` | 五条纯逻辑断言（见下 §2.5.6）——**不需要 explorer、不需要 Show** |

#### 2.5.5 T16-4 的两窗口分工（**必须拆**，否则测不到）

| 窗口 | 序列 | 覆盖 | 为什么不能合并 |
|---|---|---|---|
| **A** | `Set(Desktop)` → `Show` → `Hide` → `Show` → `Release()` | 装 ×1 · **Hide 不卸** · **Show 不重装** · **Release 必卸 ×1** | `Show()` 之后 `m_shown == true` ⇒ 再调 `SetWindowLayer` 会被 **C10 配置期契约拒绝** ⇒ **无法在同一窗口上测「离档 ⇒ 卸」** |
| **B** | `Set(Desktop)` → `Set(Normal)` | **离档 ⇒ 卸 ×1**（配置期内） | 需在 `Show()` **之前**完成 |

**A 的精确计数序列**（`g_hookOn` / `g_hookOff` 从 0 起）：

```
Set(Desktop)  →  on=1, off=0
Show / Hide / Show（各 PumpMessages(32)）  →  on=1, off=0   ← ★ Hide 未卸、Show 未重装
Release()     →  on=1, off=1                             ← ★ 必卸，且不重复
（块结束，析构再调 Release ⇒ 无第二次 off —— 幂等）
```

**B 的精确计数序列**：`Set(Desktop)` ⇒ `on=2`；`Set(Normal)` ⇒ `off=2`（且 `on` 仍为 2——**离档不装**）。

> ⚠️ **A 的 `EXPECT_TRUE(g_hookOn == 1)` 同时是一条环境断言**：「`SetWinEventHook` 在本机成功」。
> 若它失败，`m_desktopHook` 保持 `nullptr`、观测缝**不会被调用** ⇒ T16-4 **响亮失败**（其含义是
> 「Desktop 档在本环境完全不可用」，正是应当失败的场景）。见 §8 L6。

#### 2.5.6 T16-7 的五条真值表（**顺序有讲究**——避开句柄回收）

```
① Normal  + nullptr            ⇒ nullptr
② 建 tmp 窗口 → dead = hwnd → tmp.Release() → 断言 IsWindow(dead) == FALSE   ← 正对照
③ Desktop + nullptr            ⇒ nullptr  ∧  != HWND_BOTTOM
④ Desktop + dead（失效句柄）    ⇒ nullptr  ∧  != HWND_BOTTOM        ← ★ C2 的核心
⑤ 建 live 窗口 → valid = hwnd
   Normal  + valid             ⇒ nullptr
   Bottom  + nullptr / valid   ⇒ HWND_BOTTOM（与 desktop 参数无关）
   Desktop + valid             ⇒ == GetWindow(valid, GW_HWNDPREV) ∧ != HWND_BOTTOM
```

> ⚠️ **② 必须在 ⑤ 之前**：若先创建 `live`，系统**可能回收** `dead` 的句柄值 ⇒ `IsWindow(dead)`
> 变真 ⇒ ④ 的「失效句柄」前提不成立（**假阴性**）。顺序即判据的一部分（skill 条 75：先说清前提怎么成立）。

#### 2.5.7 注册（文件末尾，与既有文件同款）

```cpp
void ECDI::Test::RegisterDesktopLayerTests(){

	GetTestRegistry().Add("DesktopLayer.BottomLayerRegression",         &TestBottomLayerRegression);
	GetTestRegistry().Add("DesktopLayer.StyleStripsMinimizeBox",        &TestStyleStripsMinimizeBox);
	GetTestRegistry().Add("DesktopLayer.StyleRoundTripAndIdempotent",   &TestStyleRoundTripAndIdempotent);
	GetTestRegistry().Add("DesktopLayer.HookLifecycle",                 &TestHookLifecycle);
	GetTestRegistry().Add("DesktopLayer.NormalLayerNotMaintained",      &TestNormalLayerNotMaintained);
	GetTestRegistry().Add("DesktopLayer.ConfigTimeContract",            &TestConfigTimeContract);
	GetTestRegistry().Add("DesktopLayer.ResolveTargetTruthTable",       &TestResolveTargetTruthTable);

}
```

> 📌 命名前缀 `DesktopLayer.` 全库唯一（须实施后以脚本扫描注册名去重——Phase 15 同款自查）。

### 2.6 `RunAllTests.h` / `RunAllTests.cpp`

| 文件 | 改动 |
|---|---|
| `RunAllTests.h` | **在 `:32` `RegisterScrollViewTests();` 之后** +1 行：`void RegisterDesktopLayerTests();   ///< Phase 16：桌面驻留层（层级分流 / 样式位 / 钩子生命周期 / ResolveTarget 真值表）` |
| `RunAllTests.cpp` | **在 `:30` `RegisterScrollViewTests();` 之后** +1 行：`RegisterDesktopLayerTests();   // Phase 16：桌面驻留层` |

---

## 3. 关键行为冻结

### 3.1 `ResolveTarget` 真值表（冻结——T16-7 逐行覆盖）

| `layer` | `desktop` | 返回 | 语义 |
|---|---|---|---|
| `Normal` | 任意 | `nullptr` | 跳过（不参与维护，C3） |
| `Bottom` | 任意 | `HWND_BOTTOM` | 既有语义（逐位不变，C4） |
| `Desktop` | `nullptr` | `nullptr` | 跳过（C2） |
| `Desktop` | 非窗口句柄 | `nullptr` | 跳过（C2） |
| `Desktop` | 有效句柄 | `GetWindow(desktop, GW_HWNDPREV)` | 紧贴桌面之上（C1）；**若为 `nullptr`（桌面已最顶）仍表示跳过** |

### 3.2 调用序列（Borderless + Desktop —— O1 结论的落点）

```
CreateWindowExW(WS_OVERLAPPEDWINDOW)                    ← 构造期（含 WS_MINIMIZEBOX）
    ↓
SetChromeMode(Borderless)                               ← 配置期：派发 SWP_FRAMECHANGED ⇒ 非客户区归零
    ↓
SetWindowLayer(Desktop)                                 ← 配置期：
    ├─ ApplyDesktopStyle(true)  移除 WS_MINIMIZEBOX       （几何中性，**不派发 FRAMECHANGED**）
    ├─ SyncDesktopHook()        SetWinEventHook + 登记映射
    └─ SetWindowPos(<桌面之上>)  立即派发一次
    ↓
Show()                                                  ⇒ m_shown = true
    ↓
（运行期）EVENT_SYSTEM_FOREGROUND = 桌面类窗口
    ↓ 回调（注册线程内执行）
    DesktopForegroundProc → HookOwners 反查 → OnForegroundChanged
    ↓
    ReinsertAboveDesktop → IsDirectlyAboveDesktop? 位在 ⇒ 返回；未位 ⇒ SetWindowPos
```

### 3.3 生命周期时序（冻结——C6 / C12）

```
SetWindowLayer(Desktop)   → 装        （SyncDesktopHook，want == true）
SetWindowLayer(Normal/Bottom) → 卸    （SyncDesktopHook，want == false）
Hide()                    → 不动手    （不触碰 m_windowLayer / m_desktopHook）
Show()                    → 不动手    （同上；钩子从未卸除 ⇒ 不存在「重装」路径）
Release()                 → SyncDesktopHookOff() **先于** hwnd 判空   ← ★ 本阶段修正
~Win32PlatformWindow()    → Release() → SyncDesktopHookOff() 幂等返回（不重复通知观测缝）
```

**注销顺序（不可交换）**：`HookOwners().erase(hook)` → `UnhookWinEvent(hook)` → `m_desktopHook = nullptr`。
反序（先 `UnhookWinEvent`）会让「回调已取出 owner、而对象正在析构」成为可能 ⇒ UAF。

### 3.4 `Bottom` 档成本不变（冻结）

| 调用 | `Bottom` 档是否发生 | 依据 |
|---|---|---|
| `FindDesktopWindow()`（= `GetShellWindow()`） | **否** | `TargetInsertAfter` 的三元表达式只在 `IsDesktopLayer()` 为真时求值 |
| `GetWindow(desktop, GW_HWNDPREV)` | **否** | `ResolveTarget` 在 `layer == Bottom` 分支提前返回 |
| `SetWinEventHook` / `UnhookWinEvent` | **否** | `SyncDesktopHook` 的 `want` 为假 ⇒ 不装；`m_desktopHook` 恒 `nullptr` ⇒ 不卸 |

### 3.5 `Desktop` 档运行期开销（冻结——D4「不加 Timer」的依据）

| 触发 | 次数 | 成本 |
|---|---|---|
| 前台变为**非**桌面类窗口 | 每次前台切换 | `GetClassNameW` ×1（32 宽字符缓冲，栈上） |
| 前台变为**桌面类**窗口 | 每次（实测 Win+D 每轮 1 次） | `GetShellWindow` + `IsWindow`×2 + `GetWindow` + （未在位时）`SetWindowPos` |

实测：不装 tick 时 `FindWindowW` 调用 **0.8 次/秒**（需求稿 §8 P3-4）；装 tick 时升到 **6.06 次/秒**且**无收益** ⇒ D4 取「不加 Timer」。

---

## 4. 契约表（C1–C12 ⭢ 实现落点 ⭢ 测试）

| # | 契约 | 实现落点 | 验证 |
|---|---|---|---|
| **C1** | `Desktop` 档目标 = `GetWindow(桌面窗口, GW_HWNDPREV)` | `ResolveTarget` 末行（§2.4.6） | T16-7 ⑤ / 手测 A5 |
| **C2** | ★ 桌面不可用或已处最顶 ⇒ **不修改**；**任何路径下都不得退化为 `HWND_BOTTOM`** | `ResolveTarget` 的 `nullptr` 分支 + `WM_WINDOWPOSCHANGING` 的 `target != nullptr` 判空（§2.4.4） | **T16-7 ③④** + A4 的 `HWND_BOTTOM` 出现点审查 |
| **C3** | `Normal` 档**不修改** z 序 | `ResolveTarget` 首分支 | **T16-5** |
| **C4** | `Bottom` 档语义**逐位不变** | `ResolveTarget` 的 `HWND_BOTTOM` 分支 + 等价性核对表（§2.4.4） | **T16-1** |
| **C5** | `Desktop` 移除 `WS_MINIMIZEBOX`；离档**补回**（幂等 + 可逆） | `ApplyDesktopStyle` + `SetWindowLayer` 调用点（§2.4.5） | **T16-2 / T16-3** |
| **C6** | 钩子生命周期四态 + **注销顺序 = 先摘映射、后 `UnhookWinEvent`** | `SyncDesktopHook` / `SyncDesktopHookOff`（§2.4.6） | **T16-4** |
| **C7** | 桌面句柄**不缓存** | `FindDesktopWindow` 为无状态 `static`（无成员缓存字段） | 代码审查（`grep` 无 `m_desktopHwnd` 一类成员） |
| **C8** | 回调在**注册线程**执行 ⇒ 进程内**无锁** | `WINEVENT_OUTOFCONTEXT` + 注释（§2.4.2 / §2.4.6） | 代码审查（MSDN 语义） |
| **C9** | `m_windowLayer` **不随实现路径降级** | `SetWindowLayer` 的 `m_windowLayer = layer;`（§2.4.5） | 现有 + 代码审查 |
| **C10** | `SetWindowLayer` 仍为**配置期 API** | `SetWindowLayer` 首段守卫（§2.4.5） | **T16-6**（补配置期反例） |
| **C11** | ★ `IsDirectlyAboveDesktop()` 不可判返回 `true` 的语义 = **禁止无依据的 z-order 操作** | 函数首行 + 内联注释（§2.4.6） | 代码审查 |
| **C12** | ★ **钩子与 `HWND` 相互独立**——判空不得覆盖脱钩 | `Release()` 顶部（§2.4.3） | **T16-4**（`Release` ⇒ `off ×1` 且不重复） |

---

## 5. 测试规格（`ecdi_tests`——T16-1..T16-7）

| # | 用例 | 断言要点 | 层级 |
|---|---|---|---|
| **T16-1** | `DesktopLayer.BottomLayerRegression` | ★ **补 Phase 12 的空缺**（初设 §1.3：`WindowChromeTests` 9 用例零 `WindowLayer` 覆盖）——`Bottom` 档窗口被维护到参照窗口**之下** | 自动（真窗口 + 泵消息） |
| **T16-2** | `DesktopLayer.StyleStripsMinimizeBox` | `WS_MINIMIZEBOX` 清零；其余位**逐位保持**（含 4 个具名位独立断言） | 自动 |
| **T16-3** | `DesktopLayer.StyleRoundTripAndIdempotent` | 往返后 `style == original`（逐位复原）；同档位重复设置幂等 | 自动 |
| **T16-4** | `DesktopLayer.HookLifecycle` | **精确计数**：`on`/`off` 在五个状态下逐次断言（含 Hide 不卸、Show 不重装、Release 必卸且不重复） | 自动（经观测缝） |
| **T16-5** | `DesktopLayer.NormalLayerNotMaintained` | 含**正对照**（先证 Bottom 档维护确实生效）⇒ 再断言 `Normal` 不受维护 | 自动（真窗口 z 序） |
| **T16-6** | `DesktopLayer.ConfigTimeContract` | `Show()` 后 `SetWindowLayer` 被拒：**两个独立观测代理**（样式位 + 钩子计数）均不变 | 自动 |
| **T16-7** | `DesktopLayer.ResolveTargetTruthTable` | 五条真值表（§3.1 逐行）+ 句柄失效正对照（§2.5.6 的顺序纪律） | 自动（**纯函数——不需要窗口 / explorer**） |

**用例数**：210 → **+7 = 217**。

### 5.1 自动化边界（与需求稿 §6 三层分层的对应）

| 契约 | 自动化 | 说明 |
|---|---|---|
| **C1**（真实 z 序 = 紧贴桌面之上） | **真机手测** | 依赖 explorer 的 `Progman` ⇒ 本阶段**唯一**不可自动项 |
| **C2** | **自动**（T16-7 纯函数） | O3 = C 的兑现 |
| **C3–C12** | **自动** | — |

> 📌 **测试纪律（本阶段新沉淀）**：凡「X 档不受影响 / X 不参与」这类**否定型断言**，必须同用例内配一条
> **正对照**（证「探针/机制确实在工作」），否则「不变」与「机制根本没跑」不可区分。T16-5 的正对照即此
> （先证 `Bottom` 被压下去，再断言 `Normal` 没有）——这与 skill 条 75（先证「现象的前提条件真的发生了」）同源。

---

## 6. 验收

| # | 项 | 判据 |
|---|---|---|
| **A1** | 四工具链构建 + 测试 | MSVC / ClangCL / Clang / MinGW 全绿；`ecdi_tests` 210 → **217**。★ 其中 **MinGW / Clang 侧须确认本阶段唯一新增的平台回调**（`CALLBACK` 签名 + `HWINEVENTHOOK` 类型 + 链接）**实际过一遍**——同型先例（`&Win32PlatformWindow::WindowProc`）**不可外推**，`WINEVENTPROC` 是另一个函数指针类型（§9.1 #7） |
| **A2** | **断言启用核验** | 按 skill 条 50 取二进制证据：**10 条条件串**（★ **本阶段零新增断言** ⇒ 特征集**不变**）——`grep -c` 判据同 Phase 15（clang `-D_DEBUG` / clangcl `-MDd` / mingw `-D_DEBUG` / visual-studio `-MDd`） |
| **A3** | **零回归** | 既有 210 用例**全部保持通过**——尤其 `WindowChromeTests` 9 条（`RuntimeApiRejectedBeforeShow` / `ChromeModeDecidedOnce` / `MaximizedClientWithinWorkArea`）与 `AnimationTests` / `ProgressBarTests` 的 `TestPlatformWindow`（3 个实现者，本阶段**无新增 pure virtual**） |
| **A4** | ★ **C2 不容降级**（**本阶段特有的结构性验收**） | ① T16-7 五条全过；② **`HWND_BOTTOM` 出现点审查**：`grep -n 'HWND_BOTTOM' ECDI/src/Platform/Win32/Win32PlatformWindow.cpp` ⇒ 只允许出现在「`ResolveTarget` 的 Bottom 分支」与注释中，**不得**出现在 `WM_WINDOWPOSCHANGING` 或任何 Desktop 分支 |
| **A5** | **手测**（需求稿 §6 L3 三层分层的手测层） | 六判据 + Win+D + explorer 重启 + 桌面图标可点：① Win+D 中仍可见（**允许一次瞬时遮挡**——见 L1 的判据措辞）② 被普通应用覆盖 ③ 桌面图标可点击 ④ 交互不降级 ⑤ explorer 重启后自愈 ⑥ 切回 `Normal` 后行为复原。载体：`ModelProbe --layer desktop` + `.workbuddy/spike/desktop_layer_probe.cpp` 的 `--auto` |
| **A6** | `ecdi_public_header_test` | **92 头**各自独立可编译（**无新增头** ⇒ 条数不变；该目标同时是编码/BOM 探针） |
| **A7** | **文档与索引同步** | 两份 README 的规模锚点（用例 210→217 · 头 92 不变）+ `desktopnest-roadmap.md` §5 **G-1 标 ✅**（验收后）+ 需求稿 K13 勘误（需求稿 **v1.4** 已回写，验收时复核） |
| **A8** | **交付提示** | `CMakeLists.txt` 零改动（`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库新 `.cpp`）；CLion 需 **Reload CMake Project**；VS 工程走 `cmake --preset vs2026`（skill 条 66） |

**A2 特征串（10 条，与 Phase 15 同集）**：`it != m_windows.end()` · `m_rootWidget != nullptr` · `current == &GetRootWidget()` · `child->m_parent == nullptr` · `index < m_children.size()` · `!child->Contains(this)` · `it != m_children.end()` · `spacing >= 0` · `stretch >= 0` · `step >= 0`。

> ⚠️ **搜索纪律**（skill 条 50）：窄串；条件串落在**库目标** obj（`CMakeFiles/ECDI.dir/**/*.obj`）+ exe；**不可**用源文件路径判据（会被调试信息污染）。

**A4 的 `HWND_BOTTOM` 出现点（本稿已核——实施后须复核一致）**：

| 位置 | 允许？ |
|---|---|
| `ResolveTarget` 的 `layer == WindowLayer::Bottom` 分支 | ✅ 唯一的生产用途 |
| `WM_WINDOWPOSCHANGING` 的注释（说明「不得降级」） | ✅ 注释 |
| `ReinsertAboveDesktop` / `SetWindowLayer` / 其它任何分支 | ❌ **出现即 A4 失败** |

---

## 7. 影响面与回归清单

| 项 | 实测 | 影响 |
|---|---|---|
| `PlatformWindow` 实现者 | **3**（`Win32PlatformWindow` + `AnimationTests` / `ProgressBarTests` 的 `TestPlatformWindow`） | ★ **本阶段不新增 pure virtual** ⇒ **3 个实现者零同步**（skill 条 33 的清单已核：两份替身只有 `SetWindowLayer(WindowLayer) override{}` 空实现） |
| `SetWindowLayer` 生产调用点 | **2**：`Window.cpp:202-204`（透传）+ `examples/ModelProbe/main.cpp:297`（唯一真实消费者，`--layer`） | 无新增调用点；ModelProbe 的过期注释须**用户授权**后改（§7.1） |
| `SetWindowLayer` 接口 / 实现声明点 | `PlatformWindow.h:96`（纯虚）· `Window.h:122` · `Win32PlatformWindow.h:54` | 签名**零变更** ⇒ 3 处声明均不动 |
| `WM_WINDOWPOSCHANGING` 处理点 | **1**（`Win32PlatformWindow.cpp:405`） | 改动唯一，无第二处需要同步 |
| `WM_DESTROY` 处理点 | 1（`:431-433`，置 `m_hwnd = nullptr`） | ★ **不改**——脱钩由 `Release()` 负责（§2.4.3）；若在此处也脱钩会造成**双路径**，违反「唯一执行点」 |
| 公共头 | 92 | **92（净增 0）** |
| 断言特征串 | 10 | **10（零新增）** |
| 构建 | `GLOB_RECURSE … CONFIGURE_DEPENDS` | CMake **零改动**；新测试 `.cpp` 自动入库，但 `RunAllTests.h` / `.cpp` 需**手工接线**（2 行） |
| 测试替身 / Demo | 无 | 除 §7.1 的 ModelProbe 注释外，`examples/` 零改动 |

### 7.1 需**单独授权**的一项（AI 不得自行修改 `main.cpp`）

| 处 | 现状（过期） | 应改为 |
|---|---|---|
| `examples/ModelProbe/main.cpp:247` | 行尾注释：`// spike 未通过 → 降级 Bottom + Warning`（**该 spike 已于 2026-09-15 结项**，本阶段即落地 ⇒ 该注释已过期） | 行尾注释改为：`// Desktop 档已落地（Phase 16）：紧贴桌面窗口正上方，Win+D 后仍可见`（**仅注释、同一行内、零逻辑改动**——`layer` 的赋值语句本身不动） |

> 需求稿 **R4** 已登记此项（`main.cpp` 的过期注释）。**AI 修改 `main.cpp` 前必须取得单独授权**（skill 条 2）——本稿只登记，不实施。

---

## 8. 已知局限与记账

| # | 局限 | 处置 |
|---|---|---|
| **L1** | **Win+D「收起」方向存在一次瞬时遮挡**——外壳先抬起桌面窗口（我们没有同步时机，实测 `wpc` 每轮只 +1 且那 1 次是我们自己派发的），随后前台事件才唤醒我们重插。应用侧不可消除（`WS_EX_TOPMOST` 违反「被应用覆盖」契约）。**量级**：仅「显示桌面」方向、每次一帧（反应 ≤10 ms） | **记账 + 判据措辞纪律**：手测判据须写「**位置不变 + 随后正常显示（允许一次瞬时遮挡）**」，**不得**写「无闪烁」——那是不可满足的判据 |
| **L2** | **不加 `WS_EX_TOOLWINDOW`**（O2 倾向 ⇒ 已定）⇒ 桌面档窗口仍占任务栏位 / 出现在 Alt+Tab | 记账（「常驻物不该占任务栏」成立但属**未立项需求**，等真实消费者驱动——YAGNI） |
| **L3** | `IsDirectlyAboveDesktop()` 的**不可判定分支**（C11）返回 `true` ⇒ 抑制动作 ⇒ 可能**错过一次**本该执行的重插 | 记账（下一次前台事件会补；且「不可判」只出现在窗口/桌面句柄失效的窗口期，此时重插本就无意义） |
| **L4** | 钩子**过滤**接受 `Progman` / `WorkerW`，而**目标**恒为 `GetShellWindow()`（= `Progman`）——若某 Windows 版本「显示桌面」抬升的是 `WorkerW`，重插锚点仍是 `Progman` | 记账（spike 与本阶段实测均未出现；A5 手测覆盖。**不对称是刻意的**：过滤宁可宽、锚点必须唯一） |
| **L5** | **无 `GetWindowLayer()`** ⇒ 测试只能经**样式位 + 钩子计数**两个代理观察档位 | 记账（需求稿 §5 非目标——仍无消费者。T16-6 已用两个独立代理交叉印证，见 §2.5.4） |
| **L6** | 钩子安装依赖**真实桌面会话**（`SetWinEventHook` 失败 ⇒ Desktop 档无维护） | 记账（T16-4 的 `on == 1` 即环境断言，失败会响亮暴露；非交互式会话下 `GetShellWindow()` 返回 `nullptr` ⇒ `ResolveTarget` 走跳过分支 ⇒ **安全退化**，不崩） |
| **L7** | `Hide()` **保留**钩子（D11 取 A）⇒ 窗口不可见期间前台事件仍会走到 `ReinsertAboveDesktop`（对隐藏窗口 `SetWindowPos` 等价空操作） | 记账（换来「零重装状态机」；冗余调用 = 每个桌面类前台事件 1 次，可忽略） |
| **L8** | **每窗口一个钩子**（D5）⇒ N 个 Desktop 窗口 = N 个钩子 + 每个前台事件 N 次回调 | 记账（YAGNI：无大规模多窗口消费者。若日后出现，方案是「应用级单钩子 + 窗口注册表」——**但那要求应用级平台接缝**，属另立范围） |
| **L9** | **`WM_WINDOWPOSCHANGING` 只改 `hwndInsertAfter`**——不阻止第三方 `SetWindowPos` 造成的**瞬时** z 序变化 | **既有契约**（Phase 12 `WindowLayer.h:16-21` 已写），本阶段沿用；手测判据 ⑤（explorer 重启自愈）覆盖「事后恢复」而非「事前阻止」 |

---

## 9. 实施顺序（三批——**按依赖切分，每批可独立编译**）

| 批 | 内容 | 交付物 | 验收 |
|---|---|---|---|
| **A** | `Win32PlatformWindow.h`（§2.3 全部落点）+ `.cpp` 的 **include**（§2.4.1）、**匿名 namespace**（§2.4.2）、**九个新方法体**（§2.4.6） | 纯新增 —— **零行为变化**（新方法尚未被任何路径调用） | 编译通过（四工具链任一）；`ecdi_tests` 仍 **210 全绿**（★ 这一步是「新增不接线」的零风险验证） |
| **B** | `.cpp` 的**三处既有代码改造**（§2.4.3 `Release` / §2.4.4 `WM_WINDOWPOSCHANGING` / §2.4.5 `SetWindowLayer`）+ **注释修正**（§2.1 `WindowLayer.h` / §2.2 `PlatformWindow.h`） | 行为切换点 | 编译通过；`ecdi_tests` 仍 **210 全绿**（`Bottom` / `Normal` 逐位等价的实证）；**A4 的 `HWND_BOTTOM` 出现点审查** |
| **C** | 新建 `DesktopLayerTests.cpp`（§2.5）+ `RunAllTests.h` / `.cpp` 接线（§2.6） | +7 用例 | `ecdi_tests` **217 全绿**（四工具链）；注册名去重扫描 |

**为什么 A 与 B 分开**：批 A 是**纯新增**——若它就能编译通过，则证明「新增的方法体自身无误」；批 B 再切换行为。两者混做时，任何编译错误都无法区分来自「新代码写错」还是「改动点写错」（Phase 15 的三批切分同款理由）。

**批 B 之后的必查项**（因涉及既有分支）：

1. `git diff -U0` 逐行核对三处改造，确认**只**改了规格里列出的行；
2. `Bottom` 档**逐位等价**的证据：`ecdi_tests` 的 `WindowChromeTests` 9 条 + T16-1（批 C 后）；
3. `grep -n 'HWND_BOTTOM'` —— A4 判据。

### 9.1 实现期盯防清单（外部评审给定 7 条——**非设计阻塞项，而是照文档落地时不许走样的点**）

> 评审结论：**设计上通过，可以进入实现**；「没有发现需要推翻架构或重新做初设级别修改的问题」。下列 7 条是评审给出的
> **实现级检查项**——**照本稿落地即可，不得在实现期「顺手优化」其中任何一条**。

| # | 盯防点 | 为什么 | 本稿落点 |
|---|---|---|---|
| 1 | `SyncDesktopHookOff()` 的**注销顺序**不可变（先 `erase`、后 `UnhookWinEvent`） | 反序会让「回调已取出 owner、而对象正在析构」成为可能 ⇒ UAF。评审原话：「别为了看起来更符合 Unhook 再清理的直觉把它反过来」 | §2.4.6 · §3.3 · **C6** |
| 2 | `WM_WINDOWPOSCHANGING` **不得**重新出现 Desktop → `HWND_BOTTOM` 的隐式 fallback | 这是本阶段唯一会**静默降级**的错误。评审称 A4 的 `grep` 判据「**比普通单元测试更强**」——它检查的是「实现结构有没有重新偷偷造出第二条路径」 | §2.4.4 · **A4** |
| 3 | `IsDirectlyAboveDesktop()` 的「**不可判定 = `true`**」语义不得被简化 | 评审原话：「这两个函数组合起来的三态语义**很容易在实现时被『优化』坏**」。⚠️ 附加禁令：**不得**把 `ReinsertAboveDesktop()` 里的 `!IsDesktopLayer() \|\| m_hwnd == nullptr \|\| !IsWindow(m_hwnd)` 三重守卫「合并」进 `IsDirectlyAboveDesktop()`——两者**前提不同**（前者问「我该不该管」、后者问「我在不在位」） | §2.4.6 · **C11** |
| 4 | `WS_MINIMIZEBOX` 的改写**不得**顺手补 `SWP_FRAMECHANGED` | O1 已实测关闭（该位**几何中性**）；补上即与 §3.2 的调用序列不符，且多一次非客户区重算 | §3.2 · §2.4.6 · **O1** |
| 5 | `Bottom` 路径**不得**意外增加 Desktop 专属开销（`GetShellWindow` / 钩子） | 既有档位的零回归是**两个维度**：行为**与成本**。评审原话：「Bottom 路径不要意外增加 `GetShellWindow()` / Hook 等 Desktop 专属开销」 | §3.4 · §2.4.6 |
| 6 | T16-7 的 **dead HWND 创建顺序**（② 必须在 ⑤ 之前） | 句柄值可能被系统回收 ⇒ 顺序反了会让「失效句柄」这个前提**不成立**（假阴性），测试变成「通过但没有意义」 | §2.5.6 |
| 7 | 四工具链——**尤其 MinGW / Clang**——的 `CALLBACK` / `HWINEVENTHOOK` 声明与链接**实际过一遍** | 本阶段唯一新增的平台回调。既有同型先例 `&Win32PlatformWindow::WindowProc`（`Win32WindowClass.cpp:28`）虽已四链实证，但 `WINEVENTPROC` 是**另一个**函数指针类型 | §2.3 落点③ · **O5** · **A1** |

> 📌 评审另有一条**不计入实现检查项**的裁定（已采纳为本稿纪律）：**§8 L1 保持不变**——**不得**把验收措辞包装成「绝对无闪烁」。
> 那是「框架响应时序 vs Windows Shell 自身动作」的边界，写成「无闪烁」等于给自己立一个**无法兑现的契约**。


---

## 10. 修订记录

- v1.5（2026-09-19）**T16-6 假失败修复（用户首轮验证唯一失败项，216/217）**。**根因不在框架，在判据**：基线 `styled` 记录于 `Show()` **之前**，而 `ShowWindow` 会把 **`WS_VISIBLE` 写进 `GWL_STYLE`** ⇒ 整值比较必然差一个位。**失败模式自证了责任方**：两个钩子计数断言全过 ⇒ `SetWindowLayer` 主体确实被拒（若误执行，`ApplyDesktopStyle(false)` 会补回 `WS_MINIMIZEBOX` 且 `SyncDesktopHook` 会卸钩）——被改的只有系统动态位。**修复**：① 基线移到 `Show() + PumpMessages` 之后；② 断言改掩码版（屏蔽 `WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE`——系统自管位，若被拒路径错跑掩码照样能抓到）。§2.5.4 T16-6 规格同步；教训沉淀 skill 条 83（整值断言跨系统状态变更点必须先枚举动态位）。

- v1.4（2026-09-19）**批 B / 批 C 实施后回写（三批全部落盘，待验收 A1–A8）**。① **§2.5.4 伪代码更正**：T16-1 规格里的 `ref.Handle` / `bot.Handle` **不存在**——实际 API 是 `GetHwndForTests()`（`DropFilesTests.cpp:205` 先例）；测试按真实 API 落地，规格已同步。② **§2.5 规模实测**：新测试文件 **435 行 / 47 条 `EXPECT_*`**（估算偏低 +74%）；`ecdi_tests` 用例 **210 → 217**（22 个含用例文件 / 217 注册名 / **零重复** / `DesktopLayer.` 前缀全库唯一——脚本扫描确认）。③ **批 B 落点**（`829bb6d`，+49/−24）：三处既有代码改造（`Release` 脱钩前置 / `WM_WINDOWPOSCHANGING` 按档位分流 / `SetWindowLayer` 样式+钩子+分流派发）+ 两处公共头注释——`git diff -U0` 删除 24 行全部为预期旧块；**A4 判据首次生效**：`HWND_BOTTOM` 代码行仅剩 `ResolveTarget` 分支 1 处（另 4 处为注释）。④ **批 C 落点**：新建 `DesktopLayerTests.cpp`（T16-1..T16-7；T16-4 两窗口**连续计数**口径同 §2.5.5；T16-6 用 `onBefore`/`offBefore` 快照）+ `RunAllTests.h`/`.cpp` 各 +1 行接线；新文件行尾**显式转 CRLF**（Write 默认 LF，26/27 既有文件为 CRLF——条 8⑤）。⑤ **双工具链静态自查**：批 B `.cpp` 与批 C 测试文件各自 g++ / clang++ `-fsyntax-only -D_DEBUG` 均 **0 error / 0 warning**；四工具链构建与测试运行由用户执行（A1/A3）。

- v1.3（2026-09-19）**批 A 实施后回写：修正 §1.1 的方法计数（+9 → +12）**，并记录批 A 的两条实证。① **计数更正**——批 A 落盘后按「实测对账」逐个数（不凭记忆）：新方法共 **12**（类外定义 **10**：`ResolveTarget` / `TargetInsertAfter` / `FindDesktopWindow` / `IsDirectlyAboveDesktop` / `ReinsertAboveDesktop` / `ApplyDesktopStyle` / `SyncDesktopHook` / `SyncDesktopHookOff` / `OnForegroundChanged` / `DesktopForegroundProc`；**内联 2**：观测缝 setter `SetDesktopHookObserverForTests` / 判据 `IsDesktopLayer`）+ 类型别名 1（`HookObserverFn`）+ 成员 2（`m_desktopHook` / `m_hookObserver`）。原写法「+9 方法 +1 缝 setter」同时犯了两个方向的错：**漏计**内联判据、**重复计** setter。⚠️ 与初设 v1.1 的「+10 → +12」是**同一处易错点**（这已是第二次）——计数类断言必须落盘后实测，不能推算。② **批 A 严格纯新增已实证**：`git diff -U0` 对被改文件统计 **删除行数 = 0**（这正是批 A 的存在意义：「新增不接线」的零风险验证）。③ **两条工具链静态自查通过**：对改动后的 `Win32PlatformWindow.cpp` 跑 `-std=c++20 -Wall -Wextra -fsyntax-only -DUNICODE -D_UNICODE -D_DEBUG -I ECDI/include -I ECDI/src` ⇒ **MinGW g++ 0 error / 0 warning** · **clang++ 0 error / 0 warning**（`-D_DEBUG` 使断言路径同时参与类型检查）。（四工具链正式构建仍归用户 A1。）

- v1.2（2026-09-19）**实施前核对补正（D-6）：`wcscmp` 的包含来源**。① **§1.3 新增 D-6**（本稿对初设的**第六**处修正）——原 §2.4.1 写「真正的新增只有标准库**一行**」，但 `wcscmp` 归 `<cwchar>`、**不属于** `<cstring>`，原稿**未列它**，等于把 TU 的自洽性押在 `<Windows.h>` 的传递包含上。② **实测两工具链**（MinGW g++ `-c` exit 0 零警告 · clang++ `-fsyntax-only` 0 error 0 warning）确认**机制上可用**，但**仍显式补入 `#include <cwchar>`**——理由：该块 4 个既有标准库 include **逐一核对均被使用**（口径 = 「列你所用的」），且本文件 `:15-17` 的 `DrawText` 防护注释**本身就记着「头展开链不稳定」**。③ **§2.4.1 整节改写**（既有 4 行的上下文对照 + 两行新增标记 + 字典序说明 + 为什么必须显式加的证据表 + 裸调用与「不用 `lstrcmpW`」的理由）。④ **§2.4.2 的 `IsDesktopClassWindow`** 补两条内联注释（序数比较 + 不用 `lstrcmpW`）。⑤ **修正 §2.4.6 注意① 的一处笔误**——原文「`wcscmp` 需要 `<cstring>`/`<string.h>`……本文件已有 `<cstring>` ✅」**不成立**。⑥ **§1.1 改动清单**第 4 行「+1 标准库 include」→「**+2**」。**规模锚点不变**（92 → 92 · 210 → 217 · 10 → 10）；八条实现级检查项（§9.1）与契约 C1–C12 均不受影响。

- v1.1（2026-09-19）**外部详细设计评审处置——「设计上通过，可以进入实现」（无阻塞项）**：① **状态行**由「待评审」改为「**评审通过 · 可进实现**」（评审原话：「**设计上通过，可以进入实现**」「没有发现需要推翻架构或重新做初设级别修改的问题」「已经具备**直接交给实现阶段**的条件」，并肯定本稿「更强调危险路径的结构性约束」的取向）；② **新增 §9.1 实现期盯防清单**——把评审给出的 **7 条实现级检查项**落成表（注销顺序不可变 / 不得重现 Desktop→`HWND_BOTTOM` 隐式 fallback / 三态语义不得简化 **且三重守卫不得合并** / 不得补 `SWP_FRAMECHANGED` / `Bottom` 路径零新增开销 / T16-7 的 dead HWND 顺序 / 四工具链回调类型实测过一遍），逐条绑定本稿落点；③ **A1 补强**——显式要求 MinGW / Clang 侧确认 `CALLBACK` 与 `HWINEVENTHOOK` 的声明与链接，并写明「同型先例不可外推」；④ 评审对 §8 L1（瞬时遮挡不得包装成「无闪烁」）的裁定**照原样保留**。评审未提出任何需要返回初设的问题 ⇒ 本稿 v1.1 即**实现依据**。
- v1.0（2026-09-19）**详细设计初稿**（初设 v1.3「评审通过 · O1 已关闭」后启动）。① **§1 实施总览**——**7 个文件**（4 改 + 1 新建 + 2 接线），Public 头 **92 → 92**、用例 **210 → 217**、断言特征串 **10 → 10**；② **§1.3 本稿对初设的五处修正**——**D-1** `PlatformWindow.h` 的 `@details`（原文「Bottom/Desktop 档持续维护普通窗口层底部位置」对 Desktop 已失实）⇒ Public 头改动由 1 处变 **2 处（仍均仅注释）**；**D-2** T16-5 的方法由「伪造 `WINDOWPOS` 直发」改为「真实 `SetWindowPos` + z 序邻居观察」（不把契约建立在未证实的 `DefWindowProc` 行为上）；**D-3** 测试落点定为**新建 `DesktopLayerTests.cpp`**（样本取「直接构造 `Win32PlatformWindow`」——`DropFilesTests.cpp:133` 先例，免去三层 `static_cast`）；**D-4** 新成员落点改为 **private 成员区末尾新增 Phase 16 分组**（不在 `m_windowLayer` 之后割裂 Phase 12 分组）；**D-5** **零新增 `FRAMEWORK_ASSERT`**（每个前置条件均由显式守卫闭合，断言只会成死代码）⇒ A2 特征集不变；③ **§2 逐文件最小 diff**——含 `WM_WINDOWPOSCHANGING` 的**等价性核对表**（`Normal` / `Bottom` **逐位等价**）、`SetWindowLayer` 的**三处差异表**、九个新方法体全文；④ **§3 关键行为冻结**——`ResolveTarget` 真值表 · Borderless+Desktop 调用序列 · 生命周期时序（含**注销顺序不可交换**的理由）· `Bottom` 档成本表 · Desktop 档运行期开销；⑤ **§4 契约表**——C1–C12 逐条给出实现落点与验证方式；⑥ **§5 测试规格**——T16-1..T16-7（含 T16-4 的**两窗口分工**与 T16-7 的**顺序纪律**）+ **§5.1 否定型断言必须配正对照**的新纪律；⑦ **§6 验收 A1–A8**——**A4「C2 不容降级」**给出 `HWND_BOTTOM` 出现点的可机检判据；⑧ **§7 影响面**——`PlatformWindow` 3 个实现者**零同步**、`WM_WINDOWPOSCHANGING` 处理点唯一、`WM_DESTROY` **刻意不改**（避免双路径脱钩）；⑨ **§8 已知局限 L1–L9**（含 L1 的**判据措辞纪律**：不得写「无闪烁」）；⑩ **§9 三批实施顺序**（A 纯新增 / B 行为切换 / C 测试）。待评审。
