# Phase 33 圆弧绘制原语（DrawArc）职责确认

> **状态**：v1.0（2026-10-14）——初稿，**待评审**
> **触发**：`ECDI_fake_harness` 账本 **REQ-08**（环形进度条 / 圆弧原语缺失）
> **定位**：渲染能力层新增**第一个曲线原语**；同时交付框架侧环形进度控件（`RingProgressBar`）

---

## §1 动机与现状勘察（代码事实实证表）

### 1.1 一句话动机

`RenderingBackend` 的绘制原语只有 **6 个**（`DrawRect` / `DrawText` / `DrawLine` / `DrawRoundedRect` / `DrawImage` / `DrawFocusRect`，`include/ECDI/Render/RenderingBackend.h:69-115`），**没有任何曲线**。
`roadmap-deferred.md:5` 自 v1.1（2026-08-15）起就把 **`DrawArc`** 列为挂账项，
而 `phase9.6-progressbar-requirements.md:27` 写明「indeterminate（循环动画 / 转圈）不在 v0.1 范围——
它需要 `DrawArc` / `PushTransform`」——**挂账理由是「无消费者不动」**。
现在消费者出现了：harness 的上下文占用环（`REQ-08`）⇒ **挂账前提已变**（条 97 的「已登记延迟设计，出现第一个真实消费者后兑现」）。

### 1.2 能力侧事实（全部实测带行号）

| # | 事实 | 出处 |
|---|---|---|
| K1 | **公共抽象接口只有 6 个绘制原语**，无 `DrawArc` / `DrawCurve` | `include/ECDI/Render/RenderingBackend.h:69-115` |
| K2 | **命令层 variant 全集同样没有**（Rect/Text/Line/RoundedRect/Image/Clip×2/FocusRect = 8 个） | `include/ECDI/Render/RenderCommand.h:93-96` |
| K3 | **GL 侧同样只有那 6 个**（`GLRenderer.cpp` 的 override 集） | `ECDI/src/Render/GLRenderer.cpp:208-496` 区间 |
| K4 | **实现者共 3 个**：`GDIBackend` / `GLRenderer` / `RecordingBackend` | `GDIBackend.h:33` · `GLRenderer.h:46` · `RecordingBackend.h:21` |
| K5 | ★ **库外还有第 4 个实现者**：harness 的 `PaintProbe : public RenderingBackend`（包装真实后端做命令计数/计时） | `ECDI_fake_harness/gui/src/PaintProbe.h:19-40` |
| K6 | ★ **`DrawRoundedRect` 的半径契约 = `[0, min(w,h)/2]` 钳制** ⇒ 传 `w==h` + 半径 `w/2` 即**实心圆**；两个同心实心圆叠出**圆环** | `RenderingBackend.h:93` · `GDIBackend.cpp:900-906` |
| K7 | ★ 但「按角度的弧」**无法用现有原语表达**（只能整圆或整圆环；弧的起止角度无载体） | K6 的直接推论 |
| K8 | ★★ **`DrawArc` 已三处登记为挂账**：`roadmap-deferred.md:5`（总表头部）· `phase9.6-progressbar-requirements.md:27`（indeterminate 的前置）· `phase24-line-antialiasing-requirements.md:120` 的 **N1**（曲线 AA 的前置） | 三处原文 |
| K9 | ★ **Phase 8.6 / 24 已为「覆盖度 AA」铺好两层机制**：第一层纯几何生成（`CornerCoverageMask` / `LineCoverageGrid`，均在 `src/Render/` Internal 头，零 GDI）+ 第二层装配；**共享合成件 `RasterizeMask` 已存在且与形状无关** | `src/Render/CornerCoverageMask.h:23` · `src/Render/LineCoverage.h:12-19` · `src/Render/CoverageRaster.h:44-45` |
| K10 | ★ `LineCoverage` 的几何模型 = **stadium 带**（「点到线段距离 ≤ hw」）；`RasterizeMask` 的输入是 `(originX, originY, width, height, coverage[])`，**与「这是什么形状」无关** | `src/Render/LineCoverage.h:26-31` · `src/Render/CoverageRaster.h:35-45` |
| K11 | **GDI 侧已有「补丁面 + 混合」基础设施**（`PatchSurface` / `BlendPatch`），`DrawLineCoverage` 正是走它 | `GDIBackend.cpp:865-889` |
| K12 | **GL 侧已有「覆盖度图集 + TexturedQuad」路径**（`LineSlot`），图集满有 `SolidSegment` 实心降级先例 | `GLRenderer.cpp:299-331` · `GLRenderer.cpp:756` |
| K13 | ★ **全库无任何「角度」概念**（grep `angle\|Angle\|degree\|radian\|sweep` 在 `ECDI/` 零命中）⇒ 角度单位/起点方向是本 Phase 的新公共契约，必须显式定 | 实测 |
| K14 | ★ `ProgressBar` 是**水平专用**控件（`OnPaint` 画轨道矩形 + 填充矩形；`ProgressBarStyle` 只有 track/fill/cornerRadius 三个字段）⇒ 环形控件**不能**复用它（继承会带上水平语义） | `src/Widget/ProgressBar.cpp:94-119` · `include/ECDI/Theme/ProgressBarStyle.h:11-16` |
| K15 | ★ `ProgressBar` 的**动画双层 API**（`m_progress` 目标 / `m_displayProgress` 呈现 + 200ms `EaseOut` 过渡 + `AnimationToken` RAII）是本仓库控件动画的标准形态，环形控件应对齐 | `src/Widget/ProgressBar.cpp:22-90` |
| K16 | ★ **harness 已有自定义 Widget 先例**（`PromptBar : public ECDI::Panel`）⇒ 即便框架不供环形控件，harness 也能自己拼 ⇒ 「框架侧是否做控件」是**独立的可选项**，不与 `DrawArc` 耦合 | `ECDI_fake_harness/gui/src/PromptBar.h:17` |
| K17 | ★ `PaintProbe` **不 override `SetBackdropSurface`**——Phase 32 正是靠「带默认实现的虚函数」（S7）让库外实现者零改动 | `RenderingBackend.h:49-53` · `PaintProbe.h:19-40` |

### 1.3 与 `framework-defect-audit` 的关系

本条**不是缺陷**，是用户新能力需求（`REQ-08` 归属已由 harness 侧四轮反转确认：前 4 条候选全部反转为 harness 侧问题，**本条是第 5 条候选、也是第 1 条「先查了源码仓且 ECDI 已自认挂账」的条目**）。⇒ 按项目口径**不占缺陷队列编号**，走正常 Phase 序列（**Phase 33**）。

---

## §2 范围内（表格）

| # | 项 | 内容 | 优先级 |
|---|---|---|---|
| R1 | **`DrawArc` 绘制原语**（公共 API +1 方法） | 圆弧**描边**（非楔形/非扇形）：给定圆心、半径、起止角度、线宽、颜色，画一段圆弧。语义 = 「到圆心距离 ∈ [r−w/2, r+w/2] ∧ 角度 ∈ [a0, a0+sweep]」的区域 | P0 |
| R2 | **覆盖度抗锯齿** | 与 `DrawLine` AA 同族：新增 Internal 覆盖度生成器（纯几何、零 GDI、可无窗口单测），**复用既有共享合成件** `RasterizeMask` | P0 |
| R3 | **命令层贯通** | `RenderCommand` variant +1 · `Renderer::ExecuteCommand` +1 重载 · `PaintContext::DrawArc` +1（Widget 层入口） | P0 |
| R4 | **两端实现** | GDI：覆盖度网格 → `PatchSurface` → `BlendPatch`（同 `DrawLineCoverage`）；GL：覆盖度网格 → 图集 `ArcSlot` → `TexturedQuad`（同 `LineSlot`） | P0 |
| R5 | **`RecordingBackend` 记录** | 作为测试观测缝记录 `DrawArc` 命令（参数原样入命令流） | P0 |
| R6 | **`RingProgressBar` 控件** | 环形 determinate 进度控件：轨道整环 + 进度弧（从 12 点钟或 3 点钟起，按约定）；双层值 API + 动画过渡 + 专属样式（`RingProgressBarStyle`） | P1 |
| R7 | **默认零回归** | `DrawArc` 为新 API ⇒ **无既有调用点**；`RingProgressBar` 为新控件 ⇒ 无既有场景。两者都不得改动任何既有公共头的行为语义 | P0 |
| R8 | **测试** | 覆盖度生成器单测（L1 数学层：面积守恒 / 值域 / 单调性 / 退化）+ 命令流集成测试（L2）+ 控件行为测试 | P0 |

---

## §3 关键决策点（含倾向）

### D1 原语形态：描边弧 vs 楔形（pie）vs 整环

- 候选：① **描边弧**（有宽度的一段弧）② 楔形/扇形（实心到圆心）③ 整环（固定整圆）
- **倾向 ①**：`REQ-08` 的三个消费者形态——环形进度（弧）/ spinner（弧）/ 仪表盘（弧）——**全部是弧**；
  楔形与整环都可由弧组合或退化表达（整环 = sweep 360° 的描边弧；楔形**不**由本原语负责）。
- ★ 理由（条 95①「一个概念只用一个词」）：**不引入 `DrawPie` / `DrawRing` 第二个名字**——将来真有楔形消费者再另立。

### D2 角度单位与起点方向

- 候选：① **度**（0–360）② 弧度（0–2π）
- **倾向 ① 度**：教学框架（条 95「Everyone Can Do It」）——`SetProgress(0.75f)` → 扫过 270° 这类心智模型无需换算。
- **起点方向倾向「3 点钟 = 0°，顺时针为正」**：与 GDI `AngleArc` / 屏幕坐标 y 向下的惯例一致，后端**零转换**；
  `sweep` 可负 ⇒ 逆时针。**环形进度条的起点**（12 点钟）由**控件层**折算（控件传 `startAngle = −90` 或 `270`），
  **不进原语契约**——原语保持中立（条 23：论证用契约语言，不把单个消费者的偏好写进公共层）。

### D3 线宽语义与 0 进度退化

- **倾向**：`width` 与 `DrawLine` 同语义（框架层 float、后端 `lround` + 下限 1px）；`sweep == 0` ⇒ **不画**（与 `DrawLine` 的零长线退化一致）；
  `radius <= 0` ⇒ 不画。★ **不留「宽度 0 = 实心弧」的后门**（那会引入第二个语义）。

### D4 AA 模型

- 候选：① **圆弧 stadium 带覆盖度**（本 Phase 倾向）② 解析覆盖度 ③ SDF
- **倾向 ①**：与 `LineCoverage` 直系同族（条 97）——「点到线段距离 ≤ hw」换成「点到圆心距离 ∈ [r−hw, r+hw] ∧ 角度 ∈ 区间」，
  **同一套超采样 + 同一张输出网格 + 同一个 `RasterizeMask`**；面积守恒可实测（继承 Phase 8.6 D9 / Phase 24 A1 的断言形态）。
- ★ 选①不选②的理由同 Phase 8.6 D3：解析法要为「圆环扇形 × 像素方格」单独推面积公式，而超采样对任何形状同一套代码；
  选①不选③的理由：覆盖度需要**面积正确**（R8 的面积守恒断言），SDF 不保证。

### D5 虚函数形态：纯虚 vs 默认空实现 ★（本 Phase 唯一涉及「破坏面」的决策）

- 候选：① **纯虚** ② 带默认空实现
- 倾向**待用户拍板**（2026-10-14 已确认取①纯虚，见 §6 v1.0 记录）。
- ★ 两条路的代价都必须写明：
  - **① 纯虚**：进穷尽集 ⇒ 库外第 4 个实现者 `PaintProbe` **编译失败**，必须同批协调 harness 加一个转发 override
    （条 104「想保持可选就不能进穷尽集」的反面——本项目选择**最强契约**）；好处 = **无静默失效**。
  - **② 默认空实现**：零破坏（Phase 32 S7 先例：`SetBackdropSurface` 带默认实现 ⇒ 7 个实现者零改动）；
    代价 = 条 104 的「半可选 / 静默失效」——库外后端忘了实现 ⇒ 环 silently 不画，无从诊断。
- ★ **本项目既有裁决先例**：Phase 12 曾因「加 7 个纯虚需同步 2 个测试替身」被记为**代价**；Phase 32 反过来因「默认实现换来零改动」被记为**收益**。
  ⇒ 两条路都有落款，选哪条都要写明代价，不许只说好处。

### D6 `RingProgressBar` 的继承与命名

- **倾向**：`RingProgressBar : public Widget`（**不**继承 `ProgressBar`——那个的 `OnPaint` 是水平语义，K14）；
  命名 `RingProgressBar`（与既有 `ProgressBar` 同族同词，条 95①「一个概念只用一个词」）。
- ★ **YAGNI 边界如实写明**：当前唯一真实消费者是 harness（K16 证明 harness 也能自己拼）⇒ 框架侧做控件属于
  **「为第一个真实消费者开能力」**（用户 2026-10-14 明确选择，见 §6），**不是**投机抽象；
  **不得**为此先把 `ProgressBar` 的值/动画/主题三件抽成共享基类（条 22：第二个消费者出现才抽）。

### D7 起点：控件层的默认朝向

- **倾向**：`RingProgressBar` 默认从 **12 点钟起、顺时针**（进度条惯例：0% 在正上方，100% 回到正上方）；
  实现 = 控件传 `startAngle = 270°`（3 点钟制）。★ 是否开放 `SetStartAngle` **留初设**（YAGNI：无第二个消费者不开放）。

---

## §4 范围外（非目标）

| # | 项 | 理由 |
|---|---|---|
| N1 | **楔形 / 扇形（pie）** | 无消费者；与「一个概念一个词」冲突（D1） |
| N2 | **贝塞尔 / 任意曲线** | 无消费者；`roadmap-deferred` 既有记账继续有效 |
| N3 | **椭圆弧** | 无消费者；将来出现按同一形态扩展生成器即可（D4 的两层拆分保证） |
| N4 | **线帽（round/cap）样式** | `DrawLine` 也没有（Phase 24 只做圆帽——stadium 带天然圆帽）；不新开概念 |
| N5 | **虚线 / 点线弧** | `DrawFocusRect` 已有点线，但那是独立能力；无消费者 |
| N6 | **`PushTransform` / 旋转** | 既有挂账项，与本 Phase 正交 |
| N7 | **indeterminate（转圈）动画控件** | `phase9.6:27` 的原范围外结论**维持**——它需要的不只是 `DrawArc`（还需循环动画策略），且无消费者 |
| N8 | **把 `ProgressBar` 重构出共享基类** | 见 D6：第二个消费者出现才抽 |
| N9 | **后端执行端（Renderer 之后）的任何改动** | 本 Phase 只加原语，不改既有原语的执行路径 |
| N10 | **harness 侧改动** | 属另一仓库；但 **D5 若取纯虚，harness 的 `PaintProbe` 必须同批加转发 override**（跨仓库协调项，须在详设写明） |

---

## §5 与既有约束的对齐（skill 条号对照表）

| 约束 | 本 Phase 的落实 |
|---|---|
| 条 16（Widget 层零 HDC/Win32） | `RingProgressBar` 只调 `PaintContext::DrawArc`；圆弧几何全在 `src/Render/` 内部件 |
| 条 19（`RenderCommand` 死数据 + 穷尽性） | 新命令 = 纯数据（center/radius/angles/width/color）；`Renderer::ExecuteCommand` 加重载 ⇒ 漏加即编译错误 |
| 条 20（能力层/决策层正交） | `DrawArc` 是能力层；`RingProgressBarStyle` 是决策层；换后端主题不动 |
| 条 22（YAGNI） | 只做描边弧 + 环形控件；楔形/椭圆/线帽/变换全部记账不做（§4） |
| 条 95①（命名即语义） | `DrawArc` / `RingProgressBar`；角度单位度 + 3 点钟起点（D2） |
| 条 95⑤（加能力不改既有调用点） | 两者都是全新 API/控件 ⇒ 既有调用点零改动；**但 D5 纯虚会破坏库外实现者编译**（如实登记为代价） |
| 条 97（先找同族先例） | AA 模型、缓存、GDI/GL 两路消费**全部**沿用 Phase 8.6/24 落款（K9–K12） |
| 条 103（抽纯计算件的四步做法） | `GenerateArcCoverage` 按该条执行：先量接触面（零 GDI）→ 签名给「调用方缓冲」形态（与 `LineCoverageGrid` 同构）→ 算法原样搬 → 行为不变用数值逐位对照证明 |
| 条 104（可选能力 vs 穷尽集） | D5 显式裁决并写明两条路代价 |
| 条 34（跨工具链兼容先证宏/枚举） | 角度/几何全用框架自有类型，无平台常量 |

---

## §6 修订记录

- **v1.0**（2026-10-14）初稿。**输入**：harness 账本 `REQ-08` · `roadmap-deferred.md:5` · `phase9.6-progressbar-requirements.md:27` · `phase24-line-antialiasing-requirements.md:120 N1` · 源码勘察 K1–K17（全部实测带行号）。**内容**：§1 动机与 17 项勘察 · §2 范围内 8 项 · §3 决策点 D1–D7（★ D2 角度约定 / D4 AA 模型 / D6 控件形态均已有实测依据；D5 虚函数形态为本 Phase 唯一涉及破坏面的决策，两条路代价都写明）· §4 范围外 10 项 · §5 约束对齐表。**已确认项（2026-10-14 用户拍板）**：① 范围取「**原语 + 环形控件**」（B）；② **角度 = 度、3 点钟 = 0°、顺时针为正**；③ **AA = 圆弧 stadium 带覆盖度**；④ 控件命名 **`RingProgressBar`**、直接继承 `Widget`；⑤ **`DrawArc` 用纯虚**（接受「harness `PaintProbe` 需同批协调」这一代价）。**待评审。**
