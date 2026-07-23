# ECDI(Everyone Can Do It Framework) 开发规范（V0.1）

## 目标

构建一个工程化、可维护、可扩展的 C++ Win32 Framework，并最终演化为一个 2D 游戏引擎 Demo。

---

## 一、总体开发原则

### ① 明确阶段目标

项目不是一步到位，按照阶段推进：

```
阶段一：能创建窗口 → 管理窗口生命周期 → 管理素材资源

未来：Renderer → Scene → 2D Engine Demo
```

### ② 小步开发（Small Step）

每次只实现一个小功能，例如：

```
WindowClass → 能注册 → 测试 → 再继续
```

而不是一次写几千行代码。

### ③ 小步重构（Incremental Refactoring）

先实现，再重构，流程固定：

```
新增功能 → 测试 → 替换旧代码 → 再次测试 → 删除旧代码
```

任何时候，项目都必须：
- 能编译
- 能运行

### ④ 不过度设计（Avoid Over Engineering）

不为了未来十年的需求设计今天的代码。

只解决当前阶段真正存在的问题。

---

## 二、Framework 设计原则

### ① Framework 提供能力

Framework 负责提供各种能力，例如：
- Window
- Renderer
- Logger
- Input

### ② Application 制定策略

Framework 支持多个 Window，Application 决定只创建一个。

不要反过来。

### ③ 默认简单，按需开放

普通用户应该能够：

```cpp
Application app;
app.Create(...);
app.Run();
```

高级用户仍然能够自定义更多内容。

### ④ 隐藏实现，保留能力

用户不需要知道 `WindowClass`，但 Framework 内部仍然存在 `WindowClass`。

---

## 三、类设计原则

### ① 单一职责（SRP）

一个类负责一件事情，例如：

```
Window → 负责 HWND 生命周期
```

而不是：

```
Window → 注册 + 创建 + 消息循环 + 日志 + 渲染全部放一起
```

### ② 高内聚，低耦合

**正确原则：**
- **高内聚（High Cohesion）**：一个类内部功能紧密相关
- **低耦合（Low Coupling）**：类与类之间依赖尽量少

例如：

```
Application → Window （合理依赖）
```

而不是：

```
Window → Renderer + Logger + Input + ResourceManager （不合理依赖）
```

### ③ 生命周期属于拥有者

谁拥有资源，谁负责释放。例如：

```
Window 拥有 HWND → Window 负责释放 HWND
```

### ④ 一个资源只能有一个拥有者

**资源类默认 Move Only**：禁用复制，允许移动。

```cpp
// 禁止复制
Window(const Window&) = delete;
Window& operator=(const Window&) = delete;

// 允许移动
Window(Window&& other) noexcept;
Window& operator=(Window&& other) noexcept;
```

**例外：若资源与对象地址绑定，则禁止移动。**

`Window` 的 `HWND` 通过 `GWLP_USERDATA` 与 C++ 对象地址绑定，移动对象会破坏句柄与对象的对应关系，并丢失 `m_application` 指针，导致消息分发崩溃。因此 `Window` 同时禁用复制和移动，仅通过 `Application` 持有的 `unique_ptr` 管理生命周期：

```cpp
// 禁止复制
Window(const Window&) = delete;
Window& operator=(const Window&) = delete;

// 同时禁止移动（资源与对象地址绑定）
Window(Window&&) = delete;
Window& operator=(Window&&) = delete;
```

---

## 四、接口设计原则

### ① 抽象类规范

所有抽象类遵循以下形式：

```cpp
class XXX
{
public:

    virtual ~XXX() = default;
};
```

- 析构函数声明为 `virtual` 且 `= default`
- 公共接口方法声明为纯虚函数（`= 0`）

### ② Getter 规范

所有 Getter 统一使用 `GetXxx()` 形式，并标记 `const`：

```cpp
const std::wstring& GetName() const;
HWND GetHandle() const;
```

### ③ 命名规范

- 类名：`PascalCase`（`Window`、`Application`）
- 方法名：`PascalCase`（`Create`、`Run`、`GetWindowClass`）
- 成员变量：`m_` 前缀 + `camelCase`（`m_handle`、`m_windowClass`）
- 枚举值：`PascalCase`（`LogLevel::Trace`）

### ④ 基类不为派生类加成员

基类永远不要为了某一个派生类去增加成员。基类只包含所有派生类共享的接口与数据。

---

## 五、资源管理规范（RAII）

### ① 提供主动释放接口

```cpp
bool Release();
```

### ② 析构函数负责兜底

```cpp
~Window()
{
    Release();
}
```

用户忘记释放时，Framework 自动释放。

### ③ Release() 可重复调用

多次调用 `Release()` 都必须安全。

### ④ 析构函数不得抛异常

失败时记录日志，绝不能 `throw`。

### ⑤ 释放失败不自动重试

Framework 尝试一次，失败则记录日志并返回失败。

### ⑥ 对象始终保持可析构

无论创建成功还是创建失败，析构都必须安全。

### ⑦ Framework 不伪造资源状态

Framework 的状态应尽可能与底层资源状态一致，不为了"看起来成功"而修改资源状态。

---

## 六、Event 设计原则

### ① Event 的唯一职责是表示已发生的事实

Event 表示一件**已经发生的事实**，并携带描述该事实所需的数据。

Event 不负责：

- 创建
- 分发
- 监听
- 处理
- 管理生命周期

这些职责由其他模块承担。Event 只是数据的载体。

### ② Event 尽量保持轻量

- 只携带描述事实所需的最小数据集
- 不持有资源所有权
- 优先值语义

---

## 七、代码规范

### ① 使用现代 C++

统一使用 `nullptr`，不用 `NULL`。

### ② 优先使用 const 引用

例如：`const std::wstring&`，减少复制。

### ③ 使用成员初始化列表

```cpp
Application::Application()
    : m_windowClass(...)
{
}
```

### ④ 删除复制，支持移动

对于资源类：
- 复制构造函数：`= delete`
- 移动构造函数：实现 Move Constructor
- 移动赋值运算符：实现 Move Assignment
