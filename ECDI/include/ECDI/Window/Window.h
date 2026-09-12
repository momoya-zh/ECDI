#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Widget/CaretGeometry.h"
#include "ECDI/Widget/HoverTracker.h"
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Render/BackendFactory.h"
#include "ECDI/Render/RenderingBackend.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/WindowLayer.h"

#include <string>
#include <memory>
#include <chrono>

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
///
/// ── 所有权与生命周期契约（`docs/window-ownership.md` 初设 v1.1 §4.1 / §4.2）────────
/// Window 对象由 Application 持有并负责生命周期管理；**调用者不得对 Window\* 执行 delete**。
/// Window::Release() 仅负责释放**平台窗口资源**，不负责销毁 Window 对象。
/// 创建：Application::Create()（**当前唯一实际构造入口**——本类构造器为 private，
/// 构造权限经 `friend class Application` 授予 Application）。
class Window : public PlatformWindowHost {
	public:

		// 禁止拷贝 / 禁止移动（留在 public——deleted 函数在 public 区诊断信息更清晰）
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		/// @brief 析构（**保持 public**——B5b：对象所有权由 Application 承担，
		/// 用契约而非访问控制禁止外部 delete；私有化会使 unique_ptr 的 default_delete 编译失败）
		~Window()noexcept;

		/// @brief 显示窗口
		void Show();

		/// @brief 获取 RootWidget（Widget 树的根节点，代表窗口客户区）
		Widget& GetRootWidget() noexcept;

		/// @brief 销毁底层窗口句柄（转调 m_platformWindow->Release——幂等）
		/// @details **仅释放平台窗口资源**（销毁 HWND）；**不销毁 Window 对象**——
		/// 对象所有权归 Application（见类注释「所有权与生命周期契约」段）。
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

		// ── Phase 12：WindowChrome（配置期——构造后 / Show() 前调用）──────

		/// @brief 设置窗口 chrome 形态（R1）
		/// @param mode Normal（系统标题栏）/ Borderless（客户区扩满整窗）
		/// @pre 必须在 Show() 之前调用（配置期——见初设 §9.1 决策 D1）；
		/// Show() 之后调用记 Warning 并忽略（不抛异常）。
		/// ⚠️ **一次确定**（详设 D-CHROME-1）：配置期内重复调用一律 Warning + 忽略。
		void SetChromeMode(ChromeMode mode);

		/// @brief 设置自定义标题栏高度（R3；逻辑坐标 DIP）
		/// @pre 必须在 Show() 之前调用（同 SetChromeMode 生命周期契约）
		/// @details 仅 Borderless 模式有意义（Normal 模式系统标题栏不由本参数控制）；
		/// 决定 WM_NCHITTEST 中 HTCAPTION 命中区的高度（顶部起算）。
		void SetCaptionHeight(int height);

		/// @brief 设置缩放热区宽度（R3；逻辑坐标 DIP）
		/// @pre 必须在 Show() 之前调用（同 SetChromeMode 生命周期契约）
		/// @details 八向 resize 的命中宽度（四边 + 四角各向内 inset）。
		void SetResizeInset(int inset);

		/// @brief 设置窗口层级档位（R10）
		/// @pre 必须在 Show() 之前调用（与 chrome 三件套同组——配置期 API）
		/// @details 档位语义见 WindowLayer 定义；实现手段（WM_WINDOWPOSCHANGING /
		/// reparent / WinEventHook）为平台细节，公共 API 不承诺任何具体机制。
		void SetWindowLayer(WindowLayer layer);

		// ── Phase 12：窗口状态（运行期——**Show() 之后**才有效）────────

		/// @brief 最小化窗口——状态变化经 WindowStateChanged 事件回流
		/// @pre Show() 之前调用记 Warning 并忽略（运行期 API——见初设 §9.2）
		void Minimize();

		/// @brief 最大化窗口
		/// @pre 同 Minimize
		void Maximize();

		/// @brief 还原窗口（最小化态/最大化态通用）
		/// @pre 同 Minimize
		void Restore();

		/// @brief 动画统一 tick 到达入口（9.6；Application::OnTimer 保留 timerId 分支直调——不经焦点派发链）
		/// @details 计算 steady_clock 真实 elapsed（d3）转调 m_animationManager.Tick(elapsed)（d9 参数化）；
		/// Window 纯转发，零动画逻辑
		void OnAnimationTick();

		/// @brief 获取本窗口动画管理器（9.6；控件经 protected GetWindow() 访问——S1 状态色过渡入口）
		AnimationManager& GetAnimationManager() noexcept;

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

		// ── Hover 状态机（9.5 R4）────────────────────────

		/// @brief 更新 Hover 状态机（Application::OnMouseMove 调用——唯一入口）
		/// @param newTarget HitTest 命中的新目标（nullable）
		/// @pre newTarget == nullptr 或 IsWidgetInTree(newTarget) == true（调用方保证——HitTest 结果必然属于当前 Window Tree）
		/// @details 委托 m_hoverTracker（纯逻辑单元——方案 A 提取，Window 零平台依赖）
		void UpdateHoverState(Widget* newTarget);

	private:


		// ── 构造权限（B1：所有权契约的编译期基础）────────────────────────────
		/// @param app      所属 Application（B2：**引用**——「无主窗口」在语法上不存在）
		/// @param title    窗口标题
		/// @param width    窗口总宽度（含边框和标题栏）
		/// @param height   窗口总高度（含边框和标题栏）
		/// @param services 渲染服务（默认 GDIBackend+GDITextMeasurer；测试/未来可注入其他后端）
		/// @pre **框架外部不可直接构造**——构造权限授予 Application（`friend class Application`）；
		///      `Application::Create()` 是**当前唯一实际构造入口**（构造即登记，两者不可分离）。
		/// @details 构造权限的粒度说明：`friend class Application` 授予的是**整个 Application 类**，
		///          不是单个成员函数——因此准确表述是「框架外无法构造 + Application 拥有构造权限」，
		///          而非「只有 Create() 能构造」。
		Window(Application& app,const std::string&title,int width,int height,
		       RenderServices services = CreateDefaultRenderServices());

		friend class Application;   ///< 构造权限（B1）

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
		/// @details Transitional adapter——当前转发 m_application.OnEvent；
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

		Application& m_application;	///< 所属 Application（B2：引用——构造器初始化列表注入；「无主窗口」在语法上不存在）

		std::unique_ptr<PlatformWindow> m_platformWindow;	///< 平台窗口（组合，非拥有创建；7.1.1）

		// 9.6：per-Window 动画管理器（能力接缝——构造注入 *m_platformWindow；
		// ⚠️ 声明在 m_platformWindow 后：初始化列表引用 *m_platformWindow 需其先构造）
		AnimationManager m_animationManager;	///< 动画管理器（9.6，表现层基础设施）

		std::chrono::steady_clock::time_point m_lastAnimationTick{};	///< 动画 elapsed 锚点（timer 启动钩子重置）

		std::unique_ptr<Widget> m_rootWidget;	///< Widget 树的根节点（拥有所有权）

		Widget* m_focusedWidget = nullptr;	///< 当前拥有键盘焦点的 Widget（非拥有指针）

		Widget* m_captureWidget = nullptr;	///< 当前鼠标捕获控件（5.4.2，非拥有指针）

		HoverTracker m_hoverTracker;			///< Hover 状态机（9.5 R4 方案 A：纯逻辑单元——构造后 SetTreeRoot(m_rootWidget.get())）

		// 7.1.4：能力接口分离（用户决策）——绘制/测量各自独立对象（unique_ptr）；
		// ⚠️ m_renderBackend 必须声明在 m_renderer 前（Renderer 持 RenderingBackend&，初始化列表绑定）
		std::unique_ptr<RenderingBackend> m_renderBackend;	///< 绘制能力（GDIBackend——声明在 m_renderer 前！）

		std::unique_ptr<TextMeasurer> m_textMeasurer;	///< 测量能力（GDITextMeasurer）

		Renderer m_renderer;	///< 渲染执行器（引用 *m_renderBackend，决策 34）

		CommandBuffer m_commands;	///< 命令缓冲（决策 4：Window 持有跨帧复用）

};

}
