# Phase 18 · 窗口/根背景能力（Window/Root background）—— 详细设计

> 状态：**v1.1**（2026-09-22）——**第二轮评审（针对 v1.0）结论：🟢 详细设计通过，可进入实现**（无阻塞项）
> v1.1 = 该轮 14 条意见的处置稿：**措辞精确化**（B14 / N1 的定性用词）· **新增 §1.4 逐条处置 + §3.1 实现盯防清单** · **措辞纪律**（「逐位相同」的范围）· **A1 标「设计目标」**。**待用户确认进入实现。**
> 输入：需求确认 **v1.1**（评审通过）· 初步设计 **v1.1**（评审：「**初步设计通过，架构方向正确**」；O1–O4 已冻结）
> 本稿目标：把评审列出的实现要点**逐项钉到行**，并解决一处初设未覆盖、而实现前必须定的问题（**测试装置**）

---

## 1. 设计输入与基线

### 1.1 已冻结（需求 + 初设，本稿不再讨论）

| 项 | 已冻结内容 |
|---|---|
| **数据流** | 颜色存 `Window` → 每帧经 `Renderer::BeginFrame(const Color&)` → `RenderingBackend::BeginFrame(const Color&)`；**Backend 不持状态、不认识 Window** |
| **签名** | `virtual void BeginFrame(const Color& background) = 0;`——**形参不得带默认实参**（唯一默认值来源 = `Window`） |
| **API** | `Window::SetBackgroundColor(const Color&)`；命名冻结（不用 `SetClearColor`） |
| **恢复默认** | `SetBackgroundColor(Color::White())`——**不加** `ResetBackground()`；**不加** getter（O4） |
| **副作用** | `SetBackgroundColor` = 改状态 **+ 请求重绘**（`Invalidate()`；依据既有职责契约 `TextBox.cpp:189`） |
| **失败行为** | `CreateSolidBrush` 失败 ⇒ **静默跳过底色**，**不加** `FRAMEWORK_ASSERT`（O2） |
| **alpha** | **被忽略**（`ToColorRef` 只取 RGB；**不接** `BlendAlphaSolid`） |
| **不做** | 像素级验收（O3）· `RenderCommand` 变体 · root 背景 · 半透明 · margin/渐变 |

### 1.2 详设新增基线（B12–B16，2026-09-22 实测）

| # | 事实 | 证据 | 影响 |
|---|---|---|---|
| **B12** | ★ **`Window::PaintFrame()` 在 `private` 区**（`public:` 起于 :44，`private:` 起于 :208） | `Window.h:263` | **测试不能手动触发帧** ⇒ 必须靠 `Show()` + 消息泵触发 `WM_PAINT` |
| **B13** | ★ **`Application::Create(title,w,h)` 不接受 `RenderServices`**，它固定走 `Window` 构造的默认实参 | `Application.h:60` · `Application.cpp:63`（`new Window(*this, title, width, height)`） | **测试拿不到 Window 内部的 backend** ⇒ 端到端测不了（本稿 §2 解决） |
| **B14** | ★ **但 `Window` 构造的 `services` 形参注释明写「测试/未来可注入其他后端」**；且 ★ **这不是疏漏——Phase 7 需求稿把这条路径写成了书面判断** | `Window.h:216`；★ `phase7-backend-requirements.md:94` 原文：「测试：`RecordingBackend` **不进 Window**（断言段直接使用），**无需注入**；**未来 Window 测试注入时**传独立对象或单类双接口对象（`RenderServices` 按需构造）」；`phase7-backend-preliminary-design.md:153-157` 已论证形参形态（`RenderServices&&` 灵活性低、`std::optional` 复杂——**均否决**，取「按值 + 默认实参」）；`window-ownership-detailed-design.md:60/100/107`（Phase 11 详设**照抄**该注释） | ⇒ B13 **不是「注释说谎」，也不是新需求**：★ **精确说法（第二轮评审校正）** = 「**已登记的延迟设计，在出现第一个真实消费者后兑现**」——**Phase 7 完成了「注入点」（`Window` 构造的形参），Phase 18 完成「注入通路」（`Application::Create`）**。★ **不称「技术债」**：技术债是「现在知道不理想、以后要修」，而这里是「**有意不做 + 有明确触发条件且登记在案**」——性质不同（论证见 §2.2） |
| **B15** | `Color::White()` 存在且 `constexpr` | `Color.h:21` | 默认值可直接用 |
| **B16** | `Window` 的配置 API 形制 = **纯转发到 `m_platformWindow`**（各 3 行）；`Invalidate()` 形制 = 转发 `m_platformWindow->Invalidate()` | `Window.cpp:190-194`（`SetCaptionHeight`）· `:202-206`（`SetWindowLayer`）· `:155-164`（`Invalidate`） | `SetBackgroundColor` **不同**：它改的是 Window 自己的成员（不转发平台层）⇒ 形制与前者不同，需在 §3 写明 |

### 1.3 本稿两处新增定案（初设未覆盖，实现前必须定）

| # | 新增定案 | 理由 |
|---|---|---|
| **★ N1** | ★ **打通 `RenderServices` 注入通路** = `Application::Create` 追加第 4 个可选形参 `RenderServices services = CreateDefaultRenderServices()` | 由 B13/B14 推出：`Window` 构造**早已**接受该 bundle，而唯一的生产构造入口 `Create` **不透出它** ⇒ 注入能力**只有半个通路**，N1 把它接通（★ **不是「为测试开洞」**——区分判据见 §2.2 第 2 点）。手法与 Phase 17 给 `VerticalLayout` 追加 `padding` **完全同型**（追加带默认值的尾形参 ⇒ 现有调用点零改动）；形参形态**沿用 `Window` 构造的既有先例**（`Window.h:223`，其论证见 `phase7-backend-preliminary-design.md:153-157`） |
| **★ N2** | **测试触发帧的途径 = `Show()` + `PumpMessages(n)`**（不新增任何 `ForTests` 缝） | B12：`PaintFrame` 是 private ⇒ 只能由 `WM_PAINT` 驱动；`WindowChromeTests.cpp:49-96` 已有 `TestWindow` + `PumpMessages` 装置可仿 |

### 1.4 第二轮评审（针对本稿 v1.0）逐条处置

**评审结论**：**🟢 详细设计通过，可以进入实现阶段**（未发现需推翻架构 / API / 数据流的问题）。14 条意见**全部采纳**，其中 4 条已落为具体条款。

| # | 评审意见 | 处置 |
|---|---|---|
| 1 | §二 测试装置问题的分析成立；且避开了 `friend class` / `PaintFrameForTests` 这类坏方案 | ✅ 保持（§2 即为该处理） |
| 2 | §三 N1 认可（兑现早期设计路径 + 零破坏 + 现有调用不需改） | ✅ 保持；★ **定性用词按第二轮评审精确化**——改称「**已登记的延迟设计，在第一个真实消费者出现后兑现**」，且一律写「**打通注入通路**」（§1.2-B14 · §1.3-N1 · §2.2） |
| 3 | §四 N2（`Show()` + `PumpMessages`）优于新增 `ForTests` 入口；独立成 `WindowBackgroundTests.cpp` 合理 | ✅ 保持（§2.4） |
| 4 | §五 `RecordingBackend` 双实例的所有权与生命周期处理正确（并已记录先后析构约束） | ✅ 保持（§2.3） |
| 5 | §六 数据流可正式冻结（全程无 `Window*` / `Widget*` / `PaintContext*` / `RenderCommand::Clear` 跨层依赖） | ✅ 保持（§1.1） |
| 6 | §七 alpha 边界定义足够明确（连 `a == 0` 也不得成为分支） | ✅ 保持（§4.2） |
| 7 | §八 `CreateSolidBrush` 失败不 assert 认可（性质不同于调用契约违规） | ✅ 保持（O2 / §4.3） |
| 8 | ★ §九 **T18-1/T18-4 的「逐位相同」不要写大**——它验证的是「默认参数 == 显式 `Color::White()`」，**不是**「GDI 像素输出与 Phase 17 逐位相同」 | ✅ **新增措辞纪律**（§5-C1 + §6 表下注） |
| 9 | §十 T18-5 收窄正确（**不能用一个测试装置证明它观察不到的东西**） | ✅ 保持（§6-T18-5 + §5-C4） |
| 10 | §十一 T18-3 价值高（同时验证 O1 的「下一帧生效」链路） | ✅ 保持（§6-T18-3） |
| 11 | ★ §十二 **帧编排顺序不得重排**（不得改成 `BeginFrame` → `Root.Paint` → `Execute`） | ✅ **升级为硬约束 + 进盯防清单第 1 条**（§3.1） |
| 12 | §十三 影响面记录诚实（**公共 API +2 未淡化成 +1**） | ✅ 保持（§8） |
| 13 | ★ §十四 **A1 的 `231` 是验收目标、非既成事实** | ✅ **已标注**（§6 + §7-A1：以**实测**回填，不强行追设计值） |
| 14 | §十四 A6 需单独授权；实现阶段勿擅自修改 ModelProbe | ✅ **已强化**（§7-A6） |

---

## 2. ★ 测试装置（初设未覆盖，详设必须解决）

### 2.1 问题陈述（两条约束合起来把路堵死了）

```
B12  Window::PaintFrame() 是 private   ⇒ 测试不能手动驱动帧
B13  Application::Create 不可注入后端   ⇒ 测试观察不到 Window 内部的 backend
⇒ 若不处理：T18-1..T18-4（端到端）无装置可写，只剩「Renderer 直连 RecordingBackend」这半段
```

### 2.2 定案：打通 `RenderServices` 注入通路（N1 = `Application::Create` 追加可选形参）

```cpp
// Application.h:60（现状）
Window& Create(const std::string& title, int width, int height);

// 追加第 4 个可选形参（现有调用点零改动）
/// @param services 渲染服务（默认 GDIBackend+GDITextMeasurer；**支持注入其他后端**——
///        与 `Window` 构造的同一形参同义，见 `Window.h:216`）
Window& Create(const std::string& title, int width, int height,
               RenderServices services = CreateDefaultRenderServices());
```

```cpp
// Application.cpp:58-73（改后核心行）
Window& Application::Create(const std::string& title, int width, int height,
                            RenderServices services) {
    m_windows.emplace_back(std::unique_ptr<Window>(
        new Window(*this, title, width, height, std::move(services))));   // ← 只多传一个实参
    ...（其余逐字不动）
}
```

**四点论证**：
1. ★ **兑现已登记的延迟设计**（B14）——Phase 7 需求稿（`:94`）当时就写明「`RecordingBackend` **不进 Window**……**未来 Window 测试注入时**传独立对象」：**推迟是显式判断而非疏漏**（那一刻的消费者是「解耦」，不是「测试注入」）；而 Phase 18 面对**第一个真实消费者**（端到端背景色测试）⇒ **触发条件达成，路径到期兑现**。且 **形参形态无需新决策**——`phase7-backend-preliminary-design.md:153-157` 已把「按值 + 默认实参」论证过（`RenderServices&&` 灵活性低、`std::optional` 引入额外复杂度，均否决）⇒ 本稿**沿用 7.1.4 的既有形态**。
2. ★ **判据：这个 API 在「换后端 / 换平台」时是否还得再改一次？**（第二轮评审补充）——**「是」⇒ 它本来就该有**。`Create` 的 `services` 服务于**后端选择 / 注入**（`BackendFactory.h` 与 `RenderServices.h:13` 都登记了「未来换后端」的意图）⇒ **属框架能力的一部分**。**对照**：`Win32PlatformWindow::GetHwndForTests()` 只对测试有意义、生产代码永不调用、且加在**内部头**（不污染公共 API）⇒ **性质完全不同**。★ **因此措辞很重要**：本文档与将来的注释一律写「**打通注入通路**」，**不得**写「为测试加个洞」——**措辞会变成先例**，后者会诱使后来人继续往公共 API 上挂测试缝。
3. ★ **反事实：不接通会怎样**（第二轮评审补充）——T18 只能测「`Renderer` → `RecordingBackend`」这半段，而 **`Window::SetBackgroundColor` → `PaintFrame` → `BeginFrame(m_backgroundColor)`** 那一半**完全无覆盖**——它恰恰是本相位**新增代码的主体、也最易错**（`Window.cpp:126` 那一行 + 成员初始化 + `Invalidate()`）。⇒ 测试会**精准地避开被测对象最脆弱的部分**（配在「好测的地方」而非「该测的地方」）；而这类缺失**最隐蔽：测试全绿，看不出缺了什么**。（退而求其次的 `Window::GetBackgroundColorForTests()` ⇒ **污染公共头**，比接通注入更差。）
4. **零破坏 + 语义沿用**：带默认值的尾形参 ⇒ 现有全部 `Create(title, w, h)` 调用**逐字不动**（手法同 Phase 17 的 `padding`）；`RenderServices` 是 move-only ⇒ 形参按值 + `std::move` 传入，与 `Window` 构造**完全同款**（`Window.cpp:42-46`），不引入新语义。

**⇒ 影响面如实**：本相位**公共 API +2**（`Window::SetBackgroundColor` + `Application::Create` 的形参）。

### 2.3 观察面：`RecordingBackend` 双重继承 ⇒ 需要**两个实例**

`RecordingBackend` 同时实现 `RenderingBackend` 与 `TextMeasurer`（`RecordingBackend.h:21`），而 `RenderServices` 要两个**独立对象**（`RenderServices.h:11-13` 注释：「unique_ptr 各自独立对象，无 shared_ptr」）⇒ 测试这样写：

```cpp
Application app;                                  // ① 先声明 app
auto recorderOwned = std::make_unique<RecordingBackend>();
RecordingBackend* recorder = recorderOwned.get(); // ② 留观察用裸指针（非拥有）
RenderServices services{ std::move(recorderOwned), std::make_unique<RecordingBackend>() };
                                                  // ↑ 渲染观察者           ↑ 测量占位（不参与断言）
Window& w = app.Create("ECDI_BgTest", 400, 300, std::move(services));
w.Show();                                         // ③ 必须 Show（B12：帧由 WM_PAINT 驱动）
PumpMessages(16);                                 // ④ 装置仿 WindowChromeTests.cpp:99-105
EXPECT_TRUE(!recorder->frameBackgrounds.empty());
```

**生命周期**（与 `WindowChromeTests.cpp:47-48` 同约束）：本对象必须先于 `Application` 析构 ⇒ **先声明 `app`、后声明观察者结构**。

### 2.4 触发帧的判据

`Show()` → 平台层产生 `WM_PAINT` → `PumpMessages(16)` 派发 → `Window::OnPaint` → `PaintFrame()` → `BeginFrame(m_backgroundColor)`。**零闪窗要求不适用于本文件**（T18 全部需要真实帧）——与 `WindowChromeTests` 的「T0–T3 不 Show」策略**不同**，因此**独立成 `WindowBackgroundTests.cpp`**，不并入既有文件。

---

## 3. 逐文件改动（△1–△9 行级）

| # | 文件 | 改动 | 备注 |
|---|---|---|---|
| **△1** | `include/ECDI/Render/RenderingBackend.h:39` | `virtual void BeginFrame() = 0;` → `virtual void BeginFrame(const Color& background) = 0;`；`@brief` 补「参数 = 本帧客户区底色（**决策层输入**；能力层**不持有**该状态）」 | 纯虚签名；`Color` 已在 include 清单（`:11`）⇒ **零新依赖** |
| **△2** | `include/ECDI/Render/Renderer.h:17` + `src/Render/Renderer.cpp:12` | `void BeginFrame(const Color& background);` → `{ m_backend.BeginFrame(background); }` | **仍纯转发**（B2），结构不变 |
| **△3** | `src/Render/GDIBackend.h:38` + `.cpp:244` | 签名同步；`:260-261` 清屏改用 `background`（代码见 §4.1）；`:260` 注释同步（D7：决策 16 的「清屏白」→「清屏用本帧背景色（**默认**白）」） | 复用 `ToColorRef`（`GDIBackend.h:62`）+ 决策 24 的画刷模式（`.cpp:343-350`） |
| **△4** | `src/Render/RecordingBackend.h:86` | `void BeginFrame() override {}` → `void BeginFrame(const Color& background) override { frameBackgrounds.push_back(background); }`；新增公开成员 `std::vector<Color> frameBackgrounds;`（置于 `:84` 之后，与既有 `draws`/`textCalls` 同风格） | `EndFrame`（`:107`）**不动**（保持空实现） |
| **△5** | `include/ECDI/Window/Window.h` | ① `public:` 区（配置 API 段，`SetWindowLayer` 附近）新增 `void SetBackgroundColor(const Color& color);` + 注释（★ 明写**副作用**：改状态 + **请求**重绘）；② `private:` 成员区新增 `Color m_backgroundColor = Color::White();`（**唯一默认值来源**，C2） | `Color` 经 `Window.h` 既有 include 链已可见（`Core/Color.h`）——实现时核对 |
| **△6** | `src/Window/Window.cpp` | ① `:126` → `m_renderer.BeginFrame(m_backgroundColor);`；② 新增实现（**不是**纯转发形制，B16）：`void Window::SetBackgroundColor(const Color& color){ m_backgroundColor = color; Invalidate(); }` | 与 `SetCaptionHeight` 形制**不同**（那两个转发平台层；本方法改自身成员） |
| **△7** | `include/ECDI/Application/Application.h:60` + `src/Application/Application.cpp:58` | 追加可选形参 `RenderServices services = CreateDefaultRenderServices()`；实现里 `std::move(services)` 传入 `Window` 构造（**其余逐字不动**） | §2.2（N1）；`Application.h` 需可见 `RenderServices`（实现时核对 include） |
| **△8** | `src/Tests/AntiAliasingTests.cpp:339` · `src/Tests/RendererTests.cpp:218` / `:281` | **3 处** `backend.BeginFrame()` → `backend.BeginFrame(Color::White())`（★ v1.2 勘误：原稿写「6 处」，实测 **6 = Begin + End 合计**——另有 `:232` / `:285` 与 `AntiAliasingTests.cpp:342` 的 `EndFrame()`，**无需改**） | 显式表达既有默认行为（§1.1）；**不改语义** |
| **△9** | 新增 `src/Tests/WindowBackgroundTests.cpp` + `RunAllTests.h` 声明 + `RunAllTests.cpp` 调用 | T18-1..T18-5（§6） | **新增源文件须手工登记两处**（CMake 的 GLOB 自动入库，但测试声明/调用是手工——记忆「新增源文件只改一处」的例外） |

**公共头 92 → 92**（无新增头）。**断言特征串 11 → 11**（O2：不新增框架断言）。


### 3.1 实现盯防清单（9 条红线——本相位最易写错的地方）

| # | 红线 | 依据 |
|---|---|---|
| 1 | ★ **帧编排顺序不得重排**：`m_renderer.BeginFrame(m_backgroundColor)` **保持在原位置**（`CommandBuffer.Clear()` → `Root.Paint()` → **`BeginFrame`** → `Execute()` → `EndFrame()`）。**不得**顺手写成「`BeginFrame` → `Root.Paint` → `Execute`」 | 评审第 11 条；B1；§4.1「帧的其他编排逐字不动」 |
| 2 | ★ **`SetBackgroundColor` 必须自带 `Invalidate()`**（改状态 **+ 请求**重绘，不拆成两步让调用方做） | 既有职责契约（`TextBox.cpp:189`）；O1 |
| 3 | ★ **`BeginFrame` 形参不得带默认实参**；`Color::White()` 只允许出现在 `Window` 的成员初始值 | C2 / A5 |
| 4 | ★ **`background.a` 不得接入任何分支**（含「`a == 0` 就跳过清屏」这种看起来合理的优化） | §4.2 红线；C4 / A4 |
| 5 | **`CreateSolidBrush` 失败 ⇒ 静默跳过底色**，**不加** `FRAMEWORK_ASSERT` | O2 / §4.3 |
| 6 | **`Window::PaintFrame` 保持 `private`**；**不得**新增 `friend` / `PaintFrameForTests` / `GetBackgroundColorForTests`（测试走 `Show()` + `PumpMessages`） | B12；N2 |
| 7 | **`Renderer::BeginFrame` 仍是单行转发**——不得在 `Renderer` 里存背景色（那是决策层状态） | C3 / A4③ |
| 8 | **`Application::Create` 只多传一个实参**——`Window` 构造调用**其余参数逐字不动**（含 `std::move`） | △7 |
| 9 | **断言特征串保持 11**——本相位不得引入任何新的 `FRAMEWORK_ASSERT` | O2 / A2 |

---

## 4. GDI 实现与两条边界

### 4.1 清屏点（`GDIBackend::BeginFrame`，替代 `:260-261`）

```cpp
void GDIBackend::BeginFrame(const Color& background)
{
	// 决策 32：Begin/End 严格配对
	FRAMEWORK_ASSERT(!m_inFrame);
	m_inFrame = true;

	// 决策 17：帧 DC 从 BeginPaint 获取，EndFrame 严格配对释放
	m_windowDC = BeginPaint(m_hwnd, &m_ps);

	// 决策 15/26/38：懒创建 + 尺寸自检重建（先建后替，失败时旧资源仍在）
	EnsureBackBuffer();

	// 决策 27：完整客户区（rcPaint 无效区域不能当 Buffer 尺寸）
	RECT client{};
	GetClientRect(m_hwnd, &client);

	// 决策 16（Phase 18 修订）：清屏用**本帧背景色**——默认白由 Window 给出（本层不持有默认值）
	// 决策 24：画刷每次创建/销毁（无缓存）——与 DrawRect 同款（:343-350）
	// 决策 21/23：ToColorRef 内含 ToByte Clamp；★ alpha 被忽略（COLORREF 无 alpha 通道，见 §4.2）
	HBRUSH brush = CreateSolidBrush(ToColorRef(background));
	if (brush)
	{
		FillRect(m_memoryDC, &client, brush);
		DeleteObject(brush);
	}
	// else：决策 30「失败 → 局部跳过」——底色不刷新，本帧其余绘制照常（O2：不加断言）
}
```

**与现状的 diff 只有 3 行**（签名 1 行 + 清屏 1 行变 5 行 + 注释），**帧的其他编排逐字不动**（`BeginPaint` / `EnsureBackBuffer` / `GetClientRect` 顺序不变）。

### 4.2 alpha 被忽略：精确定义（C4）

| 输入 | 行为 | 依据 |
|---|---|---|
| `Color(1,0,0,1.0f)` | 清屏 `RGB(255,0,0)` | 实色 |
| `Color(1,0,0,0.5f)` | **同样**清屏 `RGB(255,0,0)` | `ToColorRef` 只取 RGB（决策 21/23 的既有转换） |
| `Color(1,0,0,0.0f)` | **同样**清屏 `RGB(255,0,0)`（**不透明**，不是"全透明"） | 同上；★ 特别注意：**不得**因 `a == 0` 而跳过清屏 |

**★ 实现红线**：**不得**把 `background.a` 接入任何分支（包括"`a==0` 就不清屏"这种看起来合理的优化）——那会让「背景色」偷偷变成 alpha 语义，把 Phase 9 拉进来。**A4 结构性审查判据**（§7）覆盖此点。

### 4.3 `CreateSolidBrush` 失败（O2 定案）

- **行为**：跳过 `FillRect`，本帧其余绘制继续（帧不中断、不崩）。
- **不加断言**：与 `padding < 0`（调用契约违规）性质不同——这是**平台资源失败**，沿用 `GDIBackend` 既有姿态（决策 30）。
- **后果**：该帧底色保持缓冲区的旧内容（可能是上一帧的内容）；因双缓冲在 `EndFrame` 才 `BitBlt`，**不会出现撕裂或闪烁**。

---

## 5. 契约 C1–C5 的验证映射

| # | 契约 | 验证方式 | 层级 |
|---|---|---|---|
| **C1** | 默认零回归：**不配置 ⇒ 送给后端的背景参数 == `Color::White()`**（⚠️ 「逐位相同」的**范围仅限参数取值**——本相位**不做像素级比对**（O3）⇒ **不得**表述成「GDI 像素输出与 Phase 17 逐位相同」，见 §6 措辞纪律） | **T18-1** + **T18-4** | 端到端（命令级） |
| **C2** | **唯一默认值来源**（默认白只在 `Window` 一处；`BeginFrame` 形参无默认实参） | **T18-1**（记录到白）+ **A5 结构性判据**（源码：`BeginFrame` 声明无 `= ` 默认实参；渲染链路上 `Color::White()` 只出现在 `Window.h` 成员初始化） | 运行时 + 源码级 |
| **C3** | 决策层给值、能力层执行（Backend 不持状态、不认识 Window） | **A4 结构性判据**（`RenderingBackend.h` 依赖零新增） | 源码级 |
| **C4** | alpha 被忽略 | **T18-5**（命令级：记录到的 `Color` 含 alpha **原样**）+ **A4**（GDI 侧只用 `ToColorRef`，无 `background.a` 分支） | 运行时 + 源码级 |
| **C5** | 每帧传递、无缓存 | **T18-2** + **T18-3**（改色 ⇒ 下一帧记录变化） | 端到端 |

> **C2/C3/C4 各有一条"运行时测不到、只能源码级保证"的部分**——这是本轮评审（T18-5 收窄）确立的纪律：**不假装测到了**（skill 条 85/87 同族）。

---

## 6. 测试用例规格（T18-1..T18-5 完整输入/期望）

**装置**：§2.3 的 `Application` + `RecordingBackend` 注入 + `Show()` + `PumpMessages(16)`。**文件**：新建 `ECDI/src/Tests/WindowBackgroundTests.cpp`。

| # | 用例名 | 输入 | 期望 |
|---|---|---|---|
| **T18-1** | `WindowBackground.DefaultWhite` | 不调 `SetBackgroundColor`；`Create(…)` → `Show()` → `PumpMessages(16)` | `!frameBackgrounds.empty()`（帧确实到达后端）；`frameBackgrounds.back().r/g/b/a == 1.0f`（**默认白**） |
| **T18-2** | `WindowBackground.CustomColorReachesBackend` | `SetBackgroundColor(Color::FromRGBA8(0x0F, 0x11, 0x15))` → `PumpMessages(16)` | `back()` 的 `r/g/b` ≈ `15/255, 17/255, 21/255`（`EXPECT_NEAR`，容差 `kEpsilon`）；`a == 1.0f` |
| **T18-3** | `WindowBackground.ChangeTakesEffectNextFrame` | ① 记录 `n0 = frameBackgrounds.size()` ② `SetBackgroundColor(Color::Red())` ③ `PumpMessages(16)` | `frameBackgrounds.size() > n0`（**确实产生了新帧**）且 `back() == Color::Red()`（**下一帧用新色**——O1 的配套证据） |
| **T18-4** | `WindowBackground.ExplicitWhiteEqualsDefault` | 显式 `SetBackgroundColor(Color::White())` 后取一帧 | 与 T18-1 的取值**逐位相同**（`r/g/b/a` 全等 ⇒ 默认与显式同义，C1 的另一面） |
| **T18-5** | `WindowBackground.AlphaIgnored` | `SetBackgroundColor(Color(1.0f, 0.0f, 0.0f, 0.5f))` → `PumpMessages(16)` | `back().a == 0.5f`（**alpha 原样传递、链路不替他做决策**）；`r/g/b == 1.0/0.0/0.0`。⚠️ **本用例不断言**「GDIBackend 未进 `BlendAlphaSolid`」——该保证由 **A4 源码审查**承担（§5） |

> ★ **措辞纪律（评审第 8 条）**：T18-1 / T18-4 验证的是「**默认背景参数 == 显式 `Color::White()`**」，**不是**「GDI 像素输出与 Phase 17 逐位相同」——后者本相位**未测**（O3 不做像素级验收）。**实现后的测试报告、提交信息与文档一律不得把这条说大**：写「默认背景参数 = `Color::White()`」或「参数取值逐位一致」即可。

**T18-6（适配，非新增）**：△8 的 **3 处**调用点传白后，`AntiAliasing` / `Renderer` 既有用例**全绿**（零回归）。

**用例数**：**226 → 231**（**+5**；⚠️ **设计目标**——以实现后 `GetTestRegistry().Add` 的**实测注册数**为准，不符则**以实测回填**，不强行追设计值）。**注册**：`RunAllTests.h` 声明 5 个函数 + `RunAllTests.cpp` 调用（手工两处）。

---

## 7. 验收（A1–A7）

| # | 项 | 判据 |
|---|---|---|
| **A1** | 四工具链构建 + 全绿 | `ecdi_tests` **231 passed, 0 failed**（MSVC / ClangCL / Clang / MinGW）——⚠️ **231 是设计目标、非既成事实**；实现后以**实测**结果回填（含实际注册数），**不强行追设计值** |
| **A2** | 断言启用核验 | 特征串 **11 条**（O2 定案：不新增）——`grep -c` 判据同 Phase 17 |
| **A3** | 零回归 | 既有 **226** 全绿（尤其 △8 适配后的 **3 处**调用点） |
| **A4** | ★ **结构性审查（三条合一）** | ① `RenderingBackend.h` **依赖零新增**（`Rect`/`Point`/`Color`/`Font`/`Image`；**无** `Window`/`Widget`/`PaintContext`）② GDI 清屏**只用 `ToColorRef`**、**无** `background.a` 分支、**无** `BlendAlphaSolid` 调用 ③ `Renderer` **仍纯转发**（`BeginFrame` 函数体只有一行转发） |
| **A5** | ★ **唯一默认值来源（源码级）** | ① `BeginFrame` 声明**无默认实参** ② 渲染链路中 `Color::White()` 只出现在 `Window.h` 的成员初始值 ③ `GDIBackend` / `RecordingBackend` / `Renderer` 内**无**白色常量 |
| **A6** | 目视 | ModelProbe：**root 级 padding + 非白底色** ⇒ 露出的四边呈**配置色**（R3 组合首次可用）。★ 需授权动 `examples/ModelProbe/main.cpp`（skill 条 2）——**实现阶段不得擅自修改**：核心代码与测试先行，A6 等**用户单独授权**后再做 |
| **A7** | 文档与索引同步 | `GDIBackend.cpp:260` 决策 16 注释（D7）· `roadmap-deferred.md` §7.7 #39 收口 · 两份 README 规模锚点（用例 231 · 头 92）· `desktopnest-roadmap.md` 无需动（G-2 仍欠） |

### 7.1 实现后实测回填（2026-09-22）——A1–A7

| # | 实测结果 |
|---|---|
| **A1** | ✅ **四工具链**（MSVC / ClangCL / Clang / MinGW）`ecdi_tests` **231 passed / 0 failed**（226 既有 + 5 新增）——**实测 = 设计目标**，无需调整 |
| **A2** | ✅ 断言特征串 **11 → 11**（未新增任何断言，O2 定案保持） |
| **A3** | ✅ 既有 **226** 全绿——△8 适配的 **3 处** `BeginFrame` 传白后零回归 |
| **A4** | ✅ **源码级核验通过**：① `RenderingBackend.h` 依赖仅 `Rect`/`Point`/`Color`/`Font`/`Image`（无 `Window`/`Widget`/`PaintContext`）② 清屏路径 `GDIBackend.cpp:263-269` **只用 `ToColorRef`**、无 `background.a` 分支、无 `BlendAlphaSolid` ③ `Renderer::BeginFrame` 仍是**单行转发**。★ **判据措辞精确化（v1.2）**：② 的「无 `BlendAlphaSolid` 调用」**只在清屏路径内成立**——全文有 **5 处** `BlendAlphaSolid`，属 AA / 图像 / 圆角既有能力，不在本相位范围 |
| **A5** | ✅ **源码级核验通过**：① `virtual void BeginFrame(const Color& background) = 0;` **无默认实参** ② `Color::White()` 仅出现在 `Window.h:312` 的成员初始值（另 2 处是注释） ③ `GDIBackend` / `RecordingBackend` / `Renderer` 内 **0 次** `Color::White` |
| **A6** | ✅ **能力达成、形态被否**：root 级 padding + 非白底色实测**确实露出配置色**（本相位能力验证通过）；但该形态**不可接受**——`CaptionBar` 被连带内缩 12px（`padding` 是布局级单一 int、框架无 per-child inset），且与**平台拖动区脱钩**（`Win32PlatformWindow.cpp:417` 的 caption 判据 `y < captionHeight` 是**全宽带**、不看 `CaptionBar` 几何 ⇒ ① 环上那块"背景"落在带内、**能拖窗口**；② 标题栏**下缘 12px** 落在带外、**拖不动**）。⇒ demo 落回 **page 级留白 + 底色同值**（`af1f080`）；「贴边 + 留白 + **露窗口底色**」三者兼得改由 **Phase 18.1 路线 C**（应用侧透明 `Panel`，**框架零改动**）落实 |
| **A7** | ✅ **本批完成**：决策 16 注释已随实现落盘（`GDIBackend.cpp:260`）· `roadmap-deferred.md` §7.7 **#39 → ✅ 已实现** · 两份 README 锚点刷新（**231** 用例 / **92** 头 / **129** 文档）· 本表实测回填 · **△8「6 处 → 3 处」勘误已落**（v1.2） |


---

## 8. 影响面（汇总）

| 项 | 实测 / 预计 |
|---|---|
| 改动文件 | 源码 **7**（`RenderingBackend.h` · `Renderer.h/.cpp` · `GDIBackend.h/.cpp` · `RecordingBackend.h` · `Window.h/.cpp` · `Application.h/.cpp`）+ 既有测试 **2**（**3 处**调用点）+ **新增测试 1** |
| 公共头 | 92 → **92** |
| **公共 API** | **+2**（`Window::SetBackgroundColor` + `Application::Create` 的 `services` 形参）——★ 与 Phase 16「净增 0」、Phase 17「净增 0」**都不同**，如实记 |
| `RenderingBackend` | 1 个纯虚**改签名**（方法数不变） |
| `Renderer` | `BeginFrame` 改签名（结构不变） |
| `Application` | `Create` **追加带默认值的尾形参**（现有调用**逐字不动**） |
| `RecordingBackend` | +1 公开记录成员；`BeginFrame` 由空实现改为记录 |
| 测试 | 既有 **3 处**适配 + 新增 5 用例（**226 → 231**） |
| 断言特征串 | **11 → 11** |
| 决策 16 注释 / `roadmap §7.7` | 落地时同步（D7） |
| `examples/ModelProbe/main.cpp` | ★ A6 目视用，**须单独授权** |

---

## 9. 文档收口清单（实现后执行）

1. `GDIBackend.cpp:260` 注释：「决策 16：清屏白（Root 白底是平台语义，不是 Widget 命令）」→ 「决策 16：**清屏用本帧背景色**（默认白由 `Window` 给出，本层不持有默认值；**仍属平台语义**，不是 Widget 命令）」
2. `roadmap-deferred.md` §7.7 **#39** → ✅ 已实现（Phase 18）+ 头部状态；两份 README 锚点（用例 **231**、公共 API +2）
3. `Window.h` 类注释是否需补「背景色」？（`Window` 的配置项清单在类注释里有枚举——实现时核对）
4. 本相位**由 Phase 17 A6 派生**的因果链在需求稿已记，收口时复核一次

---

## 10. 修订记录

- **v1.2**（2026-09-22）**实现后回填 + 一处勘误**。① ★ **勘误**：△8 原写「**6 处** `backend.BeginFrame()`」——实测 **3 处**（`AntiAliasingTests.cpp:339` · `RendererTests.cpp:218/281`），**6 是 `BeginFrame` 与 `EndFrame` 的合计**（另 3 处 `EndFrame` 无需改）；§3/§6/§7/§8 共 **5 处**「6 处」表述同步订正，行号按实测更新（217/231/280/284 → 218/232/281/285）；**v1.0 修订记录内的同款表述按历史记录保留不改写**（条 61② 纪律）。② **§7.1 新增「实现后实测回填」**：A1–A7 逐项附实测结果。③ ★ **A4-② 判据措辞精确化**：「无 `BlendAlphaSolid` 调用」**只在清屏路径内成立**。④ ★ **A6 记录形态被否的根因**：`CaptionBar` 被 root padding 连带内缩 + 与 `WM_NCHITTEST` 的**全宽 caption 带**脱钩（拖动行为异常）⇒ 替代方案见 **Phase 18.1 路线 C**。

- **v1.1**（2026-09-22）**第二轮评审（针对 v1.0）处置——评审结论「🟢 详细设计通过，可进入实现」**。① **§1.4 新增逐条处置表**（14 条，**全部采纳**）；② ★ **定性用词精确化**：B14 改称「**已登记的延迟设计，在第一个真实消费者出现后兑现**」——**Phase 7 完成「注入点」、Phase 18 完成「注入通路」**，且**不称「技术债」**（性质不同：有意不做 + 有明确触发条件）；§2.2 一律写「**打通 `RenderServices` 注入通路**」而**不写**「为测试加洞」（措辞会变先例）；③ ★ **§2.2 论证由三点扩为四点**——新增「**换后端 / 换平台时是否还得再改一次**」的判据（对照 `GetHwndForTests` 留在内部头）与**反事实**（不接通则测试**精准避开最脆弱的那半段**，且全绿看不出缺失）；④ ★ **新增 §3.1 实现盯防清单 9 条红线**（含评审第 11 条「**帧编排顺序不得重排**」）；⑤ ★ **新增措辞纪律**（§5-C1 + §6 表下注）：**「逐位相同」的范围仅限参数取值**，不是像素级——**不得把这条说大**；⑥ ★ **§6 用例数与 §7-A1 标注「设计目标」**，以**实测**回填（`231` 非既成事实）；⑦ §7-A6 补「实现阶段不得擅自修改 ModelProbe」；⑧ `Application::Create` 新增形参的注释改「测试/未来可注入」为「**支持注入**」（该通路在本相位后**已可达**——skill 条 91 的注释纪律）。

- **v1.0**（2026-09-22）**详细设计初稿**（**同日自查补记**：B14 补入**原始文档依据**——`phase7-backend-requirements.md:94` 已把「未来 Window 测试注入」写成书面判断，`phase7-backend-preliminary-design.md:153-157` 已论证形参形态 ⇒ **N1 不是新决策，是兑现既定路径 + 沿用既有形态**；结论未变，仅证据加强）。输入 = 需求 **v1.1** + 初设 **v1.1**（O1–O4 已冻结）。① **§1.2 新增基线 B12–B16**——其中 **B12**（★ `Window::PaintFrame` 在 private 区）与 **B13**（★ `Application::Create` 不可注入 `RenderServices`）**两条合起来把测试路堵死**；**B14** 发现「**测试可注入后端**」这条意图**只有半个通路**（`Window` 构造支持、`Create` 不透出）；**B16** 记明 `Window` 配置 API 的形制 = 纯转发平台层（而 `SetBackgroundColor` **不是**，它改自身成员）；② **§1.3 / §2 新增两处定案**：**N1** `Application::Create` 追加带默认值的 `RenderServices` 尾形参（**手法与 Phase 17 追加 `padding` 同型**，零破坏；且 `Window` 构造已有同款先例）· **N2** 测试触发帧走 `Show()` + `PumpMessages`（不新增任何 `ForTests` 缝）；③ **§2.3** 给出可编译的测试装置骨架（`RecordingBackend` 双重继承 ⇒ 需**两个实例**，观察者用非拥有裸指针；生命周期与 `WindowChromeTests` 同约束）；④ **§3 △1–△9 逐行钉死**（含 6 处既有调用点的适配与「新增测试文件须手工登记两处」）；⑤ **§4.1** 给出清屏点的完整替换代码（与现状只差 3 处）；**§4.2 精确定义 alpha 忽略**——★ 特别写出**实现红线**：连「`a == 0` 就跳过清屏」这种看起来合理的优化**也不得**做；⑥ **§5 建立契约 → 验证的映射**，并明确 **C2/C3/C4 各有一段"运行时测不到、只能源码级保证"**（承接评审对 T18-5 的收窄）；⑦ **§6 给出 T18-1..T18-5 的完整输入/期望**（含容差与断言层级）；⑧ **§7 A4/A5 为结构性判据**（依赖零新增 / 唯一默认值来源，各含 2–3 条源码级子判据）；⑨ **§8 影响面如实记「公共 API +2」**。**待评审。**
