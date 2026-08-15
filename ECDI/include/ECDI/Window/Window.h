#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Window/WindowMessageHandler.h"
#include "ECDI/Render/GDIBackend.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护：DrawText → DrawTextW 会污染公共头的方法名（与 GDIBackend.h 同款）
#endif

#include <string>
#include <memory>

namespace ECDI{

class WindowClass;
class Application;
class Widget;
class KeyDownEvent;

/// @brief 框架层 Window 封装
/// @details
/// 管理一个 Win32 HWND，拥有 RootWidget 和 Focus 状态。
/// Window 通过 WindowProc 静态函数接收系统消息，分发给 WindowMessageHandler 翻译为 Framework Event。
///
/// 禁止拷贝和移动（HWND 通过 GWLP_USERDATA 绑定对象地址）。
class Window {
	public:

		/// @param app         所属 Application
		/// @param windowClass 已注册的窗口类
		/// @param title       窗口标题
		/// @param width       窗口总宽度（含边框和标题栏）
		/// @param height      窗口总高度
		Window(Application* app,const WindowClass &windowClass,const std::string&title,int width,int height);

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

		/// @brief Win32 窗口消息回调（静态函数，通过 GWLP_USERDATA 路由到实例）
		static LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);

		/// @brief 销毁底层 HWND
		bool Release()noexcept;

		/// @brief 设置当前拥有键盘焦点的 Widget
		/// @param widget 目标 Widget，必须属于当前窗口的 Widget 树；nullptr 表示清除焦点
		/// @pre widget != nullptr 时，必须可通过 Parent 链回溯到 RootWidget
		void SetFocusedWidget(Widget* widget);

		/// @brief 获取当前拥有键盘焦点的 Widget（可能为 nullptr）
		Widget* GetFocusedWidget() const noexcept;

		/// @brief 请求重绘整个客户区（Widget::Invalidate 上溯到根后调用 → WM_PAINT → PaintFrame）
		void Invalidate();

		/// @brief 获取文本测量器（5.5 T1；GDIBackend 兼 TextMeasurer——返回抽象接口不暴露后端）
		/// @details 控件经 protected GetWindow() 获取——非 Paint 时刻测量（点击定位光标等）
		TextMeasurer& GetTextMeasurer() noexcept;

		/// @brief 设置鼠标捕获控件（5.4.2 隐式捕获：Down 命中即捕获；Up 后释放）
		/// @param widget 捕获目标（后续 MouseMove/Up 直接派发给它，跳过 HitTest）；nullptr 释放
		void SetCaptureWidget(Widget* widget);

		/// @brief 获取当前鼠标捕获控件（无捕获返回 nullptr）
		Widget* GetCaptureWidget() const noexcept;

		/// @brief 键盘按键按下入口（5.4.4；Application::OnKeyDown 路由到这里）
		/// @details Tab → FocusNext（框架拦截，焦点导航是 Window 职责）；否则派发给焦点控件
		void HandleKeyDown(const KeyDownEvent& event);

		/// @brief IME 组合窗口定位（5.6；WM_IME_STARTCOMPOSITION 触发）
		/// @details MVP：焦点控件是 TextBox 时，把候选窗口移到光标位置
		/// （TextBox 给客户区坐标 → 这里 ClientToScreen → ImmSetCompositionWindow）。
		/// 非 TextBox 焦点直接返回（fail-safe：IME 交系统默认行为，不写错位置）。
		void NotifyIMEComposition();

		/// @brief 更新文本输入插入点位置（5.6 v1.0.3：系统 caret + IMM 双通道）
		/// @details TextBox 光标变动/IME 组合时调用，两个通道：
		/// ① SetCaretPos（TSF 输入法——Win11 微软拼音查询系统 caret 定位候选窗，实测主路径）
		/// ② ImmSetCompositionWindow（老 IMM 输入法查询——保底，GPT 双保险）
		/// 系统 caret 隐藏（HideCaret）：光标竖线由控件 OnPaint 自画，caret 仅作位置信标。
		/// @param clientPos 光标顶部客户区坐标（TextBox 零平台依赖，坐标转换是 Window 职责）
		void UpdateTextInputCaret(const Point& clientPos);

		/// @brief 销毁文本输入插入点（TextBox 失焦时调用——系统 caret 释放）
		void DestroyTextInputCaret();

private:

		/// @brief 实例级消息处理（WindowProc 路由到这里）
		LRESULT HandleMessage(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);

		/// @brief 帧编排（决策 41 改名，原 OnPaint）：clear→Paint→BeginFrame→Execute→EndFrame
		void PaintFrame();

		/// @brief 焦点导航（5.4.4）：树前序收集 CanFocus 控件，当前焦点按 direction 移动（循环）
		/// @param direction +1 正向（5.4 仅正向）；5.5 Shift+Tab 传 -1 反向
		void FocusNext(int direction = 1);

		HWND m_handle=nullptr;	///< 底层 Win32 窗口句柄

		Application* m_application = nullptr;	///< 所属 Application

		WindowMessageHandler m_messageHandler;	///< Win32 消息 → Framework Event 翻译器

		std::unique_ptr<Widget> m_rootWidget;	///< Widget 树的根节点（拥有所有权）

		Widget* m_focusedWidget = nullptr;	///< 当前拥有键盘焦点的 Widget（非拥有指针）

		Widget* m_captureWidget = nullptr;	///< 当前鼠标捕获控件（5.4.2，非拥有指针）

		bool m_caretCreated = false;	///< 系统 caret 是否已创建（5.6 v1.0.3 懒创建标记）

		GDIBackend m_backend;	///< GDI 渲染后端（值成员，决策 35：声明在 Renderer 之前）

		Renderer m_renderer;	///< 渲染执行器（引用 m_backend，决策 34）

		CommandBuffer m_commands;	///< 命令缓冲（决策 4：Window 持有跨帧复用）

};

}
