# Phase 7.2 测试体系补强 初步设计

> 状态：v0.1（2026-08-23）｜初步设计待审
> 前序：职责确认 v1.1 ✅（`phase7.2-test-system-requirements.md`）
> 产出：本稿 + 后续详细设计

## 1. 设计目标与硬约束

| 目标 | 说明 |
|---|---|
| **最低侵入接入** | 现有 ~1000 行测试（4 模块）**接入不重写**——机械迁移 + 形态调整 |
| 5 组件落地 | TestCase / Registry / Runner / Assert / Summary |
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

## 3. 组件设计（初步方案）

### 3.1 测试断言（Assert）——双轨落地

| 轨 | 用途 | 失败行为 |
|---|---|---|
| `FRAMEWORK_ASSERT` | 框架运行时不变量（框架 bug） | 日志 + 弹框 + 终止（现状不变） |
| **测试断言（新增）** | 测试期望值（test failure） | 记录失败（表达式/文件/行/函数）+ **继续** |

初步形态（倾向，签名级留详细设计）：
- `EXPECT_TRUE(cond)` / `EXPECT_EQ(a, b)` / `EXPECT_NEAR(a, b, eps)`（浮点，替代散落的 FloatEq）
- **宏形态**（与 FRAMEWORK_ASSERT 风格一致：自动捕获 `__FILE__/__LINE__/__func__`）
- 失败计数挂在"当前测试上下文"（Runner 提供），断言只上报不弹框

### 3.2 注册（Registry）——决策 C 落地（显式优先）

**方案**：模块入口**签名不变**（`RunXxxTests()` 仍无参），语义从"执行"变为"**注册**"：

```cpp
void ECDI::Test::RunWidgetTests() {
    GetTestRegistry().Add("Widget.PanelPaint", &TestPanelPaint);
    GetTestRegistry().Add("Widget.LabelPaint", &TestLabelPaint);
    ...
}
```

- 测试名带模块前缀（汇总报告可读性）
- `RunAllTests` 演进为：**注册（调 4 模块入口）→ Runner 统一执行 → 汇总**
- **main.cpp 零改动**（仍调 `RunAllTests`）
- 判断标准（职责确认 C）：显式优先；仅当测试数量增长后显式维护成本明显高于自动注册时才转自动注册

### 3.3 Runner

- 遍历 Registry 逐个执行测试函数
- **每个测试独立 try-catch**（捕获严重异常 → 记为失败 + 继续下一个——决策 B 细化）
- 维护统计：总测试数 / 通过 / 失败（失败含断言消息 + 位置）

### 3.4 Summary（汇总报告）

- 输出通道：**Logger（OutputDebugStringW，Debug 通道）**——现有调试链路，Release 下测试不跑（现状保持）
- 格式（倾向）：
  ```
  [PASS] Widget.PanelPaint
  [FAIL] Widget.WidgetTree  (TestWidgetTree:123  commands.size() == 1)
  ...
  ─────────────────────────
  Tests: 20  Passed: 18  Failed: 2
  ```
- **有失败 → 结束时 MessageBox 汇总一次**（保留"失败提醒"，替代逐个弹框）

### 3.5 接入迁移策略（核心：不重写）

| 现有代码 | 处置 |
|---|---|
| 测试函数体 `FRAMEWORK_ASSERT(期望值)` | **机械替换**为测试断言（EXPECT_*）——语义保持，仅失败行为变（记录继续） |
| "测框架断言路径"的测试 | 保留 FRAMEWORK_ASSERT（双轨依据，不迁移） |
| 模块入口 `RunXxxTests()` | 执行 → 注册（签名不变） |
| `RunAllTests` | 注册 + Runner 执行 + 汇总（main.cpp 零改动） |
| `RecordingBackend` / 像素测试 | 不动，正常注册进 Runner |

## 4. P0：TextBox Selection 测试边界（初步拆分）

现有 TextBoxTests 已有部分 Selection 测试（9 处引用）——**P0 是补全，非从零**。覆盖方向：

| 路径 | 覆盖要点 |
|---|---|
| 鼠标拖选 | 按下→移动→释放的选区范围（含反向拖动） |
| 键盘选择 | Shift+方向键扩展 / Ctrl+A 全选 |
| 双击选择 | 词选择 |
| 边界 | 跨 emoji/中文代理对；选区与光标一致性；Delete/Insert/退格对选区的影响（删选区 vs 删单字符） |

具体用例拆分留详细设计（需先确认 TextBox 现有 Selection API 能力面）。

## 5. P1 方向（Event + 7.1 回归，可测性初判）

### 5.1 Event 系统测试（兑现 RunEventTests 挂起）
- **无窗口可行性**：Event 是值对象，可直接构造（不依赖 Win32 消息）→ 测 EventRouter 分派、事件层次、Bubbling 路径（Application 的 while+GetParent 迭代）
- 覆盖方向：Event 基类/类型分发 / Router 具名方法分派 / 输入事件层次

### 5.2 Phase 7.1 关键边界契约回归（只测契约，不追消息覆盖率）
| 链路 | 可测性初判 |
|---|---|
| Window message → Translator → Event | **可无窗口测**（若 Translator 为纯函数：构造消息结构 → 断言 Event 输出）——详细设计核实 WindowMessageHandler 形态 |
| Translator → Application / EventRouter | 可无窗口测（Event 对象级联） |
| Window::Host 回调边界 | 待详细设计核实（可能需要最小窗口） |
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
| D1 | 测试框架文件位置 | `ECDI/src/Tests/TestFramework.h/.cpp`（测试模块内部，非公共 include——测试基础设施不属框架运行时公共 API；与 RecordingBackend 的公共位置差异待权衡） |
| D2 | 断言宏命名 | `EXPECT_TRUE / EXPECT_EQ / EXPECT_NEAR`（EXPECT 前缀，与 FRAMEWORK_ASSERT 的 ASSERT 语义区分） |
| D3 | 迁移策略 | 确认"机械替换期望值断言 + 保留框架断言路径测试"（双轨落地） |
| D4 | 失败汇总行为 | 结束时 MessageBox 汇总一次（替代逐个弹框）；失败详情同时进 Debug 输出 |
| D5 | P0/P1 测试组织 | Selection 并入 TextBoxTests.cpp / Event 新开 EventTests.cpp（兑现 RunEventTests 挂起） |

## 8. 明确不做（重申职责确认 §4）

第三方框架 / Mock / 覆盖率 / CI / 独立跨平台 Runner / UI 自动化 / Phase 8 全能力像素测试。

## 9. 修订历史

| 版本 | 日期 | 内容 |
|---|---|---|
| v0.1 | 2026-08-23 | 初稿（5 组件方案 + 接入迁移策略 + P0/P1 边界 + 决策点 D1-D5） |
