# Phase 8 渲染增强（能力层）职责确认

> 状态：v1.1（2026-08-20）｜职责确认待审（GPT 评审整合）
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.2 无窗口单元测试体系 ✅ / Phase 7.5 事件回调 ✅
> 相关：phase6-checkboxradio-requirements.md（6.2 消费 DrawLine/DrawRoundedRect）/ roadmap-deferred.md（Phase 8 延期项）/ MEMORY.md（Alpha 归属决策）

## 1. 动机

Phase 7 完成平台抽象与测试体系后，Framework 层接口已稳定。Phase 8 的目标是**为渲染层补充缺失的底层能力**，使后续阶段（CheckBox/Radio 绘制、主题系统）能直接消费这些能力，而无需在控件层重复实现图形原语。

当前 GDIBackend 已实现基础 DrawText/DrawRect/FillRect，但缺少：
- 透明混合（AlphaBlend）
- 圆角矩形（DrawRoundedRect）
- 线条绘制（DrawLine）
- 图像绘制（DrawImage）
- 裁剪区域（PushClip/PopClip）——TextBox 文本裁切的性能优化需求
- 焦点框虚线框（现 DrawRect 实线）
- 渲染浮点化/亚像素（DrawTextContent int 参数等）

这些能力属于**渲染后端的职责扩展**，不涉及 Widget 层逻辑。

## 2. 范围内（6 项）

| # | 内容 | 优先级 | 备注 |
|---|---|---|---|
| R1 | **DrawingContext 扩展**：在 RenderingBackend 接口中添加 DrawLine/DrawRoundedRect/DrawImage/AlphaBlend 方法；DrawingContext 作为调用入口转发到 Backend | P0 | 接口契约定义；Backend 实现留待初步设计；DrawImage 资源模型留待初步设计确定 |
| R2 | **裁剪区域支持**：PushClip/PopClip 虚方法 + GDI  Region 实现 | P1 | TextBox 文本裁切优化；可推迟到 8.5 若无紧迫需求 |
| R3 | **焦点框虚线框**：DrawFocusRect 虚方法（或 DrawRect 增加虚线参数） | P2 | 现 DrawRect 两命令实线；虚线框是常见控件交互反馈 |
| R4 | **渲染坐标浮点化**：坐标/尺寸参数从 int 过渡到 float（DrawTextContent 等） | P2 | 渐进式，不影响现有控件；为后续高 DPI、抗锯齿及亚像素渲染提供接口基础 |
| R5 | **GDIBackend 实现**：为 R1-R3 提供 Windows 原生图形 API 实现（AlphaBlend 需 32bpp+AC_SRC_ALPHA） | P0 | 能力落地；具体 API 选型（GDI/msimg32/WIC 等）在初步设计确定；GDI+ 仅限 GDIBackend 内部，Framework 上层不暴露 GDI+ 类型 |
| R6 | **无窗口测试**：为 DrawingContext 接口编写 MockBackend 单元测试（验证参数传递正确性） | P1 | 7.2 体系扩展；不依赖真实窗口 |

## 3. 关键决策点（含倾向）

### D1 能力归属：DrawingContext vs RenderingBackend？

- 现有 DrawingContext（见 `Renderer/DrawingContext.h`）是轻量命令封装，不包含虚函数；RenderingBackend 是平台实现层（GDIBackend）
- **倾向**：**能力 API 的使用入口是 `DrawingContext`**，**能力接口的抽象定义在 `RenderingBackend`**，**具体实现位于 `GDIBackend`**；DrawingContext 保持值语义、转发到 Backend——与现有 DrawText/DrawRect 模式一致
- 理由：分层清晰——Widget 通过 DrawingContext 调用，不直接操作 RenderingBackend；DrawingContext 不负责"怎么画"，只负责收集命令

### D2 接口设计：新虚函数 vs 扩展现有 DrawRect？

- DrawRect 已有 `DrawRect(int x, int y, int w, int h, Color color)`；DrawRoundedRect 需要 `cornerRadius`
- **倾向**：**新增独立虚函数**（DrawRoundedRect/DrawLine/DrawImage/AlphaBlend），不改造 DrawRect——保持接口清晰，避免参数爆炸
- 理由：YAGNI（不为未来扩展预留空参数）；新能力语义独立

### D3 裁剪区域：PushClip/PopClip vs SetClipRect？

- 裁剪区域有两种模型：栈式（Push/Pop）和状态式（SetClipRect + ClearClipRect）
- **倾向**：**栈式（PushClip/PopClip）**——与 GDI 的 SaveDC/RestoreDC 对称，嵌套安全
- 理由：Widget 树的裁剪通常嵌套（父容器裁剪 → 子控件裁剪），栈式更自然

### D4 焦点框虚线：DrawFocusRect 虚函数 vs DrawRect 增加 style 参数？

- 现 DrawRect 是实线；虚线框是特殊绘制模式
- **倾向**：**新增 DrawFocusRect 虚函数**——语义明确（"焦点框"是控件交互状态，不是通用矩形）
- 理由：与 DrawRect 实线区分；后端可针对虚线优化（GDI DrawFocusRect 专用 API）

### D5 浮点化范围：全面 float vs 仅部分参数？

- 现有接口参数多为 int；全面 float 改动大
- **倾向**：**渐进式——先改 DrawingContext 新接口用 float，旧接口保持 int**；后续统一
- 理由：最小化破坏；现有控件坐标是 int，全面 float 收益暂不明显

### D6 测试策略：MockBackend 单元测试 vs 可视化测试？

- 渲染能力通常需要视觉验证（画出来对不对）
- **倾向**：**7.5 只做 MockBackend 单元测试**（验证参数传递正确性，不验证像素）；可视化测试推迟到集成测试或 Phase 10
- 理由：与 7.2 无窗口测试体系一致；像素对比测试依赖环境，不适合单元测试

### D7 与 Phase 9 主题系统的边界？

- Phase 9 消费 Phase 8 能力（圆角、透明等），但 Phase 8 不应包含主题逻辑
- **倾向**：**Phase 8 只实现能力，不定义主题 API**——DrawingContext 接口稳定后，Phase 9 通过 Theme/Style 注入参数
- 理由：职责分离；能力层与决策层正交（skill 第 19 条）

### D8 Alpha 归属：能力实现 vs 消费？

- MEMORY.md 决策：Alpha 归属 = 能力实现（Phase 8）；消费（Phase 9）
- **倾向**：**Phase 8 实现 AlphaBlend 接口**（32bpp + AC_SRC_ALPHA），但**不改变现有控件渲染**（控件仍用不透明绘制）
- 理由：能力先行，消费后置；避免 Phase 8 范围扩大
- **架构问题留给初步设计**：AlphaBlend 是独立绘制 API（`AlphaBlend(...)`）还是绘制属性（`DrawRect(..., alpha)`）？两种架构差别大，初步设计再定

### D9 CheckBox/Radio 勾/圆绘制归属？

- phase6-checkboxradio-requirements.md 延期到 Phase 8 后，消费 DrawLine/DrawRoundedRect
- **倾向**：**Phase 8 不实现 CheckBox/Radio 控件**——只提供能力；控件实现是独立阶段（可能在 Phase 8 之后、Phase 8.5 之前）
- 理由：能力层与控件层分离；CheckBox/Radio 实现需要组合多个能力（圆角+线条+填充+回调），应单独设计

### D10 DrawImage 资源模型？

- DrawImage 的复杂度比 DrawLine/DrawRoundedRect 高很多（图像来源、资源生命周期、缩放、Alpha 处理等）
- **倾向**：**Phase 8 DrawImage 只负责绘制已解码的 Image 数据结构**；文件格式加载（BMP/PNG/JPEG）不属于 RenderingBackend 能力，作为后续 Image Loader 能力单独设计
- 理由：职责分离——Renderer 只管"画"，不管"解码"；避免 Phase 8 范围扩大；未来可用 Windows WIC（而非 GDI+）解码 PNG

## 4. 范围外

- CheckBox/Radio 控件实现（独立阶段，消费 Phase 8 能力）
- 主题系统（Phase 9）
- 文本系统 2.0（Phase 8.5）
- 多行文本/滚动（Phase 8.5）
- Undo/Redo（Phase 8.5）
- 字体设置（SetFont，Phase 8.5）
- Dirty Region / Partial Redraw / 局部更新系统（Phase 9.5）
- 高 DPI 缩放（Phase 9.5 或更后）
- 图像解码库（libpng 等；Phase 8 不引入 GDI+，解码能力单独设计）

## 5. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | DrawingContext 转发到 Backend；Backend 是唯一接触 GDI 的层 |
| skill 16 Event 原则 | 渲染能力不涉及事件 |
| skill 18 RenderCommand | 新能力对应的 RenderCommand 由 Renderer 展开转发；Backend 不认识命令体系 |
| skill 19 能力层/决策层正交 | Phase 8 只实现能力（怎么画），Phase 9 定义主题（画成什么样） |
| skill 21 YAGNI | 不做 JPEG/GIF/高 DPI/全面 float；裁剪区域可推迟到 8.5 |
| skill 22 分层论证 | 接口设计用契约语言（"DrawLine = 在指定两点间绘制指定宽度颜色的线段"），不引用 GDI 实现细节 |
| 资源类禁复制禁移动 | DrawingContext 值语义；Backend 指针语义（与 Renderer 一致） |
| 测试由用户做 | MockBackend 单元测试写好后，用户编译验证（skill 第 1 条） |
| 五阶段法 | 本文档 = 职责确认；确认后进初步设计 |
| GDI+ 限制 | GDI+ 仅限 GDIBackend 内部实现，Framework 上层（Widget/DrawingContext/RenderingBackend 接口）不得暴露 GDI+ 类型 |

## 6. 修订记录

- v1.1（2026-08-20）整合 GPT 评审：
  - R1 明确 DrawingContext 是调用入口，RenderingBackend 是能力抽象接口，具体实现位于 GDIBackend
  - R4 "渲染浮点化/亚像素" → "渲染坐标浮点化"，亚像素作为未来收益
  - R5 "GDI+ 实现" → "Windows 原生图形 API 实现"，具体 API 选型留待初步设计；GDI+ 限制在 GDIBackend 内部
  - D1 补充分层明确（DrawingContext 调用入口 / RenderingBackend 抽象接口 / GDIBackend 实现）
  - D8 补充架构问题（AlphaBlend 是独立 API 还是绘制属性）留给初步设计解决
  - D10 改为 DrawImage 只负责绘制已解码 Image 数据结构，文件格式加载作为后续 Image Loader 能力单独设计
  - 范围外：局部更新/裁剪系统 → Dirty Region / Partial Redraw / 局部更新系统
  - 范围外：图像解码库明确 Phase 8 不引入 GDI+
  - §5 对齐表补充 GDI+ 限制条款
- v1.0（2026-08-20）职责确认初稿
