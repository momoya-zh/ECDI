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
    RegisterWindowChromeTests();   // Phase 12：WindowChrome
    RegisterCaptionBarTests();   // Phase 13：CaptionBar
    RegisterTrayTests();   // Phase 14：托盘
    RegisterDropFilesTests();   // Phase 14：拖入
    RegisterScrollViewTests();   // Phase 15：滚动容器
    RegisterDesktopLayerTests();   // Phase 16：桌面驻留层
    RegisterWindowBackgroundTests();   // Phase 18：窗口底色
    RegisterDpiTests();   // Phase 20：DPI 换算（纯函数层——T20-1..T20-6）

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
