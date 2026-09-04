#pragma once

#include "ECDI/Widget/Panel.h"

#include <memory>
#include <vector>

namespace ECDI{

class Button;

namespace Demo{

/// @brief Showcase 导航栏（9.6 demo 代码——不入框架，CollapsiblePanelDemo 先例）
/// @details panel = 导航面板（VerticalLayout：标题 + 6 页签，卡片底）；
/// buttons = 6 个页签按钮裸指针——与页面索引一一对应
/// （调用方在 panel 入树前挂 SetOnClick；按钮生命周期 = 面板 = 窗口，树内地址稳定）。
struct ShowcaseNav{
	std::unique_ptr<Panel> panel;    ///< 导航面板（调用方 AddChild 到 RootWidget）
	std::vector<Button*> buttons;    ///< 6 个页签按钮（按页序：Buttons/Input/Selection/Containers/Animation/Rendering）
};

/// @brief 构建左侧导航栏（标题 + 6 页签；VerticalLayout 垂直排布）
ShowcaseNav BuildNavPanel(int width, int height);

/// @brief 构建 Showcase 页面（预建 Panel——全部 AddChild 到内容区同一区域，SetVisible 切换）
/// @param width 页面宽（= 内容区宽）
/// @param height 页面高（= 内容区高）
/// @return 预建页面 Panel（默认 visible——调用方把非当前页 SetVisible(false)）
std::unique_ptr<Panel> BuildButtonsPage(int width, int height);
std::unique_ptr<Panel> BuildInputPage(int width, int height);
std::unique_ptr<Panel> BuildSelectionPage(int width, int height);
std::unique_ptr<Panel> BuildContainersPage(int width, int height);
std::unique_ptr<Panel> BuildAnimationPage(int width, int height);
std::unique_ptr<Panel> BuildRenderingPage(int width, int height);

}
}
