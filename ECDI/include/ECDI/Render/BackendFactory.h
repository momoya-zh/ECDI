#pragma once

#include "ECDI/Render/RenderServices.h"

namespace ECDI{

/// @brief 平台默认渲染服务工厂（7.1.4：默认后端选择从 Window 移出——GPT D3）
/// @details Win32 → GDIBackend + GDITextMeasurer（两个独立对象，unique_ptr）；
/// 未来 Linux → OpenGLRenderer + FreeTypeTextMeasurer 填同一 bundle（接口分离天然支持）。
RenderServices CreateDefaultRenderServices();

}
