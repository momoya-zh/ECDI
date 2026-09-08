# Phase 12 WindowChrome 需求确认（v1.0）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-08
> 状态：待评审
> 前置：Phase 10 库化 ✅ / Phase 11 图片解码 ✅（81 Public 头）
> 一句话：自定义标题栏/无边框窗口——应用不再受系统灰标题栏限制，客户区即整窗

---

## 1. 背景与现状勘察

| 项 | 现状 |
|---|---|
| PlatformWindow 抽象面 | Show/Release/Invalidate/GetClientSize/RenderContext/IME caret/Clipboard/Timer——**零 chrome 概念**（grep 实证） |
| 消息处理 | 框架从未碰过 `WM_NCCALCSIZE`/`WM_NCHITTEST`/`WM_NCACTIVATE`——全新领域 |
| 窗口像素测试先例 | RendererTests 已有 `WS_POPUP` 真窗口 + HWND 像素读回模式（视觉验收接缝现成） |
| 立项动机 | GUI 框架的标志能力：应用自绘标题栏（品牌化 UI），系统灰栏消失 |

## 2. 技术路线（业界标准：保留样式 + 改 NCCALCSIZE）

**不走 `WS_POPUP`**（丢系统动画/Aero Snap/最小化动画）——保留 `WS_OVERLAPPEDWINDOW` 全样式，通过消息拦截实现无边框：

```text
WM_NCCALCSIZE   → 返回 0：客户区 = 整个窗口（系统标题栏/边框消失）
WM_NCHITTEST    → 命中测试：标题栏区返回 HTCAPTION（拖动/双击最大化），
                  边缘 inset 返回 HTLEFT..HTBOTTOMRIGHT（八向缩放）
WM_NCACTIVATE   → 返回 DefWindowProc 结果但禁掉非客户区重绘（防闪烁）
DWM             → DwmExtendFrameIntoClientArea（1px 阴影保留）/ Win11 圆角属性
最大化修正      → 最大化时 NCCALCSIZE 补偿负边距（Windows 默认把窗口撑出屏幕一圈）
```

## 3. 需求条目

### R1：Chrome 模式 API（Public——经 Window 透传）

`Window` 层公开 API（PlatformWindow 抽象同步扩展虚接口，Win32 实现——依赖方向单向律）：

```cpp
window.SetChromeMode(ChromeMode::Borderless);   // 形态候选（决策点 §3.3）
```

### R2：无边框客户区

`WM_NCCALCSIZE`（wParam=TRUE 时返回 0）——客户区扩满窗口；样式保留 `WS_OVERLAPPEDWINDOW`（系统缩放动画/Aero Snap 全保留）

### R3：命中测试（拖动/缩放/双击最大化）

- 标题栏区（`captionHeight` 可配置）→ `HTCAPTION`：拖动移动、双击最大化/还原——**系统免费获得**
- 四边 + 四角 inset（默认 8px，可配置）→ 八向缩放命中
- HTCAPTION 区自动获得 Aero Snap（拖到屏幕边缘分屏）——零额外代码

### R4：最大化修正

最大化时 Windows 会将窗口外扩一圈（边框尺寸）——`WM_NCCALCSIZE` 中检测最大化状态并收缩客户区，保证内容不越出屏幕、不遮挡任务栏

### R5：CaptionBar 控件（决策点 §3.1——范围核心分歧）

- **方案 a（窗口层 only）**：框架只提供 chrome 事件与命中测试；标题栏 UI（按钮/图标/文字）完全由应用自绘——框架薄，应用重
- **方案 b（框架内置 CaptionBar Widget）**：提供 `CaptionBar` 控件（标题文字 + 最小化/最大化/关闭三钮 + 双击最大化 + 拖动区委托 chrome）——开箱即用，框架多一个控件
- 倾向 b 的最小版（三钮 + 标题 + 拖动），否则 ModelProbe 类工具每个都要重写一遍按钮

### R6：DWM 增强（决策点 §3.2）

- 阴影：`DwmExtendFrameIntoClientArea`（1px margin——无边框后系统阴影保留的最低成本方案）
- Win11 圆角：`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`
- 失败容忍：DWM API 失败仅日志（Win7 无 DWM 属性时优雅降级）

### R7：标题栏动作事件

- 关闭：复用既有 `WindowCloseRequested`（CaptionBar close 钮触发同一事件链——应用拦截一致）
- 最小化/最大化还原：`WindowMinimized`/`WindowMaximized` 事件（新增 WindowEvent 子类）或 CaptionBar 内部直调 API——决策点

### R8：兼容性底线

- IME 候选窗/键盘（Alt+F4、Alt+Space 系统菜单）/ DPI——行为与有边框模式一致
- `WM_NCACTIVATE` 防闪烁处理
- 多窗口：chrome 状态 per-Window 独立

## 4. 决策点（评审拍板）

1. **范围**：R5 方案 a（窗口层 only）vs b（+CaptionBar 控件最小版）——倾向 b
2. **DWM 增强**：阴影/圆角进 MVP 吗——倾向进（成本低、无它视觉不完整）
3. **API 形态**：`ChromeMode` 枚举 vs bool 开关 vs 分解 API（SetBorderless/SetCaptionHeight/SetResizeInset）——倾向「枚举 + 分解配置项」混合
4. **最大化/最小化事件形态**：新 WindowEvent 子类 vs CaptionBar 内部直调

## 5. 非目标（YAGNI 圈定）

- MDI 子窗口 chrome
- Mica/Acrylic 毛玻璃材质（Win11 DWM 材质——另立）
- 自绘窗口动画（最小化/恢复动画跟随系统）
- 跨平台 chrome（Linux 窗管差异大——抽象接口先立，实现仅 Win32）
- 布局系统改造（chrome 模式下客户区即整窗，标题栏由应用/控件自己布局——Layout 已能表达）

## 6. 测试/验证方向

- 自动：chrome 窗口创建后 `GetClientRect == GetWindowRect`（无边框生效）；NCHITTEST 命中点断言（SendMessage WM_NCHITTEST 带坐标 → 返回值）；最大化修正后客户区在屏幕工作区内
- 渲染：RendererTests 窗口像素先例复用（chrome 窗口 BackBuffer 读回）
- 视觉：VisualTest 式人工核窗口（自定义标题栏外观 + 拖动/缩放/双击/Snap 手测清单）
- 四工具链 + VS

## 7. 影响面

| 区 | 范围 |
|---|---|
| include/ECDI/Platform/ | PlatformWindow 虚接口扩展（chrome API） |
| include/ECDI/Window/ | Window 公共 API 透传 + ChromeMode/事件 |
| src/Platform/Win32/ | Win32PlatformWindow + WindowMessageHandler（NCCALCSIZE/NCHITTEST/NCACTIVATE 拦截）+ Dwmapi 链接 |
| Widget（若 R5 选 b） | CaptionBar 控件 + 样式 |
| docs/ | phase12-windowchrome-* 三件套 + 索引 |

## 8. 修订记录

- v1.0（2026-09-08）需求确认初稿：现状勘察（抽象面零 chrome/消息零拦截/像素测试先例）+ 技术路线（保留 WS_OVERLAPPEDWINDOW + NCCALCSIZE/HITTEST 拦截——非 WS_POPUP）+ R1-R8（chrome API/无边框/命中/最大化修正/CaptionBar 分歧/DWM/事件/兼容底线）+ 4 决策点（范围/DWM/API 形态/事件形态）+ 非目标（MDI/毛玻璃/跨平台）+ 测试方向 + 影响面。待评审。
