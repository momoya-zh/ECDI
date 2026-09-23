#include "Render/CoverageRaster.h"

#include <algorithm>
#include <cstddef>

namespace ECDI{

void RasterizeCornerPatch(std::uint8_t* dest, int stride,
                          const CornerCoverageMask& mask, const Color& color, CornerId corner)
{
	// 无意义组合直接跳过（不改变既有正常路径的行为：R <= 0 时原循环本就不执行）
	if (dest == nullptr || stride <= 0 || mask.Empty())
	{
		return;
	}

	const auto ToByte = [](float v)
	{
		const float clamped = std::clamp(v, 0.0f, 1.0f);
		return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
	};

	const int R = mask.radius;

	// 颜色分量（0~255）在循环外算好；每像素只再做一次 × c / 255
	const int cb = static_cast<int>(ToByte(color.b));
	const int cg = static_cast<int>(ToByte(color.g));
	const int cr = static_cast<int>(ToByte(color.r));

	for (int j = 0; j < R; ++j)
	{
		const int my = MaskIndexY(j, R, corner);
		std::uint8_t* line = dest + static_cast<std::size_t>(j) * static_cast<std::size_t>(stride);

		for (int i = 0; i < R; ++i)
		{
			const std::uint8_t c = mask.At(MaskIndexX(i, R, corner), my);

			// 预乘（约束 1）：color.a == 1 → A = c，RGB = colorByte × c（四舍五入）
			// 不变量：RGB = round(colorByte × c / 255) <= c = A（因 colorByte <= 255）✓
			line[i * 4 + 0] = static_cast<std::uint8_t>((cb * c + 127) / 255);
			line[i * 4 + 1] = static_cast<std::uint8_t>((cg * c + 127) / 255);
			line[i * 4 + 2] = static_cast<std::uint8_t>((cr * c + 127) / 255);
			line[i * 4 + 3] = c;
		}
	}
}

}
