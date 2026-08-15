#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{

/// @brief 水平布局（Phase 6：VerticalLayout 的水平镜像）
/// @details 子控件顶部对齐 + 水平流：y=0，x 从左到右累加宽度。
/// 不处理换行/溢出/spacing——Layout 只负责坐标计算（phase6-horizontallayout-requirements.md 边界原则）。
/// 设计契约（详细设计 v1.1）：Arrange 幂等 / 完全接管 Position / 不修改子控件尺寸。
class HorizontalLayout : public Layout{

public:

    void Arrange(Widget& parent) override;

};

}
