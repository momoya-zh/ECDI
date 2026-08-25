# Phase 5.6 IME（候选窗口跟随）初步设计

> 状态：v1.0（2026-08-14）｜初步设计定稿（两轮评审 + 用户确认），待详细设计
> 相关：phase5.6-ime-requirements.md（职责确认 v1.0：I1-I5）/ phase5.5.2-selection-*.md（5.5.2 同源模式）

## 1. 定稿决策（P1-P5 + 两轮评审修正）

### P1 翻译器消息表 —— A ✅（GPT ③ 修正：预留 COMPOSITION 空通道）

```cpp
// WindowMessageHandler.cpp switch 新增（键盘事件之后）：
// 注意：IME 消息必须走 DefWindowProc（维护 IME 内部状态——与普通消息不同，不能 return 0 吞掉）
case WM_IME_STARTCOMPOSITION:{
	// 组合开始 → 通知 Window 定位候选窗口（跟随焦点 TextBox 光标）
	window->NotifyIMEComposition();
	return std::nullopt;
}
case WM_IME_COMPOSITION:{
	// v1.1 实测升级：START 单次定位不可靠（窗口移动后候选窗飘回左上角）——
	// 组合期间每次按键重新定位（P5 决定项执行；消息结构不变）
	window->NotifyIMEComposition();
	return std::nullopt;
}
case WM_IME_ENDCOMPOSITION:{        // 预留通道：组合结束——MVP 无清理动作
	return std::nullopt;
}
```

### P2 Window::NotifyIMEComposition() —— A ✅（GPT ② 修正：dynamic_cast fail-safe）

```cpp
// Window.h public（HandleKeyDown 之后）：
void NotifyIMEComposition();

// Window.cpp（include TextBox.h + <imm.h>；.h 不依赖 TextBox）：
void Window::NotifyIMEComposition(){
	// MVP 技术债（显式记录）：Window 临时识别具体控件 TextBox（框架内首例）。
	// 可接受理由：fail-safe——非 TextBox 焦点时跳过更新，IME 交系统默认行为；
	// 而 Widget 基类虚函数方案会把候选窗钉死在 (0,0)（fail-wrong）。
	// 演进路径：第二个可编辑控件出现时 → 抽象 EditableTextWidget，
	// dynamic_cast<TextBox*> 升级为 dynamic_cast<EditableTextWidget*>；
	// 同时随 Phase 7 PlatformWindow 下沉 Imm 调用。
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){
		const Point caret = textBox->GetCaretClientPosition();   // 客户区坐标（TextBox 零平台依赖）
		POINT pt{ static_cast<LONG>(caret.x), static_cast<LONG>(caret.y) };
		ClientToScreen(m_handle, &pt);   // 平台边界：Client → Screen
		if (HIMC imc = ImmGetContext(m_handle)){   // Imm 三件套：取上下文 → 设置 → 释放
			COMPOSITIONFORM cf{};
			cf.dwStyle = CFS_POINT;         // 候选窗左上角对准 ptCurrentPos（屏幕坐标）
			cf.ptCurrentPos = pt;
			ImmSetCompositionWindow(imc, &cf);
			ImmReleaseContext(m_handle, imc);
		}
	}
}
```

### P3 TextBox 光标几何共享 —— A ✅（GPT 强推 + DeepSeek 补充 GetTextAreaWidth）

```cpp
// TextBox.h public（GetCaret 之后）：
Point GetCaretClientPosition();     // 光标底部客户区坐标（IME 锚点——候选窗贴光标下方，决策 c；TextBox 零平台依赖）

// TextBox.h private：
float GetTextAreaWidth() const noexcept;                     // 可视宽（裁切/Selection/光标共用）
Point CalculateCaretPosition(TextMeasurer& measurer) const;  // 光标底部锚点（相对控件原点，含钳制；OnPaint 竖线 y-lineH 还原顶部）

// TextBox.cpp：
Point TextBox::GetCaretClientPosition(){
	const Point abs = GetAbsolutePosition();
	const Point local = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
	return Point{ abs.x + local.x, abs.y + local.y };
}
```

- **消灭三处漂移**：点击定位（x→caret）/ 光标绘制（caret→x）/ IME（caret→x）——后两处共享 `CalculateCaretPosition`，第一处共享同一坐标系（CalculateTextPosition 同源），不合并为同一函数（方向相反）
- **GetTextAreaWidth 提取**：`控件宽 − 焦点框内缩(2px×2)`——原 maxTextWidth 局部逻辑在裁切/Selection/光标三处内联，改 4px 会漏改（GPT 漂移论据）

### P4 构建链接 Imm32 —— A ✅

- vcxproj：4 个配置 `<Link>` 段加 `<AdditionalDependencies>imm32.lib;%(AdditionalDependencies)</AdditionalDependencies>`
- CMakeLists.txt：`target_link_libraries(ECDI PRIVATE user32 imm32)`
- RTTI：已核实三工具链默认开启（vcxproj 无 RuntimeTypeInfo 覆盖 / CMake 无 -fno-rtti），`dynamic_cast` 零配置

### P5 验证 —— A ✅（GPT 要求：先跑起来再决定）

- 人工交互：中文输入候选窗跟随光标（不再飘左上角）+ 中文上屏回归 + 编辑/点击/拖选不回归
- **输入法矩阵**：搜狗 / 微软拼音 / 中文 / 日文 / 长文本——据实测决定 COMPOSITION 空通道是否升级为"再调 Notify"
- fail-safe 验证：焦点在 Button 时输入，候选窗不强制移动（交系统默认）

## 2. 评审记录（两轮闭环）

- **第一轮 GPT**：① Window 中介 ✅ ② `GetCaretClientPosition()` 不进 Widget 基类（建议 dynamic_cast）⚠️ ③ 只监听 START 建议先验证 ⚠️
- **DeepSeek 评审**：② 采纳 + 补强论据——基类虚函数方案在非 TextBox 焦点时用 (0,0) 更新（fail-wrong），dynamic_cast 跳过更新（fail-safe）；③ 采纳 + 指出 IME 消息必须走 DefWindowProc
- **第二轮 GPT**：全部通过；新建议——Window.cpp 依赖 TextBox 方向略怪，加注释显式化技术债
- **DeepSeek**：注释采纳 + 补"为什么可接受"层（fail-safe 论证 + 演进路径 EditableTextWidget + Phase 7 下沉）
- **用户确认**（2026-08-14）：方案完全一致

## 3. 修订记录

- v1.0（2026-08-14）初步设计定稿：P1-P5。无实现细节变更；与职责确认 I1-I5 完全一致（只做候选窗口跟随、TextBox 零平台依赖、END 空通道扩展为 COMPOSITION/END 双预留）。
- v1.1（2026-08-14，实测驱动）：① **COMPOSITION 由预留升级为实际调用**（P5 决定项执行——START 单次定位不可靠，窗口移动后候选窗飘回左上角；组合期间每次按键重新定位）② **GetCaretClientPosition 返回光标底部锚点**（职责确认 P3 决策 c 回归——候选窗贴光标下方）③ 方法非 const（测量链经 GetWindow()->GetTextMeasurer()）。
