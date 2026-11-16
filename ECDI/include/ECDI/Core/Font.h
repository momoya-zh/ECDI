#pragma once

#include <string>
#include <cstdint>

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
	std::uint16_t weight = 400;		///< 字重（**0–1000 刻度** = GDI `lfWeight` 同度量；默认 400 = 常规体 = 既有行为逐位不变）。
								///< 语义（Phase 36 C36-2）＝**族内最近字面、平手取轻，不跨族**——GDI 由字体映射器实现（探针验证样本），GL 按 face 的 `OS/2.usWeightClass` 档位归并。
								///< 族名内嵌样式（`"Consolas Bold"`）优先于本字段（C36-3）；消费边界 clamp（>1000 归一，C36-1）。
								///< 族内无更重真字面且 `weight >= 551` 时 **GDI 与 GL/FreeType 均水平合成加粗**（Phase 46 D46-1；`551..699` 真粗体族仍沿 `PickNearestTier` 分档）。★ 声明序：必须位于 `family` 之后（C7560）。
	bool italic = false;			///< 斜体（默认 false = 既有行为逐位不变）。
								///< 语义（Phase 45/46）：GDI = LOGFONT `lfItalic`；GL = 优先选 `OS/2.fsSelection` bit0 的真斜体面，**族内无真斜体面时按 `B -> O` 合成斜切并在回落链保留合成标记**
								///< （Phase 46 D46-2/D46-3；斜切及粗体左/右探出由控件 `PushClip` 裁剪）。★ 声明序：必须位于 `weight` 之后。
};

}
