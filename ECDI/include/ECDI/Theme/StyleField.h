#pragma once

namespace ECDI{

/// @brief 主题样式字段（值 + Override 标志位）
/// @details D7 契约的代码级保证：Apply() 只在 !overridden 时更新值。
/// 状态机：Set() → overridden=true（Apply 被忽略）；Reset() → 清标志（值不变，下次 Apply 可更新）。
template<typename T>
struct StyleField{
	T value{};                       ///< 当前值（默认构造）
	bool overridden = false;         ///< true = SetStyle 覆盖过

	/// @brief 运行时覆盖（标记 overridden = true）
	void Set(const T& v)             { value = v; overridden = true; }

	/// @brief 主题默认值同步（只在未 Override 时更新）
	void Apply(const T& themeValue)  { if (!overridden) value = themeValue; }

	/// @brief 清除 Override 标志（值不变，下次 Apply 可更新）
	/// @details Reset() 不恢复 Theme 值，仅清除标志——恢复 Theme 默认值需对对应字段 Reset() 后重新 ApplyTheme()。
	/// Phase 9 MVP 不提供 Widget 层 Override 清除 API（m_style 内部状态；Reset 场景通过重建 Widget 实现——YAGNI）。
	/// @note Reset() 是 StyleField 层原语，不构成 Widget 的运行时样式 API。
	/// Widget 层是否允许清除 Override 由具体 Widget API 决定——Phase 9 MVP 一律不提供（不要据此推断"Button 应该提供 ClearStyle()"）。
	void Reset()                     { overridden = false; }
};

}
