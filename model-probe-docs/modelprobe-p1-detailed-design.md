# ModelProbe P1 详细设计（v1.1）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-01（v1.1 修订 2026-09-01）
> 状态：待评审（用户 / GPT）——v1.0 已过 GPT 评审（架构/拆分/测试/YAGNI 全过），v1.1 吸收其 7 条意见
> 前置：初步设计 v1.3（用户 2026-09-01 确认「差不多不缺东西了」）
> 文档目录：本 demo 文档独立存 `model-probe-docs/`，不与框架 `docs/`（phaseN-*）混放
> v1.1：吸收 GPT 评审（2026-09-01，逐条已回应）——① ChildProcess 父侧句柄 `SetHandleInformation`（**P0 硬问题**：防子进程继承父写端 → CloseInput 永收不到 EOF）② Start 改 `(executable, args vector)` 接口边界（引号转义归 Win32 实现）③ 描边环几何公式冻结（outer/inner/radius + 退化保护）④ 新增分包测试 `FetchFragmentedOutput` ⑤ 关窗清理顺序写死（StopTimer→CloseInput→Wait/Terminate→关窗）⑥ hover 配色陷阱标注（改 background 必须同设 hoverBackground）⑦ 7 能力最小实现边界声明

---

## 1. 概述

本版把初步设计 v1.3 的 7 项框架能力（ChildProcess / GetExecutableDirectory / TextBox echo / TextBox 只读 / TextBox 形态 / Button hover / Panel 形态）+ demo 组装（ModelProbePage）落成**可实现的精确规格**：精确签名、实现要点、命令流变化、门禁清单、测试用例。所有引用均已对照源码核实（2026-09-01）。

## 2. Platform 层能力

### 2.1 ChildProcess 接口（ECDI/Platform/ChildProcess.h）

```cpp
#include <string>
#include <vector>

namespace ECDI{

/// @brief 子进程 + 匿名管道（stdin/stdout）抽象——demo/测试零 Win32 类型
/// @details 生命周期：Start → WriteLine×N → (ReadAvailable 轮询)×N → CloseInput(EOF) → WaitForExit / Terminate
/// stdin EOF = 子进程退出信号（协议约定——probe.exe 读到 EOF 自退）
class ChildProcess{
public:
	virtual ~ChildProcess() = default;

	/// @brief 工厂——返回平台实现（Win32ChildProcess）；测试可注入 fake
	static std::unique_ptr<ChildProcess> Create();

	/// @brief 启动（UTF-8 可执行路径 + 参数列表）+ 建立匿名管道；失败返回 false（原因查日志）
	/// @details 参数以 vector 传入（**非完整命令行**）——引号/空格转义是 Win32 实现职责，
	/// 接口不暴露 Windows command line quoting（GPT 评审 v1.0 边界冻结）；P1 传空 args
	virtual bool Start(const std::string& executablePath, const std::vector<std::string>& args = {}) = 0;

	/// @brief 存活查询（非阻塞）
	virtual bool IsRunning() const = 0;

	/// @brief 写一行（UTF-8 字节 + 自动补 \n；协议约束：单行 < 4KB——管道缓冲内，不阻塞）
	virtual bool WriteLine(const std::string& line) = 0;

	/// @brief 非阻塞读 stdout 当前全部可用字节（无数据返回空串；UTF-8 文本）
	virtual std::string ReadAvailable() = 0;

	/// @brief 关 stdin（幂等）→ 子进程读 EOF 自退
	virtual void CloseInput() = 0;

	/// @brief 等退出（超时返回 false）
	virtual bool WaitForExit(unsigned int timeoutMs) = 0;

	/// @brief 兜底强杀
	virtual void Terminate() = 0;
};

}
```

**测试接缝**：接口注入——ModelProbePage 构造收 `unique_ptr<ChildProcess>`（默认 `ChildProcess::Create()`），测试传 fake（RecordingBackend 式命令断言）。

### 2.2 Win32ChildProcess 实现（ECDI/src/Platform/Win32/Win32ChildProcess.{h,cpp}）

| 项 | 规格 |
|---|---|
| 成员 | `HANDLE m_hProcess / m_hStdinWrite / m_hStdoutRead`（默认 nullptr） |
| Start | ① `CreatePipe`×2（SECURITY_ATTRIBUTES `bInheritHandle=TRUE`）② **`SetHandleInformation` 清父侧句柄继承**：`hStdinWrite` 与 `hStdoutRead` 的 `HANDLE_FLAG_INHERIT = 0`（**P0 硬问题**——否则子进程会意外继承父写端：父 `CloseInput()` 后子仍持写端 → **永远收不到 EOF**，`WaitForExit` 卡死）③ `CreateProcessW`（cmdline = 引号包裹的 UTF8ToWide 路径 + 逐参引号包裹（内嵌 `"` 转义为 `\"`——quoting 是 Win32 实现职责）；`CREATE_NO_WINDOW` **必设**——否则 GUI 弹控制台窗；`bInheritHandles=TRUE`）④ `STARTUPINFO{ cb, hStdInput=读端, hStdOutput=写端, hStdError=写端(合并 stderr→stdout——后端崩溃输出可排查), dwFlags=STARTF_USESTDHANDLES }` ⑤ 关闭父侧子端拷贝（pi.hThread / stdin 读端 / stdout 写端）⑥ 失败逐句柄清理 + 返回 false |

> 句柄继承矩阵（GPT 评审冻结）：stdin 管道——子 Read 可继承 / 父 Write **不可继承**；stdout 管道——子 Write 可继承 / 父 Read **不可继承**。父侧两端清 `HANDLE_FLAG_INHERIT` 后，`CreateProcess(bInheritHandles=TRUE)` 只会让子进程拿到它真正需要的 2 个句柄。
| WriteLine | `line + "\n"` → `WriteFile(m_hStdinWrite)`（UTF-8 原字节直写） |
| ReadAvailable | `PeekNamedPipe` 查可用字节 → 0 返回空串；否则 `ReadFile` 读全量 → std::string |
| CloseInput | `CloseHandle(m_hStdinWrite)` + 置 nullptr（幂等） |
| WaitForExit | `WaitForSingleObject(m_hProcess, ms) == WAIT_OBJECT_0` |
| Terminate | `TerminateProcess(m_hProcess, 1)` |
| IsRunning | `WaitForSingleObject(m_hProcess, 0) == WAIT_TIMEOUT` |
| 析构 | 若仍在运行 → Terminate + 全部 CloseHandle（安全网；正常路径 demo 先 CloseInput+Wait） |

**编码**：路径/文本 UTF-8 → `UTF8ToWide`（Core/String.h 既有）。

**P1 边界（GPT 评审确认）**：可控的管道式子进程，**非完整进程管理器**——不做 ReadAsync/WriteAsync/事件循环/环境变量/工作目录/进程树/信号/PTY（YAGNI）；未来需求出现再按需扩展。

### 2.3 GetExecutableDirectory（ECDI/Platform/ExecutablePath.h + src/Platform/Win32/ExecutablePath.cpp）

```cpp
namespace ECDI::Platform{ std::string GetExecutableDirectory(); }
```

实现：`GetModuleFileNameW(NULL, buf, MAX_PATH)` → 截到最后一个 `\`（去文件名段）→ `WideToUTF8`。用途：P1 定位 `<exe_dir>/networkbackend/probe.exe`；P2 释放复用。薄封装——仅集成验证（无单元测试）。

## 3. TextBox 三能力（ECDI/include/ECDI/Widget/TextBox.h + src/Widget/TextBox.cpp）

### 3.1 EchoMode（密码掩码）

```cpp
enum class EchoMode{ Normal, Password };
void SetEchoMode(EchoMode mode);   EchoMode GetEchoMode() const;   // 默认 Normal（零回归）
```

**机制：显示串与真实串码点 1:1，仅字节不同**（每码点 → U+2022 `•`，UTF-8 3 字节）。

- 私有辅助 `std::string BuildDisplayText() const`：码点级遍历 `m_text`——非组合区间每码点替换为 `EncodeUTF8(U'\u2022')`（Core/UTF8.h 既有）；**组合区间 `[m_compositionStart, m_compositionStart+m_compositionLength)` 保留原文**（IME 组合期显示原文，P1 初设决策）
- **OnPaint 全部文本操作换用 display 串**（TextBox.cpp:964-1043 段落）：
  - 992 行 lineText → `display.substr(CodepointIndexToByteOffset(display, startCp), ...)`（字节偏移须从 display 算）
  - 1004-1005 行 Selection 前缀测量 → display（高亮与 `•` 对齐）
  - 1015 行 `ctx.DrawText` → display 行文本
  - 1028-1029 行组合下划线测量 → display（组合区间 = 原文，等价）
- **光标/点击测量同源换 display**：`CalculateCaretPosition` / `CaretIndexFromPosition` / `CaretIndexFromLineX` / `GetCaretClientGeometry` 内部用 display 串测量——码点索引 1:1，映射安全；光标/IME 锚点对齐 `•`
- 编辑/GetText：**真实 m_text 不变**（`GetText()` 返回真实值——FETCH 命令要用）
- 性能：O(n) 每帧构建——demo 输入短，可接受；不做缓存（YAGNI）

### 3.2 ReadOnly（只读）

```cpp
void SetReadOnly(bool ro);   bool IsReadOnly() const;   // 默认 false（零回归）
```

**门禁清单**（方法首行 `if (m_readOnly) return;`）：

| 方法 | 门禁 |
|---|---|
| `InsertCodepoint / InsertText / DeleteBackward / DeleteForward` | no-op（编辑键/字符输入自然被拦） |
| `Undo / Redo` | no-op |
| `UpdateComposition / CommitComposition` | no-op（只读框不参与 IME；`CancelComposition` 无害放行） |
| 方向键/Home/End/Shift+方向/Ctrl+A/C | **照常**（只读 ≠ 不可选——预览全选复制必需） |
| `SetText`（程序写入） | **照常**（生成 JSON 刷新预览；D7 契约：SetText 不触发回调） |

### 3.3 形态（圆角 + 恒显边框）

`TextBoxStyle` / `TextBoxStyleOverride` 追加（声明序 = 初设 v1.1 既定序后追加）：

```cpp
StyleField<float> cornerRadius;   // 0 = 直角（默认，现状不变）
StyleField<float> borderWidth;    // 0 = 无恒显边框（默认）；>0 = 1px 描边环
StyleField<Color> borderColor;    // 恒显边框色（默认透明）
```

> ⚠️ 语义说明：`borderWidth` 曾在 9.6 收尾删除（旧「焦点内缩」语义随方案 B 移除）；本次**新增的是「恒显描边」语义**，与 `border`（焦点点线框色）并存不冲突——遵守「StyleField 存了必须被消费」纪律。

**OnPaint 命令流（TextBox.cpp:949-962 背景段）**：

```
if (borderWidth > 0 && borderColor.a > 0):
    ① DrawRoundedRect(全尺寸, cornerRadius, borderColor)     ← 外层环色
    ② DrawRoundedRect(内缩 borderWidth, max(0, cornerRadius-borderWidth), background)  ← 背景
    双矩形描边环（无需新 RenderCommand；Phase 8 能力现成）
else:
    cornerRadius > 0 ? DrawRoundedRect(背景) : DrawRect(背景)   ← 现状扩展
```

焦点框：`DrawFocusRect(..., 0.0f, ...)` → radius 参数改 `cornerRadius`（圆角输入框焦点框贴圆角——Button 9.5 R4 先例）。

**几何公式（GPT 评审冻结——防圆角边缘覆盖）**：

```
outer       = bounds                        // 外层矩形 = 控件全尺寸
inner       = bounds.inset(borderWidth)     // 内层矩形 = 四边各内缩 borderWidth
outerRadius = cornerRadius
innerRadius = max(0, cornerRadius − borderWidth)
内层宽/高 ≤ 0 → 跳过内层绘制（退化保护）
双矩形仅在 background.a > 0 成立（透明背景挂账——见限制）
```

**限制（记录）**：双矩形描边要求**背景不透明**（a>0）——背景 a==0 时内层 no-op，外层实色填满（无法形成环）。demo 输入框全实色，可接受；透明背景 + 恒显边框 = 不支持场景（挂账）。

### 3.4 三能力互斥关系

| 组合 | 行为 |
|---|---|
| echo × 只读 | 兼容——Key 框可编辑、预览框只读且不掩码（互不干扰，独立开关） |
| echo × IME | 组合期显示原文、提交后掩码（3.1 既定） |
| 只读 × IME | 组合直接拒绝（3.2 门禁） |

## 4. Button hover（ECDI/include/ECDI/Theme/ButtonStyle.h + Widget/Button.{h,cpp}）

`ButtonStyle` / `ButtonStyleOverride` 末尾追加：

```cpp
StyleField<Color> hoverBackground;   // hover 背景色；默认 = background（无 override 零视觉变化）
```

Button 变更：

```cpp
bool m_hovered = false;                              // 新成员
void OnMouseEnter() override;                        // m_hovered=true → AnimateBackgroundTo(GetTargetBackground())
void OnMouseLeave() override;                        // m_hovered=false → 同上
// GetTargetBackground() 三态化（Button.cpp:156-162）：
//   m_pressed ? pressedBackground : (m_hovered ? hoverBackground : background)
```

- 优先级：**按下 > hover**（QSS `:active` 覆盖 `:hover` 同语义）
- **配色陷阱（GPT 评审提示）**：`hoverBackground` 框架默认 = 主题 background——demo 若只 `SetStyle(.background)` 不设 `.hoverBackground`，hover 会回落到主题默认蓝 (80,120,220)；**demo 改底色时必须同设 hoverBackground**（实配：normal `#2f7fd9` → hover `#4f9cf7`）
- 过渡复用 9.6 S1 管道（`AnimateBackgroundTo`——200ms EaseOut；无窗口测试树即时到位，Button.cpp:173-180 既有分支）
- 按下态：**已消费**（pressedBackground → S1，核实于 Button.cpp:158-159）——P1 只补 hover，按下态只需 demo 配色

## 5. Panel 形态（ECDI/include/ECDI/Theme/PanelStyle.h + Widget/Panel.{h,cpp}）

`PanelStyle` / `PanelStyleOverride` 追加（同 TextBox 语义）：

```cpp
StyleField<float> cornerRadius;   // 0 = 直角（默认）
StyleField<float> borderWidth;    // 0 = 无边框（默认）
StyleField<Color> borderColor;    // 边框色（默认透明）
```

`Panel::OnPaint` 对齐 Button 结构（radius>0 → DrawRoundedRect；borderWidth>0 → 双矩形描边环）。默认全 0/透明 → **透明容器语义不变**（背景 a==0 既有短路 + 边框默认 0 不画）。限制同 3.3（透明背景不配边框）。

## 6. DefaultTheme 默认值变更（src/Theme/DefaultTheme.cpp）

| 方法 | 新增默认 | 说明 |
|---|---|---|
| `GetButtonStyle` | `hoverBackground = (80,120,220)`（= background） | 无 override 行为不变（零回归） |
| `GetTextBoxStyle` | `cornerRadius=0` `borderWidth=0` `borderColor=透明` | 现状不变（零回归） |
| `GetPanelStyle` | `cornerRadius=0` `borderWidth=0` `borderColor=透明` | 透明容器语义不变 |

## 7. Demo：ModelProbePage（ECDI/src/Demo/ModelProbe.{h,cpp}）

### 7.1 类结构

```cpp
namespace ECDI::Demo{

/// @brief 模型列表滚动容器（demo 局部类——列表区滚轮 + 行重排）
class ModelListPanel : public Panel{
public:
	void SetRows(std::vector<Widget*>& rows);          // 行控件（树内稳定）
	void SetRowHeight(float h);
protected:
	void OnMouseWheel(const MouseWheelEvent&) override;  // offset += delta 方向量 → clamp → 行 SetPosition 重排 + Invalidate
private:
	float m_offset = 0.0f; float m_rowHeight = 28.0f; std::vector<Widget*> m_rows;
};

class ModelProbePage : public Panel{
public:
	explicit ModelProbePage(std::unique_ptr<ChildProcess> process = ChildProcess::Create());  // 注入接缝
	void PollProbe();    // Application::OnTimer(timerId==kProbePollTimer) 直调（demo app 接线）
	// 状态数据
	struct ModelInfo{ std::string id; std::string meta; long long created = 0; std::string ownedBy; };
private:
	enum class Phase{ Idle, Fetching, Testing };
	Phase m_phase = Phase::Idle;
	std::unique_ptr<ChildProcess> m_process;
	std::string m_pending;                    // stdout 行缓冲（半行累计）
	std::vector<ModelInfo> m_models;          // JSON 导出数据源
	std::vector<std::unique_ptr<Panel>> m_rowPanels;  // 行容器（CheckBox + 2 Label）
	// 控件指针（AddChild 前抓取，树内稳定）：baseBox/keyBox/searchBox/previewBox/queryBtn/testBtn/eyeBtn/statLabel/list...
};

}
```

### 7.2 布局树（纵向，暗色，QSS 90%）

```
ModelProbePage
├─ Label 标题「模型探测工具」(18px 白) + 副标题（灰）
├─ Label「BaseURL」+ TextBox base（圆角6 边框1px 深灰底 #1c212b 白字）
├─ Label「API Key」+ TextBox key（EchoMode::Password + 圆角边框）+ Button「显示/隐藏」（toggle SetEchoMode）
├─ Button 行：查询模型（蓝 #2f7fd9 白字 hover #4f9cf7 pressed 深蓝）+ 测试所选（ghost 近似：深灰实底浅字，busy 禁用灰化）
├─ 行：Label 统计「共 N 个模型 · 已选 M」+ TextBox 搜索 + Button 全选/清空
├─ ModelListPanel（圆角8 边框1px 底 #161a21；行 = Panel + CheckBox + id Label(Consolas) + meta Label(灰)，行分隔线自绘 DrawLine）
├─ 导出格式：Radio×3（仅 ID 数组 / 对象数组 / 配置格式）
├─ Button「生成 JSON」
└─ TextBox 预览（Consolas 等宽 + 圆角边框 + **SetReadOnly(true)**）
```

### 7.3 轮询接线（main.cpp——单独授权项）

```cpp
static constexpr int kProbePollTimer = 100;   // demo 专用，避开框架保留段 1–15（1=光标 2=动画）

class DemoApplication : public ECDI::Application{
	void OnTimer(const ECDI::TimerEvent& event) override{
		if (event.GetTimerId() == kProbePollTimer){ m_probePage->PollProbe(); return; }
		ECDI::Application::OnTimer(event);   // 其余 timerId 转发（光标闪烁/动画原路由）
	}
	ECDI::Demo::ModelProbePage* m_probePage = nullptr;
};
// 启动轮询：win.GetPlatformWindow().StartTimer(kProbePollTimer, 50);
// 停止：StopTimer(kProbePollTimer)
```

### 7.4 主流程伪码

**查询（OnQueryClick）**：
```
busy=true（query/test 按钮 SetEnabled(false) + 灰化；stat 行「查询中…」）
if (!m_process->IsRunning()) m_process->Start(GetExecutableDirectory() + "\\networkbackend\\probe.exe")
m_process->WriteLine("FETCH " + base + " " + key)
m_phase = Fetching; StartTimer(100, 50ms)
```

**轮询（PollProbe）**：
```
m_pending += m_process->ReadAvailable()
while (行 = 取 m_pending 首个 \n 前完整行):
    fields = split(line, '\t')
    if fields[0]=="OK"    && fields[1]=="FETCH": 记 base/count
    elif fields[0]=="MODEL": m_models.push({id, meta})   // meta 直接存（owned by X / 日期）
    elif fields[0]=="DONE" && fields[1]=="FETCH": StopTimer; busy=false; 重建列表行 + 统计
    elif fields[0]=="ERR"  && fields[1]=="FETCH": StopTimer; busy=false; stat 显示错误
    # TEST 同构：OK/TEST → TEST/id/OK|FAIL/msg 逐条收集 → DONE/TEST 汇总写预览
```

**生成 JSON（OnGenerateClick）**：按 Radio 格式从 `m_models` 选中集拼字符串（三格式同原版：ids / full / config），转义 `"` `\` → `m_previewBox->SetText(json)`。

**退出（OnWindowCloseRequested——demo app 覆写，清理顺序冻结——GPT 评审建议）**：
```
StopTimer(kProbePollTimer);              // ① 停轮询（防清理期间 PollProbe 再入）
m_process->CloseInput();                 // ② EOF → probe.exe 自退（Fetching 中途关闭 = 丢弃未消费输出，安全）
if (!m_process->WaitForExit(1000)) m_process->Terminate();   // ③ 超时兜底（防止用户点 × 后 probe 残留在后台）
ECDI::Application::OnWindowCloseRequested(event);            // ④ 转发基类关窗口
// 不删除 networkbackend/probe.exe（用户决策——固定路径固定名，KSN 信誉积累）
```

### 7.5 TSV 解析 / JSON 生成（demo 级工具，ModelProbe.cpp 内部）

- `std::vector<std::string> SplitTsv(const std::string& line)`：按 `\t` 切（无引号语义——协议自控）
- `std::string EscapeJson(const std::string& s)`：`"`→`\"`、`\`→`\\`（换行后端已清洗，无需处理）
- JSON 拼装：ids = `["id1","id2"]`；full = `[{"id":...,"created":...,"owned_by":...}]`；config = `{"base_url":"...","models":[...]}`

## 8. 测试用例表（Phase 7.2 TestFramework——EXPECT_* / RecordingBackend / TestPlatformWindow 既有模式）

| # | 用例 | 断言要点 |
|---|---|---|
| 1 | `ChildProcess.StartReadEcho` | 集成：`cmd.exe /c echo hello` → ReadAvailable 读回 `hello`（真实子进程，不依赖 probe.exe） |
| 2 | `ChildProcess.EofExit` | CloseInput 后进程自退（WaitForExit 立即 true） |
| 3 | `ChildProcess.TerminateTimeout` | 起 `cmd.exe /c pause`-类长驻 → WaitForExit(100) false → Terminate → 退出 |
| 4 | `TextBoxEcho.PaintMasked` | SetEchoMode(Password) + Paint → RecordingBackend 记录 DrawTextCommand.text == `•••` |
| 5 | `TextBoxEcho.GetTextReal` | 真实值不变；编辑操作作用于真实文本 |
| 6 | `TextBoxEcho.MaskedGeometry` | 光标 X **与选区高亮**均按掩码串测量（与 `•` 对齐——GPT 评审扩展：caret+selection+scroll 全链基于 display string） |
| 7 | `TextBoxEcho.CompositionShowsReal` | 组合期 UpdateComposition 后 Paint → 组合串原文可见 |
| 8 | `TextBoxReadOnly.EditNoOp` | SetReadOnly(true) → InsertText/DeleteBackward/Undo 全 no-op（文本/光标不变） |
| 9 | `TextBoxReadOnly.NavSelectCopy` | 方向键/Ctrl+A/Ctrl+C 照常（剪贴板断言） |
| 10 | `TextBoxReadOnly.ImmediateRefuse` | UpdateComposition no-op |
| 11 | `TextBoxReadOnly.SetTextWorks` | SetText 仍写入（预览刷新） |
| 12 | `TextBoxShape.RoundedCommand` | cornerRadius=6 → 背景命令为 DrawRoundedRect(radius 6)；默认 0 → DrawRect（零回归） |
| 13 | `TextBoxShape.BorderRing` | borderWidth=1 → 命令流 = 外层 borderColor 圆角矩形 + 内层背景（双矩形序） |
| 14 | `ButtonHover.Target` | OnMouseEnter → m_displayedBackground 即时（无窗口树）== hoverBackground；Leave 还原 |
| 15 | `ButtonHover.PressedPriority` | hover 中按下 → 目标 = pressedBackground（优先级） |
| 16 | `PanelShape.RoundedBorder` | cornerRadius/borderWidth 命令流（同构 12/13）；默认 0 零回归 |
| 17 | `ModelProbePage.FetchFlow` | 注入 fake ChildProcess：模拟 OK/MODEL×2/DONE → PollProbe 后列表行数/统计正确 |
| 18 | `ModelProbePage.FetchFragmentedOutput` | fake 分两次喂 `OK\nMOD` / `EL\t123\nDONE\n` → 解析出 OK/MODEL/DONE（**管道读取边界 ≠ 协议消息边界**——GPT 评审强推） |
| 19 | `ModelProbePage.FetchError` | fake 回 ERR → 状态行显示 + busy 还原 + StopTimer |
| 20 | `ModelProbePage.TestFlow` | fake 回 OK/TEST/OK/FAIL/DONE → 预览汇总正确 |
| 21 | `ModelProbePage.BusyGuard` | Fetching 中再点查询 → 无第二次 FETCH 写入（防重入） |
| 22 | `TsvParse.Basic` | 正常行/空 meta/半行缓冲拼接 |
| 23 | `JsonEscape.Quotes` | `"`/`\` 转义；三格式拼装形状 |

## 9. 影响面与授权清单（原子授权）

| 区 | 文件 | 动作 |
|---|---|---|
| 框架 | `ECDI/Platform/ChildProcess.h` + `ECDI/src/Platform/Win32/Win32ChildProcess.{h,cpp}` + `ECDI/Platform/ExecutablePath.h` + `ECDI/src/Platform/Win32/ExecutablePath.cpp` | 新增 |
| 框架 | `ECDI/include/ECDI/Widget/TextBox.h` / `ECDI/src/Widget/TextBox.cpp`（echo/只读/形态） | 修改 |
| 框架 | `ECDI/include/ECDI/Theme/TextBoxStyle.h` | 修改 |
| 框架 | `ECDI/include/ECDI/Widget/Button.h` / `ECDI/src/Widget/Button.cpp`（hover） | 修改 |
| 框架 | `ECDI/include/ECDI/Theme/ButtonStyle.h` | 修改 |
| 框架 | `ECDI/include/ECDI/Theme/PanelStyle.h` + `ECDI/include/ECDI/Widget/Panel.h` / `ECDI/src/Widget/Panel.cpp` | 修改 |
| 框架 | `ECDI/src/Theme/DefaultTheme.cpp` | 修改 |
| 框架 | 测试：`ECDI/src/Tests/` 新增 ChildProcessTests.cpp + 扩展 TextBoxTests/ButtonTests/PanelTests + 解析单测 | 新增/修改 |
| demo | `ECDI/src/Demo/ModelProbe.{h,cpp}` | 新增 |
| 入口 | `ECDI/main.cpp`（ModelProbe 窗口 + DemoApplication::OnTimer/OnWindowCloseRequested 覆写 + Showcase 并存） | **单独授权** |
| 工程 | `ECDI/ECDI.vcxproj`（登记新文件） | 修改 |
| 文档 | `model-probe-docs/`（本目录） | 新增 |

## 10. 修订记录

- v1.0（2026-09-01）详细设计初稿：7 项框架能力落成精确规格（ChildProcess 句柄表/创建参数、GetExecutableDirectory、TextBox echo 显示串机制/只读门禁/形态命令流、Button hover 三态、Panel 形态、DefaultTheme 默认值表）+ ModelProbePage 类结构/布局树/轮询接线/主流程伪码 + 22 条测试用例 + 授权清单。全部引用已对照源码核实。
- v1.1（2026-09-01）吸收 GPT 评审（逐条已回应，7 项全采纳）：① **§2.2 Start 补 `SetHandleInformation` 清父侧句柄继承（P0 硬问题）** + 句柄继承矩阵冻结 ② §2.1 Start 改 `(executable, args vector)`——引号转义归 Win32 实现，接口不收命令行字符串 + §2.2 P1 边界声明（非进程管理器）③ §3.3 描边环几何公式冻结（outer/inner/outerRadius/innerRadius + 退化保护）④ §8 新增 `ModelProbePage.FetchFragmentedOutput`（分包/半行缓冲）+ `MaskedGeometry` 扩展（caret+selection 全链 display 串）⑤ §7.4 退出清理顺序写死（StopTimer→CloseInput→Wait/Terminate→关窗）⑥ §4 hover 配色陷阱标注（改 background 必须同设 hoverBackground）⑦ §2.2 最小实现边界。
