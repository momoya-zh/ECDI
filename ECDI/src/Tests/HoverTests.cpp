#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Widget/Widget.h"
#include "ECDI/Widget/HoverTracker.h"

#include <memory>
#include <string>
#include <vector>

using namespace ECDI;

namespace {

// ── TestWidget：记录 OnMouseEnter/Leave 调用序列 ──
class TestWidget : public Widget
{
public:
    std::vector<std::string> events;   ///< 事件序列（"Enter"/"Leave"）

    void OnMouseEnter() override { events.push_back("Enter"); }
    void OnMouseLeave() override { events.push_back("Leave"); }
};

// ── 测试环境：普通 Widget 树（无窗口——7.2 体系） + HoverTracker ──
// 方案 A：hover 状态机是纯逻辑单元，树根用普通 Widget 即可验证契约 C/D

struct HoverFixture
{
    Widget root;              ///< 树根（非 Window 的 RootWidget——纯逻辑验证锚点）
    HoverTracker tracker;

    HoverFixture()
    {
        root.SetSize(400, 300);
        tracker.SetTreeRoot(&root);
    }

    TestWidget* AddWidget(int x, int y, int w, int h)
    {
        auto widget = std::make_unique<TestWidget>();
        widget->SetPosition(x, y);
        widget->SetSize(w, h);
        auto* raw = widget.get();
        root.AddChild(std::move(widget));
        return raw;
    }
};

// ── R4-S1：进入（null→A） ──
void TestHoverEnter()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.tracker.Update(a);

    EXPECT_EQ(a->events.size(), 1);
    EXPECT_EQ(a->events[0], "Enter");
    EXPECT_EQ(f.tracker.GetHoverWidget(), a);
}

// ── R4-S2：离开（A→null） ──
void TestHoverLeave()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.tracker.Update(a);           // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    f.tracker.Update(nullptr);     // 未命中 → 正常离开
    EXPECT_EQ(a->events.size(), 2);
    EXPECT_EQ(a->events[1], "Leave");
    EXPECT_EQ(f.tracker.GetHoverWidget(), nullptr);
}

// ── R4-S3：切换（A→B，Leave 先于 Enter——契约 D） ──
void TestHoverSwitch()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(150, 10, 100, 50);

    f.tracker.Update(a);           // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    f.tracker.Update(b);           // 切到 B
    EXPECT_EQ(a->events.size(), 2);
    EXPECT_EQ(a->events[1], "Leave");
    EXPECT_EQ(b->events.size(), 1);
    EXPECT_EQ(b->events[0], "Enter");
    EXPECT_EQ(f.tracker.GetHoverWidget(), b);
}

// ── R4-S4：同目标不重复通知 ──
void TestHoverSameTargetNoRepeat()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.tracker.Update(a);           // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    f.tracker.Update(a);           // 同目标再次移动
    EXPECT_EQ(a->events.size(), 1);   // 不重复
}

// ── R4-S8：脱树不补发 Leave（契约 C——RemoveChild 后目标失效） ──
void TestHoverDetachNoLeave()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(150, 10, 100, 50);

    f.tracker.Update(a);           // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    // A 脱树——⚠️ 必须持有 RemoveChild 返回值（unique_ptr 丢弃即析构——§4.4 契约：调用者持有期间对象存活）
    auto removedA = f.root.RemoveChild(a);

    f.tracker.Update(b);           // 移到 B

    EXPECT_EQ(a->events.size(), 1);   // A 不补发 Leave（契约 C：脱树 ≠ 正常离开）
    EXPECT_EQ(b->events.size(), 1);
    EXPECT_EQ(b->events[0], "Enter");
    EXPECT_EQ(f.tracker.GetHoverWidget(), b);
}

// ── R4-S9：脱树后目标位置无通知 ──
void TestHoverDetachHitTestOriginal()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.tracker.Update(a);           // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    // A 脱树——持有返回值（同上 §4.4 契约）
    auto removedA = f.root.RemoveChild(a);

    f.tracker.Update(nullptr);     // 原位置已无目标（HitTest=null）

    EXPECT_EQ(a->events.size(), 1);   // 无 Leave、无 Enter（脱树失效）
    EXPECT_EQ(f.tracker.GetHoverWidget(), nullptr);
}

} // anonymous namespace

void ECDI::Test::RegisterHoverTests()
{
    GetTestRegistry().Add("Hover.Enter", &TestHoverEnter);
    GetTestRegistry().Add("Hover.Leave", &TestHoverLeave);
    GetTestRegistry().Add("Hover.Switch", &TestHoverSwitch);
    GetTestRegistry().Add("Hover.SameTargetNoRepeat", &TestHoverSameTargetNoRepeat);
    GetTestRegistry().Add("Hover.DetachNoLeave", &TestHoverDetachNoLeave);
    GetTestRegistry().Add("Hover.DetachHitTestOriginal", &TestHoverDetachHitTestOriginal);
}
