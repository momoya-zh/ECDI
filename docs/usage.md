# ECDI 使用指南

> 本文是 **ECDI 框架的使用文档**——面向「要把 ECDI 用起来的第三方开发者」，讲清三件事：
> **① 各控件怎么用**（每个控件的构造、配置、回调、常见组合）· **② 布局逻辑**（谁排谁、Stretch 与
> Layout 的关系、`Arrange` 何时发生）· **③ 从零接一个新窗口的完整路径**（建 → 摆 → 交互 → 跑）。
> 设计文档（「为什么这样设计」）在 [dev/README.md](dev/README.md)；项目总览（定位 / 构建 / 四工具链）在
> [仓库根 README.md](../README.md)。

**声明与约定**

- 所有 API 结论均以 `include/ECDI/**` 的公共头为准（**102 个**，`find ECDI/include -name '*.h' | wc -l` 可复核）；文中标注 `ECDI/include/...` 的是可直接核对的源头。
- 源码引用一律锚在**符号**（`class Widget`、`Button::OnMouseButtonDown`）而不是行号——行号会随阶段演进漂走，符号不会。
- 示例默认 **GDI 后端**（`CreateDefaultRenderServices()` 的默认值，Windows 开箱即用）。
- 框架只支持 **Windows**（Win32 + GDI / WGL）；Win32 头需自备（示例见下方「宏冲突防护」）。
- 已知限制与「v1.0 API 审查项」集中在 [§11 已知限制与坑](#11-已知限制与坑)，不要向使用者承诺已解决。

---

## 1. 五分钟上手：一个窗口 + 一个按钮

```cpp
#include <ECDI/Application/Application.h>
#include <ECDI/Widget/Button.h>
#include <ECDI/Widget/Panel.h>
#include <ECDI/Layout/VerticalLayout.h>

#ifdef DrawText            // Win32 宏会撞上 PaintContext::DrawText
#undef DrawText            // 任何 .cpp 同时包 Windows.h 与 ECDI 头时都要这句
#endif

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    using namespace ECDI;

    Application application;
    Window& window = application.Create("Hello ECDI", 480, 320);

    Widget& root = window.GetRootWidget();
    root.SetLayout(std::make_unique<VerticalLayout>(0, /*fillCrossAxis=*/true));

    auto panel = std::make_unique<Panel>();
    panel->SetStretch(1);                       // 伸满剩余主轴空间
    auto button = std::make_unique<Button>("点我");
    button->SetOnClick([&window] { window.Invalidate(); });
    panel->AddChild(std::move(button));
    root.AddChild(std::move(panel));

    root.Arrange();                             // 手动触发一次布局（建树期）
    window.Show();
    return application.Run();
}
```

**五条铁律**（本节之后的所有内容都在展开它们）：

1. `Application` 是进程内单点，`Run()` 之前 `Create()` 窗口；窗口由 `Application` 持有，**调用方永不 `delete`**。
2. **控件树**：`window.GetRootWidget()` 是根；`AddChild(std::unique_ptr<Widget>)` 建立父子（所有权移交）。
3. **布局**：容器 `SetLayout(...)` 排直接子；`SetStretch(n)` 让子控件在主轴方向按权重瓜分剩余空间。
4. **建树后、`Show()` 前后各 `root.Arrange()` 一次**（运行期缩窗不用管——`OnResized` 链会自动重排，见 §4.4）。
5. **一切交互走虚函数与回调**：`SetOnXxx`（业务便利层）+ `OnXxx()` override（继承扩展层）两套并存。

---

## 2. 应用与窗口生命周期

### 2.1 Application（`ECDI/include/ECDI/Application/Application.h` · `class Application`）

| API | 语义 |
|---|---|
| `Application()` | 构造即声明 Per-Monitor-V2 DPI 感知（失败仅 `Log(Warning)`，继续） |
| `Window& Create(title, width, height, RenderServices services = CreateDefaultRenderServices())` | 建窗；`width/height` 是 **DIP 逻辑像素**（含边框总尺寸的请求值）；`services` 用于换渲染后端 |
| `int Run()` | 阻塞，直到最后一个窗口关闭 |
| `void Exit()` | 退出消息循环 |
| `bool PostToUi(std::function<void()>)` | **线程安全投递到 UI 线程**（工作线程 → UI 的唯一正道）。注意：不保活载荷、不保活 `Application` 本体；可能抛 `std::bad_alloc` |
| `SetTrayIcon / RemoveTrayIcon / ShowTrayMenu` | 托盘图标与一级纯文本菜单（`ShowTrayMenu` 同步返回选中 id，0=未选中） |
| `SetQuitOnLastWindowClosed(bool)` | 默认 `true`；`Hide()` 关窗 → 托盘常驻场景要设 `false` |

`Application` 继承 `EventRouter`，可 override 应用级 `OnXxx`（见 §9）。

### 2.2 Window（`ECDI/include/ECDI/Window/Window.h` · `class Window`）

**生命周期铁律**：`Window` 由 `Application` 持有，调用方不得 `delete`；`Release()` 幂等销毁 HWND；
`Hide()` 只隐藏不销毁；`RequestClose()` 同步派发 `WindowCloseRequested`（可被 override 拦截，
与点系统 X 同一路径，适合「未保存 → 询问」流程）。

**配置期 API**（只能在 `Show()` 之前调用，超时记 `Warning` 并忽略）：

| 调用 | 语义 |
|---|---|
| `SetChromeMode(ChromeMode)` | `Normal`（默认，系统标题栏）/ `Borderless`（自绘，配合 `CaptionBar`，§7.3） |
| `SetCaptionHeight(int)` | 自绘标题栏高度 |
| `SetResizeInset(int)` | 无边框窗口的裁边热区宽度 |
| `SetWindowLayer(WindowLayer)` | `Normal` / `Bottom` / `Desktop`（桌面驻留） |
| `SetWindowBackdrop(BackdropMode)` | `None` / `Acrylic`（亚克力；系统不支持会降级，`Degraded` 只能作为查询结果出现，**不能作为请求值**） |

**运行期 API**：`Show/Hide` · `SetBounds(const Rect&)/GetBounds()`（屏幕坐标 DIP，含边框总尺寸；
**请求语义**——读回以 `GetBounds` 为准）· `SetMinimumSize(int w, int h)`（★ REQ-13——**客户区** DIP；
`<= 0` = 该轴不设下限；`Show()` 前后均可。低于下限的请求被系统**在生效之前**夹住：拖边框在边框处硬停、
不回弹，`SetBounds` 照其为请求语义——读回以 `GetBounds` 为准，本方法**不**替调用方放大窗口）·
`Minimize/Maximize/Restore/GetWindowState` ·
`SetBackgroundColor(const Color&)`（运行期可用，忽略 alpha，默认白）· `SetFocusedWidget/GetFocusedWidget` ·
`Invalidate()` · `GetTextMeasurer()` · `GetPlatformWindow()` · `GetDpiScale()` ·
`SetFileDropEnabled(bool)`（开启 `WM_DROPFILES` → `OnDropFiles`，§9.5）· `SetCaptureWidget/GetCaptureWidget`
（拖拽期鼠标捕获；配合 `Application::PostToUi` 时可跨线程投递回调）· `HandleKeyDown` / `UpdateHoverState`。

**浮层族**（Phase 42，用法见 §7.4）：`GetOverlayRoot()`（底阶原语）· `ShowOverlay(std::unique_ptr<Widget>, const OverlayOptions& = {})` ·
`HideOverlay(Widget* = nullptr)`（无参 = 关顶层）· `HasActiveOverlay(std::optional<OverlayLayer> = std::nullopt)`。

---

## 3. 控件总览与共同基类

### 3.1 控件清单（按「要不要管它」分层）

| 控件 | 头文件 | 管什么 | 典型场景 |
|---|---|---|---|
| `Label` | `Widget/Label.h` | 只读文本（透明背景） | 标题、说明文字 |
| `Button` | `Widget/Button.h` | 点击 + 文字 | 确认 / 取消 |
| `CheckBox` / `Radio` | `Widget/CheckBox.h` · `Radio.h` | 布尔态 / 同父互斥单选 | 设置项；单选组 |
| `TextBox` | `Widget/TextBox.h` | 单行 / 多行输入、选择、IME、撤销 | 表单、搜索框 |
| `ProgressBar` / `RingProgressBar` | `Widget/ProgressBar.h` · `RingProgressBar.h` | determinate 进度（水平 / 环形） | 下载进度；上下文环 |
| `Panel` | `Widget/Panel.h` | 纯容器（分组 + 底色） | 分组框、卡片 |
| `CollapsiblePanel` | `Widget/CollapsiblePanel.h` | 折叠面板（四向） | 详情区、工具箱 |
| `ScrollView` | `Widget/ScrollView.h` | 视口 + 内容偏移 + 滚动条 | 长列表、大图 |
| `ScrollBar` | `Widget/ScrollBar.h` | 轨道 + 滑块（一般经 `ScrollView` 派生） | 独立滚动控制 |
| `CaptionBar` | `Window/CaptionBar.h` | 自绘标题栏（`Borderless` 窗口用） | 无边框应用 |

### 3.2 Widget 基类（`ECDI/include/ECDI/Widget/Widget.h` · `class Widget`）

所有控件的公共合约。**三个继承约定**：

```cpp
class MyWidget : public Widget {
protected:
    void OnPaint(PaintContext& ctx, int x, int y) override;  // x,y = 本控件绝对位置
    void OnMouseButtonDown(const MouseButtonDownEvent& e) override;
    // 命中判定不满足矩形时 override ContainsPoint
    bool ContainsPoint(int x, int y) const noexcept override { /* 局部坐标 */ }
};
```

| 主题 | 关键 API | 语义要点 |
|---|---|---|
| 树 | `AddChild(std::unique_ptr<Widget>)` / `RemoveChild(Widget*)` | `AddChild` 三条前置条件在 Release 也有运行时分支：**空指针会写空崩、多父 double-free、成环 Arrange/Paint/HitTest 栈溢出**；`RemoveChild` 返回所有权（返回 `nullptr` = 空或非直接子） |
| 几何 | `SetPosition(x,y)` / `virtual SetSize(w,h)` | `SetSize` 是**尺寸唯一入口且是虚函数**（`ScrollView`/`CollapsiblePanel`/`TextBox` 都 override）；`SetPosition` 直接写几何、**不触发重绘** |
| 伸缩 | `SetStretch(n)` / `GetStretch()` | `0`=不参与分配（保持当前尺寸）；`>0`=主轴尺寸由父 `Layout` 按权重分配；负值钳 0 |
| 意图 | `virtual Size GetPreferredSize()` | 默认=当前尺寸；`TextWidget`/`TextBox` 按内容测量 override；`AutoSize()` 只在**显式调用**时把 preferred 落到 `SetSize` |
| 可见 / 启用 | `SetVisible(bool)` / `SetEnabled(bool)` | `SetVisible` 内建 `Invalidate` + 同值守卫（**守卫是正确性要求**：`TextBox` 每帧同步滚动条显隐，无守卫会自激励重绘）；`SetEnabled` 故意不重绘——全库只用它 gate 命中 / 焦点 / 键盘，**没有控件按 disabled 变色** |
| 事件 | `OnMouseMove/OnMouseButtonDown/OnMouseButtonUp/OnMouseWheel/OnKeyDown/OnKeyUp/OnCharInput/OnTimer/OnDropFiles/OnMouseEnter/OnMouseLeave` | 见 §9 |
| 焦点 | `CanFocus()` / `OnFocusGained/Lost` / `HasFocus()` / `SetShowFocusRect(bool)` | `SetShowFocusRect(false)` = 保留焦点能力但去掉焦点框（Button/CheckBox/Radio/TextBox 统一门控） |
| 命中 | `Widget* HitTest(x,y)`（公开） / `virtual ContainsPoint(x,y)`（protected，局部坐标） | `HitTest` 返回的控件就是事件目标（"返回即合法"契约）；但 `Panel` 恒不命中（§7.1）；禁止用 `ContainsPoint` 实现 `ClipsChildren` 门控 |
| 坐标 | `GetAbsolutePosition()` | 父链累加（含沿途内容偏移）= **视觉位置**；鼠标事件的 `GetMouseX/Y` 是**客户区绝对坐标**，控件内一律先减它（`ScrollBar`/`TextBox` 都这么干） |
| 裁剪 | `virtual bool ClipsChildren()`（默认 false） | true = 命中点必须落在本容器矩形内，否则不递归子树；**这是命中门控不是几何裁剪**。`ScrollView` override 为 true |
| 绘制 | `Invalidate()` / `Paint(PaintContext&, offsetX, offsetY)` | 无窗口（无头测试）时 `Invalidate()` 是 no-op |
| 布局失效 | `NeedsLayout()`（`[[nodiscard]]`，Phase 41） | 语义是**整棵所属树**、不是本节点（脏标志寄在根，沿父链上溯）⇒ 非根节点返回 `true` 意味着「同树别的分支有欠账」。用于测试与诊断；框架自身走帧首 `SettleLayoutIfDirty`（§4.4） |

**坐标不变量（全框架级，写自定义控件必读）**：`OnPaint` 收到的 `x,y` 是绝对位置；`HitTest` 传的是父链累加后的绝对坐标；自定义控件要**用局部坐标做命中、用绝对坐标转发绘制**。

---

## 4. 布局逻辑（核心）

### 4.1 分层

```
Widget 树（SetLayout / AddChild / RemoveChild）
   └── Arrange() ──► Layout::Arrange(parent) ──► 只调子控件的 SetPosition（见下）
```

- `Layout` 只做一件事：为**直接可见子控件**算位置（D9 契约：**只调 `SetPosition`，不调 `SetSize`**）。
- 因此**尺寸有两个来源**：① 子控件自己 `SetSize`（含 `AutoSize()`）② `SetStretch(n)` 交给父 Layout 分配。
- `SetStretch` 的数字是**权重**不是像素；权重相同的多个子控件按剩余空间等分，未被拉伸的子控件先量出 `GetPreferredSize()`（尺寸意图三分：显式 `SetSize` > `SetStretch` > `AutoSize`/内容测量，**后调用者赢**——Phase 9.7/9.8 契约）。

### 4.2 四种 Layout（ECDI/include/ECDI/Layout/）

| Layout | 构造 | 排列规则 |
|---|---|---|
| `VerticalLayout` | `(int spacing = 0, bool fillCrossAxis = false, int padding = 0)` | 纵向堆叠；`spacing` 子间距；`padding` 四边内缩；`fillCrossAxis=true` 时所有子跨轴 = 父跨轴 − 2×padding |
| `HorizontalLayout` | 同签名 | 横向排列；`spacing/padding` 语义同上 |
| `ListLayout` | `(int rowHeight, int spacing = 0)` | 固定行高按序排列；**只排 `IsVisible()` 的直接子**（连续槽位）；不可见子停泊到 `(-w, -h)`（对 `UpdateContentExtent` 贡献归零——隐藏行不污染内容范围）；**不读 parent 尺寸**（挂在 `ScrollContent` 上才安全） |
| `GridLayout` | `(int columns, int cellWidth, int cellHeight, int spacing = 0)` | 按序填格（`row = index/columns`，末行左对齐）；★ `cellWidth/cellHeight` 是**槽位间距不是尺寸约束**——子控件大于格子会溢出，不裁不缩，消费者自治 |

共同契约：`spacing/padding` 负值 Release 钳 0（Debug 断言）；`Arrange` 幂等，每次从头计算，**不依赖子控件当前位置**；布局只处理直接 children，不递归孙节点。

### 4.3 可见性是布局的一等公民（C-VIS）

四个 Layout 都只排列 `IsVisible()` 的直接子；不可见子被停泊到负区。**含义**：

- 用 `SetVisible(false)` 隐藏控件 = 它既不画、也不参与布局。
- 隐藏后**无需 `Arrange`**（`SetVisible` 内建 `Invalidate`；`OnResized` 链会重排）。
- `UpdateContentExtent()`（§5）按可见子推导，隐藏行不撑内容范围。

### 4.4 `Arrange` 什么时候发生

Phase 41 起，布局失效是**帧级结算**：改动只把**根**置脏，真正的重排发生在**帧首**（脏才排，排一次）。

| 时机 | 谁做 |
|---|---|
| 建树完毕、`Show()` 之前 | **你手动调 `root.Arrange()`**（推荐——让首帧就有正确几何） |
| `SetSize` / `SetLayout` / `SetStretch` / `AddChild` / `RemoveChild` | 被改动的控件置根脏（`NotifyGeometryChanged`，门控 = 「值**真变** ∧ 某 Layout 会读它」） |
| 窗口尺寸变化 | 根几何脏 → 帧首结算（Phase 41 删除 `Application::OnWindowResized` 的同帧重复 `Arrange`） |
| 帧首 | `Window::PaintFrame` → `SettleLayoutIfDirty()`（根与浮层根各一次） |

**实践建议**：建树期照旧写成「建树 → 设布局 → `Arrange()` → `Show()`」；**运行期不必再手动排**——改尺寸、增删子控件、改 `SetStretch` 都会在下一帧自动结算。要「立刻」而不是「下一帧」，仍可显式调 `parent.Arrange()`；查有没有欠账用 `NeedsLayout()`（§3.2）。

### 4.5 一个更真实的例子（左右分栏 + 底部固定）

```cpp
auto left  = std::make_unique<Panel>();  left->SetStretch(1);
auto right = std::make_unique<Panel>();  right->SetStretch(2);   // 2:1 分剩余
auto footer = std::make_unique<Panel>();
footer->SetStretch(1);                     // 底部也参与分配；要固定高度就 SetSize()
auto cols = std::make_unique<HorizontalLayout>(8, /*fillCrossAxis=*/true);
// ...
root.SetLayout(std::move(cols));
root.AddChild(std::move(left));
root.AddChild(std::move(right));
root.AddChild(std::move(footer));
```

`SetStretch(0)` 的控件保持自己 `SetSize` 的尺寸 ⇒ **「固定高度栏 + 可伸缩主区」= 底部栏 `SetStretch(0)+SetSize(h)`、主区 `SetStretch(1)`**。

---

## 5. 滚动容器 ScrollView

**职责边界**（Phase 15 定案，写文档时不得越界承诺）：只做**视口 + 内容偏移 + 内容范围 + 滚轮/拖拽条**；
**不**提供样式（要背景/圆角/边框，用外层 `Panel` 包住）；**不**做虚拟化 / ListBox；内容怎么摆是
你的事（在 `GetContentView()` 上挂 `ListLayout` / `GridLayout`）。

**正确用法**（坐标：偏移层之外 = 固定视口；偏移层之内 = 内容坐标系）：

```cpp
auto sv = std::make_unique<ScrollView>();
sv->SetSize(400, 300);                 // 视口尺寸（SetSize 会同步重算条 + clamp 偏移）
sv->SetScrollStep(28);                  // 一滚 28px（默认 kDefaultScrollStep = 32）

Widget& content = sv->GetContentView(); // 内容坐标空间的根，AddChild 加在它下面
content.SetLayout(std::make_unique<ListLayout>(/*rowHeight=*/24, /*spacing=*/4));
// ... content.AddChild(...) 你的行控件 ...

sv->UpdateContentExtent();              // ★ 显式动作：按可见子的 bbox 右下角推导内容范围
parent->AddChild(std::move(sv));        // 最后才挂上父容器
```

| API | 说明 |
|---|---|
| `GetContentView()` | 内容子控件加在这（实际类型是内部 `ScrollContent`，公开面是 `Widget&`） |
| `UpdateContentExtent()` | 从内容视图**直接子**重新推导 extent。定义 = 内容坐标系下二维包围盒右下角（负向钳 0）——**不是各子尺寸之和**（那样会漏掉 spacing） |
| `SetContentExtent(w,h)` | 显式设内容范围（动态内容自己做账时用） |
| `SetContentOffset(x,y)` / `GetScrollOffsetX/Y()` | 读 offset 用 `GetScrollOffsetX/Y`；命名不可改回 `GetContentOffsetX`（那是 Widget 虚接缝名，同名非虚成员会 name-hide） |
| `SetScrollStep(n)` / `GetScrollStep()` | 一滚像素，负值 debug 断言 + 忽略 |
| `SetScrollBarVisible(bool)` | 默认 true；**单个条**的显隐由溢出状态自动驱动（多行且内容溢出时显示） |
| `GetVerticalScrollBar()` / `GetHorizontalScrollBar()` | 非拥有指针，只读观察；**不要用 `ScrollBar::SetOffset` 反向驱动**（条的 offset 不是权威，权威在 `ScrollView`） |
| `GetMaxOffsetX/Y()` | `max(0, contentExtent − viewportExtent)` |
| `SetSize(w,h)` override | public：尺寸变化 → 原子重算（条几何 → clamp 偏移 → 同步条 → 失效） |
| `ClipsChildren()` = true | 命中约束在视口内：视口外的子控件**收不到事件** |

**坑**：装饰与交互子节点必须待在偏移层之外——挂在 `GetContentView()` 下的控件会随内容一起滚。
条（`ScrollBar`）的可拖动滑块由它自己实现；它的 `MainAxisPosFromClient` 负责把客户区坐标换算成控件局部坐标（漏了换算 = 拖拽与鼠标无关）。

---

## 6. 文本与输入控件

### 6.1 Label / 文本家族

```cpp
auto label = std::make_unique<Label>("状态：就绪");
label->SetText("状态：处理中");            // virtual（TextBox override 会失效行缓存）
label->SetTextColor(Color::FromRGBA8(0, 120, 215));
label->SetFont(Font{14.0f, "Microsoft YaHei"});   // size = DIP；family 空 = 系统默认
label->SetStyle(TextStyleOverride{});     // 单点覆盖（D7：标记后 ApplyTheme 不再覆盖该字段）
label->SetWordWrap(true);                 // ★ Phase 29 起 Label 支持换行（默认 false）
root.AddChild(std::move(label));
```

- `Label` = `TextWidget` 的「透明背景只画文本」特化（默认左对齐 + 垂直居中），无自带 Layout（由父容器排）。
- `SetTextColor` / `SetFont` 内部都转发 `SetStyle`——**单一状态来源**，没有第二份。

### 6.2 Button

```cpp
auto btn = std::make_unique<Button>("保存");
btn->SetOnClick([this] { Save(); });       // 覆盖式；传空 = 解注册
btn->SetStyle(ButtonStyleOverride{});      // 背景/边框/圆角/按下色/悬停色/对齐
```

- 调用链：`OnMouseButtonUp` → `RaiseClick` → **`OnClick()` 虚方法 + `m_onClick` 回调**（`RaiseXxx` 分离 = 继承扩展与业务回调两条独立的覆盖点）。
- `CanFocus()=true`、`ConsumesMouseInput()=true`（拖它不会误拖窗口）；Space/Enter 触发（焦点态）。
- 文字视觉唯一来源是 `TextWidget::m_style.foreground`；`ButtonStyle.foreground`（主题注入 `White`）
  与 `TextStyleOverride.foreground` 都能覆盖文字色，**后写者赢**（H10 契约）。

### 6.3 CheckBox / Radio

```cpp
auto cb = std::make_unique<CheckBox>("记住我");
cb->SetOnCheckedChanged([](bool checked) { /* 新状态 */ });
// cb->SetChecked(true);  程序可设任意值

auto r1 = std::make_unique<Radio>("A");
auto r2 = std::make_unique<Radio>("B");   // ★ 同父互斥：两者必须是同一个父的直接子
parent->AddChild(std::move(r1));          // Radio 分组依赖「直接父」——跨容器嵌套不互斥
parent->AddChild(std::move(r2));
```

- 唯一状态入口 `SetChecked(bool)`（virtual：`Radio` override 出互斥）；值变化才触发 `OnCheckedChanged` + 回调 + `Invalidate`，相同值 no-op。
- **Radio 互斥范围 = 直接父的直接子中类型为 `Radio` 的控件**（不跨 Container；没有 RadioGroup）。
- 用户点击不可取消单选（`OnClickToggle` = `SetChecked(true)` 非反转）；**程序 API 可以取消**。
- 通知顺序契约：`SetChecked(true)` → 兄弟先 `OnCheckedChanged(false)` → 自身后 `(true)`。
- 键盘：Space 切换（`StateWidget::OnKeyDown`）。

### 6.4 TextBox

```cpp
auto tb = std::make_unique<TextBox>();
tb->SetSingleLine(true);                 // Enter 不插 \n（粘贴含换行仍按多行渲染）
tb->SetEchoMode(TextBox::EchoMode::Password);  // 显示层打点 •，数据层仍是真实文本
tb->SetReadOnly(true);                   // 禁编辑，不禁导航/选择/复制/程序 SetText
tb->SetOnTextChanged([](const std::string& s) { /* 编辑操作才触发；SetText 不触发 */ });
tb->SetStyle(TextBoxStyleOverride{});    // 背景/边框/选区色/光标色/内距/圆角/垂直对齐（verticalAlign：Top 缺省 / Center 内容不足一屏时居中）
tb->SetSize(300, 28);                    // ★ 多行 TextBox 的尺寸由你 SetSize（GetPreferredSize 返回当前尺寸）
tb->SetText("初始值");                   // 只失效缓存 + Invalidate；不触发 OnTextChanged
```

- **编辑键内建**：←→ Home End、Shift+方向键选、Ctrl+A/C/V/X（剪贴板是平台能力，走 `GetPlatformWindow()->SetClipboardText` UTF-8）、Ctrl+Z/Ctrl+Y 撤销重做（快照模式，深度上限 100）、双击选词。
- `GetCaretClientGeometry()` 返回**客户区坐标**下的光标矩形（与 `GetAbsolutePosition()` / 事件鼠标坐标同系）。
- **`InsertCodepoint/InsertText/DeleteBackward/...` 编辑操作目前是 public（5.5.1 阶段为可测试，v1.0 API 审查定稿）**——不要在新代码里把它们当稳定 API 依赖。
- IME 组合期不触发 `OnTextChanged`、不产生快照；`CommitComposition("")` 是合法提交。
- 垂直滚动条**自动显隐**（多行且内容溢出 + 主题驱动）；`GetVerticalScrollBar()` 是只读观察入口。
- **富文本**：只读 `TextBox` 可显示段内样式（`SetStyledText`，§6.6）；**可编辑 `TextBox` × spans 本期不支持**（Phase 44 红线 4，记账 `L44-1`）——编辑类场景先按纯文本用。

### 6.5 进度条家族

```cpp
auto bar = std::make_unique<ProgressBar>();
bar->SetSize(200, 8);          // 尺寸你给；cornerRadius=0 表示「自动半圆」= 高度/2
bar->SetPercent(42);           // clamp [0,100]；SetProgress(float) clamp [0,1]

auto ring = std::make_unique<RingProgressBar>();
ring->SetSize(48, 48);         // 直径 = min(w,h)，圆心居中——宽高不等不拉伸不报错
ring->SetProgress(0.75f);      // NaN ⇒ 拒绝（值不变、不启动动画）
```

- 两个控件都是**状态分离**：`m_progress` 目标逻辑 / `m_displayProgress` 呈现视觉（动画驱动）；`SetProgress` 只改目标并启动过渡，`OnPaint` 只读呈现值；`GetProgress()` 返回**目标值**。
- 无动画宿主（无头测试）时 `ResolveAnimationManager()==nullptr` ⇒ 瞬时到位。

### 6.6 富文本（段内内联样式，Phase 44 / 45）

```cpp
#include <ECDI/Widget/TextSpan.h>

auto label = std::make_unique<Label>();
label->SetStyledText("粗体 常规 mono", {
    { 0, 2, SpanStyle{ .weight = 700 } },                        // [0,2) 码点 → 粗体
    { 6, 4, SpanStyle{ .family = "Consolas" } },                 // [6,10) → monospace
    { 3, 2, SpanStyle{ .foreground = Color::FromRGBA8(200, 0, 0),
                       .italic = true, .size = 16.0f } },        // 颜色 + 斜体 + 字号
});
```

| 概念 | 契约 |
|---|---|
| 索引空间 | **码点**（UTF-8 解码后的字符位，**不是字节**）；区间半开 `[startCp, startCp + cpCount)` |
| `SpanStyle` 可覆盖字段 | `family` · `weight`（400 = Regular / 700 = Bold）· `foreground` · `italic`（Phase 45）· `size`（DIP 绝对值，Phase 45）；`std::nullopt` = **继承**控件基样式（`TextStyle`） |
| 生效控件 | `TextWidget` 家族——`Label` / `Button`，以及**只读** `TextBox`（§6.4） |
| 失效值 | `size` 非有限或 `≤ 0` ⇒ 视同 `nullopt`（不覆盖），不会画出异常字号 |
| 规范化 | 起点越界 ⇒ 丢弃；尾部越界 ⇒ 钳到串末；空区间 ⇒ 丢弃；按起点 `stable_sort`；重叠 ⇒ **先到者赢**（后到被裁剪或整段剔除） |
| 空 spans | 等价纯文本，**逐位等价**——不设 / 传 `{}` 时命令流、测量、折行与今日完全一致（C44-1） |

三条容易踩的：

- **`SetText` 会让 spans 失活**（串身份自检 C44-8）：`SetText` 改串后旧 spans 不再生效；要带样式就重新 `SetStyledText`，别指望它自动跟着走。
- **行高由基控件字体决定**（C44-3）；Phase 45 起混字号按「该行最大字号」建行框，所以同一行内不同字号会与该行最高 run 对齐。
- **跨族混排有已知降级**：同一串里 `family` 不同时，两链统一按单元格顶边对齐，存在至多约 7px 的 run 内基线偏置（`L44-9`）；**斜体**在 GDI 走合成斜切、GL 选真斜体面（族内无真斜体面 ⇒ GL 回落常规面，不合成——`C36-4` 同族已知分歧）。要求严格基线一致时避免跨族；斜体优先挑有真斜体面的族。

---

## 7. 容器、分组与窗口装饰

### 7.1 Panel

- **纯容器语义**：组织和分组子控件，自身无行为。
- **`Panel` 永不参与命中**（`ContainsPoint` 恒 false）⇒ 鼠标事件只由子控件接收。
  ⚠️ 副作用：把 `Panel` 当「整块点击区」不会命中——要可点区域用 `Button`/`ScrollView`（后者空白可滚）或自绘控件。
- 默认**背景透明**（RGBA 0,0,0,0）；要底色/边框：`panel->SetStyle(PanelStyleOverride{...})`。
- `Panel` 自己不实现 Layout：子控件排列由你 `panel->SetLayout(...)` 或手动 `SetPosition`。

### 7.2 CollapsiblePanel

```cpp
auto cp = std::make_unique<CollapsiblePanel>();
cp->SetExpandDirection(CollapsiblePanel::ExpandDirection::Right); // ★ 须在首次 SetSize 前
cp->SetPosition(x, y);
cp->SetSize(240, 200);          // 收起态：SetSize 定义「展开基准」；实际呈现收缩到 0
// 折叠时只有「非动画轴」保持给出值（Right ⇒ 高度=200，宽呈现 0）
cp->Toggle();                   // 等价 SetExpanded(!IsExpanded())
Widget* content = cp->GetContent();   // 内容容器（铺满面板，不画背景）
content->AddChild(std::move(myWidget));
```

- 初始化顺序约定（无运行时 assert）：**`SetExpandDirection` → `SetPosition` → `SetSize`**。
- 默认 `IsExpanded() == false`（初始收起）。
- header **完全外部自组**——本控件不持有/管理/隐藏标题；折叠是 resize 收缩，不是 translate 滑出。
- **v0.1 已知限制**：动画进行中的外部 `SetPosition/SetSize` 不是支持场景。
- 四向：`Down/Up` 收缩高度轴、`Right/Left` 收缩宽度轴；`Up` 锚底边、`Left` 锚右边。

### 7.3 自绘标题栏（Borderless + CaptionBar）

```cpp
ECDI::Window& w = application.Create("标题", 900, 600);
w.SetChromeMode(ECDI::ChromeMode::Borderless);          // 配置期，一次确定（重复调用 Warning+忽略）
w.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);    // 行为区 = 系统标题栏命中区（DIP）
auto bar = std::make_unique<ECDI::CaptionBar>(w, "标题");
bar->SetSize(900, ECDI::CaptionBar::kDefaultHeight);     // 实体区 = 控件几何
w.GetRootWidget().AddChild(std::move(bar));
bar->SetTitle("新标题");
```

- `CaptionBar` 是**组合式控件**（继承 `Panel`，`Panel::ContainsPoint` 恒 false ⇒ 空白区天然是拖拽区），内部 = 标题 `Label` + 最小化 / 最大化·还原 / 关闭 三个矢量按钮（零资源依赖）。
- 它只**发起**窗口命令（`Minimize` / `Maximize` / `Restore` / `RequestClose`）并查询状态，**非拥有** `Window&`；系统 `NCHITTEST` 委托进 Widget 树（Phase 13）。
- **两个区不联动**：`SetCaptionHeight`（行为区）与控件 `SetSize`（实体区）由你保持一致（建议都取 `kDefaultHeight = 32`）。
- 宽度自适应：`SetSize` override 内部重排子控件 ⇒ 父容器 `fillCrossAxis` 布局下窗口 resize 零接线。
- `SetResizeInset` 控裁边热区；交互控件 `ConsumesMouseInput()==true` 是拖拽不误触的前提。

### 7.4 浮层（overlay，Phase 42）

窗口有**两个根**：`GetRootWidget()`（内容树）+ `GetOverlayRoot()`（浮层根，几何覆盖全客户区）。浮层因此**不受内容树裁剪影响**——这是「在下拉列表里点开二级菜单不被父容器切掉」的结构性保证。

```cpp
auto popup = std::make_unique<Panel>();     // 浮层 = 任意 Widget
popup->SetSize(180, 120);                   // ★ 尺寸必须你自己给（框架不替你量）
popup->SetPosition(40, 40);                 // 无锚点时才用得上（见下）
popup->SetLayout(std::make_unique<VerticalLayout>(4, true));

window.ShowOverlay(std::move(popup), {
    .layer        = OverlayLayer::Normal,               // Normal / Modal / Tooltip
    .lightDismiss = LightDismissPolicy::PassThrough,    // 点外关闭策略
    .closeOnEsc   = true,                               // ESC 关顶层
    .exemptWidget = triggerButton,                      // 点它不算「点外」
    .anchorWidget = triggerButton,                      // 可选：跟随锚点自动定位
    .placement    = AnchorPlacement::Below,             // Below / Above / Left / Right
    .ownerOverlay = nullptr,                            // 逻辑父浮层（级联关闭）
    .onClosed     = [] { /* 已脱树 */ },
});

window.HideOverlay();                                     // 无参 = 关当前顶层
bool open = window.HasActiveOverlay(OverlayLayer::Modal); // 无参 = 任意层

// 底阶原语：只把控件挂到浮层根，**不带** light dismiss / ESC / 焦点转移
window.GetOverlayRoot().AddChild(std::move(someWidget));
```

| 层 | 顺序 | 行为 |
|---|---|---|
| `Tooltip` | 最高 | 纯视觉，**不参与命中**（点击穿透到底下） |
| `Modal` | 中 | 存在时 Tab 焦点被**陷阱**在模态子树内循环；关闭后焦点安全恢复到打开前的控件 |
| `Normal` | 低 | 下拉列表 / 气泡 / 右键菜单 |

- **点外关闭三策略**：`PassThrough`（默认：关闭浮层 + 这次点击**继续**下派给底层控件）/ `Consume`（关闭 + 吞掉这次点击）/ `None`（强模态，不自动关）。
- **`exemptWidget` 必填于弹出型控件**：把触发按钮填进去，否则「点按钮关掉、再点又开」会抖成两次。
- **锚点跟随会改写位置**：给了 `anchorWidget` 就由框架按 `placement` 算坐标并做**翻转 + 夹取**（贴边时自动翻到另一侧、超出客户区则夹回），你设的 `SetPosition` 不再生效。
- **关闭是延迟析构**：`HideOverlay` 先脱树 → `onClosed()` → 帧末才真正析构（防回调栈内 UAF）⇒ 回调里别再用那个指针判断存活。
- 返回值 `false` = 参数非法（空指针 / 层冲突）或正在关闭；不要重试，先查状态。

---

## 8. 主题与样式（Theme 系统）

- `Theme` 是**规范提供者**：9 个 `GetXxxStyle()` 返回**值**（不返回引用）——Theme 不管理 Style 生命周期。
- `DefaultTheme` + 自由函数 `GetDefaultTheme()`（static local，**不是单例**，无全局 ThemeManager）。
- 每个控件构造时把自己所需的 Style 用**主题默认值**注入自己的 `m_style` 成员；`ApplyTheme(const Theme&)`
  在主题生效时刷新。`SetStyle(XxxStyleOverride)` 是**单点覆盖**：`StyleField::Set()` 标记 `overridden`
  后，后续 `ApplyTheme` **不再覆盖该字段**（D7 契约）。

| Style | 字段 | 注意 |
|---|---|---|
| `TextStyle` | `foreground, font` | 文字视觉**唯一**来源 |
| `ButtonStyle` | `background, border, borderWidth, cornerRadius, pressedBackground, hoverBackground, textAlignment, foreground` | 文字色来自 `foreground`（默认 White）；`TextStyleOverride.foreground` 优先级更高，后写者赢 |
| `PanelStyle` | `background, cornerRadius, borderWidth, borderColor` | `borderWidth>0` 双矩形描边环 |
| `ProgressBarStyle` | `trackColor, fillColor, cornerRadius` | `cornerRadius=0` = 自动半圆（高度/2），**当前无法表达方角** |
| `RingProgressBarStyle` | `trackColor, fillColor, thickness` | `thickness<=0` 后端判定不绘制 |
| `CheckBoxStyle` / `RadioStyle` / `ScrollBarStyle` / `TextBoxStyle` | 各有专属字段 | `TextBoxStyle` 无 `foreground`（文字走 `TextStyle`）；`verticalAlign`（Phase 37）= 垂直对齐，缺省 `Top` 逐位不变，`Center` 仅在内容高 ≤ 文本区高时居中（超屏自动退化 `Top`）；`ScrollBarStyle.thickness` 参与 `ScrollView` 视口计算 |

换肤 = 给控件 `SetStyle(...)`（控件级覆盖）或进入 `ApplyTheme` 路径（全局刷新）；
`StyleField::Reset()` 只清标志**不恢复值**，Widget 层无清除 API。

---

## 9. 事件与动画

### 9.1 事件流

```
Win32 消息 → PlatformWindow 翻译 → Event → EventRouter::OnEvent
   → Application（应用级 override：OnWindowCreated/Destroyed/Resized/StateChanged/CloseRequested/
      OnTimer/OnMouse*/OnKey*/OnCharInput/OnDropFiles/OnTrayEvent）
   → 目标 Widget（命中链上的 OnMouse* / 焦点链上的 OnKey* / 冒泡给父）
```

- 键盘走**焦点链**（`CanFocus` 控件）+ `Application` 路由；鼠标走 `HitTest` 目标 → 父链冒泡。
- hover 有专门的事实链：目标切换 `A → B` 严格 `A.OnMouseLeave()` → `B.OnMouseEnter()`；
  控件脱树导致的失效**不派发** `OnMouseLeave`（`HoverTracker` 契约 C/D）。
- 鼠标事件带**按键与修饰键维度**（Phase 19）；`TimerEvent` 供 `OnTimer` 用。

| 查询（鼠标事件通用） | 语义 |
|---|---|
| `GetMouseX()` / `GetMouseY()` | **窗口客户区绝对坐标**（不是控件局部坐标） |
| `GetButton()`（Down/Up 事件） | 触发本次事件的按键：`MouseButton::Left` / `Right` / `Middle` |
| `IsButtonDown(MouseButton)` | 事件发生时**该键是否处于按下态**（含本次）——多键组合、拖拽判定用它 |
| `HasModifier(KeyModifier)` | `Shift` / `Ctrl` / `Alt`（可位或组合） |
- 应用级事件（托盘）`GetWindow()` 恒 `nullptr`；`TrayEvent::GetX/Y` 是**屏幕坐标**（菜单定位直接可用）。

**事件消费（Phase 35）**——回调里 `event.SetHandled()` 即**停止向祖先冒泡**。冒泡循环每节点判两个条件：该节点已被移除（跳过）、事件已消费（停）。

| 你要的事 | 写法 |
|---|---|
| 子按钮点击**不要**连带触发父行 | 不用做——`Button` 自动消费 Down 与 Up（`Button.cpp:126-148` Down / `Button.cpp:152-177` Up） |
| 自定义控件也「子优先」 | `OnMouseButtonDown` 与 `OnMouseButtonUp` **都**调 `event.SetHandled()`（只消费一个阶段 = 父控件按下态卡死） |
| 子控件**不**消费、交还给父 | 什么都不写（默认即冒泡） |
| 点击回调里删自己 / 删包含自己的祖先 | `Widget::RequestRemove()`（**不要**用 `RemoveChild`——它立即析构）。派发中 `RequestRemove` 只摘树、**整个派发结束后**才析构 |

「父行要能切换、子按钮要能删除」的复合控件：**父行的业务动作不能放在父行自己的点击回调里**，放进子按钮的回调（先 `RequestRemove()` 再执行父级的切换语义），或干脆让父行不做点击控件。完整语义见 [docs/dev/规范/事件消费与安全销毁规范.md](dev/规范/事件消费与安全销毁规范.md)。

### 9.2 动画（per-Window AnimationManager）

```cpp
class Fader : public Widget {
    AnimationToken token_;          // 每个动画属性持一个 token 成员
    float alpha_ = 0.0f;
    void FadeTo(float to) {
        if (Window* w = GetWindow()) {
            w->GetAnimationManager()->Start(
                token_, /*from=*/alpha_, /*to=*/to, 200ms, Easing::EaseOut,
                [this](float v) { alpha_ = v; Invalidate(); });
        }
    }
};
```

硬契约：**值回调式**（`Animation` 不认识目标，属性由持有者写）；**同 token 再 `Start` = 替换式重启**
（旧动画静默移除、**不调 `onFinished`**，`from` 由你传当前呈现值）；`onFinished` 只在**正常到终点**触发；
完成帧顺序 `onValue(final)` → `onFinished` → 移除；`onValue` 内允许 `Start/Cancel`（本轮新动画下一轮才推进）。
`Easing` 四种：`Linear` / `EaseIn`(t²) / `EaseOut`(1−(1−t)²) / `EaseInOut`；`ApplyEasing` 自身 clamp `t∈[0,1]`。
token 析构 = 标脏失效 ⇒ 回调永不打在死对象上。

### 9.3 图片（WIC 解码）

```cpp
Image img = ECDI::Decode::DecodeFile(u8"assets/logo.png");   // 或 DecodeMemory / DecodeSystemIcon
// Image: 32bpp 预乘 BGRA top-down；width==0||height==0 ⇒ 空，DrawImage 跳过
```

失败返回空 `Image` **不抛异常**（经 Logger 记 Error）；格式由系统 WIC 决定。
`DecodeFile("x.ico")`（解文件内容）与 `DecodeSystemIcon("x.ico")`（取系统图标）**不是同一件事**。

### 9.4 自绘控件（ PaintContext 门面 ）

见 §10；注意 `DrawArc(center, radius, startAngleDeg, sweepAngleDeg, width, color)`：**0° = 3 点钟，正角 = 顺时针**，`|sweep|>360` 在执行端钳制；方法自身不做角度归一化。

### 9.5 文件拖放（Window 级，Phase 14）

```cpp
window.SetFileDropEnabled(true);        // ★ 运行期调用；Show() 之前调记 Warning 并忽略

class DropZone : public Panel {
protected:
    void OnDropFiles(const DropFilesEvent& e) override {
        for (const auto& path : e.GetPaths()) {   // UTF-8 绝对路径列表
            if (path.empty()) continue;
            LoadDocument(path);
        }
        e.SetHandled();                            // 需要时阻止继续冒泡（可选）
        Invalidate();
    }
};
```

- **派发路径与鼠标同族**：平台 → `Application` → `HitTest(e.GetX(), e.GetY())` 找目标 → `OnDropFiles` → 父链冒泡。
- 落点 `GetX/Y()` 是**客户区绝对坐标**，控件内做局部判定要自己减偏移（同鼠标事件）。
- 平台层已完成 `DragFinish`：事件携带的是**纯数据**（`std::vector<std::string>`），不含系统句柄。
- `GetPaths()` **可能为空**（无有效文件）——消费方必须忽略，而不是当错误。
- 应用级也能接：`Application::OnDropFiles`（`GetWindow()` **恒非空**——与托盘事件的语义不同）。

---

## 10. 自定义控件最小骨架

```cpp
class Badge : public Widget {
public:
    void SetCount(int n) { if (n != count_) { count_ = n; Invalidate(); } }
protected:
    bool ContainsPoint(int x, int y) const noexcept override {   // 局部坐标
        const float cx = GetWidth() * 0.5f, cy = GetHeight() * 0.5f;
        const float dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= cx * cx;          // 圆形命中
    }
    void OnPaint(PaintContext& ctx, int x, int y) override {       // 绝对坐标
        const Rect r{ static_cast<float>(x), static_cast<float>(y),
                      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };
        if (!ctx.IsRectVisible(r)) return;             // 视口剔除（Phase 27）
        ctx.DrawRoundedRect(r, 6.0f, Color::FromRGBA8(220, 53, 69));
        ctx.DrawText({ r.x + 6, r.y + 4 }, std::to_string(count_),
                     Color::White(), Font{12.0f, ""});
    }
private:
    int count_ = 0;
};
```

`PaintContext` 绘制门面（`Render/PaintContext.h:22-104`）：`DrawRect / DrawText / DrawLine / DrawArc /
DrawRoundedRect / DrawImage / PushClip / PopClip / DrawFocusRect / IsRectVisible / MeasureText / LineHeight`。
**坐标语义 = 最终坐标零坐标逻辑**：控件算好对齐后原样传。`MeasureText` 与 `DrawText` 必须同源
（同一个 `TextMeasurer`，经 `ctx.GetTextMeasurer()`）。

---

## 11. 已知限制与坑

| # | 事实 | 影响 / 对策 |
|---|---|---|
| 1 | `SetEnabled()` 无任何绘制消费者 | 没有控件「变灰」；disabled 只 gate 命中/焦点/键盘。要视觉反馈自己画 |
| 2 | `Panel` 恒不命中 | 整块点击需求别用 Panel 承载 |
| 3 | `TextBox` 的编辑操作是临时 public（5.5.1 可测试性） | 新代码别依赖稳定签名；v1.0 API 审查定稿 |
| 4 | `ProgressBar` 的 `cornerRadius=0` = 自动半圆 | 当前无法表达方角进度条 |
| 5 | `CollapsiblePanel` 动画中外部 `SetPosition/SetSize` | 不支持场景（v0.1 已知限制） |
| 6 | `ScrollView` 的条显隐自动（溢出驱动） | 想固定显隐用外层 Panel 包 + 自行控制 |
| 7 | `AddChild` 三条前置条件 | 空指针/多父/成环在 Release 也有防护，但成环的症状是 Arrange/Paint/HitTest 栈溢出 |
| 8 | Win32 宏 `DrawText` 与 `PaintContext::DrawText` 同名 | 同时包 `Windows.h` 与 ECDI 头时 `#ifdef DrawText / #undef DrawText`（框架内部已防御性处理，用户侧照写无害） |
| 9 | `WindowLayer::Bottom` 不承诺「绝对最底」；`Win+D` 时与普通窗口一起隐藏 | 桌面层需求用 `Desktop` |
| 10 | DPI | 框架声明 Per-Monitor-V2；`Font::size` 是 DIP；`SetBounds/GetBounds` 用屏幕 DIP |
| 11 | 输入层半抽象、多窗口焦点语义、光标色未主题化 | 均在 `dev/README.md` 技术债务表与 `dev/roadmap-deferred.md`（v1.0 API 审查项） |
| 12 | `Button` 的 Down/Up 都会被自动消费（Phase 35） | **嵌套可点击控件时父行收不到点中子按钮的那次点击**——把父级动作放进子按钮回调（见 §9.1「事件消费」） |
| 13 | `RemoveChild` 立即交回所有权（很可能马上析构） | 事件回调里删控件只用 `RequestRemove()`；延迟边界 = **整次派发结束**，不是单次回调 |
| 14 | 富文本只覆盖**显示**通路：可编辑 `TextBox` × spans 不支持（Phase 44 红线 4，记账 `L44-1`） | 编辑类场景按纯文本用，或自绘；只读预览可用 `SetStyledText`（§6.6） |
| 15 | 跨族混排（同串不同 `family`）有至多约 7px 的 run 基线偏置；GL 族内无真斜体面时回落常规面 | 要求严格基线一致就别跨族；斜体挑有真斜体面的族（GDI 会合成斜切，两链因此可能不一致） |
| 16 | 浮层 Widget 的尺寸与位置要自己给；`GetOverlayRoot().AddChild` 只是挂上去，**不带**点外关闭 / ESC / 焦点转移 | 要交互闭环一律走 `ShowOverlay`（§7.4） |

---

## 12. 从「能跑」到「能交付」

1. **接库**：`find_package(ECDI CONFIG REQUIRED)` + `target_link_libraries(app PRIVATE ECDI::ECDI)`；
   安装前缀的产出见 [仓库根 README §安装与消费](../README.md)。
2. **建窗口**：`Application::Create` → 拿 `GetRootWidget()` → 建树 → 布局 → `Arrange()` → `Show()` → `Run()`。
3. **接业务**：`SetOnXxx` 回调先跑通；需要状态机/复用再 `override OnXxx`。
4. **接线程**：工作线程一律 `Application::PostToUi` 回 UI 线程（**不要**跨线程直接摸控件）。
5. **交付前**：跑 `examples/VisualTest`（全控件目视）+ `examples/ModelProbe`（可探针工具）；
   数值型需求（布局/成本）先看 harness 式探针的做法——**目录探针、帧时、命令混合**都在
   `ECDI_fake_harness` 的 `--cull-probe` 里现成。

---

## 13. 延伸到更多

| 想知道 | 去哪 |
|---|---|
| 为什么这样设计（逐 Phase 三件套） | [dev/README.md](dev/README.md) 的逐阶段索引 |
| 库化 / 安装 / 外部消费 | [仓库根 README](../README.md) §安装与消费 |
| 各控件详设（契约级） | `dev/phase5.x-*` / `dev/phase9.x-*` / `dev/phase15-*` 等对应详设 |
| 已知限制 / 排期 | `dev/README.md` 技术债务表 + `dev/roadmap-deferred.md` |
| 可运行样例 | `examples/`（MinimalApp · ModelProbe · VisualTest · DesktopNest） |
| 富文本（styled spans）契约 | `dev/phase44-styled-spans-detailed-design.md` · `dev/phase45-styled-spans-expressiveness-detailed-design.md` |
| 浮层与点外关闭 | `dev/phase42-window-overlay-root-detailed-design.md` |
| 帧级布局失效结算 | `dev/phase41-frame-layout-invalidation-pipeline-detailed-design.md` |
| 文件拖放 / 托盘 | `dev/phase14-tray-and-drop-detailed-design.md` |

## 14. 再分发义务（FreeType / FTL）

ECDI 静态链接 **FreeType 2.14.3**（GL 后端文本栈，vendored 于仓库 `third_party/freetype`，双许可取 **FTL**）。凡以**二进制**形态分发嵌入了 ECDI 的产物（exe / dll / 静态库 / 安装包），FTL §2 要求在**分发文档**中包含如下声明：

> This product is based in part on the work of the FreeType Team (https://freetype.org).

FreeType Team 首选致谢格式（FTL 引言，鼓励采用；`<year>` 取所用版本版权年）：

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

### 14.1 安装前缀自带许可（`cmake --install`）

FreeType 的目标码已并入 `ECDI` 静态库（OBJECT 库合并）⇒ **install 前缀本身即二进制分发**，许可不能只留在源码仓。`install()` 落盘（`<prefix>` 下）：

| 路径 | 内容 |
|---|---|
| `share/licenses/ECDI/LICENSE` | ECDI 本体 MIT 许可（MIT 亦要求声明随副本） |
| `share/licenses/ECDI/NOTICE.md` | 再分发声明：based-in-part + 首选致谢 + 版本 + 许可指针 |
| `share/licenses/ECDI/FreeType/LICENSE.TXT` | FreeType 许可入口（双许可说明 + 子许可指引） |
| `share/licenses/ECDI/FreeType/FTL.TXT` | FTL 全文（本项目所依据的许可） |
| `share/licenses/ECDI/FreeType/GPLv2.TXT` | GPLv2 全文（`LICENSE.TXT` 正文引用，缺则断链） |

★ `GPLv2.TXT` 不是「顺手多带」：`LICENSE.TXT` 正文明确指向它，只带 `FTL.TXT` 会重演 Phase 26 vendor 时修过的「引用断链」缺口（详设 §11.3）。配置期对五份文件做存在性断言，缺一即 `FATAL_ERROR`。

**源码**形态再分发只须把 `third_party/freetype/LICENSE.TXT` / `docs/FTL.TXT` / `docs/GPLv2.TXT` 原样随附；其正文引用的子许可（bdf/pcf X11 风格、gzip/zlib、autofit `ft-hb-*` Old MIT）已随 `src/` 保留。对 FreeType 原始文件的裁剪与「零本地改动」说明见 `third_party/freetype/README.ecdi.md`（FTL §2 第一项要求增删改在随附文档中说明）。FTL §3：未经许可不得将 FreeType 名称用于商业、广告或推广背书。

### 14.2 交付单固定字段

以二进制形态交付 ECDI（`ECDI.lib` / `libECDI.a` / 安装前缀）时，交付单必须包含：

| 字段 | 说明 |
|---|---|
| **notice 项** | 声明本库内嵌 FreeType 2.14.3、取 FTL，并给出 `share/licenses/ECDI/NOTICE.md` 指针（缺此即不满足 FTL §2 二进制项） |
| **库指纹** | 交付的 `.lib` / `.a` 的 **SHA256 + 字节数**（接收方按此复核；下游 `THIRD-PARTY-NOTICES` 的指纹表以实际链入的库为准） |
| **版本** | ECDI 版本宏 + FreeType 版本（致谢年份随 FreeType 版本版权年走） |
| **许可文件** | 确认 `share/licenses/ECDI/` 五份文件随前缀交付 |

★ 交付单是**指纹**，不是复印件：notice 正文只写进 `NOTICE.md`（随前缀），交付单只固定「已含 notice + 这份库的 SHA256」，避免两处声明漂移。
