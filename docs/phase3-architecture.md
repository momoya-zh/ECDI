# 第三阶段 Widget System 总体架构

> 阶段 0 输出文档。2026-07-27 三方（开发者 + GPT + GLM）讨论收敛后的架构方向。
> 本文档是 3.1~3.4 子模块开发的前置依据，各子模块仍走 8 步流程。

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
- **Window 拥有 RootWidget**（第二阶段已有 Window，第三阶段扩展加 `m_rootWidget`）
- **RootWidget 拥有 Widget 树**（第三阶段新增，RootWidget 是树根）

关键原则：Application 不拥有 Widget，Window 不拥有 Widget。只有 RootWidget 是 Widget 树的入口。

## 2. 事件流（扩展，不是重构）

**第二阶段已有**：

```
Win32 → WindowMessageHandler → Event → Application::OnEvent
```

**第三阶段新增**：

```
Application::OnEvent → event.GetWindow() → RootWidget → Widget 树
```

关键代码：

```cpp
void Application::OnEvent(const Event& event)
{
    event.GetWindow()->GetRootWidget().Dispatch(event);
}
```

Event 基类第二阶段已带 `m_window`，Application 不需要知道"哪个 Window"，Event 自带来源。不需要额外路由层（WidgetEventRouter 已否决）。

## 3. RootWidget 定位

- **是 Widget 树的入口**：框架层操作（Dispatch / Layout / Paint / Invalidate / HitTest）必须从 RootWidget 进入
- **不是特殊 Widget**：继承 Widget，`m_parent == nullptr`，生命周期绑定 Window，是树根
- **不是逻辑中心**：HitTest / Dispatch 是 Widget 基类的能力，RootWidget 只是从树根开始调用（类比第二阶段 `Window::WindowProc` 是入口，真正处理在 `WindowMessageHandler`）
- **不是 God Object**：不要往 RootWidget 塞 LayoutManager / FocusManager 等职责

### 唯一入口原则的边界

- **框架层操作**（Dispatch / Layout / Paint / Invalidate / HitTest）必须从 RootWidget 进入
- **Widget 状态 API**（SetText / SetColor / SetVisible 等）可以直接调用，不需要走 RootWidget

边界澄清原因：如果连 SetText 都要走 RootWidget 转发，RootWidget 就变成 God Object，违反"不要让 RootWidget 变成上帝对象"。

## 4. 子模块与验收目标

| 子模块 | 内容 | 验收标准 |
|--------|------|----------|
| 3.1 Widget Foundation | Widget + Parent/Children + 生命周期 + Visibility + Geometry | 能构建 Widget 树，父子关系正确，生命周期管理正确（销毁父时子正确清理） |
| 3.2 Widget Event System | HitTest + Dispatch + Propagation | 鼠标事件路由到 Hit Test 命中的目标 Widget，Debug Drawing 高亮命中 |
| 3.3 Focus | CurrentFocusedWidget 数据模型 | 能设置/获取焦点 Widget，焦点 Widget 收到键盘事件 |
| 3.4 Basic Widgets | Button + Label | Button 状态机（Pressed/Normal），Label 显示文字，Debug Drawing 可视化 |

## 5. 最终结论

1. 不新增 WidgetEventRouter
2. Window 持有 RootWidget
3. RootWidget 是 Widget 树根和唯一框架入口，但不是特殊逻辑中心
4. 不引入 WidgetTree 类，树由 Widget 自身（Composite 模式）构成
5. 第三阶段保持"扩展第二阶段，而不是重构"
6. 唯一入口原则针对框架层操作，Widget 状态 API 可直接调用

## 附：开发原则

- **Debug Drawing**：第三阶段用 GDI 直接画（验证 Widget 状态机），区分第四阶段 Renderer（RenderCommand → Renderer → D2D/GL）
- **不提前设计未来需求**：传播策略（Direct / Bubbling / Tunneling）推迟到 3.2 Widget Event System 子模块讨论
- **8 步流程**：每个子模块按 职责确认 → 初步设计 → 详细设计 → 实现 → 编译 → 单元测试 → 接入测试 → 实际运行测试 推进
