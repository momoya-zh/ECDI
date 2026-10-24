#pragma once

namespace ECDI::Test {

/// @brief 运行所有无窗口单元测试（Debug 模式调用）
/// @details orchestration 层（初步设计 §3.7）：注册全部测试 → Runner 统一执行 → 汇总报告。
/// 平台入口只换这里——测试核心（Registry/Runner/Assert/Summary）平台无关。
int RunAllTests();   ///< 返回失败数（0 = 全绿；console 测试入口退出码）

// 各模块测试注册入口（定义在对应的 Tests/*.cpp 中；其中 TestFrameworkTests 为基础设施自测）
void RegisterWidgetTests();
void RegisterLayoutTests();
void RegisterTextBoxTests();
void RegisterRendererTests();
void RegisterEventTests();
void RegisterTestFrameworkTests();
void RegisterThemeTests();
void RegisterCheckBoxTests();
void RegisterHoverTests();
void RegisterClipTests();
void RegisterAnimationTests();
void RegisterCollapsiblePanelTests();
void RegisterProgressBarTests();
void RegisterChildProcessTests();
void RegisterModelProbeTests();
void RegisterImageDecodeTests();
void RegisterAntiAliasingTests();   ///< Phase 8.6：圆角覆盖度抗锯齿（L1 掩码数学 + L2 GDI 集成）
void RegisterWindowChromeTests();   ///< Phase 12：WindowChrome（chrome 形态 / 九宫格命中 / rcWork / 状态事件）
void RegisterCaptionBarTests();   ///< Phase 13：CaptionBar（命中委托 / 按钮命令 / 状态查询 / 命令断言）
void RegisterTrayTests();   ///< Phase 14：托盘（状态机 / Shell 序列 / 自愈 / 失败语义 / 析构防线）
void RegisterDropFilesTests();   ///< Phase 14：拖入（HDROP 生命周期 / 事件内容 / 默认关闭）
void RegisterScrollViewTests();   ///< Phase 15：滚动容器（坐标接缝 / 裁剪命中 / 偏移层隔离 / 范围模型）
void RegisterDesktopLayerTests();   ///< Phase 16：桌面驻留层（档位分流 / 样式位 / 钩子生命周期 / ResolveTarget 真值表）
void RegisterWindowBackgroundTests();   ///< Phase 18：窗口底色（端到端注入 / 默认零回归 / 每帧传递 / alpha 原样）
void RegisterDpiTests();   ///< Phase 20：DPI 换算（纯函数真值表 / 恒等护栏 G5 / 往返不恒等 C9 / 负数对称 C10）
void RegisterIconDecodeTests();   ///< Phase 21：系统图标（内核 HICON→Image 逐字节 / 外壳路径矩阵 / 端到端 DrawImage）
void RegisterPreshowGeometryTests();   ///< Phase 22：Create 的 DIP 尺寸契约（T22-1..T22-3）
void RegisterApplicationDispatchTests();   ///< Phase 23：工作线程 → UI 线程投递（T23-1..T23-11）
void RegisterLineCoverageTests();   ///< Phase 24：DrawLine 线段抗锯齿（T24-1..T24-11）
void RegisterTextMeasurerTests();   ///< Phase 26：文本测量链（T26-10 D-8 闭合 / T26-12 GDI 测量缓存）
void RegisterFontWeightTests();   ///< Phase 36 批一：字重红线（T36-2 SimSun 合成加粗串宽带——A2 库内载体）
void RegisterFontEngineTests();   ///< Phase 26：FreeType 共享底层（T26-1..T26-6——PixelSize / 两级缓存 / GlyphKey / FontSource）
void RegisterGLBackendTests();   ///< Phase 26：GL 后端（T26-7 图集 shelf 分配 / T26-8 初始化失败 no-op）
void RegisterDesktopNestTests();   ///< M1：DesktopNest（T-M1-1..T-M1-6——模型/浮层/折叠/拖动路由/关闭路径）
void RegisterWindowBackdropTests();   ///< Phase 32：窗口衬底（T32-1 零回归 / T32-4 Degraded / T32-7 切换无残留——批一）
void RegisterMouseCaptureTests();     ///< ★ 审计 #60 / C3：真实鼠标捕获（::SetCapture/ReleaseCapture + 失捕上报）
void RegisterBackendContractTests();  ///< ★ 审计 #62 / §7.2：后端契约（H3 图像守卫 / M17 边量化——两端共用的纯函数判据）
void RegisterBackendQuantizationTests(); ///< Phase 34：GDI 几何边量化与区间宽度
void RegisterVisibilityRepaintTests();   ///< ★ 审计 M9：可见性变化的重绘归属（SetVisible 内建 Invalidate + 同值守卫 + 范围边界）
void RegisterAssertDisciplineTests();    ///< ★ 审计 #63 / §7.3：断言纪律推广（AddChild/RemoveChild/SetStretch 补 Release 运行时分支）
void RegisterAuditRemainderTests();
void RegisterArcCoverageTests();   ///< Phase 33 批一：圆弧原语（T33-1..T33-9b——覆盖度生成层，零窗口依赖）
void RegisterArcCommandTests();    ///< Phase 33 批二：命令层贯通（T33-10..T33-12——原样转发 / 不归一化 / 两角度不折 scale）
void RegisterRingProgressBarTests();   ///< Phase 33 批三：环形进度控件（T33-13..T33-18——Theme 契约 / 值 / 动画 / 命令流 / 尺寸 / 真窗口 E2E）
void RegisterEventConsumptionTests();  ///< Phase 35：事件消费 + 嵌套点击 + 派发期安全销毁（需求稿验收 1..5）

} // namespace ECDI::Test
