#pragma once

#include <cmath>    // std::abs（EXPECT_NEAR 用——double 语义，避免无符号减法下溢）
#include <vector>

namespace ECDI::Test {

/// @brief 测试函数类型（无参无返回值；断言失败经上下文上报，不抛异常）
using TestFunction = void (*)();

/// @brief 测试用例（注册单元——name + function 成对，非平行数组）
struct TestCase {
    const char* name;        ///< 带模块前缀："Widget.PanelPaint"
    TestFunction function;
};

/// @brief 单条断言失败记录（无 pretty-print——只记表达式文本 + 位置）
struct FailureRecord {
    const char* expression;  ///< 失败表达式文本（#lhs " == " #rhs 等），或 "unhandled exception"
    const char* file;        ///< __FILE__；异常记录时置空（非异常发生处，不冒充 Runner 位置）
    int line;                ///< __LINE__；异常记录时为 0
    const char* function;    ///< __func__ 或测试名（异常时）
};

/// @brief 单个测试用例的结果（统计单位 = TestCase，非断言）
struct TestResult {
    const char* name = nullptr;   ///< 测试名
    bool passed = true;           ///< 全部断言通过且无异常
    std::vector<FailureRecord> failures;   ///< 多断言失败 → 多记录，仍是一个 FAIL TestCase
};

/// @brief 测试注册表（显式注册——决策 C；append-only，无状态风险）
class TestRegistry {
public:
    void Add(const char* name, TestFunction function);   ///< 显式注册
    const std::vector<TestCase>& GetCases() const noexcept { return m_cases; }
    void Clear() noexcept { m_cases.clear(); }   ///< 仅供测试基础设施自测（防跨测试污染）
private:
    std::vector<TestCase> m_cases;
};

TestRegistry& GetTestRegistry();   ///< 函数局部静态（magic static，无静态初始化顺序问题）

/// @brief 当前测试上下文（Runner 持有；断言失败经此上报）
class TestContext {
public:
    explicit TestContext(TestResult& result) : m_result(result) {}
    void RecordFailure(const char* expression, const char* file, int line, const char* function);
    TestResult& GetResult() noexcept { return m_result; }
private:
    TestResult& m_result;
};

namespace Detail {

    /// @brief 唯一全局：当前 TestContext 指针（函数局部静态——无静态初始化顺序问题）
    /// @details Runner 在测试前后设置/恢复；所有状态数据仍在 TestResult（Runner 持有），
    /// 全局只存"指向哪"，不存"什么值"——跨平台迁移负担最小（初步设计 §3.7 约束）。
    TestContext*& CurrentContext();

    /// @brief 断言失败上报入口（EXPECT_* 宏调用）
    /// @details 无上下文时（测试外误用）Debug 输出警告——防御，不崩溃。
    void ReportFailure(const char* expression, const char* file, int line, const char* function);

}

// ── 测试断言（EXPECT_*：记录 + 继续；与 FRAMEWORK_ASSERT 的终止语义分离——双轨）──
// 位置信息自动捕获（__FILE__/__LINE__/__func__）；失败不弹框、不终止。

#define EXPECT_TRUE(condition) \
    do { if (!(condition)) ECDI::Test::Detail::ReportFailure(#condition, __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_FALSE(condition) \
    do { if ((condition)) ECDI::Test::Detail::ReportFailure(#condition " (expected false)", __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_EQ(lhs, rhs) \
    do { if (!((lhs) == (rhs))) ECDI::Test::Detail::ReportFailure(#lhs " == " #rhs, __FILE__, __LINE__, __func__); } \
    while (false)

#define EXPECT_NE(lhs, rhs) \
    do { if (!((lhs) != (rhs))) ECDI::Test::Detail::ReportFailure(#lhs " != " #rhs, __FILE__, __LINE__, __func__); } \
    while (false)

/// @brief 浮点近似断言（v0.2：强制 double 语义 + std::abs——无符号类型 a-b 下溢修复）
/// @details 首版仅用于浮点场景；int/无符号请用 EXPECT_EQ/EXPECT_TRUE。
#define EXPECT_NEAR(lhs, rhs, eps) \
    do { \
        const double _v1 = static_cast<double>(lhs); \
        const double _v2 = static_cast<double>(rhs); \
        const double _e  = static_cast<double>(eps); \
        if (std::abs(_v1 - _v2) > _e) \
            ECDI::Test::Detail::ReportFailure(#lhs " ≈ " #rhs, __FILE__, __LINE__, __func__); \
    } while (false)

/// @brief 测试运行器：遍历 Registry 逐个执行（失败继续 + 异常标记 FAIL）
class TestRunner {
public:
    void Run(const TestRegistry& registry);
    const std::vector<TestResult>& GetResults() const noexcept { return m_results; }
    int GetPassedCount() const noexcept;
    int GetFailedCount() const noexcept;
private:
    void RunOne(const TestCase& testCase);
    std::vector<TestResult> m_results;
};

/// @brief 汇总报告：逐测试行 + 汇总进 Debug 输出；有失败 → MessageBoxW 仅提醒失败数
void PrintSummary(const std::vector<TestResult>& results);

} // namespace ECDI::Test
