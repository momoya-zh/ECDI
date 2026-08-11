#pragma once
#include"WindowMessageHandler.h"

#include<string>
#include<Windows.h>
#include<memory>
#include"ECDI/Render/GDIBackend.h"
#include"ECDI/Render/Renderer.h"
#include"ECDI/Render/RenderCommand.h"
namespace ECDI{

class WindowClass;
class Application;
class Widget;

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

private:

		/// @brief 实例级消息处理（WindowProc 路由到这里）
		LRESULT HandleMessage(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);

		/// @brief 帧编排（决策 41 改名，原 OnPaint）：clear→Paint→BeginFrame→Execute→EndFrame
		void PaintFrame();

		HWND m_handle=nullptr;	///< 底层 Win32 窗口句柄

		Application* m_application = nullptr;	///< 所属 Application

		WindowMessageHandler m_messageHandler;	///< Win32 消息 → Framework Event 翻译器

		std::unique_ptr<Widget> m_rootWidget;	///< Widget 树的根节点（拥有所有权）

		Widget* m_focusedWidget = nullptr;	///< 当前拥有键盘焦点的 Widget（非拥有指针）

		GDIBackend m_backend;	///< GDI 渲染后端（值成员，决策 35：声明在 Renderer 之前）

		Renderer m_renderer;	///< 渲染执行器（引用 m_backend，决策 34）

		CommandBuffer m_commands;	///< 命令缓冲（决策 4：Window 持有跨帧复用）

};

}
