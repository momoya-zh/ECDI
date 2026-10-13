#include "Render/RecordingBackend.h"

namespace ECDI {

	void RecordingBackend::DrawRect(const Rect& rect, const Color& color)
	{
		draws.emplace_back(DrawCall{ rect, color });
	}

	void RecordingBackend::DrawText(const Point& pos, const std::string& text,
	                                 const Color& color, const Font& font)
	{
		textDraws.emplace_back(TextDraw{ pos, text, color, font });
	}

	void RecordingBackend::DrawLine(const Point& start, const Point& end,
	                                float width, const Color& color)
	{
		// Phase 8：原样记录（width float 契约层数据不预取整——取整是 GDI 后端细节）
		lineCalls.emplace_back(LineDraw{ start, end, width, color });
	}

	void RecordingBackend::DrawRoundedRect(const Rect& rect, float cornerRadius,
	                                       const Color& color)
	{
		roundedRectCalls.emplace_back(RoundedRectDraw{ rect, cornerRadius, color });
	}

	void RecordingBackend::DrawImage(const Rect& dest, const Image& image)
	{
		// Image 值拷贝：imageCalls 持有独立副本（值语义测试——改原图不影响记录）
		imageCalls.emplace_back(ImageDraw{ dest, image });
	}

	void RecordingBackend::PushClip(const Rect& rect)
	{
		// isPush=true：与 PopClip 共享一列，保持 Push/Pop 相对顺序（状态命令顺序有语义）
		clipOps.emplace_back(ClipOp{ rect, true });
		++m_clipDepth;   // ★ 审计 M14：与 PopClip 的「栈空不记」配对（本替身的最小子集状态）
	}

	void RecordingBackend::PopClip()
	{
		// ★★ 审计 M14：**栈空时不再记录**——与两个真实后端对齐。
		//   修复前本处无条件 `clipOps.emplace_back(...)`，而 `GDIBackend.cpp:1119-1122`
		//   与 `GLRenderer.cpp:489-492` 都**直接返回** ⇒ 测试替身在真实后端会 no-op 的
		//   路径上照记 ⇒ 用例断言的是**真实后端不会有的命令**（假阳性风险）。
		//   ★ 语义对齐 = 「用户漏一次 PopClip」在替身上也表现为「多出的 Push 无人配对」，
		//     与真实后端「裁剪区不被弹出」是同一个可观测后果（而非替身多记一条）。
		//   ⚠️ 未引入显式栈：本替身的定位是**命令流记录器**（不是状态模拟器）——
		//     真实后端的 clip 栈是它们的私有实现细节，替身只需在「是否记录」上同构。
		//     故此处只做「栈空不记」的等价判断，不复制一份栈。
		if (m_clipDepth > 0)
		{
			clipOps.emplace_back(ClipOp{ Rect{}, false });
			--m_clipDepth;
		}
	}

	void RecordingBackend::DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color)
	{
		focusRectCalls.emplace_back(FocusRectDraw{ rect, cornerRadius, color });
	}

}
