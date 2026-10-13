#pragma once

#ifdef DrawText
#undef DrawText   // 防御性 undef（2026-08-16）：本头声明 DrawText override——自包含防护
// （虽经 RenderingBackend.h 传递 undef，但头应自给自足——声明 DrawText 的头自己防护）
#endif

#include "ECDI/Core/Image.h"
#include "ECDI/Render/RenderingBackend.h"
#include "ECDI/Render/TextMeasurer.h"

#include <string>
#include <vector>

namespace ECDI {

	/// @brief 测试后端（决策 12 + D4）：记录绘制调用 + 固定测量值
	/// @details 同时实现 RenderingBackend 与 TextMeasurer 是**测试便利**，
	/// 不改变两个接口的独立关系（正交能力）。textDraws 记录完整参数——
	/// 既能测"画了文本"，又能验证 Renderer 原样转发（D4 约束 2）。
	class RecordingBackend : public RenderingBackend, public TextMeasurer {

	public:
		struct DrawCall
		{

			Rect rect;

			Color color;

		};

		struct TextDraw
		{
			Point pos;          ///< 文本起点
			std::string text;   ///< 文本内容（原样转发验证）
			Color color;
			Font font;
		};

		// Phase 8：记录结构（公开，测试断言）——与命令字段一一对应，验证原样转发
		struct LineDraw
		{
			Point start;
			Point end;
			float width = 1.0f;
			Color color;
		};

		struct RoundedRectDraw
		{
			Rect rect;
			float cornerRadius = 0.0f;
			Color color;
		};

		struct ImageDraw
		{
			Rect dest;   ///< 目标矩形（拉伸语义）
			Image image; ///< 值拷贝（命令持有独立副本——值语义测试用）
		};

		struct ClipOp
		{
			Rect rect;
			bool isPush = false;   ///< true=Push，false=Pop（Push/Pop 共享一列保序）
		};

		struct FocusRectDraw
		{
			Rect rect;
			float cornerRadius = 0.0f;   ///< 圆角半径（9.5 R4：焦点框跟随控件圆角）
			Color color;
		};

		std::vector<DrawCall> draws;   ///< DrawRect 记录（公开，测试断言）

		std::vector<TextDraw> textDraws;   ///< DrawText 记录（公开，测试断言）

		std::vector<LineDraw> lineCalls;            ///< DrawLine 记录（Phase 8）
		std::vector<RoundedRectDraw> roundedRectCalls;   ///< DrawRoundedRect 记录（Phase 8）
		std::vector<ImageDraw> imageCalls;          ///< DrawImage 记录（Phase 8）
		std::vector<ClipOp> clipOps;                ///< Push/Pop 共列保序（Phase 8）
		std::vector<FocusRectDraw> focusRectCalls;  ///< DrawFocusRect 记录（Phase 8）
		std::vector<Color> frameBackgrounds;        ///< BeginFrame 记录（Phase 18）：每帧清屏底色（**原样**记录——链路不替调用方做决策）

		void BeginFrame(const Color& background) override { frameBackgrounds.push_back(background); }           

		void DrawRect(const Rect& rect, const Color& color) override;     

		void DrawText(const Point& pos, const std::string& text,
		              const Color& color, const Font& font) override;    // textDraws.emplace_back

		void DrawLine(const Point& start, const Point& end,
		              float width, const Color& color) override;         // Phase 8

		void DrawRoundedRect(const Rect& rect, float cornerRadius,
		                     const Color& color) override;               // Phase 8

		void DrawImage(const Rect& dest, const Image& image) override;   // Phase 8（值拷贝）

		void PushClip(const Rect& rect) override;                        // Phase 8（isPush=true）

		void PopClip() override;                                         // Phase 8（isPush=false）

		void DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color) override;   // Phase 8 + 9.5 R4 圆角

		void EndFrame() override {}

		// TextMeasurer：固定值（测试断言"测量被调用"，不关心精度——那是 GDIBackend 真实 GDI 的事）
		//
		// ★★ 审计 M14：**空串返回 {0,0}**（与两个真实测量器对齐）。
		//   修复前对空串也返回 {10,14}，而 `FontEngine::MeasureText`（空码点序列不迭代 ⇒
		//   默认 `Size{}`）与 `GDITextMeasurer::MeasureText`（同族空循环）都返回 **{0,0}**
		//   ⇒ 替身与真实语义分裂，且「空串宽度」这条契约**从未被测过**。
		//   ★ 非空串仍返回固定 {10,14}（本替身的既有定位：让断言「测量被调用」，
		//     不模拟真实字形宽度）——只有**空串**这一档需要与真实对齐。
		Size MeasureText(const Font&, const std::string& text) override
		{
			return text.empty() ? Size{ 0.0f, 0.0f } : Size{ 10.0f, 14.0f };
		}

		float LineHeight(const Font&) override { return 14.0f; }

	private:

		/// @brief clip 配对深度（★ 审计 M14：令 `PopClip` 在栈空时**不记录**——与两个真实后端对齐）
		/// @details 真实后端（`GDIBackend.cpp:1119` / `GLRenderer.cpp:489`）栈空时直接返回；
		///          修复前本替身无条件记录 ⇒ 用例会断言「真实后端不会发出的命令」。
		///          ★ 只需这一个计数（**不是**完整栈）：本替身是**命令流记录器**而非状态模拟器，
		///          真实后端的 clip 栈是其私有实现细节，替身只需在「是否记录」上同构。
		int m_clipDepth = 0;
	};


}
