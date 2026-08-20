#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include <memory>
#include <utility>

using namespace ECDI;

namespace {

void TestHorizontalLayout()
{
    // ── Phase 6 原 #8：HorizontalLayout（迁移）──

    // 测试 1：不同宽度累加
    {
        Panel panel;
        panel.SetSize(300, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(80, 30);
        auto box3 = std::make_unique<Widget>();
        box3->SetSize(60, 30);
        auto* b1 = box1.get();
        auto* b2 = box2.get();
        auto* b3 = box3.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.AddChild(std::move(box3));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetX() == 0    && b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetX() == 100  && b2->GetY() == 0);
        FRAMEWORK_ASSERT(b3->GetX() == 180  && b3->GetY() == 0);

        // 幂等
        panel.Arrange();
        FRAMEWORK_ASSERT(b1->GetX() == 0 && b2->GetX() == 100 && b3->GetX() == 180);
    }

    // 测试 2：超出父容器
    {
        Panel panel;
        panel.SetSize(200, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 30);
        auto box3 = std::make_unique<Widget>();
        box3->SetSize(100, 30);
        auto* b3 = box3.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.AddChild(std::move(box3));
        panel.Arrange();

        FRAMEWORK_ASSERT(b3->GetX() == 200);
    }

    // 测试 3：边界——0 子控件 / 1 子控件
    {
        Panel empty;
        empty.SetSize(200, 50);
        empty.SetLayout(std::make_unique<HorizontalLayout>());
        empty.Arrange();

        Panel single;
        single.SetSize(200, 50);
        single.SetLayout(std::make_unique<HorizontalLayout>());
        auto box = std::make_unique<Widget>();
        box->SetSize(100, 30);
        auto* b = box.get();
        single.AddChild(std::move(box));
        single.Arrange();
        FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
    }
}

void TestVerticalLayout()
{
    // ── T6：VerticalLayout 补测 ──

    // 两个子节点
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 40);
        auto* b1 = box1.get();
        auto* b2 = box2.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetX() == 0 && b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetX() == 0 && b2->GetY() == 30);
    }

    // 幂等性
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 40);
        auto* b1 = box1.get();
        auto* b2 = box2.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.Arrange();
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetY() == 30);
    }

    // 0 子节点
    {
        Panel empty;
        empty.SetSize(200, 100);
        empty.SetLayout(std::make_unique<VerticalLayout>());
        empty.Arrange();
    }

    // 1 子节点
    {
        Panel single;
        single.SetSize(200, 100);
        single.SetLayout(std::make_unique<VerticalLayout>());
        auto box = std::make_unique<Widget>();
        box->SetSize(100, 50);
        auto* b = box.get();
        single.AddChild(std::move(box));
        single.Arrange();
        FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
    }

    // 不同高度累加
    {
        Panel panel;
        panel.SetSize(200, 200);
        panel.SetLayout(std::make_unique<VerticalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 20);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 50);
        auto box3 = std::make_unique<Widget>();
        box3->SetSize(100, 30);
        auto* b1 = box1.get();
        auto* b2 = box2.get();
        auto* b3 = box3.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.AddChild(std::move(box3));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetY() == 20);
        FRAMEWORK_ASSERT(b3->GetY() == 70);
    }
}

} // anonymous namespace

void ECDI::Test::RunLayoutTests()
{
    TestHorizontalLayout();
    TestVerticalLayout();
}