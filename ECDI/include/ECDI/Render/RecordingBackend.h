#pragma once

#include"RenderingBackend.h"

#include<vector>

namespace ECDI {

	class RecordingBackend :public RenderingBackend {

	public:
		struct DrawCall
		{

			Rect rect;

			Color color;

		};

		std::vector<DrawCall> draws;   ///< 记录的命令（公开，测试断言）

		void BeginFrame() override {}           
		
		void DrawRect(const Rect& rect, const Color& color) override;     
		
		void EndFrame() override {}
	};


}