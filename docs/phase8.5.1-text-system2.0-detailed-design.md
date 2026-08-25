
# Phase 8.5.1 文本系统 2.0 详细设计（核心升级：IME 组合串内嵌 + 剪贴板 + Timer + SetFont）

> 状态：v1.3（2026-08-24）｜已实现 + 验证通过（commit 8ab8300）
> 前序：Phase 8.5 职责确认 v1.1 / 初步设计 v1.2（GPT 两轮评审通过）
> 相关：phase8.5-text-system2.0-requirements.md（职责确认 v1.1）/ phase8.5-text-system2.0-preliminary-design.md（初步设计 v1.2）/ phase5.5-textbox-detailed-design.md（5.5）/ phase5.6-ime-detailed-design.md（5.6）
> 拆分说明：8.5.2 → phase8.5.2-text-system2.0-detailed-design.md / 8.5.3 → phase8.5.3-text-system2.0-detailed-design.md

## 1. 范围（8.5.1）

```
8.5.1 核心升级：IME Composition（模型 B）+ 剪贴板 + Timer/Caret Blink + SetFont
实现 + TestCase F1-F15 + 视觉验证 —— 已完结（commit 8ab8300）
```

---

## 2. 文件改动清单（8.5.1）

| 文件 | 改动 | 类型 |
|---|---|---|
| `ECDI/include/ECDI/Platform/PlatformWindow.h` | 新增 GetClipboardText/SetClipboardText/StartTimer/StopTimer 纯虚 | 修改 |
| `ECDI/include/ECDI/Platform/Win32/Win32PlatformWindow.h` | 新增 4 方法 override + 组合串提取（OnIMECompositionUpdate 平台实现） | 修改 |
| `ECDI/src/Platform/Win32/Win32PlatformWindow.cpp` | 实现 4 方法 + WM_IME_COMPOSITION 组合串提取 | 修改 |
| `ECDI/include/ECDI/Platform/PlatformWindowHost.h` | 新增 OnIMECompositionUpdate 虚回调（组合串内容上报） | 修改 |
| `ECDI/include/ECDI/EventSystem/EventType.h` | 新增 Timer 枚举值 | 修改 |
| `ECDI/include/ECDI/EventSystem/Window/TimerEvent.h` | 新 Event 类（带 timerId） | 新增 |
| `ECDI/src/Platform/Win32/WindowMessageHandler.cpp` | WM_TIMER → TimerEvent 翻译 | 修改 |
| `ECDI/include/ECDI/EventSystem/EventRouter.h` | 新增 OnTimer 虚方法分派 | 修改 |
| `ECDI/include/ECDI/Widget/Widget.h` | 新增 OnTimer 虚方法（protected，空实现） | 修改 |
| `ECDI/include/ECDI/Widget/TextWidget.h` | 新增 SetFont（基类——m_font 归属 TextWidget） | 修改 |
| `ECDI/src/Widget/TextWidget.cpp` | SetFont 实现 | 修改 |
| `ECDI/include/ECDI/Widget/TextBox.h` | Composition 状态成员 + InsertText + OnTimer + 编辑操作 Composition 感知 | 修改 |
| `ECDI/src/Widget/TextBox.cpp` | Composition 生命周期 + Ctrl 组合 + OnTimer + SetFont 接入 | 修改 |
| `ECDI/src/Tests/TextBoxTests.cpp` | 新增 8.5.1 TestCase（Composition/Clipboard 键盘路径/插入点） | 修改 |
| `ECDI/ECDI.vcxproj` + `CMakeLists.txt` | 新增 TimerEvent.h（双工程同步） | 修改 |

main.cpp 不动（skill 2）。

## 3. 剪贴板（B2/C1：Platform capability，非 Event）

### 3.1 PlatformWindow.h 新增（剪贴板 capability + 通用 Timer——C2）

```cpp
// PlatformWindow.h public 区新增：

	/// @brief 从系统剪贴板读取文本（UTF-8；平台转换封装在实现内）
	/// @return 空字符串 = 剪贴板无文本数据（或非文本格式）
	virtual std::string GetClipboardText() const = 0;

	/// @brief 写入文本到系统剪贴板（UTF-8）
	/// @param text 待写入文本（空串 = 清空剪贴板——调用方负责避免误清）
	virtual void SetClipboardText(const std::string& text) = 0;

	/// @brief 启动周期定时器（通用平台能力——ID 语义由调用方定义，平台不知道业务）
	/// @param timerId    定时器标识（调用方自定义；重复 Start 同 id = 重置周期）
	/// @param intervalMs 触发间隔（毫秒）
	virtual void StartTimer(int timerId, unsigned int intervalMs) = 0;

	/// @brief 停止定时器（幂等——未启动/已停止返回无动作）
	virtual void StopTimer(int timerId) = 0;
```

**理由**：剪贴板与 Timer 都是平台能力（同 UpdateTextInputCaret 模式）；C2 契约——`StartCaretBlink/StopCaretBlink` 泄漏 TextBox 语义，通用 `StartTimer/StopTimer` 让平台只知"ID+周期"。

### 3.2 Win32 实现（Win32PlatformWindow.cpp）

**局部 RAII（C10——OpenClipboard/CloseClipboard 资源配对；非 ClipboardManager，YAGNI）**：

```cpp
// Win32PlatformWindow.cpp 匿名 namespace：
namespace{

/// @brief 剪贴板打开守卫（局部 RAII——OpenClipboard/CloseClipboard 配对）
/// @details 仅封装资源配对，不做任何业务逻辑（YAGNI——非 ClipboardManager）；
/// 失败（其他应用占用）时 IsOpen() 为 false，调用方安全跳过。
class ClipboardGuard{
public:
	explicit ClipboardGuard(HWND hwnd): m_opened(OpenClipboard(hwnd) != FALSE){}
	~ClipboardGuard(){ if (m_opened) CloseClipboard(); }
	ClipboardGuard(const ClipboardGuard&) = delete;
	ClipboardGuard& operator=(const ClipboardGuard&) = delete;
	bool IsOpen() const noexcept{ return m_opened; }
private:
	bool m_opened;
};

}
```

**剪贴板读写实现**（CF_UNICODETEXT + GlobalLock/GlobalAlloc + UTF-8↔UTF-16 边界转换）：

```cpp
std::string Win32PlatformWindow::GetClipboardText() const{
	ClipboardGuard guard(m_hwnd);   // RAII：任何路径自动 CloseClipboard
	if (!guard.IsOpen())
		return {};   // 占用失败 → 空（下个机会重试，不崩）
	HANDLE hData = GetClipboardData(CF_UNICODETEXT);
	if (hData == nullptr)
		return {};
	const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(hData));
	if (wide == nullptr)
		return {};
	std::string utf8 = WideToUTF8(wide);
	GlobalUnlock(hData);
	return utf8;
}

void Win32PlatformWindow::SetClipboardText(const std::string& text){
	ClipboardGuard guard(m_hwnd);
	if (!guard.IsOpen())
		return;
	EmptyClipboard();   // 标准流程：先清空再设置
	const std::wstring wide = UTF8ToWide(text);
	const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);   // 含终止符
	HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (hData == nullptr)
		return;
	void* dest = GlobalLock(hData);
	if (dest == nullptr){
		GlobalFree(hData);   // 锁定失败 → 释放（未被剪贴板接管）
		return;
	}
	memcpy(dest, wide.c_str(), bytes);
	GlobalUnlock(hData);
	// ⚠️ C10 契约：SetClipboardData 失败必须释放 hData（成功才由剪贴板接管）
	if (SetClipboardData(CF_UNICODETEXT, hData) == nullptr)
		GlobalFree(hData);
}
```

**注意**：
- `GetClipboardText` 是 const 但调 OpenClipboard——OpenClipboard 不修改 m_hwnd（HWND 按值传），OK
- 剪贴板失败静默返回（占用/无数据 → 空串；不崩不报错）

**Timer 实现**（SetTimer/KillTimer——timerId 直接映射，Win32 定时器 ID 即 wParam）：

```cpp
void Win32PlatformWindow::StartTimer(int timerId, unsigned int intervalMs){
	if (m_hwnd)
		SetTimer(m_hwnd, timerId, intervalMs, nullptr);
}

void Win32PlatformWindow::StopTimer(int timerId){
	if (m_hwnd)
		KillTimer(m_hwnd, timerId);
}
```

### 3.3 TextBox OnKeyDown 扩展（Ctrl 组合——C1 契约）

```cpp
void TextBox::OnKeyDown(const KeyDownEvent& event){
	// 8.5.1：Ctrl 组合优先（剪贴板/全选）——KeyDown 是事实，Copy/Paste 是 TextBox 的语义解释
	if (event.IsCtrlDown()){
		switch (event.GetKeyCode()){
		case KeyCode::C:    CopySelectionToClipboard();  return;
		case KeyCode::V:    PasteFromClipboard();        return;
		case KeyCode::X:    CutSelectionToClipboard();   return;
		case KeyCode::A:    SelectAll();                 return;
		default: break;   // 其余 Ctrl 组合交默认（8.5.3 加 Ctrl+Z/Y）
		}
	}
	// 原有编辑键映射（Backspace/Delete/←→/Home/End + Shift+方向键 5.5.2）...
}
```

**新私有方法**（TextBox.cpp，全 private）：

```cpp
/// @brief 复制选中区到剪贴板（无选区 = 空操作；Ctrl+C/X）
void CopySelectionToClipboard();

/// @brief 剪切选中区（复制 + 删选中区；Ctrl+X）
void CutSelectionToClipboard();

/// @brief 从剪贴板粘贴（Ctrl+V；经 InsertText——多码点插入）
void PasteFromClipboard();

/// @brief 全选（Ctrl+A；anchor=0, caret=末尾）
void SelectAll();
```

**InsertText 新增（8.5.1 粘贴/IME Commit 共用——多码点插入）**：

```cpp
/// @brief 在光标处插入一段文本（UTF-8；粘贴/IME Commit/程序调用共用）
/// @details 与 InsertCodepoint 同构：有 Selection 先删选中区 → 插入 → 光标后移（码点数）→
/// 清 Selection → Invalidate + SyncTextInputCaret + RaiseTextChanged
void InsertText(const std::string& text);
```

实现（码点数 = ByteOffsetToCodepointIndex(text, text.size())）：
```cpp
void TextBox::InsertText(const std::string& text){
	if (text.empty())
		return;   // 空串粘贴 = 空操作（不触发回调——D7 边界语义）
	if (HasSelection())
		m_caret = DeleteSelection();
	const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
	m_text.insert(byte, text);
	m_caret += ByteOffsetToCodepointIndex(text, text.size());
	ClearSelection();
	Invalidate();
	SyncTextInputCaret();
	RaiseTextChanged();
}
```

## 4. IME Composition（B1 模型 B：覆盖 m_text 临时区间）

### 4.1 平台层组合串提取（Win32PlatformWindow.cpp）

当前 WM_IME_COMPOSITION 只调 `m_host.OnIMEComposition()`（候选窗定位）。8.5.1 增加组合串内容上报，**并明确区分 Update（GCS_COMPSTR）与 Commit（GCS_RESULTSTR）**（C7 契约——不能把"空组合串 = Commit"当可靠判断）：

```cpp
// Win32PlatformWindow::HandleMessage 状态同步区，WM_IME_COMPOSITION case 扩展：
case WM_IME_COMPOSITION:{
	// 8.5.1：候选窗定位（既有）+ 组合串内容上报（新——组合串内嵌渲染）
	m_host.OnIMEComposition();

	if (lParam & GCS_COMPSTR){
		// ① 组合串更新（正在组合的内容）——GCS_COMPSTR
		if (HIMC imc = ImmGetContext(m_hwnd)){
			// ⚠️ 实施要点（GPT 检查点 1）：ImmGetCompositionStringW 返回 LONG（字节数），
			// 非 DWORD——负值 = 调用失败/无数据，必须先判负再使用
			const LONG len = ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
			if (len > 0){
				std::wstring composition(static_cast<size_t>(len) / sizeof(wchar_t), L'\0');
				ImmGetCompositionStringW(imc, GCS_COMPSTR,
					composition.data(), static_cast<DWORD>(len));
				m_host.OnIMECompositionUpdate(WideToUTF8(composition));
			}
			else{
				m_host.OnIMECompositionUpdate({});   // 组合串清空（组合仍在——非 Commit）
			}
			ImmReleaseContext(m_hwnd, imc);
		}
	}

	if (lParam & GCS_RESULTSTR){
		// ② 组合提交（最终结果）——GCS_RESULTSTR（C7：Commit 的唯一可靠来源）
		if (HIMC imc = ImmGetContext(m_hwnd)){
			const LONG len = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
			if (len > 0){
				std::wstring result(static_cast<size_t>(len) / sizeof(wchar_t), L'\0');
				ImmGetCompositionStringW(imc, GCS_RESULTSTR,
					result.data(), static_cast<DWORD>(len));
				m_host.OnIMECompositionCommit(WideToUTF8(result));
			}
			else{
				m_host.OnIMECompositionCommit({});   // 空结果 Commit（合法——见 §4.4 C12）
			}
			ImmReleaseContext(m_hwnd, imc);
		}
	}

	break;   // 继续 DefWindowProcW（IME 内部状态机必需）
}
```

**C9 契约（Composition caret——8.5.1 明确）**：`GCS_CURSORPOS`（组合内光标，UTF-16 code unit）**8.5.1 不提取**——`m_compositionCaret` 固定为组合串末尾（简单输入场景成立）；8.5.2 组合内移动时再接入，届时转换（UTF-16 → code point）**完全放在 Win32PlatformWindow**，不泄漏 UTF-16 单位到框架层。

### 4.2 Host 回调（PlatformWindowHost.h 新增虚方法）

```cpp
/// @brief IME 组合串内容更新（8.5.1：平台层状态上报——非 Event 系统成员，同 OnIMEComposition）
/// @param compositionText 当前组合串（UTF-8；空串 = 组合中无内容——组合仍在，非 Commit）
/// @details 平台实现（Win32）在 WM_IME_COMPOSITION 且 lParam & GCS_COMPSTR 时提取上报；
/// 框架层 Window 转发焦点控件更新 Composition 状态（临时编辑，不触发正式编辑语义）。
virtual void OnIMECompositionUpdate(const std::string& compositionText) = 0;

/// @brief IME 组合提交（8.5.1：C7 契约——Commit 的唯一可靠来源）
/// @param resultText 最终结果文本（UTF-8）
/// @details 平台实现（Win32）在 WM_IME_COMPOSITION 且 lParam & GCS_RESULTSTR 时提取上报；
/// 框架层 Window 转发焦点控件——组合区间转正式文本（进 Undo 历史）。
virtual void OnIMECompositionCommit(const std::string& resultText) = 0;
```

### 4.3 Window 转发（Window.h/cpp）

```cpp
// Window.h public（NotifyIMEComposition 附近）：
/// @brief IME 组合串内容更新（8.5.1；平台层 OnIMECompositionUpdate 回调）
/// @details 转发焦点 TextBox：更新组合状态（模型 B——覆盖 m_text 临时区间）。
/// dynamic_cast<TextBox*> 为既有债务（同 NotifyIMEComposition——EditableTextWidget 以后做）。
void NotifyIMECompositionUpdate(const std::string& compositionText);

/// @brief IME 组合提交（8.5.1；平台层 OnIMECompositionCommit 回调）
/// @details 转发焦点 TextBox：组合区间转正式文本 + 进 Undo（C3/C7 契约）。
void NotifyIMECompositionCommit(const std::string& resultText);

// Window.cpp：
void Window::NotifyIMECompositionUpdate(const std::string& compositionText){
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){
		textBox->UpdateComposition(compositionText);
	}
}

void Window::NotifyIMECompositionCommit(const std::string& resultText){
	if (auto* textBox = dynamic_cast<TextBox*>(m_focusedWidget)){
		textBox->CommitComposition(resultText);
	}
}
```

### 4.4 TextBox 组合状态（TextBox.h 新增 private 成员 + 方法）

```cpp
// TextBox.h private 区（Selection 辅助之后）：

	// ── IME Composition（8.5.1；模型 B——覆盖 m_text 临时区间；C7：Update ≠ Commit）──

	/// @brief 组合串内容更新（Window::NotifyIMECompositionUpdate 转发；GCS_COMPSTR 来源）
	/// @param compositionText 新组合串（UTF-8；空串 = 组合中无内容——组合仍在，非 Commit）
	/// @details 模型 B 状态机：首次（m_isComposing=false）→ 标记区间 + Push Undo；更新 → 替换区间。
	/// ⚠️ 临时编辑语义：不触发 RaiseTextChanged、不产生新 Undo（C3/C8 契约）。
	void UpdateComposition(const std::string& compositionText);

	/// @brief 组合提交（Window::NotifyIMECompositionCommit 转发；GCS_RESULTSTR 来源）
	/// @param resultText 最终结果文本（UTF-8）
	/// @details 组合区间转正式文本（C7）：区间替换为 resultText → 清组合标记 →
	/// Invalidate + SyncTextInputCaret + RaiseTextChanged（正式编辑语义）。
	/// Undo：组合开始前已 Push 一次快照——Commit **不再 Push**（Ctrl+Z 一次撤销整个组合，C3）。
	void CommitComposition(const std::string& resultText);

	/// @brief 组合取消（8.5.1 预留接口——ESC 取消组合；实现可后补）
	/// @details 组合区间擦除 + 清标记（不改正式文本历史）。
	void CancelComposition();

	/// @brief 纯文本模型区间替换（C8：InsertText 与 UpdateComposition 共享的底层操作）
	/// @param startCp 起始码点索引（含）
	/// @param endCp   结束码点索引（不含）
	/// @param replacement 替换文本（UTF-8；空串 = 删除区间）
	/// @return 替换后光标应处码点索引（= startCp + replacement 码点数）
	/// @details 无副作用（不 Invalidate/不 Sync/不 RaiseTextChanged）——副作用由调用方按语义添加。
	size_t ReplaceTextRange(size_t startCp, size_t endCp, const std::string& replacement);

	std::string m_compositionText;    ///< 当前组合串（UTF-8；与 m_text 区间同步——冗余但便于绘制/编辑判断）
	size_t m_compositionStart = 0;    ///< 组合起始码点索引（相对 m_text）
	size_t m_compositionLength = 0;   ///< 组合覆盖码点长度（= 上次 compositionText 的码点数）
	size_t m_compositionCaret = 0;    ///< 组合内光标（相对组合串起点；8.5.1 固定组合末尾——C9）
	bool m_isComposing = false;       ///< 是否在组合中
```

**ReplaceTextRange（C8 抽取——纯文本模型操作，无副作用）**：

```cpp
size_t TextBox::ReplaceTextRange(size_t startCp, size_t endCp, const std::string& replacement){
	const size_t startByte = CodepointIndexToByteOffset(m_text, startCp);
	const size_t endByte   = CodepointIndexToByteOffset(m_text, endCp);
	m_text.erase(startByte, endByte - startByte);
	if (!replacement.empty())
		m_text.insert(startByte, replacement);
	return startCp + ByteOffsetToCodepointIndex(replacement, replacement.size());
}
```

**InsertText 重构（正式编辑——经 ReplaceTextRange + 全副作用）**：

```cpp
void TextBox::InsertText(const std::string& text){
	if (text.empty())
		return;   // 空串粘贴 = 空操作（不触发回调——D7 边界语义）
	const size_t insertAt = HasSelection() ? GetSelectionMin() : m_caret;
	if (HasSelection())
		DeleteSelection();
	m_caret = ReplaceTextRange(insertAt, insertAt, text);   // 纯模型操作
	ClearSelection();
	Invalidate();
	SyncTextInputCaret();
	RaiseTextChanged();
}
```

**模型 B 状态机（TextBox.cpp）——C7/C8 修正版**：

```cpp
void TextBox::UpdateComposition(const std::string& compositionText){
	if (!m_isComposing){
		// 首次：组合开始——标记起点 + Push Undo（C3：一次撤销整个组合）
		m_isComposing = true;
		m_compositionStart = m_caret;
		PushUndoSnapshot();   // 8.5.3 落地；8.5.1 阶段先留调用点（空实现注释）
	}
	// 替换组合区间（模型 B：m_text 含组合串；ReplaceTextRange 无副作用——不触发 TextChanged）
	m_caret = ReplaceTextRange(m_compositionStart, m_compositionStart + m_compositionLength, compositionText);
	m_compositionLength = ByteOffsetToCodepointIndex(compositionText, compositionText.size());
	m_compositionText = compositionText;
	m_compositionCaret = m_compositionLength;   // C9：8.5.1 固定组合末尾
	ClearSelection();
	Invalidate();              // 视觉更新（临时编辑也需重绘）
	SyncTextInputCaret();
	// ⚠️ 不 RaiseTextChanged（C8：Composition Update ≠ 正式编辑）
}

void TextBox::CommitComposition(const std::string& resultText){
	if (!m_isComposing)
		return;   // 无组合中 → no-op（fail-safe）
	// 组合区间 → resultText（正式文本）；清组合标记
	m_caret = ReplaceTextRange(m_compositionStart, m_compositionStart + m_compositionLength, resultText);
	m_isComposing = false;
	m_compositionText.clear();
	m_compositionLength = 0;
	m_compositionStart = m_caret;   // 组合结束光标 = 结果末尾
	m_compositionCaret = 0;
	ClearSelection();
	Invalidate();
	SyncTextInputCaret();
	RaiseTextChanged();   // ✅ Commit = 正式编辑（C3：进 Undo 历史——快照在组合开始时已 Push）
	// ⚠️ 不 PushUndoSnapshot——组合开始前已 Push（C3：Ctrl+Z 一次撤销整个组合）
}

void TextBox::CancelComposition(){
	if (!m_isComposing)
		return;
	// 擦除组合区间（恢复组合开始前状态——该快照已在 UndoStack 顶，无需额外处理）
	m_caret = m_compositionStart;
	ReplaceTextRange(m_compositionStart, m_compositionStart + m_compositionLength, {});
	m_isComposing = false;
	m_compositionText.clear();
	m_compositionLength = 0;
	m_compositionCaret = 0;
	ClearSelection();
	Invalidate();
	SyncTextInputCaret();
}
```

**关键契约（C3/C7/C8——详细设计锁死）**：
- **Update ≠ Commit**：`UpdateComposition`（GCS_COMPSTR）= 临时编辑——不 RaiseTextChanged、不产生新 Undo；`CommitComposition`（GCS_RESULTSTR）= 正式编辑——RaiseTextChanged + 进 Undo（快照在组合开始时 Push 一次，Commit 不再 Push）
- **空组合串 ≠ Commit**：`UpdateComposition("")` 只表示组合中无内容（组合仍在），Commit 必须由 `CommitComposition` 显式触发
- **C12 契约（GPT 实施前补充）**：`CommitComposition("")` 是**合法 Commit**（IME 最终提交空文本——区间删除、正常结束组合）；**取消语义必须走 `CancelComposition()`**（擦除区间 + 无正式编辑副作用）。二者不混用
- **Composition 不调正式编辑 API**：UpdateComposition 内部经 `ReplaceTextRange`（纯模型操作），不调 `InsertText`（后者带正式编辑副作用）——F6 测试成立

**绘制（OnPaint 改造——组合串与正式文本同源）**：
- 模型 B 下 m_text 含组合串，**文本绘制零改动**（DrawText(m_text) 天然包含组合串）
- 组合串视觉区分：在组合区间画**下划线**（标准输入法观感）——DrawRect 细线（Rect{ textPos.x + prefixStart, textPos.y + lineH - 1, prefixEnd - prefixStart, 1 }）或不同颜色；Phase 8 已备 DrawRect
- 光标位置：组合期间光标 = m_compositionStart + m_compositionCaret（CalculateCaretPosition 需感知组合）

**编辑操作 Composition 感知（8.5.1 最小集）**：
- `InsertCodepoint/DeleteBackward/DeleteForward/MoveCaret` 在 `m_isComposing` 时的行为：**8.5.1 暂不处理组合内编辑**（组合内 Backspace/移动归 8.5.2 组合交互）；当前组合中这些键仍作用于 m_text 全局——**但候选窗每次按键会重新定位 + 组合串更新覆盖**（IME 自身管理组合内容）。注释说明限制。

## 5. Timer / Caret Blink（B3/C2）

### 5.1 TimerEvent（新 Event 类）

`ECDI/include/ECDI/EventSystem/Window/TimerEvent.h`：

```cpp
#pragma once

#include "ECDI/EventSystem/Event.h"

namespace ECDI{

/// @brief 周期定时器触发事件（8.5.1；由平台 WM_TIMER 翻译）
/// @details 轻量事实：某 TimerId 触发。语义（光标闪烁/动画）由消费者解释——
/// Event 原则（"已发生的事实"），平台不知道 timerId 属于谁。
class TimerEvent: public Event{
public:
	static EventType StaticType()noexcept{ return EventType::Timer; }

	EventType GetType()const noexcept override{ return StaticType(); }

	/// @param window  事件来源窗口
	/// @param timerId 定时器标识（StartTimer 传入的 id）
	TimerEvent(Window* window, int timerId)noexcept
		: Event(window), m_timerId(timerId){}

	int GetTimerId()const noexcept{ return m_timerId; }

private:
	int m_timerId;
};

}
```

### 5.2 EventType.h 新增

```cpp
	// ── 窗口事件 ────────────────────────────────────
	WindowCreated,			///< 窗口创建完成
	// ...
	Timer,					///< 周期定时器触发（8.5.1；WM_TIMER 翻译）
```

### 5.3 翻译（WindowMessageHandler.cpp 新增 case）

```cpp
	// ── 定时器（8.5.1：WM_TIMER → TimerEvent；timerId 直传 wParam）──
	case WM_TIMER:{
		TimerEvent event(window, static_cast<int>(wParam));
		m_host.OnEvent(event);
		return 0;   // 已处理（无需 DefWindowProc）
	}
```

（include 加 TimerEvent.h；WM_TIMER 常量 Windows.h 已含。）

### 5.4 EventRouter 分派（EventRouter.h/.cpp——OnTimer 虚方法）

```cpp
// EventRouter.h（键盘事件区后新增）：
	/// @brief 定时器触发（8.5.1；焦点控件消费——光标闪烁等）
	/// @details 非坐标事件（无 HitTest）——派发给焦点控件（与 OnCharInput 同路径）
	virtual void OnTimer(const TimerEvent& event){}

// EventRouter.cpp（CharInputEvent Dispatch 后新增）：
	dispatcher.Dispatch<TimerEvent>([this](const TimerEvent& e){
			OnTimer(e);
		});
```

**派发路径（代码事实已核实——Application.cpp 现有 OnCharInput 同构）**：

```
Win32PlatformWindow WM_TIMER
    ↓
WindowMessageHandler → TimerEvent{timerId}
    ↓
m_host.OnEvent(event) → Window::OnEvent → Application::OnEvent（7.1.2 转发链）
    ↓
EventRouter::OnEvent → Dispatch<TimerEvent> → Application::OnTimer（新增 override）
    ↓
FindFocusedWidget(*event.GetWindow()) → target->OnTimer(event)   // 与 OnCharInput 同构，非 Window::DispatchTimerEvent
```

### 5.5 Widget 基类 OnTimer（Widget.h protected）

```cpp
	/// @brief 周期定时器触发（8.5.1；焦点控件可 override——TextBox 光标闪烁）
	/// @details 空实现——无定时器需求的控件不感知；Event 原则"语义由消费者解释"
	virtual void OnTimer(const TimerEvent& event);
```

### 5.6 TextBox 光标闪烁接线

```cpp
// TextBox.h：
	static constexpr int kCaretBlinkTimer = 1;             ///< 光标闪烁定时器 ID
	static constexpr unsigned int kCaretBlinkMs = 500;     ///< 闪烁周期（毫秒）

// TextBox.cpp：
void TextBox::OnFocusGained(){
	m_showCaret = true;
	Invalidate();
	SyncTextInputCaret();
	// 8.5.1：获焦 → 启动光标闪烁定时器（平台层产生，TextBox 经 Window 注册）
	if (Window* window = GetWindow()){
		window->GetPlatformWindow()->StartTimer(kCaretBlinkTimer, kCaretBlinkMs);
	}
}

void TextBox::OnFocusLost(){
	m_showCaret = false;
	Invalidate();
	if (Window* window = GetWindow()){
		window->DestroyTextInputCaret();
		window->GetPlatformWindow()->StopTimer(kCaretBlinkTimer);   // 失焦停止闪烁
	}
}

void TextBox::OnTimer(const TimerEvent& event){
	if (event.GetTimerId() == kCaretBlinkTimer){
		// ⚠️ 焦点防御（GPT 检查点 2）：失焦→获焦切换瞬间，旧控件可能收到排队中的
		// 最后一次 TimerEvent（SetFocusedWidget 顺序 = 旧 OnFocusLost → 新 OnFocusGained，
		// 已核实 Window.cpp:152——StopTimer 在 StartTimer 前，但已排队的 WM_TIMER 无法撤回）
		if (!HasFocus())
			return;   // 已失焦 → 忽略（不闪）
		m_showCaret = !m_showCaret;   // 切换可见性（视觉闪烁）
		Invalidate();
	}
}
```

**架构说明**：TextBox 经 `GetWindow()` 拿 Window，Window 需暴露 `PlatformWindow& GetPlatformWindow()`（新增——与 GetTextMeasurer 同模式）。**TextBox 不直接碰定时器 ID 以外的平台细节**；Timer 产生在平台层、翻译在翻译器、消费在焦点控件（B3 链完整）。

**C11 契约（Timer ID 作用域——GPT 评审补充）**：Timer ID 是 **Window 级别**（SetTimer 挂 m_hwnd）。同一 Window 内同一 timerId 同时只能有一个所有者——当前焦点模型保证"一个 Window → 一个 focusedWidget → 一个 TextBox caret timer"不并发注册，**8.5.1 可接受**；Phase 9 做动画/Tooltip/延迟动作多 timer 并发时再评估（不同 id 即可隔离，无架构改动）。

### 5.7 Window 暴露 PlatformWindow（Window.h/cpp 新增）

```cpp
	/// @brief 获取平台窗口（8.5.1；控件经 protected GetWindow() 获取——平台能力入口）
	/// @details 剪贴板/Timer 等平台能力经此访问（与 GetTextMeasurer 同模式；
	/// 返回抽象接口——实现是 Win32PlatformWindow，框架层零 Win32 类型）
	PlatformWindow& GetPlatformWindow() noexcept;
```

## 6. SetFont（B8/C6：Font 值语义已确认）

```cpp
// TextWidget.h public（SetTextColor 附近）：
	/// @brief 设置字体（8.5.1；Font 是纯数据值语义——m_font = font 值拷贝）
	/// @details m_font 原为"预留"成员（Phase 5.5 注释：未来 SetFont 一行接入，OnPaint 零改动）——
	/// 8.5.1 兑现：赋值 + Invalidate。Label/Button 同享基类能力。
	void SetFont(const Font& font);

// TextWidget.cpp：
void TextWidget::SetFont(const Font& font){
	m_font = font;
	Invalidate();
}
```

**零 OnPaint 改动**：TextWidget::DrawTextContent 与 TextBox::OnPaint 已用 `m_font` 测量/绘制——SetFont 后自动生效。

## 7. 8.5.1 TestCase（TextBoxTests.cpp 新增，7.2 体系）

| # | 测试 | 断言点 | 说明 |
|---|---|---|---|
| F1 | InsertText 多码点插入 | m_text/caret 变化、空串 no-op | 粘贴核心 |
| F2 | InsertText 有 Selection 先删 | 选中区被替换为插入文本 | 粘贴覆盖选中区 |
| F3 | Composition 首帧占位 | UpdateComposition("nihao") → m_text 追加 "nihao"、m_isComposing=true | 模型 B 起始 |
| F4 | Composition 更新替换区间 | 首帧后 UpdateComposition("nihao2") → 区间替换非追加 | 模型 B 核心 |
| F5 | Composition 空串不结束 | UpdateComposition("") → m_isComposing **仍 true**（C7：空串 ≠ Commit） | C7 契约 |
| F6 | Composition 期间不触发 TextChanged | 注册回调，UpdateComposition 多次后回调未调用 | C8 契约（经 ReplaceTextRange，非 InsertText） |
| F7 | CommitComposition 正式生效 | UpdateComposition("nihao") → CommitComposition("你好") → m_text 区间替换、m_isComposing=false、回调触发 | C7/C3 契约 |
| F8 | Commit 无组合 no-op | 未组合时 CommitComposition → m_text 不变、无崩溃 | fail-safe |
| F9 | CancelComposition 擦除区间 | UpdateComposition("nihao") → CancelComposition() → m_text 恢复、m_isComposing=false | 预留接口 |
| F10 | Ctrl+A 全选 | SelectAll → GetSelection() == {0, 末尾} | 键盘路径 |
| F11 | CopySelectionToClipboard 无选区 no-op | 无选区调用 → m_text 不变、无崩溃 | 边界 |
| F12 | OnTimer 切换光标 | OnTimer(kCaretBlinkTimer) → m_showCaret 翻转 | 闪烁逻辑 |
| F13 | OnTimer 非本 id 忽略 | OnTimer(999) → m_showCaret 不变 | 多 timer 隔离 |
| F14 | Composition 中间替换（GPT 检查点 4） | 原 "abcDEF" caret=3 → UpdateComposition("nihao") → "abcnihaoDEF"；再 Update("你好") → "abc你好DEF"；Commit("你好") → "abc你好DEF"（组合不破坏前后文本） | 验证 start+length+ReplaceTextRange 协作 |
| F15 | Composition code point 索引（GPT 检查点 5） | 原 "你ABC好" caret 移动 → UpdateComposition("中文") → compositionStart/Length/caret 全部按 code point 而非 UTF-8 byte（"你"=3 字节 1 码点） | 索引单位契约 B10 |

**说明**：剪贴板真实读写（GetClipboardText/SetClipboardText 的 Win32 实现）需窗口——留待视觉验证（用户实测复制粘贴）；TestCase 覆盖 TextBox 侧逻辑（F10/F11 走 Ctrl 组合处理路径，剪贴板调用用 Fake 平台窗口 stub——若测试基建允许；否则标注"最小窗口集成待办"同 7.2 遗留）。

## 8. 8.5.1 视觉验证计划（用户 VS 编译运行）

1. **IME 组合串内嵌**：焦点 TextBox 拼音输入——组合串显示在 TextBox 内（非候选窗），带下划线；候选窗仍跟随光标（5.6 回归）
2. **IME 上屏回归**：确认后汉字进入文本；Undo（8.5.3 前不测）
3. **剪贴板**：Ctrl+A 全选 → Ctrl+C → 粘贴到记事本验证；记事本复制 → Ctrl+V 进 TextBox；Ctrl+X 剪切
4. **光标闪烁**：焦点时 500ms 闪烁；失焦停止；无焦点控件不闪
5. **SetFont**：main.cpp 临时调用 SetFont（⚠️ 验证后清除——main 是测试入口允许）——字号/字体变化生效
6. **回归**：中文上屏、光标移动、拖选、编辑操作无视觉回归

---

## 11. GPT 评审回应（v1.1，C7-C10 契约）

> GPT 评审时间：2026-08-24 21:00
> 评审结论：**8.5.1 架构 90% 成熟，但 IME Composition 有 2 个关键契约未闭合（Update/Commit 识别 + Composition 副作用），必须在实现前修掉**。已全部落地。

### C7：IME Commit 必须显式识别（❌→✅）
- **问题**：原设计把"空组合串（GCS_COMPSTR==""）= Commit"——不严谨；Win32 必须区分 GCS_COMPSTR（组合中）与 GCS_RESULTSTR（最终结果）
- **修正**：平台层 `lParam & GCS_RESULTSTR` → `OnIMECompositionCommit(resultText)`；`lParam & GCS_COMPSTR` → `OnIMECompositionUpdate(compositionText)`（空串只表示组合中无内容，组合仍在）；TextBox 新增 `CommitComposition(resultText)`（见 §4.1/§4.2/§4.3/§4.4）

### C8：Composition 不得调用正式编辑 API（❌→✅）
- **问题**：原设计 `UpdateComposition` 首帧调 `InsertText`——InsertText 带 RaiseTextChanged 副作用，与 F6/C3 冲突（代码逻辑直接矛盾）
- **修正**：抽取 **`ReplaceTextRange(startCp, endCp, replacement)`** 纯文本模型操作（无副作用）；`InsertText`（正式编辑：全副作用）与 `UpdateComposition`（临时编辑：仅 Invalidate+Sync）各自经 ReplaceTextRange 组合语义，不再互相调用（见 §4.4）

### C9：Composition caret 接口不得半成品（⚠️→✅ 明确化）
- **问题**：数据模型有 m_compositionCaret 但接口没传（平台只传 compositionText）
- **修正**：8.5.1 **明确不提取 GCS_CURSORPOS**——m_compositionCaret 固定组合末尾（简单输入成立）；8.5.2 组合内移动时接入，UTF-16→code point 转换完全在 Win32PlatformWindow（不泄漏 UTF-16 单位）（见 §4.1 C9 契约）

### C10：Clipboard SetClipboardData 失败释放（⚠️→✅）
- **问题**：SetClipboardData 失败时 hData 仍属调用方，不释放会泄漏
- **修正**：`if (SetClipboardData(...) == nullptr) GlobalFree(hData);` + 局部 `ClipboardGuard` RAII 统一 OpenClipboard/CloseClipboard 配对（非 ClipboardManager，YAGNI）（见 §3.2）

### 采纳的观察项
- **Timer ID Window 级限制**：契约注明 8.5.1 焦点模型保证不并发，Phase 9 多 timer 时不同 id 隔离（C11）
- **Window::GetPlatformWindow() 可接受**：GPT 判定非 8.5.1 blocker（架构洁癖级，YAGNI 不要求改）
- **8.5.2 m_lineStarts 单位**：code point index，取内容须经 CodepointIndexToByteOffset 转 byte 再 substr（与 B10 索引单位契约一致）

## 12. 实施前检查点核实（GPT 5 项，2026-08-24 已全部处理）

| # | 检查点 | 状态 | 核实/落地 |
|---|---|---|---|
| 1 | ImmGetCompositionStringW 返回值按 LONG 处理 | ✅ 已落地 | §4.1：`const LONG len` + `len > 0` 判负 + 转 DWORD 传入 |
| 2 | Focus 生命周期与 Timer Start/Stop 顺序 | ✅ 已核实 + 防御 | Window.cpp:152 核实：旧 OnFocusLost → 新 OnFocusGained（Stop 先于 Start）；OnTimer 加 `if (!HasFocus()) return;` 防御排队消息 |
| 3 | Timer 派发链核实 | ✅ 已核实修正 | Application.cpp 同 OnCharInput 模式：`Application::OnTimer → FindFocusedWidget → target->OnTimer`（非 Window::DispatchTimerEvent）——§5.4 修正 |
| 4 | 补 F14：Composition 中间替换 | ✅ 已补 | §7 F14：原 "abcDEF" caret=3 → Update/Commit 不破坏前后文本 |
| 5 | 补中文/Emoji code point 索引测试 | ✅ 已补 | §7 F15："你ABC好" + UpdateComposition("中文") 按 code point 断言 |

**附加契约（GPT 本轮补充）**：C12——`CommitComposition("")` 是合法 Commit（空结果提交）；取消必须走 `CancelComposition()`（§4.4）。

## 13. 与既有约束的对齐（8.5.1 落地）

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | 剪贴板/Timer/IME 组合串提取全在 PlatformWindow 层；TextBox 零 Win32 类型 |
| skill 16 Event 原则 | TimerEvent 只带 timerId（事实）；剪贴板不是 Event（C1）；IME 组合走 Host 回调非 Event 系统 |
| skill 22 分层论证 | 契约语言描述（"TimerEvent = 某定时器触发"）；WM_* 细节只在平台实现层 |
| skill 10 宏防护 | Win32PlatformWindow.h 已有 DrawText undef；新增代码不新增方法名冲突 |
| skill 11 字符串 | 公共 API UTF-8；Win32 边界 UTF8ToWide/WideToUTF8（剪贴板/组合串/结果串提取） |
| skill 3 原子授权 | 本设计文件清单 15 个文件——实施前用户确认 |
| 资源类禁复制禁移动 | TextBox 新增成员均值语义（string/size_t/bool）；TimerEvent 值语义；ClipboardGuard 禁复制 |
| 双工程同步 | vcxproj + CMakeLists 同时加 TimerEvent.h |

## 14. 修订记录

- v1.3（2026-08-24）8.5.1 完结（commit 8ab8300）+ **8.5.2 定稿**（§9 v1.0：多行 RecalculateLines/LineRange、滚动 OnMouseWheel/EnsureCaretVisible、CaretIndexFromPosition（B9）、双击选词平台层 WM_LBUTTONDBLCLK 翻译 + WindowClass CS_DBLCLKS、多行绘制 + 组合串下划线补欠账、F16-F25）。
- v1.2（2026-08-24）GPT 第三轮评审整合（"设计可以冻结，进入实现"）：§4.1 ImmGetCompositionStringW 改 LONG 判负；**C12 Commit("") 合法/取消走 Cancel**；**§5.4 Timer 派发链修正**（Application::OnTimer → FindFocusedWidget，代码事实核实——同 OnCharInput 模式，非 Window::DispatchTimerEvent）；§5.6 OnTimer 加 HasFocus 防御（Focus 顺序已核实 Window.cpp:152）；TestCase 补 F14（中间替换）/F15（code point）；新增 §12 实施前检查点核实（GPT 5 项全处理）。
- v1.1（2026-08-24）GPT 评审整合（C7-C10）：IME **Update/Commit 分离**（GCS_COMPSTR/GCS_RESULTSTR + CommitComposition）；**ReplaceTextRange 抽取**（C8——UpdateComposition 不再调 InsertText，消除 TextChanged 副作用矛盾）；**C9 composition caret 明确**（8.5.1 固定组合末尾）；**C10 剪贴板失败释放 + ClipboardGuard RAII**；TestCase 重排 F3-F13（新增 Commit no-op/Cancel/空串不结束）；新增 §11 GPT 评审回应。
- v1.0（2026-08-24）8.5.1 定稿：剪贴板（3 方法）/IME Composition 模型 B/Timer+闪烁/SetFont/TestCase/视觉验证；8.5.2/8.5.3 草案。

