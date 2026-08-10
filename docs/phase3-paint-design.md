# Paint 详细设计（phase3-paint-design.md）

> 阶段：第三阶段 Widget System → Paint 子模块
> 前置：职责确认 + 初步设计 + 详细设计（v1.2）已评审通过
> **状态：已实现（2026-08-07，随 Widget System MVP 提交 `a11cb9b`）**
> 修订：v1.1 修正 HDC 前向声明漏星号；v1.2 修正 `BLUE_BRUSH`（不存在于 Win32，改用 CreateSolidBrush）

---

## 0. 目标与定位

Phase3 最后一块拼图：**验证 WidgetTree → 像素链路成立**。

```
WM_PAINT → Window → RootWidget.Paint() → Widget Tree Traversal → Widget::OnPaint() → GDI → 屏幕
```

明确**不引入 Renderer 抽象**——Renderer / RenderCommand / D2D / GL 属于 Phase4。本阶段用 GDI 直接画（Debug Drawing，验证状态机），Phase3 允许 Win32 GDI 类型进入 Paint 接口，作为**临时桥梁**，Phase4 替换。

## 1. 架构决策（职责确认 + 初步设计定稿）

### 1.1 与既有系统的统一模式

| 系统 | Public 驱动 | Internal / Hook |
|------|------------|-----------------|
| Event | Dispatch | Widget::OnMouseXXX |
| Layout | Arrange | ArrangeInternal |
| **Paint** | **Paint** | **OnPaint** |

不另起炉灶——遍历控制放基类，子类只负责"画什么"。

### 1.2 接口

```cpp
// Widget.h
struct HDC__;              // 前向声明（HDC = HDC__*，指针参数无需完整类型）
using HDC = HDC__*;        // ★ 必须带星号！漏写会与 Windows SDK 的
                           //   typedef struct HDC__* HDC 冲突（C 重定义）

public:
    void Paint(HDC hdc, int offsetX, int offsetY);   // public 驱动，约定从 RootWidget 调

protected:
    virtual void OnPaint(HDC hdc, int x, int y);     // 子类钩子，默认空
```

- Widget.h **不 include Windows.h**（框架核心头不被 Win32 污染，宏污染/编译时间/平台耦合）
- 只有真正调用 GDI 的 cpp（Panel.cpp / Button.cpp / Window.cpp）才 include Windows.h
- 实测：`using HDC = HDC__*;` 与 Windows.h 的 `DECLARE_HANDLE(HDC)` 双 include 顺序均编译通过（C++11 起同类型重复声明合法）

### 1.3 Paint 流程（Widget.cpp）

```cpp
void Widget::Paint(HDC hdc, int offsetX, int offsetY) {

	if (!IsVisible())          // 可见性：父隐藏 → 子树全不画（与 HitTest 一致）
		return;

	int x = offsetX + m_geometry.x;   // 绝对坐标 = 父绝对 + 子局部（offset 累加）
	int y = offsetY + m_geometry.y;

	OnPaint(hdc, x, y);               // 画自己（子类钩子）

	for (auto& child : m_children) {  // children 正序（后添加 = 上层，最后画覆盖）
		child->Paint(hdc, x, y);
	}
}
```

### 1.4 关键规则

| 规则 | 结论 |
|------|------|
| 坐标 | offset 累加：父绝对坐标 + 子局部坐标；与 HitTest 的逐层减偏移**镜像** |
| Visibility | `IsVisible()` 入口判断；父隐藏 → 子树全不绘制（树级继承） |
| Z-Order | Paint 正序（index 0 先画，后添加覆盖）；HitTest 反序（后添加先命中）——两者对应"后添加 = 上层" |
| 绘制顺序 | Parent → Self（OnPaint）→ Children（父背景先画，子覆盖） |
| Clip | 不实现——子 Widget 可画出父边界/客户区外，**已知行为**，靠开发者自觉 |

### 1.5 WM_PAINT 入口（Window.cpp）

- **不进 EventSystem**：WM_PAINT 是系统重绘请求，不是用户事件（区别于 WM_CLOSE/鼠标/键盘）。与 WM_NCCREATE 同款特判，留在 `Window::WindowProc`
- 位置：`if (window)` 块内、`HandleMessage` 调用之前

```cpp
if (msg == WM_PAINT) {

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);

	// 客户区白底（Root 背景归 Window 层，见 §1.6）
	FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(WHITE_BRUSH));

	window->m_rootWidget->Paint(hdc, 0, 0);   // 从根进（唯一入口原则）

	EndPaint(hwnd, &ps);
	return 0;
}
```

### 1.6 Root 背景（归 Window 层）

- `WNDCLASSW wc{}` 零初始化 → `hbrBackground = nullptr`（窗口类未设背景画刷，系统不擦背景）
- Root 是**普通 Widget 实例**（无 RootWidget 类），无法 override OnPaint 画背景
- 因此客户区背景由 **Window 层在 WM_PAINT 里 FillRect 白**——职责划分：Window 管背景，Widget 管自身内容

### 1.7 颜色（硬编码，不引入 Theme）

第一版颜色**不存入 Widget 状态**（引入 Color 成员 = 滑向 Theme/Style 系统，Phase4 才做）。各 Widget 在 OnPaint 内临时创建画刷：

| Widget | 颜色 | 实现 |
|--------|------|------|
| Panel | `RGB(180,180,180)` 灰 | `CreateSolidBrush` + `FillRect` + `DeleteObject` |
| Button | `RGB(80,120,220)` 蓝 | 同上 |
| Widget（基类） | — | OnPaint 默认空 |
| Root 背景 | 白 | Window 层 `GetStockObject(WHITE_BRUSH)` |

**坑**：`BLUE_BRUSH` **不存在于 Win32 SDK**（WinGDI.h 只有 WHITE/LTGRAY/GRAY/DKGRAY/BLACK/NULL/DC_BRUSH，BLUE/RED/GREEN 是 MFC 扩展）。必须用 `CreateSolidBrush(RGB(...))` 或可染色的 `DC_BRUSH` + `SetDCBrushColor`。`LTGRAY_BRUSH` 是标准库存画刷，Panel 可直接用。

画刷生命周期：OnPaint 内临时 Create/Delete，**不缓存**（static 缓存有全局析构顺序问题，MVP 不需要）。`FillRect` 内部临时 SelectObject 并在返回前恢复，所以 Create → FillRect → Delete 的顺序安全。

## 2. 实现清单（已落地，6 文件）

| 文件 | 改动 |
|------|------|
| `Widget.h` | `struct HDC__; using HDC = HDC__*;` + `Paint`（public）+ `OnPaint`（protected 虚） |
| `Widget.cpp` | `Paint` 递归实现 + `OnPaint` 空默认（**不需要 include Windows.h**，HDC 前向声明足够） |
| `Panel.h/cpp` | `OnPaint` override（protected 区，灰色矩形）——Panel.h 需新增 protected 区 |
| `Button.h/cpp` | `OnPaint` override（蓝色矩形）——Button.h 已有 protected 区 |
| `Window.cpp` | WM_PAINT 特判（BeginPaint → FillRect 白 → root.Paint → EndPaint） |

## 3. 不做（Phase4 范围）

❌ Renderer 抽象 / RenderCommand / Direct2D / OpenGL / GPU
❌ Clip（已知限制）
❌ Dirty Rectangle / 局部刷新（WM_PAINT 全树重绘，天然无残影）
❌ Text 绘制 / 字体系统（Label 第一版不绘制，文本归 Phase4）
❌ Theme / Style / Animation / Layer

## 4. 测试计划（main.cpp 已含，T1~T5）

| 用例 | 结构 | 预期 |
|------|------|------|
| T1+T2 基础绘制 + 坐标转换 | `Panel(50,50, 200x100)` 内嵌 `Button(20,20, 100x50)` | 灰 Panel 在屏幕 (50,50)，蓝 Button 在 **(70,70)**（offset 累加 50+20）——验证递归坐标 |
| T3 Z-Order | `A(270,10)` + `B(310,40)` 部分重叠，B 后添加 | 重叠区显示 B（后添加在上层） |
| T4 Visibility | `hiddenButton(350,250)` 设 `SetVisible(false)` | 该位置不出现（可见性失效则会显示 = 失败） |
| T5 Resize | 手动拖窗口 | 无残影（全量重绘天然通过） |
