#pragma once

// ── Phase 32：窗口衬底（Frosted Backdrop）公共类型 ─────────────────────────────
// 叶子头：零依赖（同 ChromeMode.h / WindowLayer.h 惯例——Phase 30 TextAlignment.h 先例）。
// 详设：docs/dev/phase32-frosted-backdrop-detailed-design.md v1.1（D32-B 取值域 / D32-M 状态语义 / D32-Q tint 契约）。

namespace ECDI {

/// @brief 衬底模式（详设 D32-B：取值域以实测可用性为准）
/// @details 探针 1 证 `DWMWA_SYSTEMBACKDROP_TYPE` 本机不生效、探针 6 证 `BLURBEHIND(3)`
/// 出不了可见白雾 ⇒ **不收 Mica / Blur 死值**（YAGNI，条 22）；未来扩展 = 纯增量。
/// ★ **`Degraded` 只允许作为 actual 出现**：作为请求传入 `SetWindowBackdrop` ⇒
/// 记 Warning 并**完全忽略**（不改变既有请求态，详设 D32-M）。
enum class BackdropMode {
	None = 0,     ///< 不启用衬底（缺省值——清屏/提交路径逐位同 Phase 18，A6①）
	Acrylic,      ///< 亚克力雾面（材质 API 尽力启用；失败 ⇒ actual 落 Degraded）
	Degraded,     ///< 降级：半透明白不透明度 0.8（只读——不可作为请求）
};

/// @brief 衬底状态（详设 D32-C/M：请求态 vs 实际态 + applied 生命周期标记）
/// @details
/// - `requested` = 应用配置的**持久意图**（`SetWindowBackdrop` 写入；Degraded 请求被忽略）。
/// - `actual` = **最近一次应用的结果**（首次在 `Show()` 时产生；此后每次重应用更新）——
///   它是对「最近一次」的陈述，**不是**对系统未来行为的保证（A7 生命周期）。
/// - `applied` = ★ **「尚未应用」与「应用失败」的区分标记**（外部评审 P2-2）：`Show()` 前
///   恒为 `false`（此时 `actual` 无语义，**不表示**应用失败）；`Show()` 后为 `true`。
///   平台默认实现返回 `{None, None, false}` = **「该平台未实现此能力」**，而非
///   「该平台已成功应用 None」（详设 D32-M / 外部评审 P2-6）。
/// - HWND 生命周期：实际态归属当前 HWND——窗口重建/重应用后刷新，**不跨 HWND 泄漏**（C32-6）。
struct WindowBackdropState {
	BackdropMode requested = BackdropMode::None;   ///< 应用声明的持久意图
	BackdropMode actual = BackdropMode::None;      ///< 最近一次应用结果（applied == false 时无语义）
	bool applied = false;                          ///< 尚未应用（Show 前）＝ false
};

} // namespace ECDI
