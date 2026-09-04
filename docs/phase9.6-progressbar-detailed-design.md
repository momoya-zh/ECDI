# Phase 9.6 ProgressBar 详细设计（v1.3）

> 阶段：详细设计（五阶段法 ③）——已实施（2026-08-31）
> 日期：2026-08-31
> 状态：v1.3 已实施（110/110 全绿；方案 D：fill 圆角化——修复 demo 实测「高进度 fill 盖满圆角轨道 → bar 呈矩形」观感缺陷）
> 前置：初步设计 v1.2 已通过（GPT 终审 2026-08-31 + 用户确认）
> v1.1：吸收 GPT 评审——测试 8/9 精确化（§5）/ 接缝不扩散约束（§2.2）/ ProgressBarStyle 头文件注释要求（§2.5）
> v1.2：实现落地同步——Zcode 实现 + ProgressBarTests 11 条注册；运行 110/110 全绿
> v1.3：方案 D 变更（用户授权）——填充恒 DrawRect → DrawRoundedRect（与轨道同心圆角）；§2.5/§4.5/§5/§7 同步

## 1. 目标与范围

`ProgressBar`：水平 determinate 进度条，继承 `Widget`（纯视觉控件——零文本、无子容器、无交互语义）。逻辑状态 `m_progress`（目标）与视觉状态 `m_displayProgress`（呈现）分离；平滑过渡消费 per-Window `AnimationManager`（单 token、EaseOut、200ms、替换式重启）。

本设计将初设 v1.2 冻结的语义落成精确签名与实现，并新增 **1 个实现级决策（测试接缝 D2）**——初设测试 8/9（动画推进/替换重启）要求"有 Window 下"可观测动画中间态，现有测试体系全无窗口，需受控接缝兑现（§2.2）。

## 2. 硬契约（冻结语义——实现不得弱化）

### 2.1 状态分离（初设 v1.2 冻结）

| 成员 | 语义 | 写者 | 读者 |
|---|---|---|---|
| `m_progress` | 目标值（逻辑状态）∈ [0,1] | `SetProgress` / `SetPercent` | `GetProgress`（业务查询） |
| `m_displayProgress` | 当前呈现值（视觉状态）∈ [0,1] | 动画 onValue 回调 / 无窗口瞬时赋值 | `OnPaint`（唯一消费者——单一视觉真相） |

- **动画不产生状态**：`SetProgress(0.8)` 只改目标；Tick 驱动呈现趋近目标；OnPaint 只读呈现值。
- **no-op 判断键恒为 `target ↔ m_progress`**（目标 vs 目标），绝不用 `m_displayProgress` 判断——动画进行中它是中间态，拿它比较会把"目标未变"误判成"需要重启"。

### 2.2 动画接缝：`ResolveAnimationManager()`（详设新增 D2）

初设测试 8/9 要求"有 Window 下 SetProgress → Tick 推进 → 呈现值可观测"。**代码事实**：

- 现有测试体系全部无窗口（`Window` 构造需要 `Application*` + 真平台窗口；测试不建真窗口是 7.2 以来的既定边界）。
- `Widget::GetWindow()` 非 virtual 且 `m_window` 仅 Window（friend）可设——测试无法经标准途径注入窗口。
- CollapsiblePanel 的"无窗口降级"对它是够的（折叠语义 = 几何瞬时切换即可断言），但 ProgressBar 的**被测命题正是动画中间态本身**——无窗口降级路径上该命题不可观测。

**定案：protected virtual 测试接缝（唯一新增虚函数，不触碰 AnimationManager）**：

```cpp
/// @brief 解析动画宿主（nullptr = 无动画宿主 → SetProgress 走瞬时降级）
/// @details 默认实现 = GetWindow() ? &GetWindow()->GetAnimationManager() : nullptr
///（运行期唯一路径——与 Button::AnimateBackgroundTo / CollapsiblePanel::SetExpanded 同构）；
/// 测试派生类 override 返回替身 manager（TestPlatformWindow + AnimationManager），
/// 使"有动画宿主"路径无窗口可测。非虚场景零开销（一次虚调用/编辑操作）。
virtual AnimationManager* ResolveAnimationManager() const noexcept;
```

- **不违反初设评审结论**"不改 AnimationManager（只消费）"——manager 本体零改动。
- 默认实现行为与"无窗口降级"逐字节等价（窗口存在 ⇔ 默认解析非空）。
- 与 Widget 既有 protected virtual 家族（OnPaint/ContainsPoint/OnXxx）同层——不是新抽象种类。
- **不扩散约束（GPT 评审 v1.1）**：本接缝是 ProgressBar 的**一次性例外**（其被测命题 = 动画中间态本身，无窗口不可观测），**不得扩散到其他动画控件**——后续动画控件（如 Button 模式）默认"无窗口降级瞬时"即可测，各自再造 `ResolveXXX()` 接缝 = 测试污染正式架构的起点。

### 2.3 AnimateTo 职责（初设 v1.2 冻结）

`AnimateTo(target)` **只启动动画、不改逻辑状态**——`m_progress` 赋值归 `SetProgress`。违反此分工 = 违反"动画不产生状态"。

### 2.4 动画参数（内部常量，不开放配置——YAGNI，同 CollapsiblePanel §2.7）

- 时长 `kProgressTransitionMs = 200`；Easing `EaseOut`；单 token `m_animToken`。
- **替换式重启**：动画中再调 `SetProgress` → 同 token 再 Start = 旧动画静默移除（不调 onFinished——本类不传 onFinished），from = 当前呈现值 `m_displayProgress`（无跳变）。
- **onValue 不显式 Invalidate**：`AnimationManager::Tick` 聚合一次重绘请求（d4 契约——与 CollapsiblePanel v1.2 / Button 同构）。⚠️ 修正初设 §6 伪码：其中 `onValue: m_displayProgress = v; Invalidate();` 的显式 Invalidate 按聚合契约定稿移除（ CollapsiblePanel 详设 §4.3 同款先例）。
- 无 onFinished（RAII token 自动；完成帧 onValue(finalValue) 必达——终值经回调写入呈现值）。

### 2.5 圆角语义（初设 v1.2 冻结）

`cornerRadius == 0 = 自动圆角（GetHeight() / 2）`——不区分"真 0 圆角"与"未指定"；方形轨道 v0.1 不可表达（挂账）。

- **绘制时计算、不存储**：`effectiveRadius = (radius > 0) ? radius : GetHeight() / 2.0f`——SetSize 后自动跟随（含"0 高 → effective 0 → 降级 DrawRect"的退化路径）。
- **轨道与填充共用同一 `effectiveRadius`**（v1.3 方案 D——初设方案 C「填充恒 DrawRect 矩形」经 demo 实测推翻：高进度时直角填充盖满圆角轨道 → bar 视觉成矩形；改填充 `DrawRoundedRect` 同心圆角，视觉无缝；fill 右端呈果冻头，现代进度条常见）。
- 低进度 `fillWidth < 2×radius` → 后端半径钳制（[0, min(w,h)/2]）自动缩圆角，无 artifact；`fillWidth == 0` → 后端空宽 no-op（与方案 C 零宽语义一致——轨道完整胶囊可见）。
- **头文件注释要求（GPT 评审 v1.1 ④）**：`ProgressBarStyle.h` 的 `cornerRadius` 字段注释必须明确写出"**0 = 自动圆角（height/2），非真实 0 圆角——`SetStyle({.cornerRadius = 0})` 得到全圆角而非方角；v0.1 无法表达方角**"——防止未来使用者（包括自己）误读为方角。

### 2.6 样式颜色即时生效（与 Button 的有意差异）

轨道/填充色**不参与动画**（动画只驱动几何呈现值）——`ApplyTheme`/`SetStyle` 改色后下一次 Paint 立即反映，**无呈现值同步问题**。对比：Button 需 `m_displayedBackground` + Cancel 在跑动画，因其背景色本身是动画属性；ProgressBar 的动画属性是 `m_displayProgress`（float），与颜色字段无耦合——**不需要** Cancel 逻辑（颜色变化与在跑的进度动画互不干扰）。

### 2.7 无交互（YAGNI）

- 不 override `CanFocus`（默认 false——不可聚焦）、不消费任何鼠标/键盘事件、不 override `ContainsPoint`（默认矩形命中——HitTest 可返回自身但无事件处理器，无害）。
- 无 `SetOnProgressChanged` 回调（无消费者——7.5 先例是需求出现再加）。

## 3. 接口定义（精确签名）

```cpp
// ECDI/include/ECDI/Widget/ProgressBar.h
#pragma once

#include "ECDI/Animation/AnimationToken.h"
#include "ECDI/Theme/ProgressBarStyle.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

class AnimationManager;   // 前置声明（ResolveAnimationManager 返回指针）

/// @brief 水平 determinate 进度条（9.6 第二动画消费者——纯视觉控件）
/// @details 继承 Widget（不继承 TextWidget/Panel——零文本、无子容器语义）。
/// 状态分离：m_progress = 目标（逻辑）/ m_displayProgress = 呈现（视觉，动画驱动）。
/// 动画不产生状态：SetProgress 只改目标并启动过渡；OnPaint 只读呈现值。
class ProgressBar: public Widget{

public:

	ProgressBar();   // 默认构造——构造体注入 ProgressBarStyle（尺寸由使用者 SetSize）

	// ── 值（双层 API）────────────────────────────────

	/// @brief 设置目标进度（clamp [0,1]；默认平滑过渡；同目标 no-op）
	/// @details 逻辑状态归本方法；呈现经动画趋近。无动画宿主（§2.2 nullptr）→ 瞬时到位。
	void SetProgress(float progress);

	/// @brief 设置目标进度（百分比；clamp [0,100]——SetProgress(p/100.0f) 包装）
	void SetPercent(int percent);

	/// @brief 目标进度（逻辑状态只读查询——呈现值不对外，测试经派生类访问）
	[[nodiscard]] float GetProgress() const noexcept{ return m_progress; }

	// ── Phase 9：主题与样式（D7 契约）────────────────

	/// @brief 应用主题（ProgressBarStyle 注入——Widget 层无此虚函数（在 TextWidget），按 Panel 先例自行声明）
	void ApplyTheme(const Theme& theme);

	/// @brief 样式运行时覆盖（颜色/圆角即时生效——§2.6：颜色不参与动画，无 Cancel 逻辑）
	void SetStyle(ProgressBarStyleOverride override);

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

	/// @brief 解析动画宿主（§2.2 测试接缝——nullptr = 瞬时降级；默认经 GetWindow()）
	/// @details 一次性例外（详设 §2.2）：ProgressBar 的被测命题 = 动画中间态，无窗口不可观测——
	/// 测试派生类 override 返回替身 manager。**不得扩散到其他动画控件**（默认无窗口降级即够）。
	virtual AnimationManager* ResolveAnimationManager() const noexcept;

	/// @brief 呈现值（视觉状态——OnPaint 唯一消费；protected——测试派生类可访问，同 TextBox::m_style 先例）
	float m_displayProgress = 0.0f;

	/// @brief ProgressBar 专属样式（protected——测试派生类可访问，同 TextBox/Button/Panel 先例）
	ProgressBarStyle m_style;

private:

	/// @brief 启动到 target 的过渡动画（§2.3：只启动动画、不改逻辑状态）
	void AnimateTo(float target);

	static constexpr int kProgressTransitionMs = 200;       ///< 过渡时长（内部常量，不开放）
	static constexpr float kProgressEpsilon = 1e-6f;        ///< 同目标 no-op 判定阈（§2.1）

	float m_progress = 0.0f;                ///< 目标值（逻辑状态）
	AnimationToken m_animToken;             ///< 进度动画令牌（RAII；每动画属性一个）

};

}
```

**依赖 include**：`AnimationToken.h`（token）、`ProgressBarStyle.h`（Style）、`Widget.h`（基类 + PaintContext/Rect）。cpp 需 `AnimationManager.h`、`DefaultTheme.h`、`Window.h`（GetAnimationManager）、`<algorithm>`（clamp）、`<chrono>`、`<cmath>`（fabs）。

## 4. 实现细节

### 4.1 构造

```cpp
ProgressBar::ProgressBar(){
	// Widget 构造期虚函数静态派发——必须在此重新调用以覆盖 ProgressBar::ApplyTheme
	//（同 Button/CollapsiblePanel 构造先例）
	ApplyTheme(GetDefaultTheme());
}
```

不设默认尺寸（与 Button/Label 一致——尺寸由使用者 `SetSize`；0 高时 auto 圆角退化 0 → 轨道 DrawRect，合法路径）。

### 4.2 SetProgress / SetPercent

```cpp
void ProgressBar::SetProgress(float progress){

	// clamp [0,1]（目标值域保证——呈现值经插值天然保持在合法区间内）
	const float target = (std::clamp)(progress, 0.0f, 1.0f);

	// no-op 判断键恒为 target ↔ m_progress（§2.1 冻结——绝不用 m_displayProgress 判断）
	if (std::fabs(target - m_progress) < kProgressEpsilon){
		return;
	}

	m_progress = target;                        // 逻辑状态归 SetProgress（§2.3 冻结）

	if (AnimationManager* manager = ResolveAnimationManager()){
		AnimateTo(target);                      // 有宿主：平滑过渡（from = 当前呈现值）
	}
	else{
		m_displayProgress = target;             // 无宿主（测试树/构造期）：瞬时到位
	}

	Invalidate();                               // 动画首帧前的重绘请求（同 CollapsiblePanel::SetExpanded 先例——
	                                            // 首帧前一帧延迟可接受；聚合契约接管后续帧）
}

void ProgressBar::SetPercent(int percent){
	SetProgress(static_cast<float>((std::clamp)(percent, 0, 100)) / 100.0f);
}
```

### 4.3 AnimateTo + ResolveAnimationManager

```cpp
void ProgressBar::AnimateTo(float target){

	// ResolveAnimationManager 非空调用方保证（SetProgress 分支前置）——本方法不做 nullptr 分支
	ResolveAnimationManager()->Start(
		m_animToken,
		m_displayProgress,                      // from = 当前呈现值（替换式重启语义兑现点——动画中再设无跳变）
		target,
		std::chrono::milliseconds(kProgressTransitionMs),
		Easing::EaseOut,
		[this](float v){ m_displayProgress = v; });   // onValue 只写呈现值；不显式 Invalidate（§2.4 聚合契约）
	                                            // 无 onFinished（RAII token；完成帧 onValue(final) 必达）
}

AnimationManager* ProgressBar::ResolveAnimationManager() const noexcept{
	const Window* window = GetWindow();
	return window ? &const_cast<Window*>(window)->GetAnimationManager() : nullptr;
}
```

> 注：`GetAnimationManager()` 为非 const（Window.h:94）——const 方法内经 const_cast 调用（GetLineHeight 先例，TextBox.cpp:285-286 同款）。

### 4.4 ApplyTheme / SetStyle（D7 契约）

```cpp
void ProgressBar::ApplyTheme(const Theme& theme){

	ProgressBarStyle defaults = theme.GetProgressBarStyle();
	m_style.trackColor.Apply(defaults.trackColor.value);
	m_style.fillColor.Apply(defaults.fillColor.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);

	Invalidate();   // 颜色/圆角即时生效（§2.6——无呈现值同步，无 Cancel 逻辑）

}

void ProgressBar::SetStyle(ProgressBarStyleOverride override){

	if (override.trackColor)   m_style.trackColor.Set(*override.trackColor);
	if (override.fillColor)    m_style.fillColor.Set(*override.fillColor);
	if (override.cornerRadius) m_style.cornerRadius.Set(*override.cornerRadius);

	Invalidate();

}
```

### 4.5 OnPaint（命令流与几何）

```cpp
void ProgressBar::OnPaint(PaintContext& ctx, int x, int y){

	const Rect bounds{ static_cast<float>(x), static_cast<float>(y),
	                   static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };

	// 圆角：绘制时计算（§2.5——不存储，SetSize 自动跟随；0 高 → 0 → 降级 DrawRect）
	const float configured = m_style.cornerRadius.value;
	const float effectiveRadius = (configured > 0.0f) ? configured
	                                                  : static_cast<float>(GetHeight()) / 2.0f;

	// ① 轨道（全尺寸；圆角 > 0 走 DrawRoundedRect，= 0 降级 DrawRect——Button 同款分支）
	if (effectiveRadius > 0.0f){
		ctx.DrawRoundedRect(bounds, effectiveRadius, m_style.trackColor.value);
	}
	else{
		ctx.DrawRect(bounds, m_style.trackColor.value);
	}

	// ② 填充（圆角矩形——v1.3 方案 D：与轨道同心 effectiveRadius，视觉无缝）
	//    0 进度 → 宽 0（命令仍发出——测试 5 锚定该语义；后端空宽 no-op 无可见输出）
	//    低进度 fillWidth < 2×radius → 后端半径钳制自动缩圆角（DrawRoundedRect 契约）
	const float fillWidth = static_cast<float>(GetWidth()) * m_displayProgress;
	ctx.DrawRoundedRect(Rect{ bounds.x, bounds.y, fillWidth, bounds.height },
	                    effectiveRadius, m_style.fillColor.value);

}
```

**完整命令流**（Widget::Paint 基类管线 = PushClip 控件边界 → OnPaint → PopClip）：

```
[PushClip(控件边界), DrawRoundedRect|DrawRect(轨道), DrawRoundedRect|DrawRect(填充), PopClip]   // 恒 4 条（尺寸合法时；v1.3 填充改圆角——轨道/填充同心）
```

### 4.6 析构

隐式 `~ProgressBar() = default`——`m_animToken` RAII 析构标脏 → 在跑动画下次 Tick 被清除且不调 onFinished（AnimationToken 生命周期不变量）——回调永不打在死对象上。无手动清理。

## 5. 测试设计（Phase7.2 体系，11 条——初设 v1.2 冻结清单兑现）

新增 `ECDI/src/Tests/ProgressBarTests.cpp`，`RegisterProgressBarTests()` 注册（RunAllTests.h/.cpp）。

**脚手架（测试文件内，匿名命名空间——同 ClipTests 的 ProportionalMeasurer 先例，不共享头）**：

- `TestPlatformWindow`：最小 PlatformWindow 替身（StartTimer/StopTimer/Invalidate 计数——从 AnimationTests.cpp 复制精简版；匿名命名空间隔离，无链接冲突）。
- `TestableProgressBar : ProgressBar`：① override `ResolveAnimationManager()` 返回注入的替身 manager（§2.2 接缝的消费点）；② `float DisplayProgress() const { return m_displayProgress; }`（protected 呈现值只读暴露——断言用）；③ `SetTestAnimationManager(AnimationManager*)` 注入口。
- 动画推进用 `manager.Tick(std::chrono::milliseconds)` 传假 elapsed（d9 参数化——确定性，不 sleep）。

| # | 用例 | 断言 |
|---|------|------|
| 1 | `ProgressBar.DefaultValues` | 构造后 `GetProgress()==0`、`DisplayProgress()==0`（逻辑/呈现双零——§2.1） |
| 2 | `ProgressBar.SetProgressClamp` | 无宿主下 `SetProgress(2.0f)` → `GetProgress()==1.0f` 且 `DisplayProgress()==1.0f`（瞬时）；`SetProgress(-1.0f)` → 双 0 |
| 3 | `ProgressBar.SetPercent` | `SetPercent(50)` → `GetProgress()==0.5f`（EXPECT_NEAR）；`SetPercent(150)` → 1.0；`SetPercent(-5)` → 0.0 |
| 4 | `ProgressBar.PaintTwoLayers` | `SetSize(400,20)` + Paint（RecordingBackend）→ 命令流 size==4；[1]=DrawRoundedRect（auto 圆角 == 10.0f = height/2，§2.5）；[2]=DrawRoundedRect 填充（v1.3 方案 D——同心圆角 == 10.0f，宽 == 400×DisplayProgress）；[3]=PopClip |
| 5 | `ProgressBar.PaintZeroProgress` | `SetProgress(0)`（构造即 0——no-op 不重启）后直接 Paint → 填充 DrawRoundedRect 宽 == 0（§4.5 语义锚定） |
| 6 | `ProgressBar.SetStyle` | `SetStyle(fillColor = 自定义)` → Paint 填充色立即反映；随后 `ApplyTheme(GetDefaultTheme())` 不回退（D7 overridden 契约——TestPanelSetStyle 同构） |
| 7 | `ProgressBar.ApplyTheme` | 构造后 trackColor/fillColor == `GetDefaultTheme().GetProgressBarStyle()` 对应值（默认注入验证） |
| 8 | `ProgressBar.AnimationProgresses` | 接缝注入替身 manager → `SetProgress(1.0f)` → `Tick(1ms)` → `0 < DisplayProgress() < 1`（中间态存在；**短 tick 防对具体时长实现的依赖**，v1.1 GPT 修订——不绑 easing 数值）→ `Tick(300ms)` → `DisplayProgress()==1.0f`（完成帧 onValue(final) 必达）且 `GetProgress()==1.0f` |
| 9 | `ProgressBar.AnimationReplacement` | `SetProgress(1.0f)` → `Tick(50ms)` → 记 `p1 = DisplayProgress()`（∈ (0,1)）→ **`SetProgress(0.5f)` 后立即断言 `DisplayProgress()==p1`**（核心不变量：**替换动画的 from == 替换瞬间的呈现值**——SetProgress 只改目标、不动呈现，v1.1 GPT 修订——取代原"p1 附近"模糊断言）→ `Tick(1ms)` → `p3 > 0`（未跳回 0——新动画从 p1 出发向 0.5 运动）→ `Tick(300ms)` → `DisplayProgress()==0.5f` |
| 10 | `ProgressBar.IdempotentTarget` | 无宿主下 `SetProgress(0.5f)` 两次 → 第二次 no-op（GetProgress 不变）；接缝下完成一次动画（Tick 至 HasActive()==false）→ 再 `SetProgress(0.5f)`（同目标）→ `manager.HasActive()==false`（未启动新动画——no-op 判断键 target↔m_progress 验证） |
| 11 | `ProgressBar.ResizeFillGeometry` | `SetProgress(0.25f)`（无宿主瞬时）→ `SetSize(400,20)` → Paint → 填充 DrawRoundedRect 宽 == 400×0.25（填充宽度跟随 Widget 宽度） |

**测试环境边界**：无宿主路径（2/3/5/6/7/11）不依赖任何平台；动画路径（8/9/10）经接缝 + 替身 manager——全程无真窗口、无 sleep。EXPECT_NEAR 精度 1e-4（同 AnimationTests）。

## 6. 文件影响清单（实现阶段原子授权预览）

| # | 文件 | 动作 |
|---|------|------|
| 1 | `ECDI/include/ECDI/Theme/ProgressBarStyle.h` | 新增（ProgressBarStyle + ProgressBarStyleOverride——StyleField 三字段；**cornerRadius 注释须写明 0 = 自动圆角语义**，§2.5） |
| 2 | `ECDI/include/ECDI/Theme/Theme.h` | 修改（+include ProgressBarStyle.h + `virtual ProgressBarStyle GetProgressBarStyle() const = 0;`） |
| 3 | `ECDI/include/ECDI/Theme/DefaultTheme.h` | 修改（+override 声明） |
| 4 | `ECDI/src/Theme/DefaultTheme.cpp` | 修改（+GetProgressBarStyle 实现：trackColor(220,220,230) / fillColor(80,120,220) / cornerRadius 0） |
| 5 | `ECDI/include/ECDI/Widget/ProgressBar.h` | 新增（§3 签名） |
| 6 | `ECDI/src/Widget/ProgressBar.cpp` | 新增（§4 实现） |
| 7 | `ECDI/src/Tests/ProgressBarTests.cpp` | 新增（11 条 + TestPlatformWindow 精简替身 + TestableProgressBar） |
| 8 | `ECDI/src/Tests/RunAllTests.h` / `RunAllTests.cpp` | 修改（RegisterProgressBarTests 声明 + 注册调用） |
| 9 | `ECDI/ECDI.vcxproj`（+filters） | 修改（4 个新文件登记） |
| 10 | `docs/phase9-theme-system-detailed-design.md` | v1.7 → v1.8（§4 DefaultTheme + §3 ProgressBarStyle 字段 + 修订记录） |
| 11 | `docs/phase9.6-progressbar-detailed-design.md` | 本文档 |

**不改动**：AnimationManager/AnimationToken/Easing（只消费）、Widget/Panel/TextWidget 基类、既有控件与测试、main.cpp（demo 接线如需，另行单独授权）。

**影响面自查**（8/30 教训——影响面必须覆盖"消费被改接口的所有文件"）：Theme 抽象基类加纯虚 → 全仓 Theme 派生类仅 `DefaultTheme` 一个（grep 核实），同步实现即闭环；ProgressBarStyle 为全新类型、ProgressBar 为全新类，无既有消费者——其余测试文件零波及。

## 7. 已知限制（v0.1 冻结，与初设 §8 一致）

1. ~~填充恒矩形（无圆角语义——方案 C）~~：v1.3 已改 `DrawRoundedRect`（方案 D，用户授权）——填充与轨道同心圆角；低进度 < 2×radius 时后端半径钳制为小圆角矩形（DrawRoundedRect 契约，非 artifact）；fill 圆角不再挂账。
2. 仅水平方向；垂直不做（挂账）。
3. indeterminate 不做（DrawArc/PushTransform 未解锁 + 无循环 token 模式——挂账）。
4. 不内置文本百分比（外部 Label 组合）。
5. **动画中外部 SetSize 不是支持场景**（同 CollapsiblePanel 冻结点延伸）：动画期间 fill 宽按新 SetWidth × 旧呈现值绘制，视觉自洽（宽是绘制时读取）；但动画不因 resize 重启/换算。
6. 方形轨道不可表达（cornerRadius==0 被自动圆角语义占用——初设 v1.2 冻结决策的代价，挂账）。
7. 无动画宿主时（未挂树）样式色与几何均瞬时——与测试可测性设计一致，非缺陷。

## 8. 评审结论

> v1.0 评审请求（① 接缝 ② SetProgress 结构 ③ OnPaint ④ 测试充分性 ⑤ 影响清单）已由 GPT 评审（2026-08-31）：**五项全过**；① 另附"不扩散到其他控件"约束（已入 §2.2）；④ 的测试 8/9 精确化修订条件已吸收（§5）；另要求 ProgressBarStyle.h 注释明确 cornerRadius==0 语义（已入 §2.5）。**已实施（2026-08-31）。**
>
> v1.3（2026-08-31）：demo 实测发现方案 C 视觉缺陷——高进度时直角填充盖满圆角轨道 → bar 呈纯矩形；经用户确认实施方案 D（填充 DrawRoundedRect 同心圆角），代码 + 测试 4/5/6/11 同步，110/110 全绿。**状态：已实施。**

## 9. 修订记录

- v1.0（2026-08-31）详细设计初稿：初设 v1.2 冻结语义落成精确签名/实现；新增 D2 测试接缝（ResolveAnimationManager）；修正初设 §6 伪码的显式 Invalidate（按 d4 聚合契约定稿）；测试 11 条落到可执行粒度（TestPlatformWindow 精简替身 + TestableProgressBar）；影响面清单 11 项 + Theme 派生类自查。
- v1.1（2026-08-31）吸收 GPT 评审：**测试 8 精确化**（中间态 tick 50ms → 1ms——降低对时长实现的依赖，只验 (0,1) → 终值）；**测试 9 精确化**（核心不变量改为"`SetProgress` 后立即 `DisplayProgress()==p1`"——直接验证替换动画 from == 替换瞬间呈现值，取代"p1 附近"模糊断言）；**§2.2 补接缝不扩散约束**（ProgressBar 一次性例外，禁止扩散）；**§2.5 补 ProgressBarStyle.h 注释要求**（cornerRadius==0 = 自动圆角语义写进头文件）；**§3 代码块同步回写**：`m_displayProgress` / `m_style` 移至 `protected`（实现同步——测试派生类需读取呈现值/样式字段，同 TextBox::m_style 先例，v1.3 主题详设已有先例）。
- v1.2（2026-08-31）实现落地同步：Zcode 实现（ProgressBar.h/.cpp + ProgressBarStyle.h + Theme/DefaultTheme + RunAllTests + vcxproj 登记）+ ProgressBarTests 11 条；运行 110/110 全绿（退出码 0）。§8 状态更新为已实施。
- v1.3（2026-08-31）方案 D 变更（用户授权）：填充恒 `DrawRect` → `DrawRoundedRect`（与轨道同心 `effectiveRadius`）——修复 demo 实测观感缺陷「高进度 fill 盖满圆角轨道 → bar 呈纯矩形」；同步 §2.5（轨道/填充共用半径 + 低进度钳制语义）、§4.5（OnPaint ②）、命令流（§4.5）、§5 测试 4/5/11（`DrawRectCommand` → `DrawRoundedRectCommand`，测试 4 增补同心圆角断言）、§7 限制 1（fill 圆角不再挂账）。
