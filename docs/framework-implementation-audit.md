# ECDI 实现代码审计报告（v1.6）

> 状态：**v1.6**（2026-10-12，批五修复接入注记——H10 Button 文字色契约变更（方案 B），不走正式 phase）｜**只读审计产物**——本文件继续承担**证据与判据**职能（条目编号 = 来源标识；修复实施不改本文件条目，状态更新走末尾接入注记）
> 类型：**非阶段文档**（跨阶段的长期参考，与 `framework-defect-audit.md` / `window-ownership.md` 同级）——**不占 Phase 编号**
> 来源：用户 2026-10-11 指示「**不要去看文档，只看实现代码**，来寻找项目中不合理的实现」
> 方法：**逐文件读实现源码**（**发现与判据阶段** `docs/` 未作为判据）＋ 关键结论用**算术/调用点独立复现**；未复现者显式标注。★ v1.1 收窄：成文阶段引用过既有台账（如 M9 的「条 65」、§8 的命名空间核对）——「不看文档」仅对发现阶段成立，v1.0 表述过宽
> 范围：`ECDI/include/ECDI/**` 与 `ECDI/src/**` 全量；`examples/` 仅在作为**消费者证据**时引用
> 与 `framework-defect-audit.md` 的分工：那份是**能力库存盘点 + 修复队列**（「有没有这个能力」）；本份是**实现质量审计**（「已有实现里哪些写得不合理」）——**两者不重复计账**

---

## 0. 本文件的用法

### 0.1 证据分级（★ 全文每一行都带这个标记，请勿忽略）

| 标记 | 含义 | 对读者的要求 |
|---|---|---|
| ✅ | **主审计者亲自读过该源码行，并（如适用）独立复现过** | 可直接作为修复依据 |
| 📋 | 由分层子审计报告、**主审计者未逐行亲核** | **动手修之前必须先自己读一遍该行** |
| 🔁 | 已用脚本/算术**独立复现**（附复现输入与输出） | 可直接作为修复依据 |
| ❌ | **查过但结论不成立**（有补偿逻辑） | **不要修** |

★ **为什么必须分级**：本轮四个分层子审计中有三个**因用量上限中断、无产出**（Widget / Win32 / Core·Application·Window），它们覆盖范围内的条目**全部由主审计者自己补读**；另一份 Render 审计（16 条）完整交付，但其结论**未经亲核的一律标 📋**——**子代理的行号与引文不能直接当事实用**。

### 0.2 严重度定义

| 级别 | 判据 |
|---|---|
| **Critical** | **正常使用路径必然触发**，且后果是 UB / 崩溃 / 数据损坏 |
| **High** | 需要特定但**常见的**输入或操作序列；后果为用户可见的错误行为，或 UB |
| **Medium** | 需要不常见输入，或后果限于视觉/性能/可维护性 |
| **Low** | 风格不一致、防御性缺口、记账项；当前无真实消费者 |

### 0.3 处置口径（本文件只给建议，不代为决策）

| 建议 | 含义 |
|---|---|
| **修** | 已确认缺陷，且修法明确、改动面小 |
| **先定线再修** | 单点修法只是止血，根因是**缺少某个统一机制**——应先定机制（见 §7） |
| **记账** | 真实但当前无消费者 / 影响可接受——进 `roadmap-deferred.md`，不立项 |
| **仅订正注释** | 代码行为正确，但注释与事实不符（★ 本项目注释承载设计理由，失配代价高） |

---

## 1. 总览

### 1.1 计数

| 严重度 | 条数 | 编号 |
|---|---|---|
| Critical | **3** | C1–C3 |
| High | **10** | H1–H10 |
| Medium | **24** | M1–M24 |
| Low | **14** | L1–L14 |
| **合计** | **51** | —— |

另有 **❌ 不成立项 10 条**（§6）——**同等重要**：它们是被怀疑过、经核查有补偿逻辑的，写出来是为了防止后来者重复怀疑或「修」出回归。

### 1.2 缺陷密度（按子系统）

| 子系统 | Critical | High | Medium | 结构性判断 |
|---|---|---|---|---|
| Application / Window（生命周期与事件） | 2 | 0 | 4 | ★ **缺统一的「非拥有指针失效」机制**（§7.1） |
| Widget 控件族 | 0 | 3 | 5 | ★ 同上 + 逐控件行为不一致（H10 = v1.1 自 C4 降级） |
| Platform / Win32 | 0 | 1 | 3 | ★ 输入捕获与消息配对是主要风险面 |
| Render（GDI + GL 双后端） | 0 | 5 | 6 | ★★ **两个后端不是同一套契约的实现**（§7.2） |
| Animation | 1 | 0 | 0 | 回调生命周期契约未闭合 |
| Layout / Theme / EventSystem / Core | 0 | 1 | 6 | 数值边界与穷尽性机制 |
| **合计** | **3** | **10** | **24** | —— |

★ 上表按**归属子系统**计，与 §4 的**分节**（4.1–4.5）口径一致；行不加总到 51 是因为 Low（14 条）单列在 §5。

### 1.3 三条最贵的结论（先看这三条就够）

1. **C1（退出路径 UB）** ——「窗口还开着就退出」是最普通的用法，而当前路径**每次都会走**到 UB；托盘「退出」菜单即是现成触发点。
2. **C3（框架从未调用真正的 `::SetCapture`）** ——「隐式捕获」是纯框架记账，**OS 层从未被告知**；丢 Up 之后无人自动解锁（仅下一次任意 Down 重设捕获，`Application.cpp:292`），期间 UI 卡在按下态、拖拽越出客户区即失效。
3. **§7.2（GL 与 GDI 不是同一套契约的实现）** —— 8 条 High/Medium 指向同一结论：`--gl` 与 GDI 的差异面比文档承认的大得多；**换后端会换行为，不只是换性能**。

---

## 2. Critical（3 条）

> ★ v1.1：原 C4（Button 文字色）经外部对码复核**降级为 H10**（见 §3 末尾）——其余编号保持稳定、不重排。

### C1 — 应用退出路径必现 UB：`m_deferredDestroy` 先于 `m_windows` 析构 ✅🔁

**位置**：`ECDI/include/ECDI/Application/Application.h:164-172` · `ECDI/src/Application/Application.cpp:51-57` · `:127-160`

**代码**（声明序，C++ 按声明**逆序**析构）：

```cpp
std::unique_ptr<PlatformApplication> m_platformApplication;   // :164  ← 最后析构
std::vector<std::unique_ptr<Window>> m_windows;               // :166
std::vector<std::unique_ptr<Window>> m_deferredDestroy;       // :168  ← 先于此析构
```

```cpp
Application::~Application(){
    m_platformApplication->SetTrayEventSink(nullptr);   // 只断了托盘 sink
}                                                        // ★ 不 Release、不清空残留窗口
```

**失效链**（逐环节已核）：

1. `m_windows` 析构 → 销毁 `unique_ptr<Window>` → `~Window`（`Window.cpp:159-163`）
2. → `Release()` → `m_platformWindow->Release()` → `DestroyWindow()`（`Win32PlatformWindow.cpp:387`）
3. → **同步**派发 `WM_DESTROY` → `WindowMessageHandler.cpp:95-103` → `WindowDestroyedEvent`
4. → `Application::OnWindowDestroyed`（`Application.cpp:127-160`）：
   - `std::find_if(m_windows.begin(), m_windows.end(), ...)`：遍历**正在执行析构**的 vector
   - `m_deferredDestroy.emplace_back(std::move(*it))`：写入**已析构**的 vector
   - `m_windows.erase(it)`：在 vector **自身析构过程中** erase

**触发条件**：`~Application` 时 `m_windows` 非空。**「窗口还开着就退出」即满足**——托盘菜单「退出」不关窗（`examples/ModelProbe/main.cpp:146` 的 `case 3` 就是 `Exit()` 而不 `Release()`）。

**旁证**：`Application.cpp:117` 的注释自己写着「（~Application 残留窗口隐患另案记账）」——但这不是「隐患」，是**必现路径**。

**建议**：**修**。★ v1.1 订正修法骨架：v1.0 建议的 `for (auto& w : m_windows) w->Release();` **本身是迭代器失效 bug**——每次 `Release()` 都会经 `WM_DESTROY → OnWindowDestroyed` **erase 掉正在遍历的元素**（`OnWindowDestroyed` 正是把该窗口从 `m_windows` erase 进 `m_deferredDestroy` 的那一步）。正确形态是「头部恒取」：`while (!m_windows.empty()){ m_windows.front()->Release(); }`——每次 Release 恰好移除 front、循环自然终止，且与 `OnWindowDestroyed` 的「在册才销毁」不变量兼容；`m_deferredDestroy` 声明仍须提前到 `m_windows` **之前**（使其后析构）。★ 若改走「先摘除再销毁」路线，必须同步放宽 `OnWindowDestroyed` 的 `FRAMEWORK_ASSERT(it != m_windows.end())`（`Application.cpp:142`）——否则 Debug 构建直接断死。

---

### C2 — 动画完成帧对已死对象调 `onFinished`（UAF）✅

**位置**：`ECDI/src/Animation/AnimationManager.cpp:163-219`

**代码**（存活检查只在循环顶）：

```cpp
while (i < tickEnd){
    Entry& entry = *m_active[i];
    // ① 失效/取消
    if (entry.cancelled || !entry.state->alive){ EraseAt(i); --tickEnd; continue; }
    // ② 推进（Advance 内部执行用户的 onValue）
    const bool finished = entry.animation->Advance(elapsed);
    // ③ ★ 只查 cancelled，不查 alive
    if (entry.cancelled){ EraseAt(i); --tickEnd; continue; }
    if (!finished){ ++i; continue; }
    // ④ 完成
    entry.state->animationId = 0;
    std::function<void()> onFinished = std::move(entry.onFinished);
    entry.cancelled = true;
    EraseAt(i); --tickEnd;
    if (onFinished){ onFinished(); }        // ← 可能打在死对象上
}
```

**失效成因**：`AnimationToken` 析构**只置 `alive = false`**（`AnimationToken.h:39-47`），**不置 `cancelled`**；只有 `DetachAnimation`（`:111-142`）才置 `cancelled`。⇒ 若 `onValue` 里销毁 token 宿主，且**该帧正好是完成帧**，则落到 ④ 的 `onFinished()`。

**契约违反**：`AnimationToken.h:30-31` 自称「**回调永不打在死对象上**（弱引用保护）」。

**为何测试没抓到**：`TestCancelSelfInOnValue`（`AnimationTests.cpp:314`）走的是 `Cancel` 路径（置 `cancelled`），不覆盖「析构 token」路径。

**必然消费者**：`Window::GetAnimationManager()` 是 public，应用可直接持有 token 并在 `onValue` 里销毁控件。

**建议**：**修**。③ 处改为 `if (entry.cancelled || !entry.state->alive)`——存活判定与 ① 同源；并补一条「onValue 内析构 token 且该帧完成 ⇒ 不得调 onFinished」的用例。

---

### C3 — 全库从未调用 Win32 `::SetCapture` ✅🔁

**位置**：`ECDI/src/Application/Application.cpp:292` · `:329` · `ECDI/src/Window/Window.cpp:351-356`

**证据**（全仓 grep，`docs/` 命中的全是叙述）：

```
SetCapture|ReleaseCapture  →  仅命中框架自有的 Window::SetCaptureWidget（纯赋值）
WM_CAPTURECHANGED          →  0 命中
WM_MOUSELEAVE | TrackMouseEvent → 0 命中
```

```cpp
void Window::SetCaptureWidget(Widget* widget){
    m_captureWidget = widget;      // ★ 只记一个指针，OS 层从未被告知
}
```

**两个后果**：

| # | 后果 | 依据 |
|---|---|---|
| ① | 拖出**客户区**后收不到 `WM_MOUSEMOVE` ⇒ 拖选/滚条不再跟手 | `ScrollBar.cpp:279` / `ScrollBar.h:123` 宣称「拖出条外仍跟手」——**越出窗口即失效** |
| ② | **丢 Up 后卡在按下态（非永久）**：Alt+Tab、右键菜单抢捕获等路径下无人自动解锁 ⇒ 捕获/按下态成员卡住，且后续 Move **全部直派捕获者**、跳过 HitTest ⇒ 整窗 hover 与命中失效。★ v1.1 收窄：任意一次 Down 会**无条件重设捕获**（`Application::OnMouseButtonDown:292`）——**下一次点击即自愈**，恢复出口不止 Up | `Application::OnMouseMove:229-244` 一旦 `captureTarget != nullptr` 就 `return`，**不查存活、不重算 hover** |

★ v1.1 修正：② 不是不可恢复的死锁——卡住态维持到用户下一次**任意点击**（Down 重设捕获）；在那之前 hover 冻结 + 拖拽者收不到 Up 的窗口期真实存在。**①（拖拽/滚条越出客户区不跟手）无自愈路径，是本条的核心**；② 的严重性还在于与 H6 ② 叠加时（捕获者已析构）升级为 UAF。

**建议**：**修（并先定线）**。最小修法二选一：① 在 `SetCaptureWidget` 里真正调 `::SetCapture/::ReleaseCapture`，并处理 `WM_CAPTURECHANGED`；② 至少在 `WM_KILLFOCUS`/`WM_CANCELMODE`/`WM_CAPTURECHANGED` 处无条件 `SetCaptureWidget(nullptr)`。**推荐 ①+② 同做**——①保证跟手，②保证不死锁。

---

## 3. High（10 条）

### H1 — `GridLayout` 在 Release 下整数除零 ✅

**位置**：`ECDI/src/Layout/GridLayout.cpp:8-15` · `:30-31`

```cpp
GridLayout::GridLayout(int columns, ...) : m_columns(columns), ... {
    FRAMEWORK_ASSERT(columns >= 1);      // ★ Release 下 = ((void)0)
}
void GridLayout::Arrange(Widget& parent){
    ...
    const int col = slot % m_columns;    // ★ columns==0 ⇒ SIGFPE
    const int row = slot / m_columns;
```

**依据**：`ECDI/include/ECDI/Core/ECDIAssert.h:41` —— `#define FRAMEWORK_ASSERT(condition) ((void)0)`（Release）。

**同族对照**（说明这是**纪律不一致**，不是「设计选择」）：

| 布局 | 构造期钳制 | 是否有 Release 兜底 |
|---|---|---|
| `VerticalLayout` | `m_padding(padding < 0 ? 0 : padding)` | ✅ 有 |
| `HorizontalLayout` | 同上 | ✅ 有 |
| `GridLayout` | **无** | ❌ **无**（且 `m_columns` 是**除数**） |
| `ListLayout` | **无** | ❌ 无（`rowHeight` 只参与乘法，不致命） |

**触发**：`GridLayout(0, w, h)` + 至少 1 个可见子。当前仅测试构造（`LayoutTests.cpp:993,1051`）——属**潜在**，但公共 API 无任何守卫。

**建议**：**修**。构造初始化列表钳 `columns = (columns >= 1 ? columns : 1)`，与 V/H 布局同形。

---

### H2 — GL 文字：缺字形跳过却不推进笔位，与测量器分家 ✅

**位置**：`ECDI/src/Render/GLRenderer.cpp:220-244` vs `ECDI/src/Render/FontEngine.cpp:88-131`

```cpp
// GLRenderer::DrawText
for (const char32_t cp : DecodeUtf8(text)){
    const std::uint32_t glyphIndex = m_fontEngine->GlyphIndex(font, cp);
    if (glyphIndex == 0){ continue; }        // ★ 不推进 pen
    ...
    pen += slot.advance;
}
```

```cpp
// FontEngine::AdvanceOf（测量链用）
if (glyphIndex != 0){ ...memo 查询... }
const bool loaded = FT_Load_Char(face, cp, FT_LOAD_DEFAULT | ... ) == 0;   // ★ gid==0 也真加载
...
out = static_cast<long>(face->glyph->advance.x >> 6);   // ★ 返回 .notdef 的**非零** advance
return true;
```

★ 注释 `FontEngine.cpp:93-94` 明写「gid = 0（未映射）**永不入 memo**：未映射字符保持 `FT_Load_Char` 原路径」——**这是有意为之**，所以不是 bug，而是**两链口径分家的设计漏洞**。

**后果**：测量 / 断行 / 光标几何**预留**了一个 .notdef advance，绘制却**不推进** ⇒ 该字符之后的字形整体左移、相互重叠；`TextBox` 光标与已绘制文字错位。**GDI 无此问题**（`TextOutW` 会真画 .notdef 并推进，与 `GetTextExtentPoint32W` 同源）。

**触发人群**：任何含 face 不覆盖码点的文本——emoji、生僻 CJK、SimSun 里的 `✔`/方框字符。

**建议**：**修**。`GLRenderer` 对 `glyphIndex == 0` 也推进 `AdvanceOf` 给出的 advance（把「画不出」与「不占位」分开）。

---

### H3 — GL 的 `DrawImage` 无 stride/尺寸校验（越界读）✅

**位置**：`ECDI/src/Render/GLRenderer.cpp:313-335` vs `ECDI/src/Render/GDIBackend.cpp:641-648`

```cpp
// GLRenderer —— 唯一守卫
if (!m_ready || image.width <= 0 || image.height <= 0){ return; }
...
const std::uint8_t* const row = image.pixels.data() + j * image.stride;   // ★ 无 stride 校验
for (int i = 0; i < image.width; ++i){ row[i*4+0..3]; }                   // ★ 无 size 校验
```

```cpp
// GDIBackend —— 有完整守卫（作为对照）
if (image.stride < width * 4 ||
    image.pixels.size() < static_cast<size_t>(image.stride) * static_cast<size_t>(height))
{ return; }
```

**触发**：`PaintContext::DrawImage` 是**公开 API**，应用自造 `Image` 即可。同一命令：**GDI 安全 no-op，GL 越界读**——**后端决定内存安全**。

**建议**：**修**。把 `GDIBackend` 的两条校验前移为共享守卫（或两端都写）。

---

### H4 — GL/FreeType 链永不刷新 DPI ✅🔁

**位置**：`ECDI/src/Render/FreeTypeTextMeasurer.cpp:16-32`（**唯一**生产写入点）

```
grep SetDpi（生产，排除测试）：
  FreeTypeTextMeasurer.cpp:30   ← Initialize 内，仅一次
  （其余全部是 Win32PlatformWindow 的 messageHandler.SetDpi 与测试）
```

```cpp
// 每帧读取，不刷新
const int pixelSize = m_fontEngine->PixelSize(font);   // GLRenderer.cpp:200 → FontEngine.cpp:180-188 用陈旧 dpi
```

**对照 GDI**：`GDIBackend.cpp:875` 与 `GDITextMeasurer.cpp:89` **每次调用**都 `GetDpiForWindow(m_hwnd)`。

**触发**：把 GL 窗口拖到不同 DPI 的显示器（`WM_DPICHANGED` 已处理，但只更新了 messageHandler 的 dpi）⇒ **几何按新 DPI 缩放、字号与 advance 停在旧 DPI** ⇒ 每行 advance 与缩放后布局不匹配（裁剪 / 光标漂移 / 字形大小错），**直到重启**。

**建议**：**修**。`WM_DPICHANGED` 路径上刷新 `FontEngine` 的 dpi（或让 `PixelSize` 每次取窗口 DPI，与 GDI 同形）。

---

### H5 — 对齐 `new` 配非对齐 `delete`（UB）✅

**位置**：`ECDI/src/Render/GDITextMeasurer.cpp:296-307`

```cpp
std::unique_ptr<BYTE[], void(*)(BYTE*)> heapBuf(
    nullptr, [](BYTE* p){ ::operator delete(p); });        // ★ 非对齐释放
...
heapBuf.reset(static_cast<BYTE*>(
    ::operator new(otmSize, std::align_val_t(alignof(OUTLINETEXTMETRICW)))));   // ★ 对齐分配
```

**依据**：C++ 规定对齐形式的分配必须用对齐形式的释放。MSVC 上对齐 `new` 走 `_aligned_malloc`、`operator delete(void*)` 走 `free` ⇒ **不匹配即堆损坏**。

**触发**：仅当 `otmSize > sizeof(stackBuf)`（1024；实测常见 256–342B）——即**字体名极长**时。

**建议**：**修**。删除器改为 `::operator delete(p, std::align_val_t(alignof(OUTLINETEXTMETRICW)))`。

---

### H6 — 焦点/捕获/hover 三个裸指针无脱树清理 ✅

**位置**：`ECDI/include/ECDI/Window/Window.h:336-338` · `ECDI/include/ECDI/Widget/HoverTracker.h:37` · `ECDI/src/Widget/Widget.cpp:64-96`

```cpp
Widget* m_focusedWidget = nullptr;   // 非拥有
Widget* m_captureWidget = nullptr;   // 非拥有
Widget* m_hoverWidget   = nullptr;   // 非拥有（HoverTracker）
```

```cpp
std::unique_ptr<Widget> Widget::RemoveChild(Widget* child){
    ...
    result->m_parent = nullptr;      // ★ 只清 parent——不通知 Window / HoverTracker
    return result;
}
Widget::~Widget() = default;         // ★ 也不通知
```

**三条失效路径**：

| # | 场景 | 后果 |
|---|---|---|
| ① | 丢弃 `RemoveChild` 返回值（最自然的写法）⇒ 控件析构 | 下次按键 `Window.cpp:424-428` `m_focusedWidget->OnKeyDown()` = **UAF** |
| ② | 同上，且该控件是 capture 目标 | `Application::OnMouseMove:233-241` 对已释放对象调 `OnMouseMove` = **UAF** |
| ③ | 同上，且是 hover 目标 | `HoverTracker::IsInTree`（`:32-56`）对已释放对象调 `GetParent()` = **UAF** |

★ **测试为何"通过"**：`HoverTests.cpp:119,139` 明写「**必须持有** RemoveChild 返回值（unique_ptr 丢弃即析构）」——**测试特意绕开了这个坑**，因此它永远不会红。

**建议**：**先定线再修**（见 §7.1）。单点补：`Window` 提供一个「Widget 注销」入口，由 `~Widget` 调用；或改 `RemoveChild` 契约强制回传并由框架延迟销毁。

---

### H7 — 禁用/隐藏对键盘与焦点导航完全无效 ✅

**位置**：`ECDI/src/Window/Window.cpp:24-38` · `:413-430` · `ECDI/src/Application/Application.cpp:195-203` · `:355-367`

```cpp
// Window.cpp:24-38 —— 焦点收集
void CollectFocusables(Widget* node, std::vector<Widget*>& out){
    if (node->CanFocus()){ out.push_back(node); }        // ★ 不查 IsVisible / IsEnabled
    for (size_t i = 0; i < node->GetChildCount(); ++i){ CollectFocusables(node->GetChildAt(i), out); }
}
```

```cpp
// Window.cpp:424-428 —— 键盘派发
if (m_focusedWidget){ m_focusedWidget->OnKeyDown(event); }   // ★ 不查 IsEnabled
```

**对照**：命中测试**确实**跳过禁用/不可见（`Widget.cpp:114-128`）⇒ **鼠标通道与键盘通道语义不一致**。

**后果**：一个 `SetVisible(false)` 的 `TextBox` 仍能被 Tab 聚焦并接受键入（用户看不见内容在变）；一个 `SetEnabled(false)` 的控件仍响应键盘。

**建议**：**修**。`CollectFocusables` 增加 `IsVisible() && IsEnabled()` 过滤（★ 注意 `HitTest` 的语义是「不可见则整棵子树不可命中」，焦点收集应对齐）；`HandleKeyDown`/`OnCharInput` 增加 `IsEnabled()` 前置判断。

---

### H8 — 长文本前缀截断在水平滚动时算错可见跨度 ✅

**位置**：`ECDI/src/Render/GDIBackend.cpp:418`

```cpp
const LONG maxSpan = clip.right - (drawX > clip.left ? drawX : clip.left);
```

**几何分析**：文本从 `drawX` 起画，正确跨度 = `clip.right - drawX`。当 `drawX < clip.left`（**正是水平滚动的情形**）时，`maxSpan` 退化为 `clip.right - clip.left` = **裁剪区宽度**，于是被接受的前缀在 `drawX + clipWidth = clip.right - scrollOffsetX` 处截止 ⇒ **可见行最右侧 `scrollOffsetX` 像素永不绘制**，且**随滚动变宽**。

**调用点**（正是水平滚动的那个）：

```cpp
// TextBox.cpp:1287
const float viewX = textX - m_scrollOffsetX;
// :1325
ctx.DrawText(Point{ viewX, lineY }, lineText, ...);
```

**范围限定**：仅 `wideText.size() > kTextPrefixMinWchars`（`GDIBackend.cpp:23` = 256）走此路——**恰是会有水平滚动的长文本群体**。

**建议**：**修**。`maxSpan = clip.right - drawX`（`drawX` 可能为负，`maxSpan` 自然更大，语义正确）。

---

### H9 — `Win32ChildProcess::QuoteArg` 转义不合规 ✅（本轮新发现）

**位置**：`ECDI/src/Platform/Win32/Win32ChildProcess.cpp:10-22`

```cpp
std::wstring QuoteArg(const std::wstring& arg){
    std::wstring q = L"\"";
    for (wchar_t c : arg){
        if (c == L'"'){ q += L"\\\""; }    // ★ 只转义引号，未处理「引号前的反斜杠」
        else{ q += c; }
    }
    q += L"\"";
    return q;
}
```

**Windows 命令行规则**：引号前的**反斜杠必须成对加倍**，否则会转义收尾引号。本例**完全没有**这一步。

**反例**：`QuoteArg(L"C:\\dir\\")` 产出 `"C:\dir\"` —— 尾反斜杠转义了收尾引号 ⇒ **参数未闭合**，后续参数被吞并进同一个参数。带引号且前面有反斜杠（`a\"b`）同样错。

**触发**：任何以反斜杠结尾的路径参数（极常见）+ 其后还有参数。★ 当前 `ChildProcess` 的消费者（ModelProbe↔probe.exe）参数简单，故未暴露。

**建议**：**修**。按 MSDN「Parsing C++ Command-Line Arguments」实现：遇 `"` 前先把已累积的反斜杠成对加倍，再输出 `\"`。

---

### H10 — `Button` 文字色：主题通路失效（v1.1 自 C4 降级 + 引证订正）✅

**位置**：`ECDI/src/Widget/Button.cpp:37-44` vs `ECDI/src/Theme/DefaultTheme.cpp:7-12`

```cpp
void Button::ApplyTheme(const Theme& theme){
    TextWidget::ApplyTheme(theme);                        // ① 注入 theme 的 foreground（默认 Black）
    // ★ v1.0 引证漏掉的 Button.cpp:40-42「设计补正（2026-08-25 实现期）」注释：
    //   「Button 文字默认白——白字叠蓝底是 Button 视觉身份，非全局主题值」⇒ 白字是有意决策
    TextWidget::m_style.foreground.Apply(Color::White()); // ② 未覆盖时注入 White（Apply 尊重 D7 override）
    ...
}
```

```cpp
TextStyle DefaultTheme::GetTextStyle() const{
    s.foreground.value = Color::Black();   // 对 Label/TextBox 生效；对 Button 恒被 ② 盖掉 = 主题侧死值
```

**判据（v1.1 收窄）**：`ButtonStyle` **没有** foreground 字段，且 ② 恒在 ① 之后执行 ⇒ **Theme 通路**改不了按钮文字色、`TextStyle.foreground` 对 Button 是死值，同一属性存在两个互相冲突的默认值。★ 但**单点覆盖通路健在**——`SetStyle(TextStyleOverride.foreground)` 置 overridden 后 ② 不再生效（D7；`Button.cpp:42` 自述）。⇒ 失效面 = 「按 `Theme.h` 的『Style 是唯一视觉属性来源』契约走主题」这一条路，**不是全部定制能力**。

★ **为何 v1.1 从 Critical 降为 High**：① 按本报告 §0.2 判据，Critical 要求「正常使用路径**必然触发** + UB/崩溃/数据损坏」——本条触发需「用户自定义主题」这一特定但常见的操作，后果是「主题改不动」（用户可见的错误行为）⇒ 判据落在 High；② v1.0 引证漏掉了紧邻的设计意图注释，把「有意决策的副作用」表述成了「契约整体失效」。**缺陷本身真实（已对码核实），降的是级、不是真伪。**

**建议**：**修**。把 `Color::White()` 移进 `DefaultTheme::GetButtonStyle()`（`ButtonStyle` 新增 `foreground` 字段，与全库契约对齐），或显式声明「Button 文字色不经主题」并在 `Theme.h` 收窄措辞。任一方向都须消除「Black/White 两个默认值互相覆盖」的现状。

---

## 4. Medium（24 条）

### 4.1 Application / Window（4 条）

**M1 — `Application::OnTimer` 非动画分支不判空，上一分支却判了** ✅
`Application.cpp:184`（动画分支）`if (event.GetWindow() != nullptr){...}`；`:195`（焦点分支）`FindFocusedWidget(*event.GetWindow())` **直接解引用**。`TrayEvent` 的 `GetWindow()` **恒为 nullptr**，今天靠「托盘事件不走 OnTimer」侥幸未触发。**建议**：修（对称判空）。

**M2 — 换主题在架构上不可能** ✅
`Widget.h` 无任何 `ApplyTheme` 匹配（非虚、逐类型）；`Window`/`Application` 都不持有 `Theme`（grep 0 命中）；`GetDefaultTheme()` 是**静态单例**（`DefaultTheme.cpp:114-117`）⇒ **没有任何 re-apply 通路**。**建议**：**记账**（若有意为 MVP 边界，则应在 `Theme.h` 显式收窄措辞——目前它与「主题系统已落地」的表述冲突）。

**M3 — `Window::SetBounds/SetChromeMode/...` 系列不判空，`GetBounds/GetDpiScale` 判空** ✅
`Window.cpp` 内两类风格并存（如 `GetDpiScale:200` 判空 vs `SetChromeMode:216` 不判）。`m_platformWindow` 在初始化列表必建，故**当前不可达**。**建议**：**仅订正注释**（或统一风格）。

**M4 — `Window::PaintFrame` 依赖 `m_rootWidget` 判空，但 `PaintFrame` 的另一半（平台窗口）不判** ✅
`Window.cpp:128-132` 判 `m_rootWidget`，随后 `:138` `m_platformWindow->GetClientSize()` 不判。构造期两者都存在性不同（前者确实可能为 null），故当前安全。**建议**：**仅订正注释**。

### 4.2 Widget 控件族（5 条）

**M5 — `ScrollView` 用垂直条厚度代表水平条** ✅
`ScrollView.cpp:196` `return (m_vBar != nullptr) ? m_vBar->GetThickness() : 0;`；而水平条几何（`:279`）与 `ViewportHeight`（`:248`）都走它。`ScrollBar::SetStyle`（`ScrollBar.cpp:90`）允许**两条各设厚度** ⇒ 一旦分别设置，视口计算即错。**建议**：修（`BarThickness(orientation)`）。

**M6 — `ScrollView` 样式变更后不重跑布局，破坏自身 C7 不变式** ✅
厚度变化**立刻**改变 `GetMaxOffsetY()`，但 `SetContentExtent` 在 extent 未变时提前返回（`:85-89`），**无人**重跑 `ApplyLayout → ClampOffset → SyncBars` ⇒ `m_offsetY` 可越界，违反 `:296` 自述的「0 ≤ offset ≤ maxOffset」。**建议**：修。

**M7 — `CollapsiblePanel` 的 `Up`/`Left` 方向**位置锚定漂移**（★ 已独立复现）** ✅🔁

**位置**：`ECDI/src/Widget/CollapsiblePanel.cpp:144-180`（`ApplyGeometry`）

```cpp
const int cx = GetX();
const int cy = GetY();                    // ★ 读「当前」几何
...
case ExpandDirection::Up:
    SetPosition(cx, cy + static_cast<int>(h0 - s));   // ★ 又把它当锚点基准用
    Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
```

**根因**：公式按「**位置是 s 的纯函数**」设计（注释 `:155` 自称），但 `cy` 取的是**上一次调用已改写过的值** ⇒ 对 `Down`/`Right`（位置不变）无害，对 `Up`/`Left`（位置随 s 变）**逐帧累加**。

**复现（脚本，本机 Python）**：`SetPosition(100,200)` + `SetSize(300,400)` + 方向 `Up`

```
收起后      : y=600 h=0    底边=600   （正确）
瞬时展开后  : y=600 h=400  底边=1000  （应为 y=200 h=400 底边=600）→ y 偏差 +400px
动画路径逐帧: 第1帧 y=1000 → 第2帧 y=1350 → 第3帧 y=1650 → ... → 第8帧 y=2350（无界增长）
```

**为何测试没红**：`CollapsiblePanelTests.cpp` 的 14 条用例**全部走无窗口瞬时路径**，而瞬时路径只调 `ApplyGeometry` **一次**——`cy` 尚未被污染，故 y 恰好正确。**唯一致命的是有窗口的动画路径**（多次调用）。

★ **这是本文件里唯一由数值复现坐实的视觉缺陷**，且**现有测试结构性地测不到它**。真实消费者 `DesktopNest` 用 `Down`（`BoxView.cpp:208`）⇒ 今天未暴露。

**建议**：**修**。锚点基准改用**固定值**（`m_expandedRect` 的对应边），或在 `SetExpanded` 起点缓存一次基准位置；并补一条**有窗口动画路径**的用例（当前 0 条）。

**M8 — `ButtonStyle::borderWidth` 有主题值、可覆盖、被文档化，却无人读取** ✅
写入点 `Button.cpp:47`（Apply）与 `:83`（Set），`OnPaint`（`:227-272`）只消费 `cornerRadius` + `border`；`:241` 明写「内框尺寸/borderWidth 逻辑随旧实现移除（YAGNI）」。而 `DefaultTheme.cpp:18` 仍广告 `2.0f`。★ **违反同文件 `:233` 自述的纪律**「存了但没用会误导 SetStyle 用户」。**建议**：删字段（连带主题值），或恢复消费。

**M9 — `SetVisible` / `SetEnabled` 不 `Invalidate`（与同族 setter 不一致）** ✅
`Widget.h:148` `void SetVisible(bool v){ m_state.visible = v; }`（`SetEnabled:152` 同）。而 `SetStyle`/`ApplyTheme` 内建 `Invalidate()`。⇒ 回调里改可见性会**静默不刷新**，与条 65 记录的 `SetText` 陷阱同族（★ v1.1 注：条 65 出自 docs 台账，系成文期补充的交叉引用、非代码判据——条目本身已独立对码核实）。**建议**：**先定线再修**（统一「改可见状态 → 谁负责重绘」的口径）。

### 4.3 Platform / Win32（3 条：M10–M12）

**M10 — WIC 解码日志伪造 HRESULT** ✅（本轮新发现）
`WicImageDecoder.cpp:115-137`：`GetFrame`/`GetSize`/`CreateFormatConverter` 三处**丢弃真实返回值**、改传常量 `E_FAIL`，令 `hr=0x%08lX` 的诊断价值归零。★ 同项目 `ShellImageDecoder.cpp:31-32` 恰好写明了**禁止**这种伪造（「不伪造 0x00000000」）。**建议**：修（透传真实 hr）。

**M11 — `TextBox` 无条件 PushClip 一个可能为负的文本区** ✅
`TextBox.cpp:1290` `ctx.PushClip(Rect{ textX, textY, GetTextAreaWidth(), GetTextAreaHeight() })`，而 `GetTextAreaHeight()`（`:447-450`）= `GetHeight() - padding*2` ⇒ **`2*padding > height` 时为负**。**无可见性门控**（PaintContext 的裁剪镜像只记录空交集，**不抑制命令**）。GDI 侧因 `IntersectClipRect` 无害；**GL 侧**触发 M14。**建议**：修（加 `> 0` 门控）。

**M12 — IME 结果 WM_CHAR 吞除计数器无复位** ✅
`Win32PlatformWindow.cpp:785` 设 `m_imeResultPendingChars = result.size()`；**仅靠**逐个 `WM_CHAR` 递减（`:806-812`）。序列被中途打断（失焦 / 输入法少发 / 窗口销毁前）⇒ **余数会吞掉之后真正输入的字符**，且**无超时、无焦点复位**。**建议**：修（加超时或焦点变化时清零）。

### 4.4 Render（6 条：M13–M18）

**M13 — GL scissor 无退化/越界守卫 ⇒ 错误码 + 残留旧盒（欠裁剪）** ✅
`GLRenderer.cpp:685-697`：无 `right <= left` 检查、无 clamp。GL 对负宽高报 `INVALID_VALUE` 并**保留原 scissor** ⇒ 上一级（父）的大 box 继续生效 ⇒ **欠裁剪**（内容溢出到不该画的地方）+ 每帧一个 GL 错误。**可达路径 = M11**。**建议**：修（宽高 <= 0 时 `glDisable(GL_SCISSOR_TEST)` 或 clamp）。

**M14 — `RecordingBackend` 在真实后端会 no-op 的路径上照记** ✅
`RecordingBackend.cpp:41-44`：栈空时仍 `clipOps.emplace_back(...)`；而 `GDIBackend.cpp:721-727` 与 `GLRenderer.cpp:392-396` 都**直接返回**。`RecordingBackend.h:111` 的 `MeasureText` 对**空串**返回非零，而两个真实测量器返回 `{0,0}`。⇒ **测试替身看不见真实后端的防御路径**，且空串宽度语义**从未被测过**。**建议**：修（对齐契约）。

**M15 — `FT_Load_Char` 失败路径与测量口径的隐式耦合（H2 的镜像）** ✅
`FontEngine.cpp:112-124`：`FT_Load_Char` 成功但 `glyphIndex == 0` 时，**既不 memo 也返回 true**（见 H2）。★ 单列为 M 是为了强调**它是有意设计**（注释 `:93-94`），修 H2 时应**只改绘制侧**，不要动这里的 memo 策略（否则会改掉测量基准）。**建议**：修 H2 时保持本处不动。

**M16 — GL 圆角半径钳制可超过 `floor(min/2)`** ✅
`GLRenderer.cpp:280-281` 用 `float*0.5 + lround`，GDI 用**整数 `/2`**（`GDIBackend.cpp:546`）。奇数边长下 GL 得 `2R > min`，三条「互不重叠」带（`:296-298`）**实际重叠** ⇒ 半透明二次混合；且注释 `:294` 自称的 `2R <= min` **不变式为假**。★ 可达消费者：`ProgressBar` 默认 `effectiveRadius = GetHeight()/2`（`ProgressBar.cpp:101-102`）。**建议**：修（GL 侧改整数钳制，与 GDI 同式）。

**M17 — 边缘量化规则在 GDI 内部与两后端间互不一致** ✅
同一矩形**四种口径**：截断求和（`GDIBackend.cpp:353-356`）· 逐边 `lround`（`:47-48` + `:215`）· 逐边截断（`:935-938`）· 裁剪用 `lround`（`:712-715`）。★ GL 的 `Snap = floor`（`GLRenderer.cpp:22-25`）自称与 GDI `static_cast<LONG>` 一致——**负数时前者 floor、后者向零截断**。⇒ 125%/150% 下同一矩形会因 alpha/AA/DPI 切换而 **1px 变宽或位移**。**建议**：**先定线再修**（统一一个取整口径 + 负坐标约定）。

**M18 — 长文本前缀截断可劈开 UTF-16 代理对** 📋
`GDIBackend.cpp:450-455` `prefix = fit + 1` 不后退代理边界；而 `FitText` 契约**明确要求**并已实现后退（`GDITextMeasurer.cpp:208-216`）。**建议**：修（同处补代理边界回退）。

### 4.5 Layout / Theme / EventSystem / Core（6 条：M19–M24）

**M19 — `StyleField` 无法表达「主题未指定此字段」** ✅
`StyleField.h:10` `T value{}` ⇒ `Color` 默认**不透明黑**、`int` 默认 **0**。用户 Theme 省略字段会被**静默注入**：`Panel` 变不透明黑（而 `DefaultTheme.cpp:50` 刻意给 alpha 0 透明）；`ScrollBarStyle.thickness` 省略 ⇒ 条宽 0 ⇒ **滚动条静默失效**。「未设置」不可表达，且恰好等于一个**合法值**。**建议**：**记账**（修法需引入 optional 语义，属公共类型变更）。

**M20 — H/V 布局只钳 `padding` 不钳 `spacing`** ✅
`VerticalLayout.cpp:11-14` / `HorizontalLayout.cpp:11-14`：`spacing < 0` 时 `remaining` **反而变大**（`:49-52` 的 `- m_spacing * gapCount`）、`:72` `y += m_spacing` 让游标**往回走** ⇒ 兄弟重叠。★ 正是同文件 `:46-47` 的 D30-G 注释所担心的乘法陷阱，却漏在**另一个字段**上。**建议**：修（同式钳制）。

**M21 — `EventRouter` 无穷尽性守卫** ✅
`EventRouter.cpp:30-122` 是 15 个同构 `Dispatch<T>` 块，`Dispatch` 的**返回值被丢弃**（而 `EventDispatcher.h:34-49` 特意返回 bool 就是为了能提前退出）。今天覆盖完整；**第 16 种事件类型会被静默丢弃**——无 `default:`、无断言、无日志。★ 与 `WindowMessageHandler.cpp:378` 对未知 `WM_*BUTTON` 用 `FRAMEWORK_ASSERT(false)` 的处理**自相矛盾**。**建议**：修（加一个「无一命中」断言）。

**M22 — `Event::SetHandled/IsHandled` 是死代码** ✅
`Event.h:34-45` 定义并注释「由 Application 控制」，但全库唯一相关行是 `Application.cpp:423` 的一句「**不查 IsHandled**」。⇒ 任何控件**无法**消费事件、也**无法**终止冒泡；`SetHandled()` 编译通过但**无任何效果**。**建议**：删（或实现冒泡早退）。

**M23 — `Core/Logger` 用裸 `assert` 而非统一的 `FRAMEWORK_ASSERT`** ✅
`Logger.cpp:31` `assert(false && "Unknown LogLevel")` ⇒ `NDEBUG` 下静默返回空标签。**建议**：修（统一宏）。

**M24 — `DpiConversion` 的舍入基准与 `Renderer` 缩放基准不同源** ✅
`DpiConversion.h:32-49` 是**整数 half-away-from-zero**；`Renderer.cpp:13-34` 的 `ScaleRect` 是**纯乘法不取整**（注释自称 `C-8`「量化留给绘图 API」）。两条链在同一帧内分别决定「几何位置」与「字号位置」，非整数缩放比下**必然出现亚像素级不一致**。**建议**：**记账**（当前无可见症状，且改动会波及全渲染链）。

---

## 5. Low（14 条，压缩列举）

| # | 位置 | 摘要 | 建议 |
|---|---|---|---|
| L1 | `AnimationManager.cpp:65,81-85` | 重入 `Start` 会**重启已在跑的** Win32 定时器并重置 elapsed 锚点（`m_timerRunning` 本意正是防这个） | 修 |
| L2 | `VerticalLayout.cpp:79` / `HorizontalLayout.cpp:81` | `remaining * GetStretch()` 无 `long long` 保护（对比 `ScrollBar.cpp:96,121` 明写「中间乘积一律 long long」） | 修 |
| L3 | `Animation.h:38` / `AnimationManager.cpp:146` | `Tick` 接受负 elapsed ⇒ 累加器回退、动画倒退且永不完成（唯一驱动方 `Window.cpp:333-341` 非负） | 记账 |
| L4 | `Easing.cpp:9-17` | `ApplyEasing` 不强制文档承诺的值域（`EaseIn(2) == 4`），且 `default:` 掩盖未来枚举 | 修 |
| L5 | `LineCoverage.cpp:55-74` | `GenerateLineCoverage` **无面积上限**，超长斜线 O(面积×S²) 可挂死/爆内存；`PatchSurface` 的 2M 像素警告在网格**已建好**后且**只告警** | 记账 |
| L6 | `RenderingBackend.h:44` | `IsReady()` 默认 true，但**未 Initialize** 的 GDIBackend 会走到 `FRAMEWORK_ASSERT(false)`（Debug 直接中止） | 修 |
| L7 | `GDIBackend.cpp:396` | `DrawText` 的 `SelectObject(hfont)` **不恢复旧字体**，同文件其余三处（`:496,:750`）与 `GDITextMeasurer.cpp:118,133` 都恢复 | 仅订正注释 |
| L8 | `PaintContext.cpp:72-83` | `PopClip` 栈空仍发命令：用户漏一次 `PopClip` 会**连带弹出祖先裁剪区**，API 无检测手段 | 修 |
| L9 | `Win32PlatformApplication.cpp:579` | `TrayEvent` 锚点用 `(0,0)` 当「未初始化」哨兵，把**合法坐标**与未设置混为一谈 | 修 |
| L10 | `GLRenderer.cpp:260-266` | 图集满时**整条线不画**（勾选标记消失）、`:300-304` 只画三条带不画四角（成十字形）；GDI 明写「宁可无 AA，不可缺绘制」并降级 legacy | 修 |
| L11 | `Widget.cpp:171-186` | `Widget::GetChildAt` 只做 `FRAMEWORK_ASSERT(index < m_children.size())`，Release 下**直接 `m_children[index]`** ⇒ 越界访问（对比 `Application.cpp:140-142` 对同类场景**保留了运行时分支**） | 修 |
| L12 | `WicImageDecoder.cpp:190` | `SHCreateMemStream` 的 `size` 参数为 `UINT`，`DecodeMemoryImpl` 收 `size_t` ⇒ **超过 4GB 静默截断**（当前 256MB 上限使其不可达） | 记账 |
| L13 | `Core/UTF8.cpp:9-15` / `Utf8Decode.h:28-37` | **两套 UTF-8 码点计数器**：前者把非法/截断字节**计为 1 个**码点，后者**跳过不产出**。`TextWidget` 用前者切片、用后者算行区间 ⇒ 畸形 UTF-8 下切错字节（`"a\x80b"` 会画出 `"\x80"` 而丢掉 `b`） | 记账（仅畸形输入可达） |
| L14 | `GLRenderer.cpp:315` | GL `DrawImage` 对 `dest` 为负尺寸时因关闭剔除而画出**镜像四边形**；GDI 的 `FillRect` 为空操作 | 修 |

---

## 6. ❌ 查过但不成立（10 条 —— **不要修**）

★ 本节与缺陷清单**同等重要**：这些都被怀疑过，经查有补偿逻辑。写下它们是为了**防止后来者重复怀疑、或「修」出回归**。

| # | 曾被怀疑 | 实际（补偿逻辑） |
|---|---|---|
| 1 | `Widget::Paint` 的 PushClip/PopClip 可能不配对 | 两处 early-return（`:250-251` 不可见、`:262-263` 剔除）**都在 Push 之前**；`:282` 之后**无第二出口** ⇒ 配对成立 ✅ |
| 2 | `AnimationManager` 迭代器失效 | `unique_ptr<Entry>` + **先 `std::move(onFinished)` 再 `EraseAt`**（`:207-211`）⇒ 无悬垂 ✅ |
| 3 | 零时长动画除零 | `NormalizeProgress`（`Animation.h:40-44`）在**除法前**返回 `1.0f` ✅ |
| 4 | `HitTest` 裁剪门控会拦死 `Panel` 子树 | 门控走**非虚 `ContainsRect`**（`Widget.cpp:132`），**刻意避开** `Panel::ContainsPoint` 恒 false 的坑 ✅ |
| 5 | 布局非幂等 | 两遍算法只依赖**父几何 + 子当前尺寸** ⇒ 重复 `Arrange` 结果一致 ✅ |
| 6 | `RenderCommand` 泄漏了后端细节 / `std::visit` 有泛型兜底 | 头内只有 Core 值类型；`Renderer.cpp:51` 的 `std::visit` 配 8 个**非模板重载** ⇒ **漏加重载即编译错误**，无 default ✅ |
| 7 | GDI 专有知识（DIB stride / 行序）外泄到共享层 | `CoverageRaster` / `CornerCoverageMask` / `LineCoverage` **确为 HDC-free**，签名按「调用方给缓冲」设计 ✅ |
| 8 | DIB 行序与释放顺序错 | 全链 **top-down**（`biHeight` 负）；释放顺序均**先恢复 oldBitmap 再 DeleteObject/DeleteDC** ✅ |
| 9 | `Core/String.cpp` 的 UTF-8↔UTF-16 转换有越界 | **显式长度**、失败即空串、无越界 ✅ |
| 10 | `ShellImageDecoder` 的 `GetDIBits` 会越界写调色板 | `BitmapInfo256`（`:92-96`）已按实测**补足 256 项**，规避了 1bpp 写入 2 项的越界 ✅ |

---

## 7. 跨条结论（三条结构性判断）

### 7.1 ★ 框架大量使用非拥有裸指针 + 构造期同步回调，却缺少统一的「失效 / 延迟销毁」机制

**同一根因的 5 条**：C1（Window 生命周期）· C3（捕获状态）· H6（focus/capture/hover 裸指针）· M9（可见性不重绘）· L1（动画重入）。

**证据**：**Widget 侧完全没有 `deleteLater` 之类的延迟销毁**（只有 Window 有 `m_deferredDestroy`）；而 `Widget` 可以被 `RemoveChild` 摘除、被应用随时析构，**框架的 5 处裸指针却都不订阅这个事件**。

**判据**：这不是 5 个独立 bug，**是 1 个缺失的机制**。修单点只能止血——第 6 处裸指针出现时同样会错。⇒ **建议先定线**：给 `Widget` 加一个统一的注销入口（`~Widget` 通知所属 `Window`），或规定「摘树必须经框架接口」并让框架持有延迟销毁权。

### 7.2 ★★ GL 与 GDI **不是同一套契约的实现**

**同一根因的 6 条**：H2（advance）· H3（DrawImage 无校验）· H4（DPI 不刷新）· M13（scissor）· M16（圆角钳制）· M17（量化口径）+ L10（失败降级）+ L14（负尺寸）。

**模式**：**GDI 侧有守卫/降级，GL 侧没有**——这不是「GL 后端还不完善」，而是**两套独立实现的契约没有被同一组断言钉住**。

★ **对使用者的实际含义**：`--gl` 目前**换的不只是性能，还有行为与内存安全**。这比性能差异重要得多，应在文档中显式声明，而不是让用户从差异现象里推断。

**建议**：把「后端契约」抽成**共享的可测试断言**（例如：同一命令序列在两端产生等价的可见结果；`DrawImage` 的非法输入两端都 no-op），而不是逐条对拍修。

### 7.3 `FRAMEWORK_ASSERT` 是 Release 死代码 ⇒ 「契约靠断言维护」的五处全是裸奔

**位置**：`ECDIAssert.h:41`。**受影响**：H1（除零）· `Widget::GetChildAt` 越界 · `Widget::AddChild` 的空指针/多父/环 · `Widget::RemoveChild` 未命中 · N 处「前置条件」。

**判据**：本项目自己在别处**已经**确立了「不把契约建立在断言上」的纪律（`ScrollView.cpp:154-158`：`if (step < 0) return;` 并注明「release 下忽略」）。⇒ **同一条纪律没有推广到 `Widget` 与布局族**。

---

## 8. 与 `framework-defect-audit.md` 的登记关系

| 项 | 说明 |
|---|---|
| **不新建第二个总账** | 本文件是**审计报告**（证据与判据），条目化登记仍归 `roadmap-deferred.md` §7 与 `framework-defect-audit.md` |
| **建议登记** | §7.1（统一失效机制）· §7.2（后端契约断言）· §7.3（断言纪律推广）三条**应各立一条**——它们是**机制级**，不是逐点修 |
| **可直接并入现有条目** | C1（Window 生命周期）宜并入 `window-ownership.md` 的已知问题清单——**它正是那份文档第 4.3 节描述的「~Application 残留窗口」隐患**，本报告给出了**必现路径**与**精确失效链** |
| **编号纪律** | 本文件用 `C/H/M/L` 前缀 + 序号，**与 `framework-defect-audit.md` 的 `D-N` / `A-NN` 命名空间不冲突**（已 grep 确认后者用的是 `D-1..D-8` / `A-1..A-10`） |

---

## 9. 建议的实施顺序（不是排期决定，只是「先修哪个收益最大」）

| 序 | 内容 | 理由 |
|---|---|---|
| **1** | **§7.1 定线**（统一失效机制） | 它阻塞 C1/C3/H6/M9 —— 不定线就修，等于把同一个 bug 改五遍 |
| **2** | **C1**（退出 UB） | 必现、后果最重、且**不依赖 §7.1**（是析构顺序问题，可独立修） |
| **3** | **C3 + H6**（捕获与裸指针） | 用户可见的卡死/崩溃；依赖 1 的结论 |
| **4** | **C2 + H1 + H5 + H9 + H10** | 五条都是小改、独立、无设计负担（UAF / 除零 / UB / 转义 / 主题字段） |
| **5** | **§7.2 定线**（后端契约） | 修 H2/H3/H4 前先定「后端契约如何被钉住」，否则同样是逐条对拍 |
| **6** | H2 / H3 / H4 / H7 / H8 | 依赖 5 的结论；H7 独立可直接修 |
| **7** | M 系列 | 逐条评估，其中 M7（CollapsiblePanel 漂移）已复现、建议优先 |
| **8** | L 系列 + §7.3 | 记账为主 |

★ **M7 特别提示**：它是本报告**唯一**「已用数值独立复现、且现有测试结构性测不到」的视觉缺陷——`Up`/`Left` 两个方向目前**等于不可用**（位置无界漂移），建议至少与 H 系列同批修。

---

## 10. 修订记录

- **v1.6**（2026-10-12）**批五修复接入注记（不改条目）**——关闭 **H10（#66）**：Button 文字色主题通路失效。★ **用户拍板（2026-10-12）**：「H10 改契约吧，目前我们还没正式成库，改契约其实没什么问题」⇒ 取**方案 B（改契约）**。★ **修法**：`ButtonStyle` 增 `foreground` 字段（`ButtonStyleOverride` 同增，追加在末尾以保 C7560）；`DefaultTheme::GetButtonStyle()` 给 `Color::White()`（**与原硬编码值逐位相同 ⇒ 默认观感零变化**）；`Button::ApplyTheme` 删掉硬编码 `TextWidget::m_style.foreground.Apply(Color::White())`，改为注入 `ButtonStyle.foreground` 主题值。★ **单一视觉真相不变**：文字视觉仍落在**同一个**字段 `TextWidget::m_style.foreground`（`OnPaint` 的 `DrawTextContent` 零改动）——变的只是它的**主题来源**（硬编码常量 → 主题值）。不是「新增第二份文字色状态」。★ **D7 完整保留**：`TextStyleOverride.foreground` 与 `ButtonStyleOverride.foreground` 写同一字段（后写者赢），任一 `Set` 过即不被主题覆盖 ⇒ 既有使用者零改动（`SetTextColor` 旧 API 亦自动受益）。★ **回归价值双重证明**：① 回退全部修复 ⇒ **编译失败**（`ButtonStyle` 无 `foreground` 成员，证明 API 确为新增）；② 隔离变体「新头 + 旧 `Button::ApplyTheme`」⇒ **2 条行为用例红**（397 passed / 2 failed）；修复后 **399/399 ×3 全绿**。★ **消费侧**：全库 10 个 `ButtonStyleOverride` 使用点全为 designated initializer 且新字段在末尾 ⇒ **零调用点改动**。★ **仍未动**：#59 / #60（C3）/ #62 / #63 + #68 的 M/L 记账余量。
- **v1.5**（2026-10-12）**批四修复接入注记（不改条目）**——关闭 **M7（#67）**：CollapsiblePanel Up/Left 方向位置无界漂移（本报告**唯一数值独立复现项**）。★ **根因**：`ApplyGeometry` 的公式本应是 **位置 = f(锚定边, s)**（纯函数），却写成「当前位置 + 增量」——而「当前位置」正是**上一次自己写过的值** ⇒ 逐帧累加。Down/Right 未暴露只因位置与 s 无关。★ **修法**：位置改由**缓存锚定边 + s 整值推导**；锚定边只在**跃迁前取一次**（`CacheAnchorFromCurrent`）+ 首次 `SetSize` 由展开基准取（`CacheAnchorFromBasis`）——因为锚定边在 s 变化中**恒不变**（唯一运动学不变量）。**保留 v1.3 原始意图**（以当前几何锚定，兼容 Layout 排位），只把「每帧取」收成「跃迁前取一次」。★ **附带**：`SetExpandDirection` 由头文件内联移入 .cpp（方向改变 ⇒ 锚定边含义改变，基准已建立时须重取锚点；正常路径 `m_anchorValid == false` ⇒ 零行为变化）。★ **回归价值已证**：回退生产修复、只留用例 ⇒ **3 条漂移用例全红**（393 passed / 3 failed），`DownRightZeroRegression` 反向对照保持绿；恢复后 **396/396 ×3 全绿**。★ **补上审计点名的空白**：真窗口动画路径逐帧验不变量（本文件此前 **0 条**有窗口用例）。★ **仍未动**：#59 / #60（C3）/ #62 / #63 / #66（待拍板 A/B）+ #68 的 M/L 记账余量。
- **v1.4**（2026-10-12）**批三修复接入注记（不改条目）**——关闭 **H7（#65）**：键盘/焦点通道与鼠标通道的可见/禁用语义对齐。★ **修法两处（单一真相源）**：① `Window.cpp` 匿名 ns 的 `CollectFocusables` 与 `Widget::HitTest` **逐条对齐**（`!IsVisible()` ⇒ **整棵子树跳过**，对齐 `HitTest:117-121` 的入口门控；`IsEnabled() && CanFocus()` ⇒ 自身入集，对齐 `HitTest:124-128` 只 return 自身、**不阻止递归**）。★ 「不可见跳整棵子树」是刻意选择：焦点语义与命中语义必须同源，否则会出现「Tab 能到、鼠标点不到」的幽灵焦点（比两通道都不过滤更糟）。② `Application::FindFocusedWidget`（`OnTimer`/`OnCharInput`/`OnKeyUp` 三个派发点的共同助手）**收敛 `IsEnabled` 门控**——判据放助手而非散在三处；`Window::HandleKeyDown` 是另一条独立入口（不经 Application），同步加前置。★ **范围边界**：只查 `IsEnabled`、不查 `IsVisible`——能成为焦点说明收集时可见；运行期被隐藏是「焦点悬空」，属 **§7.1（#59）** 不在本条。★ **回归价值已证**：回退生产修复、只留用例 ⇒ 3 条新用例**全红**（389 passed / 3 failed：焦点停在被隐藏的 box 上、停在被禁用的 b 上、禁用后 `y` 仍被插入）；恢复后 **392/392 ×3 全绿**。★ **测试自身一处假绿如实记账**：`DispatchSkipsDisabled` 首版把「字符 + 退格 + 删除」串在一次比对里 ⇒ 未修代码下**竟然通过**——诊断打印 `s0='x' / after y='xy' / after backspace='x'` 才抓出：多余的 `y` 被紧跟的 Backspace 删掉、`x` 恰好回到原位，**两个错误相抵**。⇒ 改为**三个互不抵消的独立观察点**（字符/退格/删除各一条，每条只发一次输入、立刻断言），任一条单独失效都会红。★ **仍未动**：#59 / #60（C3）/ #62 / #63 / #66（待拍板 A/B）/ #67（M7）+ #68 的 M/L 记账余量。
- **v1.3**（2026-10-12）**批二修复接入注记（不改条目）**——用户 2026-10-12 指示「这些就不走正式的 phase 了」⇒ 直接实现 + 回归 + 台账回写，**不立需求/初设/详设三件套**。★ **本批关闭 15 条**：M1 · M10 · M12 · M13 · M16 · M18 · M20 · M21 · M23 · L1 · L6 · L7 · L9 · L11 · L14（逐条修法与文件行号见 `roadmap-deferred.md` §8 v2.20）。★ **两条机制订正（审计原描述不成立，如实记账）**：① **L1**——「重入 `Start` 会重启已在跑的定时器」不成立（`StartInternal` 的 `wasEmpty` 已挡住）；真机制 = **`Tick` 之外取消最后一个动画后定时器永不停止**（`DetachAnimation` 非 tick 分支不 `StopTimer`，而唯一 StopTimer 在 `Tick` ⑥ 段、被开头 `m_active.empty()` 早退挡住）⇒ 已在非 tick 排空分支补 StopTimer。② **L6**——不得给 `GDIBackend` 加 `IsReady()` override：后缓冲是 `BeginFrame` 内懒创建的，以「有缓冲」为就绪判据会打破 `ModelProbeTests.cpp:296`（GDI 未 Initialize 亦可用）与 `WindowBackdropTests.cpp:90`（Initialize 后即可用）两条既有契约；正确修法 = `BeginFrame` 内 `m_hwnd == nullptr` 早退 + `EndFrame` 配对守卫。★ **范围边界**：L8（`PopClip` 栈空仍发命令）不改——`PaintContext.cpp:75-80` 的注释与评审 §12 已把「命令发射照旧、不受守卫门控」定为**有意决策**，改它属于推翻既有裁决而非修缺陷；M10 的另两处 `E_FAIL` 亦非伪造（`ComScope::usable` 是 bool / `SHCreateMemStream` 返裸指针，无真实 hr 可透传）。★ **验证**：MinGW Debug **389/389**（用例 385 → 389，+4）连跑三次稳定；一次 `DesktopLayer.DesktopFollowChain` 假阳性已按「stash 到 HEAD 复跑基线 + 恢复后同二进制三连绿」证伪。★ **GL 侧三项（M13/M16/L14）与 H2/H3 记人工目视项**——全库无任何测试创建真实 WGL context（`GLBackendTests` 只走失败路径），沿用 T26-9 先例，不造假绿的替身断言。★ **仍未动**：#59 / #60（C3）/ #62 / #63 / #65 / #66（待拍板 A/B）/ #67（M7）+ #68 的 M/L 记账余量。
- **v1.2**（2026-10-12）**接入 defect 体系**（`framework-defect-audit.md` v1.84 + `roadmap-deferred.md` v2.18）：**C1 = 已随 #57 修复关闭（ca612eb）**——机制同一（成员析构序 + 析构期容器改写），修复形态（m_shuttingDown 旗标 + 显式排水）覆盖本条建议「头部恒取」骨架的同一保证且免除在册断言联动；其余分组登记 **#59–#68**（§7.1/§7.2/§7.3 机制条 + C2/C3/H7/H10/M7 点条 + H1/H5/H8/H9 小修群 + M/L 余量）——**映射与排期见 deferred v2.18；本文件条目编号继续作为来源标识**。
- **v1.0**（2026-10-11）**首版**。用户指示「不要去看文档，只看实现代码」寻找不合理实现。范围 = `ECDI/include/ECDI/**` + `ECDI/src/**` 全量实现源码（`docs/` 全程未作为判据）。**产出**：Critical 4 · High 9 · Medium 24 · Low 14 = **51 条**，另 **❌ 不成立 10 条**。**方法**：主审计者逐文件读核心层（Widget / Application / Window / Render / Animation / Layout / Theme / EventSystem / Core / Win32），并用 **3 个并行分层子审计**覆盖 Widget / Render / Win32 / Core 面。**★ 子审计产出情况（如实记账）**：Render 审计（16 条）完整交付，其结论**未经亲核的标 📋**；**Widget / Win32 / Core·Application·Window 三份因用量上限连续两轮中断、零产出** ⇒ 其范围内条目**全部由主审计者自己补读**（标记 ✅）。**独立复现**：① `CollapsiblePanel` 位置锚定漂移（脚本算术，M7）② `AdvanceOf` 的 `.notdef` advance 路径（H2）③ `FontEngine::SetDpi` 生产调用点唯一性 grep（H4）④ `GridLayout` 除零的断言失效链（H1）⑤ `::SetCapture` 全库 0 命中（C3）。**统计自证**：编号空间已 grep 确认与 `framework-defect-audit.md`（`D-N` / `A-NN`）不冲突。**未做**：本轮**不改任何代码**。
  - ★ **落盘后核对（按文档纪律逐项做完，如实记账）**：① **BOM** ✅（写入时缺失、核对后补上）；② **行尾** ✅ 全 LF（无裸 CR）；③ **代码围栏配平** ✅ 48 行 = 24 对；④ **表格列数对账** ✅ 12 个表块、0 处不匹配；⑤ **表内未转义 `|标识符|`** ✅ 0 命中；⑥ **「表头+分隔线同行」** ✅ 0 处；⑦ **setext-H2 风险（`---` 紧贴正文）** ✅ 0 处；⑧ **活代码区残留扫描** ✅ 0 命中；⑨ **编号连续性与计数对账** ✅（`C 1–4` / `H 1–9` / `M 1–24` / `L 1–14` 全连续；§1.1 声明的分级计数与正文实际条目数**逐项相等**，由脚本从文档内**读取**声明值再与实测比对，非人工推算）。
  - ★ **核对过程本身抓到 4 处真错并已订正**（**记账，不掩盖**）：① §1.1 计数写 `Medium 18` 而正文实为 **24**（「编号标题 ↔ 实际条目数」不对账）；② §1.2 密度表按错误分节重算，合计与 §1.1 冲突；③ §4.5 标题写「5 条」而实含 6 条；④ 交叉引用残留旧编号（`§5.1`/`§5.2` 应为 `§7.1`/`§7.2`）与一处 `M1(旧编号)` 编辑残留。⇒ **全部由「落盘后实测对账」抓出**，无一是靠通读发现的——★ 印证「计数类断言必须落盘后实测、不得推算」。
  - ★ **条目重排**：核对中发现 Medium 编号在文档内**非严格递增**（重复出现 `M18`）⇒ 由脚本按**文档顺序**重排为 `M1..M24`，并把 `M18–M24` 归入 §4.5、`M13–M17` 归入 §4.4（**修正了「Render 层条目被记在 Platform 分节下」的归属错误**）。
  - ★ **v1.1 对本条自检记录的勘误（外部对码复核抓出）**：上述 ②/③/⑨ 的「✅」与 v1.0 正文不符——`§1.2` 的 Platform/Win32 与 Render 两行 Medium 计数**对调**（写 6/3、实际 3/6；两区之和恰为 9，只对总和的脚本查不出来）；§4.4 标头「5 条：M13–M17」实含 **6** 条、§4.5 标头「7 条：M18–M24」实含 **6** 条且 M18 实际仍在 §4.4（与本条「M18–M24 归入 §4.5」的描述不符）；⑨ 的对账只核了 §1.1 总数与编号连续性，**未核分区计数与分节标头**——印证「对账必须核到最细粒度，总和一致会掩盖分区错误」。
- **v1.1**（2026-10-11）**吸收外部对码复核**（对码复核 Critical 全部 4 条 + High 全部 9 条 + 约 20 条 M/L/❌ 抽查；**事实层零假阳性**——所有发现均对上源码，订正集中在定级/引证/自检声明）。**四处订正**：① **C4 → H10**（降级 + 补引 `Button.cpp:40-42` 设计意图注释——白字为有意视觉身份决策、`SetStyle` 单点通路健在，失效面收窄为「Theme 通路」；按 §0.2 判据归 High，编号保持稳定不重排）；② **C3 ② 收窄**（「永久卡死」→「卡到下一次任意点击」——`OnMouseButtonDown:292` 无条件重设捕获，恢复出口不止 Up；① 拖拽不跟手维持原判，仍为 Critical 的核心）；③ **对账订正**（§1.2 两行对调、§4.4/§4.5 标头计数、§1.1 随 C4→H10 更新为 Critical 3 / High 10、§9 序 4 增补 H10）；④ **方法学声明收窄**（M9 的「条 65」证实成文期引用过 docs 台账——「不看文档」仅对发现与判据阶段成立）＋ **C1 修法骨架订正**（v1.0 建议的 for 循环自身是迭代器失效 bug → 改「头部恒取」while 形态；`OnWindowDestroyed` 的在册断言联动）。v1.1 落盘后再次**全项实测对账**（见下）。本轮仍**未改任何代码**。
  - ★ **v1.1 落盘后核对**：① **BOM** ✅；② **行尾** ✅ 全 LF；③ **代码围栏配平** ✅；④ **编号连续性** ✅（C 1–3 / H 1–10 / M 1–24 / L 1–14）；⑤ **计数逐项对账** ✅（§1.1 声明 = 正文实测；§1.2 分区 = §4 分节实测，含 Platform 3 / Render 6；合计 3+10+24+14 = 51）；⑥ **残留扫描** ✅（`C4` 仅存于 v1.0 历史记录、§2 墓碑注与 H10 溯源标注，无悬挂引用）。
