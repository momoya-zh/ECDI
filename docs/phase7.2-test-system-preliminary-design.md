# Phase 7.2 测试体系补强 初步设计

> 状态：v0.4（2026-08-23）｜初步设计待审（GPT 评审整合；v0.3 审查通过，补视觉验证资产记账）
> 前序：职责确认 v1.1 ✅（`phase7.2-test-system-requirements.md`）
> 产出：本稿 + 后续详细设计

## 1. 设计目标与硬约束

| 目标 | 说明 |
|---|---|
| **最低侵入接入** | 现有 ~1000 行测试（4 模块）**接入不重写**——机械迁移 + 形态调整 |
| 轻量测试基础设施落地 | TestCase / TestRegistry / TestRunner / TestContext / TestResult / TestAssert / Summary（物理上单文件，逻辑分块——见 D1） |
| 硬约束 | 零第三方依赖；无窗口哲学；双轨断言；失败继续 + 汇总；单测失败不影响其他；严重异常 Runner 捕获 |

## 2. 现状基线（接入面）

实测确认的现有形态：

```cpp
// WidgetTests.cpp（4 模块同构）
namespace {
void TestPanelPaint() {
    ...
    FRAMEWORK_ASSERT(commands.size() == 1);      // 期望值断言
    ...
}
} // anonymous namespace

void ECDI::Test::RunWidgetTests() {
    TestPanelPaint();   // 入口 = 显式顺序调用
    ...
}
```

- 测试函数：`void TestXxx()` 无参、匿名 namespace、块内多断言
- 断言：`FRAMEWORK_ASSERT`（Debug 下失败 → 日志 + MessageBox + 终止进程）
- 入口：`RunXxxTests()` 显式顺序调用；`RunAllTests` 聚合 4 模块；main.cpp 调 `RunAllTests`
- 浮点场景普遍用 `FloatEq` 辅助（kEpsilon = 0.001）

**关键结论**：现有 FRAMEWORK_ASSERT 的"失败即终止"与"失败继续"目标冲突 → **测试内"期望值断言"必须迁移为测试断言**（机械替换，非重写）；"测框架断言路径"的少量测试保留 FRAMEWORK_ASSERT（双轨的依据）。

**已有测试/验证资产（三层结构，v0.4 记账——防转库时产生"从未做过视觉验证"的错觉）**：

```
契约测试（自动）        RecordingBackend / RendererTests      → 验证 RenderCommand / Backend 契约
真实后端测试（自动）     TestGDIBackendAlphaBlend（像素断言）    → 验证实际 GDI 渲染路径
视觉验证（人工/实际渲染） Phase 8 收尾实际渲染目测（8-21）       → 确认最终视觉表现
```

> Phase 8 收尾阶段已完成实际渲染视觉验证（AlphaDemoPanel：半透明红叠蓝目测混合正确），用于确认新增渲染能力在真实 GDI 渲染路径上的最终视觉结果。属人工/实际渲染结果确认，**不扩展为自动化 UI 测试体系**——"测试不是所有东西都必须自动化"：契约适合自动测试，实际后端适合少量真实测试，最终视觉表现保留视觉验证。

## 3. 组件设计（初步方案）

### 3.1 测试断言（Assert）——双轨落地

| 轨 | 用途 | 失败行为 |
|---|---|---|
| `FRAMEWORK_ASSERT` | 框架运行时不变量（框架 bug） | 日志 + 弹框 + 终止（现状不变） |
| **测试断言（新增）** | 测试期望值（test failure） | 记录失败（表达式/文件/行/函数）+ **继续** |

初步形态（倾向，签名级留详细设计）：
- **第一版断言范围（v0.2 明确）**：仅 `EXPECT_TRUE / EXPECT_FALSE / EXPECT_EQ / EXPECT_NE / EXPECT_NEAR`（NEAR 覆盖浮点，替代散落的 FloatEq）——**不做模板魔法**（不搞 EXPECT_VECTOR_EQ/EXPECT_RECT_EQ/EXPECT_COLOR_EQ，不做任意类型 pretty-print），保持"轻"
- **宏形态**（与 FRAMEWORK_ASSERT 风格一致：自动捕获 `__FILE__/__LINE__/__func__`）
- 失败计数挂在"当前测试上下文"（Runner 提供），断言只上报不弹框
- **两层责任分离（v0.2 明确）**：断言只保证"断言失败不终止测试"；**不保证测试代码自身的非法访问（如解引用 nullptr）不终止**——那是测试代码正确性问题，Runner 的 try-catch 只负责"测试函数抛异常 → 标记 FAIL 继续下一个"

### 3.2 注册（Registry）——决策 C 落地（显式优先）

**方案（v0.2：模块入口改名）**：模块入口**签名不变**（无参），**语义与命名对齐**——`RunXxxTests()` 改为 **`RegisterXxxTests()`**（它不再"执行"，只"注册"，命名必须匹配语义，避免阅读认知错位）：

```cpp
// TestCase 是真正的数据结构（非裸函数指针/平行数组）
using TestFunction = void (*)();
struct TestCase {
    const char* name;        ///< 带模块前缀："Widget.PanelPaint"
    TestFunction function;
};

// 模块入口：注册语义
void ECDI::Test::RegisterWidgetTests() {
    GetTestRegistry().Add(TestCase{ "Widget.PanelPaint", &TestPanelPaint });
    GetTestRegistry().Add(TestCase{ "Widget.LabelPaint", &TestLabelPaint });
    ...
}
```

- `RunAllTests` 演进为：**注册（调 4 个 RegisterXxxTests）→ Runner 统一执行 → 汇总**
- **main.cpp 零改动**（仍调 `RunAllTests`）
- 判断标准（职责确认 C）：显式优先；仅当测试数量增长后显式维护成本明显高于自动注册时才转自动注册

### 3.3 Runner 与 TestResult

- 遍历 Registry 逐个执行测试函数；**每个测试独立 try-catch**
- **异常捕获边界（v0.4 措辞严谨化）**：Runner 只捕获**标准 C++ 异常**（`throw std::runtime_error` 等）→ 标记 FAIL + 继续；**不承诺捕获访问违规（access violation）、栈溢出等 SEH/进程级异常**（Windows 下这些不是 C++ exception）——测试框架不是"进程崩溃隔离器"，非法访问是测试代码自身正确性问题
- **Test Case 状态模型（v0.2 明确）**：
  ```
  TestResult
  ├── name        // "Widget.PanelPaint"
  ├── passed      // true / false
  └── failures[]  // 断言失败记录（expression / file / line / message）
  ```
  - 多断言失败 → **仍是一个 FAIL Test Case**，但保留多个 failure records（不是"每个断言算一个 Failed Test"）
  - 异常 / EXPECT 失败 → 均 FAIL
- 维护统计：总测试数 / 通过 / 失败（基于 TestResult 汇总）

### 3.4 Summary（汇总报告）

- 输出通道：**Logger（OutputDebugStringW，Debug 通道）**——现有调试链路，Release 下测试不跑（现状保持）
- 格式（倾向）：
  ```
  [PASS] Widget.PanelPaint
  [FAIL] Widget.WidgetTree
      WidgetTests.cpp:123  EXPECT_EQ(commands.size(), 1)
      WidgetTests.cpp:130  EXPECT_TRUE(widget->IsVisible())
  ...
  ─────────────────────────
  Tests: 20  Passed: 18  Failed: 2
  ```
- **MessageBox 定位（v0.2 明确）**：只负责"**提醒失败**"（如 `"Tests failed: 2"`），**不是报告载体**——完整信息（逐测试行 + failure records）在 Debug Output；失败几十条时 MessageBox 塞全量日志会不可读

### 3.5 接入迁移策略（核心：不重写）

| 现有代码 | 处置 |
|---|---|
| 测试函数体 `FRAMEWORK_ASSERT(期望值)` | **机械替换**为测试断言（EXPECT_*）——语义保持，仅失败行为变（记录继续） |
| "测框架断言路径"的测试 | 保留 FRAMEWORK_ASSERT（双轨依据，不迁移） |
| 模块入口 `RunXxxTests()` | **改名 `RegisterXxxTests()`** + 执行 → 注册（签名不变） |
| `RunAllTests` | 注册（调 RegisterXxxTests）+ Runner 执行 + 汇总（main.cpp 零改动） |
| `RecordingBackend` / 像素测试 | 不动，正常注册进 Runner |

### 3.6 TestFramework 自测（v0.2 新增——基础设施本身不能无测试）

TestFramework 是新代码，若不测自身则成了"没有测试的基础设施"。**最小自测**（防"测试框架测自己"的递归，直接复用框架自身即可）：

| 自测项 | 验证点 |
|---|---|
| Registry 注册 | 能注册 TestCase 且可取回 |
| Runner 执行 | 能执行全部注册测试 |
| 失败不阻塞 | 一个 FAIL 不阻止后续 TestCase 执行 |
| 多失败汇总 | 多个 failure records 正确归集到单个 FAIL TestCase |
| 异常标记 | 抛异常的 TestCase 被记为 FAIL，Runner 继续 |

**归属与定位（v0.4 明确）**：单独一组（如 `TestFrameworkTests`），注册进统一 Runner——与业务测试同构，只是测的是框架自身。**定位为"基础设施回归测试"，而非"Runner 正确性证明"**（存在天然的鸡生蛋问题：Runner 若严重损坏，自测自身也可能无法运行）——**不作为 Runner 正常运行的硬前置依赖**。

### 3.7 详细设计约束（v0.3，GPT 补充——不改变决策，仅约束详细设计）

1. **状态生命周期与拥有关系**：目标模型为 `TestRegistry`（持有 `TestCase[]`）→ `TestRunner`（持有当前 `TestResult`（内含 `failures[]`）→ 执行 `TestFunction` → 内部经 TestContext 调用 `EXPECT_*` 上报。**避免全局测试状态**（`g_currentTest / g_failures / g_runner`）——全局状态越少越好，为未来跨平台迁移减负（不并行，但也不留全局负担）。
2. **RunAllTests 定位 = orchestration，不是第二个 Runner**：只做 `Register all → Run → Report` 三件事；测试核心（Registry/Runner/Result/Assert）保持平台无关——未来跨平台时 `WindowsTestMain / LinuxTestMain` 只是不同平台入口，核心复用（与 Phase 7.1 平台解耦方向一致）。

## 4. P0：TextBox Selection 测试边界（初步拆分）

现有 TextBoxTests 已有部分 Selection 测试（9 处引用）——**P0 是补全，非从零**。覆盖方向：

| 路径 | 覆盖要点 |
|---|---|
| 鼠标拖选 | 按下→移动→释放的选区范围（含反向拖动） |
| 键盘选择 | Shift+方向键扩展 / Ctrl+A 全选 |
| 双击选择 | 词选择 |
| 边界 | 跨 emoji/中文代理对；选区与光标一致性；Delete/Insert/退格对选区的影响（删选区 vs 删单字符） |

具体用例拆分留详细设计，但**详细设计第一步不是写测试，而是先确认 TextBox Selection 的索引单位契约（v0.2 强调）**——Selection 单位是 UTF-16 code unit、Unicode code point 还是 grapheme cluster，是**已有语义契约**；必须先确认实现口径，否则测试会"偷偷定义新需求"（测试按 code point 写、实现按 code unit 算 → 测试失败其实是需求不清）。确认顺序：**先读 TextBox Selection 实现 → 定索引单位契约 → 再拆用例**。

## 5. P1 方向（Event + 7.1 回归，可测性初判）

### 5.1 Event 系统测试（兑现 RunEventTests 挂起）
- **无窗口可行性**：Event 是值对象，可直接构造（不依赖 Win32 消息）→ 测 EventRouter 分派、事件层次、Bubbling 路径（Application 的 while+GetParent 迭代）
- 覆盖方向：Event 基类/类型分发 / Router 具名方法分派 / 输入事件层次

### 5.2 Phase 7.1 关键边界契约回归（只测契约，不追消息覆盖率）
**总原则（v0.2 明确）**：**若某个 7.1 回归测试必须创建真实 HWND 才能测，先证明该真实窗口依赖无法被平台抽象隔离；否则优先测抽象后的边界**——防 Phase 7.2 变回 "Win32 integration test suite"（与 7.1 解耦成果背道而驰）。

| 链路 | 可测性初判 |
|---|---|
| Window message → Translator → Event | **可无窗口测**（若 Translator 为纯函数：构造消息结构 → 断言 Event 输出）——详细设计核实 WindowMessageHandler 形态 |
| Translator → Application / EventRouter | 可无窗口测（Event 对象级联） |
| Window::Host 回调边界 | 待详细设计核实（可能需要最小窗口）——若必须 HWND，先证隔离不可行 |
| CaretGeometry → PlatformWindow | 待核实（坐标/几何纯计算部分可测） |
| IME composition → Framework | 待核实（Win32 IME 上下文强绑定，可能仅契约层验证） |

## 6. 文件与改动面预估

| 类别 | 文件 | 说明 |
|---|---|---|
| 新增 | `TestFramework.h` / `TestFramework.cpp` | Registry + Runner + 断言 + Summary（位置见决策 D1） |
| 修改 | 4 个测试模块 cpp | 断言迁移 + 入口改注册 |
| 修改 | `RunAllTests.h/.cpp` | 演进为注册 + 执行 + 汇总 |
| 不动 | main.cpp / 框架运行时代码 / RecordingBackend | 零侵入 |

## 7. 决策点（待用户确认）

| # | 决策点 | 倾向 |
|---|---|---|
| D1 | 测试框架文件位置 | `ECDI/src/Tests/TestFramework.h/.cpp`（测试模块内部，非公共 include）。**逻辑分块、物理单文件（v0.2）**：内部按 TestCase / TestRegistry / TestRunner / TestContext / TestResult / TestAssert 分块组织，不新增文件数（YAGNI） |
| D2 | 断言宏命名 | `EXPECT_TRUE / EXPECT_FALSE / EXPECT_EQ / EXPECT_NE / EXPECT_NEAR`（EXPECT 前缀，与 FRAMEWORK_ASSERT 的 ASSERT 语义区分；第一版不扩模板魔法） |
| D3 | 迁移策略 | 确认"机械替换期望值断言 + 保留框架断言路径测试"（双轨落地） |
| D4 | 失败汇总行为 | MessageBox 只提醒失败数（"Tests failed: N"），完整报告（逐测试行 + failure records）在 Debug Output |
| D5 | P0/P1 测试组织 | Selection 并入 TextBoxTests.cpp / Event 新开 EventTests.cpp（兑现 RunEventTests 挂起） |
| D6 | 模块入口命名（v0.2 新增） | `RunXxxTests()` → **`RegisterXxxTests()`**（语义=注册非执行，命名与语义对齐；改的是测试基础设施内部 API，成本最低时机） |

## 8. 明确不做（重申职责确认 §4）

第三方框架 / Mock / 覆盖率 / CI / 独立跨平台 Runner / UI 自动化 / Phase 8 全能力像素测试。

## 9. 修订历史

| 版本 | 日期 | 内容 |
|---|---|---|
| v0.4 | 2026-08-23 | GPT 评审通过 + 补充：视觉验证历史资产记账（三层：契约/真实后端/人工视觉——Phase 8 收尾目测留痕）；异常捕获边界措辞严谨化（只捕标准 C++ 异常，不承诺 SEH/访问违规/栈溢出）；自测定位明确（基础设施回归测试，非 Runner 正确性证明，不作硬前置）；"5 组件"措辞改"轻量测试基础设施"（列全 7 概念） |
| v0.3 | 2026-08-23 | v0.2 审查通过（GPT）；补 §3.7 详细设计约束 2 条：状态生命周期（避免全局测试状态）/ RunAllTests = orchestration（平台入口与测试核心分离） |
| v0.2 | 2026-08-23 | GPT 评审整合：模块入口改名 RegisterXxxTests（D6）；TestCase/TestResult 明确为数据结构；EXPECT 首版范围限定（5 个基础断言，无模板魔法）；断言/异常两层责任分离；MessageBox 仅提醒失败数；新增 §3.6 TestFramework 自测；P0 先确认 Selection 索引单位契约；7.1 回归加"真实 HWND 依赖先证隔离不可行"原则 |
| v0.1 | 2026-08-23 | 初稿（5 组件方案 + 接入迁移策略 + P0/P1 边界 + 决策点 D1-D5） |
