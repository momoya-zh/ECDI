#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 规范 10：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Platform/Win32/WindowMessageHandler.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/EventSystem/EventRouter.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/EventSystem/Window/WindowResizedEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"

#include <stdexcept>
#include <utility>
#include <vector>

using namespace ECDI;

namespace {

// ── FakeHost：假 PlatformWindowHost（无窗口翻译测试的支点）──────────
// 事件字段在 OnEvent 内立即拷贝（值语义）——不存指向翻译器局部 Event 的指针，
// 完全避开悬垂（比"仅即时断言"更安全；测试意图不变：验证翻译输出正确）。

struct ReceivedEvent
{
    EventType type = EventType::None;
    KeyCode keyCode = KeyCode::Unknown;
    MouseButton button = MouseButton::Left;
    int x = 0;
    int y = 0;
    char32_t codepoint = 0;
    int width = 0;
    int height = 0;
};

class FakeHost : public PlatformWindowHost
{
public:
    std::vector<ReceivedEvent> received;   ///< 翻译输出的值拷贝记录

    void OnPaint() override {}
    void OnResized(int, int) override {}
    void OnExitSizeMove() override {}
    Window* GetWindow() const noexcept override { return nullptr; }

    void OnEvent(const Event& event) override
    {
        ReceivedEvent r;
        r.type = event.GetType();
        switch (event.GetType())
        {
        case EventType::KeyDown:
            r.keyCode = static_cast<const KeyDownEvent&>(event).GetKeyCode();
            break;
        case EventType::CharInput:
            r.codepoint = static_cast<const CharInputEvent&>(event).GetCodepoint();
            break;
        case EventType::MouseButtonDown:
        {
            const auto& e = static_cast<const MouseButtonDownEvent&>(event);
            r.button = e.GetButton();
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            break;
        }
        case EventType::MouseMove:
        {
            const auto& e = static_cast<const MouseMoveEvent&>(event);
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            break;
        }
        case EventType::WindowCloseRequested:
            break;
        case EventType::WindowResized:
        {
            const auto& e = static_cast<const WindowResizedEvent&>(event);
            r.width = e.GetWidth();
            r.height = e.GetHeight();
            break;
        }
        default:
            break;
        }
        received.push_back(r);
    }

    void OnIMEComposition() override {}
};

// ── T1: TranslateKeyCode（private static——经 Handle + FakeHost 间接测）──
// 只断言 KeyCode 映射；modifier 依赖真实键盘状态（TranslateModifier 走 GetKeyState），不断言。

void TestTranslatorKeyCode()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // 字母键
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, 'A', 0);
    EXPECT_EQ(host.received.size(), 1);
    EXPECT_EQ(host.received[0].type, EventType::KeyDown);
    EXPECT_EQ(host.received[0].keyCode, KeyCode::A);

    // 左右 Shift：lParam 高字位 scanCode 区分（MapVirtualKey 计算，不硬编码）
    const UINT leftScan = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, VK_SHIFT, leftScan << 16);
    EXPECT_EQ(host.received[1].keyCode, KeyCode::LeftShift);

    const UINT rightScan = MapVirtualKey(VK_RSHIFT, MAPVK_VK_TO_VSC);
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, VK_SHIFT, rightScan << 16);
    EXPECT_EQ(host.received[2].keyCode, KeyCode::RightShift);

    // 左右 Ctrl：lParam bit24（extended）区分
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, VK_CONTROL, 0);
    EXPECT_EQ(host.received[3].keyCode, KeyCode::LeftCtrl);
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, VK_CONTROL, 1 << 24);
    EXPECT_EQ(host.received[4].keyCode, KeyCode::RightCtrl);

    // 主键盘 / 小键盘 Enter：extended bit 区分
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, VK_RETURN, 0);
    EXPECT_EQ(host.received[5].keyCode, KeyCode::Enter);
    handler.Handle(nullptr, nullptr, WM_KEYDOWN, VK_RETURN, 1 << 24);
    EXPECT_EQ(host.received[6].keyCode, KeyCode::NumpadEnter);
}

// ── T2: TranslateMouseButton（private static——经 Handle 间接测）──

void TestTranslatorMouseButton()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    handler.Handle(nullptr, nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 20));
    EXPECT_EQ(host.received.size(), 1);
    EXPECT_EQ(host.received[0].type, EventType::MouseButtonDown);
    EXPECT_EQ(host.received[0].button, MouseButton::Left);
    EXPECT_EQ(host.received[0].x, 10);
    EXPECT_EQ(host.received[0].y, 20);

    handler.Handle(nullptr, nullptr, WM_RBUTTONDOWN, 0, 0);
    EXPECT_EQ(host.received[1].button, MouseButton::Right);

    // WM_XBUTTONDOWN：wParam 高 16 位（HIWORD）= 按键标识 XBUTTON1/2，低 16 位 = MK_ 修饰标志
    // （GET_XBUTTON_WPARAM = HIWORD——按键必须放高位；放低位会命中 assert 兜底）
    handler.Handle(nullptr, nullptr, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), 0);
    EXPECT_EQ(host.received[2].button, MouseButton::X1);
    handler.Handle(nullptr, nullptr, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), 0);
    EXPECT_EQ(host.received[3].button, MouseButton::X2);
}

// ── T3: ConsumeCodeUnit 代理对状态机（private 实例方法——经 Handle(WM_CHAR) 间接测）──

void TestTranslatorSurrogatePair()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // 😀 = U+1F600 = UTF-16 高代理 0xD83D + 低代理 0xDE00
    handler.Handle(nullptr, nullptr, WM_CHAR, 0xD83D, 0);   // 高代理：暂存等待，不产生事件
    EXPECT_EQ(host.received.size(), 0);

    handler.Handle(nullptr, nullptr, WM_CHAR, 0xDE00, 0);   // 低代理：组合成完整码点
    EXPECT_EQ(host.received.size(), 1);
    EXPECT_EQ(host.received[0].type, EventType::CharInput);
    EXPECT_EQ(host.received[0].codepoint, 0x1F600);

    // 孤立低位代理：丢弃（不负责 Unicode error recovery）
    handler.Handle(nullptr, nullptr, WM_CHAR, 0xDE00, 0);
    EXPECT_EQ(host.received.size(), 1);   // 不增长

    // 普通 BMP 字符：独立事件
    handler.Handle(nullptr, nullptr, WM_CHAR, U'A', 0);
    EXPECT_EQ(host.received.size(), 2);
    EXPECT_EQ(host.received[1].type, EventType::CharInput);
    EXPECT_EQ(host.received[1].codepoint, U'A');
}

// ── T4: Handle 全流程（窗口事件 + 鼠标移动）──

void TestTranslatorHandleFlow()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // WM_CLOSE → WindowCloseRequestedEvent
    handler.Handle(nullptr, nullptr, WM_CLOSE, 0, 0);
    EXPECT_EQ(host.received.size(), 1);
    EXPECT_EQ(host.received[0].type, EventType::WindowCloseRequested);

    // WM_SIZE → WindowResizedEvent（LOWORD/HIWORD 解析）
    handler.Handle(nullptr, nullptr, WM_SIZE, 0, MAKELPARAM(300, 200));
    EXPECT_EQ(host.received[1].type, EventType::WindowResized);
    EXPECT_EQ(host.received[1].width, 300);
    EXPECT_EQ(host.received[1].height, 200);

    // WM_MOUSEMOVE → MouseMoveEvent（GET_X/Y_LPARAM）
    handler.Handle(nullptr, nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(42, 57));
    EXPECT_EQ(host.received[2].type, EventType::MouseMove);
    EXPECT_EQ(host.received[2].x, 42);
    EXPECT_EQ(host.received[2].y, 57);
}

// ── Event 值对象直接构造 + EventRouter 分派（验证 7.1 解耦成果：事件系统独立于 Win32）──

void TestEventHierarchy()
{
    MouseButtonDownEvent m(nullptr, 5, 6, MouseButton::Left);
    EXPECT_EQ(m.GetType(), EventType::MouseButtonDown);
    EXPECT_EQ(m.GetButton(), MouseButton::Left);
    EXPECT_EQ(m.GetMouseX(), 5);
    EXPECT_EQ(m.GetMouseY(), 6);

    CharInputEvent c(nullptr, U'中');
    EXPECT_EQ(c.GetType(), EventType::CharInput);
    EXPECT_EQ(c.GetCodepoint(), U'中');

    // EventRouter 分派到具名虚方法（默认空实现；override 计数）
    class CountingRouter : public EventRouter
    {
    public:
        int keyDownCount = 0;
        int charCount = 0;
        int mouseDownCount = 0;
    protected:
        void OnKeyDown(const KeyDownEvent&) override { ++keyDownCount; }
        void OnCharInput(const CharInputEvent&) override { ++charCount; }
        void OnMouseButtonDown(const MouseButtonDownEvent&) override { ++mouseDownCount; }
    };

    CountingRouter router;
    router.OnEvent(m);   // MouseButtonDown → 具名分派
    EXPECT_EQ(router.mouseDownCount, 1);
    EXPECT_EQ(router.keyDownCount, 0);   // 未处理事件不误派

    KeyDownEvent k(nullptr, KeyCode::A, KeyModifier::None);
    router.OnEvent(k);
    EXPECT_EQ(router.keyDownCount, 1);

    router.OnEvent(c);
    EXPECT_EQ(router.charCount, 1);
}

// ── TestFramework 自测（F1-F5：基础设施回归测试——局部 registry/runner，不污染全局）──

void SelfPass() { EXPECT_TRUE(true); }
void SelfFail() { EXPECT_TRUE(false); }
void SelfMultiFail()
{
    EXPECT_TRUE(false);
    EXPECT_TRUE(false);
    EXPECT_TRUE(false);
}
void SelfThrow() { throw std::runtime_error("boom"); }

// F1: Registry 注册/取回/Clear
void TestFrameworkRegistrySelfTest()
{
    Test::TestRegistry registry;   // 局部实例（类本身可独立使用——不依赖全局）
    registry.Add("Self.A", &SelfPass);
    EXPECT_EQ(registry.GetCases().size(), 1);
    EXPECT_EQ(registry.GetCases()[0].function, &SelfPass);
    registry.Add("Self.B", &SelfPass);
    EXPECT_EQ(registry.GetCases().size(), 2);
    registry.Clear();
    EXPECT_EQ(registry.GetCases().size(), 0);
}

// F2: Runner 执行全部 PASS
void TestFrameworkRunnerSelfTest()
{
    Test::TestRegistry registry;
    registry.Add("Self.PassA", &SelfPass);
    registry.Add("Self.PassB", &SelfPass);
    registry.Add("Self.PassC", &SelfPass);
    Test::TestRunner runner;
    runner.Run(registry);
    EXPECT_EQ(runner.GetPassedCount(), 3);
    EXPECT_EQ(runner.GetFailedCount(), 0);
}

// F3: 失败不阻塞后续测试（Phase 7.2 核心新行为，必须有测试证明）
void TestFrameworkFailContinueSelfTest()
{
    Test::TestRegistry registry;
    registry.Add("Self.Fail", &SelfFail);
    registry.Add("Self.PassAfter", &SelfPass);
    Test::TestRunner runner;
    runner.Run(registry);
    EXPECT_EQ(runner.GetResults().size(), 2);   // 两个都执行了
    EXPECT_EQ(runner.GetPassedCount(), 1);
    EXPECT_EQ(runner.GetFailedCount(), 1);
}

// F4: 多断言失败 = 1 个 FAIL TestCase + 多个 failure records
void TestFrameworkMultiFailureSelfTest()
{
    Test::TestRegistry registry;
    registry.Add("Self.MultiFail", &SelfMultiFail);
    Test::TestRunner runner;
    runner.Run(registry);
    EXPECT_EQ(runner.GetResults().size(), 1);
    EXPECT_EQ(runner.GetFailedCount(), 1);                    // 统计单位 = TestCase
    EXPECT_EQ(runner.GetResults()[0].failures.size(), 3);     // 3 条记录
}

// F5: 异常测试 → FAIL + Runner 继续；异常记录 file 置空（不冒充异常位置）
void TestFrameworkExceptionSelfTest()
{
    Test::TestRegistry registry;
    registry.Add("Self.Throw", &SelfThrow);
    registry.Add("Self.AfterThrow", &SelfPass);
    Test::TestRunner runner;
    runner.Run(registry);
    EXPECT_EQ(runner.GetResults().size(), 2);                 // 异常后继续
    EXPECT_EQ(runner.GetFailedCount(), 1);
    EXPECT_EQ(runner.GetResults()[0].failures.size(), 1);
    EXPECT_TRUE(runner.GetResults()[0].failures[0].file == nullptr);
}

} // anonymous namespace

void ECDI::Test::RegisterEventTests()
{
    GetTestRegistry().Add("Event.TranslatorKeyCode", &TestTranslatorKeyCode);
    GetTestRegistry().Add("Event.TranslatorMouseButton", &TestTranslatorMouseButton);
    GetTestRegistry().Add("Event.TranslatorSurrogatePair", &TestTranslatorSurrogatePair);
    GetTestRegistry().Add("Event.TranslatorHandleFlow", &TestTranslatorHandleFlow);
    GetTestRegistry().Add("Event.EventHierarchy", &TestEventHierarchy);
}

void ECDI::Test::RegisterTestFrameworkTests()
{
    GetTestRegistry().Add("TestFramework.Registry", &TestFrameworkRegistrySelfTest);
    GetTestRegistry().Add("TestFramework.Runner", &TestFrameworkRunnerSelfTest);
    GetTestRegistry().Add("TestFramework.FailContinue", &TestFrameworkFailContinueSelfTest);
    GetTestRegistry().Add("TestFramework.MultiFailure", &TestFrameworkMultiFailureSelfTest);
    GetTestRegistry().Add("TestFramework.Exception", &TestFrameworkExceptionSelfTest);
}
