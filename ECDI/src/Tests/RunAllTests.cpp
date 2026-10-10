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
    RegisterIconDecodeTests();   // Phase 21：系统图标（T21-1..T21-9）
    RegisterPreshowGeometryTests();   // Phase 22：Create 的 DIP 尺寸契约（T22-1..T22-3）
    RegisterApplicationDispatchTests();   // Phase 23：工作线程 → UI 线程投递（T23-1..T23-11）
    RegisterLineCoverageTests();   // Phase 24：DrawLine 线段抗锯齿（T24-1..T24-11）
    RegisterTextMeasurerTests();   // Phase 26：文本测量链（T26-10 / T26-12）
    RegisterFontEngineTests();   // Phase 26：FontEngine（T26-1..T26-6）
    RegisterGLBackendTests();   // Phase 26：GL 后端（T26-7 / T26-8）
    RegisterDesktopNestTests();   // M1：DesktopNest（T-M1-1..T-M1-6）

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
