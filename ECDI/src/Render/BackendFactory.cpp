#include "ECDI/Render/BackendFactory.h"

#include "ECDI/Render/GDIBackend.h"
#include "ECDI/Render/GDITextMeasurer.h"

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

}
