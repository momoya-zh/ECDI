#include "RunAllTests.h"
#include "TestFramework.h"

void ECDI::Test::RunAllTests()
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

    TestRunner runner;
    runner.Run(GetTestRegistry());
    PrintSummary(runner.GetResults());
}
