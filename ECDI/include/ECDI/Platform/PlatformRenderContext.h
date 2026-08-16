#pragma once

namespace ECDI{

/// @brief 平台渲染上下文基类（7.1.1 c-2 定稿：防 void* 类型擦除假抽象）
/// @details 空基类——平台句柄的类型安全容器。归位 Platform/（GPT 论证）：
/// 句柄本质是"窗口/平台句柄"不是"渲染句柄"——Win32RenderContext/X11RenderContext/
/// WaylandRenderContext 都是平台实现家族（与 Win32PlatformWindow 同族），语义统一在平台层。
/// RenderingBackend::Initialize 接收它（渲染层仅前置声明——零 include 依赖）。
class PlatformRenderContext{
public:
	virtual ~PlatformRenderContext() = default;
};

}
