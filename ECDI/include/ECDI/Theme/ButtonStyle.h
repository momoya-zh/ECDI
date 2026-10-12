#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/TextAlignment.h"   // ★ Phase 30 △3：textAlignment 字段（公共→公共——叶子头零依赖）

#include <optional>

namespace ECDI{

/// @brief Button 专属样式（★ 审计 H10/#66：**含 foreground**——Button 文字色经主题/ButtonStyle 通路，见下）
/// @details ★★ **契约变更（审计 H10，2026-10-12）**：本结构新增 `foreground`。
///   变更前：`Button::ApplyTheme` 先注入 `TextStyle.foreground`（全局默认 **Black**），
///   紧接着**硬编码注入 `Color::White()`** ⇒ ① 用户自定义主题的 `GetTextStyle().foreground`
///   对 Button **恒被盖掉**（主题侧死值）；② 同一属性存在**两个互相冲突的默认值**
///   （Black 注入后立刻被 White 覆盖）；③ 与 `Theme.h` 的「Style 是唯一视觉属性来源」相矛盾。
///   变更后：Button 文字默认色**只**来自 `ButtonStyle.foreground`（`DefaultTheme` 给 White
///   —— 白字叠蓝底是 Button 视觉身份，语义不变），主题通路真正生效。
///   ★ 保留 `TextStyle.foreground` 作为**单点覆盖**通路（`SetStyle(TextStyleOverride.foreground)`
///   仍有效且优先级最高——它是运行期 Override，不走主题）——**既有使用者零改动**。
/// ⚠️ 与 `TextStyle` 的分工：`TextStyle` 是**全库文本控件的共享默认**（Label/TextBox 黑字）；
///   `ButtonStyle.foreground` 是 **Button 的主题默认**（白字）。二者不再互相覆盖——
///   Button 的主题注入顺序改为「先 TextStyle（供 Label/TextBox 语义）→ 再 ButtonStyle.foreground
///   覆盖为 Button 自己的主题默认」，且**尊重 D7 Override 标志**（Set 过则不被主题覆盖）。
struct ButtonStyle{
	StyleField<Color> background;         ///< 背景色（正常态）
	StyleField<Color> border;             ///< 焦点边框色
	StyleField<float> borderWidth;        ///< 边框宽度（内缩量）
	StyleField<float> cornerRadius;       ///< 圆角半径（0 = 直角；Phase 8 DrawRoundedRect 消费）
	StyleField<Color> pressedBackground;  ///< 按下态背景色
	StyleField<Color> hoverBackground;    ///< hover 背景色（默认 = background——无 override 零视觉变化；改 background 必须同设，否则 hover 回落主题默认）
	StyleField<TextAlignment> textAlignment;   ///< ★ Phase 30 △3：文字水平对齐（主题缺省 **Center** = Phase 30 前行为逐位一致——⚠️ 不得依赖枚举零值 Left，缺省必须显式注入 C30-3；垂直恒居中 P7 不动）
	StyleField<Color> foreground;         ///< ★ 审计 H10：Button 文字色（主题默认 **White**——白字叠蓝底是 Button 视觉身份；改此值即改按钮文字色）
};

/// @brief Button 样式运行时覆盖
/// @details ★ 审计 H10：新增 `foreground` 重载位——**双通路**（均可改按钮文字色）：
///   ① `ButtonStyleOverride.foreground`（本处）= 经 ButtonStyle 通路，语义 = 「改 Button 的
///      文字样式」，与其它 ButtonStyle 字段同批设置；
///   ② `TextStyleOverride.foreground`（经 `using TextWidget::SetStyle`）= 通用文本通路，
///      对 Button 同样有效。二者写的是**同一个** `TextStyle::m_style.foreground` 字段
///      （Button 的文字视觉唯一来源）⇒ 后写者赢（`Set()` 幂等标记 Override）。
struct ButtonStyleOverride{
	std::optional<Color> background;
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<float> cornerRadius;
	std::optional<Color> pressedBackground;
	std::optional<Color> hoverBackground;
	std::optional<TextAlignment> textAlignment;   ///< ★ Phase 30 △3：对齐覆盖
	std::optional<Color> foreground;              ///< ★ 审计 H10：文字色覆盖（与 TextStyleOverride.foreground 等价的 Button 侧入口）
};

}
