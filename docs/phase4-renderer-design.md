# Phase 4 Renderer 初步设计（phase4-renderer-design.md）

> 阶段：第四阶段 Renderer System → 初步设计
> 前置：职责确认已评审通过（Renderer 不知道 Widget / Paint 管"画什么"、Renderer 管"怎么画"）
> 本文档收录 Renderer 全部决策（问题 1-23 复盘 + 双缓冲 + 迁移项），供详细设计使用。
> 修订记录：v1.0 初版（2026-08-09），基于决策链复盘；已修正复盘中的两处决策误记（RenderCommand 形态、FromRGBA8）。v1.1（2026-08-09）：决策 34/35 落盘——Renderer 改持 `RenderingBackend&` 引用（推翻决策 10 的 unique_ptr）、Window 持 GDIBackend 值成员（构造体内 SetHwnd 两阶段初始化），连带修订决策 11/12、§9.2、§19 第 3/9 条。v1.2（2026-08-09）：决策 36 落盘——Renderer::Execute 用 std::visit（决策 9 重演确认）。v1.3（2026-08-09）：决策 37 落盘——DrawRectCommand = { Rect, Color }（决策 2 重演确认）+ emplace_back 入缓冲。v1.4（2026-08-09）：决策 38 落盘——Back Buffer 重建"先创建后替换"事务性顺序（决策 15/19/26 重建体系的细化）。v1.5（2026-08-09）：决策 39 落盘——WM_PAINT 路径：WindowProc 删特判 → HandleMessage `case WM_PAINT: OnPaint(); return 0;`。v1.6（2026-08-09）：详细设计评审修订——P0 删 §18 末尾 7 行重复记录（含与决策 34 矛盾的旧决策 10）、§9.3/决策 26 改"构造注入 hwnd"→"默认构造+SetHwnd"；P1 修正 §19 三处编号漂移（决策15→§7、决策24→25、决策26→18/20/31）；P2 §2架构图/§3职责链表补 GDIBackend、§19 加 OnPaint 撞名声明；P3 确认 Rect/Point float（§4.2/§17-3）。v1.7（2026-08-10）：GPT 终审建议落盘——决策 40 namespace ECDI 全项目引入（含旧文件，独立 Commit 4.0）、决策 41 Window::OnPaint 改名 PaintFrame、决策 42 PaintContext 改 class+私有成员；新增 §20 Header 依赖规则、§21 实现顺序 4.0-4.8；§16 补 RecordingBackend DrawCall 形态；同步 §2/§7/§10/决策 39/§19-10。

---

## 1. Design Goals

把 ECDI 从 Phase 3 的：

```
Widget → GDI → 屏幕（直接绘制）
```

变成：

```
Widget → PaintContext → RenderCommand → Renderer → RenderingBackend → Graphics API
```

核心目标：**Widget 与具体图形 API 解耦**。Renderer 不是 Widget System 的一部分，是独立的基础设施。

## 2. Architecture

```
                        Application
                             │
              ┌──────────────┴──────────────┐
              │                             │
           Window A                      Window B
          │      │                      │      │
      GDIBackend Renderer            GDIBackend Renderer
                      │
       RenderingBackend（抽象接口，Renderer 持引用）
              │
       ├── GDIBackend（默认实现）
       ├── RecordingBackend（测试实现）
       └── 第三方 Backend（允许扩展）
```

一次 WM_PAINT 的完整流程（Window 编排）：

```
WM_PAINT → Window::PaintFrame()
   ├── m_commands.clear()                     // 复用命令缓冲
   ├── PaintContext ctx(m_commands)           // 收集门面（栈上，一次一帧）
   ├── m_rootWidget->Paint(ctx)               // Widget 树产生命令
   ├── m_renderer.BeginFrame()                // 帧开始（GDIBackend 内部拿 HDC）
   ├── m_renderer.Execute(m_commands)         // visit 分发 → Backend 操作
   └── m_renderer.EndFrame()                  // 帧结束（GDIBackend 内部还 HDC + BitBlt）
```

## 3. 职责链

| 角色 | 职责 | 认识谁 | 不认识谁 |
|------|------|--------|----------|
| Widget | 决定画什么 | PaintContext | RenderCommand / Renderer / Backend / GDI |
| PaintContext | 收集门面，构造命令写缓冲 | CommandBuffer | Widget / Renderer / RenderCommand 类型细节 |
| RenderCommand | 纯数据（死数据） | — | 一切（无执行逻辑） |
| CommandBuffer | 保存本次 Paint 的绘制数据 | — | — |
| Renderer | 解析命令（visit → 调 Backend 操作） | RenderCommand / RenderingBackend | Widget / GDI / D2D |
| RenderingBackend | 操作 → 具体 API | Rect / Color（公共类型） | Widget / variant / CommandBuffer |
| Window | WM_PAINT 编排 + 持有命令缓冲 + GDIBackend + Renderer | 渲染层全部组件 | HDC / GDI 类型 |

依赖方向全程单向：`Widget → PaintContext → CommandBuffer ← Renderer → Backend`，两两不相识的角色靠数据或编排层连接。

## 4. Core Types（公共基础类型）

Rect / Point / Color 是 **cross-cutting 基础类型**（不属于 Widget 层或 Renderer 层），放公共层（`ECDI/Core/`），全框架共用，零转换。

```
ECDI/Core/
    Rect
    Point
    Color
```

依赖关系：

```
        Core
       /    \
   Widget  Renderer
```

分层隔离的是**职责和语义**，不是数学基础数据类型。

### 4.1 Color

```cpp
struct Color {
	float r, g, b, a;   // RGBA float（不采用 COLORREF；公共 API 不暴露 Win32 类型）

	static Color White()   { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
	static Color Black()   { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
	static Color Red()     { return { 1.0f, 0.0f, 0.0f, 1.0f }; }
	static Color Green()   { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
	static Color Blue()    { return { 0.0f, 0.0f, 1.0f, 1.0f }; }
	static Color Gray()    { return { 0.5f, 0.5f, 0.5f, 1.0f }; }

	static Color FromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);  // 8-bit 直觉入口
};
```

决策：
- float RGBA（渲染生态长期标准；颜色插值免 banding；公共契约锚定未来）
- 少量标准色 + FromRGBA8（标准色服务"常用命名"，FromRGBA8 服务"自定义颜色"，两者互补，均头文件内联）
- 不做完整色表（140 个 CSS 色名是膨胀）
- **Alpha 第一版后端不消费**（数据结构保留，GDIBackend 只画不透明；混合语义等真需要半透明的控件出现再定——避免为不存在需求定错混合模式）

### 4.2 Rect / Point

- 数值类型 **float**（✅ 已确认，与 Color 一致、渲染通用）
- 注意：Phase 3 的 `Widget::m_geometry` 是 int，命令 Rect(float) 接收时隐式转换（OnPaint 收到的最终坐标是 int，构造 Rect 时转 float）

## 5. RenderCommand

### 5.1 形态（关键决策，v1.0 修正）

**采用 `std::variant`，即使第一版只有一个命令类型**：

```cpp
struct DrawRectCommand {
	Rect rect;
	Color color;
};

using RenderCommand = std::variant<DrawRectCommand>;
using CommandBuffer = std::vector<RenderCommand>;
```

理由（决策链一致性）：已经选择 variant 作为多类型表示方式，第一版直接采用它，避免将来改变命令类型表示方式。**variant 不是新增功能，只是 Command 的类型表示方式**（单成员 variant 与裸结构运行时等价）。

未来扩展（无需改任何结构）：

```cpp
using RenderCommand = std::variant<DrawRectCommand, DrawTextCommand, DrawImageCommand>;
```

### 5.2 定性

- **纯数据**：值语义、无虚函数、无执行逻辑、无 Renderer/Backend 依赖
- **最终坐标**：命令存"算好的绝对/窗口坐标"（坐标转换在 Paint 阶段完成，Renderer 零上下文执行）
- **顺序即绘制序**：命令在 buffer 中的顺序 = 最终绘制顺序（Z-Order 唯一真源是 Widget 树 children 顺序，Renderer 不排序）
- **短生命周期**：一次 Paint → 执行 → 失效；不做长期对象（值进 vector，无堆分配）

## 6. CommandBuffer

- 就是 `std::vector<RenderCommand>`（**第一版不搞封装类**，YAGNI）
- **Window 持有并跨帧复用**（`clear()` 不释放容量，避免 Resize 高频 WM_PAINT 时重复分配）
- 读写权限靠**引用形态**分离（不用类）：

```
写：PaintContext 持 std::vector<RenderCommand>&（非 const）
读：Renderer::Execute(const std::vector<RenderCommand>&)（const，类型硬保证）
调度：Window 持成员缓冲 + clear
```

- 升级为封装类的触发条件（未来）：第三个写者出现 / 缓冲需要承载语义（统计/脏标记/资源引用）/ 需要隐藏 vector 形态

## 7. PaintContext

- **每次 Paint 在栈上创建**（收集门面，一次一帧，用完即毁；长期存在的是 CommandBuffer）
- **不认识 Widget / Renderer**（纯渲染层类型，只依赖命令缓冲）
- 提供**按绘制意图命名的高层方法**（第一版 `DrawRect`），内部构造命令写入缓冲；Widget 不接触 RenderCommand/variant
- **零坐标逻辑**：收到的坐标是最终坐标，原样进命令（转换发生在 Widget::Paint 基类遍历的 offset 累加）

```cpp
class PaintContext {
public:
	explicit PaintContext(CommandBuffer& commands);   // 决策 42：私有成员，构造绑定

	void DrawRect(const Rect& rect, const Color& color);   // 内部 push_back(DrawRectCommand{...})

private:
	CommandBuffer& m_commands;   // 决策 42：class + 私有，强化决策 8 完全封装；未来加 dpiScale 等状态不动公开结构
};
```

## 8. Renderer

- **Window 级**（每窗口一个 Renderer，生命周期随 Window，多窗口天然隔离）
- 职责：`RenderCommand → std::visit → RenderingBackend 操作`
- **不认识 Widget**（不 include Widget.h、不遍历树、不 Layout/HitTest/Focus）
- 执行形态（v1.0 修正：用 visit，穷尽性由编译器保证——加新命令忘处理 = 编译错误）：

```cpp
void Renderer::Execute(const std::vector<RenderCommand>& commands) {
	for (const auto& command : commands) {
		std::visit([this](const auto& cmd) { ExecuteCommand(cmd); }, command);
	}
}

void Renderer::ExecuteCommand(const DrawRectCommand& cmd) {
	m_backend.DrawRect(cmd.rect, cmd.color);   // 操作粒度：Backend 不认识命令（决策 34：引用成员用 . 访问）
}
```

- 不负责：排序 / batching / 刷新控制 / 资源管理（Backend 持有渲染资源）

## 9. RenderingBackend

### 9.1 抽象接口（操作粒度）

```cpp
class RenderingBackend {
public:
	virtual ~RenderingBackend() = default;

	virtual void BeginFrame() = 0;
	virtual void DrawRect(const Rect& rect, const Color& color) = 0;
	virtual void EndFrame() = 0;
};
```

**操作粒度而非命令粒度**：Backend 不认识 `RenderCommand`/variant/Widget/PaintContext——任何系统（不只 Renderer）都能直接用 Backend 画。

### 9.2 所有权（✅ 已定：决策 34/35）

**Renderer 持引用、Window 持值成员**（最终裁决，推翻决策 10 的 unique_ptr）：Window 持有 `GDIBackend` 值成员并注入 Renderer（构造体内 SetHwnd，见决策 35）；Renderer 只依赖不拥有。详见 §18 决策 34/35。

### 9.3 第一版两个实现

| 实现 | 用途 | 要点 |
|------|------|------|
| GDIBackend | 默认，实际绘制 | 视觉与 Phase 3 一致；默认构造 + 构造体内 SetHwnd（决策 35，hwnd 在初始化列表阶段不可用）；HDC 是内部实现细节 |
| RecordingBackend | 测试 | 记录每次 DrawRect 调用，供断言"渲染行为" |

"别人写后端" = 实现 3 个虚函数。

## 10. Window / WM_PAINT

- **Window 完整编排**（A 方案）：Window 是导演，知道所有渲染组件；Widget/PaintContext/Renderer 互相不认识
- **Window 不接触 HDC**：HDC 进出藏在 GDIBackend 帧管理里（BeginFrame 内 `BeginPaint`，EndFrame 内 `EndPaint`），Window::PaintFrame 零平台类型
- 编排逻辑抽私有方法 `PaintFrame()`（✅ 决策 41 改名，原 OnPaint——避免与 Widget::OnPaint 撞名），`WindowProc` 的 WM_PAINT case 只调它（保持 WindowProc 薄）

```cpp
void Window::PaintFrame() {
	m_commands.clear();
	PaintContext ctx(m_commands);
	m_rootWidget->Paint(ctx, 0, 0);   // 根从 (0,0) 开始（决策 6：保留 offset 参数）
	m_renderer.BeginFrame();
	m_renderer.Execute(m_commands);
	m_renderer.EndFrame();
}
```

## 11. Double Buffering（双缓冲）

**第一版做**（初版功能少，但必须到"能用的程度"——闪烁是最毁可用感的视觉问题）。

- 归属：**GDIBackend**（Window/Renderer 只看到 BeginFrame/DrawRect/EndFrame，不知道 Memory DC/BitBlt）
- 实现：BeginFrame 用/建内存 DC（CreateCompatibleDC + CreateCompatibleBitmap）→ 命令画到内存 DC → EndFrame 时 BitBlt 到窗口 HDC
- **⚠️ Resize 必踩坑**：内存位图必须随客户区尺寸重建（WM_SIZE 时旧位图尺寸失效，不重建会花屏）——记为详细设计/实现注意事项

## 12. Root 白色背景（✅ 已确认：决策 16）

**视觉要求确定**：Phase 4 必须保持 Phase 3 的白色 Root 背景效果。

**实现归属已拍板（详细设计决策 16：A）**：`GDIBackend::BeginFrame` 内部隐式填充白色（FillRect WHITE_BRUSH 到 Memory DC，作为每帧第一个操作）。

理由：
- Phase 3 决策延续（Root 背景归平台/窗口层）——Backend 就是平台层
- 白底是**窗口/平台语义**，不是"Widget 想画的东西"——塞进命令体系是层次混淆
- 所有 Backend 实现的统一模式：GDI FillRect / D2D Clear / Recording 记录清屏调用
- 测试职责：白底归 **Backend 行为测试**（RecordingBackend 断言 BeginFrame 清屏），不归命令测试（Widget 命令不含白底）

## 13. Phase 3 → Phase 4 Migration

| 改动 | 说明 |
|------|------|
| Widget.h 删 HDC 前向声明 | 删除 `struct HDC__; using HDC = HDC__*;`（Phase 3 的 GDI 临时桥梁） |
| Widget.cpp | `Paint(HDC, int, int)` → `Paint(PaintContext&)`；OnPaint 签名含最终坐标参数（`OnPaint(PaintContext&, int x, int y)`，保留 Phase 3 的 x/y 传递方式） |
| Panel / Button | OnPaint 改造：`ctx.DrawRect(x, y, w, h, Color::Gray()/Blue())` |
| Window.cpp | 删旧 WM_PAINT 特判（BeginPaint/FillRect 白/root.Paint/EndPaint）→ 换 `OnPaint()` 编排 |
| 新增文件 | `ECDI/include/ECDI/Render/` + `ECDI/src/Render/`（RenderCommand / PaintContext / Renderer / RenderingBackend / GDIBackend / RecordingBackend） |

## 14. MVP Scope

```
✅ Core Types：Rect / Point / Color（float RGBA + 标准色 + FromRGBA8）上移 Core/
✅ Command：DrawRectCommand + std::variant + CommandBuffer（Window 持有复用）
✅ Paint：PaintContext.DrawRect() + Widget::Paint(PaintContext&) + 坐标转换在 Paint
✅ Renderer：std::visit 分发 + RenderingBackend 抽象接口
✅ Backend：GDIBackend（默认 + 双缓冲）+ RecordingBackend（测试）
✅ Window：OnPaint() 编排 + 不接触 HDC
✅ 迁移：Phase3 HDC 临时桥梁删除（含 Widget.h 前向声明）
✅ 测试：双层（命令断言 + RecordingBackend 断言）
❌ Alpha Blending
```

## 15. Out of Scope（明确不做）

```
❌ Text / Font System        ❌ Image / Texture
❌ Resource Manager          ❌ Clip
❌ Dirty Region / Partial Redraw
❌ Animation / VSync         ❌ Render Graph / Material / Pipeline / Shader
❌ GPU Batching              ❌ Alpha Blending
```

目标不是"造游戏引擎级渲染器"，而是"把 ECDI 从 Widget→GDI 直画变成 Widget→Command→Renderer→Backend→API"。

## 16. Testing（双层）

**第一层：Paint / Command 生成**

```
Widget → PaintContext → CommandBuffer
断言：Rect/Color/顺序 是否正确（命令是纯数据，可断言）
```

**第二层：Renderer / Backend 转换**

```
CommandBuffer → Renderer → RecordingBackend
断言：Renderer 是否正确把命令转换成 DrawRect() 调用
```

不需每个测试都真的打开 GDI 窗口。

**RecordingBackend 形态**（决策 12 落地；`draws` 公开是测试实现的刻意选择，测试直接断言实例）：

```cpp
class RecordingBackend : public RenderingBackend {
public:
	struct DrawCall { Rect rect; Color color; };
	std::vector<DrawCall> draws;      // 公开：测试直接断言（决策 12）

	void BeginFrame() override;       // 可选：记录清屏事件（决策 16：白底行为测试归 Backend）
	void DrawRect(const Rect&, const Color&) override;   // draws.emplace_back({ rect, color })
	void EndFrame() override;
};
```

## 17. Acceptance Criteria

1. main.cpp 两窗口正常显示（灰 Panel + 蓝 Button）
2. **视觉结果与 Phase 3 基本一致**（Root 白 / Panel 灰 / Button 蓝）
3. **实际使用无明显闪烁**（双缓冲生效）
4. 双层测试通过

---

## 待详细设计确认项（实时更新）

> ✅ 全部确认（2026-08-10）：Architecture / API Boundary / Render Pipeline / Backend Model / GDI Strategy / Testing Strategy 全部冻结，进入实现阶段（见 §21）。

| # | 项 | 状态 |
|---|----|------|
| 1 | RenderingBackend 所有权形式 | ✅ 已定（决策 34/35：Renderer 持引用、Window 持 GDIBackend 值成员，推翻决策 10 的 unique_ptr） |
| 2 | Root 白底实现归属 | ✅ 已定（决策 16：GDIBackend::BeginFrame 隐式填充） |
| 3 | Rect/Point 数值类型（float） | ✅ 已定（float）：与 Color 一致、渲染通用；Phase3 int Geometry 经决策 1 过渡（内部 Rect float、API 保持 int） |
| 4 | GDIBackend 双缓冲位图管理 | ✅ 已细化（决策 26/27：懒创建 + GetClientRect 自检） |
| 5 | OnPaint 签名 | ✅ 已定（决策 6：`OnPaint(PaintContext&, int x, int y)` + 决策 6 修正：`Paint(PaintContext&, int offsetX, int offsetY)` 保留 offset 参数） |

---

## 18. 详细设计决策记录（按编号，实时追加）

> 本表记录详细设计阶段已拍板的决策（v1.0 文档发布后新增），供实现直接照抄。

| # | 决策 | 结论 |
|---|------|------|
| 1 | Widget Geometry 迁移 | A 过渡形态：删独立 Geometry 类，`m_geometry` 改用公共 `Rect(float)`；公开 API（SetPosition/SetSize/GetX 等）**保持 int 签名**，内部转换；Layout/HitTest/main.cpp 零改动 |
| 2 | DrawRectCommand 形态 | A：`struct DrawRectCommand { Rect rect; Color color; };`（复用公共类型，零字段搬运） |
| 3 | RenderCommand 定义 | A：`using RenderCommand = std::variant<DrawRectCommand>;`（直接别名，无外壳） |
| 4 | CommandBuffer 形态 | A：`using CommandBuffer = std::vector<RenderCommand>;`（问题 12 重演，引用形态读写分离，升级触发条件见 §6） |
| 5 | PaintContext 形态 | A：持 `CommandBuffer&` + `DrawRect(const Rect&, const Color&)` 方法；不认识 Widget/Renderer |
| 6 | Paint/OnPaint 坐标接口 | A + **修正**：`Paint(PaintContext&, int offsetX, int offsetY)`（**必须保留 offset 参数**）+ `OnPaint(PaintContext&, int x, int y)`；坐标遍历保持 Phase3 模型 |
| 7 | PaintContext::DrawRect 参数 | A：`(const Rect&, const Color&)`（与 DrawRectCommand 同构，零参数重组） |
| 8 | PaintContext 封装边界 | A：完全封装，只暴露绘制方法（DrawRect）；**不提供 GetCommands()/写访问**——Widget 只面对门面，不接触 CommandBuffer（问题 13/决策 5 的强化锁定） |
| 9 | Renderer::Execute 命令访问 | A：`std::visit` + `ExecuteCommand` 重载集（问题 21 重演）。穷尽性由编译器保证：未来加命令忘加重载 = 编译错误（B 直接 get 未来要重写判断、C holds_alternative 链啰嗦且漏分支静默） |
| 10 | Renderer 与 Backend | ~~A：`std::unique_ptr<RenderingBackend>`~~ **已被决策 34 推翻** → 改为 `RenderingBackend&` 引用（见决策 34）。⚠️ 原规则保留为**项目通用规范**：凡类拥有 `unique_ptr<前向声明类型>` 成员，构造/析构在 .cpp 定义并 include 完整类型（Layout 复用教训）——Renderer 改持引用后**不再触发**此规则（引用成员可前向声明，C2027 风险消失） |
| 11 | Backend 注入方式 | A：**Window 创建具体 Backend（GDIBackend）→ 注入 Renderer**。✅ 已被决策 35 细化：GDIBackend 是 Window **值成员**、Renderer 引用它（决策 34）。⚠️ 注入时机：构造体内、CreateWindowExW 成功（hwnd 就绪）之后，**不能放成员初始化列表**（hwnd 在初始化列表阶段是 nullptr，见决策 35 的 SetHwnd 两阶段方案）。测试注入 RecordingBackend 绕开 Window 直接构造 Renderer（第二层测试独立于 Window，决策 12） |
| 12 | Renderer 是否暴露 Backend | A：**完全不暴露**（无 GetBackend()）——Renderer 是 Backend 唯一正常使用者。✅ 决策 34 修订测试检查模式：RecordingBackend 是测试函数内**栈对象**，Renderer 引用它，测试直接访问 RecordingBackend 实例检查记录（引用方案下连 unique_ptr 的 `get()` 裸指针都不需要，观察方式更简单）。B 模糊职责边界、C 测试宏过度设计均否决 |
| 13 | Renderer Begin/End 转发 | A：`Renderer::BeginFrame/EndFrame` **直接转发**给 `Backend::BeginFrame/EndFrame`（Execute 走 visit 分发）。"Window 编排 Renderer、Renderer 编排 Backend"两层对应，每层只调下一层门面，不跨层。B（Window 直接调 Backend）跨两层破坏封装、C（Execute 内部包 Begin/End）未来帧级操作没位置，均否决 |
| 14 | 双缓冲归属 | A：**完全封装在 GDIBackend**（HDC/HBITMAP/Memory DC/BitBlt 全是实现细节，Window/Renderer 零感知）——决策 16/26/27 的统一点。换 D2D/OpenGL 后端 Renderer/Window/命令体系零改动，各后端用各自缓冲机制（操作粒度 Backend 接口的回报）。B（Renderer 管缓冲）违反公共层不碰 API 类型、C（Window 管缓冲）违反编排不参与实现，均否决 |
| 15 | 双缓冲资源创建时机 | B：**BeginFrame 懒创建 + 尺寸自检重建**（决策 26 重演，EnsureBackBuffer 模式）——WM_SIZE 零通知链路，首帧/Resize/复用三合一。A（构造创建）客户区尺寸不稳定、C（WM_SIZE 通知）让 Window 认识 Backend 的 Resize 概念，均否决 |
| 16 | 清屏白 | A：**GDIBackend::BeginFrame 隐式 FillRect 白**（Backend 建立绘制目标时的初始化行为，**不增加 ClearCommand、Root 不负责**）——白色背景是平台语义不是 Widget 命令（见 §12） |
| 17 | BeginPaint/EndPaint 处理 | A：**BeginFrame 获取帧 DC、EndFrame 释放**（严格成对）——`m_windowDC`/`m_ps` 是**帧状态**（BeginPaint 写入、EndFrame 消费、下帧覆盖，无需重置）。B（构造时 GetDC 长期保存）脱离 WM_PAINT 生命周期、C（每个 DrawRect 自取 DC）重复获取破坏帧语义，均否决 |
| 18 | 双缓冲资源析构 | A：**GDIBackend 析构统一释放**（DeleteObject(bitmap) + DeleteDC(memoryDC)），Resize 重建前先释放旧的（ReleaseBackBuffer 公共路径）。所有权随 Backend、Window 零参与。⚠️ 澄清：GDIBackend 无不完整类型成员，析构放 .cpp 是**风格一致性 + 释放逻辑集中**（非 Layout 那种编译必需）；B 每帧创建销毁与复用决策相反、C Window 释放破坏封装，均否决 |
| 19 | Resize 旧 Bitmap 处理 | A：**BeginFrame 发现尺寸变化立即重建**（决策 15 的必然推论）——当前帧从一开始就是正确尺寸，无"通知了但下帧才处理"的中间状态。B（帧末重建）当前帧 BitBlt 区域不匹配会花屏、C（只扩大不缩小）资源浪费且第一版无容量策略需求，均否决 |
| 20 | Memory DC / Bitmap 创建 | A：保存 `m_oldBitmap`（SelectObject 返回值），**释放前先恢复原选择再 DeleteObject**——GDI 铁律：不能删除 selected 对象。创建：CreateCompatibleDC → CreateCompatibleBitmap → SelectObject（保存 old）。释放（ReleaseBackBuffer）：SelectObject(old) → DeleteObject(bitmap) → DeleteDC。B 直接删 selected 对象（不干净且 DeleteObject 会失败）、C 每绘制 Select/恢复（无必要，bitmap 长期 selected），均否决 |
| 21 | Color→COLORREF 转换 | A：**转换完全封闭在 `GDIBackend::DrawRect()`**（决策 23 的落地：ToByte Clamp 在 Backend）。依赖：Color（公共，不认识 GDI）← Backend（消费）。B（Color::ToCOLORREF）Core 依赖 GDI、C（Renderer 转换）Renderer 认识 COLORREF，均否决。**Alpha 澄清**：`a` 是 Color 数据模型完整部分（数据保留），第一版 GDIBackend 暂不消费（功能范围限制 ≠ 数据模型缺失）——未来加 Alpha 只改 Backend，不动数据结构 |
| 22 | Color 分量范围 | A：**语义约定 [0.0f, 1.0f]**（与 FromRGBA8/标准色天然一致）。⚠️ 与决策 23 分工：语义约定（22）指导怎么写 + 数据层不强制（23 构造不 Clamp）+ Backend 兜底（23 ToByte Clamp）——双保险。B（[0,255] 浮点）语义退化、C（不规定）含义模糊，均否决 |
| 23 | 超出 [0,1] 的 Color | A：**Color 纯数据零约束；Backend 转换时 Clamp**（ToByte 辅助函数：`std::clamp(v,0,1)*255`）——数据层不承担约束，约束在消费边界（分层原则）。B（构造 Clamp）破坏聚合初始化、C（仅 Debug Assert）Release 下 Backend 仍收非法值，均否决 |
| 24 | DrawRect 画刷管理 | A：**每次 CreateSolidBrush → FillRect → DeleteObject**（无缓存）——长期资源（DC/Bitmap）复用、短期（Brush）即用即弃；无缓存状态无泄漏风险。B（Brush Cache）引入缓存/淘汰/生命周期管理第一版膨胀、C（当前 Brush 状态）Backend 引入状态，均否决 |
| 25 | Rect→RECT 转换 | A：**直接截断（`(LONG)x`），转换封闭在 GDIBackend**——公共 Rect 不认识 Win32 RECT（分层）；round/DPI/亚像素策略留待未来单独设计 |
| 26 | 双缓冲初始化时机 | A：**构造不持 hwnd（决策 35 默认构造），SetHwnd 后 BeginFrame 懒创建**（EnsureBackBuffer：首帧创建 + 尺寸自检重建）——不绘制不建资源，Resize 零通知链路（决策 15 的原始版） |
| 27 | 客户区尺寸获取 | **B**：BeginPaint 后 `GetClientRect`（完整客户区）；`ps.rcPaint` 不用（无效区域，当 Buffer 尺寸会每帧重建） |
| 28 | 局部更新（rcPaint 复用） | A：**第一版始终完整绘制 + 完整 BitBlt**（rcPaint 从头不被使用，决策 27+28 合并）——Clip/Dirty Region/Partial Redraw 是独立裁剪系统，§15 Out of Scope 已列，留待未来真实瓶颈出现再设计。B（rcPaint 裁剪）引入整套裁剪语义、C（只 BitBlt rcPaint）收益有限，均否决 |
| 29 | EndFrame 的 BitBlt 策略 | A：**完整 BitBlt（SRCCOPY）+ EndPaint**，无额外策略——与完整重绘模型一致。B（StretchBlt）为不存在的缩放需求引入拉伸语义（YAGNI）、C（只复制 rcPaint）重新引入刚排除的局部更新，均否决。⚠️ 设计原则写入 §15：**Phase4 不以极致渲染性能为目标，优先职责隔离/简单/可测/可扩展；性能优化仅采用不显著增加架构复杂度的措施**（双缓冲是符合此原则的少数例子） |
| 30 | Back Buffer 创建失败 | A：**FRAMEWORK_ASSERT 暴露（不可恢复框架错误），不引入错误传播/降级路径**。失败分级：框架级（Back Buffer 创建失败 → Assert）vs 局部（CreateSolidBrush 失败 → if(!brush) return 跳过，决策 24 补充）。B（bool 错误链 GDIBackend→Renderer→Window）第一版过重、C（降级直画双路径）复杂度增加，均否决。Release 下极端内存不足场景接受静默失败（不做处理） |
| 31 | 析构释放顺序 | A：**严格逆序（SelectObject(old) → DeleteObject(bitmap) → DeleteDC）**——GDI 依赖：Bitmap 必须先解除选择才能删。⚠️ 实现简化：析构直接调用 `ReleaseBackBuffer()`（决策 18/20 公共路径），析构/Resize 重建/空句柄防御三处共用一份释放逻辑（用户手写 if 链等价但重复）。B（先删 DC）顺序错、C（只删 DC）泄漏，均否决 |
| 32 | Frame 状态维护 | A：**GDIBackend 维护 `m_inFrame` + FRAMEWORK_ASSERT**（BeginFrame 断言 !inFrame 后置 true；EndFrame 断言 inFrame 后置 false）——重复 Begin/End 开发期直接暴露。`m_inFrame` 是 GDIBackend 实现细节，不污染 RenderingBackend 接口。B（不检查）错误变难定位的 GDI 状态问题、C（Frame 对象包装）改变已定接口形态，均否决。Release 下断言随 FRAMEWORK_ASSERT 编译掉（开发期工具，与决策 30 一致） |
| 33 | Begin/Execute/End 配对 | A：**严格成对，Renderer 不负责恢复**——内部错误按 FRAMEWORK_ASSERT 程序错误模型处理，不设计恢复机制。与决策 32 合成完整契约：Backend 检查状态（32）+ 调用方严格配对（33）+ 断言暴露错误。B（Renderer 兜底主动 EndFrame）违反职责单一（Renderer 不该管 Backend 生命周期恢复）、C（RAII Frame Guard）引入新接口层次第一版无需求，均否决 |
| 34 | Renderer 与 Backend 依赖方式（最终裁决，推翻决策 10） | A：**Renderer 持 `RenderingBackend&` 引用**——`explicit Renderer(RenderingBackend& backend);` + `private: RenderingBackend& m_backend;`。理由：所有权归 Window（决策 35）、Renderer 只依赖不拥有（保持职责边界）、引用成员可前向声明（无 C2027 风险，调用在 .cpp include 完整类型）、测试模式更简单（RecordingBackend 栈对象直接观察，决策 12 修订）。⚠️ 副作用：引用成员 → Renderer 不可默认构造、复制赋值隐式删除（引用不可重绑定）——框架无此需求（Window 禁复制禁移动、Renderer 生命周期随 Window），接受。B（保持 unique_ptr）所有权在 Renderer、测试需 get() 裸指针、析构链多一层，C（裸指针）无所有权语义，均否决 |
| 35 | Backend 在哪里创建、由谁持有 | A：**Window 创建并持有**——`GDIBackend m_backend;`（值成员，默认构造）+ `Renderer m_renderer;`（值成员，初始化列表 `m_renderer(m_backend)` 绑定引用）。B（Application 创建）一个 Backend 对应一个 Window 时无收益、多窗口 HWND 管理反而复杂、Window 无法表达"我的绘制后端是谁"，C（Renderer 创建）Renderer 认识具体 Backend 类型/承担工厂职责，均否决。⚠️ **初始化顺序坑（最高优先级实现注意）**：`m_handle` 在初始化列表阶段是 `nullptr`（CreateWindowExW 在构造体内执行），用户示例的 `m_backend(m_hwnd)` 实际拿不到 hwnd → **GDIBackend 必须两阶段**：默认构造（hwnd 暂空）+ 构造体内 `m_backend.SetHwnd(m_handle)`（CreateWindowExW 成功检查之后调用）。与决策 15/26 懒创建一致：真正需要 hwnd 的操作（GetClientRect / CreateCompatibleDC）都在 BeginFrame，那时 hwnd 早已就绪。⚠️ **成员声明顺序**：`m_backend` 声明在 `m_renderer` **之前**——初始化按声明序（m_backend 先构造供 m_renderer 绑定引用），析构按声明逆序（m_renderer 先析构、m_backend 后析构，引用不悬垂）。⚠️ **代价**：GDIBackend 是具体类型值成员，Window **无法注入假 Backend 做集成测试**——渲染行为由两层独立测试覆盖（第一层 Widget→命令 / 第二层 Renderer+RecordingBackend，见 §16），Window+GDIBackend 集成走真实窗口验证（§17 Acceptance Criteria） |
| 36 | Renderer::Execute 分发方式（决策 9 重演确认） | A：**`std::visit`** + `ExecuteCommand` 重载集——`for (const auto& c : commands) std::visit([this](const auto& cmd){ ExecuteCommand(cmd); }, c);`。与决策 9 结论完全一致（详细设计阶段重新提问，锁定不翻案）。**关键性质**：Renderer 不维护"variant 里有哪些类型"的控制流程——命令类型是类型系统的一部分，未来加 DrawText/DrawImage 只需新增 ExecuteCommand 重载，忘加 = 编译错误（visit 穷尽性保证），无需回头改 Execute。B（`std::get<DrawRectCommand>`）第一版最简但未来加命令要重写 Execute 分发、C（type 字段 + switch）回到已否决的"Type+字段混合"设计，均否决 |
| 37 | DrawRectCommand 具体结构（决策 2 重演确认） | A：**`struct DrawRectCommand { Rect rect; Color color; };`**（直接复用公共类型，零字段搬运）+ `PaintContext::DrawRect` 用 `m_commands.emplace_back(DrawRectCommand{ rect, color })` 入缓冲。与决策 2 结论完全一致（重演锁定）。**关键性质**：纯死数据、无渲染层专用转换；公共 Rect/Color 被 Widget/Layout/Paint/Renderer 共同使用，不重复拆分。B（拆成 x/y/w/h/r/g/b/a 标量字段）把公共结构复制一遍无收益、C（存 Widget*）执行时回查 Widget 破坏"命令是死数据、Renderer/Backend 不认识 Widget"，均否决 |
| 38 | Back Buffer 尺寸变化重建顺序 | A：**先创建新资源 → 全部成功后替换成员 → 最后释放旧资源**（事务性替换，决策 15/19/26 重建体系的顺序细化）。BeginFrame 发现尺寸不匹配时：① 创建新 Memory DC + Bitmap 并 SelectObject（新 DC 也保存自己的 old 选择，统一释放路径）→ ② 全部成功后替换成员（m_memoryDC / m_bitmap / m_oldBitmap / m_bitmapWidth·Height）→ ③ 释放旧资源（走决策 20/31 逆序：SelectObject(old) → DeleteObject(bitmap) → DeleteDC）。**实现原则**：尺寸变化时不修改当前资源，直到新 Back Buffer 完整创建成功——创建失败时旧资源仍在（与决策 30 的 assert/Release 静默失败模型兼容），杜绝"先删后用"的中间态。B（先释放旧再创建）失败即无 Buffer，无必要冒险、C（固定最大尺寸）需猜尺寸 + 浪费 GDI 内存，均否决 |
| 39 | WM_PAINT → PaintFrame 访问路径 | A：**WindowProc 只做 HWND↔Window 路由（删除 Phase3 的 WM_PAINT 特判，§13 迁移项）→ `HandleMessage` 加 `case WM_PAINT: PaintFrame(); return 0;`**（与 WM_DESTROY/WM_SIZE 状态同步 switch 并列）。`PaintFrame()` 私有方法负责完整编排：clear → PaintContext → rootWidget->Paint → BeginFrame → Execute → EndFrame（§10；✅ 决策 41 改名，原 OnPaint）。**WM_PAINT 不进 m_messageHandler 翻译器**（绘制不是 Event，直接 return 0）。HandleMessage 保持薄：只转发不实现绘制逻辑。B（绘制逻辑直接写进 HandleMessage 分支）消息处理函数变胖、C（Application 处理 WM_PAINT）绘制生命周期从 Window 转移，违反"Window 管绘制生命周期"，均否决 |
| 40 | namespace ECDI | A：**从现在开始全部引入 `namespace ECDI`，旧文件一并迁移**（2026-08-10 拍板，推翻"转库时再包"旧规划、提前落地）——所有 .h/.cpp 包 namespace；main.cpp 使用处加 `ECDI::` 前缀或 using；独立 Commit 4.0（§21）。理由：转库已确定要做、现在文件少迁移成本低、新 Render 文件天然在 namespace 内、避免"新旧不一致"过渡态 |
| 41 | Window 编排函数命名 | A：**`Window::OnPaint()` 改名 `PaintFrame()`**（GPT 终审建议采纳）——语义准确（编排整帧 vs `Widget::OnPaint(PaintContext&, int, int)` 画自己），顺带消除 §19-10 撞名。同步改 §2/§10/决策 39/§19-10 |
| 42 | PaintContext 形态 | A：**class + 私有成员**（GPT 终审建议采纳）——`explicit PaintContext(CommandBuffer&)` + `private: CommandBuffer& m_commands;`。强化决策 8"完全封装"（只暴露绘制方法）；未来加 dpiScale 等状态不动公开结构；与项目类风格一致。原 §7 struct + 公共 members 形态已修订 |

---

## 19. 实现注意事项与坑（评审中发现，实现必看）

1. **⚠️ OnPaint 不能用 GetRect()（最高优先级）**：`GetRect()` 返回局部坐标（相对父），OnPaint 必须用基类传入的**最终坐标 x/y** + `GetWidth()/GetHeight()` 画自己。Phase3→Phase4 迁移最容易写错的点（详细设计问题 5/6 评审指出）。

2. **⚠️ Paint 签名必须保留 offset 参数**：`Paint(PaintContext&, int offsetX, int offsetY)`——没有 offset 参数算不出最终坐标；禁止"PaintContext 存偏移"（违反 §7 零坐标逻辑：收到的坐标是最终坐标，原样进命令）或"回溯 parent 链"（低效偏离 Phase3 模型）。

3. **`unique_ptr<不完整类型>` 成员 → 构造/析构移 .cpp**：C2027 教训（Phase3 Layout 已踩）。**项目通用规范**：凡类拥有 `unique_ptr<前向声明类型>` 成员，构造/析构在 .cpp 定义并 include 完整类型。⚠️ 决策 34 修订：Renderer 改持**引用**后不再触发此规则（引用成员可前向声明，调用处 include 完整类型即可）。⚠️ **反向注意（决策 35）**：**值成员必须完整类型**——`GDIBackend m_backend;` 是 Window 值成员，Window.h 必须 include GDIBackend.h（不能前向声明）；`Renderer m_renderer;` 值成员同理（Window.h include Renderer.h；Renderer.h 内部对 RenderingBackend 只需前向声明）。

4. **⚠️ rcPaint 不能当 Back Buffer 尺寸**（决策 27）：rcPaint 是无效区域（部分重绘时大小不定）→ 当 Buffer 尺寸会**每帧重建 Bitmap**（性能灾难 + 闪烁）。必须 GetClientRect 拿完整客户区。

5. **EnsureBackBuffer 模式**（决策 26）：懒创建 + 尺寸自检（`m_bitmapWidth/Height` vs GetClientRect）一次处理首帧和 Resize——**不需要 Window 通知重建**，每次 BeginFrame 自动检查。

6. **RECT 开区间**（决策 25 Rect→RECT 转换实现）：`right = x + width`（exclusive），正好画满 `[x, x+width)`。

7. **CreateSolidBrush 失败防御**（可选）：返回 NULL，`FillRect` 静默失败——加 `if (!brush) return;` 一行。

8. **GDI 对象泄漏**（决策 18/20/31 实现）：GDIBackend 析构必须 `DeleteObject(bitmap)` + `DeleteDC(memoryDC)`（走 ReleaseBackBuffer 公共路径）；HBRUSH 每次配对 DeleteObject（决策 24）。GDI 对象是系统资源（10,000 上限）。

9. **GDIBackend 成员清单**：`HWND m_hwnd`（✅ 决策 35：**默认构造为空、构造体内 `SetHwnd()` 设置**——不是构造参数）、`PAINTSTRUCT m_ps`（BeginPaint/EndPaint 配对用）、`HDC m_windowDC`（BeginPaint 返回值，EndFrame BitBlt 目标）、`HDC m_memoryDC`、`HBITMAP m_bitmap`、`int m_bitmapWidth/Height`（尺寸自检）。

10. **OnPaint 命名撞名（✅ 已解决：决策 41）**：~~`Window::OnPaint()`~~ 已改名 **`Window::PaintFrame()`**（私有、无参，编排整帧 clear→Paint→BeginFrame→Execute→EndFrame），与 `Widget::OnPaint(PaintContext&, int x, int y)`（虚方法、画自己）不再冲突。实现时注意：**Widget 的绘制虚方法叫 OnPaint，Window 的帧编排叫 PaintFrame**——两者职责完全不同。

---

## 20. Header 依赖规则（编码必读）

> 决策 34/35 + 决策 40 的落地。**值成员必须完整类型；引用成员可前向声明。**

### RenderingBackend.h

include `ECDI/Core/Rect.h` + `ECDI/Core/Color.h`（接口参数是公共类型）；纯抽象类，零依赖：

```cpp
#pragma once
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Color.h"

namespace ECDI {

class RenderingBackend {
public:
	virtual ~RenderingBackend() = default;
	virtual void BeginFrame() = 0;
	virtual void DrawRect(const Rect& rect, const Color& color) = 0;
	virtual void EndFrame() = 0;
};

}
```

### Renderer.h

**不 include RenderingBackend.h**，前置声明（决策 34：引用成员可前向声明）：

```cpp
#pragma once

namespace ECDI {

class RenderingBackend;

class Renderer {
public:
	explicit Renderer(RenderingBackend& backend);   // 定义在 .cpp（风格一致性）
private:
	RenderingBackend& m_backend;
};

}
```

### Renderer.cpp

`#include "ECDI/Render/RenderingBackend.h"`——`m_backend.DrawRect()` 需要完整类型。

### Window.h

**值成员必须完整类型**（决策 35/§19-3 反向规则）：

```cpp
#include "ECDI/Render/GDIBackend.h"
#include "ECDI/Render/Renderer.h"
// 不能 class GDIBackend; 前向声明——值成员 incomplete type 编译错误
```

### 通用规则

- 值成员 → include 完整类型；引用/指针成员 → 可前向声明
- `unique_ptr<前向声明类型>` 成员 → 构造/析构移 .cpp（C2027，Layout 教训）

---

## 21. 实现顺序（Commit 4.0 - 4.8）

> GPT 终审建议采纳。原则：**平台相关（GDI）最后接**，先用 RecordingBackend 验证 命令→Renderer→Backend 链路。

| Commit | 内容 | 验证 |
|--------|------|------|
| 4.0 | **namespace ECDI 全项目迁移**（决策 40：所有 .h/.cpp 包 namespace；main.cpp 使用处加 `ECDI::` 前缀或 using） | 编译（三工具链） |
| 4.1 | Core：Rect / Point / Color（float + FromRGBA8 + 标准色）上移 Core/ | 编译 |
| 4.2 | RenderingBackend 空接口（namespace 内） | 编译 |
| 4.3 | RenderCommand：DrawRectCommand + variant + CommandBuffer | 编译 |
| 4.4 | PaintContext（决策 42：class + 私有 m_commands） | 编译 |
| 4.5 | Renderer（决策 36：std::visit）+ RecordingBackend | **双层测试**：Command→Renderer→Recording 断言（不碰 GDI） |
| 4.6 | Widget 迁移：Paint(HDC)→Paint(PaintContext&, offsetX, offsetY)；OnPaint 签名（决策 6） | 编译 + 命令层测试 |
| 4.7 | GDIBackend 基础绘制（决策 35：默认构造 + SetHwnd） | 真实窗口无缓冲绘制 |
| 4.8 | 双缓冲（决策 38 先建后替 + 决策 15/26 懒创建） | 真实窗口 + 无闪烁验收（§17） |
