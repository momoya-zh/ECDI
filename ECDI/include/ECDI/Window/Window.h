#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Widget/CaretGeometry.h"
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Render/BackendFactory.h"
#include "ECDI/Render/RenderingBackend.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"

#include <string>
#include <memory>

namespace ECDI{

class PlatformWindow;   // 前置声明（unique_ptr 成员——平台抽象，7.1.1）
class Application;
class Widget;
class KeyDownEvent;
class Event;            // 前置声明（OnEvent 引用参数——7.1.2）

/// @brief 框架层 Window 封装
/// @details
/// 拥有 RootWidget、Focus 状态和 PlatformWindow（平台抽象——7.1.1 起零 Win32）。
/// Window 实现 PlatformWindowHost 契约：平台事件（绘制/尺寸/移动结束）→ Host 回调 → 框架响应。
/// 平台细节（HWND/WindowProc/消息翻译/IME 调用）全部在 Win32PlatformWindow。
///
/// 禁止拷贝和移动（Widget 树节点地址稳定 + PlatformWindow 生命周期绑定）。
class Window : public PlatformWindowHost {
	public:

		/// @param app         所属 Application
		/// @param title       窗口标题
		/// @param width       窗口总宽度（含边框和标题栏）
		/// @param height      窗口总高度
		/// @param services    渲染服务（7.1.4：默认 GDIBackend+GDITextMeasurer——平台默认工厂；
		///                   测试/未来可注入其他后端）
		/// 7.1.5：窗口类参数移除（窗口系统资源归平台层 WindowClass::Instance()——Window 不再认识平台对象）
		Window(Application* app,const std::string&title,int width,int height,
		       RenderServices services = CreateDefaultRenderServices());

		// 禁止拷贝
		Window(const Window&) = delete;

		Window& operator=(const Window&) = delete;

		// 禁止移动
		Window(Window&&) = delete;

		Window& operator=(Window&&) = delete;

		~Window()noexcept;

		/// @brief 显示窗口
		void Show();

		/// @brief 获取 RootWidget（Widget 树的根节点，代表窗口客户区）
		Widget& GetRootWidget() noexcept;

		/// @brief 销毁底层窗口句柄（转调 m_platformWindow->Release——幂等）
		bool Release()noexcept;

		/// @brief 设置当前拥有键盘焦点的 Widget
		/// @param widget 目标 Widget，必须属于当前窗口的 Widget 树；nullptr 表示清除焦点
		/// @pre widget != nullptr 时，必须可通过 Parent 链回溯到 RootWidget
		void SetFocusedWidget(Widget* widget);

		/// @brief 获取当前拥有键盘焦点的 Widget（可能为 nullptr）
		Widget* GetFocusedWidget() const noexcept;

		/// @brief 请求重绘整个客户区（Widget::Invalidate 上溯到根后调用 → WM_PAINT → PaintFrame）
		void Invalidate();

		/// @brief 获取文本测量器（7.1.4：m_textMeasurer——独立测量器，拆类后不再兼后端）
		/// @details 控件经 protected GetWindow() 获取——非 Paint 时刻测量（点击定位光标等）
		TextMeasurer& GetTextMeasurer() noexcept;

		/// @brief 获取平台窗口（8.5.1；控件经 protected GetWindow() 获取——平台能力入口）
		/// @details 剪贴板/Timer 等平台能力经此访问（与 GetTextMeasurer 同模式；
		/// 返回抽象接口——实现是 Win32PlatformWindow，框架层零 Win32 类型）
		PlatformWindow& GetPlatformWindow() noexcept;

		/// @brief 设置鼠标捕获控件（5.4.2 隐式捕获：Down 命中即捕获；Up 后释放）
		/// @param widget 捕获目标（后续 MouseMove/Up 直接派发给它，跳过 HitTest）；nullptr 释放
		void SetCaptureWidget(Widget* widget);

		/// @brief 获取当前鼠标捕获控件（无捕获返回 nullptr）
		Widget* GetCaptureWidget() const noexcept;

		/// @brief 键盘按键按下入口（5.4.4；Application::OnKeyDown 路由到这里）
		/// @details Tab → FocusNext（框架拦截，焦点导航是 Window 职责）；否则派发给焦点控件
		void HandleKeyDown(const KeyDownEvent& event);

		/// @brief IME 组合窗口定位（5.6；翻译器 WM_IME_START/COMPOSITION 直调）
		/// @details MVP：焦点控件是 TextBox 时，把候选窗口移到光标位置
		/// （TextBox 给客户区坐标 → 转调 m_platformWindow->UpdateTextInputCaret）。
		/// 非 TextBox 焦点直接返回（fail-safe：IME 交系统默认行为，不写错位置）。
		/// dynamic_cast<TextBox*> 为 Phase 5 遗留债务（7.1.1 不处理——EditableTextWidget 以后做）。
		void NotifyIMEComposition();

		/// @brief IME 组合串内容更新（8.5.1；平台层 OnIMECompositionUpdate 回调转发）
		/// @details 转发焦点 TextBox：更新组合状态（模型 B——覆盖 m_text 临时区间）。
		/// dynamic_cast<TextBox*> 为既有债务（同 NotifyIMEComposition——EditableTextWidget 以后做）。
		void NotifyIMECompositionUpdate(const std::string& compositionText);

		/// @brief IME 组合提交（8.5.1；平台层 OnIMECompositionCommit 回调转发）
		/// @details 转发焦点 TextBox：组合区间转正式文本 + 进 Undo（C3/C7 契约）。
		void NotifyIMECompositionCommit(const std::string& resultText);

		/// @brief 更新文本输入插入点位置（5.6 v1.0.3：系统 caret + IMM 双通道；7.1.3 参数升级 CaretGeometry）
		/// @details TextBox 光标变动/IME 组合时调用——薄转发 m_platformWindow
		/// （平台实现：SetCaretPos + ImmSetCompositionWindow，客户区坐标语义封装在平台层；
		/// visible=逻辑可见性——false 平台层 HideCaret）。
		/// @param geometry 插入点几何（客户区坐标，TextBox 零平台依赖）
		void UpdateTextInputCaret(const CaretGeometry& geometry);

		/// @brief 销毁文本输入插入点（TextBox 失焦时调用——薄转发 m_platformWindow）
		void DestroyTextInputCaret();

	private:

		// ── PlatformWindowHost 实现（7.1.1：平台事件 → 框架响应）──

		/// @brief 绘制请求（WM_PAINT）→ 帧编排
		void OnPaint() override;

		/// @brief 客户区尺寸变化（WM_SIZE）→ RootWidget 尺寸同步
		void OnResized(int width, int height) override;

		/// @brief 窗口移动/缩放结束（WM_EXITSIZEMOVE）→ 销毁+重建文本插入点（IME 归位）
		void OnExitSizeMove() override;

		/// @brief 事件来源窗口（翻译器构造 Event 需 Window*）
		Window* GetWindow() const noexcept override;

		/// @brief 事件转发（7.1.2 Dispatch 一级：翻译器 → 框架契约 → 本方法）
		/// @details Transitional adapter——当前转发 m_application->OnEvent；
		/// 最终派发目标可能随 7.1.5 Application 解耦变化（可能直接 EventRouter）
		void OnEvent(const Event& event) override;

		/// @brief IME 组合发生（7.1.2 方案 B：平台层状态同步区上报，非事件系统成员）
		/// @details 转发既有框架逻辑 NotifyIMEComposition（候选窗定位）
		void OnIMEComposition() override;

		/// @brief IME 组合串更新（8.5.1：Host 契约——平台层 GCS_COMPSTR 上报）
		/// @details 转发 NotifyIMECompositionUpdate（焦点 TextBox 更新组合状态）
		void OnIMECompositionUpdate(const std::string& compositionText) override;

		/// @brief IME 组合提交（8.5.1：Host 契约——平台层 GCS_RESULTSTR 上报）
		/// @details 转发 NotifyIMECompositionCommit（焦点 TextBox 组合转正式文本）
		void OnIMECompositionCommit(const std::string& resultText) override;

		/// @brief 帧编排（决策 41 改名，原 OnPaint）：clear→Paint→BeginFrame→Execute→EndFrame
		void PaintFrame();

		/// @brief 焦点导航（5.4.4）：树前序收集 CanFocus 控件，当前焦点按 direction 移动（循环）
		/// @param direction +1 正向（5.4 仅正向）；5.5 Shift+Tab 传 -1 反向
		void FocusNext(int direction = 1);

		Application* m_application = nullptr;	///< 所属 Application

		std::unique_ptr<PlatformWindow> m_platformWindow;	///< 平台窗口（组合，非拥有创建；7.1.1）

		std::unique_ptr<Widget> m_rootWidget;	///< Widget 树的根节点（拥有所有权）

		Widget* m_focusedWidget = nullptr;	///< 当前拥有键盘焦点的 Widget（非拥有指针）

		Widget* m_captureWidget = nullptr;	///< 当前鼠标捕获控件（5.4.2，非拥有指针）

		// 7.1.4：能力接口分离（用户决策）——绘制/测量各自独立对象（unique_ptr）；
		// ⚠️ m_renderBackend 必须声明在 m_renderer 前（Renderer 持 RenderingBackend&，初始化列表绑定）
		std::unique_ptr<RenderingBackend> m_renderBackend;	///< 绘制能力（GDIBackend——声明在 m_renderer 前！）

		std::unique_ptr<TextMeasurer> m_textMeasurer;	///< 测量能力（GDITextMeasurer）

		Renderer m_renderer;	///< 渲染执行器（引用 *m_renderBackend，决策 34）

		CommandBuffer m_commands;	///< 命令缓冲（决策 4：Window 持有跨帧复用）

};

}
