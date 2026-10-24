#pragma once

// ════════════════════════════════════════════════════════════════════════
// Phase 36（C36-1/C36-2）：字重规则共享件——**内部头**（不入 include/ECDI/，非公共 API）。
// 批一只落 ClampWeight（GDI 两处消费边界）；PickNearestTier（GL 档位归并纯函数）
// 随批二与其消费者（FontEngine）及真值表测试同批落地——条 95：不超前引入无消费者的代码。
// ════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cstddef>
#include <vector>
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

/// @brief 档位归并（C36-2 第 3 层）：|w-request| 最小者；**平手取轻**（w 小者）；再平手取下标小者。
/// @details 与 GDI 映射器实测语义一致（探针 1：550→400 / 560→700，边界 ≈555——
///          ★ 探针定位为该规则的**验证样本**而非契约来源，Phase 31 铁律）。
///          纯函数无 FT 依赖 ⇒ T36-3 真值表零环境。
/// @param tiers 存活档位（候选过滤后的 face `OS/2.usWeightClass` 集合，**非空**）
/// @return 选中档位的**下标**（P3-⑮：`WeightTier` 结构已删，直接下标）
inline std::size_t PickNearestTier(const std::vector<std::uint16_t>& tiers, std::uint16_t request)
{
	std::size_t best = 0;
	for (std::size_t i = 1; i < tiers.size(); ++i)
	{
		const unsigned dNew = (tiers[i] > request) ? tiers[i] - request : request - tiers[i];
		const unsigned dBest = (tiers[best] > request) ? tiers[best] - request : request - tiers[best];
		if (dNew < dBest || (dNew == dBest && tiers[i] < tiers[best]))
		{
			best = i;
		}
	}
	return best;
}

}
