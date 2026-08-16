# Phase 7.1.3 输入层抽象 — 初步设计

> 状态：v1.0（2026-08-15）｜待用户确认后进详细设计
> 相关：phase7-textinput-requirements.md（D1-D5）
> 本质（GPT）：文本插入点模型升级——**光标不是点，是矩形区域**

## P1 CaretGeometry.h（新文件，Widget/ 领域目录——GPT 赞成）

```cpp
// include/ECDI/Widget/CaretGeometry.h
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
	/// @note 注意（防后续维护者误解，GPT 三轮）：
	/// 此标志控制逻辑光标状态，**不是平台可见性**——
	/// Win32 系统 caret 永远隐藏（仅作 TSF/IMM 定位锚点），
	/// 用户看到的光标竖线由控件 OnPaint 自画。
	/// visible=false → 平台层 HideCaret（失焦/只读/代码补全弹窗统一处理）；
	/// **切勿在 true 时 ShowCaret**（系统 caret + 自画光标 = 双光标 Bug，8.5 闪烁尤其危险）。
	bool visible = true;
};

}
```

## P2 TextBox 升级（h/cpp）——GPT 修订：改名 + 常量提取

```cpp
// TextBox.h：
CaretGeometry GetCaretClientGeometry();   // ⚠️ GPT：GetCaretClientPosition → GetCaretClientGeometry
                                          // （返回值已是 CaretGeometry，Position 名不副实）

// TextBox.cpp：
namespace{   // 匿名 namespace：光标视觉常量（GPT：不散落魔法数字）
constexpr float kCaretWidth = 2.0f;   ///< 光标宽度（OnPaint 竖线 / CaretGeometry 共用——同源）
}

CaretGeometry TextBox::GetCaretClientGeometry(){
	// 客户区绝对坐标 = 控件绝对位置 + 光标相对控件位置（同一 CalculateCaretPosition 变换链）
	const Point abs = GetAbsolutePosition();
	const Point local = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
	// 光标尺寸：宽 kCaretWidth（与 OnPaint 竖线同源——不写死魔法数字）+ 高 lineH
	const Size textSize = GetWindow()->GetTextMeasurer().MeasureText(m_font, m_text);
	const float lineH = (textSize.height > 0.0f) ? textSize.height
	                                             : GetWindow()->GetTextMeasurer().LineHeight(m_font);
	return CaretGeometry{
		Rect{ abs.x + local.x, abs.y + local.y, kCaretWidth, lineH },
		m_showCaret   // 语义真实：焦点显示 / 失焦隐藏（false → 平台层 HideCaret）
	};
}
```

**同步改动**：
- `TextBox.h`：`GetCaretClientGeometry()` 声明 + OnPaint 光标竖线宽 `2.0f` → `kCaretWidth`（同源）
- **SyncTextInputCaret 12 调用点零改动**（内部参数类型变了，调用形态不变）
- Window.cpp `NotifyIMEComposition` 内 `textBox->GetCaretClientPosition()` → `GetCaretClientGeometry()`

## P3 Window 薄转发升级（h/cpp）

```cpp
// Window.h：
void UpdateTextInputCaret(const CaretGeometry& geometry);   // Point → CaretGeometry
// Window.cpp：
void Window::UpdateTextInputCaret(const CaretGeometry& geometry){
	if (m_platformWindow){
		m_platformWindow->UpdateTextInputCaret(geometry);
	}
}
// Window.h include：+ "ECDI/Widget/CaretGeometry.h"（参数引用需完整定义）
```

## P4 PlatformWindow 抽象 + Win32PlatformWindow 实现（h/cpp）

```cpp
// PlatformWindow.h（抽象签名升级）：
virtual void UpdateTextInputCaret(const CaretGeometry& geometry) = 0;
// include：+ "ECDI/Widget/CaretGeometry.h"

// Win32PlatformWindow.h/cpp：
void UpdateTextInputCaret(const CaretGeometry& geometry) override;

void Win32PlatformWindow::UpdateTextInputCaret(const CaretGeometry& geometry){
	// 7.1.3：visible 判断在**平台表现层**（GPT：Window 不知道 CreateCaret/HideCaret 细节）
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
	// 光标竖线由控件 OnPaint 自画）。visible=true 不做 ShowCaret（与 GPT 建议的分歧点：
	// GPT 认为 ShowCaret 配 HideCaret，但 ECDI 自画光标——ShowCaret 会产生双光标）。
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

## P5 构建 + 验证

- vcxproj：ClInclude 注册 `include\ECDI\Widget\CaretGeometry.h`；CMake GLOB 零改动
- 验证（V1-V3）：
  | # | 验收项 | 判据 |
  |---|---|---|
  | V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
  | V2 | 回归-IME | 中文候选窗跟随光标（caret 尺寸来自 rect 2xlineH——视觉与硬编码 2x20 一致）+ 移动窗口归位 |
  | V3 | grep 实证 | `UpdateTextInputCaret` 参数链零 `Point`（全链 CaretGeometry） |

## 边界（7.1.3 不做）

- ❌ 不抽 TextInputInterface / EditableTextWidget（D3 YAGNI）
- ❌ 不拆 Win32IME 类（D4）
- ❌ 不实现 baseline 字段（注释预留）
- ❌ 不做光标闪烁（visible 字段就位但闪烁逻辑归 8.5）

## 修订记录

- v1.0（2026-08-15）初步设计定稿：P1-P5。CaretGeometry{ rect, visible } 全链升级（TextBox → Window → PlatformWindow → Win32PlatformWindow）；CreateCaret 尺寸来自 rect（消灭硬编码 2x20）；visible=false 跳过更新（失焦/隐藏语义）；12 个 SyncTextInputCaret 调用点零改动。
- v1.1（2026-08-16，GPT 二轮）五处修订：① **改名 GetCaretClientPosition → GetCaretClientGeometry**（返回值已是 CaretGeometry，Position 名不副实）② **kCaretWidth 常量提取**（匿名 namespace，OnPaint 竖线与 CaretGeometry 同源——不散落魔法数字）③ **visible 判断放平台表现层**（Window 不知 CreateCaret/HideCaret 细节）④ **visible=false → HideCaret**（存在 ≠ 可见——区别于销毁语义）⑤ **⚠️ 分歧点：不做 ShowCaret**——保持 5.6 无条件 HideCaret（系统 caret 仅作 TSF 信标，光标竖线控件自画；ShowCaret 会双光标）。
- v1.2（2026-08-16，GPT 三轮）分歧点**消解**（GPT 完全认同自绘模型论证：系统 caret = 定位锚点 / 绘制 caret = 用户可见，两套独立）→ ① **visible 语义精化为"逻辑可见性"**（非平台可见性；防 8.5 闪烁阶段误 ShowCaret 双光标 Bug——注释防误解）② 预见 **CaretState/CaretController/BlinkTimer 发展方向**（8.5 消费），7.1.3 的 visible 是其第一块基石。
