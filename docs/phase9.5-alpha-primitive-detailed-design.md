# Phase 9.5 Alpha Primitive 补强（GDIBackend 半透明绘制）

> 状态：v1.0（2026-08-26）｜简短设计（用户定：能力补强记账级，不展开三档）
> 定位：**能力层补齐**——RenderCommand 的 Color.a 语义一直存在，GDIBackend 未兑现（DrawRect/RoundedRect 的 ToColorRef 丢弃 alpha）。非 API 变化，RecordingBackend/Renderer/Command 零改动。
> 与 9.6 边界：本次 = **primitive alpha 能力**（Color.a → AlphaBlend）；9.6 = opacity(t) 时间驱动（只把动画结果转成渲染属性，**不重新实现 alpha blending**）。

## 1. 目标

使 `Color.a < 1` 的基础几何绘制在 GDIBackend 中正确合成（半透明矩形/圆角矩形）。

## 2. 方案（GDIBackend.cpp 单文件）

| Primitive | `a == 1` | `a < 1` |
|---|---|---|
| DrawRect | 原 FillRect 快速路径（零开销，不回归） | 临时预乘 32bpp DIB + AlphaBlend |
| DrawRoundedRect | 原 RoundRect 快速路径 | 同上（DIB 内画圆角，仅颜色 alpha） |
| DrawLine | 原路径 | 记账（细长区域 DIB 不划算） |
| DrawText | 原路径 | 记账（GDI 文本无 alpha） |
| DrawImage | 已支持 | 已支持（Phase 8 已验证） |

实现：匿名 namespace 辅助 `BlendAlphaSolid(HDC, Rect, Color, cornerRadius)`——创建临时 32bpp 顶降 DIB（负 biHeight），逐像素填充，AlphaBlend(AC_SRC_ALPHA) 到 m_memoryDC。

## 3. 两条硬约束（GPT 评审写死）

- **约束 1（预乘规则）**：DIB 像素必须**预乘 BGRA**——`B=round(b·a·255), G=round(g·a·255), R=round(r·a·255), A=round(a·255)`。禁止写非预乘 RGB（`(255,0,0,128)` 是错的，应为 `(128,0,0,128)`）。复用 Phase 8 DrawImage §8.3 已验证的 AC_SRC_ALPHA 链路。
- **约束 2（圆角无抗锯齿）**：`DrawRoundedRect` 半透明分支只做颜色 alpha 合成，**不引入几何抗锯齿**（圆角边缘与现有 GDI RoundRect 同款非 AA 语义）。防膨胀成 alpha compositor。

## 4. 明确不做（YAGNI）

Global/Widget Opacity、PushOpacity/PopOpacity、DIB 缓存池、Alpha batching、Dirty Region、GPU compositor、抗锯齿升级、DrawLine alpha、DrawText alpha。

## 5. 测试

- 复用 `RendererTests.cpp::TestGDIBackendAlphaBlend` 模式（真实窗口 + GetPixel）：DrawRect 半透明红叠纯蓝底 → 期望 `RGB(128, 0, 127)`（±3 容差，premultiplied 语义）。
- 回归：`a == 1` 路径像素输出与改造前一致（原 DrawRect 测试覆盖）。

## 6. 修订记录

- v1.0（2026-08-26）简短设计：方案 + 两条硬约束 + 不做清单。
