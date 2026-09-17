#pragma once

#include <string>
#include <vector>

namespace ECDI{

/// @brief 托盘图标配置（Phase 14 R2/R3）
/// @details 值类型（可拷贝可赋值）；**公共 API 零 Win32 类型**——
/// 图标来源为 exe 资源 ID（int），HICON 的加载/持有/销毁全部收在平台层内部（D4）。
struct TrayIconOptions{

	/// @brief 图标资源 ID（exe 内 ICON 资源；默认与窗口类图标同源）
	/// @details 默认值 = kDefaultIconResourceId（与 Win32WindowClass.cpp 的 kAppIconId 一致）。
	/// ⚠️ 两侧必须同步修改（.rc 宏对 C++ 编译器不可见——cpp 侧本地常量，既有先例）。
	int iconResourceId = kDefaultIconResourceId;

	/// @brief 悬停提示文本（UTF-8；空串 = 不显示提示）
	std::string tooltip;

	/// @brief 图标资源 ID 默认值（与 ECDI.rc 的 IDI_APP 对齐）
	static constexpr int kDefaultIconResourceId = 102;
};

/// @brief 托盘菜单项（Phase 14 R5 / D6）
/// @details 只支持**一级 + 纯文本 + ID**（图标/勾选/子菜单不做——YAGNI，见需求 §5）。
/// 菜单项 `id <= 0` 由平台层忽略（保证 `ShowTrayMenu` 的 0 能安全表示「未选中」）。
struct TrayMenuItem{

	/// @brief 命令 ID（> 0；`ShowTrayMenu` 返回它）
	int id = 0;

	/// @brief 菜单项文本（UTF-8）
	std::string text;
};

/// @brief 托盘菜单（一级列表）
struct TrayMenu{

	/// @brief 菜单项（按序显示；空列表 = 不弹菜单并返回 0）
	std::vector<TrayMenuItem> items;
};

}
