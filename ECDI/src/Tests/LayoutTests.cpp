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

// ── 17：padding（新增 8 用例——详设 v1.1 §5.2）──────────────────────────

void TestPaddingDefaultZero()
{
    // 17 T17-1（**守门用例**）：padding = 0 **显式传参** ⇒ 与既有场景期望值逐位相同（契约 C1）
    // 三个块分别重跑既有 Layout.SpacingPositions / Layout.CrossFill / Layout.StretchBasic 的场景。

    // 块 A：对齐 Layout.SpacingPositions（H(12) 500×50，三子 100/80/60）
    {
        Panel panel;
        panel.SetSize(500, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>(12, false, 0));

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

    // 块 B：对齐 Layout.CrossFill（V(0,true) 200×100，单子 150×30）
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, true, 0));

        auto child = std::make_unique<Widget>();
        child->SetSize(150, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetWidth(), 200);   // 跨轴填充 = 父宽（padding 0 时不缩减）
        EXPECT_EQ(cp->GetHeight(), 30);
        EXPECT_EQ(cp->GetX(), 0);
        EXPECT_EQ(cp->GetY(), 0);
    }

    // 块 C：对齐 Layout.StretchBasic（H(0) 1000×50，A(100) / B(s1) / C(s2)）
    {
        Panel panel;
        panel.SetSize(1000, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>(0, false, 0));

        auto a = std::make_unique<Widget>(); a->SetSize(100, 30);
        auto b = std::make_unique<Widget>(); b->SetSize(999, 30); b->SetStretch(1);
        auto c = std::make_unique<Widget>(); c->SetSize(999, 30); c->SetStretch(2);

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
        EXPECT_EQ(ap->GetWidth() + bp->GetWidth() + cp->GetWidth(), 1000);   // Σ 恒等
    }
}

void TestPaddingSingleChild()
{
    // 17 T17-2：单子 + padding ⇒ 起点 = padding；fillCrossAxis = false ⇒ **不碰跨轴尺寸**

    // 块 A：V(0,false,20) 200×100，单子 50×30
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, false, 20));

        auto child = std::make_unique<Widget>();
        child->SetSize(50, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetX(), 20);        // 跨轴坐标 = padding
        EXPECT_EQ(cp->GetY(), 20);        // 主轴起点 = padding
        EXPECT_EQ(cp->GetWidth(), 50);    // 跨轴尺寸不被改写（fill=false）
        EXPECT_EQ(cp->GetHeight(), 30);
    }

    // 块 B：H(0,false,20) 200×100，单子 50×30（对称面）
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<HorizontalLayout>(0, false, 20));

        auto child = std::make_unique<Widget>();
        child->SetSize(50, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetX(), 20);
        EXPECT_EQ(cp->GetY(), 20);
        EXPECT_EQ(cp->GetWidth(), 50);
        EXPECT_EQ(cp->GetHeight(), 30);
    }
}

void TestPaddingCrossAxisWidth()
{
    // 17 T17-3：fillCrossAxis = true + padding ⇒ 每子跨轴 = 父跨轴 − 2p（契约 C4：与 fill/stretch 正交）

    // 块 A：V(0,true,20) 200×100 —— A fixed(50×30) / B(s1)
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, true, 20));

        auto a = std::make_unique<Widget>(); a->SetSize(50, 30);
        auto b = std::make_unique<Widget>(); b->SetSize(1, 1); b->SetStretch(1);

        auto* ap = a.get(); auto* bp = b.get();
        panel.AddChild(std::move(a));
        panel.AddChild(std::move(b));
        panel.Arrange();

        EXPECT_EQ(ap->GetWidth(), 160);   // 200 − 2×20
        EXPECT_EQ(bp->GetWidth(), 160);
        EXPECT_EQ(ap->GetX(), 20);
        EXPECT_EQ(ap->GetY(), 20);
        EXPECT_EQ(bp->GetX(), 20);
        EXPECT_EQ(bp->GetY(), 50);        // 20 + 30（A 主轴高度）+ 0 spacing
        EXPECT_EQ(bp->GetHeight(), 30);   // remaining = 100 − 40 − 30 = 30
        EXPECT_EQ(ap->GetHeight() + bp->GetHeight() + 2 * 20, 100);   // 等式闭合
    }

    // 块 B：H(0,true,20) 200×100 —— A fixed(30×50) / B(s1)
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<HorizontalLayout>(0, true, 20));

        auto a = std::make_unique<Widget>(); a->SetSize(30, 50);
        auto b = std::make_unique<Widget>(); b->SetSize(1, 1); b->SetStretch(1);

        auto* ap = a.get(); auto* bp = b.get();
        panel.AddChild(std::move(a));
        panel.AddChild(std::move(b));
        panel.Arrange();

        EXPECT_EQ(ap->GetHeight(), 60);   // 100 − 2×20
        EXPECT_EQ(bp->GetHeight(), 60);
        EXPECT_EQ(ap->GetX(), 20);
        EXPECT_EQ(bp->GetX(), 50);        // 20 + 30
        EXPECT_EQ(bp->GetWidth(), 130);   // remaining = 200 − 40 − 30 = 130
        EXPECT_EQ(ap->GetWidth() + bp->GetWidth() + 2 * 20, 200);
    }
}

void TestPaddingWithStretch()
{
    // 17 T17-4（**黄金数据**）：H(10,false,20) 500×50，3 个 stretch=1
    // available = 500−40 = 460 → remaining = 460−10×2 = 440 → 440/3 = 146 … 2 ⇒ 146 / 146 / 148（末位吃余数）
    Panel panel;
    panel.SetSize(500, 50);
    panel.SetLayout(std::make_unique<HorizontalLayout>(10, false, 20));

    auto a = std::make_unique<Widget>(); a->SetStretch(1);
    auto b = std::make_unique<Widget>(); b->SetStretch(1);
    auto c = std::make_unique<Widget>(); c->SetStretch(1);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    EXPECT_EQ(ap->GetWidth(), 146);
    EXPECT_EQ(bp->GetWidth(), 146);
    EXPECT_EQ(cp->GetWidth(), 148);
    EXPECT_EQ(ap->GetX(), 20);        // 主轴起点 = padding
    EXPECT_EQ(bp->GetX(), 176);       // 20 + 146 + 10
    EXPECT_EQ(cp->GetX(), 332);       // 176 + 146 + 10
    // 等式：Σ stretch + spacing×(n−1) + 2p == 父主轴
    EXPECT_EQ(ap->GetWidth() + bp->GetWidth() + cp->GetWidth() + 10 * 2 + 20 * 2, 500);
}

void TestPaddingOverflow()
{
    // 17 T17-5：**remaining 是「给 stretch 子的可用空间」，不是「所有子的主轴尺寸总和」**

    // 块 A：溢出混排 —— H(5,false,20) 40×50，A fixed(10×30) / B(s1)
    // remaining = max(0, 40−40−10−5) = 0 ⇒ **A 保持自身尺寸 10**（不是 0）、B 归零
    {
        Panel panel;
        panel.SetSize(40, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>(5, false, 20));

        auto a = std::make_unique<Widget>(); a->SetSize(10, 30);
        auto b = std::make_unique<Widget>(); b->SetSize(1, 1); b->SetStretch(1);

        auto* ap = a.get(); auto* bp = b.get();
        panel.AddChild(std::move(a));
        panel.AddChild(std::move(b));
        panel.Arrange();

        EXPECT_EQ(ap->GetWidth(), 10);    // ★ fixed 子主轴尺寸**不被布局改写**（而非 0）
        EXPECT_EQ(bp->GetWidth(), 0);     // ★ stretch 子吃 remaining = 0
        EXPECT_EQ(ap->GetX(), 20);        // 起点仍为 padding（硬 inset）
        EXPECT_EQ(ap->GetY(), 20);
        EXPECT_EQ(bp->GetX(), 35);        // 20 + 10 + 5
        EXPECT_EQ(bp->GetY(), 20);
    }

    // 块 B：跨轴钳 0 + 坐标不钳 —— V(0,true,30) 50×100，单子 10×10
    // cross = max(0, 50−60) = 0（内容区退化为 0），但坐标仍 = padding = 30（硬 inset，不反向缩减 padding）
    {
        Panel panel;
        panel.SetSize(50, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, true, 30));

        auto child = std::make_unique<Widget>();
        child->SetSize(10, 10);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetWidth(), 0);     // 跨轴钳 0（非负）
        EXPECT_EQ(cp->GetHeight(), 10);   // 主轴保持（stretch=0）
        EXPECT_EQ(cp->GetX(), 30);        // ★ 坐标不钳：30 > 内容区宽 0
        EXPECT_EQ(cp->GetY(), 30);
    }
}

void TestPaddingNested()
{
    // 17 T17-6：嵌套 = **每级各自生效、自然累加**（契约 C3）——不是 max / 不是继承 / 不是覆盖
    // root V(0,true,10) 400×300 → page s1（内含 V(0,true,20)）→ child 50×30 fixed
    Panel root;
    root.SetSize(400, 300);
    root.SetLayout(std::make_unique<VerticalLayout>(0, true, 10));

    auto page = std::make_unique<Panel>();
    page->SetSize(1, 1);
    page->SetStretch(1);
    page->SetLayout(std::make_unique<VerticalLayout>(0, true, 20));

    auto child = std::make_unique<Widget>();
    child->SetSize(50, 30);
    auto* chp = child.get();
    page->AddChild(std::move(child));

    auto* pageP = page.get();
    root.AddChild(std::move(page));
    root.Arrange();

    // 一级：root 的 padding 10 ⇒ page 位置 (10,10)、尺寸 (400−20)×(300−20)
    EXPECT_EQ(pageP->GetX(), 10);
    EXPECT_EQ(pageP->GetY(), 10);
    EXPECT_EQ(pageP->GetWidth(), 380);
    EXPECT_EQ(pageP->GetHeight(), 280);

    // 二级：page 内再让 20 ⇒ child 相对 page (20,20)、跨轴 380−40
    EXPECT_EQ(chp->GetX(), 20);
    EXPECT_EQ(chp->GetY(), 20);
    EXPECT_EQ(chp->GetWidth(), 340);

    // ★ 累加：绝对坐标 = 10 + 20 = 30（同时否定 max(10,20)=20 / 40 / 继承）
    const Point abs = chp->GetAbsolutePosition();
    EXPECT_EQ(static_cast<int>(abs.x), 30);
    EXPECT_EQ(static_cast<int>(abs.y), 30);
}

void TestPaddingIdempotent()
{
    // 17 T17-7：padding + spacing + stretch + fillCrossAxis **全开** ⇒ 两次 Arrange 几何逐项一致（契约 C5）
    // V(6,true,12) 300×200：A fixed(100×40) / B(s1) / C(s2)
    Panel panel;
    panel.SetSize(300, 200);
    panel.SetLayout(std::make_unique<VerticalLayout>(6, true, 12));

    auto a = std::make_unique<Widget>(); a->SetSize(100, 40);
    auto b = std::make_unique<Widget>(); b->SetSize(1, 1); b->SetStretch(1);
    auto c = std::make_unique<Widget>(); c->SetSize(1, 1); c->SetStretch(2);

    auto* ap = a.get(); auto* bp = b.get(); auto* cp = c.get();
    panel.AddChild(std::move(a));
    panel.AddChild(std::move(b));
    panel.AddChild(std::move(c));
    panel.Arrange();

    // 首轮：钉住实际分配（② 的 padding 扣减 + ③ 的跨轴钳 + 末位吃余数）
    EXPECT_EQ(ap->GetY(), 12);
    EXPECT_EQ(ap->GetWidth(), 276);   // 300 − 2×12
    EXPECT_EQ(ap->GetHeight(), 40);
    EXPECT_EQ(bp->GetY(), 58);        // 12 + 40 + 6
    EXPECT_EQ(bp->GetHeight(), 41);   // remaining = 200−24−40−12 = 124 → 124×1/3
    EXPECT_EQ(cp->GetY(), 105);       // 58 + 41 + 6
    EXPECT_EQ(cp->GetHeight(), 83);   // 末位吃余数 = 124 − 41
    EXPECT_EQ(ap->GetHeight() + bp->GetHeight() + cp->GetHeight() + 6 * 2 + 12 * 2, 200);   // 等式闭合

    // 二次 Arrange：**不读取上一次结果**（每次从 parent 几何 + 当前尺寸 + spacing + padding 重算）
    const int ay = ap->GetY(), aw = ap->GetWidth(), ah = ap->GetHeight();
    const int by = bp->GetY(), bh = bp->GetHeight();
    const int cy = cp->GetY(), ch = cp->GetHeight();
    panel.Arrange();

    EXPECT_EQ(ap->GetY(), ay);
    EXPECT_EQ(ap->GetWidth(), aw);
    EXPECT_EQ(ap->GetHeight(), ah);
    EXPECT_EQ(bp->GetY(), by);
    EXPECT_EQ(bp->GetHeight(), bh);
    EXPECT_EQ(cp->GetY(), cy);
    EXPECT_EQ(cp->GetHeight(), ch);
}

void TestPaddingNegativeClamped()
{
    // 17 T17-8：负 padding（契约 C6）—— **Debug / Release 语义分离**
    //
    // Debug：VerticalLayout(0, false, -10) 会在构造期触发 FRAMEWORK_ASSERT(padding >= 0)
    //        ⇒ HandleAssertFailure 走 Logger(Fatal) + MessageBoxW + assert(false) ⇒ **测试进程被终止**。
    //        ⇒ 该分支**必须**留空——**Debug 侧没有（也不该有）运行时用例**；
    //        它由 **A2 的断言特征串核验**覆盖（二进制里存在 `padding >= 0`）。
    // Release：无断言层 ⇒ 由初始化列表的三元 `padding < 0 ? 0 : padding` 钳 0 ⇒ 几何等价于 padding = 0。
#ifdef NDEBUG
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>(0, false, -10));

        auto child = std::make_unique<Widget>();
        child->SetSize(50, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetX(), 0);        // 等价于 padding = 0
        EXPECT_EQ(cp->GetY(), 0);
        EXPECT_EQ(cp->GetWidth(), 50);
        EXPECT_EQ(cp->GetHeight(), 30);
    }
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<HorizontalLayout>(0, false, -10));

        auto child = std::make_unique<Widget>();
        child->SetSize(50, 30);
        auto* cp = child.get();
        panel.AddChild(std::move(child));
        panel.Arrange();

        EXPECT_EQ(cp->GetX(), 0);
        EXPECT_EQ(cp->GetY(), 0);
        EXPECT_EQ(cp->GetWidth(), 50);
        EXPECT_EQ(cp->GetHeight(), 30);
    }
#endif
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
    GetTestRegistry().Add("Layout.PaddingDefaultZero", &TestPaddingDefaultZero);
    GetTestRegistry().Add("Layout.PaddingSingleChild", &TestPaddingSingleChild);
    GetTestRegistry().Add("Layout.PaddingCrossAxisWidth", &TestPaddingCrossAxisWidth);
    GetTestRegistry().Add("Layout.PaddingWithStretch", &TestPaddingWithStretch);
    GetTestRegistry().Add("Layout.PaddingOverflow", &TestPaddingOverflow);
    GetTestRegistry().Add("Layout.PaddingNested", &TestPaddingNested);
    GetTestRegistry().Add("Layout.PaddingIdempotent", &TestPaddingIdempotent);
    GetTestRegistry().Add("Layout.PaddingNegativeClamped", &TestPaddingNegativeClamped);
}
