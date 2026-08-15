# Phase 6 布局系统完善 — HorizontalLayout 详细设计

> 状态：v1.0（2026-08-15）｜实现蓝图（待用户确认后实现）
> 相关：phase6-horizontallayout-requirements.md（职责确认 v1.0）/ phase6-horizontallayout-preliminary-design.md（初步设计 v1.1）

## 1. 文件改动清单

| 文件 | 改动 | 类型 |
|---|---|---|
| `ECDI/include/ECDI/Layout/HorizontalLayout.h` | 新头：HorizontalLayout : Layout（Arrange override） | 新增 |
| `ECDI/src/Layout/HorizontalLayout.cpp` | 新实现：Arrange（VerticalLayout 镜像） | 新增 |
| `ECDI/main.cpp` | 断言块（测试 1 + 测试 2）+ 交互（win1 hpanel） | 修改 |
| `ECDI/ECDI.vcxproj` | ClCompile/ClInclude 各注册新文件 | 修改 |
| `CMakeLists.txt` | 零改动（GLOB CONFIGURE_DEPENDS 自动收录） | — |

## 2. 实现步骤（顺序）

### Step 1：HorizontalLayout.h（新文件）

```cpp
#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{

/// @brief 水平布局（Phase 6：VerticalLayout 的水平镜像）
/// @details 子控件顶部对齐 + 水平流：y=0，x 从左到右累加宽度。
/// 不处理换行/溢出/spacing——Layout 只负责坐标计算（phase6-horizontallayout-requirements.md 边界原则）。
class HorizontalLayout : public Layout{

public:

	void Arrange(Widget& parent) override;

};

}
```

### Step 2：HorizontalLayout.cpp（新文件）

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

### Step 3：main.cpp 断言块（P2，放 5.5.1.3 断言块之后）

测试 1（不同宽度累加）+ 测试 2（超出父容器不裁切）——代码见初步设计 v1.1 P2，逐字照搬。

**测试 3（GPT 第三轮补充）：0/1 子控件边界**：

```cpp
// 测试 3：边界——0 子控件不崩溃 / 1 子控件归零位
{
	ECDI::Panel empty;
	empty.SetSize(200, 50);
	empty.SetLayout(std::make_unique<ECDI::HorizontalLayout>());
	empty.Arrange();   // count=0：循环不跑，不崩

	ECDI::Panel single;
	single.SetSize(200, 50);
	single.SetLayout(std::make_unique<ECDI::HorizontalLayout>());
	auto box = std::make_unique<ECDI::Widget>();
	box->SetSize(100, 30);
	auto* b = box.get();
	single.AddChild(std::move(box));
	single.Arrange();
	FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
}
```

### Step 4：main.cpp 交互（P3，win1 底部 hpanel）

代码见初步设计 v1.1 P3，逐字照搬。

### Step 5：vcxproj 注册

- `<ClCompile Include="src\Layout\HorizontalLayout.cpp" />`（Layout 区）
- `<ClInclude Include="include\ECDI\Layout\HorizontalLayout.h" />`（Layout 区）

## 3. 设计契约（GPT 第三轮补充，写死为实现约束）

**HorizontalLayout v1.0 设计契约**：

1. **Arrange() 是幂等的**——每次从 currentX=0 开始，不依赖子控件当前 Position（重复调用结果一致）
2. **Layout 完全接管子控件 Position**——预设 SetPosition 被 Arrange 覆盖忽略
3. x 从 0 开始，按 childWidth 累加
4. y 固定为 0
5. 不处理裁切
6. 不处理换行
7. 不处理滚动
8. 不处理对齐
9. 不处理 AutoSize
10. **不修改子控件尺寸**

## 4. 验收标准

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告 |
| V2 | 断言测试 1 | b2.x=100、b3.x=180（x += childWidth，不同宽） |
| V3 | 断言测试 2 | 总宽 300 > 父宽 200 时 b3.x=200（不裁切不换行） |
| V4 | 断言测试 3 | 0 子控件不崩溃；1 子控件 x=0,y=0 |
| V5 | 幂等 | Arrange 连续调两次，位置一致（契约 1） |
| V6 | 交互 | win1 底部 B1/B2/B3 水平排列；点击各按钮可聚焦（水平 HitTest 命中正确） |
| V7 | 焦点导航（细化） | Tab：B1→B2→B3；Shift+Tab：B3→B2→B1（验证 Layout→WidgetTree→FocusNext 全链） |
| V8 | 回归 | 垂直布局 panel1 不受影响（保护 Phase 5 既有行为） |

## 5. 修订记录

- v1.0（2026-08-15）详细设计定稿：5 步实现 + 5 项验收。实现按此文档逐条落地。
- v1.1（2026-08-15，GPT 第三轮）：① 新增**设计契约 10 条**（幂等/完全接管 Position/不碰尺寸等）② 加**断言测试 3**（0/1 子控件边界）③ 验收 V5→V8（幂等/交互/焦点导航细化 Tab+Shift+Tab 顺序/回归）。
