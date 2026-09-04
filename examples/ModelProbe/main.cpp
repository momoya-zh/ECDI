#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

// Windows.h 宏防护（规范条 10：入口 cpp 显式 include Windows.h 同样要防护——DrawText 等宏不污染 ECDI 头声明）
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Window/Window.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"   // 文件名沿框架既有拼写（Requested → Requsted）
#include "ECDI/Widget/Panel.h"
#include "ECDI/Layout/VerticalLayout.h"

#include "ModelProbe.h"   // examples/ModelProbe 同目录（2026-09-03：demo 独立文件夹——原 src/Demo/ 相对路径废弃）

#include <memory>
#include <utility>

/// @brief ModelProbe 工具 Application：轮询接线（timerId=100 → PollProbe）+ 关窗清理（详设 §7.3/§7.4）
/// @details OnTimer：100 已消费（不转发基类）；其余（1=光标 2=动画）转发基类。
/// OnWindowCloseRequested：ShutdownBackend（StopTimer → CloseInput → Wait/Terminate）→ 转发基类关窗。
class DemoApplication : public ECDI::Application
{
public:

	void SetProbePage(ECDI::Demo::ModelProbePage* page) noexcept{ m_probePage = page; }

protected:

	void OnTimer(const ECDI::TimerEvent& event) override
	{
		if (m_probePage && event.GetTimerId() == ECDI::Demo::ModelProbePage::kProbePollTimer){
			m_probePage->PollProbe();   // 非阻塞——读可用字节 → 行缓冲 → 分发（GUI 轮询同款）
			return;
		}
		ECDI::Application::OnTimer(event);   // 光标闪烁 / 动画 tick 转发基类
	}

	void OnWindowCloseRequested(const ECDI::WindowCloseRequestedEvent& event) override
	{
		// 关窗清理（详设 §7.4 ①②③④）：StopTimer → CloseInput(EOF→probe 自退) → Wait/Terminate 兜底
		if (m_probePage){
			m_probePage->ShutdownBackend();
		}
		ECDI::Application::OnWindowCloseRequested(event);
	}

private:

	ECDI::Demo::ModelProbePage* m_probePage = nullptr;   ///< main 设置——窗口生命周期内有效（main 未返回）

};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	// 2026-09-03（demo 独立）：测试回框架侧（src/Tests + RunAllTests 保留，跑法随框架测试入口/工程——Phase 10 定）；
	// demo 入口 Release 纯 GUI。框架测试源码仍在 ECDI.vcxproj 编译（obj 级——不链接不执行）。

	// 后端就位（P2 资源释放）：检测 <exe_dir>\networkbackend\probe.exe 存在且大小匹配 → 复用；
	// 否则从 exe 内嵌 RCDATA 资源释放（tmp+rename 原子性）。失败仅记日志——GUI 照常启动，查询时提示。
	if (!ECDI::Demo::EnsureBackendExtracted()){
		ECDI::Logger::Log(ECDI::LogLevel::Error, L"后端 probe.exe 未就位——请检查程序完整性");
	}

	DemoApplication application;

	// ── ModelProbe 工具单窗口（release 形态：简易工具 exe——Showcase 不进入，代码保留编译）──
	// 9.7 自适应：RootWidget 设 VLayout(fillCrossAxis) → page SetStretch(1) 铺满全窗（D4——Window 不替用户
	// 决定 RootWidget 布局，显式设于 demo 入口）；窗口拉伸 → OnResized → Arrange → 页面整体跟随。
	// 原 bg 垫底层（页面 640×710 外露白底补丁）已随铺满化废弃——page 自身 #0f1115 背景即覆盖全窗（D3 必要改造）。
	ECDI::Window& win = application.Create("ECDI 模型探测工具", 680, 780);
	ECDI::Widget& root = win.GetRootWidget();
	root.SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true));   // spacing 0 / fillCrossAxis——单子场景无间隙语义

	auto page = std::make_unique<ECDI::Demo::ModelProbePage>();   // 默认 ChildProcess::Create()——真实 probe.exe 管道
	page->SetStretch(1);   // 9.7：主轴(height)+跨轴(width) 均随窗口
	page->SetStyle(ECDI::PanelStyleOverride{ .background = ECDI::Color::FromRGBA8(15, 17, 21, 255) });   // #0f1115 全窗底

	application.SetProbePage(page.get());

	root.AddChild(std::move(page));
	root.Arrange();
	win.Show();

	return application.Run();
}
