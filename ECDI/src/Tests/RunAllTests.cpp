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
    RegisterFontWeightTests();   // Phase 36 批一：字重红线（T36-2）
    RegisterFontEngineTests();   // Phase 26：FontEngine（T26-1..T26-6）
    RegisterGLBackendTests();   // Phase 26：GL 后端（T26-7 / T26-8）
    RegisterDesktopNestTests();   // M1：DesktopNest（T-M1-1..T-M1-6）
    RegisterWindowBackdropTests();   // Phase 32：窗口衬底（T32-1 / T32-4 / T32-7）
    RegisterMouseCaptureTests();     // ★ 审计 #60 / C3：真实鼠标捕获（::SetCapture / 失捕上报）
    RegisterBackendContractTests();  // ★ 审计 #62 / §7.2：后端契约（H3 图像守卫 / M17 边量化）
    RegisterBackendQuantizationTests(); // Phase 34：GDI 几何量化回归
    RegisterVisibilityRepaintTests();   // ★ 审计 M9：可见性变化的重绘归属（SetVisible Invalidate + 同值守卫）
    RegisterAssertDisciplineTests();    // ★ 审计 #63 / §7.3：断言纪律推广（Release 运行时分支）
    RegisterAuditRemainderTests();      // ★ 审计 #68 余量批：M22 / L4 / M14 / L2
    RegisterArcCoverageTests();         // Phase 33 批一：圆弧原语（T33-1..T33-9b）
    RegisterArcCommandTests();          // Phase 33 批二：命令层贯通（T33-10..T33-12）
    RegisterRingProgressBarTests();     // Phase 33 批三：环形进度控件（T33-13..T33-18）
    RegisterEventConsumptionTests();    // Phase 35：事件消费 + 嵌套点击 + 派发期安全销毁
    RegisterPlatformQuickfixTests();    // ★ #72 平台面快修（GL 失败态验证 / 光标 / WM_MOUSELEAVE）
    RegisterLayoutInvalidationTests();  // ★ Phase 41（#79）：帧级布局失效结算（批 0 只注册量具 T41-10）

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
