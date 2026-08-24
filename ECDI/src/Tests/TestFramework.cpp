#include "TestFramework.h"

#include "ECDI/Core/Logger.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 规范 10：本单元含 Windows.h——防 DrawTextW 宏污染
#endif

#include <algorithm>
#include <string>
#include <utility>

namespace ECDI::Test {

TestRegistry& GetTestRegistry()
{
    static TestRegistry registry;   // 函数局部静态（magic static）——无静态初始化顺序问题
    return registry;
}

namespace Detail {

TestContext*& CurrentContext()
{
    static TestContext* current = nullptr;   // 唯一全局：Runner 设置/恢复，测试代码不触碰
    return current;
}

void ReportFailure(const char* expression, const char* file, int line, const char* function)
{
    TestContext* context = CurrentContext();
    if (context)
    {
        context->RecordFailure(expression, file, line, function);
    }
    else
    {
        // 无上下文（测试外误用 EXPECT_*）：防御——不崩溃，仅 Debug 输出警告
        Logger::Log(LogLevel::Warning, L"EXPECT_* used outside a running test");
    }
}

} // namespace Detail

void TestRegistry::Add(const char* name, TestFunction function)
{
    m_cases.push_back(TestCase{ name, function });
}

void TestContext::RecordFailure(const char* expression, const char* file, int line, const char* function)
{
    m_result.failures.push_back(FailureRecord{ expression, file, line, function });
    m_result.passed = false;
}

void TestRunner::Run(const TestRegistry& registry)
{
    m_results.clear();
    for (const TestCase& testCase : registry.GetCases())
    {
        RunOne(testCase);
    }
}

void TestRunner::RunOne(const TestCase& testCase)
{
    TestResult result;
    result.name = testCase.name;
    TestContext context(result);

    auto& slot = Detail::CurrentContext();
    TestContext* previous = slot;
    slot = &context;
    try
    {
        testCase.function();   // 断言失败 → 记录，不抛
    }
    catch (...)
    {
        result.passed = false;   // 标准 C++ 异常 → FAIL + 继续（不承诺 SEH/访问违规/栈溢出）
        // file/line 置空（非异常发生处，避免冒充 TestRunner 位置）；Summary 特判输出
        result.failures.push_back(FailureRecord{ "unhandled exception", nullptr, 0, testCase.name });
    }
    slot = previous;   // 恢复（防御性）
    if (!result.failures.empty())
    {
        result.passed = false;
    }
    m_results.push_back(std::move(result));
}

int TestRunner::GetPassedCount() const noexcept
{
    int count = 0;
    for (const TestResult& result : m_results)
    {
        if (result.passed)
        {
            ++count;
        }
    }
    return count;
}

int TestRunner::GetFailedCount() const noexcept
{
    return static_cast<int>(m_results.size()) - GetPassedCount();
}

namespace {

/// @brief ASCII 窄串 → 宽串（测试名/表达式均为 ASCII——Logger 走 OutputDebugStringW）
std::wstring ToWide(const std::string& text)
{
    std::wstring wide;
    for (char c : text)
    {
        wide.push_back(static_cast<wchar_t>(c));
    }
    return wide;
}

} // anonymous namespace

void PrintSummary(const std::vector<TestResult>& results)
{
    for (const TestResult& result : results)
    {
        if (result.passed)
        {
            Logger::Log(LogLevel::Info, ToWide(std::string("[PASS] ") + result.name));
        }
        else
        {
            Logger::Log(LogLevel::Error, ToWide(std::string("[FAIL] ") + result.name));
            for (const FailureRecord& failure : result.failures)
            {
                if (failure.file != nullptr)
                {
                    const std::string line = std::string("    ") + failure.file + ":" +
                        std::to_string(failure.line) + "  " + failure.expression;
                    Logger::Log(LogLevel::Error, ToWide(line));
                }
                else
                {
                    // 异常记录特判：不显示文件/行（非异常发生处）
                    Logger::Log(LogLevel::Error, L"    unhandled exception");
                }
            }
        }
    }

    const int passed = std::count_if(results.begin(), results.end(),
        [](const TestResult& r) { return r.passed; });
    const int failed = static_cast<int>(results.size()) - passed;
    Logger::Log(LogLevel::Info, ToWide(std::string("----------------------------------\nTests: ") +
        std::to_string(results.size()) + "  Passed: " + std::to_string(passed) +
        "  Failed: " + std::to_string(failed)));

    // MessageBoxW 只负责"提醒失败"，完整报告在 Debug 输出（初步设计 v0.4 定位）
    if (failed > 0)
    {
        const std::wstring message = L"Tests failed: " + std::to_wstring(failed);
        MessageBoxW(nullptr, message.c_str(), L"ECDI Tests", MB_OK | MB_ICONWARNING);
    }
}

} // namespace ECDI::Test
