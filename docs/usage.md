# ECDI 使用指南

> 本文是 **ECDI 框架的使用文档**——面向「要把 ECDI 用起来的第三方开发者」，讲清三件事：
> **① 各控件怎么用**（每个控件的构造、配置、回调、常见组合）· **② 布局逻辑**（谁排谁、Stretch 与
> Layout 的关系、`Arrange` 何时发生）· **③ 从零接一个新窗口的完整路径**（建 → 摆 → 交互 → 跑）。
> 设计文档（「为什么这样设计」）在 [docs/README.md](README.md)；项目总览（定位 / 构建 / 四工具链）在
> [仓库根 README.md](../README.md)。

**声明与约定**

- 所有 API 结论均以 `include/ECDI/**` 的公共头为准（98 个）；文中标注 `ECDI/include/...` 的是可直接核对的源头。
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

### 2.1 Application（ECDI/include/ECDI/Application/Application.h:43-180）

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

### 2.2 Window（ECDI/include/ECDI/Window/Window.h:46-436）

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
`SetFileDropEnabled(bool)`（开启 `WM_DROPFILES` → `OnDropFiles`）· `SetCaptureWidget/GetCaptureWidget`
（拖拽期鼠标捕获；配合 `Application::PostToUi` 时可跨线程投递回调）· `HandleKeyDown` / `UpdateHoverState`。

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

### 3.2 Widget 基类（ECDI/include/ECDI/Widget/Widget.h:39-350）

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

| 时机 | 触发者 |
|---|---|
| 建树完毕、`Show()` 之前 | **你手动调 `root.Arrange()`**（示例的固定动作） |
| 窗口尺寸变化 | `OnResized` 链自动 `Arrange`（Phase 9.7 触发链；所以运行期缩窗不用手动排） |
| `SetLayout` / 第一次 `SetSize` | 容器自身 |
| `SetStretch` 改变 | 下一次 `Arrange` 生效（`SetStretch` 自己不重排） |

**实践建议**：把「建树 → 设布局 → `Arrange()` → `Show()`」写成固定五步；运行期增删子控件后调一次
`parent.Arrange()`（`AddChild/RemoveChild` 不自动重排父容器）。

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
tb->SetStyle(TextBoxStyleOverride{});    // 背景/边框/选区色/光标色/内距/圆角
tb->SetSize(300, 28);                    // ★ 多行 TextBox 的尺寸由你 SetSize（GetPreferredSize 返回当前尺寸）
tb->SetText("初始值");                   // 只失效缓存 + Invalidate；不触发 OnTextChanged
```

- **编辑键内建**：←→ Home End、Shift+方向键选、Ctrl+A/C/V/X（剪贴板是平台能力，走 `GetPlatformWindow()->SetClipboardText` UTF-8）、Ctrl+Z/Ctrl+Y 撤销重做（快照模式，深度上限 100）、双击选词。
- `GetCaretClientGeometry()` 返回**客户区坐标**下的光标矩形（与 `GetAbsolutePosition()` / 事件鼠标坐标同系）。
- **`InsertCodepoint/InsertText/DeleteBackward/...` 编辑操作目前是 public（5.5.1 阶段为可测试，v1.0 API 审查定稿）**——不要在新代码里把它们当稳定 API 依赖。
- IME 组合期不触发 `OnTextChanged`、不产生快照；`CommitComposition("")` 是合法提交。
- 垂直滚动条**自动显隐**（多行且内容溢出 + 主题驱动）；`GetVerticalScrollBar()` 是只读观察入口。

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
| `CheckBoxStyle` / `RadioStyle` / `ScrollBarStyle` / `TextBoxStyle` | 各有专属字段 | `TextBoxStyle` 无 `foreground`（文字走 `TextStyle`）；`ScrollBarStyle.thickness` 参与 `ScrollView` 视口计算 |

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
- 鼠标事件带 `wParam` 维度（按下键位 + 修饰键，Phase 19）；`TimerEvent` 供 `OnTimer` 用。
- 应用级事件（托盘）`GetWindow()` 恒 `nullptr`；`TrayEvent::GetX/Y` 是**屏幕坐标**（菜单定位直接可用）。

**事件消费（Phase 35）**——回调里 `event.SetHandled()` 即**停止向祖先冒泡**。冒泡循环每节点判两个条件：该节点已被移除（跳过）、事件已消费（停）。

| 你要的事 | 写法 |
|---|---|
| 子按钮点击**不要**连带触发父行 | 不用做——`Button` 自动消费 Down 与 Up（`Button.cpp:126-148` Down / `Button.cpp:152-177` Up） |
| 自定义控件也「子优先」 | `OnMouseButtonDown` 与 `OnMouseButtonUp` **都**调 `event.SetHandled()`（只消费一个阶段 = 父控件按下态卡死） |
| 子控件**不**消费、交还给父 | 什么都不写（默认即冒泡） |
| 点击回调里删自己 / 删包含自己的祖先 | `Widget::RequestRemove()`（**不要**用 `RemoveChild`——它立即析构）。派发中 `RequestRemove` 只摘树、**整个派发结束后**才析构 |

「父行要能切换、子按钮要能删除」的复合控件：**父行的业务动作不能放在父行自己的点击回调里**，放进子按钮的回调（先 `RequestRemove()` 再执行父级的切换语义），或干脆让父行不做点击控件。完整语义见 [docs/规范/事件消费与安全销毁规范.md](规范/事件消费与安全销毁规范.md)。

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
| 11 | 输入层半抽象、多窗口焦点语义、光标色未主题化 | 均在 `docs/README.md` 技术债务表与 `roadmap-deferred.md`（v1.0 API 审查项） |
| 12 | `Button` 的 Down/Up 都会被自动消费（Phase 35） | **嵌套可点击控件时父行收不到点中子按钮的那次点击**——把父级动作放进子按钮回调（见 §9.1「事件消费」） |
| 13 | `RemoveChild` 立即交回所有权（很可能马上析构） | 事件回调里删控件只用 `RequestRemove()`；延迟边界 = **整次派发结束**，不是单次回调 |

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
| 为什么这样设计（逐 Phase 三件套） | [docs/README.md](README.md) 的逐阶段索引 |
| 库化 / 安装 / 外部消费 | [仓库根 README](../README.md) §安装与消费 |
| 各控件详设（契约级） | `docs/phase5.x-*` / `phase9.x-*` / `phase15-*` 等对应详设 |
| 已知限制 / 排期 | `docs/README.md` 技术债务表 + `roadmap-deferred.md` |
| 可运行样例 | `examples/`（MinimalApp · ModelProbe · VisualTest · DesktopNest） |
