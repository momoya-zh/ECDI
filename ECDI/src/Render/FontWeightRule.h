#pragma once

// ════════════════════════════════════════════════════════════════════════
// Phase 36（C36-1/C36-2）：字重规则共享件——**内部头**（不入 include/ECDI/，非公共 API）。
// 批一只落 ClampWeight（GDI 两处消费边界）；PickNearestTier（GL 档位归并纯函数）
// 随批二与其消费者（FontEngine）及真值表测试同批落地——条 95：不超前引入无消费者的代码。
// ════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cstdint>

namespace ECDI
{

/// @brief 字重消费边界 clamp（C36-1）：0–1000 刻度，>1000 归一。
/// @details 消费点两处同式：GDI `lfWeight` 前（批一）/ `FontEngine` 档位归并前（批二）。
///          ★ 缓存键存 `Font::weight` **原值**——clamp 只作用于消费，用户改回合法值不残留污染。
inline std::uint16_t ClampWeight(std::uint32_t weight)
{
	return static_cast<std::uint16_t>((std::min)(weight, 1000u));   // (std::min)：本头会被 windows.h 之后的 TU 包含，防 min 宏污染
}

}
