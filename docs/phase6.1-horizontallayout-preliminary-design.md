# Phase 6 布局系统完善 — HorizontalLayout 初步设计

> 状态：v1.0（2026-08-15）｜初步设计（待用户确认后实现）
> 相关：phase6.1-horizontallayout-requirements.md（职责确认 v1.0）/ VerticalLayout（镜像基准）

## 1. 定稿决策（P1-P5）

### P1 HorizontalLayout.h/cpp —— A ✅（VerticalLayout 水平镜像）

**同构约束（GPT 2026-08-15）**：HorizontalLayout.cpp 与 VerticalLayout.cpp 的 diff **尽可能只出现 y→x、height→width**——除变量名外零逻辑差异。收益：① VerticalLayout 修 bug 时 HorizontalLayout 同步可见 ② 未来 Wrap/Grid/Flex 出现时两者天然成 LinearLayout 抽象模板。

**新头** `Layout/HorizontalLayout.h`（与 VerticalLayout.h 同构）：

```cpp
#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{

/// @brief 水平布局（Phase 6：VerticalLayout 的水平镜像）
/// @details 子控件顶部对齐 + 水平流：y=0，x 从左到右累加宽度。
/// 不处理换行/溢出/spacing——Layout 只负责坐标计算（phase6.1-horizontallayout-requirements.md 边界原则）。
class HorizontalLayout : public Layout{

public:

	void Arrange(Widget& parent) override;

};

}
```

**新实现** `src/Layout/HorizontalLayout.cpp`（与 VerticalLayout.cpp 镜像——差异一行代码：y→x）：

```cpp
#include "ECDI/Layout/HorizontalLayout.h"

#include "ECDI/Widget/Widget.h"

namespace ECDI{

void HorizontalLayout::Arrange(Widget& parent){

	int currentX = 0;

	size_t count = parent.GetChildCount();

	for (size_t i = 0; i < count; i++){

		Widget* child = parent.GetChildAt(i);

		child->SetPosition(currentX, 0);   // 顶部对齐（y=0，决策 a）+ 水平流

		currentX += child->GetWidth();     // 累加宽度（镜像 VerticalLayout 的 y += GetHeight()）

	}

}

}
```

### P2 main.cpp 断言 —— A ✅（可编程断言，不依赖窗口）

```cpp
// ── Phase 6 HorizontalLayout：Arrange 后子控件位置断言 ──
{
	ECDI::Panel panel;
	panel.SetSize(300, 50);
	panel.SetLayout(std::make_unique<ECDI::HorizontalLayout>());

	auto box1 = std::make_unique<ECDI::Widget>();
	box1->SetSize(100, 30);
	auto box2 = std::make_unique<ECDI::Widget>();
	box2->SetSize(80, 30);
	auto box3 = std::make_unique<ECDI::Widget>();
	box3->SetSize(60, 30);
	auto* b1 = box1.get();
	auto* b2 = box2.get();
	auto* b3 = box3.get();

	panel.AddChild(std::move(box1));
	panel.AddChild(std::move(box2));
	panel.AddChild(std::move(box3));
	panel.Arrange();

	FRAMEWORK_ASSERT(b1->GetX() == 0    && b1->GetY() == 0);
	FRAMEWORK_ASSERT(b2->GetX() == 100  && b2->GetY() == 0);   // 累加宽度（不同宽 80）
	FRAMEWORK_ASSERT(b3->GetX() == 180  && b3->GetY() == 0);   // 100+80——验证 x += childWidth 非固定步长
}

// 测试 2（GPT 补充）：超出父容器——Layout 不裁切不换行（边界原则验证）
{
	ECDI::Panel panel;
	panel.SetSize(200, 50);   // 父宽 200
	panel.SetLayout(std::make_unique<ECDI::HorizontalLayout>());

	auto box1 = std::make_unique<ECDI::Widget>();
	box1->SetSize(100, 30);
	auto box2 = std::make_unique<ECDI::Widget>();
	box2->SetSize(100, 30);
	auto box3 = std::make_unique<ECDI::Widget>();
	box3->SetSize(100, 30);
	auto* b3 = box3.get();

	panel.AddChild(std::move(box1));
	panel.AddChild(std::move(box2));
	panel.AddChild(std::move(box3));
	panel.Arrange();

	FRAMEWORK_ASSERT(b3->GetX() == 200);   // 总宽 300 > 父宽 200——仍放 x=200（不裁切不换行，坐标照算）
}
```

### P3 main.cpp 交互 —— A ✅（demo 窗口可见水平排列）

窗口 1（win1）加水平 panel（panel1 下方，垂直布局旁对照）：

```cpp
// Phase 6：水平布局示例（HorizontalLayout 交互验证——与 panel1 垂直布局对照）
auto hpanel = std::make_unique<ECDI::Panel>();
hpanel->SetPosition(50, 300);
hpanel->SetSize(300, 50);
hpanel->SetLayout(std::make_unique<ECDI::HorizontalLayout>());

auto hb1 = std::make_unique<DemoButton>("B1");
hb1->SetSize(100, 40);
auto hb2 = std::make_unique<DemoButton>("B2");
hb2->SetSize(100, 40);
auto hb3 = std::make_unique<DemoButton>("B3");
hb3->SetSize(100, 40);

hpanel->AddChild(std::move(hb1));
hpanel->AddChild(std::move(hb2));
hpanel->AddChild(std::move(hb3));

win1.GetRootWidget().AddChild(std::move(hpanel));
win1.GetRootWidget().Arrange();   // 已有一处，顺带覆盖新 panel
```

（win1 500x400：panel1 占 50,30-350,280；hpanel 放 50,300-350,350，不重叠。）

### P4 构建 —— A ✅

- **vcxproj**：注册 HorizontalLayout.h + HorizontalLayout.cpp（两个 ItemGroup）
- **CMakeLists**：零改动（`GLOB src/*.cpp` + CONFIGURE_DEPENDS 自动收录）

### P5 验证 —— A ✅

- 编译（VS 优先 + 三工具链惯例）
- 断言段通过（P2 三行位置断言）
- 交互：win1 底部三个按钮水平排列（B1 B2 B3 横排），点击可聚焦（HitTest 验证水平坐标命中）

## 2. 修订记录

- v1.0（2026-08-15）初步设计定稿：P1-P5。HorizontalLayout 为 VerticalLayout 纯镜像（差异一行代码）；断言/交互/构建三路验证。
- v1.1（2026-08-15，GPT 评审）：① P1 加 **diff 同构约束**（仅 y→x / height→width，维护可同步 + 未来 LinearLayout 抽象模板）② P2 加**边界测试 2**（超出父容器：总宽 300 > 父宽 200，第 3 子仍放 x=200——验证 Layout 不裁切不换行原则）③ P4 记未来项：CMake 自动生成 VS 工程（2.x 后，减少 vcxproj 手工维护）。
