#pragma once

#include <functional>

namespace ECDI {

class Widget;

/// @brief 浮层所属层级（层间顺序严格满足 Tooltip > Modal > Normal > Content）
enum class OverlayLayer {
    Normal = 0,  ///< 普通交互浮层（下拉列表、气泡弹窗、右键菜单）
    Modal = 1,   ///< 模态浮层（全屏半透明遮罩、居中对话框、设置抽屉）
    Tooltip = 2  ///< 提示浮层（纯视觉呈现，恒定置顶，默认不参与命中）
};

/// @brief 点击浮层外部时的关闭与事件分派策略
enum class LightDismissPolicy {
    PassThrough = 0, ///< 关闭浮层，且点击事件继续向下派发给底层命中控件（默认）
    Consume = 1,     ///< 关闭浮层，并拦截吞噬该点击事件，不向底层派发
    None = 2         ///< 禁用点外关闭（强模态对话框）
};

/// @brief 浮层相对锚点控件的展开方向
enum class AnchorPlacement {
    Below = 0, ///< 锚点正下方（默认）
    Above = 1, ///< 锚点正上方
    Left = 2,  ///< 锚点正左侧
    Right = 3  ///< 锚点正右侧
};

/// @brief 浮层显示配置选项
struct OverlayOptions {
    OverlayLayer layer = OverlayLayer::Normal;
    LightDismissPolicy lightDismiss = LightDismissPolicy::PassThrough;
    bool closeOnEsc = true;                   ///< 是否支持按 ESC 键关闭
    const Widget* exemptWidget = nullptr;     ///< 豁免控件（通常为触发浮层的 EntryButton）
    const Widget* anchorWidget = nullptr;     ///< 跟随的锚点控件（开启锚点跟随定位）
    const Widget* ownerOverlay = nullptr;     ///< 逻辑父浮层（嵌套浮层级联关闭约束）
    AnchorPlacement placement = AnchorPlacement::Below;
    std::function<void()> onClosed = nullptr; ///< 浮层完全关闭脱树后的回调
};

} // namespace ECDI
