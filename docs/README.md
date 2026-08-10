# ECDI 设计文档索引

> 本文档是 `docs/` 的索引。设计文档随代码提交 git，从 Phase4 起为强制约定（职责确认 / 初步设计 / 详细设计 各阶段文档正常写入本目录）。

## Phase3 Widget System

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase3-architecture.md](phase3-architecture.md) | Phase3 总体架构：所有权、事件流、RootWidget 定位、唯一入口原则、子模块总览 | ✅ 完成态（2026-08-08 更新） |
| [phase3-layout-design.md](phase3-layout-design.md) | Layout 子模块详细设计：Layout 策略基类、VerticalLayout、Arrange 递归、unique_ptr 完整类型坑（v1.1） | ✅ 已实现并测试（2026-08-06） |
| [phase3-focus-design.md](phase3-focus-design.md) | Focus 子模块详细设计：CanFocus、MouseDown 获取、SetFocusedWidget 验证、点击空白保持、键盘不 Bubbling | ✅ 已实现（2026-08-07） |
| [phase3-paint-design.md](phase3-paint-design.md) | Paint 子模块详细设计：Paint/OnPaint 同构、HDC 前向声明、offset 累加、WM_PAINT 入口、颜色硬编码 | ✅ 已实现（2026-08-07） |

## Phase4 Renderer（规划中）

> 尚未开始。架构方向：`Widget → RenderCommand → Renderer → D2D/GL`，替换 Phase3 的 GDI 临时桥梁。

## 文档约定

- 命名：`phaseN-<module>-<type>.md`（如 `phase3-layout-design.md`）
- 五阶段法：职责确认 → 初步设计 → 详细设计 → 实现 → 测试，设计文档在实现前评审通过
- 文档内附修订记录（v1.0 → v1.1...），实现中发现的与文档出入必须回写
