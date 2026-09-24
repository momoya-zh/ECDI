#pragma once

#include <string>

namespace ECDI
{

/// @brief 字体描述符（纯数据，平台无关——P1 撤回后的正解）
/// @details
/// 零平台资源、无方法、可值拷贝进命令；实例化（HFONT）与测量在平台层
/// （TextMeasurer 接口 / GDIBackend 实现）。
/// - size：**DIP 高度**（Phase 20 D4 起——此前语义为「GDI 像素高度」；
///         数值不变，是**语义兑现**：100% DPI 下两者 1:1，150% 下 14 DIP ⇒ 21 px）
/// - family：UTF-8；空串 = 系统默认字体（内部表达，用户无需理解）
struct Font
{
	float size = 14.0f;			///< 字号（**DIP 高度**——Phase 20 D4 起）
	std::string family;			///< 字体族（UTF-8，空 = 系统默认）
};

}
