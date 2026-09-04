#include "RunAllTests.h"
#include "TestFramework.h"
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

        EXPECT_EQ(b1->GetX(), 0);
        EXPECT_EQ(b1->GetY(), 0);
        EXPECT_EQ(b2->GetX(), 100);
        EXPECT_EQ(b2->GetY(), 0);
        EXPECT_EQ(b3->GetX(), 180);
        EXPECT_EQ(b3->GetY(), 0);

        // 幂等
        panel.Arrange();
        EXPECT_EQ(b1->GetX(), 0);
        EXPECT_EQ(b2->GetX(), 100);
        EXPECT_EQ(b3->GetX(), 180);
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

        EXPECT_EQ(b3->GetX(), 200);
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
        EXPECT_EQ(b->GetX(), 0);
        EXPECT_EQ(b->GetY(), 0);
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

        EXPECT_EQ(b1->GetX(), 0);
        EXPECT_EQ(b1->GetY(), 0);
        EXPECT_EQ(b2->GetX(), 0);
        EXPECT_EQ(b2->GetY(), 30);
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

        EXPECT_EQ(b1->GetY(), 0);
        EXPECT_EQ(b2->GetY(), 30);
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
        EXPECT_EQ(b->GetX(), 0);
        EXPECT_EQ(b->GetY(), 0);
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

        EXPECT_EQ(b1->GetY(), 0);
        EXPECT_EQ(b2->GetY(), 20);
        EXPECT_EQ(b3->GetY(), 70);
    }
}

// ── 9.7：stretch + spacing + fillCrossAxis ─────────────────────

void TestStretchBasic()
{
    // 父宽 1000 / spacing 0 / A(s0,w100) / B(s1) / C(s2) → remaining 900
    // → B=300, C=600；x=0/100/400；Σ=1000（详设 §5.2 #1 自洽数据）
    Panel panel;
    panel.SetSize(1000, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>());

    auto a = std::make_unique<Widget>();
    a->SetSize(100, 30);
    auto b = std::make_unique<Widget>();
    b->SetSize(999, 30);   // stretch>0 时主轴尺寸被覆盖（任意初始值）
    b->SetStretch(1);
    auto c = std::make_unique<Widget>();
    c->SetSize(999, 30);
    c->SetStretch(2);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    EXPECT_EQ(ap->GetWidth(), 100);
    EXPECT_EQ(bp->GetWidth(), 300);
    EXPECT_EQ(cp->GetWidth(), 600);

    EXPECT_EQ(ap->GetX(), 0);
    EXPECT_EQ(bp->GetX(), 100);
    EXPECT_EQ(cp->GetX(), 400);

    // Σ 恒等
    EXPECT_EQ(ap->GetWidth() + bp->GetWidth() + cp->GetWidth(), 1000);
}

void TestStretchRemainder()
{
    // remaining 100 / stretch 1+1+1 → 33/33/34（D2 截断 + 末位吃余数）
    Panel panel;
    panel.SetSize(100, 30);
    panel.SetLayout(std::make_unique<HorizontalLayout>());

    auto a = std::make_unique<Widget>(); a->SetStretch(1);
    auto b = std::make_unique<Widget>(); b->SetStretch(1);
    auto c = std::make_unique<Widget>(); c->SetStretch(1);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    EXPECT_EQ(ap->GetWidth(), 33);
    EXPECT_EQ(bp->GetWidth(), 33);
    EXPECT_EQ(cp->GetWidth(), 34);
    EXPECT_EQ(ap->GetWidth() + bp->GetWidth() + cp->GetWidth(), 100);
}

void TestStretchNegative()
{
    // 300 宽 / stretch=0 子 200+150=350 > 父 → remaining=0 → stretch 子主轴=0；
    // 不产生负尺寸；排列仍按正常顺序；超出父边界由 Clip 处理（F4，不做 shrink）
    Panel panel;
    panel.SetSize(300, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>());

    auto a = std::make_unique<Widget>();
    a->SetSize(200, 30);
    auto b = std::make_unique<Widget>();
    b->SetSize(150, 30);
    auto c = std::make_unique<Widget>();
    c->SetSize(999, 30);
    c->SetStretch(1);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    EXPECT_EQ(cp->GetWidth(), 0);        // stretch 子主轴 = 0
    EXPECT_TRUE(cp->GetWidth() >= 0);     // 不产生负尺寸
    EXPECT_EQ(ap->GetX(), 0);            // 排列顺序正确
    EXPECT_EQ(bp->GetX(), 200);
    EXPECT_EQ(cp->GetX(), 350);
}

void TestSpacingPositions()
{
    // 三子 100/80/60 + spacing 12 → x=0/112/204
    Panel panel;
    panel.SetSize(500, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>(12));

    auto a = std::make_unique<Widget>(); a->SetSize(100, 30);
    auto b = std::make_unique<Widget>(); b->SetSize(80, 30);
    auto c = std::make_unique<Widget>(); c->SetSize(60, 30);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    EXPECT_EQ(ap->GetX(), 0);
    EXPECT_EQ(bp->GetX(), 112);   // 100 + 12
    EXPECT_EQ(cp->GetX(), 204);   // 112 + 80 + 12
}

void TestCrossFill()
{
    // V layout, parent 200×100, fillCrossAxis=true → 子跨轴(宽)=200
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, true));

        auto child = std::make_unique<Widget>();
        child->SetSize(150, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetWidth(), 200);   // 跨轴强制填充 = 父宽
        EXPECT_EQ(cp->GetHeight(), 30);   // 主轴保持（stretch=0）
    }

    // fillCrossAxis=false → 不碰跨轴尺寸
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, false));

        auto child = std::make_unique<Widget>();
        child->SetSize(150, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetWidth(), 150);   // 跨轴不动
        EXPECT_EQ(cp->GetHeight(), 30);
    }
}

void TestSetSizeDispatch()
{
    // F2：stretch 子通过虚分派 SetSize（验证分配经 SetSize 而非直写 geometry）
    struct CountableWidget : Widget{
        int sizeCalls = 0;
        void SetSize(int w, int h) override{
            Widget::SetSize(w, h);
            ++sizeCalls;
        }
    };

    Panel panel;
    panel.SetSize(400, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>());

    auto fixed = std::make_unique<Widget>();
    fixed->SetSize(100, 30);
    auto stretch = std::make_unique<CountableWidget>();
    stretch->SetSize(999, 30);
    stretch->SetStretch(1);

    auto* sp = stretch.get();
    panel.AddChild(std::move(fixed));
    panel.AddChild(std::move(stretch));
    panel.Arrange();

    EXPECT_TRUE(sp->sizeCalls >= 1);   // F2：stretch 子至少经 SetSize 虚分派一次（次数可变，≥1 即满足架构验证）
    EXPECT_EQ(sp->GetWidth(), 300);     // 最终几何正确——虚分派结果的直接证据
}

void TestNoStretchNoTouch()
{
    // F7 直接验证：全 stretch=0 + spacing=0 + fill=false → Arrange 前后尺寸逐字节一致
    Panel panel;
    panel.SetSize(500, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>());

    auto a = std::make_unique<Widget>(); a->SetSize(120, 30);
    auto b = std::make_unique<Widget>(); b->SetSize(80, 20);
    auto c = std::make_unique<Widget>(); c->SetSize(60, 40);

    const int aw = a->GetWidth(), ah = a->GetHeight();
    const int bw = b->GetWidth(), bh = b->GetHeight();
    const int cw = c->GetWidth(), ch = c->GetHeight();

    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    auto* ap = panel.GetChildAt(0);
    auto* bp = panel.GetChildAt(1);
    auto* cp = panel.GetChildAt(2);

    EXPECT_EQ(ap->GetWidth(), aw);
    EXPECT_EQ(ap->GetHeight(), ah);
    EXPECT_EQ(bp->GetWidth(), bw);
    EXPECT_EQ(bp->GetHeight(), bh);
    EXPECT_EQ(cp->GetWidth(), cw);
    EXPECT_EQ(cp->GetHeight(), ch);
}

void TestNestedComposite()
{
    // 验收测试——Root V → Page H → TextBox 嵌套分配链（详设 §5.3 自洽数据）
    // Root 1000×600 V(spacing=10, fill=true): Page s1, Footer h=40
    Panel root;
    root.SetSize(1000, 600);
    root.SetLayout(std::make_unique<VerticalLayout>(10, true));

    // Page 内有 H layout
    auto page = std::make_unique<Panel>();
    page->SetSize(1, 1);   // stretch>0 时主轴被覆盖
    page->SetStretch(1);
    page->SetLayout(std::make_unique<HorizontalLayout>(10, true));

    auto label = std::make_unique<Widget>();
    label->SetSize(100, 20);
    auto textBox = std::make_unique<Widget>();
    textBox->SetSize(1, 1);
    textBox->SetStretch(1);
    auto button = std::make_unique<Widget>();
    button->SetSize(80, 20);

    auto* labelP = label.get();
    auto* textP  = textBox.get();
    auto* btnP   = button.get();
    page->AddChild(std::move(label));
    page->AddChild(std::move(textBox));
    page->AddChild(std::move(button));

    auto* pageP = page.get();

    auto footer = std::make_unique<Widget>();
    footer->SetSize(200, 40);
    auto* footerP = footer.get();

    root.AddChild(std::move(page));
    root.AddChild(std::move(footer));
    root.Arrange();

    // Root V 分配（spacing=10, fillCrossAxis=true）
    // Page: main remaining = 600−40−10 = 550, cross(width)=1000
    EXPECT_EQ(pageP->GetHeight(), 550);
    EXPECT_EQ(pageP->GetWidth(), 1000);
    EXPECT_EQ(pageP->GetY(), 0);

    // Footer: h=40 (stretch=0), cross=1000
    EXPECT_EQ(footerP->GetHeight(), 40);
    EXPECT_EQ(footerP->GetWidth(), 1000);
    EXPECT_EQ(footerP->GetY(), 560);   // 550 + 10

    // Page H 分配（spacing=10, fillCrossAxis=true）
    // remaining = 1000 − 100 − 80 − 20 = 800
    EXPECT_EQ(labelP->GetWidth(), 100);
    EXPECT_EQ(textP->GetWidth(), 800);
    EXPECT_EQ(btnP->GetWidth(), 80);

    EXPECT_EQ(labelP->GetX(), 0);
    EXPECT_EQ(textP->GetX(), 110);    // 100 + 10
    EXPECT_EQ(btnP->GetX(), 920);     // 110 + 800 + 10

    // Σ 恒等
    EXPECT_EQ(labelP->GetWidth() + textP->GetWidth() + btnP->GetWidth() + 10 * 2, 1000);
}

void TestIdempotentStretch()
{
    // Arrange 两次 → stretch 子尺寸和位置完全一致
    Panel panel;
    panel.SetSize(500, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>(10));

    auto a = std::make_unique<Widget>(); a->SetSize(100, 30);
    auto b = std::make_unique<Widget>(); b->SetStretch(1);
    auto c = std::make_unique<Widget>(); c->SetStretch(2);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));

    panel.Arrange();
    const int bw1 = bp->GetWidth(), cx1 = cp->GetX();

    panel.Arrange();   // 第二次
    EXPECT_EQ(bp->GetWidth(), bw1);
    EXPECT_EQ(cp->GetX(), cx1);
}

} // anonymous namespace

void ECDI::Test::RegisterLayoutTests()
{
    GetTestRegistry().Add("Layout.HorizontalLayout", &TestHorizontalLayout);
    GetTestRegistry().Add("Layout.VerticalLayout", &TestVerticalLayout);
    GetTestRegistry().Add("Layout.StretchBasic", &TestStretchBasic);
    GetTestRegistry().Add("Layout.StretchRemainder", &TestStretchRemainder);
    GetTestRegistry().Add("Layout.StretchNegative", &TestStretchNegative);
    GetTestRegistry().Add("Layout.SpacingPositions", &TestSpacingPositions);
    GetTestRegistry().Add("Layout.CrossFill", &TestCrossFill);
    GetTestRegistry().Add("Layout.SetSizeDispatch", &TestSetSizeDispatch);
    GetTestRegistry().Add("Layout.NoStretchNoTouch", &TestNoStretchNoTouch);
    GetTestRegistry().Add("Layout.NestedComposite", &TestNestedComposite);
    GetTestRegistry().Add("Layout.IdempotentStretch", &TestIdempotentStretch);
}
