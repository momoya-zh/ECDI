# Phase 7.2 测试体系补强（基础设施 + 历史欠账）职责确认

> 状态：v1.1（2026-08-23）｜职责确认定案（用户 + GPT 评审整合；v1.1 落实评审修正）
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.5 事件回调 ✅ / Phase 8 渲染增强（已进入实现，**不阻塞**）
> 定位：Phase 10 转库（跨平台移植）前的测试保障安全网

## 1. 动机

Phase 7.1 平台抽象收官后，框架组件进入爆发期（Widget 树 / Event 系统 / 布局 / 文本框 / 渲染能力），测试需求显著增长。现有测试体系为**手写测试函数 + FRAMEWORK_ASSERT**（约 1000 行 / 4 模块），缺少统一注册、统计、汇总基础设施；且存在**历史欠账**：

- 5.5.2 P8 明确承诺的 TextBox Selection 测试
- `RunAllTests.cpp` 中挂起的 `RunEventTests(); // P2`
- Phase 7.1 平台解耦后无回归测试（平台消息 → Translator → Event → Framework 的边界）

Phase 7.2 定位 = **测试基础设施补强 + 历史欠账清理**，不扩大为泛化测试体系；为后续跨平台移植提供安全网。

**时间线关系**：7.2 不阻塞已确认的 Phase 8——Phase 8 自带 `RecordingBackend + RendererTests` 契约测试；GDI 像素验证作 P2 后续补充即可，不作为 7.2 前置条件。

## 2. 范围内（两个目标）

### 目标 1：轻量统一测试基础设施

```
Test Case
   ↓
Test Runner
   ↓
模块测试
   ↓
统一统计
   ↓
Pass / Fail Summary
```

硬约束（全部确认）：
- **零第三方依赖**（延续自研框架精神）
- **保留无窗口测试哲学**（契约层测试不依赖真实窗口；真实 Backend 像素测试经最小测试窗口承载 HWND——见 §5 说明，无窗口交互语义，不违背哲学）
- `RecordingBackend` 继续作为契约测试后端——测"命令是否正确发出"
- `FRAMEWORK_ASSERT` 与测试断言**分离**（双轨，语义不同）
- **失败继续执行**，最终统一汇总报告；单测试失败不影响其他测试执行，测试内严重异常由 Runner 捕获或提前结束当前 Test Case

### 目标 2：历史欠账清理（优先级）

| 优先级 | 内容 | 覆盖要点 | 来源 |
|---|---|---|---|
| **P0** | TextBox Selection | 鼠标拖选 / 键盘选择 / Shift 扩展选择 / 双击选择 | 5.5.2 P8 明确承诺，必须兑现 |
| **P1** | Event 系统测试 | 兑现 `RunEventTests` 挂起项；补事件分发/路由测试 | RunAllTests.cpp 注释 |
| **P1** | Phase 7.1 平台解耦回归 | 平台消息 → Translator → Event → EventRouter/Application；IME / CaretGeometry / PlatformWindow / Window→Host 边界 | 7.1 收官回归面空白 |

> **P1 范围限制（v1.1 明确）**：Phase 7.1 回归**只测平台抽象层的关键边界契约，不追求 Win32 消息覆盖率**——验证 7.1 的关键契约未被破坏，而非"把 Phase 7.1 再测一遍"。代表性链路（初步设计据此拆分）：
> - Window message → Translator → Event
> - Translator → Application / EventRouter
> - Window::Host 回调边界
> - CaretGeometry → PlatformWindow
> - IME composition → Framework
| **P2** | 覆盖度审计（不阻塞） | Focus / Tab / Capture / Invalidate；HorizontalLayout；Phase 8 像素测试扩展 | 审计 ≠ 100% 覆盖 |

> **P2 审计原则**：优先判断"哪些核心行为完全没有测试"，而非"哪些代码行没有测试"——测试规模有限不等于覆盖度低。

## 3. 关键决策（A-G 定案）

| # | 决策点 | 结论 |
|---|---|---|
| A | 测试框架选型 | ✅ **自研轻量框架**——零第三方依赖；现有测试已有断言与模块划分，只缺统一注册/统计/汇总基建 |
| B | 失败策略 | ✅ **失败继续 + 汇总**——比现状"失败即停"更适合真正的测试系统；失败测试最后统一报告。**细化原则（v1.1）**：单测试失败不影响其他测试执行；测试内严重异常由 Runner 捕获或提前结束当前 Test Case（防一个坏测试干掉整个 Runner） |
| C | 注册方式 | ⚠️ **不锁宏注册**——倾向**显式注册优先**（`RegisterTest(TestFoo)` 或模块级入口）；**判断标准（v1.1 明确）**：优先显式注册，**仅当测试数量增长后显式维护成本明显高于自动注册时才采用自动注册**；详细设计依实现复杂度定夺 |
| D | 欠账范围 | ✅ 按 P0/P1/P2 执行（见目标 2）；Phase 8 像素测试继续 P2，不阻塞 7.2 |
| E | 像素测试扩展 | ✅ **维持 AlphaBlend 单项**——定位（v1.1）为"**契约测试为主 + 少量真实 Backend/视觉测试兜底**"，而非纯单元测试体系；已有真实 Backend 像素验证作代表性验证，不为了表面完整度把每个 GDI primitive 扩成像素测试（个别 primitive 出问题再补对应测试） |
| F | 测试目标形态 | ✅ **维持现有入口**（RunAllTests 在 Debug 下自动跑）——Phase 10 转库时再考虑独立 test target |
| G | 断言双轨 | ✅ **独立测试断言 + FRAMEWORK_ASSERT 分离**——运行时断言（框架不变量）与测试断言（期望值）语义不同，混用会导致"框架 bug"与"测试失败"无法区分 |

## 4. 范围外（YAGNI，明确不做）

| 排除项 | 理由 |
|---|---|
| ❌ 第三方测试框架 | 零依赖原则；自研补齐成本低 |
| ❌ Mock 框架 | 自建 RecordingBackend 已覆盖契约测试需求 |
| ❌ 覆盖率统计工具 | 无 CI/发布需求，YAGNI |
| ❌ CI 集成 | v0.1 阶段不定 |
| ❌ 独立跨平台 Test Runner | 转库时（Phase 10）再评估 |
| ❌ UI 自动化测试 | 平台特定，与无窗口哲学冲突 |
| ❌ Phase 8 全能力像素测试 | 只保留 AlphaBlend 单项（决策 E） |

## 5. 现有资产（复用，不重构）

| 资产 | 形态 | 处置 |
|---|---|---|
| 4 个测试模块 | WidgetTests(157) / LayoutTests(189) / TextBoxTests(392) / RendererTests(247) | 接入统一基础设施，不重写 |
| `RecordingBackend` | 契约测试后端（转发记录 + 固定测量值） | 保留 |
| `TestGDIBackendAlphaBlend` | **真实 GDIBackend 像素/视觉语义测试**——绕过 RecordingBackend 直接验证真实后端"最终真的画出来了没有"（测绘制结果，非命令转发） | 保留，归 P2 范围不动 |

> **TestGDIBackendAlphaBlend 说明（v1.1 按实现事实校正）**：经**最小测试窗口**承载 HWND（`GDIBackend::BeginFrame` 依赖 `BeginPaint`，且 `GetPixel` 受 DC 裁剪约束需窗口可见——2026-08-21 实测），窗口短暂闪现、无交互语义——测试内容仍是**绘制结果验证**，不属于 UI 自动化；它是契约测试（RecordingBackend：命令是否正确发出）之外的真实 Backend 验证（像素/视觉语义是否成立）。
| `RunAllTests` | Debug 下 wWinMain 顺序调用入口 | 演进为统一 Runner 的调用面 |

## 6. 待初步设计展开的接口点（本阶段不锁架构）

| 接口点 | 说明 |
|---|---|
| 测试断言 API | 独立于 FRAMEWORK_ASSERT 的断言宏/函数（EXPECT 语义），命名与形态初步设计定 |
| 注册机制定夺 | 显式 vs 自动——**判断标准（决策 C）**：显式优先；测试数量增长后显式维护成本明显高于自动注册时才用自动注册；详细设计依实现复杂度定 |
| 汇总报告形态 | Pass/Fail Summary 的输出通道（Debug 输出 vs 日志）初步设计定 |
| P0/P1 测试边界拆分 | Selection 四种选择路径 / Event 构造方式 / 7.1 关键边界契约拆分（见 §2 P1 范围限制——代表性链路，不追消息覆盖率），初步设计展开 |

## 7. 修订历史

| 版本 | 日期 | 内容 |
|---|---|---|
| v1.1 | 2026-08-23 | GPT 评审整合：TestGDIBackendAlphaBlend 表述按实现事实校正（最小测试窗口承载 HWND）；P1 范围限制（只测关键边界契约，不追消息覆盖率）；C 判断标准明确（显式优先，维护成本明显高时才自动注册）；B 细化原则（单测失败不影响其他；异常捕获）；E 定位明确（契约为主 + 少量真实 Backend 视觉兜底） |
| v1.0 | 2026-08-23 | 初稿定案（用户确认 A-G 决策 + GPT 评审整合；C 改为"不锁宏注册"） |