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

} // namespace ECDI::Test
