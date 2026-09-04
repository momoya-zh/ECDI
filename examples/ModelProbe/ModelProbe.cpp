// Windows.h 前置 + 宏防护（P2 资源释放用 Win32——FindResource/WriteFile；DrawText 宏不污染 ECDI 头声明；
// min/max 宏已由全文件 (std::min)/(std::max) 括号形式防护，同 main.cpp 先例）
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ModelProbe.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Platform/ExecutablePath.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CheckBox.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Radio.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ECDI{
namespace Demo{

namespace{

// ── ModelProbe 暗色配色（对齐 PySide6 原版 QSS——QSS 观感 90% 方案）──
constexpr Color kText()        noexcept{ return Color::FromRGBA8(230, 233, 239, 255); }  // #e6e9ef
constexpr Color kHint()        noexcept{ return Color::FromRGBA8(154, 163, 178, 255); }  // #9aa3b2
constexpr Color kInputBg()     noexcept{ return Color::FromRGBA8(28, 33, 43, 255); }     // #1c212b
constexpr Color kInputBorder() noexcept{ return Color::FromRGBA8(42, 49, 64, 255); }     // #2a3140
constexpr Color kListBg()      noexcept{ return Color::FromRGBA8(22, 26, 33, 255); }     // #161a21
constexpr Color kPrimary()     noexcept{ return Color::FromRGBA8(47, 127, 217, 255); }   // #2f7fd9
constexpr Color kPrimaryHover()noexcept{ return Color::FromRGBA8(79, 156, 247, 255); }   // #4f9cf7
constexpr Color kPrimaryPress()noexcept{ return Color::FromRGBA8(30, 90, 160, 255); }    // 按下深蓝
constexpr Color kSecondary()   noexcept{ return Color::FromRGBA8(35, 41, 54, 255); }     // #232936
constexpr Color kSecondaryHover() noexcept{ return Color::FromRGBA8(44, 52, 68, 255); }  // hover 提亮

constexpr float kRadius = 6.0f;    ///< 常规圆角（QSS 6px）
constexpr float kRowHeight = 28.0f;   ///< 模型行高

/// @brief 模型列表滚动容器（Panel 子类——裁切 + 滚轮改行 Y 偏移；行位置 = i*rowH - offset）
class ModelListPanel : public Panel{
public:
	void SetRows(std::vector<Widget*> rows){
		m_rows = std::move(rows);
		ApplyLayout();
	}

	void SetRowHeight(float h){
		m_rowHeight = h;
		ApplyLayout();
	}

protected:
	void OnMouseWheel(const MouseWheelEvent& event) override{
		// delta > 0 = 上滚（远离用户）→ 内容上移（offset 减）；一滚一行
		const float step = event.GetDelta() > 0 ? -m_rowHeight : m_rowHeight;
		m_offset += step;
		const float total = static_cast<float>(m_rows.size()) * m_rowHeight;
		const float maxOffset = (std::max)(0.0f, total - static_cast<float>(GetHeight()));
		m_offset = (std::clamp)(m_offset, 0.0f, maxOffset);
		ApplyLayout();
		Invalidate();
	}

private:
	void ApplyLayout(){
		for (size_t i = 0; i < m_rows.size(); ++i){
			m_rows[i]->SetPosition(0, static_cast<int>(static_cast<float>(i) * m_rowHeight - m_offset));
		}
	}

	std::vector<Widget*> m_rows;
	float m_rowHeight = kRowHeight;
	float m_offset = 0.0f;
};

}   // namespace

// ── TSV / JSON 工具（Demo 公开——头文件声明，测试直接调用）──

std::vector<std::string> SplitTsv(const std::string& line){
	std::vector<std::string> fields;
	size_t start = 0;
	while (true){
		const size_t sep = line.find('\t', start);
		if (sep == std::string::npos){
			fields.push_back(line.substr(start));
			break;
		}
		fields.push_back(line.substr(start, sep - start));
		start = sep + 1;
	}
	return fields;
}

std::string EscapeJson(const std::string& s){
	std::string out;
	out.reserve(s.size() + 4);
	for (char c : s){
		if (c == '"') out += "\\\"";
		else if (c == '\\') out += "\\\\";
		else out += c;
	}
	return out;
}

// ── 后端释放（P2：RCDATA 资源嵌入 + 检测复用——release 单 exe 分发）──

namespace{

/// @brief RCDATA 资源 ID（与 ModelProbe.rc `IDR_PROBE_BIN` 对齐——改一侧必须同步另一侧）
constexpr int kProbeResourceId = 101;

/// @brief 从当前模块 RCDATA 资源取字节（FindResourceW/LoadResource/LockResource；nullptr 模块 = exe 自身）
bool LoadEmbeddedProbe(std::vector<BYTE>& out){
	HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(kProbeResourceId), RT_RCDATA);
	if (hRes == nullptr)
		return false;
	const HGLOBAL hGlob = LoadResource(nullptr, hRes);
	if (hGlob == nullptr)
		return false;
	const void* data = LockResource(hGlob);
	if (data == nullptr)
		return false;
	const DWORD size = SizeofResource(nullptr, hRes);
	if (size == 0)
		return false;
	out.assign(static_cast<const BYTE*>(data), static_cast<const BYTE*>(data) + size);
	return true;
}

}   // namespace

bool EnsureBackendExtracted(){
	// 固定路径固定名（用户决策）：<exe_dir>\networkbackend\probe.exe——检测存在就复用，不删除
	const std::string dir  = Platform::GetExecutableDirectory() + "\\networkbackend";
	const std::string path = dir + "\\probe.exe";
	const std::wstring wDir  = UTF8ToWide(dir);
	const std::wstring wPath = UTF8ToWide(path);

	// 资源字节（先取——大小校验与释放共用；资源缺失直接失败）
	std::vector<BYTE> embedded;
	if (!LoadEmbeddedProbe(embedded))
		return false;

	// ① 已存在 → 大小校验（半截/陈旧文件自动重写——版本陈旧的轻量兜底）
	if (GetFileAttributesW(wPath.c_str()) != INVALID_FILE_ATTRIBUTES){
		HANDLE h = CreateFileW(wPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE){
			LARGE_INTEGER size{};
			GetFileSizeEx(h, &size);
			CloseHandle(h);
			if (static_cast<unsigned long long>(size.QuadPart) == embedded.size())
				return true;   // 复用——零释放（KSN 信誉积累）
		}
		// 大小不等 → 落入释放路径覆盖
	}

	// ② 释放（tmp + rename 原子性——防提取到一半被杀留下半截）
	CreateDirectoryW(wDir.c_str(), nullptr);   // 已存在则失败可忽略
	const std::wstring tmp = wPath + L".tmp";
	HANDLE hf = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hf == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	const BOOL ok = WriteFile(hf, embedded.data(), static_cast<DWORD>(embedded.size()), &written, nullptr);
	CloseHandle(hf);
	if (!ok || written != embedded.size())
		return false;
	return MoveFileExW(tmp.c_str(), wPath.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

ModelProbePage::ModelProbePage(std::unique_ptr<ChildProcess> process)
	: m_process(std::move(process)){

	SetLayout(std::make_unique<VerticalLayout>(10, true));   // 9.7：spacing=10 替代全部 MakeSpacer；fillCrossAxis 替代手写宽 600

	// ── 标题 ──
	auto title = std::make_unique<Label>("模型探测工具");
	title->SetSize(600, 30);
	title->SetFont(Font{ .size = 18.0f });
	title->SetTextColor(kText());
	AddChild(std::move(title));

	auto subtitle = std::make_unique<Label>("输入 BaseURL 与 API Key，查看该 key 可调用的模型，勾选后生成 JSON 配置");
	subtitle->SetSize(600, 20);
	subtitle->SetTextColor(kHint());
	AddChild(std::move(subtitle));

	// ── BaseURL ──
	auto baseLabel = std::make_unique<Label>("BaseURL");
	baseLabel->SetSize(600, 20);
	baseLabel->SetTextColor(kText());
	AddChild(std::move(baseLabel));

	auto baseBox = std::make_unique<TextBox>("https://api.longcat.chat/openai/v1");
	baseBox->SetSize(600, 32);
	baseBox->SetSingleLine(true);   // 单行输入（Enter 不换行——URL 语义）
	baseBox->SetTextColor(kText());
	baseBox->SetStyle(TextBoxStyleOverride{
		.background = kInputBg(),
		.border = kPrimary(),
		.selection = kPrimary(),
		.padding = 8.0f,   // 单行垂直居中（Zcode 选项 B：(32−16)/2；四边同源左右亦内缩 8）
		.cornerRadius = kRadius,
		.borderWidth = 1.0f,
		.borderColor = kInputBorder(),
	});
	m_baseBox = baseBox.get();
	AddChild(std::move(baseBox));

	// ── API Key（掩码 + 显示切换）──
	auto keyLabel = std::make_unique<Label>("API Key");
	keyLabel->SetSize(600, 20);
	keyLabel->SetTextColor(kText());
	AddChild(std::move(keyLabel));

	auto keyRow = std::make_unique<Panel>();
	keyRow->SetSize(600, 32);
	keyRow->SetLayout(std::make_unique<HorizontalLayout>());

	auto keyBox = std::make_unique<TextBox>("sk-...");
	keyBox->SetSize(540, 32);
	keyBox->SetStretch(1);   // 9.7：H 布局内拉伸，窗口变宽输入框跟随变宽
	keyBox->SetSingleLine(true);   // 单行输入（Key 语义——Enter 不换行）
	keyBox->SetEchoMode(TextBox::EchoMode::Password);
	keyBox->SetTextColor(kText());
	keyBox->SetStyle(TextBoxStyleOverride{
		.background = kInputBg(),
		.border = kPrimary(),
		.selection = kPrimary(),
		.padding = 8.0f,   // 单行垂直居中（32 高 − 行高 16）/ 2；与右侧 32 高按钮文字中心对齐
		.cornerRadius = kRadius,
		.borderWidth = 1.0f,
		.borderColor = kInputBorder(),
	});
	m_keyBox = keyBox.get();

	auto eyeBtn = std::make_unique<Button>("显示");
	eyeBtn->SetSize(56, 32);
	eyeBtn->SetStyle(ButtonStyleOverride{
		.background = kSecondary(),
		.cornerRadius = kRadius,
		.hoverBackground = kSecondaryHover(),
	});
	eyeBtn->SetTextColor(kText());
	m_eyeBtn = eyeBtn.get();
	eyeBtn->SetOnClick([this]{
		const bool showing = m_keyBox->GetEchoMode() == TextBox::EchoMode::Normal;
		m_keyBox->SetEchoMode(showing ? TextBox::EchoMode::Password : TextBox::EchoMode::Normal);
		m_eyeBtn->SetText(showing ? "显示" : "隐藏");
	});

	keyRow->AddChild(std::move(keyBox));
	keyRow->AddChild(std::move(eyeBtn));
	AddChild(std::move(keyRow));

	// ── 查询 / 测试按钮行 ──
	auto btnRow = std::make_unique<Panel>();
	btnRow->SetSize(600, 40);
	btnRow->SetLayout(std::make_unique<HorizontalLayout>());

	auto queryBtn = std::make_unique<Button>("查询模型");
	queryBtn->SetSize(150, 40);
	queryBtn->SetStyle(ButtonStyleOverride{
		.background = kPrimary(),
		.cornerRadius = kRadius,
		.pressedBackground = kPrimaryPress(),
		.hoverBackground = kPrimaryHover(),
	});
	queryBtn->SetTextColor(Color::White());
	m_queryBtn = queryBtn.get();
	queryBtn->SetOnClick([this]{ OnQueryClick(); });

	auto testBtn = std::make_unique<Button>("测试所选");
	testBtn->SetSize(150, 40);
	testBtn->SetStyle(ButtonStyleOverride{
		.background = kSecondary(),
		.cornerRadius = kRadius,
		.hoverBackground = kSecondaryHover(),
	});
	testBtn->SetTextColor(kText());
	m_testBtn = testBtn.get();
	testBtn->SetOnClick([this]{ OnTestClick(); });

	btnRow->AddChild(std::move(queryBtn));
	btnRow->AddChild(std::move(testBtn));
	AddChild(std::move(btnRow));

	// ── 统计 + 搜索 + 全选/清空 ──
	auto statRow = std::make_unique<Panel>();
	statRow->SetSize(600, 28);
	// 9.8：fillCrossAxis 拉高 statLabel 与兄弟一致（§3.5 条 2——statLabel AutoSize 高度 16 被跨轴填充覆盖，宽度保留随内容）
	statRow->SetLayout(std::make_unique<HorizontalLayout>(0, true));
	m_statRow = statRow.get();   // 9.8：RefreshStatText Arrange 目标（AddChild 前抓取——树地址稳定先例）

	auto statLabel = std::make_unique<Label>("共 0 个模型 · 已选 0");
	statLabel->SetSize(180, 28);
	statLabel->SetTextColor(kHint());
	m_statLabel = statLabel.get();

	auto searchBox = std::make_unique<TextBox>("搜索模型…");
	searchBox->SetSize(220, 28);
	searchBox->SetTextColor(kText());
	searchBox->SetStyle(TextBoxStyleOverride{
		.background = kInputBg(),
		.border = kPrimary(),
		.selection = kPrimary(),
		.padding = 6.0f,   // 单行垂直居中（28 高 − 行高 16）/ 2
		.cornerRadius = kRadius,
		.borderWidth = 1.0f,
		.borderColor = kInputBorder(),
	});
	m_searchBox = searchBox.get();
	searchBox->SetOnTextChanged([this](const std::string& keyword){
		std::string kw = keyword;
		// 行可见性过滤（大小写不敏感子串——原版 QListWidget 语义）
		std::transform(kw.begin(), kw.end(), kw.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		for (size_t i = 0; i < m_models.size() && i < m_rows.size(); ++i){
			std::string id = m_models[i].id;
			std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
			m_rows[i].panel->SetVisible(kw.empty() || id.find(kw) != std::string::npos);
		}
	});

	auto allBtn = std::make_unique<Button>("全选");
	allBtn->SetSize(70, 28);
	allBtn->SetStyle(ButtonStyleOverride{
		.background = kSecondary(),
		.cornerRadius = kRadius,
		.hoverBackground = kSecondaryHover(),
	});
	allBtn->SetTextColor(kText());
	allBtn->SetOnClick([this]{
		for (auto& row : m_rows){
			if (row.panel->IsVisible())
				row.cb->SetChecked(true);   // 触发回调 → 统计更新
		}
	});

	auto noneBtn = std::make_unique<Button>("清空");
	noneBtn->SetSize(70, 28);
	noneBtn->SetStyle(ButtonStyleOverride{
		.background = kSecondary(),
		.cornerRadius = kRadius,
		.hoverBackground = kSecondaryHover(),
	});
	noneBtn->SetTextColor(kText());
	noneBtn->SetOnClick([this]{
		for (auto& row : m_rows)
			row.cb->SetChecked(false);
	});

	statRow->AddChild(std::move(statLabel));
	statRow->AddChild(std::move(searchBox));
	statRow->AddChild(std::move(allBtn));
	statRow->AddChild(std::move(noneBtn));
	AddChild(std::move(statRow));

	// ── 模型列表（滚动容器——裁切 + 滚轮）──
	auto list = std::make_unique<ModelListPanel>();
	list->SetSize(600, 200);
	list->SetStretch(1);   // 9.7：模型列表垂直拉伸，吃剩余空间
	list->SetStyle(PanelStyleOverride{
		.background = kListBg(),
		.cornerRadius = 8.0f,
		.borderWidth = 1.0f,
		.borderColor = kInputBorder(),
	});
	m_list = list.get();
	AddChild(std::move(list));

	// ── 导出格式 ──
	auto fmtRow = std::make_unique<Panel>();
	fmtRow->SetSize(600, 28);
	fmtRow->SetLayout(std::make_unique<HorizontalLayout>());

	auto fmtLabel = std::make_unique<Label>("导出格式");
	fmtLabel->SetSize(80, 28);
	fmtLabel->SetTextColor(kText());

	auto fmtIds = std::make_unique<Radio>("仅 ID 数组");
	fmtIds->SetSize(140, 28);
	fmtIds->SetChecked(true);
	fmtIds->SetTextColor(kText());
	m_fmtIds = fmtIds.get();

	auto fmtFull = std::make_unique<Radio>("对象数组");
	fmtFull->SetSize(140, 28);
	fmtFull->SetTextColor(kText());
	m_fmtFull = fmtFull.get();

	auto fmtCfg = std::make_unique<Radio>("配置格式（含 base_url）");
	fmtCfg->SetSize(220, 28);
	fmtCfg->SetTextColor(kText());
	m_fmtCfg = fmtCfg.get();

	fmtRow->AddChild(std::move(fmtLabel));
	fmtRow->AddChild(std::move(fmtIds));
	fmtRow->AddChild(std::move(fmtFull));
	fmtRow->AddChild(std::move(fmtCfg));
	AddChild(std::move(fmtRow));

	// ── 生成 JSON ──
	// H 布局行包裹（fillCrossAxis 默认 false）——按钮保持固定宽，不被 page V 布局的跨轴填充拉宽（Qt 原版 actRow 同构）
	auto actRow = std::make_unique<Panel>();
	actRow->SetSize(600, 36);
	actRow->SetLayout(std::make_unique<HorizontalLayout>());

	auto genBtn = std::make_unique<Button>("生成 JSON");
	genBtn->SetSize(150, 36);
	genBtn->SetStyle(ButtonStyleOverride{
		.background = kPrimary(),
		.cornerRadius = kRadius,
		.pressedBackground = kPrimaryPress(),
		.hoverBackground = kPrimaryHover(),
	});
	genBtn->SetTextColor(Color::White());
	genBtn->SetOnClick([this]{ GenerateJson(); });
	actRow->AddChild(std::move(genBtn));
	AddChild(std::move(actRow));

	// ── JSON 预览（只读——Ctrl+A/C 复制）──
	auto preview = std::make_unique<TextBox>("勾选模型后点击「生成 JSON」，这里显示可用的 JSON 内容…");
	preview->SetSize(600, 120);
	preview->SetReadOnly(true);
	preview->SetFont(Font{ .size = 13.0f, .family = "Consolas" });
	preview->SetTextColor(kText());
	preview->SetStyle(TextBoxStyleOverride{
		.background = kInputBg(),
		.border = kPrimary(),
		.selection = kPrimary(),
		.padding = 8.0f,   // 多行预览：文本不贴边（左上内缩；行间无 padding 概念）
		.cornerRadius = kRadius,
		.borderWidth = 1.0f,
		.borderColor = kInputBorder(),
	});
	m_previewBox = preview.get();
	AddChild(std::move(preview));
}

int ModelProbePage::GetSelectedCount() const noexcept{
	int count = 0;
	for (const auto& row : m_rows){
		if (row.cb->IsChecked())
			++count;
	}
	return count;
}

const std::string& ModelProbePage::GetPreviewText() const{
	return m_previewBox->GetText();
}

void ModelProbePage::OnQueryClick(){
	if (m_phase != Phase::Idle)
		return;   // busy 防重入（BusyGuard——一个 FETCH 在途时拒绝再发）
	StartFetch();
}

void ModelProbePage::OnTestClick(){
	if (m_phase != Phase::Idle)
		return;
	StartTest();
}

void ModelProbePage::SetBaseUrl(const std::string& url){
	m_baseBox->SetText(url);
}

void ModelProbePage::SetApiKey(const std::string& key){
	m_keyBox->SetText(key);
}

void ModelProbePage::SelectModel(int index, bool checked){
	if (index >= 0 && static_cast<size_t>(index) < m_rows.size()){
		m_rows[static_cast<size_t>(index)].cb->SetChecked(checked);
		UpdateStat();
	}
}

void ModelProbePage::StartFetch(){
	const std::string base = m_baseBox->GetText();
	const std::string key = m_keyBox->GetText();
	if (base.empty() || key.empty() || key == "sk-..."){
		RefreshStatText("请同时填写 BaseURL 和 API Key");
		return;
	}
	if (!m_process->IsRunning()){
		m_process->Start(Platform::GetExecutableDirectory() + "\\networkbackend\\probe.exe");
	}
	if (!m_process->WriteLine("FETCH " + base + " " + key)){
		RefreshStatText("写入后端失败");
		return;
	}
	m_pending.clear();
	m_phase = Phase::Fetching;
	SetBusy(true);
	if (Window* window = GetWindow()){
		window->GetPlatformWindow().StartTimer(kProbePollTimer, kProbePollIntervalMs);
	}
}

void ModelProbePage::StartTest(){
	if (m_phase != Phase::Idle)
		return;
	if (GetSelectedCount() == 0){
		RefreshStatText("请先勾选要测试的模型");
		return;
	}
	std::string ids;
	for (size_t i = 0; i < m_models.size(); ++i){
		if (m_rows[i].cb->IsChecked())
			ids += " " + m_models[i].id;
	}
	if (!m_process->WriteLine("TEST " + m_baseBox->GetText() + " " + m_keyBox->GetText() + ids)){
		RefreshStatText("写入后端失败");
		return;
	}
	m_pending.clear();
	m_phase = Phase::Testing;
	SetBusy(true);
	m_previewBox->SetText("");   // 测试结果写入预览
	if (Window* window = GetWindow()){
		window->GetPlatformWindow().StartTimer(kProbePollTimer, kProbePollIntervalMs);
	}
}

void ModelProbePage::ShutdownBackend(){
	// 详设 §7.4：① StopTimer → ② CloseInput（stdin EOF = probe.exe 自退）→ ③ Wait/Terminate 兜底 → ④ 关窗（调用方）
	// 幂等：多调无害（StopTimer/CloseInput 均幂等；进程已退时 WaitForExit 立即 true）
	if (Window* window = GetWindow()){
		window->GetPlatformWindow().StopTimer(kProbePollTimer);
	}
	m_process->CloseInput();
	if (!m_process->WaitForExit(1000)){
		m_process->Terminate();   // 兜底强杀（防孤儿进程残留）
	}
	m_phase = Phase::Idle;
}

void ModelProbePage::PollProbe(){
	if (m_phase == Phase::Idle)
		return;   // 无在途任务——忽略轮询（防御）
	m_pending += m_process->ReadAvailable();
	// 取完整行分发（管道读取边界 ≠ 协议消息边界——半行留缓冲）
	size_t newline = 0;
	while ((newline = m_pending.find('\n')) != std::string::npos){
		const std::string line = m_pending.substr(0, newline);
		m_pending.erase(0, newline + 1);
		if (line.empty())
			continue;
		HandleLine(SplitTsv(line));
		if (m_phase == Phase::Idle)
			return;   // 已收尾——停止处理（防 DONE 后残留行误入下一任务）
	}
	// 进程意外退出（未发 DONE）→ 超时兜底：标记失败
	if (!m_process->IsRunning() && m_phase != Phase::Idle){
		Finish(false, "后端进程意外退出（未收到 DONE）");
	}
}

void ModelProbePage::HandleLine(const std::vector<std::string>& fields){
	if (fields.empty())
		return;
	const std::string& tag = fields[0];
	if (tag == "OK" && fields.size() >= 2 && fields[1] == "FETCH"){
		// OK\tFETCH\t<base>\t<count>——后续 MODEL 行逐条到达
		return;
	}
	if (tag == "MODEL" && fields.size() >= 2){
		m_models.push_back(ModelInfo{ fields[1], fields.size() >= 3 ? fields[2] : "" });
		return;
	}
	if (tag == "DONE" && fields.size() >= 2 && fields[1] == "FETCH"){
		RebuildRows();
		UpdateStat();
		Finish(true, "成功获取 " + std::to_string(m_models.size()) + " 个模型");
		return;
	}
	if (tag == "OK" && fields.size() >= 2 && fields[1] == "TEST"){
		return;   // OK\tTEST\t<count>——TEST 行逐条到达
	}
	if (tag == "TEST" && fields.size() >= 4){
		// TEST\t<id>\tOK|FAIL\t<message>
		std::string line = "[";
		line += fields[2];
		line += "] ";
		line += fields[1];
		line += " — ";
		line += fields[3];
		std::string text = m_previewBox->GetText();
		if (!text.empty()) text += "\n";
		text += line;
		m_previewBox->SetText(text);
		return;
	}
	if (tag == "DONE" && fields.size() >= 2 && fields[1] == "TEST"){
		Finish(true, "测试完成");
		return;
	}
	if (tag == "ERR" && fields.size() >= 3){
		Finish(false, fields[2]);
		return;
	}
	// 未知行——忽略（协议演进容忍）
}

void ModelProbePage::Finish(bool ok, const std::string& message){
	if (Window* window = GetWindow()){
		window->GetPlatformWindow().StopTimer(kProbePollTimer);
	}
	m_phase = Phase::Idle;
	SetBusy(false);
	RefreshStatText(message);
}

void ModelProbePage::RefreshStatText(const std::string& message){
	// 9.8 状态消息统一出口（详设 §6 顺序硬要求）：① SetText → ② AutoSize（宽度随内容——
	// 后调用者赢覆盖构造期 180 定宽）→ ③ statRow Arrange（statLabel 变宽后 searchBox/allBtn/noneBtn 随动）→ ④ Invalidate
	m_statLabel->SetText(message);
	m_statLabel->AutoSize();
	m_statRow->Arrange();
	Invalidate();
}

void ModelProbePage::SetBusy(bool busy){
	// busy 期禁用查询/测试按钮（功能禁 + 视觉灰化近似——SetEnabled 已有功能语义）
	m_queryBtn->SetEnabled(!busy);
	m_testBtn->SetEnabled(!busy);
	Invalidate();
}

void ModelProbePage::RebuildRows(){
	// 行池复用（避免反复 AddChild 抖动树）：旧行隐藏 + 复用/新建；行序 = model 序（回调 idx 稳定）
	for (auto& row : m_rows){
		row.panel->SetVisible(false);
	}
	std::vector<Widget*> rows;
	rows.reserve(m_models.size());
	for (size_t i = 0; i < m_models.size(); ++i){
		RowWidgets row;
		if (i < m_rows.size()){
			// 复用既有行（更新文本 + 显示）
			row = m_rows[i];
			row.idLabel->SetText(m_models[i].id);
			row.metaLabel->SetText(m_models[i].meta);
			row.panel->SetVisible(true);
		}
		else{
			// 新建行：cb + idLabel + metaLabel（绝对定位；行高固定）
			row.panel = new Panel();
			row.panel->SetSize(600, static_cast<int>(kRowHeight));
			auto cb = std::make_unique<CheckBox>();
			cb->SetSize(28, static_cast<int>(kRowHeight));
			cb->SetTextColor(kText());
			row.cb = cb.get();
			auto idLabel = std::make_unique<Label>(m_models[i].id);
			idLabel->SetPosition(32, 0);
			idLabel->SetSize(380, static_cast<int>(kRowHeight));
			idLabel->SetFont(Font{ .family = "Consolas" });
			idLabel->SetTextColor(kText());
			row.idLabel = idLabel.get();
			auto metaLabel = std::make_unique<Label>(m_models[i].meta);
			metaLabel->SetPosition(420, 0);
			metaLabel->SetSize(170, static_cast<int>(kRowHeight));
			metaLabel->SetTextColor(kHint());
			row.metaLabel = metaLabel.get();
			row.panel->AddChild(std::move(cb));
			row.panel->AddChild(std::move(idLabel));
			row.panel->AddChild(std::move(metaLabel));
			m_list->AddChild(std::unique_ptr<Widget>(row.panel));   // 所有权入树（unique_ptr<Panel> → Widget 移动）
			m_rows.push_back(row);
		}
		// 新查询 → 全不选；回调绑定（idx 稳定 = model 序）
		row.cb->SetChecked(false);
		const size_t idx = i;
		row.cb->SetOnCheckedChanged([this, idx](bool){ UpdateStat(); });
		rows.push_back(row.panel);
	}
	static_cast<ModelListPanel*>(m_list)->SetRows(std::move(rows));
}

void ModelProbePage::UpdateStat(){
	RefreshStatText("共 " + std::to_string(m_models.size()) + " 个模型 · 已选 " + std::to_string(GetSelectedCount()));
}

void ModelProbePage::GenerateJson(){
	if (GetSelectedCount() == 0){
		RefreshStatText("请先勾选要导出的模型");
		return;
	}
	std::string json;
	if (m_fmtIds->IsChecked()){
		// 仅 ID 数组（pretty：每项一行——preview 多行显示，免横向滚动）
		json = "[\n";
		bool first = true;
		for (size_t i = 0; i < m_models.size(); ++i){
			if (!m_rows[i].cb->IsChecked()) continue;
			if (!first) json += ",\n";
			json += "  \"" + EscapeJson(m_models[i].id) + "\"";
			first = false;
		}
		json += "\n]";
	}
	else if (m_fmtFull->IsChecked()){
		// 对象数组（TSV 提供 id + meta；pretty：每对象两字段各一行）
		json = "[\n";
		bool first = true;
		for (size_t i = 0; i < m_models.size(); ++i){
			if (!m_rows[i].cb->IsChecked()) continue;
			if (!first) json += ",\n";
			json += "  {\n    \"id\": \"" + EscapeJson(m_models[i].id)
			     + "\",\n    \"meta\": \"" + EscapeJson(m_models[i].meta) + "\"\n  }";
			first = false;
		}
		json += "\n]";
	}
	else{
		// 配置格式（含 base_url；pretty：字段各一行，models 数组缩进）
		json = "{\n  \"base_url\": \"" + EscapeJson(m_baseBox->GetText()) + "\",\n  \"models\": [\n";
		bool first = true;
		for (size_t i = 0; i < m_models.size(); ++i){
			if (!m_rows[i].cb->IsChecked()) continue;
			if (!first) json += ",\n";
			json += "    \"" + EscapeJson(m_models[i].id) + "\"";
			first = false;
		}
		json += "\n  ]\n}";
	}
	m_previewBox->SetText(json);
	RefreshStatText("JSON 已生成");
}

}
}
