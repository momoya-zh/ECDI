#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 规范 10：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "Platform/Win32/WindowMessageHandler.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/EventSystem/EventRouter.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
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
    unsigned int pressedButtons = 0;             ///< 19：此刻按下的键（由谓词折叠而来）
    KeyModifier modifiers = KeyModifier::None;   ///< 19：此刻的修饰键
    int delta = 0;                               ///< 19：滚轮增量（证明 LOWORD 与 HIWORD 两条路径并存）
};

// ── P19 小工具：把公共谓词折叠成"可断言的记录值" ────────────────
// ★ 只用公共 API（IsButtonDown / HasModifier），**不读私有掩码** ⇒ 断言的是公共可见行为；
//   将来掩码换成命名类型时，这些小工具与断言都不必改。

unsigned int SnapshotButtons(const MouseEvent& e)
{
    unsigned int mask = 0;
    const MouseButton all[] = { MouseButton::Left, MouseButton::Right,
                                MouseButton::Middle, MouseButton::X1, MouseButton::X2 };
    for (MouseButton b : all)
        if (e.IsButtonDown(b))
            mask |= 1u << static_cast<unsigned int>(b);
    return mask;
}

KeyModifier SnapshotModifiers(const MouseEvent& e)
{
    KeyModifier m = KeyModifier::None;
    if (e.HasModifier(KeyModifier::Shift)) m = m | KeyModifier::Shift;
    if (e.HasModifier(KeyModifier::Ctrl))  m = m | KeyModifier::Ctrl;
    if (e.HasModifier(KeyModifier::Alt))   m = m | KeyModifier::Alt;   // 恒不成立——"照抄谓词"
    return m;
}

class FakeHost : public PlatformWindowHost
{
public:
    std::vector<ReceivedEvent> received;   ///< 翻译输出的值拷贝记录

    void OnPaint() override {}
    void OnResized(int, int) override {}
    void OnExitSizeMove() override {}

    // ── Phase 13：新增纯虚（本替身不关心客户区命中——恒定「不可交互」）──
    bool IsClientInteractiveAt(int, int) const noexcept override { return false; }
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
            r.pressedButtons = SnapshotButtons(e);    // 19
            r.modifiers = SnapshotModifiers(e);       // 19
            break;
        }
        case EventType::MouseButtonUp:                // 19：新增（装置原先没有此分支）
        {
            const auto& e = static_cast<const MouseButtonUpEvent&>(event);
            r.button = e.GetButton();
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);
            r.modifiers = SnapshotModifiers(e);
            break;
        }
        case EventType::MouseMove:
        {
            const auto& e = static_cast<const MouseMoveEvent&>(event);
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);    // 19
            r.modifiers = SnapshotModifiers(e);       // 19
            break;
        }
        case EventType::MouseWheel:                   // 19：新增（装置原先没有此分支）
        {
            const auto& e = static_cast<const MouseWheelEvent&>(event);
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);
            r.modifiers = SnapshotModifiers(e);
            r.delta = e.GetDelta();                   // 唯一读 delta 的地方
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

    void OnIMECompositionUpdate(const std::string&) override {}   ///< 8.5.1：Host 契约新增（测试不消费）
    void OnIMECompositionCommit(const std::string&) override {}   ///< 8.5.1：Host 契约新增（测试不消费）
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

// ── P19：鼠标状态维度（T19-1..T19-9）——★ 全部经 FakeHost + Handle() 走真翻译路径 ──
// 依据 docs/phase19-mouse-event-dimensions-detailed-design.md §5.2。
// ★ 手工构造事件只能证明"字段存得下"，**不能**证明"平台信息没被丢掉" ⇒ 平台路径一律走翻译器；
//   个别"位 ↔ 谓词"映射的自证用本地构造（probe），注释里标明它与平台路径的分工。

// T19-1 / T19-2 / T19-5：此刻按下的键（含"两键同时在位"与"空 = 空"）
void TestMouseStatePressedButtons()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // T19-5：wParam = 0 ⇒ 空集合（"未按下"与"无信息"不作区分——契约 C4 / R6）
    handler.Handle(nullptr, nullptr, WM_MOUSEMOVE, 0, 0);
    EXPECT_EQ(host.received.size(), 1);
    EXPECT_EQ(host.received[0].type, EventType::MouseMove);
    EXPECT_EQ(host.received[0].pressedButtons, 0u);
    EXPECT_EQ(host.received[0].modifiers, KeyModifier::None);

    // T19-1：按住左键移动 ⇒ 从**移动事件本身**读到"左键按着"（A1 的核心诉求）
    handler.Handle(nullptr, nullptr, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, 20));
    EXPECT_EQ(host.received.size(), 2);
    EXPECT_EQ(host.received[1].x, 10);
    EXPECT_EQ(host.received[1].y, 20);
    EXPECT_EQ(host.received[1].pressedButtons, 0x01u);   // 严格相等 ⇒ 其余四位必为 0

    // T19-2：左 + 右同时在位 ⇒ 0x03（它是"集合"，不是"某一个键"——R1）
    handler.Handle(nullptr, nullptr, WM_MOUSEMOVE, MK_LBUTTON | MK_RBUTTON, 0);
    EXPECT_EQ(host.received[2].pressedButtons, 0x03u);

    // ★ mask 严格相等就够用的原因：ReceivedEvent.pressedButtons 是 SnapshotButtons() 用
    //   IsButtonDown() 逐键折叠而来 ⇒ "== 0x01" 等价于「Left 真，Right/Middle/X1/X2 全假」。
    //   （具名谓词形式的断言见 T19-4 / T19-7 的 probe。）
}

// T19-3 / T19-6：修饰键（并做"修饰键位不串进按键位"的双向验证——R2）
void TestMouseStateModifiers()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // T19-3：Ctrl + Shift 点击左键 ⇒ 按键事件读到修饰键（A2）；且按下集为**空**
    //         （该 wParam 只含 MK_CONTROL|MK_SHIFT，不含任何按键位 ⇒ 两维天然不串）
    handler.Handle(nullptr, nullptr, WM_LBUTTONDOWN, MK_CONTROL | MK_SHIFT, MAKELPARAM(1, 2));
    EXPECT_EQ(host.received[0].type, EventType::MouseButtonDown);
    EXPECT_EQ(host.received[0].button, MouseButton::Left);
    EXPECT_EQ(host.received[0].modifiers, KeyModifier::Ctrl | KeyModifier::Shift);
    EXPECT_EQ(host.received[0].pressedButtons, 0u);

    // T19-6：只按 Shift 移动 ⇒ 修饰键在位、按下集仍为空（与 T19-3 反向）
    handler.Handle(nullptr, nullptr, WM_MOUSEMOVE, MK_SHIFT, 0);
    EXPECT_EQ(host.received[1].modifiers, KeyModifier::Shift);
    EXPECT_EQ(host.received[1].pressedButtons, 0u);
}

// T19-4 ★：XBUTTON 的两维分离（LOWORD = 此刻按下的键 / HIWORD = 本次是哪个键）
void TestMouseStateXButtonTrap()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // wParam = MAKEWPARAM(低 = MK_XBUTTON1, 高 = XBUTTON2)
    //   LOWORD = MK_XBUTTON1 (0x0020) ⇒ 按下集含 X1
    //   HIWORD = XBUTTON2    (0x0002) ⇒ 本次是 X2
    // ★ 陷阱：XBUTTON2 的 0x0002 恰等于 MK_RBUTTON ⇒ 若把 GET_XBUTTON_WPARAM 的返回值
    //   当作"按下集"使用，就会**点亮右键**。下面的 probe 专门钉住这个后果。
    handler.Handle(nullptr, nullptr, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON2), 0);
    EXPECT_EQ(host.received[0].type, EventType::MouseButtonDown);
    EXPECT_EQ(host.received[0].pressedButtons, 0x08u);   // X1 = bit3；严格相等 ⇒ 其余全 0
    EXPECT_EQ(host.received[0].button, MouseButton::X2);  // 来自 HIWORD

    // probe：位 ↔ 谓词 的映射自证（本地构造——与上面的平台路径分工不同：
    //        上面证明"平台事实完整抵达"，这里证明"谓词读的是哪一个位"）
    MouseMoveEvent probe(nullptr, 0, 0, 0x08u, KeyModifier::None);
    EXPECT_TRUE(probe.IsButtonDown(MouseButton::X1));
    EXPECT_FALSE(probe.IsButtonDown(MouseButton::X2));
    EXPECT_FALSE(probe.IsButtonDown(MouseButton::Right));   // ← 钉住"X1 位不会点亮右键"
}

// T19-7：Alt 边界（把"当前 Win32 映射不产生 Alt"钉成回归契约——D3 / C3）
void TestMouseStateAltBoundary()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // 平台路径：按住左键移动，实测**没有任何**修饰键（鼠标消息的 wParam 里没有 MK_ALT）
    handler.Handle(nullptr, nullptr, WM_MOUSEMOVE, MK_LBUTTON, 0);
    EXPECT_EQ(host.received[0].modifiers, KeyModifier::None);

    // probe：具名谓词形式。★ 给一个**确实含修饰键**的状态再断言 Alt 为假——
    //   否则"Alt 假"可能只是因为"本来就没有修饰键"，那样这条断言什么也没钉住。
    //   ★ 注意这不是"事件模型不支持 Alt"：KeyModifier 本就含 Alt，缺的是当前 Win32 映射；
    //     将来出现 Alt + 鼠标的真实消费者 ⇒ 只改 Win32 翻译器，事件 API 无需二次设计。
    MouseMoveEvent probe(nullptr, 0, 0, 0u, KeyModifier::Shift | KeyModifier::Ctrl);
    EXPECT_TRUE(probe.HasModifier(KeyModifier::Shift));
    EXPECT_TRUE(probe.HasModifier(KeyModifier::Ctrl));
    EXPECT_FALSE(probe.HasModifier(KeyModifier::Alt));
}

// T19-8 ★ / T19-9：滚轮的 HIWORD（delta）与新 LOWORD（状态）并存；抬起集的语义
void TestMouseStateWheelAndUp()
{
    FakeHost host;
    WindowMessageHandler handler(host);

    // T19-8：wParam = MAKEWPARAM(低 = MK_CONTROL, 高 = WHEEL_DELTA)
    //   LOWORD = MK_CONTROL ⇒ 修饰键 Ctrl（本相位新增的读取路径）
    //   HIWORD = 120        ⇒ delta（既有路径）
    // ⇒ 两条读取路径在同一条消息里并存且互不干扰——本相位最容易写错的地方
    // ⚠️ 不断言坐标：hwnd == nullptr ⇒ ScreenToClient 不生效（B10）
    handler.Handle(nullptr, nullptr, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    EXPECT_EQ(host.received[0].type, EventType::MouseWheel);
    EXPECT_EQ(host.received[0].modifiers, KeyModifier::Ctrl);
    EXPECT_EQ(host.received[0].delta, WHEEL_DELTA);
    EXPECT_EQ(host.received[0].pressedButtons, 0u);

    // T19-9：抬起 ⇒ 按下集不含被抬起的键（该消息的 wParam 为 0）
    handler.Handle(nullptr, nullptr, WM_LBUTTONUP, 0, MAKELPARAM(1, 2));
    EXPECT_EQ(host.received[1].type, EventType::MouseButtonUp);
    EXPECT_EQ(host.received[1].button, MouseButton::Left);
    EXPECT_EQ(host.received[1].pressedButtons, 0u);
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

    // 19：鼠标状态维度（真翻译路径；命名与既有 Event.Translator* 并列）
    GetTestRegistry().Add("Event.MouseStatePressedButtons", &TestMouseStatePressedButtons);
    GetTestRegistry().Add("Event.MouseStateModifiers",      &TestMouseStateModifiers);
    GetTestRegistry().Add("Event.MouseStateXButtonTrap",    &TestMouseStateXButtonTrap);
    GetTestRegistry().Add("Event.MouseStateAltBoundary",    &TestMouseStateAltBoundary);
    GetTestRegistry().Add("Event.MouseStateWheelAndUp",     &TestMouseStateWheelAndUp);
}

void ECDI::Test::RegisterTestFrameworkTests()
{
    GetTestRegistry().Add("TestFramework.Registry", &TestFrameworkRegistrySelfTest);
    GetTestRegistry().Add("TestFramework.Runner", &TestFrameworkRunnerSelfTest);
    GetTestRegistry().Add("TestFramework.FailContinue", &TestFrameworkFailContinueSelfTest);
    GetTestRegistry().Add("TestFramework.MultiFailure", &TestFrameworkMultiFailureSelfTest);
    GetTestRegistry().Add("TestFramework.Exception", &TestFrameworkExceptionSelfTest);
}
