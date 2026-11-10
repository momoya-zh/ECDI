# ECDI

**ECDI 是一个从零手写的 Windows C++20 GUI 框架**——直接建立在原生 Win32 与 GDI 之上，纯 C++ 加平台 SDK，第三方库只在满足《目标》那三条门槛时才会被引入。

> 当前正推进到 **v0.1.0**（首个库化版本）。尚未稳定——按 SemVer `0.y.z`，API 可自由变更。

> 📄 语言：[English](README.md) · **简体中文**
> 本文件对译自 `README.md` @ `f217807`（2026-11-10）。**数字与阶段状态以英文版为准**：两版的规模数字（测试 / 头文件 / 文档篇数）由 `tools/doc_lint.py` 的 **L14** 强制一致，不一致即门禁变红。

## 目标：*Everyone Can Do It*

名字就是使命——**E**veryone **C**an **D**o **I**t。

ECDI 是一个**教学优先**的框架：它存在的意义，是让任何人都能以尽可能低的学习成本，看懂一个 GUI 框架是怎么建起来的，并且真的用上它。这不是口号而是设计约束，所以它落成了规则：

- **公共 API 说人话**——一个名字说清一件事；一个概念只用一个词，绝不用两个名字表示同一个想法。
- **不引入投机抽象**——抽象在**第二个真实消费者**出现时才加；「暂时不需要」写进延后账本（[`docs/dev/roadmap-deferred.md`](docs/dev/roadmap-deferred.md)），而不是先写进代码。
- **设计过程本身就是教材**——每个阶段的**需求 → 初步设计 → 详细设计**都写在任何代码之前，所以留下的是推理过程，不只是结论。
- **能跑的示例是正门**——`examples/MinimalApp` 约 20 行就是一个完整应用；`examples/ModelProbe` 是用同一套公共 API 建出来的真实工具。
- **新增能力永不破坏既有代码**——新行为的默认值一律「与现状**逐位等价**」；近几个阶段都以此为硬契约。
- **依赖必须过门槛**——引入第三方库需三条同时成立：它覆盖的领域**不属于本项目自身**（GUI 框架本身）；手写它的验收标准会依赖「正在被写的那个东西」；且愿意承担长期跟版（含安全）的义务。渲染管线与平台解耦两条都不满足，所以**必须自研**。目前唯一的第三方是 **FreeType**（字形栅格化与 hinting），以 vendored 形式入仓并跟版。

## 为什么是 ECDI

多数业余 GUI 项目止步于「一个能显示按钮的窗口」。ECDI 按真实框架的方式建造：严格分层、平台抽象、自研测试套件、库优先的构建方式——并且每个设计决策都有文档（`docs/`，**219 篇**中文设计文档）。

## 它是怎么做出来的

「从零」描述的是代码来源——没有借来的 UI 框架或引擎——而不是作者身份。实现与文档起草都在 AI 的重度辅助下完成。

但这份辅助**不是无人看管的生成**。每个阶段都跑同一套循环，每一步都留下书面记录：

1. **先设计后代码**——需求 → 初步设计 → 详细设计，评审通过后才动实现（`docs/`，逐阶段）
2. **外部评审闸门**——每份设计稿都会被挑错再修订，每轮评审后版本号递增
3. **原子授权**——一个改动集只有在**全部**文件都被批准后才落地；只要有一个文件未授权，就一行都不动
4. **手工验证**——构建与测试在 Visual Studio / CLion 上跨四条工具链真跑，从不外包

架构、分层、阶段划分、以及每一个设计决策都是我的。`docs/` 就是这件事的证据。

## 架构

```
Widget ──▶ PaintContext ──▶ CommandBuffer ──▶ Renderer ──▶ RenderingBackend (GDI)
                                                                    ▲
   Application / Window / EventSystem ──────────▶ Platform abstraction (PlatformWindow, ...)
```

- **四层渲染契约**：Widget、PaintContext、CommandBuffer、Renderer 两两不相识——`Widget` 只发命令，后端只消费命令。把 GDI 换成别的后端，等于实现一个接口。
- **平台抽象**：`PlatformWindow` / `PlatformApplication` / `ChildProcess` 三个接口隔离了全部 `Windows.h` 用法，框架核心是与平台无关的 C++20。窗口级能力（装饰、输入、文件拖放）挂在 `PlatformWindow` 上，应用级能力（托盘图标）挂在 `PlatformApplication` 上——两条对称的接缝，谁都不长出自己的平台对象树。
- **依赖按例外处理**：没有 GDI+、没有 UI 框架、没有渲染库——框架本身只依赖 Windows SDK（`user32`、`imm32`、`msimg32`、`windowscodecs`、`ole32`、`shlwapi`、`shell32`）。渲染用 GDI + `msimg32`（`AlphaBlend`），图像解码用系统 WIC。**FreeType**——GPU 后端的文字栅格化与 hinting——是第一个过门槛的库，以 vendored 方式跟踪而非动态链接。

## 能力

- **控件**：Panel、Label、Button、CheckBox、Radio、TextBox（单行 / 多行、软折行、垂直对齐、IME、撤销重做、剪贴板）、ProgressBar、RingProgressBar、CollapsiblePanel
- **布局**：`VerticalLayout` / `HorizontalLayout` 支持伸缩权重、间距、跨轴填充；另有 `ListLayout` / `GridLayout` 处理按索引排列的行与固定格子网格
- **布局失效**：几何 / 文本 / 可见性的 setter 把根标脏，帧首**每帧结算一次**布局（`NeedsLayout`）——应用层不必再手写 `Arrange` / `Invalidate` 调用链
- **AutoSize**：内容驱动尺寸（`GetPreferredSize` / `AutoSize`），尺寸意图有明确优先级（显式尺寸 > stretch > auto）
- **动画**：per-window `AnimationManager`，token 制，内置多种 easing
- **主题**：样式层（颜色 / 字体 / 圆角 / 边框）支持按实例单点覆盖
- **文本**：测量、选择、IME 组字、剪贴板、撤销重做、软折行（Label 与多行编辑器）、字体族 / 字重，以及**段内内联样式**（`SetStyledText`——同一段文本流里逐 run 的粗体 / 字体族 / 颜色 / 斜体 / 字号，按**码点**索引，行内共享基线；零 span 路径与纯文本**逐位等价**）
- **图像**：WIC 解码（`Decode::DecodeFile` / `Decode::DecodeMemory`），输出预乘 BGRA，可直接喂 `DrawImage`
- **抗锯齿**：圆角用超采样覆盖度掩码（`S=8`）并按半径缓存——GDI 本身没有 AA，所以圆弧走预乘 alpha 合成路径，与主题的圆角半径统一口径
- **窗口装饰**：无边框模式（拦截 `WM_NCCALCSIZE`）+ 自绘标题栏（标题 + 矢量最小化 / 最大化 / 关闭按钮），`NCHITTEST` 委托进控件树，于是标题栏里的可交互控件照样能点，其余区域拖动窗口
- **浮层与弹层**：窗口级双根拓扑，独立的 `m_overlayRoot` 与三层 Panel（Normal / Modal / Tooltip），逃逸祖先容器裁剪；支持 Light Dismiss（点外关闭，可选穿透 / 吞掉策略）、ESC 关闭、模态焦点陷阱与关闭后焦点恢复、Flip-then-Clamp 锚点跟随、脱树自动关闭
- **窗口几何与材质**：运行期 `SetBounds` / `GetBounds`（**请求语义**——读回值才是权威）、最小尺寸、雾面衬底（`SetWindowBackdrop`，Acrylic，带明确记录的降级回退）
- **Shell 集成**：托盘图标（应用级——挂在 `PlatformApplication` 上而非任何 `Window`，所以关掉重建全部窗口它依然在）配原生弹出菜单；窗口级文件拖入（`WM_DROPFILES` → UTF-8 路径列表，`HDROP` 在事件发出前就已释放）。两者都藏在平台接缝之后——公共 API 不暴露任何 Win32 类型。
- **测试**：自研测试框架（**608** 条用例，2026-11-09，零依赖），带 recording backend 做绘制断言

## 构建

需要 C++20。四条工具链经 CMake 支持（MSVC / Clang / ClangCL / MinGW）：

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

要生成原生 Visual Studio 解决方案，用 `vs2026` preset：

```bash
cmake --preset vs2026            # → cmake-build-vs/ : 每个 target 一个 project
cmake --build --preset vs2026-debug
```

本仓库**没有手写的 `.vcxproj`**——解决方案由 CMake 生成，所以每个 target 成为独立 project 并用 `ProjectReference` 连线，文件清单不可能不同步。

目标：

| Target | 类型 | 说明 |
|---|---|---|
| `ECDI` | 静态库 | 框架本体（`include/ECDI/*.h`——**102** 个公共头；内部实现放在 `src/`） |
| `ecdi_tests` | 可执行 | 自研测试套件——**608** 条用例（2026-11-09），零依赖 |
| `modelprobe` | 可执行 | ModelProbe——建在 ECDI 上的真实工具（见下） |
| `visualtest` | 可执行 | 图像解码的并排目视检查（Phase 11） |
| `ecdi_public_header_test` | 测试 | 自包含检查：每个公共头作为独立 TU 编译（经 `--target` 选入） |

### 作为库安装与消费

```bash
cmake -S . -B build && cmake --build build
cmake --install build --prefix <prefix>       # 头文件 + ECDI.lib + ECDIConfig.cmake
```

然后从任意外部 CMake 工程：

```bash
cmake -S examples/MinimalApp -B build-minimal -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build-minimal && .\build-minimal\MinimalApp.exe
```

`examples/MinimalApp` 就是库化的冒烟测试：它严格通过 `find_package(ECDI CONFIG REQUIRED)` + `ECDI::ECDI` 消费 ECDI——没有手写的 include / lib 路径，平台库经安装好的 CMake target 自动传递。

Visual Studio 与 CLion 都能直接打开这个 CMake 工程——本仓库没有手写的 `.vcxproj`（见上面「构建」）。

## 最小用法

```cpp
#include <ECDI/Application/Application.h>
#include <ECDI/Window/Window.h>
#include <ECDI/Widget/Panel.h>
#include <ECDI/Layout/VerticalLayout.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    ECDI::Application application;

    ECDI::Window& window = application.Create("Hello ECDI", 640, 480);
    ECDI::Widget& root = window.GetRootWidget();
    root.SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true));

    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStretch(1);                       // 撑满窗口
    root.AddChild(std::move(panel));
    root.Arrange();

    window.Show();
    return application.Run();
}
```

## 示例

- **[ModelProbe](examples/ModelProbe/)**——以单个静态链接可执行文件交付的模型探测工具：后端进程作为 RC 资源内嵌、运行期释放，动态状态文字（AutoSize）、自适应布局、自定义应用图标。它是本框架的参考消费者。

## 目录结构

```
ECDI/      框架源码（include/ = 102 个公共头，src/ = 实现与测试）
examples/  消费者：ModelProbe（真实工具）、MinimalApp（库化冒烟测试）、VisualTest；DesktopNest（桌面整理器——应用侧 M1：需求 v1.1 评审通过、初设 v1.3（五个开放问题全部裁决）、详设 v1.3 于 2026-10-09 落盘——三件套齐备、三轮评审吸收完毕、待实施）
probe-go/  以 RC 资源内嵌进 ModelProbe 的 Go 后端
docs/      usage.md（API / 使用指南，讲怎么用） + dev/（218 篇设计文档：逐阶段的需求 → 初设 → 详设；含 usage.md 共 219 篇）
```

📚 **设计文档**（中文）：[docs/dev/README.md](docs/dev/README.md)——逐阶段设计稿的完整索引、开发进度与技术债账本。

📖 **使用指南**（中文）：[docs/usage.md](docs/usage.md)——怎么用这个框架：每个控件、布局逻辑（`SetStretch` / 四种 `Layout` / `Arrange` 何时发生）、事件、动画、主题，以及已知限制清单。

## 进度（Status）

| 阶段 | 范围 | 状态 |
|---|---|---|
| 1–9 | 核心窗口 / 事件 / 控件 / 文本与 IME / 布局 / 平台解耦 / 测试框架 / 渲染扩展 / 主题 / 动画 / AutoSize | ✅ |
| **10** | **库化 v0.1.0**：公共 API 边界、install / export、外部消费者验收 | ✅ 2026-09-06 |
| 11–18 | 图片解码 / 窗口装饰 / 标题栏 / 托盘与拖入 / 滚动容器 / 桌面驻留 / 布局内边距 / 根背景 | ✅ |
| 19–25 | 鼠标事件维度 / DPI 感知 / 渲染层 DPI / 图标转 Image / 预显示几何 / 线程投递 / 线段抗锯齿 / 列表与网格容器 | ✅ |
| 26–30 | FreeType + GL 后端 / 视口剔除 / 长文本成本 / 多行文本 / 交互快修 | ✅ |
| 31–36 | 窗口几何与行高 / 雾面衬底 / `DrawArc` + 环形进度 / 后端取整口径 / 事件消费与安全销毁 / `Font` 表达力 | ✅ |
| 37 | 单行 `TextBox` 垂直对齐 | ✅ 2026-10-26 |
| 38 | `TextBox` 编辑器内软折行 | ✅ 2026-10-29 |
| 39 | UTF-8 码点索引 O(N²) → 单趟线性 | ✅ 2026-10-29 |
| 40 | 折行支 O(L·N) → 码点↔字节偏移表 | ✅ 2026-10-29 |
| 41 | 帧级布局 / 重绘失效结算管线（`NeedsLayout`） | ✅ 2026-10-30 |
| 42 | 窗口级浮层根 + Light Dismiss | ✅ 2026-10-30 |
| 43 | 滚动条双轴厚度 | ✅ 2026-11-01 |
| 44 | 段内内联样式（styled spans） | ✅ 2026-11-08 |
| 45 | styled spans 表达力扩展（斜体 + 混字号） | ✅ 2026-11-09 |

逐阶段的完整叙事——需求 / 初设 / 详设三稿、每轮评审的吸收与版本递增、负对照与机检记录——见 [docs/dev/README.md](docs/dev/README.md)；那是本项目的中文一手账本，也是英文版状态表里那些长段落的中文源头。

## 许可

ECDI 本体采用 [MIT](LICENSE) 许可（Copyright © 2026 momoya-zh）。

**第三方**：FreeType 2.14.3——vendored 于 `third_party/freetype`，静态链入 GL 后端的文本栈——采用 FreeType License（**FTL**）或 GPLv2 双许可；本项目取 **FTL**。完整许可正文随仓库发布：`third_party/freetype/LICENSE.TXT`（入口）指向 `docs/FTL.TXT` / `docs/GPLv2.TXT`；它引用的各个 contrib 子许可（bdf/pcf X11 风格、gzip/zlib、autofit `ft-hb-*` Old MIT）随 `src/` 一同保留。FTL §2 要求任何内嵌 FreeType 的二进制分发自带免责声明（法律文本保留英文原文，不翻译）：

> This product is based in part on the work of the FreeType Team (https://freetype.org).

FreeType Team 首选致谢格式（FTL 引言，鼓励采用；`<year>` = 所用版本的版权年）：

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

`cmake --install` 会把 notice 一起装进前缀——`<prefix>/share/licenses/ECDI/NOTICE.md`，以及 ECDI 的 MIT `LICENSE` 与 FreeType 的 `LICENSE.TXT` / `FTL.TXT` / `GPLv2.TXT`，都在 `share/licenses/ECDI/` 下——因为安装前缀本身就是一次二进制分发（FreeType 的 object 已并入 `ECDI` 静态库）。交付单须固定 notice 项与所交付库的 SHA256；详见 [docs/usage.md §14](docs/usage.md)。
