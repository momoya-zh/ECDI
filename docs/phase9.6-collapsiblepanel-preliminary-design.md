# Phase 9.6 CollapsiblePanel 初步设计（v1.1）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-08-30
> 状态：待用户评审（v1.1：四向折叠 + 内容容器）

## 1. 目标与定位

`CollapsiblePanel`：继承 `Panel` 的可折叠内容容器。**不含 header**（外部自组），**四向折叠**（Down/Up/Left/Right），折叠时内容整体隐藏 + 跳过命中，沿锚定边 resize 收缩到 0；展开时沿对侧边恢复展开尺寸。复用 9.6 `AnimationManager`（per-Window 组合）。

## 2. 方向模型（核心）

**语义**：header 锚定边固定不动，内容向对侧 resize 展开/收缩。

| 方向 | header 位置 | 动画轴 | 锚定边 | 对侧边行为 |
|------|-------------|--------|--------|------------|
| `Down` | 上 | 高度 h | top（y 固定） | 底边向上收缩 |
| `Up` | 下 | 高度 h | bottom（y+h 固定） | 顶边向下收缩 |
| `Right` | 左 | 宽度 w | left（x 固定） | 右边向左收缩 |
| `Left` | 右 | 宽度 w | right（x+w 固定） | 左边向右收缩 |

**几何推导**：动画只驱动「尺寸轴值 s」（h 或 w），位置是 s 的纯函数（锚定边固定）：

```
Down:  pos(x0,  y0)              size(w0, s)          // top 锚定，y 恒定
Up:    pos(x0,  y0 + H - s)      size(w0, s)          // bottom 锚定，y 随 s 推导
Right: pos(x0,  y0)              size(s,  h0)         // left 锚定，x 恒定
Left:  pos(x0 + W - s,  y0)      size(s,  h0)         // right 锚定，x 随 s 推导
```

其中 `m_expandedRect = (x0, y0, w0, h0)` 为**展开态基准几何**（折叠触发时记忆当前完整几何）；折叠时 s: 当前→0，展开时 s: 当前→H/W。**单 token 即可**——位置与尺寸同帧由一个动画值推导，无需两个动画。

## 3. 接口设计（草案）

```cpp
// ECDI/include/ECDI/Widget/CollapsiblePanel.h

/// @brief 展开方向（内容向对侧展开；header 锚定在反侧）
enum class ExpandDirection{
    Down,    ///< 内容向下展开（header 在上；默认）
    Up,      ///< 内容向上展开（header 在下）
    Right,   ///< 内容向右展开（header 在左）
    Left,    ///< 内容向左展开（header 在右）
};

/// @brief 可折叠面板（9.6 S2 demo 产品化升格）
/// @details 继承 Panel；四向折叠：沿锚定边 resize 收缩到 0 + 内容容器 SetVisible(false)
/// （visible 机制双跳过 Paint/HitTest——Widget 基类已有，零新增）。
/// header 外部自组：用户把 header 放在面板外，方向 = 内容展开方向。
class CollapsiblePanel: public Panel{
public:

    CollapsiblePanel();

    ~CollapsiblePanel() override = default;

    /// @brief 设置展开方向（默认 Down）
    /// @note 建议在首次折叠前设置；折叠动画中切换方向 = 替换式重启（from = 当前呈现值）
    void SetExpandDirection(ExpandDirection dir) noexcept{ m_direction = dir; }

    /// @brief 当前展开方向
    [[nodiscard]] ExpandDirection GetExpandDirection() const noexcept{ return m_direction; }

    /// @brief 设置展开状态（false = 折叠：沿锚定边收缩到 0，动画结束隐藏内容）
    /// @details 幂等（同状态重复调用无副作用）；无 Window（未挂树）时降级为瞬时切换（无动画）——测试可测性
    void SetExpanded(bool expanded);

    /// @brief 展开/折叠切换
    void Toggle();

    /// @brief 是否展开（默认 true——初始展开）
    [[nodiscard]] bool IsExpanded() const noexcept{ return m_expanded; }

    /// @brief 获取内容容器（裸 Widget——不画背景；用户内容统一 AddChild 进此容器）
    /// @details 容器铺满面板（尺寸随折叠动画同步）；内容坐标相对容器，不受折叠影响。
    /// 内容布局（Layout 或手动摆位）由用户自理。
    [[nodiscard]] Widget* GetContent() noexcept{ return m_content; }

private:

    /// @brief 动画回调：按方向推导位置 + 尺寸（单动画值驱动），同步内容容器
    void ApplyGeometry(float s);

    /// @brief 内容容器可见性统一切换（折叠完成隐藏 / 展开开始前显示）
    void SetContentVisible(bool visible);

    static constexpr int kToggleDurationMs = 200;   ///< 尺寸过渡时长（同 demo）

    ExpandDirection m_direction = ExpandDirection::Down;	///< 展开方向

    bool m_expanded = true;             ///< 展开状态（触发点翻转——动画不产生状态）

    Rect m_expandedRect{};              ///< 展开态基准几何（折叠触发时记忆完整 rect）

    Widget* m_content = nullptr;        ///< 内容容器（裸 Widget，树拥有所有权，非拥有指针）

    AnimationToken m_sizeToken;         ///< 尺寸动画令牌（RAII；析构自动标脏）

};
```

## 4. 状态机与折叠语义

### 4.1 状态与几何

```
m_expanded == true  → 几何 = m_expandedRect（动画目标）
m_expanded == false → 尺寸轴 s = 0（动画目标），内容容器 invisible
```

- **展开基准**：折叠触发时 `m_expandedRect = GetGeometry()`（记忆完整 rect——含锚定边坐标）。展开动画目标 = 该 rect。
- **替换式重启**：动画中再次 `Toggle` → token 替换键静默移除旧动画，`from` = 当前呈现值（`GetHeight()` 或 `GetWidth()` 按轴）——与 demo 同构（无跳变）。
- **边界（v0.1 已知限制）**：折叠态对面板 `SetPosition/SetSize` 不同步展开基准（展开回记忆 rect）。

### 4.2 内容可见性时序

| 动作 | 时序 |
|------|------|
| 折叠 | 先启动动画 `s: 当前→0`；**动画 onFinished 后** `SetContentVisible(false)` |
| 展开 | 先 `SetContentVisible(true)` + Invalidate；再启动动画 `s: 当前→H/W` |

- **折叠为何结束后才隐藏**：动画期间尺寸渐缩、内容被父 Clip（R1）天然裁切，视觉自然；尺寸到 0 后内容已不可见，此时设 invisible 只用于**断开命中**。
- **展开为何先显示**：动画期间尺寸渐增，内容需立即可见（否则表现为空面板长高、结束突现）。

## 5. 内容容器

- **为何引入**：四向锚定需要统一载体——① 折叠/展开时容器尺寸跟随面板（铺满）；② 隐藏只对一个容器 `SetVisible(false)`（子树整体跳过 Paint/HitTest）；③ 用户内容坐标相对容器，与折叠几何解耦。
- **为何是裸 Widget 而非 Panel**：Panel 会画背景（`PanelStyle.background` 默认 Gray），与 CollapsiblePanel 自身背景叠加成两层灰；裸 Widget `OnPaint` 空实现——内容区背景由面板统一呈现。
- **用户用法**：
  ```cpp
  auto panel = std::make_unique<CollapsiblePanel>();
  panel->SetSize(300, 220);
  panel->SetExpandDirection(ExpandDirection::Up);   // 例：header 在下方
  panel->GetContent()->AddChild(std::make_unique<Button>("OK"));  // 内容进容器
  ```
- 容器位置恒为 (0,0)（面板局部坐标），尺寸 = 面板尺寸（`ApplyGeometry` 内同步 `m_content->SetSize`）。

## 6. 动画集成

- 依赖：`Window::GetAnimationManager()`（9.6 per-Window 组合；demo 已验证同一模式）。
- **无窗口降级**：`GetWindow() == nullptr`（未挂树 / 测试）→ 瞬时 `ApplyGeometry(0 或 H/W)` + `SetContentVisible` + `Invalidate`，不启动动画。
  - 意义：① 测试可在无 Window 场景验证全部控件契约（状态/几何/可见性/命中），不依赖平台替身；② 动画时序本身已由 AnimationTests 12 用例独立覆盖，控件测试不重复。
- easing：折叠 `EaseIn`、展开 `EaseOut`（同 demo）；duration 200ms 常量。

## 7. 与 demo 的差异

| 维度 | CollapsiblePanelDemo（9.6 S2） | CollapsiblePanel（正式） |
|------|-------------------------------|--------------------------|
| header | 内置标题按钮（面板内） | **外部自组**（面板内无） |
| 折叠目标 | kHeaderHeight=40（保留按钮） | **0**（完全收起） |
| 方向 | 仅高度（固定向下展开） | **四向**（Down/Up/Left/Right） |
| 内容 | 固定 Label（demo 专用） | **内容容器 m_content**（裸 Widget，用户 AddChild） |
| 可见性 | 不处理 | `SetContentVisible`（visible 双跳过） |
| 动画 | 同构（200ms / EaseIn·EaseOut / 替换式重启） | 同构（单 token 驱动位置+尺寸） |
| 无窗口 | `return`（忽略） | 瞬时切换（可测） |

## 8. 测试计划（Phase7.2）

新增 `ECDI/src/Tests/CollapsiblePanelTests.cpp`（无 Window 场景，纯控件契约）：

1. `CollapsiblePanel.DefaultExpanded`：构造即展开（IsExpanded()==true）、方向默认 Down。
2. `CollapsiblePanel.DownCollapse`：Down 折叠 → h=0、y 不变、内容 invisible、内容坐标 HitTest 返回 nullptr。
3. `CollapsiblePanel.UpCollapse`：Up 折叠 → h=0、底边保持（y0+H 不变，y 变为 y0+H）。
4. `CollapsiblePanel.RightCollapse`：Right 折叠 → w=0、x 不变。
5. `CollapsiblePanel.LeftCollapse`：Left 折叠 → w=0、右边保持（x0+W 不变，x 变为 x0+W）。
6. `CollapsiblePanel.ExpandRestoresRect`：展开 → 几何回 m_expandedRect、内容可见。
7. `CollapsiblePanel.Idempotent`：同状态重复 SetExpanded 无副作用。
8. `CollapsiblePanel.ToggleFlipsState`：Toggle 翻转状态。
9. `CollapsiblePanel.ContentContainer`：GetContent() 非空、可 AddChild、容器尺寸跟随面板。

> 注：动画路径（含 onFinished 隐藏时序）由 AnimationTests 覆盖 manager 时序 + GUI demo 人工验证。

## 9. 文件影响清单

| 文件 | 动作 |
|------|------|
| `ECDI/include/ECDI/Widget/CollapsiblePanel.h` | 新增（含 ExpandDirection 枚举） |
| `ECDI/src/Widget/CollapsiblePanel.cpp` | 新增 |
| `ECDI/src/Tests/CollapsiblePanelTests.cpp` | 新增（注册入 RunAllTests） |
| `ECDI/ECDI.vcxproj` | 新文件加入工程（用户编译环境） |
| `docs/phase9.6-collapsiblepanel-{requirements,preliminary-design,detailed-design}.md` | 三档文档 |
| `main.cpp` | **待单独授权**——demo 替换/并存演示（实现阶段另行确认） |

## 10. 风险与开放问题

1. **折叠态命中**：折叠后尺寸轴 s=0 → `ContainsPoint` 必 false（面板自身不可命中）；内容容器 invisible 双保险。架构债「HitTest 不校验父级裁剪」仍在——但 CollapsiblePanel 场景已闭环，全局修复另立账。
2. **展开基准记忆**：折叠态 SetPosition/SetSize 不同步（v0.1 限制）；override SetSize 同步展开基准——倾向不做（YAGNI），待第二次用例。
3. **方向切换时机**：折叠动画中切方向 = 替换式重启（from=当前呈现值），几何按新方向推导——语义自然但属边界场景，详细设计再定实现细节。
4. **demo 去留**：正式控件落地后 `CollapsiblePanelDemo` 保留（9.6 S2 验证存档 + 含内部 header 参考）还是删除——建议保留；main.cpp 是否切换演示用正式控件另行授权。

## 评审请求

请评审：① 方向模型（锚定边固定 + 对侧收缩 + 位置推导）② 接口面（4 方法 + GetContent + 方向枚举）③ 内容容器（裸 Widget 铺满）④ 可见性时序 ⑤ 展开基准记忆语义 ⑥ demo 保留建议。
