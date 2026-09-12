#include "RunAllTests.h"
#include "TestFramework.h"

#include <cstdio>

int ECDI::Test::RunAllTests()
{
    // orchestration：Register all → Run → Report（无业务逻辑——不做第二个 Runner）
    RegisterWidgetTests();
    RegisterLayoutTests();
    RegisterTextBoxTests();
    RegisterRendererTests();
    RegisterEventTests();
    RegisterTestFrameworkTests();
    RegisterThemeTests();
    RegisterCheckBoxTests();
    RegisterHoverTests();
    RegisterClipTests();
    RegisterAnimationTests();
    RegisterCollapsiblePanelTests();
    RegisterProgressBarTests();
    RegisterChildProcessTests();
    RegisterModelProbeTests();
    RegisterImageDecodeTests();
    RegisterAntiAliasingTests();   // Phase 8.6：圆角覆盖度抗锯齿

    TestRunner runner;
    runner.Run(GetTestRegistry());
    PrintSummary(runner.GetResults());

    // stdout 汇总（CLion/console 跑测试可见——PrintSummary 走 OutputDebugString/MessageBox）
    for (const auto& r : runner.GetResults()) {
        if (!r.passed) {
            std::printf("[FAIL] %s\n", r.name);
            for (const auto& f : r.failures)
                std::printf("       %s (%s:%d)\n", f.expression, f.file, f.line);
        }
    }
    std::printf("Test summary: %d passed, %d failed, %zu total\n",
                runner.GetPassedCount(), runner.GetFailedCount(), runner.GetResults().size());
    return runner.GetFailedCount();
}
