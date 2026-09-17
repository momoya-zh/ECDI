#pragma once

#include "ECDI/Platform/ChildProcess.h"
#include "ECDI/Widget/Panel.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace ECDI{

class TextBox;
class Button;
class Label;
class CheckBox;
class Radio;
class Window;   // 前置声明（Phase 12 实测接缝——非拥有指针）
class DropFilesEvent;   // 前置声明（Phase 14：OnDropFiles override 形参——.cpp 侧含全头）

namespace Demo{

/// @brief TSV 行切分（按 `\t`；ModelProbe 协议自控——无引号语义；demo 工具公开供测试）
std::vector<std::string> SplitTsv(const std::string& line);

/// @brief JSON 字符串转义（`"`→`\"`、`\`→`\\`；后端已清洗换行；demo 工具公开供测试）
std::string EscapeJson(const std::string& s);

/// @brief 确保后端 probe.exe 就位（P2 资源嵌入释放——release 单 exe 分发）
/// @details 目标 = `<exe_dir>\networkbackend\probe.exe`（固定路径固定名——KSN 信誉友好，不删除）：
/// ① 存在且大小 == 资源大小 → 复用（零释放）；
/// ② 不存在 / 半截 / 陈旧（大小不等）→ 从 RCDATA 资源（ModelProbe.rc IDR_PROBE_BIN）释放（tmp + rename 原子性）。
/// @return 是否就位（false = 资源缺失/写入失败——调用方记日志）
bool EnsureBackendExtracted();

/// @brief ModelProbe 页面（ModelProbe demo——examples/ModelProbe，不入框架）
/// @details 状态型控件：输入 base/key → 查询 /models 列模型 → 勾选 → 生成 JSON → 测试所选。
/// 后端 = probe.exe 子进程（TSV 行协议 + stdin/stdout 管道，P0 已验证）；
/// 轮询由 demo Application::OnTimer(timerId=100) 直调 PollProbe（详设 §7.3 接线）。
/// 注入接缝：构造收 unique_ptr<ChildProcess>（默认工厂——测试传 fake）。
class ModelProbePage : public Panel{
public:
	/// @brief 轮询 timerId（demo 专用——避开框架保留段 1–15：1=光标 2=动画；main.cpp app OnTimer 引用）
	static constexpr int kProbePollTimer = 100;
	static constexpr unsigned int kProbePollIntervalMs = 50;

	explicit ModelProbePage(std::unique_ptr<ChildProcess> process = ChildProcess::Create());

	/// @brief 轮询后端 stdout（demo app OnTimer 直调；非阻塞——读可用字节 → 行缓冲 → 分发）
	void PollProbe();

	/// @brief 关停后端（关窗清理——详设 §7.4 ①②③④）
	/// @details StopTimer → CloseInput（stdin EOF = probe.exe 自退）→ WaitForExit(1000) 兜底 Terminate；
	/// 幂等（多调无害）；不删除 networkbackend/probe.exe（用户决策——固定路径固定名，KSN 信誉积累）
	void ShutdownBackend();

	// ── 只读查询（测试/状态显示）──

	int GetModelCount() const noexcept { return static_cast<int>(m_models.size()); }
	int GetSelectedCount() const noexcept;
	bool IsBusy() const noexcept { return m_phase != Phase::Idle; }
	const std::string& GetPreviewText() const;   ///< 预览区文本（JSON/测试结果——测试断言用）

	/// @brief 界面查询（demo 内触发——测试经 fake 直接驱动等价路径）
	void OnQueryClick();

	/// @brief 界面测试（对称 public——demo 按钮/测试共用）
	void OnTestClick();

	// ── 窗口控制（Phase 12 实测入口——运行期 API + 状态事件显示）──

	/// @brief 设置目标窗口（**非拥有**指针——B 契约：Window 对象归 Application，本页只触发运行期 API）
	void SetWindow(Window* window) noexcept;

	/// @brief 窗口状态事件显示（DemoApplication::OnWindowStateChanged 转发——winRow 右侧单行显示最近一条）
	void AppendWindowState(const std::string& stateName);

	/// @brief 运行期通知显示（Phase 14 A7 手测观测通道——托盘/拖入事件；与窗口状态共用 winRow 右侧标签）
	void AppendNotice(const std::string& text);

	/// @brief 只写事件日志、不动界面标签（Phase 14 A4/A5 观测补充）
	/// @details 一次操作常产生多行细节（拖入的路径列表、与实时光标坐标的对照），而标签是 SetText
	/// **替换**语义只能留一条 ⇒ 细节走日志、标签留给"最新一条事件"。
	void LogEvent(const std::string& text);

	/// @brief 文件拖入（Phase 14 R9——bubbling 落点观测）
	/// @details 拖到**页面区域**才会到达这里；拖到标题栏条带时 `HitTest` 命中 `CaptionBar`
	/// （root 的兄弟、非本页祖先），bubbling 不跨兄弟 ⇒ 本方法不被调用。默认空实现不感知。
	void OnDropFiles(const DropFilesEvent& event) override;

	// ── 测试/调用驱动 API（demo 控件公开接口——构造界面与测试共用）──

	void SetBaseUrl(const std::string& url);        ///< 设置 base URL 输入框
	void SetApiKey(const std::string& key);         ///< 设置 API Key 输入框
	void SelectModel(int index, bool checked);      ///< 勾选/取消第 index 个模型行

private:
	enum class Phase{ Idle, Fetching, Testing };

	struct ModelInfo{
		std::string id;
		std::string meta;   ///< "owned by X" / 日期 / 空（TSV 第三列原样）
	};

	void SetBusy(bool busy);
	void StartFetch();
	void StartTest();
	void GenerateJson();
	void RebuildRows();
	void UpdateStat();

	/// @brief 事件日志写入（AppendNotice / LogEvent 共用——惰性打开 + 相对毫秒时间戳 + 立即 flush）
	void WriteLog(const std::string& text);

	/// @brief 状态消息统一出口（9.8）：SetText → AutoSize（宽度随内容）→ statRow Arrange（兄弟随动）→ Invalidate
	void RefreshStatText(const std::string& message);

	void HandleLine(const std::vector<std::string>& fields);
	void Finish(bool ok, const std::string& message);

	// 控件（AddChild 前抓取，树内稳定）
	TextBox* m_baseBox = nullptr;
	TextBox* m_keyBox = nullptr;
	TextBox* m_searchBox = nullptr;
	TextBox* m_previewBox = nullptr;
	Button* m_queryBtn = nullptr;
	Button* m_testBtn = nullptr;
	Button* m_eyeBtn = nullptr;
	Label* m_statLabel = nullptr;
	Panel* m_statRow = nullptr;   ///< 状态行容器（9.8 RefreshStatText Arrange 用）
	Panel* m_list = nullptr;
	Radio* m_fmtIds = nullptr;
	Radio* m_fmtFull = nullptr;
	Radio* m_fmtCfg = nullptr;
	Window* m_window = nullptr;           ///< 非拥有——运行期 API 实测接缝（SetWindow 注入）
	Label* m_windowEventLabel = nullptr;  ///< 最近一条窗口状态事件（winRow 右侧）

	/// A3 手测：事件日志（exe 同目录 `modelprobe-events.log`——**追加**语义，带相对毫秒时间戳）
	/// @details 为什么需要：标签是 SetText **替换**语义，只留最新一条，中间态与到达顺序全丢；
	/// 而"一次操作到底来几个事件、什么顺序"恰恰是托盘判据（2026-09-17 实测反复受困的一点）。
	/// 惰性打开（首次 AppendNotice），打不开则静默跳过——观测设施不阻塞主流程。
	std::ofstream m_eventLog;
	unsigned long long m_logStartTick = 0;   ///< 日志起点（相对时间戳用；不可用 Win32 类型——本头不引 Windows.h）

	// ── 模型行池（复用——避免反复 AddChild 抖动树；idx 稳定 = model 序）──
	struct RowWidgets{
		Panel* panel = nullptr;
		CheckBox* cb = nullptr;
		Label* idLabel = nullptr;
		Label* metaLabel = nullptr;
	};
	std::vector<RowWidgets> m_rows;

	std::vector<ModelInfo> m_models;
	std::unique_ptr<ChildProcess> m_process;
	std::string m_pending;     ///< stdout 行缓冲（半行累计——管道读取边界 ≠ 协议消息边界）
	Phase m_phase = Phase::Idle;
};

}
}
