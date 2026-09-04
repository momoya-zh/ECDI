# Phase 9.6 动画系统 · 详细设计

> 版本 v1.1（2026-08-29）。五阶段法第三步：接口与实现方案定稿。前置：requirements v1.1（职责确认通过）、preliminary-design v1.1（初步设计通过）、GPT 实现前评审（有条件通过，四硬约束已补齐）。

## 0. 决策汇总（逐题收敛记录，2026-08-29 用户拍板）

| # | 议题 | 结论 | 来源 |
|---|---|---|---|
| d1 | TimerId 分配约定 | **owner-held + 保留段**：框架保留 1–15；TextBox=1（不迁移），Animation=2；登记表见 §7 | 用户（否决集中 TimerIds.h 倾向） |
| d2 | tick 路由 | 方案 A：`Application::OnTimer` 顶部 `timerId == kAnimationTick` 分支 → `event.GetWindow()->OnAnimationTick()` | 初步设计，GPT 确认 |
| d3 | 时间源 | `steady_clock` 真实 elapsed；**由 Window 计算、以参数传入 manager**（`Tick(elapsed)`） | 初步设计 + 测试参数化决策 |
| d4 | 循环模式 | 挂账（消费者出现再评估） | 职责确认 |
| d5 | 悬挂 / 重启 | 重启 = 替换式（GPT 赞成已收敛）；悬挂 = **弱引用令牌**（生命周期保护机制，非 Widget 指针管理机制——Widget 裸指针树/所有权模型不动） | 用户逐题拍板 |
| d6 | 值应用形态 | **值回调式**：Animation 是纯「时间→值」计算器，不认识目标；持有者经 onValue 回调自写属性 | 用户 |
| d7 | 替换判定键 | **调用方 AnimationToken**：同 token 再 Start = 替换重启；from 由调用方传当前呈现值 | 用户 |
| d8 | 完成回调 | onFinished 纳入 v0.1（S2 折叠收尾真实消费者） | 用户 |
| d9 | 测试时间确定性 | `Tick(elapsed)` 参数化——manager 不持时钟，测试直接传假 elapsed 确定性推进 | 用户 |

## 1. 边界重申（实现期硬约束）

- **动画不产生状态**：控件状态仍由事件驱动（R4 hover/焦点事实）；动画层只平滑到达状态
- **manager 能力接缝**：AnimationManager 只持 `PlatformWindow&`（StartTimer/StopTimer/Invalidate 三能力）；类型上拿不到 `Window&`
- **框架不认识目标**：值回调式的直接推论——Animation/Manager 不持 Widget 指针、不含属性寻址
- **替换式重启的 from 语义由调用方兑现**：manager 不知道"当前显示值"，Start 的 from 参数就是调用方传的当前呈现值（Button 传自己的 m_backgroundColor）——d5 重启语义零框架成本

## 2. 核心类型设计（草案，实现期微调不再另行走确认）

### 2.1 Easing（新增 `Animation/Easing.h`）

```cpp
namespace ECDI {

enum class Easing { Linear, EaseIn, EaseOut, EaseInOut };

// t ∈ [0,1] → eased ∈ [0,1]；纯函数、constexpr 友好
float ApplyEasing(Easing easing, float t);

}  // namespace ECDI
```

- 用 enum + switch 而非函数指针：动画对象内存布局稳定、可比较、测试可枚举
- 公式：Linear=t；EaseIn=t²；EaseOut=1-(1-t)²；EaseInOut=对称组合（t<0.5 ? 2t² : 1-2(1-t)²）——实现期如需更平滑（三次方）在验证阶段调整，接口不变

### 2.2 插值（`Animation/Animation.h` 内部）

- `float`：`from + (to - from) * eased`
- `Color`：RGBA 四通道 float 各自 lerp（Color 为 float RGBA，Phase 4 事实）
- 两个类：`FloatAnimation` / `ColorAnimation`（或模板化——实现期二选一，倾向两个具体类：类型清晰、variant-free）

### 2.3 AnimationToken（新增 `Animation/AnimationToken.h`）

```cpp
namespace ECDI {

// 动画身份键 + RAII 生命周期保护（d5+ d7 统一机制）
// 语义：持有者每"动画属性"持一个成员 token
//  - 同 token 再 Start = 替换式重启（旧动画静默移除）
//  - token 析构 = 自动取消对应动画（弱引用保护：回调永不打在死对象上）
//  - 复制禁用；移动 = 所有权转移（资源类惯例对齐：语义上是"句柄"）
class AnimationToken { /* shared_ptr<TokenState> 内部状态；析构通知 manager */ };

}  // namespace ECDI
```

- **TokenState**：`{ AnimationManager* manager; uint64_t animationId; bool alive; }`——token 析构时若 alive 且 manager 存活 → `manager->CancelByToken(id)`
- **生命周期不变量（GPT 评审 ①，实现前写死）**：TokenState 本质是**共享生命周期状态块，不是弱 Widget 指针**；TokenState 对 AnimationManager 的引用必须是**可失效引用**——manager 不得对 TokenState 承担强生命周期责任（否则形成隐蔽生命周期环）；**manager 析构时必须遍历置空所有关联 TokenState 的 manager 指针**（写死不变量，实现期不得弱化为"假设 manager 存活"）
- **安全析构顺序（GPT 评审 ①）**：token 析构只做 `TokenState.alive = false`，**不得依赖 manager 存活**；动画的实际移除发生在下一次 Tick 检查 alive 时——即「析构标脏、Tick 清理」，而非「析构直接回调 manager」
- **弱引用保护机制边界（用户澄清）**：保护的是回调生命周期，不是 Widget 指针管理——Widget 不用 shared_from_this、所有权模型零改动；manager 在移除动画后置 TokenState 失效
- **Window 先亡场景**：manager 随 Window 析构 → TokenState.manager 置空（manager 析构时遍历置空——上述不变量），token 后析构安全 no-op

### 2.4 AnimationManager（新增 `Animation/AnimationManager.h/.cpp`）

```cpp
namespace ECDI {

class AnimationManager {
public:
    explicit AnimationManager(PlatformWindow& platformWindow);  // 能力接缝：唯一依赖

    // d6 值回调式 + d7 token + d8 onFinished —— Start 签名一次到位
    // from 由调用方传当前呈现值（d5 重启语义的兑现点）
    void Start(AnimationToken& token, float from, float to,
               std::chrono::milliseconds duration, Easing easing,
               std::function<void(float)> onValue,
               std::function<void()> onFinished = nullptr);
    void Start(AnimationToken& token, Color from, Color to, /* 同上，Color 重载 */);

    void Cancel(AnimationToken& token);   // 显式取消（一般不需要——替换/析构已覆盖）
    void Tick(std::chrono::milliseconds elapsed);   // d3+d9：时间以参数进入
    bool HasActive() const;               // 测试与断言用

private:
    PlatformWindow& m_platformWindow;
    std::vector<...> m_active;            // 实现期定具体容器
    uint64_t m_nextId = 1;
};

}  // namespace ECDI
```

**Tick 推进规则**（d9 确定性核心；GPT 评审 ②③④ 三条硬契约已冻结）：

1. 遍历 m_active：entry.elapsed += elapsed；t = clamp(elapsed/duration)；value = lerp(from, to, ApplyEasing(easing, t))；调 onValue(value)
2. token 失效（TokenState.alive == false 或 manager 指针空）→ 移除，**不调 onFinished**
3. t >= 1 → **先 onValue(finalValue)（终值必须经过 onValue 应用到目标，防最后一帧缺失）→ 再 onFinished() → 后移除**（GPT 评审 ③ 写死：onFinished 里读到的目标状态必须是已到达终态）
4. 结束后若 `!HasActive()` → `m_platformWindow.StopTimer(kAnimationTickTimer)`；同时 Invalidate 聚合：tick 内若有任何 onValue 实际执行 → 一次 `m_platformWindow.Invalidate()`

**onFinished 触发契约（GPT 评审 ④，写死）**：

- **正常到达终点 → 调用**；**Replace（同 token 重启）/ Cancel / Token 失效 → 一律不调用**——被取消的动画不得执行其完成逻辑（防「A 已取消却执行 A 的收尾」类 bug）

**Tick 回调重入契约（GPT 评审 ②，行为现在冻结、机制留实现期）**：

- 遍历活动动画期间，**允许 onValue / onFinished 内 Start / Cancel 其他动画**——回调重入是 AnimationManager 的核心工作机制，不得 YAGNI 掉
- **Manager 必须保证当前 Tick 的容器遍历安全**（迭代器不失效、本轮 Tick 启动的新动画不得在本轮被推进）——实现机制（延迟删除 / snapshot / swap / pending 操作等）实现期选定，行为契约不变
- 衍生硬约束：本轮 Tick 中被 Start 的新动画，其首次推进发生在下一次 Tick

**timer 启停时机**：首个 Start → `StartTimer(kAnimationTickTimer, 16ms)`；活跃归零 → StopTimer——空闲零开销（职责确认 §2 兑现）

### 2.5 常量

```cpp
// AnimationManager.h 内（owner-held 语义：AnimationManager 拥有此 id）
inline constexpr UINT_LIKE kAnimationTickTimer = 2;   // 保留段 1–15；TextBox=1（见 §7 登记表）
inline constexpr std::chrono::milliseconds kAnimationTickInterval{16};
```

## 3. 路由通道落地（d2，改动清单）

### 3.1 `Application::OnTimer`（Application.cpp 修改）

```cpp
// 现状（Application.cpp:127）：FindFocusedWidget → target->OnTimer；无焦点 return
// 改动：函数最前面加分支——
if (event.GetTimerId() == kAnimationTickTimer) {
    if (auto* window = event.GetWindow()) window->OnAnimationTick();
    return;  // 动画 tick 不进焦点派发链
}
// 原焦点派发逻辑原样保留（8.5.1 模型零改动）
```

### 3.2 `Window::OnAnimationTick()`（Window.h/.cpp 修改）

```cpp
void Window::OnAnimationTick() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - m_lastAnimationTick;   // 首次 tick：elapsed 从 Start 时刻起算
    m_lastAnimationTick = now;
    m_animationManager.Tick(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed));
}
```

- Window 成员新增：`AnimationManager m_animationManager;`（组合，职责确认 §2）+ `std::chrono::steady_clock::time_point m_lastAnimationTick;`
- **初始化顺序核实点（初步设计 §2 遗留，实现时确认）**：m_animationManager 构造需要 `PlatformWindow&`——依赖 Window 持 PlatformWindow 的实际形态（值/unique_ptr/引用）；若 PlatformWindow 在 Window 内后构造，则 manager 改为构造后 `SetPlatformWindow` 注入或成员顺序调整——实现期以真实代码为准，保持"manager 只持引用"契约不变
- Window 头文件 include AnimationManager.h——**注意 Framework 层零 Windows.h 红线**：Animation 层只用 std::chrono/std::function，无平台类型

### 3.3 TimerEvent 派发语义

零改动——`GetTimerId()` 现状已返回 wParam（若接口名不同以实际为准）；焦点派发路径一字不动。

## 4. S1 / S2 消费实现要点

### 4.1 S1 Button 状态色过渡

- Button 新增成员：`AnimationToken m_bgAnimToken;`（每动画属性一个）
- hover/pressed/focus 事实变化处（R4 已产）：目标状态色 ≠ 当前色 → `Start(m_bgAnimToken, m_backgroundColor /*当前呈现值*/, targetColor, 120ms 量级, EaseOut, [this](const Color& c){ SetBackgroundColor(c); });`
- **析构**：`~Button` 无需手写 Cancel——token 成员析构自动取消（RAII 兜底）；显式 Cancel 仅在"状态反转要立即停"场景使用
- 快速 Enter→Leave：Leave 触发 Start 同 token → 旧动画移除、from = 此时 m_backgroundColor（呈现值）——无跳变

### 4.2 S2 demo 展开/折叠（demo 容器，不入框架）

- 新增 demo 文件（**不动 main.cpp**——AI 惯例红线；demo 容器类放新文件，main 接线由用户执行）
- 容器持 `AnimationToken m_heightToken` + children；折叠：`Start(m_heightToken, 当前高度, 0, 200ms, EaseIn, [this](float h){ SetFixedHeight(h); }, [this]{ 隐藏 children 收尾 });`——onFinished 消费（d8）
- 高度变化 → Geometry 改变 → Layout/Invalidate 链（复用 5.4 基础设施）——验证"动画作为 GUI 体系正常消费者"（GPT 锚定的 S2 定位）
- 高度插值用 float（初步设计结论）；Rect 形态不预建

## 5. 测试计划（Phase 7.2 框架，新增 Phase 同步补测试惯例）

纯逻辑层（确定性，d9 Tick(elapsed) 参数化）：

1. **Easing**：四曲线端点（t=0→0、t=1→1）+ 单调性 + 中点抽样
2. **插值**：float/Color 端点、中间值、from==to 退化
3. **Tick 推进**：Start 后分步 Tick(16ms)×n——中间值序列、duration 到达后 onFinished 恰调一次、随后 HasActive()==false
4. **替换式重启**：动画 A 进行中同 token Start（from=新值）→ 旧动画终止不调 onFinished、新动画从新 from 推进
5. **token 析构**：动画进行中持有者 token 销毁 → 下次 Tick 动画已移除、onFinished 不调
6. **Cancel**：显式取消后 HasActive()==false
7. **timer 启停**：首个 Start 后 HasActive/timer 状态、归零后停——timer 调用经 PlatformWindow 契约，用测试替身（test double）验证 StopTimer/StartTimer 调用序列（7.1 抽象的红利）
8. **回调重入**：onValue 内 Start 新动画 → 本轮不被推进、下轮正常推进；onFinished 内 Start 新动画 → 正确注册（对应重入契约 §2.4）
9. **终值顺序**：完成 tick 断言 onValue(finalValue) 先于 onFinished 调用（onFinished 内读目标状态应为终态）
10. **生命周期不变量**：manager 先析构、token 后析构 → 安全 no-op（TokenState.manager 已置空）

测试文件落点随实现期定（`ECDI/tests/` 既有框架惯例）。

## 6. YAGNI 边界（不做清单，沿袭前两阶段文档）

- 循环模式、速度曲线配置化、全局倍率、动画队列/编排/时间线、跨窗口协调、Fade/PushOpacity、DrawArc、Point/Rect 插值类、ProgressBar/spinner/缩放——全部挂账
- 虚拟时钟抽象——不做（d9 参数化已覆盖测试需求）

## 7. TimerId 保留段登记表（d1：owner-held + 保留段）

| id | owner | 用途 |
|---|---|---|
| 1 | TextBox（`kCaretBlinkTimer`） | 光标闪烁（不迁移） |
| 2 | AnimationManager（`kAnimationTickTimer`） | 动画统一 tick |
| 3–15 | 框架保留 | 新 timer 依序登记于此表 |

- 约定落点：各 owner 头文件常量处注释「框架 timerId 保留段 1–15，登记表见本文档 §7」；新增 timer 必须更新本表

## 8. 实现文件清单（原子授权范围预览——动手前另行确认）

| 动作 | 文件 |
|---|---|
| 新增 | `ECDI/include/ECDI/Animation/Easing.h`、`Animation.h`、`AnimationToken.h`、`AnimationManager.h` |
| 新增 | `ECDI/src/Animation/AnimationManager.cpp`（+ 若 cpp 分离的 easing/插值实现） |
| 修改 | `ECDI/src/Application/Application.cpp`（OnTimer 分支）+ 对应头（若声明变动） |
| 修改 | `Window.h/.cpp`（manager 成员 + OnAnimationTick + 初始化顺序处理） |
| 修改 | `Button.h/.cpp`（S1：token 成员 + 状态变化处 Start） |
| 新增 | S2 demo 容器文件（位置实现期定；**main.cpp 接线两种方式**：AI 修改 = 需单独授权（skill 条 2：2026-08-25 由「禁止」放宽为「需单独授权」）或用户自行接线——实现授权时二选一） |
| 新增 | 测试文件（§5 清单） |

## 9. 修订记录

- v1.0（2026-08-29）初稿：九个决策点全部用户拍板收敛（d6 值回调式 / d7 AnimationToken 替换键 / d8 onFinished 纳入 / d5 弱引用令牌=生命周期保护机制、用户澄清非 Widget 指针管理 / d1 owner-held+保留段（否决集中头倾向）/ d9 Tick(elapsed) 参数化）；核心类型草案（Easing/插值/Token RAII/Manager/Tick 规则）；路由落地清单；S1/S2 实现要点；测试计划；TimerId 登记表；实现文件清单（原子授权预览）。
- v1.1（2026-08-29）GPT 评审整合（有条件通过 → 四约束补齐）：① §2.3 生命周期不变量写死——TokenState = 共享状态块非弱 Widget 指针、对 manager 引用必须可失效、「析构标脏 / Tick 清理」顺序（token 析构不依赖 manager 存活）、manager 析构遍历置空为硬不变量；② §2.4 Tick 回调重入契约冻结——允许 onValue/onFinished 内 Start/Cancel，manager 保证遍历安全，本轮 Start 的新动画下轮才推进（机制留实现期，行为契约不改）；③ 完成帧顺序写死——onValue(finalValue) → onFinished → 移除；④ onFinished 触发契约明确——正常完成才调，Replace/Cancel/Token 失效一律不调；⑤ 测试计划补 8/9/10（重入 / 终值顺序 / 生命周期不变量）；⑥ §8 main.cpp 措辞修正——skill 条 2 实为「需单独授权」（2026-08-25 由禁止放宽），非绝对禁止；接线 AI 修改（单独授权）或用户自接二选一；工作区 MEMORY.md「不动 main」同步修订。GPT 附加确认无异议项：per-Window / Tick(elapsed) / 值回调 / Token 替换键 / onFinished 纳入 / float+Color / 四 easing / owner-held 保留段 / S2 demo 承载 / 失效不调 onFinished。
- v1.2（2026-08-29）实现落账（§8 全清单已实现，main.cpp 接线待用户二选一）：① TokenState 最终形态 = { animationId, alive }——**无 manager 指针字段**（token 永不回调 manager——比「可失效引用」更彻底；manager 条目持 TokenState 的 shared_ptr 弱所有权共享，无环）；② Tick 重入机制定稿 = Entry 堆稳定（vector<unique_ptr<Entry>>）+ tickEnd 边界（本轮 Start 的新动画下轮推进）+ 重入期 Cancel/Replace 延迟删除（cancelled 标记）；③ 新增 SetOnTimerStarted 钩子——Window 重置 elapsed 锚点（首次 tick 从 timer 启动时刻起算，防 epoch 累积跳变）；④ S1 实现落点：m_displayedBackground 呈现值 = OnPaint 单一视觉真相（ApplyTheme/SetStyle = 即时重置语义）；hover/focus 暂无专属色字段（ButtonStyle v0.1）——机制就绪待主题扩展，YAGNI 未预建；⑤ 测试 12 项落 AnimationTests.cpp（TestPlatformWindow 测试替身验 timer 启停序列）。
