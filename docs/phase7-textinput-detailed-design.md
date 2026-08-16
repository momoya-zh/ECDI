# Phase 7.1.3 输入层抽象 — 详细设计

> 状态：v1.1（2026-08-16）｜**已实现并验证通过**（V1 编译零警告 + V2 IME 回归正常）
> 相关：phase7-textinput-requirements.md（D1-D5）/ phase7-textinput-preliminary-design.md（v1.2）
> 本质（GPT）：文本插入点模型升级——光标不是点，是矩形区域；**独立 Caret 子系统第一块基石**

## 0. 实现前置事实（已核实）

| # | 事实 | 影响 |
|---|---|---|
| F1 | TextBox.h:41 `Point GetCaretClientPosition();` | 改名 + 返回 CaretGeometry |
| F2 | TextBox.cpp:397 OnPaint 光标竖线 `Rect{ fx+caretLocal.x, fy+caretLocal.y, 2.0f, lineH }` | **2.0f 硬编码 → kCaretWidth 常量**（同源） |
| F3 | ⚠️ TextBox.cpp:133/134 的 `2.0f` 是**焦点框 padding**（非光标宽度） | **不能动**——勿误改 |
| F4 | TextBox.cpp:163 SyncTextInputCaret 调 `UpdateTextInputCaret(GetCaretClientPosition())` | 参数类型随链升级 |
| F5 | Window.cpp:230 NotifyIMEComposition 调 `textBox->GetCaretClientPosition()` | 改名同步 |
| F6 | PlatformWindow.h:34 `UpdateTextInputCaret(const Point&)` | 签名升级 CaretGeometry |
| F7 | Win32PlatformWindow.cpp:201-246 实现（CreateCaret 2x20 硬编码 + SetCaretPos + Imm） | CreateCaret 尺寸来自 rect；visible 逻辑 |

## 1. 文件清单（1 新 + 5 改 + 1 构建）

| 文件 | 动作 |
|---|---|
| `include/ECDI/Widget/CaretGeometry.h` | **新增**（领域目录，rect + visible 逻辑可见性注释） |
| `include/ECDI/Widget/TextBox.h` + `src/Widget/TextBox.cpp` | 改：改名 GetCaretClientGeometry + kCaretWidth 常量 + OnPaint 竖线同源 |
| `include/ECDI/Window/Window.h` + `src/Window/Window.cpp` | 改：UpdateTextInputCaret(const CaretGeometry&) + include + NotifyIMEComposition 改名 |
| `include/ECDI/Platform/PlatformWindow.h` | 改：抽象签名 Point → CaretGeometry + include |
| `include/ECDI/Platform/Win32/Win32PlatformWindow.h/cpp` | 改：实现升级（visible + CreateCaret 尺寸来自 rect） |
| `ECDI.vcxproj` | 注册 CaretGeometry.h；CMake GLOB 零改动 |

## 2. 逐文件详细

### 2.1 `Widget/CaretGeometry.h`（新）

```cpp
#pragma once

#include "ECDI/Core/Rect.h"

namespace ECDI{

/// @brief 文本插入点几何（7.1.3 输入层抽象——"光标不是点，是矩形区域"）
/// @details 语义放大（GPT）：光标位置是通用能力——单行/多行/富文本/代码编辑器都需要，
/// 不只 IME。由 TextBox 输出（唯一生产者），经 Window 转发给平台层消费。
/// 领域落点：Widget/ 目录（非 Core——迁 Core 条件 = 3~4 个独立子系统使用）。
/// 扩展预留（注释记录，非现在实现）：baseline——部分输入法候选框按基线定位。
struct CaretGeometry{
	Rect rect;	///< 插入点矩形（客户区坐标：x/y = 光标顶部 + width/height = 光标尺寸）

	/// @brief 光标**逻辑可见性**（存在 ≠ 可见——GPT 三轮语义精化）
	/// @note 注意（防后续维护者误解）：
	/// 此标志控制逻辑光标状态，**不是平台可见性**——
	/// Win32 系统 caret 永远隐藏（仅作 TSF/IMM 定位锚点），
	/// 用户看到的光标竖线由控件 OnPaint 自画。
	/// visible=false → 平台层 HideCaret（失焦/只读/代码补全弹窗统一处理）；
	/// **切勿在 true 时 ShowCaret**（系统 caret + 自画光标 = 双光标 Bug，8.5 闪烁尤其危险）。
	bool visible = true;
};

}
```

### 2.2 `TextBox.h`（改）

```cpp
// include：+ "ECDI/Widget/CaretGeometry.h"（返回类型完整定义）

// 41 行：Point GetCaretClientPosition();  → 改名 + 返回类型
CaretGeometry GetCaretClientGeometry();   // ⚠️ GPT 二轮：返回值已是 CaretGeometry，Position 名不副实
```

### 2.3 `TextBox.cpp`（改）

```cpp
// 文件顶部匿名 namespace 或函数外常量（放在 CalculateTextPosition 相关区域前）：
namespace{
constexpr float kCaretWidth = 2.0f;   ///< 光标竖线宽（GPT 二轮：OnPaint 与 CaretGeometry 同源——不散落魔法数字）
}

// 152 行：实现改名 + 返回 CaretGeometry
CaretGeometry TextBox::GetCaretClientGeometry(){
	// 客户区绝对坐标 = 控件绝对位置 + 光标相对控件位置（同一 CalculateCaretPosition 变换链）
	const Point abs = GetAbsolutePosition();
	const Point local = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
	// 光标尺寸：宽 kCaretWidth（与 OnPaint 竖线同源）+ 高 lineH（与 CalculateCaretPosition 同源测量）
	TextMeasurer& measurer = GetWindow()->GetTextMeasurer();
	const Size textSize = measurer.MeasureText(m_font, m_text);
	const float lineH = (textSize.height > 0.0f) ? textSize.height : measurer.LineHeight(m_font);
	return CaretGeometry{
		Rect{ abs.x + local.x, abs.y + local.y, kCaretWidth, lineH },
		m_showCaret   // 逻辑可见性：焦点显示 / 失焦隐藏（false → 平台层 HideCaret）
	};
}

// 397 行 OnPaint 光标竖线：2.0f → kCaretWidth（同源）
ctx.DrawRect(Rect{ fx + caretLocal.x, fy + caretLocal.y, kCaretWidth, lineH }, Color::Black());

// 163 行 SyncTextInputCaret：调用形态不变（内部类型变了）
window->UpdateTextInputCaret(GetCaretClientGeometry());
```

**SyncTextInputCaret 12 调用点零改动**（内部参数类型变了，调用形态不变）。

### 2.4 `Window.h`（改）

```cpp
// include：+ "ECDI/Widget/CaretGeometry.h"（参数引用需完整定义）

// 97 行：
void UpdateTextInputCaret(const CaretGeometry& geometry);   // Point → CaretGeometry
```

### 2.5 `Window.cpp`（改）

```cpp
// 236 行：薄转发签名同步
void Window::UpdateTextInputCaret(const CaretGeometry& geometry){
	if (m_platformWindow){
		m_platformWindow->UpdateTextInputCaret(geometry);
	}
}

// 230 行 NotifyIMEComposition：改名
UpdateTextInputCaret(textBox->GetCaretClientGeometry());
```

### 2.6 `PlatformWindow.h`（改）

```cpp
// include：+ "ECDI/Widget/CaretGeometry.h"（参数引用需完整定义）

// 34 行：
virtual void UpdateTextInputCaret(const CaretGeometry& geometry) = 0;   // Point → CaretGeometry
```

### 2.7 `Win32PlatformWindow.h/cpp`（改）

```cpp
// .h 41 行签名同步：
void UpdateTextInputCaret(const CaretGeometry& geometry) override;

// .cpp 201 行实现升级：
void Win32PlatformWindow::UpdateTextInputCaret(const CaretGeometry& geometry){
	// 7.1.3：visible 判断在**平台表现层**（GPT：Window 不知 CreateCaret/HideCaret 细节）
	// visible=false → HideCaret（**存在 ≠ 可见**——caret 仍存在但不显示；
	// 区别于"销毁"（DestroyTextInputCaret）——失焦销毁 vs 存在隐藏是两种语义）
	if (!geometry.visible){
		HideCaret(m_hwnd);
		return;
	}
	// ① 系统 caret（TSF 主路径）——尺寸来自 rect（消灭硬编码 2x20；定位/绘制/输入同源）
	if (!m_caretCreated){
		CreateCaret(m_hwnd, nullptr,
			static_cast<int>(geometry.rect.width),
			static_cast<int>(geometry.rect.height));
		m_caretCreated = true;
	}
	SetCaretPos(static_cast<int>(geometry.rect.x), static_cast<int>(geometry.rect.y));
	// ⚠️ 保持 5.6 行为：始终 HideCaret（**不自画双光标**——系统 caret 仅作 TSF 位置信标，
	// 光标竖线由控件 OnPaint 自画）。visible=true 不做 ShowCaret（GPT 三轮认同——分歧消解）。
	HideCaret(m_hwnd);
	// ② ImmSetCompositionWindow（IMM 保底通道）——客户区坐标语义（5.6 实测，注释保留）
	POINT pt{ static_cast<LONG>(geometry.rect.x), static_cast<LONG>(geometry.rect.y) };
	if (HIMC imc = ImmGetContext(m_hwnd)){
		COMPOSITIONFORM cf{};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos = pt;
		ImmSetCompositionWindow(imc, &cf);
		ImmReleaseContext(m_hwnd, imc);
	}
}
```

### 2.8 `ECDI.vcxproj`（改）

- ClInclude 注册：`include\ECDI\Widget\CaretGeometry.h`

## 3. 实现顺序（Step 1-4）

| Step | 内容 | 验证 |
|---|---|---|
| 1 | 新建 CaretGeometry.h + vcxproj 注册 | 编译（未引用不报错） |
| 2 | TextBox.h/cpp（改名 + kCaretWidth + OnPaint 同源） | 编译（Window 调用点还引用旧名——预期报错） |
| 3 | Window/PlatformWindow/Win32PlatformWindow 全链签名升级 | 编译（应全部通过——同步改完） |
| 4 | 验证 V1-V3 | 全过收尾 |

## 4. 验证（V1-V3）

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | 回归-IME | 中文候选窗跟随光标（caret 尺寸来自 rect 2xlineH——视觉与硬编码 2x20 一致）+ 移动窗口归位 + **失焦 caret 隐藏**（visible=false → HideCaret） |
| V3 | grep 实证 | `UpdateTextInputCaret` 参数链零 `Point`（全链 CaretGeometry）；`GetCaretClientPosition` 零出现（已改名） |

## 5. 技术债（7.1.3 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| dynamic_cast\<TextBox\>（NotifyIMEComposition） | Window.cpp | 第二个可编辑控件 → EditableTextWidget |
| Window::OnEvent Transitional adapter | Window.cpp | 7.1.5 Application 解耦 |
| CaretGeometry.baseline 未实现（注释预留） | CaretGeometry.h | 多行/富文本时代 |

## 6. 修订记录

- v1.0（2026-08-16）详细设计定稿：1 新 + 5 改 + 构建；F1-F7 前置事实；Step 1-4；V1-V3。关键点：kCaretWidth 与 OnPaint 竖线同源（F2/F3 区分 padding 2.0f——勿误改）；visible 逻辑可见性 + 平台层判断 + 保持 5.6 无条件 HideCaret（防双光标）；12 个 SyncTextInputCaret 调用点零改动。
- v1.1（2026-08-16）**验证通过**：实现落地（CaretGeometry.h + TextBox 改名 + 全链签名升级 + vcxproj）。V2 用户实测"目前都正常"（IME 候选窗跟随 + caret 尺寸来自 rect 视觉一致）。顺手清理 PlatformWindow.h 无用 Point.h include。V3 grep 实证：旧名 GetCaretClientPosition 零出现 / UpdateTextInputCaret(const Point 零出现。
