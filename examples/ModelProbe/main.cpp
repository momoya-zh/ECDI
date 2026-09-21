#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

// Windows.h 宏防护（规范条 10：入口 cpp 显式 include Windows.h 同样要防护——DrawText 等宏不污染 ECDI 头声明）
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Window/Window.h"
#include "ECDI/Window/CaptionBar.h"   // Phase 13：自绘标题栏（CaptionBar Widget）
#include "ECDI/Application/Application.h"
#include "ECDI/Application/TrayIcon.h"   // Phase 14 A7：托盘图标/菜单（值类型——零 Win32 类型）
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"   // Phase 14 A7：日志打印 UTF-8 路径需转 UTF-16
#include "ECDI/EventSystem/Application/TrayEvent.h"   // Phase 14 A7：托盘交互事件（应用级）
#include "ECDI/EventSystem/Window/DropFilesEvent.h"   // Phase 14 A7：文件拖入事件（窗口级）
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"   // 文件名沿框架既有拼写（Requested → Requsted）
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"   // Phase 12 实测：状态事件回流
#include "ECDI/Widget/Panel.h"
#include "ECDI/Layout/VerticalLayout.h"

#include "ModelProbe.h"   // examples/ModelProbe 同目录（2026-09-03：demo 独立文件夹——原 src/Demo/ 相对路径废弃）

#include <memory>
#include <string>
#include <utility>

namespace {

/// @brief 窗口标题（单一来源——任务栏/Alt-Tab 取自 application.Create，自绘标题栏取自 CaptionBar 构造）
constexpr const char* kWindowTitle = "ECDI 模型探测工具";

}

/// @brief ModelProbe 工具 Application：轮询接线（timerId=100 → PollProbe）+ 关窗清理（详设 §7.3/§7.4）
/// @details OnTimer：100 已消费（不转发基类）；其余（1=光标 2=动画）转发基类。
/// OnWindowCloseRequested：ShutdownBackend（StopTimer → CloseInput → Wait/Terminate）→ 转发基类关窗。
class DemoApplication : public ECDI::Application
{
public:

	void SetProbePage(ECDI::Demo::ModelProbePage* page) noexcept{ m_probePage = page; }

	/// @brief 设置目标窗口（Phase 14 A7——托盘菜单的运行期操作 + 模式 B 隐藏；非拥有——B 契约）
	void SetWindow(ECDI::Window* window) noexcept{ m_window = window; }

	/// @brief 模式 B（A4-Mode）：关闭按钮 → `Hide()` 隐藏到托盘（不销毁窗口、不关停后端）
	void SetHideOnClose(bool enabled) noexcept{ m_hideOnClose = enabled; }

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
		// Phase 14 模式 B（--hide-on-close）：关窗 = 隐藏到托盘——不 ShutdownBackend、不销毁窗口。
		// 窗口仍在 Application::m_windows ⇒ 「最后窗口关闭」的隐式退出根本不会发生（详设 §6.6 三角关系）
		if (m_hideOnClose && m_window){
			m_window->Hide();
			ECDI::Logger::Log(ECDI::LogLevel::Info, L"Phase14: close -> Hide (mode B; restore via tray menu)");
			return;
		}

		// 关窗清理（详设 §7.4 ①②③④）：StopTimer → CloseInput(EOF→probe 自退) → Wait/Terminate 兜底
		if (m_probePage){
			m_probePage->ShutdownBackend();
		}
		ECDI::Application::OnWindowCloseRequested(event);

		// 窗口已进入销毁（Release → deferred destroy）：置空非拥有指针，防托盘菜单再操作悬空对象
		// （模式 A --stay：销毁后进程存活 ⇒ 菜单的显示/隐藏失效，仅「退出」有意义）
		m_window = nullptr;
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

	// ── Phase 14 A7：托盘交互（应用级事件——无窗口上下文，不经 HitTest，直接到这里）──

	void OnTrayEvent(const ECDI::TrayEvent& event) override
	{
		const char* kind = "Select";
		switch (event.GetTrayType()){
			case ECDI::TrayEventType::KeySelect:   kind = "KeySelect";   break;
			case ECDI::TrayEventType::DoubleClick: kind = "DoubleClick"; break;
			case ECDI::TrayEventType::ContextMenu: kind = "ContextMenu"; break;
			default: break;
		}

		// 观测通道（A3）：**序号** + 种类 + 锚点（屏幕坐标）；日志用 ASCII（条 48），界面用中文
		// 序号是判定"一次操作来了几个事件"的关键（标签是 SetText 替换语义，不带序号看不出次数）
		++m_trayEventSeq;
		if (m_probePage){
			m_probePage->AppendNotice(std::string("托盘 #") + std::to_string(m_trayEventSeq) + "：" + kind
				+ "(" + std::to_string(event.GetX()) + "," + std::to_string(event.GetY()) + ")");

			// A5 判据：锚点坐标语义实测——记录**同一时刻的实时光标位置**做对照
			//（两者基本重合 ⇒ 锚点是屏幕坐标；ContextMenu 走 GetCursorPos 兜底，必然重合）
			POINT cursor{};
			GetCursorPos(&cursor);
			m_probePage->LogEvent("    cursor=(" + std::to_string(cursor.x) + ","
				+ std::to_string(cursor.y) + ")");
		}
		ECDI::Logger::Log(ECDI::LogLevel::Info, ECDI::UTF8ToWide(std::string("TrayEvent: ") + kind));

		if (event.GetTrayType() != ECDI::TrayEventType::ContextMenu){
			return;
		}

		// 右键 ⇒ 弹菜单（D10：同步返回选中 ID；0 = 未选中/取消）
		const int id = ShowTrayMenu(ECDI::TrayMenu{ .items = { { 1, "显示窗口" }, { 2, "隐藏窗口" }, { 3, "退出" } } });
		ECDI::Logger::Log(ECDI::LogLevel::Info, ECDI::UTF8ToWide("TrayMenu id: " + std::to_string(id)));

		switch (id){
			case 1:
				if (m_window){ m_window->Show(); }
				else{ ECDI::Logger::Log(ECDI::LogLevel::Warning, L"TrayMenu: window already closed (use Quit)"); }
				break;
			case 2:
				if (m_window){ m_window->Hide(); }
				else{ ECDI::Logger::Log(ECDI::LogLevel::Warning, L"TrayMenu: window already closed (use Quit)"); }
				break;
			case 3:
				if (m_probePage){
					m_probePage->ShutdownBackend();   // 幂等（多调无害）——模式 B 下窗口未关，此处首次关停
				}
				Exit();
				break;
			default:
				break;   // 0 = 取消 / 点击菜单外部
		}
	}

	/// @brief 文件拖入（Phase 14 R11——观测后转发基类，保持 Widget 层 bubbling）
	void OnDropFiles(const ECDI::DropFilesEvent& event) override
	{
		int index = 0;

		for (const std::string& path : event.GetPaths()){
			++index;
			ECDI::Logger::Log(ECDI::LogLevel::Info, ECDI::UTF8ToWide("DropFiles path: " + path));
			if (m_probePage){
				// A4-1 判据：UTF-8 路径**原样**落日志（中文正常显示 = 平台层 UTF-8 边界正确）
				m_probePage->LogEvent("    路径 " + std::to_string(index) + ": " + path);
			}
		}

		if (m_probePage){
			m_probePage->AppendNotice("拖入：" + std::to_string(event.GetPaths().size()) + " 个文件 ("
				+ std::to_string(event.GetX()) + "," + std::to_string(event.GetY()) + ")");
		}

		ECDI::Application::OnDropFiles(event);   // ★ 不得省略：HitTest → Dispatch → Widget::OnDropFiles
	}

private:

	ECDI::Demo::ModelProbePage* m_probePage = nullptr;   ///< main 设置——窗口生命周期内有效（main 未返回）

	ECDI::Window* m_window = nullptr;   ///< Phase 14 A7：非拥有（Application 拥有——B 契约）；销毁时置空

	bool m_hideOnClose = false;         ///< Phase 14 A7 模式 B：关闭按钮 → 隐藏到托盘

	int m_trayEventSeq = 0;             ///< Phase 14 A3 观测：托盘事件序号（判定"一次双击来几个事件"）

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
	ECDI::Window& win = application.Create(kWindowTitle, 680, 780);

	// ── chrome 形态（默认自绘标题栏；配置期 API 只能在 Show 前生效——ChromeMode 一次确定，故经命令行选择）──
	// 用法：modelprobe.exe [--native] [--borderless [caption inset]] [--layer bottom|desktop]
	//                     [--no-tray] [--no-drop] [--stay] [--hide-on-close]
	//   （无参数）             自绘标题栏（Borderless + CaptionBar）+ 托盘图标 + 文件拖入
	//   --native               系统标题栏（Normal——零回归对照）
	//   --borderless 40 12     自绘 + 自定义标题栏高度与缩放热区
	//   --layer bottom         置底档；--layer desktop 桌面档（已落地——紧贴桌面窗口正上方，Win+D 后仍可见）
	//   --no-tray / --no-drop  Phase 14 A7 对照：不注册托盘 / 不开启文件拖入
	//   --stay                 Phase 14 A4 模式 A：真关窗后进程存活（SetQuitOnLastWindowClosed(false)）
	//   --hide-on-close        Phase 14 A4 模式 B：关闭按钮 = 隐藏到托盘（托盘「显示窗口」恢复）
	bool borderless = true;    // 默认自绘标题栏（--native 回退系统标题栏）
	bool trayIcon = true;      // Phase 14 A7：托盘图标（--no-tray 关闭）
	bool fileDrop = true;      // Phase 14 A7：文件拖入（--no-drop 关闭）
	bool stayAlive = false;    // Phase 14 A4 模式 A：最后窗口关闭时不退出
	bool hideOnClose = false;  // Phase 14 A4 模式 B：关闭 = 隐藏到托盘
	int captionHeight = ECDI::CaptionBar::kDefaultHeight;   // D7：行为区与实体区建议同值
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
				else if (v == L"desktop") layer = ECDI::WindowLayer::Desktop;   // Desktop 档已落地（Phase 16）：紧贴桌面窗口正上方，Win+D 后仍可见
			} else if (tok == L"--native"){
				borderless = false;   // 系统标题栏（Normal——零回归对照）
			} else if (tok == L"--no-tray"){
				trayIcon = false;
			} else if (tok == L"--no-drop"){
				fileDrop = false;
			} else if (tok == L"--stay"){
				stayAlive = true;
			} else if (tok == L"--hide-on-close"){
				hideOnClose = true;
			}
		}
	}
	ECDI::Widget& root = win.GetRootWidget();
	root.SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true));   // spacing 0 / fillCrossAxis——单子场景无间隙语义
	// ★ Phase 17 A6：留白落在 **page 一级**（见 `ModelProbe.cpp` 的 SetLayout），**不放 root**——原因有二：
	//   ① root 是裸 Widget（**无背景能力**），而 Backend 每帧以 WHITE_BRUSH 清屏
	//      （`GDIBackend.cpp:261` 决策 16「Root 白底是平台语义」）⇒ root 一级 padding 让出的四边会**露出白色**，
	//      深色窗口上形成一圈白框；
	//   ② CaptionBar 也是本布局的子（`--borderless` 时位于上方、SetStretch(0)）⇒ root padding 会**连带把标题栏内缩**。
	//   放 page 一级则相反：page 背景 `#0f1115` 铺满客户区 ⇒ 内容四边各缩 12px，而边缘仍是深色、标题栏保持贴边。
	// ★ Phase 13：自绘标题栏（仅 Borderless——实体区高度与行为区 captionHeight 取同值；D7 不联动）。
	// ⚠️ 顺序约束：必须在 page 之前 AddChild —— VerticalLayout 按 children 顺序排布竖直次序（先前 = 上方）；
	//    bar 保持 SetStretch(0)（主轴固定高度），page 保持 stretch=1。
	if (borderless){
		auto bar = std::make_unique<ECDI::CaptionBar>(win, kWindowTitle);
		bar->SetSize(680, captionHeight);   // 初始宽度（Arrange 会以真实宽度再次 SetSize）
		root.AddChild(std::move(bar));

		ECDI::Logger::Log(ECDI::LogLevel::Info, L"CaptionBar: attached（自绘标题栏）");
	}

	auto page = std::make_unique<ECDI::Demo::ModelProbePage>();   // 默认 ChildProcess::Create()——真实 probe.exe 管道
	page->SetStretch(1);   // 9.7：主轴(height)+跨轴(width) 均随窗口
	page->SetStyle(ECDI::PanelStyleOverride{ .background = ECDI::Color::FromRGBA8(15, 17, 21, 255) });   // #0f1115 全窗底

	application.SetProbePage(page.get());
	application.SetWindow(&win);         // Phase 14 A7：托盘菜单的运行期窗口操作入口
	application.SetHideOnClose(hideOnClose);
	if (stayAlive){
		application.SetQuitOnLastWindowClosed(false);   // Phase 14 A4 模式 A：最后窗口关闭不退出
		ECDI::Logger::Log(ECDI::LogLevel::Info, L"Phase14: quit-on-last-window-closed disabled (--stay)");
	}
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

	// ── Phase 14 A7：托盘图标（应用级能力——与窗口无关；图标资源 ID 默认 102 = ModelProbe.rc 的 IDI_APP）──
	if (trayIcon){
		application.SetTrayIcon(ECDI::TrayIconOptions{ .tooltip = kWindowTitle });
		ECDI::Logger::Log(ECDI::LogLevel::Info, L"Phase14: tray icon registered (right-click for menu)");
	}

	win.Show();

	// 拖入是**运行期 API**（Show 之后调用才生效——Show 前 = Warning + 忽略，见详设 §2.4 门控）
	if (fileDrop){
		win.SetFileDropEnabled(true);
		ECDI::Logger::Log(ECDI::LogLevel::Info, L"Phase14: file drop enabled (drag files onto the page)");
	}

	return application.Run();
}
