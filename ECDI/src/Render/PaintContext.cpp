// PaintContext.cpp

#include "ECDI/Render/PaintContext.h"

#include <algorithm>   // (std::min)/(std::max)——括号防御 Windows min/max 宏

namespace ECDI {

	PaintContext::PaintContext(CommandBuffer& commands, TextMeasurer& measurer)
		: m_commands(commands)
		, m_measurer(measurer)
	{
	}

	PaintContext::PaintContext(CommandBuffer& commands, TextMeasurer& measurer,
	                           const Rect& initialClip, bool enableCulling)
		: m_commands(commands)
		, m_measurer(measurer)
		, m_cullStack{ initialClip }   // 种子按值拷贝（PaintContext 为每帧栈对象——无悬垂面，D27-A）
		, m_cullBase(1)                // 种子永不被 PopClip 弹出（守卫下限）
		, m_cullingEnabled(enableCulling)
	{
	}

	void PaintContext::DrawRect(const Rect& rect, const Color& color){

		m_commands.emplace_back(DrawRectCommand{ rect, color });

	}

	void PaintContext::DrawText(const Point& pos, const std::string& text,
	                            const Color& color, const Font& font){

		// 命令顺序 = 绘制顺序（控件先 DrawRect 背景再 DrawText 文本 → 文本叠背景）
		m_commands.emplace_back(DrawTextCommand{ pos, text, color, font });

	}

	void PaintContext::DrawLine(const Point& start, const Point& end,
	                            float width, const Color& color){

		// 原样进命令：宽度 float 契约层数据，GDI 端 lround 取整（后端实现细节）
		m_commands.emplace_back(DrawLineCommand{ start, end, width, color });

	}

	void PaintContext::DrawRoundedRect(const Rect& rect, float cornerRadius,
	                                   const Color& color){

		// 半径钳制封在后端（[0, min(w,h)/2]）——契约层只保证 cornerRadius >= 0
		m_commands.emplace_back(DrawRoundedRectCommand{ rect, cornerRadius, color });

	}

	void PaintContext::DrawImage(const Rect& dest, const Image& image){

		// Image 值拷贝进命令：命令持有独立副本，调用方随后修改/释放不影响绘制
		m_commands.emplace_back(DrawImageCommand{ dest, image });

	}

	void PaintContext::PushClip(const Rect& rect){

		// 状态命令：缓冲中的位置 = 生效范围起点（与其后绘制命令求交）
		// Phase 27：构建侧镜像栈先更新（栈空 = 无界 ⇒ next = rect 本身）——
		// 命令仍收原 rect：构建层「累计裁剪」与执行层「逐条求交」职责分离（C27-1）
		m_cullStack.push_back(m_cullStack.empty() ? rect : Intersection(rect));
		m_commands.emplace_back(PushClipCommand{ rect });

	}

	void PaintContext::PopClip(){

		// 状态命令：缓冲中的位置 = 裁剪区恢复点
		// Phase 27：构建侧镜像栈同步出栈——守卫仅作用于本镜像栈（种子永不弹出；
		// 即使上层多调一次 PopClip，镜像栈与命令栈也同步失衡而非崩溃，与既有
		// 「栈空跳过」防御同级）。★ m_cullBase 不是命令 Pop 的合法下限——
		// 命令发射照旧、不受守卫门控（评审 §12）
		if (m_cullStack.size() > m_cullBase)
			m_cullStack.pop_back();
		m_commands.emplace_back(PopClipCommand{});

	}

	bool PaintContext::IsRectVisible(const Rect& rect) const
	{
		if (!m_cullingEnabled)
			return true;   // 开关关闭 = 剪枝决策关（镜像栈照常维护——C27-6）
		if (m_cullStack.empty())
			return true;   // 无种子且无 push（双参构造首层前）= 无界
		return Intersects(m_cullStack.back(), rect);
	}

	Rect PaintContext::Intersection(const Rect& rect) const
	{
		const Rect& top = m_cullStack.back();
		const float x0 = (std::max)(top.x, rect.x);
		const float y0 = (std::max)(top.y, rect.y);
		const float x1 = (std::min)(top.x + top.width, rect.x + rect.width);
		const float y1 = (std::min)(top.y + top.height, rect.y + rect.height);
		return Rect{ x0, y0, x1 - x0, y1 - y0 };
	}

	void PaintContext::DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color){

		m_commands.emplace_back(DrawFocusRectCommand{ rect, cornerRadius, color });

	}

	Size PaintContext::MeasureText(const Font& font, const std::string& text){

		// 转发测量器（D2：测量帧无关，任何时刻可测）
		return m_measurer.MeasureText(font, text);

	}

	float PaintContext::LineHeight(const Font& font){

		// 转发测量器（P7：行高与 Measure 同源，垂直居中用）
		return m_measurer.LineHeight(font);

	}

}
