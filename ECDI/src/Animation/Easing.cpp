#include "ECDI/Animation/Easing.h"

#include "ECDI/Core/ECDIAssert.h"

#include <algorithm>

namespace ECDI{

float ApplyEasing(Easing easing, float t) noexcept{

	// ★★ 审计 L4：**函数自身强制值域**（此前不 clamp ⇒ `EaseIn(2) == 4`）。
	//   修复前契约分裂：头注释承诺 `t ∈ [0,1] → eased ∈ [0,1]`，而 `@param t`
	//   却写「调用方负责 clamp」——两边都没强制，越界时**承诺静默失效**。
	//   ★ **零行为变化**：生产路径的 t 恒在 [0,1]（`Detail::NormalizeProgress`
	//     `Animation.h:49` 已 `std::clamp(t, 0.0, 1.0)`）⇒ 本 clamp 对既有调用逐位无影响；
	//     它让**直接调用本函数**的其它消费者（含未来）也拿到契约承诺的值域。
	//   ★ 与条 134 同族判据：值域保证属**正确性契约**，不是"顺手优化"。
	const float clamped = (std::max)(0.0f, (std::min)(1.0f, t));

	switch (easing){

	case Easing::Linear:	return clamped;
	case Easing::EaseIn:	return clamped * clamped;
	case Easing::EaseOut:	return 1.0f - (1.0f - clamped) * (1.0f - clamped);
	case Easing::EaseInOut:	return clamped < 0.5f
		                        ? 2.0f * clamped * clamped
		                        : 1.0f - 2.0f * (1.0f - clamped) * (1.0f - clamped);
	default:
		// ★ 审计 L4：`default` 不再静默回落 Linear——**掩盖未来枚举**。
		//   与 `EventRouter.cpp:136`（M21 穷尽性收口）同族：新枚举必须被编译期/运行期看见。
		//   ⚠️ `noexcept` 函数内不能抛 ⇒ 用断言暴露；Release 下落点取**最保守的 Linear**
		//      （恒等映射：至少不会把动画值域放大到承诺之外）。
		FRAMEWORK_ASSERT(false && "unknown Easing enum");
		return clamped;

	}

}

}


