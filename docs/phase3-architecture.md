# 第三阶段 Widget System 总体架构

> 阶段 0 输出文档。2026-07-27 三方（开发者 + GPT + GLM）讨论收敛后的架构方向。
> 2026-08-08 更新至 **Phase3 完成态**：补充 Layout / Focus / Paint 子模块，修正各子模块状态为已实现。
> 本文档是 3.1~3.4 及后续 Layout/Focus/Paint 子模块开发的架构依据，各子模块仍走五阶段流程。

## 1. 所有权关系

```
Application
    ├── Window（多个）
    │       └── RootWidget
    │               └── Widget 树（Composite 模式，无独立 WidgetTree 类）
    └── ...
```

三个层次的生命周期：

- **Application 拥有 Window**（第二阶段已有）
- **Window 拥有 RootWidget**（第二阶段已有 Window，第三阶段扩展 `m_rootWidget`）
- **RootWidget 拥有 Widget 树**（第三阶段，RootWidget 是树根）

关键原则：Application 不拥有 Widget，Window 不拥有 Widget。只有 RootWidget 是 Widget 树的入口。

## 2. 事件流（扩展，不是重构）

**第二阶段已有**：

```
Win32 → WindowMessageHandler → Event → Application::OnEvent
```

**第三阶段已实现**：

```
Application::OnEvent → event.GetWindow() → RootWidget → Widget 树（HitTest → Dispatch → Bubbling）
```

关键代码：

```cpp
void Application::OnEvent(const Event& event)
{
    event.GetWindow()->GetRootWidget().Dispatch(event);
}
```

Event 基类带 `m_window`，Application 不需要知道"哪个 Window"，Event 自带来源。无额外路由层（WidgetEventRouter 已否决）。

**传播策略（3.2 定稿）**：Target Dispatch + Bubbling（`while` + `GetParent()` 迭代，冒泡逻辑放 Application 不放 Widget，避免子类忘调基类断链）。不引入 Tunneling。MouseUp 经 HitTest 直接派发，不引入 Capture 机制（后续需要时再设计）。

## 3. RootWidget 定位

- **是 Widget 树的入口**：框架层操作（Dispatch / Layout / Paint / HitTest）必须从 RootWidget 进入
- **不是特殊 Widget**：继承 Widget，`m_parent == nullptr`，生命周期绑定 Window，是树根
- **不是逻辑中心**：HitTest / Dispatch / Arrange 是 Widget 基类的能力，RootWidget 只是从树根开始调用
- **不是 God Object**：不往 RootWidget 塞 LayoutManager / FocusManager 等职责

### 唯一入口原则的边界

- **框架层操作**（Dispatch / Layout / Paint / HitTest）必须从 RootWidget 进入
- **Widget 状态 API**（SetText / SetVisible / SetLayout 等）可直接调用，不需要走 RootWidget

边界澄清原因：如果连 SetText 都要走 RootWidget 转发，RootWidget 就变成 God Object。

### 与实现的落地（2026-08-08 核实）

- 项目中**没有 RootWidget 类**——根就是一个普通 `Widget` 实例（`m_parent == nullptr`），由 `Window::m_rootWidget`（`std::unique_ptr<Widget>`）持有
- 入口调用方式：`window.GetRootWidget().Arrange()` / `.Paint(hdc, 0, 0)` / `.Dispatch(event)` / `.HitTest(x, y)`，与 Dispatch 完全同构
- "禁止对非根 Widget 调用框架层操作"是**架构约定（软约束）**，与 HitTest 的约束方式一致，不做类型隔离

## 4. 子模块与完成状态（Phase3 全量）

| 子模块 | 内容 | 状态（2026-08-08） |
|--------|------|----------|
| 3.1 Widget Foundation | Widget + Parent/Children + 生命周期 + Geometry + Visibility/Enabled | ✅ 已实现（Phase3 初期） |
| 3.2 Widget Event System | HitTest + Dispatch + Bubbling | ✅ 已实现，`FindTargetWidget` + 冒泡迭代（Application 层） |
| Layout | 布局策略（`unique_ptr<Layout>` + VerticalLayout + `Arrange` 递归） | ✅ 已实现（详细设计见 phase3-layout-design.md） |
| Focus | 焦点数据模型 + 获取 + 键盘分发 | ✅ 已实现（详细设计见 phase3-focus-design.md） |
| Paint | 树 → 像素链路（GDI Debug Drawing，`Paint`/`OnPaint`） | ✅ 已实现（详细设计见 phase3-paint-design.md） |
| 3.3/3.4 原定项 | Focus 数据模型 / Button+Label 基础控件 | ✅ 已并入上述模块完成；TextBox 移出第三阶段 |

## 5. 最终结论

1. 不新增 WidgetEventRouter
2. Window 持有 RootWidget
3. RootWidget 是 Widget 树根和唯一框架入口，但不是特殊逻辑中心
4. 不引入 WidgetTree 类，树由 Widget 自身（Composite 模式）构成
5. 第三阶段保持"扩展第二阶段，而不是重构"
6. 唯一入口原则针对框架层操作，Widget 状态 API 可直接调用
7. 传播策略：Target + Bubbling，不做 Tunneling
8. Focus 归属 Window 层（每个 HWND 一个焦点），Application 负责分发
9. Paint 阶段用 GDI 直接画（Debug Drawing），Renderer 抽象推迟到第四阶段

## 附：开发原则

- **Debug Drawing**：第三阶段用 GDI 直接画（验证 Widget 状态机），区分第四阶段 Renderer（RenderCommand → Renderer → D2D/GL）——Paint 子模块已按此落地
- **五阶段法**：每个子模块按 职责确认 → 初步设计 → 详细设计 → 实现 → 编译 → 测试 推进；设计文档写入 `docs/` 随代码提交（从 Phase4 起为强制约定）
- **不提前设计未来需求**：Capture / Dirty System / Text 绘制 / Theme 等均推迟到后续阶段
