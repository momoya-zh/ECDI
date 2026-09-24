#pragma once

// ── Phase 20：DPI 换算（**唯一真相源**；内部头——不进公共 API）──────────────────
// ★ 为什么**独立成件**而不挂在某个类上：它有**两个消费者**——
//   ① `Win32PlatformWindow`（窗口创建尺寸 / WM_NCHITTEST / IME / GetClientSize）
//   ② `WindowMessageHandler`（鼠标与尺寸事件的坐标翻译）
//   而 ① 的**头**已经 include 了 ②（`Win32PlatformWindow.h`）⇒ 若把换算留在 ①，
//   ② 就必须反向依赖 ① 的类（架构上"翻译器依赖窗口类"，且头层面成环）。
//   ⇒ 抽成**中立件**。
//   ★ 这是项目「**不引入投机抽象——第二个真实消费者才加**」的直接体现：
//     详设把换算放在 `Win32PlatformWindow`（当时唯一消费者，合理），
//     批三出现第二个消费者（翻译器）⇒ **此刻**才抽取（不是提前设计）。
//
// ★ 三个不变量（Phase 20 详设 §3）：
//   **G5**  —— `dpi == 96` ⇒ 两个方向**都恒等**（本阶段的零回归护栏）
//   **C9**  —— 两个方向**各自保精度，但不追求往返恒等**（dpi > 96 时物理像素网格更密
//              ⇒ 多对一 ⇒ 信息已丢失，数学上不可能恒等）
//   **C10** —— **负数必须对称舍入**（远离零）；不得用「加偏移再除」——
//              C++ 整数除法**向 0 截断**，「向零」≠「远离零」（实测负数区间 100% 偏差）
//
// ★ 与「文本测量链路」的区别（**不得合并**）：测量折回 DIP 走 `px * 96.0f / dpi`
//   （float，保精度）；本件是**坐标链路的整数口径**（框架几何全是 `int`）。见详设 §3.5。

namespace ECDI{

/// @brief 带符号的整数 half-up（**远离零**）：sign(x) · floor(|x| + 0.5)
/// @param num 分子（**可为负**——鼠标坐标在窗口外侧时为负）
/// @param den 分母（**必须 > 0**——由调用点保证）
/// @return 按「远离零」舍入的整数商
/// @details ★★ **契约 C10 的唯一落点**：本件的两个换算函数**共用本函数**，
///          **舍入只允许出现在这一处**（grep 可检）。
inline int RoundHalfAwayFromZero(long long num, long long den){

	// C10：sign(x) · floor(|x| + 0.5)——正负完全对称
	const bool negative = (num < 0);

	const long long magnitude = negative ? -num : num;

	long long q = magnitude / den;

	if (2 * (magnitude % den) >= den){

		++q;   // half-up（远离零）

	}

	return static_cast<int>(negative ? -q : q);

}

/// @brief DIP → 物理像素（★ `dpi == 96` 时恒等——G5）
/// @param dpi 目标 DPI（`<= 0` ⇒ 按 96 处理——fail-safe）
/// @details 溢出：中间量升 `long long` ⇒ **防止中间乘法溢出**（**不承诺**对最终
///          `int` 转换做全范围饱和——详设 §3.6）。
inline int DipToPixels(int dip, int dpi){

	if (dpi <= 0){

		dpi = 96;   // fail-safe：按 96 ⇒ 换算恒等（回到「DPI 未落地」的现状行为）

	}

	return RoundHalfAwayFromZero(static_cast<long long>(dip) * dpi, 96);

}

/// @brief 物理像素 → DIP（★ `dpi == 96` 时恒等——G5）
/// @param dpi 源 DPI（`<= 0` ⇒ 按 96 处理——fail-safe）
/// @note ★ **本函数不服务文本测量链路**（测量走它自己的 `px * 96.0f / dpi`，float
///       口径）——两条链路**舍入口径不同，实现时不得合并**（详设 §3.5）。
inline int PixelsToDip(int px, int dpi){

	if (dpi <= 0){

		dpi = 96;   // fail-safe

	}

	return RoundHalfAwayFromZero(static_cast<long long>(px) * 96, dpi);

}

}
