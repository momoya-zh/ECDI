#include "ECDI/Render/BackendFactory.h"

#include "Render/GDIBackend.h"
#include "Render/GDITextMeasurer.h"

// ★ Phase 26 批三：GL 侧（WGL 后端 + FreeType 文本栈）。
//   本文件是**平台装配点**——只有这里知道「Win32 → 哪个后端」，故允许 include 平台件
//   （Render 侧**头文件**仍不得反向依赖 Platform——盯防⑥ 的 grep 对象是 `.h`）。
#include "Render/FontEngine.h"
#include "Render/FreeTypeTextMeasurer.h"
#include "Render/GLRenderer.h"
#include "Platform/Win32/Win32FontSource.h"

#include <memory>

namespace ECDI{

RenderServices CreateDefaultRenderServices()
{
	// 7.1.4：默认后端 = GDI 渲染 + GDI 测量（两个独立对象——拆类后 unique_ptr 各自拥有）
	RenderServices services;
	services.renderer = std::make_unique<GDIBackend>();
	services.measurer = std::make_unique<GDITextMeasurer>();
	return services;
}

RenderServices CreateGLRenderServices()
{
	// ★★ 共享**同一个** `FontEngine`（Phase 26 结论 S6）——「同源」= 同一 face / size / dpi /
	//    rasterization policy；★ **不代表**共用 GPU 图集（图集只归 GLRenderer，详设 §3-⑤）。
	auto engine = std::make_shared<FontEngine>();
	engine->SetFontSource(std::make_unique<Win32FontSource>());

	RenderServices services;
	services.renderer = std::make_unique<GLRenderer>(engine);
	services.measurer = std::make_unique<FreeTypeTextMeasurer>(engine);
	return services;
}

}
