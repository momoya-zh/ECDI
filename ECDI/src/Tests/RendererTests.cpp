#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/UTF8.h"
#include <cmath>    // std::abs（浮点 epsilon 比较）
#include <utility>

using namespace ECDI;

// 浮点比较辅助（GPT 建议：避免浮点 == 直接比较）
constexpr float kEpsilon = 0.001f;
inline bool FloatEq(float a, float b) { return std::abs(a - b) < kEpsilon; }

namespace {

void TestRendererForwarding()
{
    // ── 4.5 原 #1：Command → Renderer → RecordingBackend ──
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(DrawRectCommand{ Rect{ 0, 0, 100, 100 }, Color::Red() });
    commands.emplace_back(DrawRectCommand{ Rect{ 10, 20, 30, 40 }, Color::Gray() });

    renderer.Execute(commands);

    FRAMEWORK_ASSERT(backend.draws.size() == 2);
    FRAMEWORK_ASSERT(FloatEq(backend.draws[0].rect.x, 0.0f) && FloatEq(backend.draws[0].rect.width, 100.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[0].color.r, 1.0f) && FloatEq(backend.draws[0].color.a, 1.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[1].rect.x, 10.0f) && FloatEq(backend.draws[1].rect.y, 20.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[1].color.g, 0.5f));
}

void TestUTF8Utility()
{
    // ── 5.5.1.1 原 #6：UTF-8 工具自测 ──
    FRAMEWORK_ASSERT(EncodeUTF8(U'A') == "A");
    FRAMEWORK_ASSERT(EncodeUTF8(U'中') == "\xE4\xB8\xAD");
    FRAMEWORK_ASSERT(EncodeUTF8(U'😀') == "\xF0\x9F\x98\x80");

    const std::string s = "a中😀";
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 0) == 0);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 1) == 1);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 2) == 4);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 3) == 8);
    FRAMEWORK_ASSERT(ByteOffsetToCodepointIndex(s, 4) == 2);
    FRAMEWORK_ASSERT(ByteOffsetToCodepointIndex(s, 8) == 3);
}

} // anonymous namespace

void ECDI::Test::RunRendererTests()
{
    TestRendererForwarding();
    TestUTF8Utility();
}