# Phase 19 · 鼠标事件维度补全（Mouse event dimensions）—— 初步设计

> 状态：**v1.1**（2026-09-23）——✅ **评审通过**（外部评审第二轮 2026-09-23，**无阻塞项**）
> 下一步：**详细设计**——须先消化 **§9 的四件事**；另见 §1.5 对本轮两条建议的处置
> 来源：需求稿 v1.0（**外部评审通过 2026-09-23，无退回项**）· `roadmap-deferred.md` **§7.9 #41** · 审计 `docs/framework-defect-audit.md` **§4 D-1**
> 定性：**契约错误修复**——"平台给了、被中途丢掉"
> 本稿任务：回答需求 §6 的 **Q1–Q6**，并把 D2 / D3 / D5 / D6 从"倾向"转为**定案**

---

## 1. 设计输入与基线

### 1.1 需求阶段已定（本稿不再讨论）

| 项 | 内容 |
|---|---|
| **R1 / R2 / R3** | 补「此刻按下的鼠标键集合」+「此刻的修饰键状态」；**四类鼠标事件同源**（Move / ButtonDown / ButtonUp / Wheel 都带） |
| **R4** | **原始值不归一化**——照抄平台位，不做语义结论（不提供 `IsDragging()`） |
| **R5** | **零破坏**（本稿 §1.3 对其口径做一次**精确化**） |
| **R6** | 「未按下」= 空，**不引入三态**（有 / 无 / 未知） |
| **D0** | 修饰键**复用 `KeyModifier`**（不新造 `MouseModifier`） |
| **D1** | 两维**落在基类 `MouseEvent`** |
| **D4** | **不简化** `ScrollBar::m_dragging` / `TextBox::m_mouseDown`（另有捕获 / 拖选职责） |
| **N1–N6** | 非目标（控件级拖拽 / OLE 拖出 / 全局热键 / hover 与双击计数 / 改两处控件 / Alt） |

### 1.2 代码基线（B1–B12，2026-09-23 逐条实测）

| # | 事实 | 证据 |
|---|---|---|
| **B1** | `MouseEvent` 基类：成员仅 `m_mouseX` / `m_mouseY`；构造为 **`protected`**，形参 `(Window*, int, int)`——★ 是**指针**不是引用 | `MouseEvent.h:18-23` · `:41-44` |
| **B2** | 五个具体类的构造签名（本项要动的全部）：`MouseMoveEvent(window,x,y)` · `MouseButtonEvent(window,x,y,button)`〔protected 中间类〕· `MouseButtonDownEvent(window,x,y,button,**bool isDoubleClick=false**)` · `MouseButtonUpEvent(window,x,y,button)` · `MouseWheelEvent(window,x,y,delta)` | `MouseMoveEvent.h:26-32` · `MouseButtonEvent.h:18-25` · `MouseButtonDownEvent.h:19-25` · `MouseButtonUpEvent.h:15-21` · `MouseWheelEvent.h:32-39` |
| **B3** ★ | **翻译器构造点为 5 处，不是 4 处**——需求 K10 漏了**双击**分支（`WM_LBUTTONDBLCLK` → `MouseButtonDownEvent`） | `WindowMessageHandler.cpp:97`（移动）· `:114`（按下组）· **`:130`（双击）** · `:149`（抬起组）· `:173`（滚轮） |
| **B4** ★ | **测试侧构造点为 22 处**（6 文件）——这是 R5「逐字不动」的**真正影响面**；**消费者侧 0 处构造**（只读不造） | `ScrollViewTests.cpp` 13 · `CaptionBarTests.cpp` 4 · `TextBoxTests.cpp` 4 · `WidgetTests.cpp` 4 · `CheckBoxTests.cpp` 2 · `EventTests.cpp` 1 |
| **B5** ★★ | **本项目已有「给鼠标事件加维度而不破坏调用点」的现成落款**：`MouseButtonDownEvent` 的 `bool isDoubleClick = false`（Phase 8.5.2）——且全库**无任何调用点传第 5 实参**（全靠默认值）⇒ **Q3 不是新设计，是沿用既有局部解法** | `MouseButtonDownEvent.h:24`；全库 grep 第 5 实参 = 0 命中 |
| **B6** ★★ | **修饰键在键盘侧走的是 `GetKeyState`，且已含 Alt**：`TranslateModifier()` 是**文件局部静态函数**（不在头里），逐个 `GetKeyState(VK_SHIFT/VK_CONTROL/VK_MENU) & 0x8000` | `WindowMessageHandler.cpp:24-36` · 调用点 `:198` |
| **B7** | `KeyEvent` 的既有词汇：`bool HasModifier(KeyModifier)`（**位与 == 全含**语义）+ 三个快捷 `IsShiftDown` / `IsCtrlDown` / `IsAltDown`；**没有** `GetModifiers()` | `KeyEvent.h:22-33` |
| **B8** ★★ | **无窗口翻译测试的基础设施已存在**：`EventTests.cpp` 内有 `FakeHost : public PlatformWindowHost`（`OnEvent` 里**值拷贝**成 `ReceivedEvent`，避开悬垂），用 `WindowMessageHandler handler(host); handler.Handle(nullptr, nullptr, msg, wParam, lParam);` 直驱——**且已处理 `MouseButtonDown` / `MouseMove` 两个分支** | `EventTests.cpp:9`（include 内部头）· `:28-44` · `:57-105` · `:111` · `:143` |
| **B9** ★★ | **决定性**：该文件注释明写「modifier **依赖真实键盘状态**（`TranslateModifier` 走 `GetKeyState`），**不断言**」⇒ **`GetKeyState` 源在无头测试里不可断言** | `EventTests.cpp:105-106` |
| **B10** | 滚轮分支用 `ScreenToClient(hwnd, &point)` 转坐标 | `WindowMessageHandler.cpp:166` |
| **B11** | `MouseWheelEvent` 只有单轴 `int GetDelta()`（无 `deltaX`） | `MouseWheelEvent.h:41-46` |
| **B12** | Win32 侧位值（Q2 用）：`MK_LBUTTON` 0x0001 · `MK_RBUTTON` 0x0002 · **`MK_SHIFT` 0x0004** · **`MK_CONTROL` 0x0008** · `MK_MBUTTON` 0x0010 · `MK_XBUTTON1` 0x0020 · `MK_XBUTTON2` 0x0040；`XBUTTON1` 0x0001 · `XBUTTON2` 0x0002；**无 `MK_ALT`** | `<winuser.h>` 常量 |

### 1.3 ★ 对评审意见的逐条处置

外部评审（2026-09-23）**结论为通过、无退回项**，并列了 6 项初设应解决的重点。逐条处置：

| # | 评审条目 | 处置 |
|---|---|---|
| 1 | **通过，可进入初步设计** | ✅ 接受。需求稿**内容不改**，仅头部状态改为「✅ 评审通过（2026-09-23）」+ 修订记录 **v1.1** |
| 2 | 初设重点① **D2：`PressedButtons` 的公共 API 表示** | **定案 §2.1**（私有位掩码 + 单一谓词查询，**零新公共类型**） |
| 3 | 初设重点② **Q2：XBUTTON 低位状态 vs 高位具体按钮** | **定案 §3.3**（含位值表；**只读 `LOWORD`** 写成契约 C2） |
| 4 | 初设重点③ **Q3：零破坏传参** | **定案 §3.2**——★ **沿用 B5 的既有先例**（带默认值尾形参），非新设计 |
| 5 | 初设重点④ **Q4：测试走直接构造还是翻译路径** | **定案 §7**——★ **走 `FakeHost` + `Handle()` 真实翻译路径**（B8 基础设施已存在）；手工构造**不作为 D-1 的验收证据** |
| 6 | 初设重点⑤ **Q6：复用 `KeyModifier` 后如何处理没有 Alt** | **定案 §2.3**——★ **理由按 B6 / B9 重写**（见下"纠正 3"） |
| 7 | 初设重点⑥ **R7 是否保持独立** | ✅ 接受「不并入」，**D6 定案 §2.4** |
| 8 | 评审写 `MouseEvent(Window& window, int mouseX, int mouseY)` | ★ **纠正**：实为 **`Window*`（指针）** — `MouseEvent.h:19`。不影响任何结论（现有调用点本就传 `nullptr` / 取地址） |
| 9 | 评审转述需求「现有构造点 / 消费点**逐字不动**」 | ★ **需精确化**：**翻译器 5 处必然修改**（那正是本项的实现本体）；**不动的** = 测试 **22 处**（B4）+ 全部消费点。需求 R5 的字面表述过宽 ⇒ 本稿 §4-C5 给出精确口径 |
| 10 | 评审认可「不做 Alt，因为要额外查 `GetKeyState`」 | ★ **理由需重写**：本框架**已经在用** `GetKeyState`（B6，键盘侧修饰键）⇒「拒绝 `GetKeyState`」**不是**理由。真因是**源的选择 + 可断言性**（§2.3 / B9） |
| 11 | 评审建议「把 `MK_XBUTTON` 与 `GET_XBUTTON_WPARAM` 画成小表」 | ✅ 已采纳 → §3.3 |
| 12 | 评审未提 | ★ **补充勘误**：需求 **K10「翻译器 4 处」→ 实测 5 处**（B3）；**R5 影响面 22 处**（B4）。两处回写需求稿 v1.1 |

### 1.4 需求 §6 的 Q1–Q6 → 本稿答案索引

| Q | 问题 | 答案 |
|---|---|---|
| **Q1** | D2 的候选形态比较（位掩码 / `MouseButton` 集合语义 / 一组 bool / 小集合类型） | **§2.1**（对照表 + 定案 + 逐项否决理由） |
| **Q2** | ★ `MK_XBUTTON` 低位 / `GET_XBUTTON_WPARAM` 高位陷阱 | **§3.3**（表 + 契约 C2 + 用例 T19-4 把它钉死） |
| **Q3** | 构造点如何最小改动传参 | **§3.2**（带默认值尾形参；**先例 B5**；影响面 B4） |
| **Q4** | 测试承载：直接构造 vs 翻译器 + FakeHost | **§7**（**翻译路径**；基础设施 B8 已在，零新建） |
| **Q5** | R7 若做横向滚轮，"轴"加在哪 | **§2.4**（本项**不做**，只记接缝；届时**倾向新增 `deltaX` 形参**而非新事件类型） |
| **Q6** | 与 `KeyModifier` 复用后 Alt 的一致性措辞 | **§2.3**（**不提供** `IsAltDown`；`HasModifier(Alt)` **恒 false**，头注释显式声明 + 用例 T19-7 钉住） |

---

### 1.5 ★ 第二轮评审（初设）的处置

外部评审（2026-09-23，第二轮）**结论：通过，无阻塞项**；指出 **2 处应在进详设前钉死**。逐条处置：

| # | 评审建议 | 处置 |
|---|---|---|
| **R1** | ★ `unsigned int pressedButtons` **虽存 private，却出现在公共构造签名里** ⇒ 文档须说清它是「**内部翻译 / 构造通道参数**」而非推荐的公共语义类型；并区分「消费接口 `IsButtonDown`」与「构造接口 `mask`」 | ✅ **采纳** → **新增 §3.6**（三层分工表 + 不给命名类型的理由 + **升级触发条件**）+ §3.1 头注释补 `@note` |
| **R2** | ★ 须把「**当前 Win32 映射不支持 Alt**」与「**Event 模型不支持 Alt**」**彻底区分**，避免写成后者 | ✅ **采纳** → **§2.3 新增两层对照表 + 措辞红线**（❌"MouseEvent 不支持 Alt" / ✅"当前 Win32 鼠标映射不从 `wParam` 产生 Alt"） |

**评审确认「原样保留」的部分**（本稿不动）：**D2** 私有掩码 + 谓词 · ★ **不得把 `MouseButton` 改成位标志枚举**（"一个键"与"一组键"必须分层；键盘侧 `KeyCode`/`KeyModifier` 即此先例）· **Q2** 的 `LOWORD`/`HIWORD` 分离（含 `XBUTTON2` = `MK_RBUTTON` 的**具体误认后果**）· **Q3** 尾默认参先例 + "**默认实参不继承**"提醒 · **R5 精确口径** · **D3 的三条理由**（评审确认其上一轮"需要额外 `GetKeyState`"的说法**确不准确**）· **P1/P2** 的"待实测而非写成事实"纪律 · 测试走**真实翻译路径**（**手工构造只能证明数据结构，不能证明平台信息没被丢掉**）· **T19-8** 把 `LOWORD`/`HIWORD` 放同一消息 · 影响面 231→240 / 92→92 / API +2。

---

## 2. ★ 核心定案（本稿重点）

### 2.1 D2「此刻按下的键集合」的公共 API 表示

需求 D2 留了四个候选。**定案：私有位掩码 + 单一谓词查询**。

```cpp
// MouseEvent.h（新增部分）
public:
	/// @brief 此刻指定的鼠标键是否处于按下状态（原始事实——不做"拖动"之类语义结论）
	bool IsButtonDown(MouseButton button) const noexcept{
		return (m_pressedButtons & (1u << static_cast<unsigned int>(button))) != 0u;
	}

private:
	unsigned int m_pressedButtons = 0;   ///< 位序 = MouseButton 枚举序（Left=0 … X2=4）
```

| 候选 | 判定 | 理由 |
|---|---|---|
| **① 私有位掩码 + `IsButtonDown()`** ★**采纳** | ✅ | **零新公共类型 / 零新词汇**（公共头 92→92，无 `MouseMask` 之类新名字）；**不泄漏平台位**（消费者永远不写 `MK_LBUTTON`）；**不含语义结论**（R4）；**可扩展**（加键不动 API）；**存储紧凑** |
| ② 把 `MouseButton` 改成**位标志枚举** | ❌ | 会让"**一个键**"（`GetButton()`）与"**一组键**"共用同一类型 ⇒ **同一词两个概念**，正是「一个概念只用一个词」的反面。对照键盘侧：`KeyCode`（哪个键）与 `KeyModifier`（集合）**是分开的两个类型** |
| ③ 一组 `bool`（`leftDown` / `rightDown` …） | ❌ | 5 个 bool ⇒ **构造形参爆炸**（基类 +5、5 个子类各 +5）；新增一个键就要改签名 ⇒ 违 R5；`IsButtonDown` 退化成 `switch` |
| ④ 小型集合类型（`std::set<MouseButton>` 或自造） | ❌ | 为一个"5 位掩码即可"的语义引入容器/新类型：动态分配或额外模板实例，**收益为负**；且当前**无任何消费者要遍历集合**（YAGNI） |

★ **与键盘侧的对称说明**（"一个概念一个词"的落地）：键盘的「哪个键」是 `KeyCode`、「集合」是 `KeyModifier`（**枚举位标志**——因为修饰键集合只有 3 个值且语义固定）；鼠标的「哪个键」是 `MouseButton`、「集合」是**掩码**——**形式不同但概念同名同位**：都是"某类输入里，此刻处于按下状态的那一维"。掩码不进公共 API，所以不构成"第二个词"。

### 2.2 D5 命名定案（Q6 的措辞也在此）

| 决定 | 内容 |
|---|---|
| 谓词 | **`IsButtonDown(MouseButton)`** —— 与 `IsShiftDown()` 一族读感一致，但它**接受参数**（键是变量，修饰键是固定三个） |
| 修饰键 | **只给 `HasModifier(KeyModifier)`**，与 `KeyEvent::HasModifier` **同名、同义、同实现**（位与 == 全含） |
| ❌ 不给 `GetModifiers()` | 键盘侧**也没有**（B7）⇒ 双侧一致；且无消费者需要取出整个值 |
| ❌ 不给 `IsShiftDown` / `IsCtrlDown` / `IsAltDown` | **"成套或不给"**：三兄弟里 `IsAltDown` 必然**恒 false**（§2.3）⇒ 给了就是**半套陷阱**。`HasModifier(KeyModifier::Ctrl)` 已足够可读，且**成套** |

### 2.3 D3 + Q6：修饰键的**源**与 Alt 的有意缺位

**定案：鼠标修饰键取自 `wParam` 的 `MK_SHIFT` / `MK_CONTROL`；`Alt` 不置位。**

⚠️ 本稿**修订了需求稿给出的理由**。需求 §1.4 排除的是"**消费侧** `GetKeyState`"——这仍然成立；但"不做 Alt"**不能**再表述为"框架拒绝 `GetKeyState`"，因为**平台层已经在用**它（B6）：

```cpp
// WindowMessageHandler.cpp:24-36（键盘侧现状，不动）
KeyModifier TranslateModifier(){              // ← 文件局部；走 GetKeyState
	... AddIfDown(VK_SHIFT, Shift); AddIfDown(VK_CONTROL, Ctrl); AddIfDown(VK_MENU, Alt); ...
}
```

于是鼠标侧实际是**两个源之间的选择**，而不是"有 / 没有"：

| 源 | 含 Alt | 时刻性 | 无头可断言（B9） | 与键盘侧 |
|---|---|---|---|---|
| **`wParam` 的 `MK_*`** ★**采纳** | ❌ | **事件时刻的权威值**（平台为这条消息准备） | ✅ **确定** | 来源不同 |
| `TranslateModifier()`（`GetKeyState`） | ✅ | **查询时刻**（消息积压时 ≠ 事件时刻） | ❌ **不可断言** | 来源相同 |

**采纳 `wParam` 的三条理由**：

1. ★ **R4 的精神**：`wParam` 就是"**平台为这条消息给出的状态**"——照抄它即"原始值不归一化"。用 `GetKeyState` 反而把**一个"查询时刻的推断"混进"事件时刻的事实"**，与 R4 相悖。
2. ★ **平台所迫 ≠ 设计偏好**：`WM_KEYDOWN` 的 `wParam` 是**虚拟键码**，**根本不携带修饰键位**（这属于键码而非状态）⇒ 键盘侧走 `GetKeyState` 是**平台限制**，不是选择。鼠标有条件拿准，**没有理由主动选近似**。
3. ★★ **可断言性（决定性）**：`GetKeyState` 源在无头测试里**不可断言**——`EventTests.cpp:105-106` 已为此写明"**不断言**"（B9）。若鼠标也走它，则验收 **A1 / A2 无法自动化**，只剩目视；而 A1/A2 正是本项的验收核心。

**Alt 缺位 = 有意定义的平台映射边界**（不是漏实现）：`WM_*MOUSE*.wParam` **不提供 `MK_ALT`**（B12）。**用户可见后果**（Q6 要求的措辞）：

- 头注释在构造与 `HasModifier` 两处**显式声明** `Alt` 恒不置位（否则 `HasModifier(KeyModifier::Alt)` 是一个静默陷阱）；
- **不提供** `IsAltDown()`（§2.2）⇒ 不存在"提供了却不工作"的暗示；
- **用例 T19-7** 把"恒 false"钉成**回归契约**（将来有人误判为 bug 时，测试会告诉他这是有意为之）。

★★ **措辞纪律（★ 采纳第二轮评审建议 2）：必须区分「模型不支持」与「当前映射不产生」**——两者**不可混写**：

| 层 | 事实 |
|---|---|
| **Event 模型** | **支持 `KeyModifier::Alt` 这一概念**——`KeyModifier` 本就含 `Alt = 4`（`KeyModifier.h:10`），`HasModifier()` 的位与实现也天然支持它 |
| **当前 Win32 鼠标映射** | **不产生** Alt——`WM_*MOUSE*.wParam` 不携带 `MK_ALT`（B12） |
| ⇒ **推论** | `MouseEvent::HasModifier(KeyModifier::Alt)` 当前**恒 false**，是**映射边界**所致，**不是**事件模型缺少 Alt |
| ⇒ **演进含义** | 将来出现 Alt+鼠标 的真实消费者 ⇒ **只改 Win32 翻译器**（见下重启条件），**Event API 无需二次设计** |

- ❌ **禁止写法**：「MouseEvent **不支持** Alt」·「Alt 不属于鼠标事件」——会被读成**事件模型永远不支持**。
- ✅ **规范写法**：「**当前 Win32 鼠标映射**不从 `wParam` 产生 Alt」·「Alt 恒 false = **当前映射的边界**」。

**重启条件（记入 `roadmap-deferred.md` #41 备注）**：出现**真实消费者**需要 Alt+拖（如 Alt 拖复制）⇒ 届时**只改平台层**：在 `TranslateMouseModifiers` 里补 `GetKeyState(VK_MENU)` 一位，并**在头注释写明"该位为查询时刻"**。★ **不得**在 Event 层引入任何 Win32 查询。

### 2.4 D6 / R7：横向滚轮（#36）不并入本项

- **本项不做** `WM_MOUSEHWHEEL` 翻译——它是"新增一条平台消息翻译"，与"补维度"是两个实现任务。
- **接缝记录（Q5）**：`MouseWheelEvent` 现为单轴 `GetDelta()`（B11）。将来做 #36 时，**倾向新增 `int deltaX = 0` 尾形参**（沿用本稿 §3.3 的同一套零破坏手法），而**不是**新增事件类型——因为"滚轮"是一个概念，横向只是它的另一根轴（「一个概念只用一个词」）。
- 本项**不为其预留任何形参**（YAGNI；将来加仍零破坏）。

---

## 3. 接口改动分解

### 3.1 公共头改动（逐文件）

**公共头数 92 → 92**（只改既有头，**无新头**）。新增 include：`MouseEvent.h` 需引 `MouseButton.h` 与 `KeyModifier.h`（两者均已是公共头）。

```cpp
// ① MouseEvent.h —— 基类（本项的核心；protected 构造 + 两个谓词 + 两个成员）
class MouseEvent : public InputEvent {
protected:
	/// @param pressedButtons 此刻按下的鼠标键位掩码（位序 = MouseButton 枚举序；0 = 无键按下）
	/// @note ★ 这是**低层构造通道参数**，**不是**推荐消费者使用的语义接口——消费侧请用 IsButtonDown()。
	///       掩码只为两件事存在：① 平台翻译器（WindowMessageHandler）填入；② 既有测试构造点保持兼容。
	///       请勿把 `1u << 序号` 写进业务逻辑；完整分工见初步设计 §3.6。
	/// @param modifiers      此刻的修饰键状态（与 KeyEvent 同类型同语义）
	/// @note **Alt 恒不置位**——**当前 Win32 鼠标映射**不从 wParam 产生 Alt（**非**事件模型不支持 Alt）。
	///       理由、措辞纪律与重启条件见初步设计 §2.3。
	MouseEvent(Window* window, int mouseX, int mouseY,
	           unsigned int pressedButtons = 0,
	           KeyModifier modifiers = KeyModifier::None);

public:
	int GetMouseX() const noexcept;          // 不动
	int GetMouseY() const noexcept;          // 不动
	bool IsButtonDown(MouseButton button) const noexcept;      // ★ 新增
	bool HasModifier(KeyModifier modifier) const noexcept;     // ★ 新增（与 KeyEvent 同名同义）

private:
	int m_mouseX, m_mouseY;                  // 不动
	unsigned int m_pressedButtons = 0;       // ★ 新增
	KeyModifier m_modifiers = KeyModifier::None;   // ★ 新增
};
```

```cpp
// ②–⑥ 五个具体类：**仅在尾部追加两个带默认值的形参**，原有形参顺序/类型不动
MouseMoveEvent     (Window*, int x, int y,
                    unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None);
MouseButtonEvent   (Window*, int x, int y, MouseButton button,          // protected 中间类
                    unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None);
MouseButtonDownEvent(Window*, int x, int y, MouseButton button, bool isDoubleClick = false,
                    unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None);
MouseButtonUpEvent (Window*, int x, int y, MouseButton button,
                    unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None);
MouseWheelEvent    (Window*, int x, int y, int delta,
                    unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None);
```

★ **注意 `Down` 类**：新形参追加在既有的 `isDoubleClick` **之后**——因为它俩都是尾默认参，顺序只需**保持既有形参相对次序不变**（B5：无调用点传第 5 实参 ⇒ 追加位置安全）。

### 3.2 Q3 定案：零破坏传参 = 带默认值的尾形参（**沿用既有先例**）

**这不是新设计**——本项目在 Phase 8.5.2 已经用同一手法给 `MouseButtonDownEvent` 加过 `isDoubleClick`（B5），且**零调用点改动**。

**为什么默认值是强制项而非可选**：影响面 **22 处测试构造点**（B4）——若不设默认值，这 22 处全部编译失败 ⇒ 直接违 R5。设默认值后：

- 22 处测试 + 全部消费点 **逐字不动**（编译即验证）；
- 默认值语义 = **空状态**（`0` / `None`）⇒ 这些调用点**行为逐位不变**（契约 C4）。

★ **一个必须写明的语言细节**：**默认实参不通过继承传递**。基类 `MouseEvent` 的默认值**不会**自动出现在子类构造上 ⇒ **五个具体类必须各自带上这两个默认形参**（§3.1 ②–⑥ 已列全）。这是本项唯一容易漏的一处。

### 3.3 Q2：`MK_XBUTTON` 的两个维度（陷阱表）

| 维度 | 取法 | 含义 | 用途 |
|---|---|---|---|
| **按下的键（状态）** | **`LOWORD(wParam)`** 的位 | `MK_LBUTTON` / `MK_RBUTTON` / `MK_MBUTTON` / **`MK_XBUTTON1` / `MK_XBUTTON2`** | ⇒ **`pressedButtons` 掩码**（本项新增） |
| **本次是哪个键（事件）** | **`HIWORD(wParam)`** = `GET_XBUTTON_WPARAM(wParam)` | `XBUTTON1` / `XBUTTON2` | ⇒ `MouseButtonEvent::GetButton()`（**既有，不动**） |

**陷阱**：`GET_XBUTTON_WPARAM` 的返回值（`0x0001` / `0x0002`）**看起来**很像"X1/X2 的按下位"，但它只回答"**这一次是哪个 X 键**"。把它 OR 进掩码 ⇒ **X1/X2 必然错其一**（`XBUTTON2` = 0x0002 恰好等于 `MK_RBUTTON` 的值 ⇒ 甚至可能点亮**右键**）。

**因此定下契约 C2（单一读取点）**：`wParam` 只被两个新 helper 读，且**只读 `LOWORD`**；`GET_XBUTTON_WPARAM` **仅**出现在既有的 `TranslateMouseButton`（`:290-320`）里。用例 **T19-4** 用 `wParam = (XBUTTON2 << 16) | MK_XBUTTON1` 一次钉死两个维度。

### 3.4 平台层改动（`WindowMessageHandler.cpp`）

新增**两个文件局部函数**（与既有 `TranslateModifier` / `TranslateMouseButton` 同风格；**不进头**、不新增公共类型）：

```cpp
/// @brief 取「此刻按下的鼠标键」掩码（位序 = MouseButton 枚举序）
/// @note 只读 LOWORD(wParam)——WM_MOUSEWHEEL 的 HIWORD 是 delta、WM_XBUTTON* 的是 XBUTTON1/2；
///       读整个 wParam 会把它们误当按下位（见初步设计 §3.3）。
unsigned int TranslateMouseButtons(WPARAM wParam){
	const WORD bits = LOWORD(wParam);
	unsigned int mask = 0;
	const auto AddIf = [&mask, bits](WORD mk, MouseButton b){
		if (bits & mk) mask |= 1u << static_cast<unsigned int>(b);
	};
	AddIf(MK_LBUTTON,  MouseButton::Left);
	AddIf(MK_RBUTTON,  MouseButton::Right);
	AddIf(MK_MBUTTON,  MouseButton::Middle);
	AddIf(MK_XBUTTON1, MouseButton::X1);
	AddIf(MK_XBUTTON2, MouseButton::X2);
	return mask;
}

/// @brief 取「此刻的修饰键」——**与键盘侧不同源**：此处走 wParam（事件时刻），不含 Alt
/// @note 键盘侧的 TranslateModifier() 走 GetKeyState（平台所迫：WM_KEYDOWN 的 wParam 是键码）。
///       两者**都不含**"消费侧查询"，分层不变。Alt 缺失见初步设计 §2.3。
KeyModifier TranslateMouseModifiers(WPARAM wParam){
	KeyModifier m = KeyModifier::None;
	if (LOWORD(wParam) & MK_SHIFT)   m = m | KeyModifier::Shift;
	if (LOWORD(wParam) & MK_CONTROL) m = m | KeyModifier::Ctrl;
	return m;
}
```

**5 个调用点**（B3）各补两个实参，形如：

```cpp
MouseMoveEvent event(window, x, y,
                     TranslateMouseButtons(wParam), TranslateMouseModifiers(wParam));
```

⚠️ **滚轮分支（`:173`）**：该分支已用 `wParam` 取 `GET_WHEEL_DELTA_WPARAM`（= `HIWORD`）——**注意别与 `LOWORD` 混**，两个 helper 内部各自取 `LOWORD`，互不干扰。

### 3.5 ⚠️ 待实测的平台语义（**不阻塞设计**，仅决定文档措辞）

| # | 待确认 | 为什么**不阻塞** |
|---|---|---|
| **P1** | `WM_LBUTTONDOWN` 的 `wParam` 是否**置** `MK_LBUTTON`（即"按下事件"的 `pressedButtons` 是否**已含**该键） | 本项是**照抄**（R4）⇒ **实现不依赖**该事实：平台给什么就是什么。仅影响头注释的示例措辞 ⇒ 详设/实现时用一段日志或 ModelProbe 目视确认即可 |
| **P2** | `WM_LBUTTONUP` 的 `wParam` 是否**不含** `MK_LBUTTON`（抬起后该位应消失） | 同上 |

★ 记法纪律：这两条在**实测前不得写成"事实"**（只写"待确认"）；若实测与预期不符，改动仅在**注释**，**代码与用例不变**（因为断言的是"`pressedButtons` == `wParam` 的低位映射"，不是"某键是否在内"）。

---

### 3.6 构造参数的通道性质（★ 采纳第二轮评审建议 1）

**评审指出的缺口**：`unsigned int pressedButtons` 虽然存进 private 成员，但**它出现在公共头的构造签名里**——使用者若手工构造，就得知道 `0x08` 代表什么。设计稿**没说清它是"内部通道"而非"公共语义"**。

**因此明确三层分工（本项定案）**：

| 层 | 接口 | 面向谁 | 需要理解位吗？ |
|---|---|---|---|
| **消费层**（公共语义） | `MouseButton` + `IsButtonDown(MouseButton)` · `HasModifier(KeyModifier)` | **应用 / 控件作者**（常态） | **不需要**——**永不写 `1u << …`，也永不写 `MK_*`** |
| **构造通道**（低层输入） | 构造尾形参 `unsigned int pressedButtons` | **平台翻译器**（`WindowMessageHandler`） | 需要，且**只在这一处** |
| **兼容通道** | 同上的**默认值**（`0` / `None`） | **既有 22 处测试构造点**（B4） | **不需要**——默认值即"空状态"（**契约 C4**）⇒ 这些调用点**逐字不动，也不必理解掩码** |

★ **为什么现在不给它一个命名类型**（与 YAGNI 一致，评审亦持同见）：`MouseButtonMask` 会**新增一个公共概念**（撞"最小 API 面"与"不引入投机抽象"），而它**唯一的使用者是平台层**——一边是"为一个消费者造类型"，另一边是"多一个要学的名字"。

★ **升级触发条件（何时才引入命名类型）**：出现**公共消费者**（应用 / 控件作者）需要**手工构造带按键状态的 `MouseEvent`**（例如写测试替身、做合成事件注入）⇒ **那时**才引入命名类型（`MouseButtonMask` 或等价物）；且**届时零破坏**——因为该形参的默认值是 `0`，追加一个取命名类型的重载即可，既有调用点不受影响。

**文档义务**：头注释必须写明该形参是**低层通道**（§3.1 的 `@note` 已落地）——否则读者看到 `unsigned int pressedButtons` 会误以为 ECDI 鼓励自己操作位掩码。

---

## 4. 契约（C1–C6）

> §3.6 的「三层分工」是**设计意图声明**（区分消费接口与构造通道）；其**可机检**的部分由 **C6**（`MK_*` 不出平台层）覆盖，故**不单列契约**。将来出现命名类型（§3.6 触发条件）时，位序约定才需要升格为独立契约。

| # | 契约 | 说明 / 可由什么检查 |
|---|---|---|
| **C1** | **位序契约**：`pressedButtons` 的位序 = **`MouseButton` 枚举序**（Left=0 · Right=1 · Middle=2 · X1=3 · X2=4）；**与 Win32 `MK_*` 的数值无关**（映射在平台层一次完成） | 头注释 + 用例 T19-1..T19-4（端到端钉住） |
| **C2** | **单一读取点**：`wParam` 只被 `TranslateMouseButtons` / `TranslateMouseModifiers` 读，且**只读 `LOWORD`**；`GET_XBUTTON_WPARAM` 仅用于 `TranslateMouseButton` | 代码可检（grep `GET_XBUTTON_WPARAM` 只应在 `TranslateMouseButton` 内）+ 用例 T19-4 |
| **C3** | **Alt 恒不置位**：鼠标路径**不得**出现 `GetKeyState`（有意边界） | grep + 用例 T19-7 |
| **C4** | **默认值 = 空状态**：不传尾部两参时 `pressedButtons = 0`、`modifiers = None` ⇒ 两个谓词**全 false**。**"没有信息"与"没有按下"不区分**（R6）⇒ 既有 22 处构造点**行为逐位不变** | 用例 T19-5 + 既有 231 用例全绿 |
| **C5** | **零破坏（精确口径）**：不新增纯虚；不改任何既存形参的**顺序与类型**；不删任何重载；**测试 22 处构造点 + 全部消费点逐字不动**。⚠️ **翻译器 5 处必然修改**（本项实现本体）——需求 R5 的"逐字不动"**不覆盖翻译器** | `git diff` 结构性核查 + 编译 |
| **C6** | **平台层零新增公共类型**：`MK_*` / `XBUTTON*` **不出** `src/Platform/Win32/`；Event 层只见 `MouseButton` + 掩码 + `KeyModifier` | 公共头 include 图 + 人工核 |

---

## 5. 影响面

| 面 | 规模 |
|---|---|
| **公共头** | **92 → 92**（改 6 个既有头：`MouseEvent.h` / `MouseMoveEvent.h` / `MouseButtonEvent.h` / `MouseButtonDownEvent.h` / `MouseButtonUpEvent.h` / `MouseWheelEvent.h`；**无新头**） |
| **公共 API 净增** | **+2**（`MouseEvent::IsButtonDown` · `MouseEvent::HasModifier`）· 另**构造形参 +12**（6 类 × 2，**均带默认值**——不计入净增，但需在详设列全） |
| **实现** | `src/Platform/Win32/WindowMessageHandler.cpp`：**+2 文件局部函数** · **5 个调用点各补 2 实参**（`:97` / `:114` / `:130` / `:149` / `:173`） |
| **测试** | `src/Tests/EventTests.cpp`：`ReceivedEvent` **+2 字段** · `FakeHost::OnEvent` 的 `MouseMove` / `MouseButtonDown` 分支**各补 2 行** · 新增 **T19-1..T19-9**。**其余 6 个测试文件零改动**（22 处构造点靠默认值） |
| **用例数** | **231 → 240**（+9） |
| **断言特征串** | **11 → 11**（本项**不新增断言**——纯追加，无新前提） |
| **构建** | 零改动（`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库；无新文件） |
| **新用例文件** | **不新建**（扩展 `EventTests.cpp`；该文件已有登记两处的先例） |
| **`examples/`** | **零改动**（A5 仅目视复用现有行为） |

---

## 6. 开放决策点（O 系列）

| # | 决策点 | 倾向 | 说明 |
|---|---|---|---|
| **O1** | 平台层用**两个文件局部函数**（`TranslateMouseButtons` / `TranslateMouseModifiers`）还是**一个返回小 struct 的**函数 | **两个** | 与既有 `TranslateModifier` / `TranslateMouseButton` 的"一函数一关切"风格一致；调用点仍是两实参、无需先声明局部变量 |
| **O2** | 将来是否需要把掩码**暴露**为集合查询（如"有没有任意键按着"） | **不暴露**（YAGNI） | 当前无消费者；真要时加一个谓词即可（零破坏） |
| **O3** | `MouseButtonEvent`（protected 中间类）是否也被 `Down`/`Up` 之外的类继承 | 无（现状） | 若将来出现，其形参已就位 |
| **O4** | 是否补 `IsShiftDown` / `IsCtrlDown` 快捷查询 | **不给**（§2.2 成套原则） | `HasModifier(KeyModifier::Ctrl)` 已足够可读；给了就会与"缺失的 `IsAltDown`"形成半套 |
| **O5** | 是否**并入** #36 横向滚轮 | **不并入**（D6） | §2.4 |
| **O6** | P1 / P2 两条平台语义的实测方式 | 详设定 | 一段日志或 ModelProbe 目视；**不影响代码与用例** |

---

## 7. 测试方向（T19-1..T19-9）

**承载方式（Q4 定案）：走 `FakeHost` + `WindowMessageHandler::Handle()` 的**真实翻译路径**。**基础设施已存在（B8），**零新建**。扩展 `ReceivedEvent` +2 字段：

```cpp
struct ReceivedEvent{
	EventType type = EventType::None;
	...                                   // 既有字段不动
	unsigned int pressedButtons = 0;      // ★ 新增
	KeyModifier modifiers = KeyModifier::None;   // ★ 新增
};
```

⚠️ **纪律**：**手工构造事件不得作为 D-1 的验收证据**——它只能证明"字段存得下"，**不能证明"平台事实完整抵达 Event 层"**。验收 A1/A2 **必须**经 `Handle()` 驱动；手工构造仅继续服务既有控件级用例（22 处，不动）。

| # | 驱动 | 断言 | 对应 |
|---|---|---|---|
| **T19-1** | `WM_MOUSEMOVE`，`wParam = MK_LBUTTON` | `MouseMove` · `IsButtonDown(Left)==true` · `IsButtonDown(Right)==false` | **A1** |
| **T19-2** | `WM_MOUSEMOVE`，`wParam = MK_LBUTTON \| MK_RBUTTON` | 左与右**同时** true（证明是"集合"而非"某一个键"） | **A1 / R1** |
| **T19-3** | `WM_LBUTTONDOWN`，`wParam = MK_CONTROL \| MK_SHIFT` | `HasModifier(Ctrl)==true` · `HasModifier(Shift)==true` · `HasModifier(Ctrl\|Shift)==true` · `GetButton()==Left`（**与新增维度正交**） | **A2 / R2** |
| **T19-4** ★ | `WM_XBUTTONDOWN`，`wParam = (XBUTTON2 << 16) \| MK_XBUTTON1` | `IsButtonDown(X1)==true`（来自 **LOWORD**）· `IsButtonDown(X2)==false` · `GetButton()==X2`（来自 **HIWORD**）——**一次钉死两个维度** | **Q2 / C2** |
| **T19-5** | `WM_MOUSEMOVE`，`wParam = 0` | 两谓词**全 false**（空 = 空，非"未知"） | **R6** |
| **T19-6** | `WM_MOUSEMOVE`，`wParam = MK_SHIFT` | `HasModifier(Shift)==true` · `HasModifier(Ctrl)==false`（修饰键**与按键位互不串味**） | **R2** |
| **T19-7** | `WM_MOUSEMOVE`，`wParam = MK_LBUTTON` | `HasModifier(KeyModifier::Alt)==false`（**Alt 恒不置位 = 有意边界，钉成回归契约**） | **D3 / C3** |
| **T19-8** | `WM_MOUSEWHEEL`，`wParam = (WHEEL_DELTA << 16) \| MK_CONTROL` | `HasModifier(Ctrl)==true` · `GetDelta()==120`（★ **HIWORD=delta 与 LOWORD=状态 并存**——滚轮是 LOWORD/HIWORD 最易混的一条） | **R3 / C2** |
| **T19-9** | `WM_LBUTTONUP`，`wParam = 0` | `IsButtonDown(Left)==false`（抬起事件的按下集**不含**被抬起键） | **R3** |

**两条平台约束（B10 / B9）**：
- **滚轮坐标**在 `FakeHost` 路径下**不可断言**（`hwnd=nullptr` ⇒ `ScreenToClient` 不生效）⇒ T19-8 **只断言状态位与 delta**，不断言坐标。
- **修饰键可断言**——这正是选 `wParam` 源（§2.3 理由 3）的直接收益：若走 `GetKeyState`，T19-3 / T19-6 / T19-7 全部**无法自动化**。

---

## 8. 验收（需求 A1–A5 的落地口径）

| # | 需求判据 | 本稿落地 |
|---|---|---|
| **A1** | 按住左键移动：订阅方**从移动事件本身**得知"左键按着" | **T19-1 / T19-2**（经 `Handle()` 真翻译路径）；**不得**用手工构造充当 |
| **A2** | 按住 Ctrl / Shift 点击：按键事件能读到修饰键 | **T19-3 / T19-6**（同上） |
| **A3** | 零回归：既有用例全绿 | `ecdi_tests` **240 全绿**（231 既有 + 9）；**须写明断言是否启用** |
| **A4** | 零破坏（结构性） | **精确口径见 C5**：`git diff` 中**测试 22 处构造点与全部消费点无改动**；翻译器 5 处**必然有改动**（本项实现） |
| **A5** | ModelProbe 目视行为与改前一致 | 拖动窗口 / 拖选文本 / 滚动条拖动**行为不变**（本项只补信息） |

---

## 9. 交给详细设计的四件事（★ 评审指定）

> 编号用 **①–④**（**不使用** `D-` 前缀——`D0–D6` 已被 §2 的决策点占用）。

| # | 事项 | 已有输入 |
|---|---|---|
| **①** | **`unsigned int pressedButtons` 的构造接口语义** | **§3.6**（三层分工 + 升级触发条件）；详设须把它落进**头注释正文**与**既有 22 处调用点的行为说明** |
| **②** | **5 个翻译点的精确修改** | **§3.4**（两个新 helper 完整草稿 + 5 处位置 `:97` / `:114` / `:130` / `:149` / `:173`）；详设须给出**逐点 diff 规格**——含滚轮分支「`HIWORD` 取 delta」与「helper 内 `LOWORD` 取状态」**并存**的那一处 |
| **③** | **`FakeHost` 测试装置的扩展** | **§7**（`ReceivedEvent` +2 字段 · 两个既有分支各补 2 行 · T19-1..T19-9）；详设须给出**改动后的 `OnEvent` 片段**＋用例登记（`RunAllTests.h` / `RunAllTests.cpp` 两处） |
| **④** | **P1 / P2 的实测记录** | **§3.5**；详设须定**实测方式**（日志 / ModelProbe 目视）与**记录位置**，并重申「**只改注释、不改代码与用例**」 |

★ 另两条**不得在详设阶段推翻**的定案（评审明确"原样保留"）：**D2 不给命名类型** · **不得把 `MouseButton` 改成位标志枚举**。

---

## 10. 修订记录

- **v1.1**（2026-09-23）**评审通过 + 两条建议采纳**。① **评审结论**：外部评审（第二轮）✅ **通过、无阻塞项**；确认 **D2 形态 / `MouseButton` 与 mask 分层 / Q2 的 LOWORD-HIWORD 分离 / Q3 零破坏 / `KeyModifier` 复用 / 横向滚轮隔离 / 平台层 helper / 真实翻译路径测试 / P1-P2 纪律 / C1–C6 / T19-1..T19-9 / 影响面** 全部成立；并**确认其上一轮"需要额外 `GetKeyState`"的说法不准确**（即本稿 §2.3 的重写被接受）。② ★ **采纳建议 1** → **新增 §3.6「构造参数的通道性质」**（消费层 / 构造通道 / 兼容通道**三层分工表** + 不给命名类型的理由 + **升级触发条件**）+ §3.1 头注释补 `@note`（标注为**低层通道**）。③ ★ **采纳建议 2** → **§2.3 新增「Event 模型支持 Alt 概念」vs「当前 Win32 映射不产生 Alt」两层对照表 + 措辞红线**（❌"MouseEvent 不支持 Alt" / ✅"当前 Win32 鼠标映射不从 `wParam` 产生 Alt"）。④ **新增 §1.5**（本轮评审逐条处置）+ **§9「交给详细设计的四件事」**，原 §9 修订记录顺延为 **§10**。⑤ **内容零删改**——B1–B12、契约 C1–C6、O1–O6、T19-1..T19-9、验收 A1–A5 **全部原样保留**。

- **v1.0**（2026-09-23）初稿。**输入**：需求稿 v1.0（外部评审通过、无退回项）+ 评审给出的 6 项初设重点。**内容**：B1–B12 代码基线（含 **4 处勘误/新证**——B3 翻译器构造点 **4→5**、B4 测试构造点 **22 处**、**B5 `isDoubleClick` 尾默认参先例**、**B6 `TranslateModifier` 走 `GetKeyState` 且含 Alt**、**B8 `FakeHost` 翻译测试基础设施已在**、**B9 `GetKeyState` 源不可断言**）· §1.3 对评审 **12 条逐条处置**（含 3 处纠正：`Window*` 非引用 / R5 口径须限定 / **D3 理由重写**）· §2 四项**定案**（D2 私有掩码 + 谓词 / D5 命名与"成套或不给" / **D3 修饰键源 = `wParam`、Alt 有意缺位 + 重启条件** / D6 不并入 #36）· §3 接口与平台层改动分解（含 **Q2 陷阱表**与"默认实参不继承"提醒）· §3.5 **两条待实测平台语义（不阻塞）** · **契约 C1–C6**（C5 给出零破坏的**精确口径**）· 影响面（公共 API **+2** · 用例 **231→240** · 特征串 **11→11**）· O1–O6 · **测试 T19-1..T19-9**（承载 = 真翻译路径）· 验收 A1–A5 落地口径。**待评审**。
