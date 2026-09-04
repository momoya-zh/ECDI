#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Widget/CollapsiblePanel.h"

#include <memory>

using namespace ECDI;

namespace {

// ── 无窗口场景（详细设计 §5：SetExpanded 无 Window 时瞬时切换——测试不依赖平台/动画）──

// 1. 默认状态：构造即收起（2026-08-30 v1.2 变更）、方向默认 Down、内容初始隐藏
void TestDefaultCollapsed()
{
    CollapsiblePanel panel;
    EXPECT_FALSE(panel.IsExpanded());
    EXPECT_EQ(panel.GetExpandDirection(), ExpandDirection::Down);
    EXPECT_FALSE(panel.GetContent()->IsVisible());
}

// 2. Down 折叠：h=0、y 不变、内容 invisible、容器内子控件不命中
//（v1.2 默认收起下：SetSize 即呈现收起，SetExpanded(false) 幂等 no-op——断言不变）
void TestDownCollapse()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.GetContent()->AddChild([] {
        auto child = std::make_unique<Widget>();
        child->SetPosition(10, 10);
        child->SetSize(50, 30);
        return child;
    }());

    panel.SetExpanded(false);

    EXPECT_FALSE(panel.IsExpanded());
    EXPECT_EQ(panel.GetWidth(), 300);
    EXPECT_EQ(panel.GetHeight(), 0);
    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetY(), 200);               // top 锚定，y 恒定
    EXPECT_FALSE(panel.GetContent()->IsVisible());
    EXPECT_EQ(panel.HitTest(15, 15), nullptr);  // 内容 invisible → 子树跳过命中
}

// 3. Up 折叠：h=0、底边保持（y 变为 y0+h0）——方向须先于 SetSize（初始化约定，v1.2）
void TestUpCollapse()
{
    CollapsiblePanel panel;
    panel.SetExpandDirection(ExpandDirection::Up);
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);                   // 已收起——幂等 no-op

    EXPECT_EQ(panel.GetHeight(), 0);
    EXPECT_EQ(panel.GetY(), 200 + 400);         // bottom 锚定：底边 y+h 恒为 600
    EXPECT_FALSE(panel.GetContent()->IsVisible());
}

// 4. Right 折叠：w=0、x 不变（left 锚定）——方向须先于 SetSize（初始化约定，v1.2）
void TestRightCollapse()
{
    CollapsiblePanel panel;
    panel.SetExpandDirection(ExpandDirection::Right);
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);                   // 已收起——幂等 no-op

    EXPECT_EQ(panel.GetWidth(), 0);
    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetHeight(), 400);          // 非动画轴：高度保持 SetSize 给出值（硬契约）
    EXPECT_FALSE(panel.GetContent()->IsVisible());
}

// 5. Left 折叠：w=0、右边保持（x 变为 x0+w0）——方向须先于 SetSize（初始化约定，v1.2）
void TestLeftCollapse()
{
    CollapsiblePanel panel;
    panel.SetExpandDirection(ExpandDirection::Left);
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);                   // 已收起——幂等 no-op

    EXPECT_EQ(panel.GetWidth(), 0);
    EXPECT_EQ(panel.GetX(), 100 + 300);         // right 锚定：右边 x+w 恒为 400
    EXPECT_FALSE(panel.GetContent()->IsVisible());
}

// 6. 展开恢复：几何回 m_expandedRect、内容可见
void TestExpandRestoresRect()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);
    panel.SetExpandDirection(ExpandDirection::Up);

    panel.SetExpanded(false);                   // 折叠（记 100,200,300,400）
    EXPECT_EQ(panel.GetHeight(), 0);

    panel.SetExpanded(true);                    // 展开回基准

    EXPECT_TRUE(panel.IsExpanded());
    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetY(), 200);
    EXPECT_EQ(panel.GetWidth(), 300);
    EXPECT_EQ(panel.GetHeight(), 400);
    EXPECT_TRUE(panel.GetContent()->IsVisible());
}

// 7. 外部改尺寸后重新记忆（冻结点 2 语义）：折叠记 A → 展开 → SetSize B → 再折叠 → 展开回 B
void TestExpandAfterResize()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);                   // 记 A (100,200,300,400)
    panel.SetExpanded(true);                    // 回 A

    panel.SetSize(500, 600);                    // 展开态改尺寸 → B

    panel.SetExpanded(false);                   // 再折叠：记 B (100,200,500,600)
    panel.SetExpanded(true);                    // 展开回 B

    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetY(), 200);
    EXPECT_EQ(panel.GetWidth(), 500);
    EXPECT_EQ(panel.GetHeight(), 600);
}

// 8. 幂等：同状态重复 SetExpanded 无副作用
void TestIdempotent()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);
    panel.SetExpanded(false);                   // 二次折叠：no-op

    EXPECT_FALSE(panel.IsExpanded());
    EXPECT_EQ(panel.GetHeight(), 0);

    panel.SetExpanded(true);
    panel.SetExpanded(true);                    // 二次展开：no-op

    EXPECT_TRUE(panel.IsExpanded());
    EXPECT_EQ(panel.GetHeight(), 400);
}

// 9. Toggle 翻转状态（默认收起——首次 Toggle = 展开，2026-08-30 v1.2）
void TestToggleFlipsState()
{
    CollapsiblePanel panel;
    panel.SetSize(300, 400);

    panel.Toggle();
    EXPECT_TRUE(panel.IsExpanded());
    EXPECT_EQ(panel.GetHeight(), 400);

    panel.Toggle();
    EXPECT_FALSE(panel.IsExpanded());
    EXPECT_EQ(panel.GetHeight(), 0);
}

// 10. 内容容器：非空、可 AddChild、展开态 SetSize 后容器尺寸跟随
//（收起态容器动画轴为 0——尺寸断言须在展开态查，2026-08-30 v1.2）
void TestContentContainer()
{
    CollapsiblePanel panel;
    panel.SetSize(300, 400);

    EXPECT_NE(panel.GetContent(), nullptr);
    EXPECT_TRUE(panel.GetContent()->GetParent() != nullptr);

    panel.GetContent()->AddChild(std::make_unique<Widget>());
    EXPECT_EQ(panel.GetContent()->GetChildCount(), 1);

    panel.SetExpanded(true);                    // 展开态：容器跟随面板全尺寸
    panel.SetSize(500, 600);
    EXPECT_EQ(panel.GetContent()->GetWidth(), 500);
    EXPECT_EQ(panel.GetContent()->GetHeight(), 600);
}

// 11. 折叠后面板自身命中失效（2026-08-30：Panel 永不命中——ContainsPoint 恒 false；折叠后 h=0 双重不命中）
void TestCollapseHitTestPanelSelf()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);                   // 已收起——幂等 no-op

    EXPECT_EQ(panel.HitTest(150, 250), nullptr);   // 面板自身区域不可命中
}

// 12. 内容跟随几何（GPT 建议）：panel 几何 → content pos (0,0) size 同步；SetSize 后跟随
//（几何断言在展开态查——收起态容器动画轴为 0，2026-08-30 v1.2）
void TestContentFollowsGeometry()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(true);

    Widget* content = panel.GetContent();
    EXPECT_EQ(content->GetX(), 0);
    EXPECT_EQ(content->GetY(), 0);
    EXPECT_EQ(content->GetWidth(), 300);
    EXPECT_EQ(content->GetHeight(), 400);

    panel.SetSize(500, 600);
    EXPECT_EQ(content->GetX(), 0);
    EXPECT_EQ(content->GetY(), 0);
    EXPECT_EQ(content->GetWidth(), 500);
    EXPECT_EQ(content->GetHeight(), 600);
}

// 13. 多次折叠记忆语义（GPT 建议）：折叠→展开→折叠→展开，基准保持「最近一次进入折叠前」
void TestRepeatedCollapseMemory()
{
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    panel.SetExpanded(false);                   // 折叠：记 A
    panel.SetExpanded(true);                    // 回 A
    panel.SetExpanded(false);                   // 再折叠：记 A（几何未变）
    panel.SetExpanded(true);                    // 回 A

    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetY(), 200);
    EXPECT_EQ(panel.GetWidth(), 300);
    EXPECT_EQ(panel.GetHeight(), 400);
}

// 14. 默认收起呈现 + 硬契约（phase9.6-panel-container-semantics v1.1 §3.3）：
// 收起态 SetSize = 定义展开基准；非动画轴保持给出值、动画轴呈现 0
void TestDefaultCollapsedPresentation()
{
    // Down：宽 = w（300，非动画轴保持）、高呈现 0（动画轴）
    CollapsiblePanel panel;
    panel.SetPosition(100, 200);
    panel.SetSize(300, 400);

    EXPECT_FALSE(panel.IsExpanded());
    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetY(), 200);
    EXPECT_EQ(panel.GetWidth(), 300);
    EXPECT_EQ(panel.GetHeight(), 0);
    EXPECT_FALSE(panel.GetContent()->IsVisible());

    // 展开恢复基准全尺寸 + 内容可见
    panel.SetExpanded(true);
    EXPECT_TRUE(panel.IsExpanded());
    EXPECT_EQ(panel.GetX(), 100);
    EXPECT_EQ(panel.GetY(), 200);
    EXPECT_EQ(panel.GetWidth(), 300);
    EXPECT_EQ(panel.GetHeight(), 400);
    EXPECT_TRUE(panel.GetContent()->IsVisible());

    // Right 对称断言：高 = h（400，非动画轴保持）、宽呈现 0（动画轴）
    CollapsiblePanel p2;
    p2.SetExpandDirection(ExpandDirection::Right);
    p2.SetPosition(50, 60);
    p2.SetSize(300, 400);

    EXPECT_FALSE(p2.IsExpanded());
    EXPECT_EQ(p2.GetHeight(), 400);
    EXPECT_EQ(p2.GetWidth(), 0);
    EXPECT_EQ(p2.GetX(), 50);                   // left 锚定，x 恒定
}

} // anonymous namespace

void ECDI::Test::RegisterCollapsiblePanelTests()
{
    GetTestRegistry().Add("CollapsiblePanel.DefaultCollapsed",     &TestDefaultCollapsed);
    GetTestRegistry().Add("CollapsiblePanel.DownCollapse",          &TestDownCollapse);
    GetTestRegistry().Add("CollapsiblePanel.UpCollapse",            &TestUpCollapse);
    GetTestRegistry().Add("CollapsiblePanel.RightCollapse",         &TestRightCollapse);
    GetTestRegistry().Add("CollapsiblePanel.LeftCollapse",          &TestLeftCollapse);
    GetTestRegistry().Add("CollapsiblePanel.ExpandRestoresRect",    &TestExpandRestoresRect);
    GetTestRegistry().Add("CollapsiblePanel.ExpandAfterResize",     &TestExpandAfterResize);
    GetTestRegistry().Add("CollapsiblePanel.Idempotent",            &TestIdempotent);
    GetTestRegistry().Add("CollapsiblePanel.ToggleFlipsState",      &TestToggleFlipsState);
    GetTestRegistry().Add("CollapsiblePanel.ContentContainer",      &TestContentContainer);
    GetTestRegistry().Add("CollapsiblePanel.CollapseHitTestPanelSelf", &TestCollapseHitTestPanelSelf);
    GetTestRegistry().Add("CollapsiblePanel.ContentFollowsGeometry",   &TestContentFollowsGeometry);
    GetTestRegistry().Add("CollapsiblePanel.RepeatedCollapseMemory",   &TestRepeatedCollapseMemory);
    GetTestRegistry().Add("CollapsiblePanel.DefaultCollapsedPresentation", &TestDefaultCollapsedPresentation);
}
