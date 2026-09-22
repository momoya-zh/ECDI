# Phase 18 · 窗口/根背景能力（Window/Root background）—— 需求确认

> 状态：**v1.1 · 评审通过，可进初步设计**（2026-09-21 立项 / 2026-09-22 外部评审：「**没有看到需求层面的阻塞问题**」）
> 评审要点：**§1.3 的纪律已冻结** · **初设第一任务 = 钉死背景色数据流**（Q1 五问）· **Q2 倾向 ①** · **新增 Q5「唯一默认值来源」** · K8 数字勘误（12 → **10**）
> 立项依据：`docs/roadmap-deferred.md` **§7.7 条目 #39「根 / 窗口的背景色可配置」**（2026-09-21 新立，**由 Phase 17 A6 实施实测派生**）
> 触发场景：Phase 17 的 **R3「root 一级即全局留白」在视觉上不可用**——root 让出的四边露出 Backend 清屏白

---

## 1. 背景与现状勘察

### 1.1 立项由来

Phase 17（布局内边距）做 A6 目视验收时发现：把 `padding` 配在 **root 一级**虽然能缩进全部内容，但**四边露出白色**（`#0f1115` 深色窗口上一圈白框），且 `CaptionBar` 作为同布局的子会被**连带内缩**。最终只能把留白下移到 **page 一级**（`ModelProbe.cpp:155`）**绕开**。

**绕开不等于解决**：若要做「**真·窗口四周留白**」（露底色那一种），前置是「客户区底色可配置」。⇒ 立本项。

### 1.2 现状勘察（K1–K8，2026-09-21 逐条实测）

| # | 事实 | 证据（行号） |
|---|---|---|
| **K1** | **客户区底色 = Backend 内的硬编码常量**：`BeginFrame` 每帧 `FillRect(…, WHITE_BRUSH)` | `GDIBackend.cpp:260-261`（其上方注释即 **决策 16「清屏白（Root 白底是平台语义，不是 Widget 命令）」**） |
| **K2** | **`RenderingBackend` 接口无任何背景/清屏色入口**——12 个 `virtual`（10 纯虚 + `Initialize` 非纯 + 虚析构）中，唯一涉及帧起点的是 `BeginFrame()` | `RenderingBackend.h:31-90`（`Initialize` :36 · `BeginFrame` :39 · `EndFrame` :90） |
| **K3** | **`Window` 公共 API 无背景相关**——有 `SetChromeMode` / `SetCaptionHeight` / `SetResizeInset` / `SetWindowLayer` / `SetFileDropEnabled`，**均非背景** | `Window.h:57-198` |
| **K4** | **root 是裸 `Widget`**（无背景能力） | `Window.cpp:63` `m_rootWidget = std::make_unique<Widget>();` |
| **K5** | **框架的「背景」全是控件级**：`StyleField<Color> background` 遍布 Button / Panel / TextBox / CheckBox / Radio（`CaptionBar.cpp:31` 亦自带底色）——**没有窗口/根级的对应物** | `Theme/*Style.h` · `DefaultTheme.cpp` |
| **K6** | **`Panel` 已具备背景绘制能力**（`background.a > 0.0f` 时 `DrawRoundedRect` / `DrawRect`）——**能力已存在，只差「根能用到它」** | `Panel.cpp:53-72` |
| **K7** | root 的 Paint 从 `(0,0)` 起、尺寸 = 客户区 ⇒ **无障碍地覆盖整个客户区**，只是**没有内容可画** | `Window.cpp:125` `m_rootWidget->Paint(ctx, 0, 0)` · `:78` 尺寸 = 客户区 |
| **K8** | **测试替身 `RecordingBackend` 位于库内**（非测试目录），被 **10 个测试文件**使用（**2026-09-22 实测复核**——原稿写 12，系误记）⇒ **任何 `RenderingBackend` 接口变动都必须同步它** | `ECDI/src/Render/RecordingBackend.{h,cpp}`（`CaptionBarTests` / `CheckBoxTests` / `ClipTests` / `ImageDecodeTests` / `ModelProbeTests` / `ProgressBarTests` / `RendererTests` / `ScrollViewTests` / `TextBoxTests` / `WidgetTests`——**正好 10 个，已穷举**） |

**⇒ 缺口的精确表述**：**「客户区底色」是 Backend 里的一个硬编码常量，框架没有任何可配置入口**——既无 `Window` 级 API（K3），也无 `RenderingBackend` 接口位（K2），root 自身也没有背景能力（K4）。所以「窗口四周留白露底色」在当前框架下**不可达**。

### 1.3 一条会定形态的既有纪律

**Phase 4 的分层不变量**：`Widget → PaintContext → CommandBuffer → Renderer → RenderingBackend` 四层两两不相识；`RenderingBackend` 是**能力提供者**（operation-level），**不是命令消费者**（`RenderingBackend.h:23` 的原话）。清屏属**能力层**动作，颜色值属**决策层**输入 ⇒ **不得让 Widget 层直接下发清屏指令**。这条直接约束 §4 的 **D1 / Q2**。

**★ 本条已冻结（2026-09-22 外部评审）**：评审明确「**建议直接冻结，不要在后续设计里动摇**」。理由：为窗口底色而引入 `RenderCommand::Clear(Color)`，等于把一个属于 **Frame / Backend 生命周期**的动作塞进 **Widget 绘制命令流**，会把 `RenderCommand` / `Renderer::Execute()` / `RecordingBackend` / `PaintContext` 整条链拖进来——**收益一个窗口底色，代价四层契约**。

---

## 2. 技术路线（三案对照，给倾向）

| 方案 | 内容 | 成本 / 风险 |
|---|---|---|
| **A（倾向）** | **窗口级配置 + Backend 消费**：新增「客户区背景色」配置（落点见 D1），`GDIBackend` 的清屏用该色替代 `WHITE_BRUSH` | 触碰 `RenderingBackend` 接口（若加方法）⇒ **必须同步 `RecordingBackend`**（K8：**10** 个测试文件）；但语义天然属窗口/能力层，与 §1.3 纪律一致。**具体数据流形态见 §6 Q1 的五问与两子方向** |
| **B（备选）** | **让「根」具备背景**：`Window` 的 root 由裸 `Widget` 换成能画背景的容器（K6 的 `Panel` 能力现成） | 改 `Window.cpp:63` 的类型；风险是**既有层级假设**（root 是普通 `Widget`，多处按此推断）与「给所有 Widget 加背景字段」的 YAGNI 反例 |
| **C（兜底 = 不做）** | 应用侧用满尺寸 `Panel` 包一层 | 0 成本；即 **Phase 17 现状**（page 级 padding）。但每个应用都要重复一遍，且 **root 级留白永远做不到**。⇒ 评审认定：它**只能表达「`Widget` 的背景」，不能表达「`Window` 客户区背景」**——是 workaround，**不是本项解法** |

**倾向 A**：唯一能真正提供「窗口底色」语义的落点；B 作为 A 的成本替代（若接口改动代价过高）；C 仅作「不做」的兜底说明。

---

## 3. 需求条目（R1–R6，初稿）

| # | 需求 | 说明 |
|---|---|---|
| **R1** | **客户区底色可配置** | 应用可指定「窗口客户区中**未被 Widget 覆盖**处」的颜色（替代当前恒白） |
| **R2** | **默认 = 现状，零回归** | 不配置时行为与当前**逐位相同**（默认白）——与 Phase 17 的 **C1「逐位退化」** 同型契约 |
| **R3** | **与 root 的 padding 组合即可表达「窗口四周留白」** | 这正是**本项的直接动机**：Phase 17 的 R3 在该能力落地后**首次可用** |
| **R4** | **与四层渲染契约一致** | 清屏属能力层、颜色属决策层（§1.3）——**不得**让 Widget 层下发清屏指令 |
| **R5** | **「恢复默认」语义明确** | 传默认值 / 专门 API —— 留 D5 决策 |
| **R6** | **验收** | 至少覆盖：默认零回归 · 自定义色生效（**命令级或像素级**，取决于 Q3）· 与 padding 组合的目视 |

---

## 4. 决策点（D 系列——全部给倾向，待拍板）

| # | 决策 | 倾向 | 说明 |
|---|---|---|---|
| **D1** | 落点：`Window` 级 API / `RenderingBackend` 接口 / 两者 | ⚠️ **待初设** | 「API 在窗口层、能力在 Backend」符合既有分层；但接口加方法要**同步 `RecordingBackend`**（K8：**10** 个测试文件），成本需初设评估。**两子方向**：(a) 给 Backend 加配置接口（长期状态）· (b) 把颜色作为 `BeginFrame` 的**输入**（每帧传入）——见 **§6 Q1** |
| **D2** | 是否**同时**支持根 Widget 背景（方案 B） | ❌ 倾向**不做** | root 铺满客户区 + 窗口底色 = 同一效果；多一套能力却无第二消费者（YAGNI） |
| **D3** | 默认值 | ✅ **白色**（= 现状） | 服务 R2 零回归 |
| **D4** | 是否支持**透明/半透明**底色（与 Alpha 合成交互） | ❌ **不做** | 属 Phase 9 Alpha 合成范畴，另立项 |
| **D5** | 「恢复默认」的表达 | ⚠️ 待定 | 传 `Color::White()` / `ResetBackground()`——初设定 |
| **D6** | 命名 | ⚠️ 待定 | `SetBackground` / `SetClearColor` / `SetClientBackground`——初设定 |
| **D7** | 顺带更新 **决策 16 的注释**（“Root 白底是平台语义”） | ✅ **是** | 能力落地后该表述变为「**默认**白、可配」——属「本阶段改动会让上一阶段描述变假」的同类（skill 条 80） |

---

## 5. 非目标（YAGNI 圈定）

| # | 非目标 | 理由 |
|---|---|---|
| **N1** | 渐变 / 图片背景 | 只需纯色；纹理是另一套 |
| **N2** | 每窗口独立主题 / 背景随主题变 | 单窗口场景够用 |
| **N3** | 背景的动画 / 过渡 | 无消费者 |
| **N4** | 透明或半透明窗口（`WS_EX_LAYERED` / Alpha） | 见 D4 |
| **N5** | 给所有 `Widget` 加背景字段 | 见 D2 与方案 B 的风险 |
| **N6** | 背景的「局部」能力（只染某区域） | 布局留白只需整体底色 |

---

## 6. 留给初步设计的问题清单（Q1–Q4）

| # | 问题 | 说明 |
|---|---|---|
| **Q1** ★ | **背景色的数据流：`Window` → `Renderer` → `Backend` 怎么走？**（**评审认定的初设第一任务**） | 需逐条回答**五问**：① **谁保存** `Color`（`Window` 自持 / `Renderer` 中转 / `Backend` 持久状态）？② **谁在帧起始读取**？③ **谁调用** `BeginFrame`（现为 `Window::OnPaint` → `Renderer`）？④ **`GDIBackend` 从哪里拿到该色**？⑤ ★ **红线**：**不得**因为「Backend 要用颜色」就把 `Window` 指针/依赖塞进 Backend——`Renderer` 是命令执行器、`RenderingBackend` 是 operation-level 能力提供者（四层两两不相识）。**两子方向**：(a) Backend 加配置接口（`SetBackgroundColor`，长期状态，好处是语义直接、代价是 K8 的 10 个文件同步）· (b) 颜色作为 `BeginFrame` 的**输入**（每帧传入，**无长期状态**，代价是改既有签名并需查 Renderer 掌握哪些 Window 状态）——需对比「状态归属」与「接口变动面」 |
| **Q2** | **清屏时机与形态** | ① `BeginFrame` 内用可配值清屏（最小改）；② 由 `Window` 下发一条「背景」命令（会**新增 `RenderCommand` 变体** ⇒ 触及 `std::visit` 穷尽性与四层契约）——倾向 ①，需初设论证 |
| **Q3** | **`RecordingBackend` 如何表达清屏色** | 决定 R6 的断言层级（命令级能否覆盖，还是必须像素级）——牵连既有 **10** 个测试文件（K8） |
| **Q4** | **决策 16 注释与 `roadmap §7.7` 的重启条件如何同步收口** | 能力落地后两处都要改（D7） |
| **Q5** ★ | **「唯一默认值来源」** | 评审新增：若 `Window` / `Renderer` / `Backend` **各带一个默认值**，就会出现**三个「默认」**（R2 的零回归随之失去单一依据）。⇒ 必须**只有一个语义来源**，其余层只做**传递 / 消费**。初设需显式回答：**默认白在哪一层初始化**，以及为何另外两层不构成第二来源 |

---

## 7. 影响面（初估——初设细化）

| 项 | 预计 |
|---|---|
| `ECDI/include/ECDI/Render/RenderingBackend.h` | 视 D1/Q1——可能 **+1 虚方法**（或改 `BeginFrame` 签名） |
| `ECDI/src/Render/GDIBackend.cpp` | `:261` 清屏色由**常量**改为**可配值** |
| `ECDI/src/Render/RecordingBackend.{h,cpp}` | **接口变则必须同步**（K8：**10** 个测试文件依赖） |
| `ECDI/include/ECDI/Window/Window.h` / `src/Window/Window.cpp` | 视 D1——可能 **+1 配置 API**（配置期） |
| 公共头 | 92 → **92**（预计不新增头） |
| 用例 | 226 → **226 + N** |
| `examples/ModelProbe/main.cpp` | ★ **须单独授权**（skill 条 2）——若用它做 R3 组合的目视验收 |
| 决策 16 注释 + `roadmap §7.7` | 同步收口（D7 / Q4） |

---

## 8. 修订记录

- **v1.1**（2026-09-22）**采纳外部评审（结论：「没有看到需求层面的阻塞问题」，可进初步设计）**。① **§1.3 纪律升格为「已冻结」**——评审要求「直接冻结，不要在后续设计里动摇」，并给出理由（`RenderCommand::Clear(Color)` 会把 Frame/Backend 生命周期动作塞进 Widget 命令流，拖入整条命令链）；② **§6 Q1 升级为「数据流五问」**（谁保存 / 谁读 / 谁调 `BeginFrame` / Backend 从哪拿 / **红线：不得把 Window 指针塞进 Backend**）+ 两子方向（配置接口 vs `BeginFrame` 输入）；评审明确这是**初设第一任务**，且「不能含糊过去」；③ **新增 Q5「唯一默认值来源」**——评审提出 R2 的零回归必须只有一个语义来源，防 Window / Renderer / Backend 三处各生默认值；④ **§2 补充评审定性**：C 案只能表达 Widget 背景、不能表达窗口客户区背景，属 workaround；A 案指向 Q1；⑤ **K8 数字勘误**：「12 个测试文件」系误记，**2026-09-22 实测复核为 10 个**（`WidgetTests` / `RendererTests` / `ClipTests` / `CaptionBarTests` / `CheckBoxTests` / `ProgressBarTests` / `ScrollViewTests` / `TextBoxTests` / `ImageDecodeTests` / `ModelProbeTests`，已穷举），§7 影响面同步；⑥ 评审确认的既有取向（技术路线 A 倾向 · R2 默认零回归第一优先 · D4 不做 Alpha · D5/D6 留初设 · R3 与 Phase 17 是干净的连续演进）**保持不动**。

- **v1.0**（2026-09-21）**需求确认初稿**。立项依据 = `roadmap-deferred.md` **§7.7 条目 #39**（**由 Phase 17 A6 实施实测派生**）。① **§1.2 现状勘察 K1–K8 全部带行号实测**——其中 **K1（硬编码 `WHITE_BRUSH`）/ K2（Backend 接口无入口）/ K3（Window API 无入口）/ K4（root 是裸 Widget）** 四条共同界定缺口：「客户区底色」**无任何可配置入口**；**K8** 记明库内测试替身 `RecordingBackend` 被 12 个测试文件依赖（接口变动的连带成本）；② **§1.3 锁定一条会定形态的纪律**（Phase 4 四层不变量：清屏属能力层、颜色属决策层）⇒ 直接约束 D1/Q2；③ **技术路线三案**（A 倾向 / B 备选 / C 兜底）；④ **需求 R1–R6**（R2「默认零回归」与 Phase 17 的 C1 同型；R3 是本项的直接动机）；⑤ **决策 D1–D7**（**D1 落点**与 **D6 命名**、**D5 恢复默认**留初设；D7 处理「决策 16 注释将变假」）；⑥ **非目标 N1–N6**；⑦ **留 Q1–Q4 给初步设计**（其中 **Q2 清屏形态**可能牵动 `RenderCommand` 变体，是最大风险点）。**待评审。**
