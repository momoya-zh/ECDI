#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

// Windows.h 宏防护（规范条 10：入口 cpp 显式 include Windows.h 同样要防护——DrawText 等宏不污染 ECDI 头声明）
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Window/Window.h"
#include "ECDI/Window/CaptionBar.h"   // Phase 13：自绘标题栏（CaptionBar Widget）
#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"   // 文件名沿框架既有拼写（Requested → Requsted）
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"   // Phase 12 实测：状态事件回流
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


	void OnWindowStateChanged(const ECDI::WindowStateChangedEvent& event) override
	{
		// Phase 12 实测：状态事件回流显示（基类空实现——WindowChromeTests TestApp 同款先例）
		if (m_probePage){
			const char* name = "restored";
			switch (event.GetState()){
				case ECDI::WindowState::minimized: name = "minimized"; break;
				case ECDI::WindowState::maximized: name = "maximized"; break;
				default: break;
			}
			m_probePage->AppendWindowState(name);
		}
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

	// ── Phase 12 实测开关（配置期 API 只能在 Show 前生效——ChromeMode 一次确定，故经命令行选择）──
	// 用法：modelprobe.exe [--borderless [caption inset]] [--layer bottom|desktop]
	//   --borderless          无边框（客户区扩满整窗；caption 32 / inset 8 默认）
	//   --borderless 40 12    自定义标题栏高度与缩放热区
	//   --layer bottom        置底档；--layer desktop 桌面档（spike 未通过 → 降级 Bottom + Warning）
	bool borderless = false;
	int captionHeight = 32;
	int resizeInset = 8;
	ECDI::WindowLayer layer = ECDI::WindowLayer::Normal;
	{
		// 轻量分词（参数集很小——不引 shell32/CommandLineToArgvW）
		const std::wstring cmd(lpCmdLine ? lpCmdLine : L"");
		size_t pos = 0;
		auto nextToken = [&cmd, &pos]() -> std::wstring {
			while (pos < cmd.size() && cmd[pos] == L' ') ++pos;
			if (pos >= cmd.size()) return L"";
			const size_t start = pos;
			while (pos < cmd.size() && cmd[pos] != L' ') ++pos;
			return cmd.substr(start, pos - start);
		};
		for (std::wstring tok = nextToken(); !tok.empty(); tok = nextToken()){
			if (tok == L"--borderless"){
				borderless = true;
				const std::wstring cap = nextToken();
				if (!cap.empty() && cap[0] != L'-') {
					captionHeight = _wtoi(cap.c_str());
					const std::wstring ins = nextToken();
					if (!ins.empty() && ins[0] != L'-') resizeInset = _wtoi(ins.c_str());
				}
			} else if (tok == L"--layer"){
				const std::wstring v = nextToken();
				if (v == L"bottom")       layer = ECDI::WindowLayer::Bottom;
				else if (v == L"desktop") layer = ECDI::WindowLayer::Desktop;   // spike 未通过 → 降级 Bottom + Warning
			}
		}
	}
	ECDI::Widget& root = win.GetRootWidget();
	root.SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true));   // spacing 0 / fillCrossAxis——单子场景无间隙语义
	// ★ Phase 13：自绘标题栏（仅 Borderless——实体区高度与行为区 captionHeight 取同值；D7 不联动）。
	// ⚠️ 顺序约束：必须在 page 之前 AddChild —— VerticalLayout 按 children 顺序排布竖直次序（先前 = 上方）；
	//    bar 保持 SetStretch(0)（主轴固定高度），page 保持 stretch=1。
	if (borderless){
		auto bar = std::make_unique<ECDI::CaptionBar>(win, "ECDI 模型探测工具");
		bar->SetSize(680, captionHeight);   // 初始宽度（Arrange 会以真实宽度再次 SetSize）
		root.AddChild(std::move(bar));

		ECDI::Logger::Log(ECDI::LogLevel::Info, L"CaptionBar: attached（自绘标题栏）");
	}

	auto page = std::make_unique<ECDI::Demo::ModelProbePage>();   // 默认 ChildProcess::Create()——真实 probe.exe 管道
	page->SetStretch(1);   // 9.7：主轴(height)+跨轴(width) 均随窗口
	page->SetStyle(ECDI::PanelStyleOverride{ .background = ECDI::Color::FromRGBA8(15, 17, 21, 255) });   // #0f1115 全窗底

	application.SetProbePage(page.get());
	page->SetWindow(&win);   // Phase 12 实测接缝——非拥有 Window*（B 契约正确用法）

	root.AddChild(std::move(page));
	root.Arrange();

	// ── 配置期 chrome 应用（Show 之前；ChromeMode 一次确定）──
	if (borderless){
		win.SetChromeMode(ECDI::ChromeMode::Borderless);
		win.SetCaptionHeight(captionHeight);
		win.SetResizeInset(resizeInset);
		ECDI::Logger::Log(ECDI::LogLevel::Info, L"WindowChrome: Borderless enabled");
	}
	win.SetWindowLayer(layer);
	win.Show();

	return application.Run();
}
