#pragma once

#include "ECDI/Render/RenderServices.h"

namespace ECDI{

/// @brief 平台默认渲染服务工厂（7.1.4：默认后端选择从 Window 移出——GPT D3）
/// @details Win32 → GDIBackend + GDITextMeasurer（两个独立对象，unique_ptr）；
/// 未来 Linux → OpenGLRenderer + FreeTypeTextMeasurer 填同一 bundle（接口分离天然支持）。
RenderServices CreateDefaultRenderServices();

/// @brief GL 渲染服务工厂（Phase 26：Windows/WGL + FreeType）
/// @details 返回 { `GLRenderer`, `FreeTypeTextMeasurer` }，两者**共享同一个 `FontEngine`**
///          （**同源同基准**——Phase 26 结论 S6：测量与渲染走同一 face / size / dpi）。
///          与 `CreateDefaultRenderServices` 并列——★ **默认仍 GDI**（详设 D1：GL 是可注入的
///          **第二后端**，默认路径逐位零回归）。
/// @note ★ 调用方**须检查就绪状态**（`GLRenderer::IsReady()`，经 `dynamic_cast` 取）——
///       **初始化失败不得静默回退 GDI**（详设 D7：否则性能对照可能实际跑的是 GDI）。
RenderServices CreateGLRenderServices();

}
