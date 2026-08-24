# Phase 7.2 测试体系补强 详细设计

> 状态：v0.2（2026-08-24）｜详细设计待审（GPT 评审整合）
> 前序：职责确认 v1.1 ✅ / 初步设计 v0.4 ✅（`phase7.2-test-system-requirements.md` / `phase7.2-test-system-preliminary.md`）
> 依据：调研实态（TextBox Selection 码点契约 / WindowMessageHandler 翻译形态 / PlatformWindowHost 接口 / vcxproj 组织）——本稿全部设计基于实际代码事实

## 1. 设计契约总览

### 1.1 组件拥有关系（初步设计 §3.7 约束落实）

```
RunAllTests（orchestration：Register all → Run → Report，无业务逻辑）
   │
   ├─ RegisterWidgetTests / RegisterLayoutTests / RegisterTextBoxTests
   │  └─ RegisterRendererTests / RegisterEventTests / RegisterTestFrameworkTests
   │        └─ GetTestRegistry().Add(name, fn)          [注册]
   │
   ├─ TestRunner runner; runner.Run(registry)           [执行]
   │     └─ 每个 TestCase：TestResult + TestContext → TestFunction → EXPECT_*
   │
   └─ PrintSummary(results) + MessageBox 提醒           [报告]
```

### 1.2 全局状态策略（§3.7 约束的关键权衡）

- **唯一允许的全局**：当前 TestContext 指针（EXPECT 宏机制需要"当前上下文可达"——GTest/Catch2 同款内部机制）。
- **收敛方式**：`Detail::CurrentContext()` 为**函数局部静态指针**（无静态初始化顺序问题），由 Runner 在测试前后设置/恢复；**所有状态数据（failures）仍在 TestResult 中**（Runner 持有），全局只存"指向哪"，不存"什么值"。
- 其余全部经显式参数传递：Registry 经 `GetTestRegistry()`（函数局部静态，append-only 无状态风险）或 runner 参数；无 `g_failures / g_runner` 等散落全局。

## 2. TestFramework 详细设计（新文件 `ECDI/src/Tests/TestFramework.h/.cpp`）

### 2.1 数据结构

```cpp
namespace ECDI::Test {

/// @brief 测试函数类型（无参无返回值；断言失败经上下文上报，不抛异常）
using TestFunction = void (*)();

/// @brief 测试用例（注册单元——name + function 成对，非平行数组）
struct TestCase {
    const char* name;        ///< 带模块前缀："Widget.PanelPaint"
    TestFunction function;
};

/// @brief 单条断言失败记录（无 pretty-print——只记表达式文本 + 位置）
struct FailureRecord {
    const char* expression;  ///< 失败表达式文本（#lhs " == " #rhs 等），或 "unhandled exception"
    const char* file;        ///< __FILE__；异常记录时置空（非异常发生处）
    int line;                ///< __LINE__；异常记录时为 0
    const char* function;    ///< __func__ 或测试名（异常时）
};

/// @brief 单个测试用例的结果（统计单位 = TestCase，非断言）
struct TestResult {
    const char* name = nullptr;   ///< 测试名
    bool passed = true;           ///< 全部断言通过且无异常
    std::vector<FailureRecord> failures;   ///< 多断言失败 → 多记录，仍是一个 FAIL TestCase
};
```

### 2.2 TestRegistry

```cpp
class TestRegistry {
public:
    void Add(const char* name, TestFunction function);   ///< 显式注册（决策 C）
    const std::vector<TestCase>& GetCases() const noexcept;
    void Clear();   ///< 供自测用（防跨测试污染）
private:
    std::vector<TestCase> m_cases;
};

TestRegistry& GetTestRegistry();   ///< 函数局部静态（magic static，append-only）
```

### 2.3 TestContext 与当前上下文

```cpp
class TestContext {
public:
    explicit TestContext(TestResult& result);
    void RecordFailure(const char* expression, const char* file, int line, const char* function);
    TestResult& GetResult() noexcept;
private:
    TestResult& m_result;
};

namespace Detail {
    /// @brief 唯一全局：当前 TestContext 指针（函数局部静态——无静态初始化顺序问题）
    /// @details Runner 设置/恢复；断言宏经此上报。测试代码不应直接触碰。
    TestContext*& CurrentContext();
    /// @brief 断言失败上报入口（EXPECT_* 宏调用；无上下文时 Debug 输出警告——防御）
    void ReportFailure(const char* expression, const char* file, int line, const char* function);
}
```

### 2.4 TestAssert（EXPECT 宏——`TestFramework.h` 内定义）

第一版仅 5 个宏（初步设计决策 D2 定案，无模板魔法）：

```cpp
#define EXPECT_TRUE(condition) \
    do { if (!(condition)) ECDI::Test::Detail::ReportFailure(#condition, __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_FALSE(condition) \
    do { if ((condition)) ECDI::Test::Detail::ReportFailure(#condition " (expected false)", __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_EQ(lhs, rhs) \
    do { if (!((lhs) == (rhs))) ECDI::Test::Detail::ReportFailure(#lhs " == " #rhs, __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_NE(lhs, rhs) \
    do { if (!((lhs) != (rhs))) ECDI::Test::Detail::ReportFailure(#lhs " != " #rhs, __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_NEAR(lhs, rhs, eps) \
    do { \
        const double _v1 = static_cast<double>(lhs); \
        const double _v2 = static_cast<double>(rhs); \
        const double _e  = static_cast<double>(eps); \
        if (std::abs(_v1 - _v2) > _e) \
            ECDI::Test::Detail::ReportFailure(#lhs " ≈ " #rhs, __FILE__, __LINE__, __func__); \
    } while (false)
```

- 失败行为：**记录 + 继续**（不弹框、不终止）；位置信息自动捕获
- 浮点迁移：现有 `FloatEq(a, b)`（kEpsilon=0.001）→ `EXPECT_NEAR(a, b, kEpsilon)`
- **实现语义（GPT 修正）**：`EXPECT_NEAR` 用 `static_cast<double>` 收窄到 double 再 `std::abs`——避免无符号类型 `a - b` 下溢出错误（如 `unsigned(1) - unsigned(2)`）；首版**仅用于浮点场景**（`TestFramework.h` 需 `#include <cmath>`）
- 双轨语义（职责确认 G）：`FRAMEWORK_ASSERT` = 框架运行时不变量（终止）；`EXPECT_*` = 测试期望值（记录继续）

### 2.5 TestRunner

```cpp
class TestRunner {
public:
    void Run(const TestRegistry& registry);
    const std::vector<TestResult>& GetResults() const noexcept;
    int GetPassedCount() const noexcept;
    int GetFailedCount() const noexcept;
private:
    void RunOne(const TestCase& testCase);
    std::vector<TestResult> m_results;
};
```

`RunOne` 核心流程（落实初步设计 §3.3/§3.7）：

```cpp
void TestRunner::RunOne(const TestCase& testCase) {
    TestResult result;
    result.name = testCase.name;
    TestContext context(result);

    auto& slot = Detail::CurrentContext();
    TestContext* previous = slot;
    slot = &context;
    try {
        testCase.function();                       // 断言失败 → 记录，不抛
    } catch (...) {
        result.passed = false;                     // 标准 C++ 异常 → FAIL + 继续
        // file/line 置空（非异常发生处，避免冒充 TestRunner 位置）；Summary 特判输出
        result.failures.push_back(FailureRecord{ "unhandled exception", nullptr, 0, testCase.name });
    }
    slot = previous;                               // 恢复（防御性）
    if (!result.failures.empty()) result.passed = false;
    m_results.push_back(std::move(result));
}
```

- **异常边界**：只捕获标准 C++ 异常；不承诺 SEH/access violation/栈溢出（初步设计 v0.4 措辞）
- 单测试失败不影响后续（循环继续）

### 2.6 Summary（`TestFramework.cpp`）

```cpp
void PrintSummary(const std::vector<TestResult>& results);
// 输出（Logger::Log(Info) → OutputDebugStringW）：
//   [PASS] Widget.PanelPaint
//   [FAIL] Widget.WidgetTree
//       WidgetTests.cpp:123  EXPECT_EQ(commands.size(), 1)
//       WidgetTests.cpp:130  EXPECT_TRUE(widget->IsVisible())
//       unhandled exception                   （file==nullptr 时特判，不显示文件/行）
//   ─────────────────────────
//   Tests: 20  Passed: 18  Failed: 2
// 有失败 → MessageBoxW 仅提醒："Tests failed: 2"（完整报告在 Debug Output）
```

## 3. 接入迁移（不重写——机械迁移）

### 3.1 断言迁移规则（4 模块测试 cpp）

| 现有写法 | 迁移为 | 说明 |
|---|---|---|
| `FRAMEWORK_ASSERT(x)`（期望值） | `EXPECT_TRUE(x)` | 机械替换 |
| `FRAMEWORK_ASSERT(a == b)` | `EXPECT_EQ(a, b)` | 需要 `operator==`（Rect/Color/Point 已有） |
| `FRAMEWORK_ASSERT(FloatEq(a, b))` | `EXPECT_NEAR(a, b, kEpsilon)` | 浮点统一 |
| `FRAMEWORK_ASSERT(a != b)` | `EXPECT_NE(a, b)` | |
| **"测框架断言路径"的测试** | **保留 FRAMEWORK_ASSERT** | 双轨依据（如 WidgetTests 越界断言注释） |

### 3.2 模块入口改名 + 注册

| 文件 | 改动 |
|---|---|
| `RunAllTests.h` | 声明 `RegisterXxxTests()`（6 个注册入口——其中 `TestFrameworkTests` 为基础设施自测，其余 5 个为业务测试）；移除 `RunXxxTests` 声明 |
| `WidgetTests.cpp` 等 4 模块 | 入口 `RunXxxTests` → `RegisterXxxTests`，内部改为 `GetTestRegistry().Add("模块.测试名", &TestXxx)` |
| 新 `EventTests.cpp` | 兑现 `RunEventTests` 挂起（P1，§5） |
| `RunAllTests.cpp` | 重构为 orchestration（§1.1 图） |

### 3.3 vcxproj

- `<ClCompile>` 新增：`src\Tests\TestFramework.cpp`、`src\Tests\EventTests.cpp`（4 配置自动继承——现有 5 个测试 cpp 同款 ItemGroup）
- `<ClInclude>` 新增：`include\ECDI\Test\TestFramework.h`（若放公共头）**或** `src\Tests\TestFramework.h`（放测试目录则无需 ClInclude——依 D1 定案：**src/Tests 内部，无需 ClInclude**）

## 4. P0：TextBox Selection 测试设计

### 4.1 索引单位契约（调研确认——先确认契约再写测试）

- **Selection/Caret 索引单位 = 码点（code point）**：`SelectionRange` 注释明确"码点索引，非字节偏移"；`GetCaret()` 返回码点索引；emoji 占 1 码点（代理对在 UTF-16 存储层，框架层不可见）
- 实现模型：`m_selectionAnchor`（固定端）+ `m_caret`（active 端）；`HasSelection() = anchor != caret`
- 建立路径：**仅事件驱动**（无 public SetSelection API）——`OnKeyDown`（Shift+方向/Home/End）、`OnMouseButtonDown/Move/Up`（拖选）

### 4.2 范围修正（调研发现——不是"补测试"而是"新功能"的排除项）

| 原覆盖要点 | 调研结论 | 处置 |
|---|---|---|
| 鼠标拖选 | `OnMouseButtonDown/Move` 硬解引用 `GetWindow()->GetTextMeasurer()`（T1 非 Paint 测量）——无 Window 崩溃 | **事件流程不可无窗口测**；定位算法可测（4.4） |
| 键盘选择（Shift+方向/Home/End） | `OnKeyDown` 纯码点逻辑 + `SyncTextInputCaret` 有 Window 防御 | ✅ **无窗口可测** |
| Ctrl+A 全选 | **TextBox 未实现**（属 Phase 8.5 剪贴板子系统 Ctrl+A/C/V/X） | ❌ 排除（8.5 功能落地时再测） |
| 双击选择 | **TextBox 未实现**（注释标注"6.x 未来"） | ❌ 排除 |
| 删除/插入选中区 | `InsertCodepoint`/`DeleteBackward`/`DeleteForward` 自包含"有 Selection 先删"（TextBox.cpp:54） | ✅ 无窗口可测（先键盘建 Selection 再编辑） |

### 4.3 测试子类（暴露 protected 路径）

```cpp
class TestableTextBox : public ECDI::TextBox {
public:
    using ECDI::TextBox::TextBox;
    using ECDI::TextBox::OnKeyDown;    ///< 暴露键盘选择路径（无窗口安全）
    /// <summary> 暴露拖选定位算法（参数注入 measurer——不依赖 Window）</summary>
    size_t CaretIndexFromXPublic(ECDI::TextMeasurer& m, float x) { return CaretIndexFromX(m, x); }
};
```

### 4.4 用例清单（P0）

| # | 用例 | 断言要点 | 路径 |
|---|---|---|---|
| S1 | Shift+→ 扩展选择 | 从 caret=2 起 Shift+Right×2 → Selection{2,4}；GetCaret()==4 | 键盘 |
| S2 | Shift+← 收缩/反向 | Shift+Left 到边界 → 选区反向（anchor 固定） | 键盘 |
| S3 | Shift+Home/End | 扩展到行首/行尾 | 键盘 |
| S4 | 无 Shift 移动清选择 | 有 Selection 后普通 → ClearSelection + 移动 | 键盘 |
| S5 | InsertCodepoint 删选中区 | Selection{1,3} 后插入 → 文本删 1..3 + 新字符；caret 落 min 处 | 键盘+编辑 |
| S6 | DeleteBackward/Forward 删选中区 | 同 S5，删除整个选区 | 键盘+编辑 |
| S7 | 跨代理对选择 | "a😀b" Shift+Right×2 → Selection{1,3}（emoji 整体，不切半个） | 键盘 |
| S8 | 选区-光标一致性 | 每次移动后 GetCaret 与 GetSelection 边界一致 | 键盘 |
| S9 | CaretIndexFromX 算法 | 注入 RecordingBackend（固定 10x14 测量）→ 各 x 偏移的码点索引（含末尾钳制） | 算法 |
| S10 | 空文本/单字符边界 | 空文本 Shift 移动不崩、选区合法 | 键盘 |

**明确不做**：鼠标拖选完整事件流程（依赖 Window——需最小窗口集成，超出 7.2 无窗口边界；定位算法已由 S9 覆盖，事件胶水留集成/目测）。

## 5. P1：Event + 7.1 契约回归测试设计

### 5.1 FakeHost（假 PlatformWindowHost——无窗口翻译测试的支点）

```cpp
class FakeHost : public ECDI::PlatformWindowHost {
public:
    std::vector<const ECDI::Event*> events;   ///< OnEvent 收到的（指针指向 Translator 局部 Event 对象）
    /// 生命周期约束：仅用于事件到达期间的即时断言；不得在事件生命周期结束后继续使用该指针
    void OnPaint() override {}
    void OnResized(int, int) override {}
    void OnExitSizeMove() override {}
    ECDI::Window* GetWindow() const noexcept override { return nullptr; }
    void OnEvent(const ECDI::Event& event) override { events.push_back(&event); }
    void OnIMEComposition() override {}
};
```

### 5.2 Translator 测试（WindowMessageHandler 形态已调研：static 翻译函数 + host 派发）

| # | 用例 | 输入 → 断言 |
|---|---|---|
| T1 | `TranslateKeyCode` 纯函数 | WPARAM/LPARAM 组合 → KeyCode 枚举（含左右 Shift/Ctrl/Alt 区分——lParam 位） |
| T2 | `TranslateMouseButton` 纯函数 | WM_LBUTTONDOWN/WM_RBUTTONDOWN + wParam → MouseButton |
| T3 | `ConsumeCodeUnit` 代理对状态机 | 高代理→低代理→完整码点；孤立低位丢弃（实例方法：FakeHost 构造 handler） |
| T4 | `Handle` 全流程 | 构造 msg/wParam/lParam → `FakeHost.events` 收到对应 Event（类型 + 字段原样）——**无窗口** |

### 5.3 Event 层次/Router 测试

- Event 值对象直接构造（`MouseButtonDownEvent(nullptr, x, y, button)` 等）→ 断言 `GetType()/StaticType()` 分派正确
- `EventRouter` 具名方法分派（输入事件层次：InputEvent←Mouse×5/Key×2/CharInput）

### 5.4 7.1 契约回归清单（5 链路定案——只测抽象后边界）

| 链路 | 定案 |
|---|---|
| Window message → Translator → Event | ✅ 无窗口测（T1-T4：FakeHost） |
| Translator → Application/EventRouter | ✅ 无窗口测（Event 级联） |
| Window::Host 回调边界 | ⚠️ `GetWindow()` 返回 nullptr 的 FakeHost 可测 OnEvent 契约；Host 真实实现（Window）留集成 |
| CaretGeometry → PlatformWindow | ⚠️ 几何纯计算部分可测（构造 TextBox+测量 → 断言 CaretGeometry 字段）；平台消费留集成 |
| IME composition → Framework | ⚠️ 仅测 `OnIMEComposition` 契约（FakeHost 记录）；Win32 IMM 状态同步留集成 |

### 5.5 现有视觉验证资产（历史记账——不框架化）

> Phase 8 渲染增强收尾阶段（2026-08-21）已完成**实际渲染视觉验证**（`AlphaDemoPanel`：半透明红叠蓝底，视觉确认叠置区呈紫色），用于确认新增渲染能力在真实 GDI 渲染路径上的最终视觉表现。
>
> 该验证属**真实渲染结果的人工确认**，不扩展为通用 UI 自动化测试，也不要求 Phase 7.2 将视觉验证框架化。

ECDI 测试/验证体系最终三层结构：

```
                  ECDI 测试/验证体系
                         │
        ┌────────────────┼────────────────┐
        ↓                ↓                ↓
   契约自动测试       真实后端测试       视觉验证
        │                │                │
 RecordingBackend     GDIBackend       实际渲染结果
 RendererTests        AlphaBlend       人工确认/视觉检查
 Widget/Layout/...    (像素断言)
        │
        └─────────── 无窗口优先
```

- **契约自动测试**：RecordingBackend/RendererTests/Widget/Layout/TextBox/Event（自动）
- **真实后端测试**：`TestGDIBackendAlphaBlend` 像素断言（自动，少量兜底）
- **视觉验证**：真实 GDI 渲染结果人工确认（非自动化，Phase 8 已做）

## 6. TestFramework 自测（§3.6 落地——`TestFrameworkTests`）

| # | 用例 | 验证点 |
|---|---|---|
| F1 | Registry 注册 | Add 后可取回（name/function 一致） |
| F2 | Runner 执行 | 注册 3 个 PASS 测试 → 全过、计数正确 |
| F3 | 失败不阻塞 | 注册 [FAIL, PASS, FAIL] → 3 个都执行（计数 1 过 2 败） |
| F4 | 多 failure records | 单测试内 3 个 EXPECT 失败 → 1 个 FAIL TestCase + 3 records |
| F5 | 异常标记 | 抛异常测试 → FAIL + Runner 继续 |

**定位**：基础设施回归测试（非 Runner 正确性证明）；不作为 Runner 硬前置（初步设计 v0.4）。

## 7. 文件改动清单（最终）

| 类别 | 文件 | 内容 |
|---|---|---|
| 新增 | `ECDI/src/Tests/TestFramework.h/.cpp` | Registry/Runner/Context/Result/Assert/Summary |
| 新增 | `ECDI/src/Tests/EventTests.cpp` | P1（§5）+ 自测（§6）可合并或分文件——**倾向：EventTests.cpp 含 P1；TestFrameworkTests 并入（同属新测试）** |
| 修改 | `RunAllTests.h/.cpp` | orchestration 重构（§3.2） |
| 修改 | 4 个模块测试 cpp | 断言迁移 + 入口改名注册 |
| 修改 | `ECDI/ECDI.vcxproj` | ClCompile 加 TestFramework.cpp / EventTests.cpp |
| 修改 | `ECDI/src/Tests/TextBoxTests.cpp` | 追加 P0 Selection 用例（§4.4） |
| 不动 | main.cpp / 框架运行时代码 / RecordingBackend / GDIBackend | 零侵入 |

## 8. 风险与验证

| 风险 | 缓解 |
|---|---|
| 当前上下文全局指针误用 | 仅 Detail 内部 + Runner 控制生命周期；测试代码不可触碰 |
| EXPECT_EQ 需 operator== | Rect/Color/Point 已有；复杂类型用 EXPECT_TRUE(a == b) 显式 |
| P0 鼠标拖选不可无窗口测 | 已拆：算法（S9）无窗口测 + 事件流程留集成（文档明示边界） |
| 迁移破坏现有测试 | 机械替换（规则表 §3.1）；迁移后现有断言语义不变（仅失败行为变） |

**验证方式**（skill 规则 1）：用户 VS 编译 + 运行；测试通过直接进 GUI。AI 侧静态自查（逐条核对用例期望值）。

## 9. 修订历史

| 版本 | 日期 | 内容 |
|---|---|---|
| v0.2 | 2026-08-24 | GPT 评审整合：EXPECT_NEAR 改 double+std::abs（修无符号减法下溢）；异常 FailureRecord 置空 file/line（不冒充 TestRunner 位置，Summary 特判）；新增 §5.5 现有视觉验证资产记账；FakeHost 事件指针加"仅即时断言"生命周期约束；6 注册入口注明含 TestFrameworkTests |
| v0.1 | 2026-08-23 | 初稿（基于实现事实调研：TextBox 码点契约 / WindowMessageHandler 形态 / FakeHost 方案 / P0 范围修正 Ctrl+A+双击=新功能排除） |
