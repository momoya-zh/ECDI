# Phase 5.6 IME（候选窗口跟随）详细设计

> 状态：v1.0.4（2026-08-15）｜已实现 + 验证通过（系统 caret 双通道 + 客户区坐标语义）
> 相关：phase5-ime-requirements.md（职责确认 I1-I5）/ phase5-ime-preliminary-design.md（初步设计 P1-P5）

## 1. 文件改动清单

| 文件 | 改动 | 类型 |
|---|---|---|
| `ECDI/include/ECDI/Window/Window.h` | public 新增 `NotifyIMEComposition()` | 修改 |
| `ECDI/src/Window/Window.cpp` | include TextBox.h + `<imm.h>`；实现 NotifyIMEComposition | 修改 |
| `ECDI/src/Window/WindowMessageHandler.cpp` | 新增 WM_IME_START/COMPOSITION/END 三个 case | 修改 |
| `ECDI/include/ECDI/Widget/TextBox.h` | public 加 GetCaretClientPosition；private 加 GetTextAreaWidth + CalculateCaretPosition | 修改 |
| `ECDI/src/Widget/TextBox.cpp` | 实现三辅助；OnPaint 两处改造 | 修改 |
| `ECDI/ECDI.vcxproj` | 4 个 Link 段加 imm32.lib | 修改 |
| `CMakeLists.txt` | user32 → user32 imm32 | 修改 |
| `docs/phase5-ime-detailed-design.md` | 本文档 | 新增 |

`main.cpp` 不动（skill 2）；无新文件 .h/.cpp（全部现有文件内改动）。

## 2. Window 侧（P2）

### 2.1 Window.h（public 区，HandleKeyDown 之后）

```cpp
		/// @brief IME 组合窗口定位（5.6；WM_IME_STARTCOMPOSITION 触发）
		/// @details MVP：焦点控件是 TextBox 时，把候选窗口移到光标位置
		/// （TextBox 给客户区坐标 → 这里 ClientToScreen → ImmSetCompositionWindow）。
		/// 非 TextBox 焦点直接返回（fail-safe：IME 交系统默认行为，不写错位置）。
		void NotifyIMEComposition();
```

### 2.2 Window.cpp

include 顺序（skill 7：.cpp 对应头 → ECDI 项目头 → Windows SDK → 标准库）：

```cpp
#include "ECDI/Window/Window.h"

#include "ECDI/Widget/TextBox.h"    // 新增：NotifyIMEComposition 的 dynamic_cast 需完整类型（仅 .cpp 依赖）
// ... 其余 ECDI 头不变 ...

#include <Windows.h>
#include <imm.h>                    // 新增：ImmGetContext/ImmSetCompositionWindow/COMPOSITIONFORM（须在 Windows.h 后）
```

实现（放 `HandleKeyDown` 之后；v1.0.4 最终版——含系统 caret 双通道 + 客户区坐标语义 + WM_EXITSIZEMOVE）：

```cpp
// Window.h public：
void NotifyIMEComposition();                              // WM_IME_START/COMPOSITION 触发入口
void UpdateTextInputCaret(const Point& clientPos);        // 系统 caret + IMM 双通道（统一入口）
void DestroyTextInputCaret();                             // TextBox 失焦销毁

// Window.cpp：
void Window::NotifyIMEComposition(){
	// MVP 技术债（显式记录）：Window 临时识别具体控件 TextBox（框架内首例"Window 认识具体控件"）。
	// 可接受理由：fail-safe——非 TextBox 焦点时跳过更新，IME 交系统默认行为；
	// 而 Widget 基类虚函数方案会把候选窗钉死在 (0,0)（fail-wrong）。
	// 演进路径：第二个可编辑控件出现时 → 抽象 EditableTextWidget，
	// dynamic_cast<TextBox*> 升级为 dynamic_cast<EditableTextWidget*>；
	// 同时随 Phase 7 PlatformWindow 下沉 Imm 调用。
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){
		UpdateTextInputCaret(textBox->GetCaretClientPosition());   // 客户区坐标（TextBox 零平台依赖）
	}
}

void Window::UpdateTextInputCaret(const Point& clientPos){
	// ① 系统 caret（TSF 输入法主路径——Win11 微软拼音查询 GetCaretPos 定位候选窗）
	if (!m_caretCreated){   // 懒创建（TextBox 获焦首次调）
		CreateCaret(m_handle, nullptr, 2, 20);   // 隐藏 caret 仅作位置信标
		m_caretCreated = true;
	}
	SetCaretPos(static_cast<int>(clientPos.x), static_cast<int>(clientPos.y));   // 客户区坐标
	HideCaret(m_handle);   // 光标竖线由控件 OnPaint 自画，系统 caret 不可见

	// ② ImmSetCompositionWindow（IMM 保底通道）
	// ⚠️ v1.0.4 关键修正（用户洞察 2026-08-15）：微软拼音（TSF）把 ptCurrentPos 当**客户区坐标**解释！
	// 证据：最小实验 SetCaretPos(300,200) 时候选框出现在窗口内 (300,200)（客户区）而非屏幕 (300,200)。
	// 若按 IMM 文档"屏幕坐标"传 ClientToScreen 后的值 → 候选框落在窗口内"屏幕坐标值"处（远离光标）
	// + 窗口移动时叠加窗口偏移（"像素过多"）。故**不再 ClientToScreen，直接传客户区坐标**。
	POINT pt{ static_cast<LONG>(clientPos.x), static_cast<LONG>(clientPos.y) };
	if (HIMC imc = ImmGetContext(m_handle)){
		COMPOSITIONFORM cf{};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos = pt;
		ImmSetCompositionWindow(imc, &cf);
		ImmReleaseContext(m_handle, imc);
	}
}

void Window::DestroyTextInputCaret(){
	if (m_caretCreated){
		DestroyCaret();
		m_caretCreated = false;
	}
}

// Window.cpp HandleMessage 内部状态同步 switch（v1.0.3）：
case WM_EXITSIZEMOVE:
	// 窗口移动/缩放结束 → 销毁+重建系统 caret（TSF 缓存候选窗位置失效，强制重新查询）
	DestroyTextInputCaret();
	NotifyIMEComposition();
	return 0;
```

**关键点**：
- `dynamic_cast` 只在 Window.cpp 用——Window.h 零新增 include，TextBox 依赖不泄漏到公共头
- `CFS_POINT` = 候选窗左上角钉在光标点（屏幕坐标）；备选 CFS_RECT（矩形区域）MVP 不需要
- `ImmGetContext` 失败（无 IME 上下文）→ 静默返回，行为安全

## 3. 翻译器（P1）

`WindowMessageHandler.cpp` switch 末尾（WM_CHAR case 之后）：

```cpp
	// ── IME（5.6：候选窗口跟随光标——MVP 只在组合建立时定位）──

	// 注意：IME 消息必须走 DefWindowProc（维护 IME 内部状态——与普通消息不同，不能 return 0 吞掉）
	case WM_IME_STARTCOMPOSITION:{
		// 组合开始 → 通知 Window 定位候选窗口（跟随焦点 TextBox 光标）
		window->NotifyIMEComposition();

		return std::nullopt;
	}

	case WM_IME_COMPOSITION:{
		// 5.6 实测升级（2026-08-14）：STARTCOMPOSITION 单次定位不可靠——
		// 窗口移动后候选窗飘回屏幕左上角（组合窗 START 时未创建，ImmSetCompositionWindow 被忽略）。
		// 组合期间每次按键重新定位（候选窗持续跟随光标；调用开销极小——单次 Imm API）
		window->NotifyIMEComposition();

		return std::nullopt;
	}

	case WM_IME_ENDCOMPOSITION:{        // 预留通道：组合结束——MVP 无清理动作（未来组合串内嵌时用）
		return std::nullopt;
	}
```

**关键点**：
- WM_IME_* 三个 case 全部 `return std::nullopt` → HandleMessage 走 `DefWindowProcW`（IME 状态机必需，GPT 第 4 点）
- 翻译器零新增 include（WM_IME_* 常量在 Windows.h 已含）；Imm API 只在 Window.cpp 出现
- **COMPOSITION 已由预留升级为实际调用**（v1.0.2 实测驱动——P5 决定项预设路径，case 内加一行，消息结构不变）

## 4. TextBox 侧（P3）

### 4.1 TextBox.h

public 区（`GetCaret()` 之后）：

```cpp
	// ── IME 位置（5.6）────────────────────────────────
	/// @brief 光标底部客户区坐标（IME 候选窗口锚点——候选窗贴光标下方，职责确认 P3 决策 c）
	/// @details 纯几何查询：GetAbsolutePosition + CalculateCaretPosition（与光标绘制同源）。
	/// 返回值 = 窗口客户区坐标（与 GetAbsolutePosition/事件 GetMouseX 同一坐标系——
	/// 非屏幕坐标、非控件相对坐标；命名保留决议 2026-08-14：项目内 Client == 窗口客户区已统一）。
	/// 平台转换（ClientToScreen）是 Window 职责（I2 分层——TextBox 不知道屏幕坐标）。
	/// 非 const：测量需经 GetWindow()->GetTextMeasurer()（Window 接口非 const，与 OnMouseButtonDown 同性质）。
	Point GetCaretClientPosition();
```

private 区（Selection 辅助之后）：

```cpp
	// ── 光标几何（5.6 提取：消灭三处漂移——点击定位/光标绘制/IME 同源）──

	/// @brief 可视文本宽度（控件宽 − 焦点框内缩 2px×2）——文本裁切/Selection 高亮/光标 共用
	float GetTextAreaWidth() const noexcept;

	/// @brief 光标像素位置 = IME 锚点（光标底部：textPos.y + lineH；相对文本框左上角——不含绝对窗口偏移；含可视钳制）
	/// @param measurer 测量器——调用方决定来源（OnPaint 经 GetWindow()->GetTextMeasurer()，
	/// PaintContext 封装不暴露 measurer；与点击定位 CaretIndexFromX 同源但不合并——方向相反）
	/// @details OnPaint 竖线用 y - lineH 还原光标顶部（lineH 绘制时已有）
	Point CalculateCaretPosition(TextMeasurer& measurer) const;
```

（TextMeasurer 完整类型经 Widget.h → PaintContext.h → TextMeasurer.h 传递可见，无需新增 include。）

### 4.2 TextBox.cpp

新增实现（Selection 辅助之后、`GetCaret` 之前）：

```cpp
// ── 光标几何（5.6 提取：与点击定位同源——CalculateTextPosition 单一入口）──

float TextBox::GetTextAreaWidth() const noexcept{
	// 可视宽度 = 控件宽 − 焦点框内缩（2px×2）——与 OnPaint 原 maxTextWidth 同款逻辑
	// （提取为共享辅助：文本裁切/Selection 高亮/光标 三处共用，改一处不漂移）
	const float padding = HasFocus() ? 2.0f : 0.0f;
	return static_cast<float>(GetWidth()) - padding * 2.0f;
}

Point TextBox::CalculateCaretPosition(TextMeasurer& measurer) const{
	// 与 OnPaint 光标绘制完全同源：一次全文测量 → CalculateTextPosition → 前缀宽 → 可视钳制
	const Size textSize = measurer.MeasureText(m_font, m_text);
	// 空串高度兜底（同 OnPaint 296 行）：GDIBackend::MeasureText("") 返回 {0,0}，垂直定位需行高
	const float lineH = (textSize.height > 0.0f) ? textSize.height : measurer.LineHeight(m_font);
	const Point textPos = CalculateTextPosition(0, 0, textSize.width, lineH);   // 相对控件原点
	const size_t byteOffset = CodepointIndexToByteOffset(m_text, m_caret);
	const Size prefixSize = measurer.MeasureText(m_font, m_text.substr(0, byteOffset));
	// 可视钳制：光标超出可视区钉在右缘（示意"后面还有"；自动水平滚动归 Phase 6）
	const float caretX = (std::min)(prefixSize.width, GetTextAreaWidth());
	// v1.0.2：返回 IME 锚点 = 光标底部（y + lineH——候选窗贴光标下方，职责确认 P3 决策 c 回归）；
	// float 直传（Point 成员 float——转 int 列表初始化触发 C2397 narrowing，且丢精度）
	return Point{ textPos.x + caretX, textPos.y + lineH };
}

Point TextBox::GetCaretClientPosition(){
	// 客户区绝对坐标 = 控件绝对位置 + 光标相对控件位置（同一 CalculateCaretPosition 变换链）
	const Point abs = GetAbsolutePosition();
	const Point local = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
	return Point{ abs.x + local.x, abs.y + local.y };
}
```

### 4.3 OnPaint 两处改造

**改造点 A**（原 300-301 行）——maxTextWidth 改共享辅助：

```cpp
	// 原：
	// 可视宽度：控件宽 − 焦点框内缩（2px×2）——否则文字覆盖蓝色边框（T8 裁切）
	// const float padding = HasFocus() ? 2.0f : 0.0f;
	// const float maxTextWidth = fw - padding * 2.0f;

	// 新（5.6：GetTextAreaWidth 共享——裁切/Selection/光标 改一处）：
	const float maxTextWidth = GetTextAreaWidth();
```

（局部变量名保留——Selection 高亮 309-310、文本裁切 314/326、光标 341 三处引用零改动。）

**改造点 B**（原 336-342 行）——光标竖线改共享辅助：

```cpp
	// 原：
	// 3. 光标竖线（与文本起点同源——不直接用 x）
	// if (m_showCaret){
	// 	const size_t byteOffset = CodepointIndexToByteOffset(m_text, m_caret);
	// 	const Size prefixSize = ctx.MeasureText(m_font, m_text.substr(0, byteOffset));
	// 	const float caretX = (std::min)(prefixSize.width, maxTextWidth);
	// 	ctx.DrawRect(Rect{ textPos.x + caretX, textPos.y, 2.0f, lineH }, Color::Black());
	// }

	// 新（5.6：CalculateCaretPosition 共享——点击定位/光标绘制/IME 同一变换链）：
	if (m_showCaret){
		// 测量经 Window：PaintContext 封装不暴露 measurer（决策 8）——
		// 与 PaintFrame 注入为同一 TextMeasurer 实例（m_backend），结果一致
		// v1.0.2：CalculateCaretPosition 返回 IME 锚点（光标底部）；竖线用 y - lineH 还原顶部
		const Point caretLocal = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
		ctx.DrawRect(Rect{ fx + caretLocal.x, fy + caretLocal.y - lineH, 2.0f, lineH }, Color::Black());
	}
```

## 5. 构建（P4）

### 5.1 ECDI.vcxproj

4 个 `<ItemDefinitionGroup>` 的 `<Link>` 段各加一行：

```xml
    <Link>
      <SubSystem>...</SubSystem>
      <AdditionalDependencies>imm32.lib;%(AdditionalDependencies)</AdditionalDependencies>  <!-- 新增 -->
      ...
    </Link>
```

（MSVC 默认系统库不含 Imm32，必须显式链接。）

### 5.2 CMakeLists.txt

```cmake
if(WIN32)
    target_link_libraries(ECDI PRIVATE user32 imm32)   # 新增 imm32
endif()
```

## 6. 关键技术点与理由汇总

| 点 | 决策 | 理由 |
|---|---|---|
| 共享辅助测量来源 | OnPaint 用 `GetWindow()->GetTextMeasurer()` | PaintContext 完全封装（决策 8）不暴露 m_measurer；TextBox.cpp 179 行已有同款先例；与 PaintFrame 注入为同一实例（已验证 `PaintContext ctx(m_commands, m_backend)`） |
| 辅助返回相对坐标 | OnPaint 加 (x,y)、GetCaretClientPosition 加 GetAbsolutePosition | 辅助不依赖调用上下文，纯函数式（输入测量器 → 输出相对位置） |
| `GetTextAreaWidth` 单独提取 | 不内嵌进 CalculateCaretPosition | OnPaint 文本裁切/Selection 高亮也需要它——独立辅助才真正消灭 maxTextWidth 漂移 |
| lineH 兜底两处 | OnPaint 与 CalculateCaretPosition 各一份 | 一行级兜底（`height>0 ? : LineHeight`），风险低；辅助只返回 Point 不含高度，OnPaint 的 lineH 另用于 Selection/竖线高度 |
| CFS_POINT | 候选窗锚点钉光标**顶部**（v1.0.3：与系统 caret 统一坐标） | 系统 caret 是主通道（TSF 输入法查询）；ImmSetCompositionWindow 保底（老 IMM 输入法），与 caret 同点；CFS_RECT（区域）无消费者，YAGNI |
| HIMC 判空 | 失败静默 | 无 IME 上下文时行为安全，不崩不写错位置 |
| Imm 调用位置 | 只在 Window.cpp | IME 是平台能力（与 GDIBackend 同性质），Phase 7 随 PlatformWindow 下沉 |

## 7. 验证计划（P5）

1. **中文输入**：焦点 TextBox，拼音输入——候选窗跟随光标（不再飘左上角）✅ 目标
2. **回归**：中文上屏（WM_CHAR 路径）正常；光标绘制/点击定位/拖选/编辑操作无视觉回归
3. **输入法矩阵**：搜狗 / 微软拼音 / 中文 / 日文 / 长文本（预填长文本 + 光标在中间输入）——重点观察候选窗位置
4. **fail-safe**：焦点在 Button 时输入，候选窗不强制移动（交系统默认行为）
5. **窗口拖动（v1.0.2 已缓解）**：组合不结束 → 拖动窗口 → 旧行为：候选窗停留旧屏幕位置（设计限制）——v1.0.2 起 COMPOSITION 每次按键重新定位，**移动后下一键即归位**；窗口移动**过程中**（不按键）候选窗仍短暂错位（设计限制，可接受）
6. **决定项（v1.0.2 已执行）**：实测确认 STARTCOMPOSITION 单次定位不可靠（窗口移动后候选窗飘回屏幕左上角）→ **COMPOSITION 空通道升级为实际调用**（P5 预设路径，case 内加一行，消息结构不变）
7. 断言：无新增可断言的（IME 为平台交互，main.cpp 断言段不动）
8. **系统 caret 双通道（v1.0.3）**：① 候选窗跟随**真实光标**（输入中移动窗口 → 下一键归位）② 焦点切 Button → 失焦销毁 caret → 候选窗回默认位置（fail-safe）③ 光标贴近屏幕右缘 + 长候选词 → 输入法自身翻转（输入法职责，记为已知行为非缺陷）④ 已移除全部诊断日志/实验代码

## 8. 技术债记账

| 债务 | 归属 |
|---|---|
| Window 认识具体控件 TextBox（dynamic_cast） | 注释显式化；第二个可编辑控件 → EditableTextWidget；随 Phase 7 下沉 |
| lineH 兜底两处重复 | 一行级低风险；光标几何若升级结构体再合并 |
| 窗口移动过程中（不按键）候选窗短暂错位 | COMPOSITION 持续定位已缓解移动后场景；移动中不按键仍错位——MVP 可接受，WM_MOVE 场景未来评估 |
| 滚动/长文本（Phase 6 才有滚动） | 届时候选位置与滚动偏移联动，随滚动实现一并处理 |
| 系统 caret 懒创建 + 单窗口单 caret | 多窗口 TextBox 焦点切换场景 MVP 未验证（失焦 DestroyCaret 已覆盖基本路径）；Phase 7 平台抽象时评估 |
| 候选框过长贴屏幕边缘翻转 | 输入法自身边界自适应（任何应用都有）；非框架缺陷，验证矩阵记录即可 |

## 9. 系统 caret 双通道（v1.0.3，TSF 实测驱动）

### 9.1 背景与证据（2026-08-14 晚诊断）

- **诊断日志**：坐标链 100% 正确（client 随光标推进 190→218→232→246；窗口移动后 screen 偏移同步）；`ImmGetContext=OK`、`ImmSetCompositionWindow=1`（TRUE）
- **但候选框不跟随** → 用户输入法为 TSF 架构（Win11 微软拼音，加载 TextShaping.dll），**ImmSetCompositionWindow 对其候选窗位置无效**（API 返回 TRUE 只是"接受调用"，TSF 候选窗位置由 TSF 框架独立管理）
- **最小实验**：硬编码 `SetCaretPos(300,200)` + HideCaret → 候选框跑到窗口 (300,200) 处 → **TSF 输入法查询系统 caret 定位候选窗，假设成立**

### 9.2 职责确认 C1-C5

| # | 决策 |
|---|---|
| C1 范围 | 系统 caret = **隐藏的文本输入插入点信标**（HideCaret 不显示——光标竖线仍 OnPaint 自画，视觉零变化）；生命周期跟 TextBox 焦点（获焦创建/失焦销毁） |
| C2 分层 | Window 中介（TextBox → `UpdateTextInputCaret`/`DestroyTextInputCaret`）；TextBox 零平台依赖，只给客户区坐标（与 GetCaretClientPosition 同款） |
| C3 双通道 | `UpdateTextInputCaret` 内同时做 `SetCaretPos`（TSF 主路径）+ `ImmSetCompositionWindow`（IMM 保底）——GPT 双保险 |
| C4 调用点 | OnFocusGained(懒创建+初始位置) / 光标变动:OnMouseButtonDown·OnMouseMove·OnKeyDown方向键·InsertCodepoint·DeleteBackward·DeleteForward·MoveCaret·MoveCaretToStart·MoveCaretToEnd / OnFocusLost(销毁) |
| C5 坐标 | 统一用**光标顶部**客户区坐标（SetCaretPos 语义=caret 左上角；Imm 转 ClientToScreen 后同点）→ CalculateCaretPosition **回归返回顶部**（v1.0.2 曾为底部锚点，现以 caret 为主通道） |

### 9.3 接口与调用点

```cpp
// Window.h public：
void UpdateTextInputCaret(const Point& clientPos);    // 懒创建 caret + SetCaretPos + Imm 三件套
void DestroyTextInputCaret();                         // DestroyCaret

// Window.cpp：
void Window::UpdateTextInputCaret(const Point& clientPos){
	if (!m_caretCreated){            // 懒创建（OnFocusGained 首次调）
		CreateCaret(m_handle, nullptr, 2, 20);
		m_caretCreated = true;
	}
	SetCaretPos(static_cast<int>(clientPos.x), static_cast<int>(clientPos.y));  // 客户区坐标
	HideCaret(m_handle);             // 隐藏：仅作位置信标，光标竖线由控件自画
	POINT pt{ static_cast<LONG>(clientPos.x), static_cast<LONG>(clientPos.y) };
	ClientToScreen(m_handle, &pt);   // 客户区 → 屏幕
	if (HIMC imc = ImmGetContext(m_handle)){   // IMM 保底通道
		COMPOSITIONFORM cf{};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos = pt;
		ImmSetCompositionWindow(imc, &cf);
		ImmReleaseContext(m_handle, imc);
	}
}

// NotifyIMEComposition 简化为转发（WM_IME case 入口不变）：
void Window::NotifyIMEComposition(){
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget))
		UpdateTextInputCaret(textBox->GetCaretClientPosition());
}

// TextBox 私有辅助（11 个调用点统一入口）：
void TextBox::SyncTextInputCaret(){
	if (Window* window = GetWindow())
		window->UpdateTextInputCaret(GetCaretClientPosition());
}
// 调用点：OnFocusGained（Sync）/ OnFocusLost（DestroyTextInputCaret）/
// OnMouseButtonDown / OnMouseMove / OnKeyDown（方向键）/ InsertCodepoint /
// DeleteBackward / DeleteForward / MoveCaret / MoveCaretToStart / MoveCaretToEnd
```

- **NotifyIMEComposition 保留**（翻译器不认识 TextBox 的分层保持：WM_IME case 调 Notify，Notify 内部 dynamic_cast + 转发）
- **诊断日志/最小实验代码已移除**（本次清理）

### 9.4 回退策略（用户 2026-08-15 确认"先测试，出问题回退"）

- **回退基线 = v1.0.2 正式状态**（Imm 单通道 + COMPOSITION 升级 + 底部锚点，无临时代码）
- 回退动作：恢复本版改动的 4 个文件（Window.h/cpp、TextBox.h/cpp）至 v1.0.2 状态（改动记录完整保留）；WindowMessageHandler.cpp 未动（case 仍调 NotifyIMEComposition）

## 10. 修订记录

- v1.0（2026-08-14）详细设计定稿：文件清单 + 逐文件改动 + 关键点理由 + 验证计划。实现按此文档逐条落地。
- v1.0.1（2026-08-14，GPT 第三轮）：① CalculateCaretPosition/GetCaretClientPosition 注释补坐标系语义（相对控件 / 窗口客户区）② 验证矩阵加"窗口拖动"项（设计限制确认）③ 命名决议：**保留 GetCaretClientPosition**（项目内 Client == 窗口客户区已统一——GetMouseX/GetAbsolutePosition 先例；改名收益 < 三份文档 + 代码变更成本）。
- v1.0.2（2026-08-14，实测驱动修正）：
  - **COMPOSITION 空通道升级为实际调用**（P5 决定项执行）——实测：STARTCOMPOSITION 单次定位不可靠，窗口移动后候选窗飘回屏幕左上角（组合窗 START 时未创建，ImmSetCompositionWindow 被忽略）；组合期间每次按键重新定位
  - **锚点回归职责确认 P3 决策 c**——CalculateCaretPosition 返回光标底部（textPos.y + lineH），OnPaint 竖线用 y - lineH 还原顶部（实现曾偏离为顶部）
  - 文档代码同步实现（float 直传无 narrowing / GetCaretClientPosition 非 const）
- v1.0.3（2026-08-15，TSF 实测驱动 + 用户确认"先测试出问题回退"）：
  - **根因确认**：用户输入法为 TSF 架构（Win11 微软拼音），ImmSetCompositionWindow 对其候选窗位置无效；最小实验证明 TSF 查询**系统 caret**
  - **系统 caret 双通道**（GPT 建议全采纳）：`UpdateTextInputCaret` = SetCaretPos（TSF 主）+ ImmSetCompositionWindow（IMM 保底）；`DestroyTextInputCaret` 失焦销毁；`SyncTextInputCaret` TextBox 私有辅助统一 11 个调用点
  - **坐标回归顶部**（C5）：CalculateCaretPosition 返回 textPos.y（顶部），OnPaint 竖线恢复 `fy + caretLocal.y`（v1.0.2 的底部锚点 + y-lineH 还原撤销——caret 语义要求顶部）
  - **清理**：移除 NotifyIMEComposition 内诊断日志 + 最小实验代码；WindowMessageHandler.cpp 未动
  - **回退基线**：v1.0.2 状态（见 9.4）
- v1.0.4（2026-08-15，用户实测洞察 + 验证通过）：
  - **关键修正（用户洞察）**：微软拼音（TSF）把 `ImmSetCompositionWindow` 的 ptCurrentPos 当**客户区坐标**解释（非 IMM 文档的屏幕坐标）——最小实验佐证（SetCaretPos(300,200) 候选框在窗口内 300,200 而非屏幕 300,200）。**不再 ClientToScreen，直接传客户区坐标**——解决"候选框远离光标"+"移动像素过多"（窗口偏移被叠加）
  - 排除项：移动量 ÷2/÷4 实验无效（不是倍数问题，是坐标系语义问题）；不移动实验证明输入法默认把候选框钉左上角（必须主动移动）
  - **WM_EXITSIZEMOVE 升级**（v1.0.3 补充）：销毁+重建 caret 强制 TSF 缓存失效（仅 SetCaretPos 不够——组合中已显示候选窗不重查）
  - **验证通过**（用户实测）：候选框跟随光标（含移动窗口 1:1、退格/打字跟随）
  - **清理**：移除全部诊断日志（Logger include、纯 API 测量块）；Window.cpp 恢复干净实现
  - 最终数据流：光标变动 12 点 SyncTextInputCaret → UpdateTextInputCaret（SetCaretPos 客户区 + ImmSetCompositionWindow 客户区）｜WM_IME START/COMPOSITION → NotifyIMEComposition → UpdateTextInputCaret｜WM_EXITSIZEMOVE → 销毁重建｜失焦 → DestroyTextInputCaret
