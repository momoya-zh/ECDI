# Phase 21 · 系统图标 → Image（icon to image）—— 初步设计（v1.0）

> 来源：`phase21-icon-to-image-requirements.md` **v1.1**（评审通过）
> 状态：**v1.0**（2026-09-26）——**待评审**（评审通过后方可进详细设计）
> 本稿输入：需求 **R1–R5** · 决策点 **D0–D5** · 非目标 **N1–N6** · 待决 **Q1–Q4** · 验收 **A1–A6** ＋ ★★ **本稿新建的平台语义实测（§1.4 P1–P8，独立探针，2026-09-26）**

---

## 1. 设计输入与基线

### 1.1 设计输入

| 项 | 内容 |
|---|---|
| **需求** | `docs/phase21-icon-to-image-requirements.md` **v1.1**（★ 评审结论「**通过，可以进入 Preliminary Design**」） |
| **核心边界（K1+K2）** | **不是「框架不能显示图标」，而是「框架拿不到系统图标的像素」**——`.ico` **文件**走 `DecodeFile` 已可出图（WIC），缺的是**运行时句柄** |
| **四个待决** | ★★ **Q1（第一优先级，与 Q4 耦合）** · **Q2（COM 边界，不得照搬 WIC）** · ★ **Q3（可测性）** · **Q4（不存在路径语义）** |
| **本稿新增输入** | ★★ **§1.4 平台语义实测**——Q1 / Q4 需求明写「**须实测 Windows API 行为后再冻结，不靠推断**」⇒ 按项目纪律（skill 条 105）先做**独立探针** |

### 1.2 代码基线（B1–B10，2026-09-26 逐条实测带行号）

| # | 事实 | 证据 |
|---|---|---|
| **B1** | `Decode` 是**无状态自由函数**（非类）；`namespace ECDI::Decode`；现有两个入口 `DecodeMemory` / `DecodeFile`；头注释自述「失败一律返回空 `Image` + `Logger` 记 Error + **不抛异常**」 | `include/ECDI/Decode/ImageDecoder.h:9-16,22,26` |
| **B2** | 实现在 `src/Platform/Win32/WicImageDecoder.cpp`（220 行）；`LogDecoderError`（`:30`）· `ComPtr`（`:46`）· `ComScope`（`:69`）· `CalculateImageBufferSize`（`:82`）· `DecodeFromDecoder`（`:112`）**全在匿名 namespace**（`:27-201`）⇒ ★ **不可跨文件复用** | 同上 |
| **B3** | `Image` 契约：**32bpp premultiplied BGRA** / top-down / `stride >= width*4` / `width==0` 视为空（`DrawImage` 天然跳过） | `include/ECDI/Core/Image.h:16-22` |
| **B4** | **消费侧已就绪**：`PaintContext::DrawImage(const Rect& dest, const Image& image)` | `include/ECDI/Render/PaintContext.h:43` |
| **B5** | ★★ `ImageDecodeTests.cpp` 已有 **8 条**用例，**其中 2 条直接调 `DecodeFile("Z:/nonexistent_dir/…")`** ⇒ **「无头环境跑 WIC + `CoInitializeEx`」已被既有 252 用例证明** | `src/Tests/ImageDecodeTests.cpp:178,209` |
| **B6** | 登记机制：**新增测试文件**须 `RunAllTests.h` 声明 + `RunAllTests.cpp` 调用 **各 +1**（现有 **25** 条声明） | `RunAllTests.h:26` · `RunAllTests.cpp:24` |
| **B7** | ★ **测试可直接 include 平台层内部头**——**9 个**测试文件已 include `Platform/Win32/*`（`DpiTests.cpp` 即 Phase 20 先例） | `src/Tests/{DpiTests,DropFilesTests,EventTests,…}.cpp` |
| **B8** | CMake：`GLOB_RECURSE … CONFIGURE_DEPENDS` ⇒ **新增 `.cpp` 自动入库**，`CMakeLists.txt` **零改动** | `CMakeLists.txt`（Phase 10 后既有） |
| **B9** | **内部头放 `src/`** 是既有做法：`src/Platform/Win32/DpiConversion.h`（Phase 20.1 抽出）· `src/Render/CoverageRaster.h`（Phase 19 抽出） | 同上 |
| **B10** | ★★ **`desktopnest-roadmap.md` 对 `SHGetFileInfo` 已有明确判定**：§3「⚠️ **仍留应用层**（纯 API，无消息依赖）」· §4「**❌ 不进框架**——纯 API 调用，无消息依赖」· §5 **G-2** 登记为「**`HICON` → `Image` 通路缺失**」 | `desktopnest-roadmap.md:105,127,176` |

★ **B10 与需求 D0（「倾向放框架」）直接冲突**——本稿在 **§2.1** 论证裁决，并在 **§2.9** 给出回填建议（**须授权**）。

### 1.3 需求 Q1–Q4 → 本稿答案索引

| Q | 本稿定案 | 落点 |
|---|---|---|
| **Q1** ★★★ | 取 `ECDI::Decode::DecodeSystemIcon(const std::string& utf8Path)`；**一律加 `SHGFI_USEFILEATTRIBUTES`**；属性由 `GetFileAttributesW` 取真实值、失败则合成 `FILE_ATTRIBUTE_NORMAL` | **§2.2 + §2.3** |
| **Q2** | **不做任何 COM 初始化**（★ 实测证否「需要」）；**不照搬 WIC 的 `ComScope`** | **§2.4** |
| **Q3** ★ | 内核 `ImageFromHIcon(HICON)` / 外壳 `DecodeSystemIcon(path)` 分离；★ **实测两层都可无头** ⇒ 本阶段**可代码级闭环** | **§2.5** |
| **Q4** | **不存在也出图标**（按「路径类型」语义）；★ 由 **P4** 实测锁定唯一实现途径 | **§2.3** |
| （D0）★★ | **放框架**（并指出 roadmap §4 判据不完整） | **§2.1** |
| （D2） | 单档 `SHGFI_LARGEICON`（实测 **32×32**）；★ 框架**不假设尺寸** | **§2.6** |
| （D4）★★ | **mask 只在没有 alpha 时才是信息源**——32bpp 用 alpha、否则用 mask | **§2.7** |
| （D5） | premultiply **在框架内**做；★ 实测 `GetDIBits(32bpp)` **保 alpha** ⇒ 无绕行需求 | **§2.8** |

### 1.4 ★★ 平台语义实测（P1–P8，2026-09-26 独立探针）

> **动机**：需求把 **Q1/Q4** 标为「**须实测 Windows API 行为后再冻结，不靠推断**」，且 Q1 是**第一优先级**；**Q2/Q3/D2/D4** 同样依赖平台事实。
> **方式**：按 skill 条 105（待实测的**平台语义**用独立探针）——**零仓库侵入**（源与产物在 `.workbuddy/tmp/`）、`-mconsole`、**全程未调 `CoInitializeEx`**、**未创建任何窗口**。全部用例输出落文件后读取（条 55）。

| # | 探针结论 | 原始读数 |
|---|---|---|
| **P1** | ★★ **`SHGFI_USEFILEATTRIBUTES` 在「无 COM / 控制台 / 无 shell 交互」下可用**——**不存在**的 `.txt` 路径也拿到图标 | `ret=1` · `hIcon≠0` · **32×32** · `bmBitsPixel=32` · `alpha(0)=355 alpha(255)=651 alpha(mid)=18` |
| **P2** | **目录有独立 shell 语义**——与普通文件**不同** | 目录 `iIcon=3`；普通文件 `iIcon=0`；`.exe` `iIcon=50` |
| **P3** | ★ **已存在路径上，加与不加 `SHGFI_USEFILEATTRIBUTES` 结果逐项相同** | `.ini`：两者 `iIcon=0` 且 alpha 分布同为 `355/651/18`；`.exe`：两者 `iIcon=50` 且同为 `339/438/247`；目录：两者 `iIcon=3` 且同为 `456/540/28` |
| **P4** | ★★ **不加 `SHGFI_USEFILEATTRIBUTES` 时，不存在路径拿不到图标** | `ret=0` · **`hIcon=NULL`**（`.txt` 与无扩展名两例皆然） |
| **P5** | ★★ **`GetIconInfo` 的 color 恒 32bpp**（11/11 例）；`hbmMask` 恒非空（1bpp）且 ★ **mask 置位数 == `alpha==0` 的像素数** | P1 `355/355` · P2 `456/456` · `.exe` `339/339` |
| **P6** | ★★ **`GetDIBits(32bpp, top-down)` 保留 alpha**——自造 `0/64/128/255` 图标往返**逐点精确**；`SMALLICON` = **16×16** | `回读 alpha: 0 64 128 255` |
| **P7** | ★ **空路径 `L""` 不报错**（`ret=1` 且**返回了图标**）；`nullptr` 则 `ret=0` | ⇒ **入口必须自己校验**，不能依赖 API 报错 |
| **P8** | **本机系统 DPI = 96**（`GetDpiForSystem()=96` · `SM_CXICON=32`），且**两种感知级别下均返回 32×32** ⇒ 「尺寸是否随 DPI 变」**本机不可判定** | 记为 **P 项**（§6 O4 之外，见 §3.4） |

★★ **P4 + P3 合起来直接解开 Q1/Q4 的耦合**：

> **`SHGFI_USEFILEATTRIBUTES` 是「不存在路径也能出图标」的唯一途径（P4），而它对已存在路径又无任何保真损失（P3）** ⇒ 单一代码路径即可同时满足 Q4 与"已存在路径正确"。**无需两层递进**。

★★ **P5 是本稿对 D4 的决定性输入**：mask 与 alpha **信息等价于「是否全透明」**，而 alpha 还带**中间值**（抗锯齿）⇒ **用 mask 会丢抗锯齿**（§2.7）。

---

## 2. ★ 核心定案

### 2.1 ★★ D0：`SHGetFileInfo` 放框架（与 `desktopnest-roadmap` §4 判据的冲突裁决）

**定案：放框架**——应用层**只给路径**，一行调用。

**论证（R1 + R2 联合迫使，不是偏好）**：

| 步 | 命题 |
|---|---|
| ① | **R2 / 核心不变量**禁止**公共 API** 出现 `HICON`（「框架层零 Win32」） |
| ② | 若框架**只**提供 `HICON` → `Image`（即 D0 的备选 A2），该函数**要么公开**——把 `HICON` 带进公共 API（**违 R2**）；**要么仅内部**——应用层**用不到**（**R1 无法达成**） |
| ③ | ⇒ **R1 + R2 联合排除 A2**，只剩 A1 |

★★ **与 `desktopnest-roadmap.md` 的冲突（B10）**——这是本稿必须点明的一处：

| 文档 | 原文判定 | 与 D0 的关系 |
|---|---|---|
| `desktopnest-roadmap.md` §4 | 「图标提取 `SHGetFileInfo` → **❌ 不进框架**（纯 API 调用，无消息依赖）」 | **相反** |
| `desktopnest-roadmap.md` §5 G-2 | 缺口登记为「**`HICON` → `Image` 通路缺失**」 | **只登记了内核** |

★ **判据不完整**：roadmap §4 的两条判据只问了「**是否需要改窗口过程 / 窗口样式**」与「**是否已是公共抽象的一部分**」，**没有问第三个问题**：

> **「留在应用层，会不会逼应用层自己 `#include` Win32 头 / 自管 Win32 句柄？」**

⇒ 按 G-2 的**登记原文**，应用层拿到 `HICON` 后**无处可转**（转换函数不公开）⇒ **§4 判据与 §5 的缺口登记内部不自洽**。回填建议见 **§2.9**。

★ **补充理由（与 demo 无关的一条）**：`Decode` 的既有定位是「**把一个来源变成 `Image`**」（B1）——本阶段是**第三个来源**（内存 / 文件 / **系统**），把它放进 `Decode` 是**同族延展**，不是新能力。

### 2.2 D1 + 命名：`ECDI::Decode::DecodeSystemIcon`

**定案**：`namespace ECDI::Decode`；函数名 **`DecodeSystemIcon`**。

| 候选 | 判断 |
|---|---|
| **`DecodeIcon`** | ❌ **禁用**（评审 §19）——会被读成「`.ico` → `Image`」，而★ **该能力 K1 已存在**（WIC 解 `.ico`） |
| `DecodeShellIcon` | ⚠️ 准确但要求使用者知道 Win32 的「shell」概念——与「**人人能用**」的定位相悖 |
| **`DecodeSystemIcon`** ★ | ✅ **采用**——★ 与需求标题「**系统**图标 → Image」**同词**（命名即语义 + 一个概念只用一个词）；且与既有 `DecodeFile` / `DecodeMemory` **同构**（源 → `Image`） |

★ **`DecodeSystemIcon` 的语义**（写进头注释，避免歧义）：「取**该路径在系统中的图标**」——**不是**「取该路径指向的图像内容」（后者是 `DecodeFile`）。

### 2.3 ★★ Q1 + Q4（耦合）：`SHGFI_USEFILEATTRIBUTES` 一律使用

**定案**：**单一代码路径**——`SHGetFileInfoW` 恒带 `SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES`；`dwFileAttributes` 的取值：

| 情形 | 属性取值 | 依据 |
|---|---|---|
| `GetFileAttributesW(path)` **成功** | **实测真实属性**（★ 从而正确区分目录 / 文件） | **P2**（目录 `iIcon` 独立） |
| `GetFileAttributesW` **失败**（不存在） | **合成 `FILE_ATTRIBUTE_NORMAL`** | **P4**（不加该 flag 则拿不到图标） |

**为什么不做「两层递进（先试不加 flag）」**：

1. **P4**：不加 flag 时**不存在路径直接失败** ⇒ 无论如何都需要 `USEFILEATTRIBUTES` 这一支；
2. **P3**：已存在路径上**加与不加结果逐项相同**（`iIcon` + alpha 分布）⇒ 加 flag **无实测保真损失**；
3. ⇒ 多一个分支**换不到可观测收益** ⇒ 按 YAGNI 取**单路径**。

**★ 已知限制（如实记录 + 给出升级路径，不是非目标）**：

> `USEFILEATTRIBUTES` 的语义 = 「**按我给的属性/扩展名判定类型**」⇒ **不含「逐文件自定义图标」**：`desktop.ini` 定制的文件夹图标 · `.lnk` 的**目标**图标 · 可执行文件的内嵌图标。
> **升级路径**（本项目**已实测可用**）：先不加该 flag 试一次，失败再退回本方案——**成本 ≈ 1 次调用 + 1 个分支**（P3 的对照读数即其可行性证据）。

★ 这条按项目的「**已登记的延迟设计**」形态处理（候选编号见 §2.9 的回填建议）。

### 2.4 Q2：不做任何 COM 初始化

**定案**：**不调用 `CoInitializeEx`**；**不引入 `ComScope`**。

- ★ **实测依据（P1–P7 全程）**：探针**未做任何 COM 初始化**，**11 个用例全部成功**（含跨 shell 的图标取得）⇒ `SHGetFileInfo` **不要求 COM**。
- ⇒ **评审 §10「不得直接照搬 WIC 的经验」在此获得正面回应**：WIC 的 `ComScope`（B2）**是 WIC 的需要**，不是 shell 的需要。
- ⇒ Q2 四问**逐条**：

| 问 | 答 |
|---|---|
| **谁**负责 COM | **无人**——不需要 |
| **何时** | — |
| 是否要求调用线程**已初始化** | **不要求**（实测：未初始化亦可） |
| 自行初始化**是否与现有 apartment 冲突** | **不可能**——因为**不初始化**（问题自动消解） |

★ 顺带一条**结构事实**（B2）：`WicImageDecoder.cpp` 的 `ComPtr` / `ComScope` / `LogDecoderError` **都在匿名 namespace** ⇒ **不可跨文件复用** ⇒ 新文件**需要自己的一份日志辅助**（是否提为共享件 ⇒ **O1**）。

### 2.5 ★ Q3：内核 / 外壳分离——且★ **两层都可无头测试**

**定案**：两层，落点**都在平台层内部**（N6：**不新建公共抽象**）：

| 层 | 形态 | 可测性（★ 实测） |
|---|---|---|
| **内核** | `Image ImageFromHIcon(HICON icon)`（**平台层内部头**，B9 先例） | ★ **可无头**——用 `CreateIconIndirect` **自造图标**（**P6** 已实测：自造 32bpp+alpha 图标往返**逐点精确**）⇒ **premultiply / alpha 保真 / mask 合成 / 尺寸契约全部可断言** |
| **外壳** | `DecodeSystemIcon(utf8Path)`：`GetFileAttributesW` → `SHGetFileInfoW` → 调内核 → 释放 | ★ **也可无头**——**P1–P7 全部在「无 COM / 控制台 / 无 shell 交互」下完成** |

★★ **结论比需求预期更好**：需求 Q3 担心「shell 依赖可能不可无头」，**实测证否**（至少对 `USEFILEATTRIBUTES` 路径）⇒ **本阶段可以代码级闭环**，沿用 Phase 19/20 的「**真路径**」纪律（不造假、不 mock）。

★ **边界如实标注**：测试用**真实路径**（自建临时目录 / 必然不存在的路径）与**自造图标**，**不覆盖**「**无 explorer 的会话**」（记账，非本阶段）。

★ **N6 的落地**：落点 = `src/Platform/Win32/`；**公共面只加一个函数**；**不建** `IIconProvider` / `Win32IconProvider` / `ShellService`。

### 2.6 D2：单档 32px；且★ **框架不假设尺寸**

- 定案：**只用 `SHGFI_LARGEICON`**（实测 **32×32**，P1/P3/P5）；`SMALLICON`（实测 **16×16**，P6）与 48 / 256 **不做**（N4）。
- ★ **框架不假设图标尺寸**——**`Image.width` / `Image.height` 是权威值**，调用方（应用）通过 `DrawImage(dest, …)` 的 **dest 矩形**决定显示尺寸。⇒ 「将来尺寸随 DPI 变化」**不构成 API 形态问题**。
- ★ 与 Phase 20 的交互（**已知、可判、不阻塞**）：进程现为 Per-Monitor V2（Phase 20），`SM_CXICON` 会随 DPI 变（96 DPI 实测 32）；而 `SHGFI_LARGEICON` 在本机**两种感知级别下均为 32×32**（P8，因系统 DPI = 96 而**不可判定**）⇒ 记为**待实测**（§3.4），**不阻塞设计**。

### 2.7 ★★ D4：mask 只在没有 alpha 时才是信息源

**定案**：

| color 位深 | 处理 |
|---|---|
| **32bpp**（★ 实测 **11/11 例命中**，P5） | ★★ **直接用 alpha 通道；mask 不参与** |
| **< 32bpp**（老式；实测**未出现**但**必须防御**） | `alpha = mask_bit ? 0 : 255` |

★★ **判据来自实测（P5）**：shell 给出的 1bpp mask **置位数恰好等于 `alpha==0` 的像素数**（`355/355` · `456/456` · `339/339`）⇒ **mask 与 alpha 在「是否全透明」上等价**，而 **alpha 还带中间值**（`alpha(mid) = 18 / 28 / 247`）⇒ ★ **若改用 mask，抗锯齿会被压成硬边**。

⇒ ★★ **这把 D4 从「要实现一套合成算法」降级为「一个分支」**——**质量理由**（保 AA）与**简洁理由**同时成立。

★ 边界（**O3**）：`< 32bpp` 分支的 AND/XOR 约定（老式图标可借 mask 表达高光 / 阴影）**本机未实测到样本** ⇒ 详设须用 `CreateIconIndirect` **自造老式图标**补测后再冻结。

### 2.8 D5：premultiply 在框架内做；★ 实测排除「绕行风险」

- 定案：**在框架内**把直通 BGRA 折成**预乘**（`Image` 契约要求，B3）——`c' = c * a / 255`（★ 舍入口径 ⇒ **O2**）。
- ★★ **实测排除了一处已知风险（P6）**：`GetDIBits(32bpp, top-down)` **保留 alpha**（自造 `0/64/128/255` 图标往返**逐点精确**）⇒ **不需要**「绕开 `GetDIBits` 直接读 DIB section 的 `bmBits`」那套绕行。
- ★ **与 WIC 路径的差异（如实记录）**：`DecodeFile` 的预乘是 **WIC 代做**（`GUID_WICPixelFormat32bppPBGRA`）；**shell 路径没有这一层** ⇒ **必须自己乘**——这正是 D5 的由来。

### 2.9 ★ 与既有文档的偏离与回填建议（★ 须授权）

| # | 现状 | 建议 | 性质 |
|---|---|---|---|
| **①** | `desktopnest-roadmap.md` **§4** 的两条判据**缺第三问**（「留应用层会不会逼应用层 include Win32」） | **加第三条判据分支**，并把 §3（`:105`）与 §4（`:127`）的 `SHGetFileInfo` 行**改判为「✅ 进框架」**（附理由与指向本稿 §2.1） | **对既有已批准文档的内容改动** ⇒ ★ **本稿只提出，动手前须授权** |
| **②** | §2.3 的「逐文件自定义图标」限制 | 建议登记为 `roadmap-deferred.md` **新记账项**（形态同 `#44`/`#45`/`#46`：**已登记的延迟设计**，含触发条件 + 升级路径） | 同上 |
| **③** | `phase21-icon-to-image-requirements.md` **标题写 `（v1.0）` 而状态行写 `v1.1`** | ★ 建议标题同步为 **v1.1**（与 **Phase 20** 同族文档的做法一致——其标题 / 状态行为 `v1.1` / `v1.1`） | 一致性修正（1 行） |

★ 三项**均不影响本稿的设计结论**；③ 属明显笔误级修正，① / ② 属对既有文档的实质改动。

---

## 3. 接口与实现改动分解

### 3.1 改动清单（△1–△6）

| △ | 文件 | 改动 |
|---|---|---|
| **△1** | `include/ECDI/Decode/ImageDecoder.h` | ① 头注释更新（来源由「文件 / 内存」扩为「文件 / 内存 / **系统**」）② **+1 声明** `[[nodiscard]] Image DecodeSystemIcon(const std::string& utf8Path);`（**★ 既有头追加 ⇒ 头文件 0 新增**） |
| **△2** | **新建** `src/Platform/Win32/ShellImageDecoder.h` | 内部头（B9 先例）：声明内核 `Image ImageFromHIcon(HICON icon);`（★ **平台层内部** ⇒ Win32 类型允许；先例 `Win32PlatformWindow.h`） |
| **△3** | **新建** `src/Platform/Win32/ShellImageDecoder.cpp` | 实现：外壳（属性 → `SHGetFileInfoW` → 内核 → 释放）+ 内核（`GetIconInfo` → `GetDIBits` → alpha/mask → premultiply）+ 日志辅助；★ 需 `#ifdef DrawText` 之外**无需新 undef**（`shellapi.h` 经 `Windows.h` 传递，B10 先例），但**须遵守条 10 的宏防护** |
| **△4** | **新建** `src/Tests/IconDecodeTests.cpp` | 承载 **T21-1..T21-8**（§7） |
| **△5** | `src/Tests/RunAllTests.h` + `RunAllTests.cpp` | ★ **各 +1 登记**（B6：新增测试文件才需要） |
| **△6** | `docs/desktopnest-roadmap.md`（**建议，须授权**） | §2.9 ① 的回填 |

**明确不动的**：`CMakeLists.txt`（B8 自动入库）· `Renderer` / `RenderingBackend` / `GDIBackend`（**渲染侧零改动**）· `Image`（**契约零改动**，B3）· `PaintContext`（B4，消费侧已就绪）· `PlatformWindow` 族（**不新增纯虚**，故**无实现者清单**要盘）· `main.cpp` · `WicImageDecoder.cpp`（**不改** ⇒ 既有 WIC 路径零回归）· 既有 252 用例（**一条不改**）

★ **新增文件 3**（2 平台件 + 1 测试件）· **改动文件 3**（1 公共头 + 2 登记）· **建议改动 1**（文档）。

### 3.2 代码草图（仅示形态，精确规格交详设）

```cpp
// ShellImageDecoder.h（内部头——平台层）
namespace ECDI::Decode {
/// @brief HICON → Image（内核；★ 不触 shell，可无头测试）
/// @details 32bpp ⇒ 用 alpha；<32bpp ⇒ 用 mask。premultiply 在内部完成。
[[nodiscard]] Image ImageFromHIcon(HICON icon);
}

// ShellImageDecoder.cpp（内部匿名 namespace 的形态骨架）
namespace ECDI::Decode {
namespace {
    // 日志辅助（B2：WicImageDecoder 的同类辅助在匿名 namespace，不可复用 ⇒ 自带一份；是否提为共享件 ⇒ O1）
    void LogIconError(const wchar_t* step, unsigned long code);

    // 直通 BGRA → 预乘（★ 舍入口径 ⇒ O2）
    void PremultiplyInPlace(std::uint8_t* bgra, std::size_t pixelCount);
}

Image ImageFromHIcon(HICON icon) {
    // 1) GetIconInfo → hbmColor / hbmMask（★ ownership 独立于 HICON，必须 DeleteObject）
    // 2) GetDIBits(hbmColor, 32bpp, top-down)（★ P6 实测保 alpha）
    // 3) color 32bpp ⇒ 用 alpha；否则 ⇒ alpha = mask_bit ? 0 : 255（§2.7）
    // 4) premultiply → Image（stride = width*4）
    // 5) 所有临时资源无条件释放（C4）
}

Image DecodeSystemIcon(const std::string& utf8Path) {
    // 0) ★ 入口自校验：空 / 全空白 ⇒ 立即失败（P7：空路径 API 不报错）
    // 1) attrs = GetFileAttributesW(wide)；INVALID ⇒ FILE_ATTRIBUTE_NORMAL（§2.3）
    // 2) SHGetFileInfoW(wide, attrs, &sfi, sizeof(sfi),
    //                   SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES)
    // 3) sfi.hIcon 为空 ⇒ 空 Image + Log（C3）
    // 4) img = ImageFromHIcon(sfi.hIcon);  DestroyIcon(sfi.hIcon);  return img;   // C4
}
}
```

### 3.3 资源清单（★ A3 要求的第一版；**详设须冻结为「成功 / 中途失败」两栏**）

| 资源 | 取得处 | 释放 | 成功路径 | 中途失败路径 |
|---|---|---|---|---|
| `HICON`（`SHFILEINFOW::hIcon`） | `SHGetFileInfoW` | **`DestroyIcon`** | ✓ | ✓（**取得即接管**，后续任一步失败都释放） |
| `HBITMAP`（`ICONINFO::hbmColor`） | `GetIconInfo` | **`DeleteObject`** | ✓ | ✓ |
| `HBITMAP`（`ICONINFO::hbmMask`） | `GetIconInfo` | **`DeleteObject`** | ✓ | ✓ |
| `HDC`（屏幕 DC） | `GetDC(nullptr)` | **`ReleaseDC(nullptr, dc)`** | ✓ | ✓ |
| DIB 缓冲 | `std::vector` | RAII | ✓ | ✓ |

★★ **两个 ownership 事实（评审 §17 点名，必须写死）**：

1. **`GetIconInfo` 返回的两个 bitmap 与 `HICON` 的 ownership 无关**——`DestroyIcon(icon)` **不会**释放它们 ⇒ **必须各自 `DeleteObject`**。
2. **屏幕 DC 的 `ReleaseDC` 首参是 `nullptr`**（与 `GetDC(nullptr)` 配对）——★ `WicImageDecoder` 的 3 处先例即此形态。

---

## 4. 契约（C1–C8）

| # | 契约 | 判据 / 依据 |
|---|---|---|
| **C1** | **公共 API 零 Win32 类型**——`HICON` / `HBITMAP` / `SHFILEINFOW` 不出现在 `include/` 下任何头 | 核心不变量（**R2**）；grep 判据见 §5 |
| **C2** | 输出 = **既有 `Image` 契约**（32bpp premultiplied BGRA / top-down / `stride == width*4`），**与 `DecodeFile` 同口径** | **R3** · B3 |
| **C3** | 失败 ⇒ **空 `Image`（`width==0`）** + `Logger::Log(Error, …)` + **不抛异常** | **R4** · K5 · B1 |
| **C4** | **资源无条件释放**（入口失败 / 中途失败 / 成功三路）；`HICON` → `DestroyIcon`，两个 bitmap → `DeleteObject`，DC → `ReleaseDC` | **R5** · §3.3 |
| **C5** | ★ **不假设图标尺寸**——`Image.width/height` 为权威；框架不承诺 32×32 | §2.6（**为将来尺寸变化留口**） |
| **C6** | ★ **入口自校验**：`utf8Path` 为空 / 全空白 ⇒ **立即失败**（★ **不得依赖 API 报错**——**P7** 实测空路径返回成功） | **P7** |
| **C7** | ★ **环境无关**：**不依赖 COM 初始化**、**不依赖 explorer**（用 `USEFILEATTRIBUTES` ⇒ 不查外壳命名空间） | **P1–P7** · §2.4 |
| **C8** | ★ **像素口径**：32bpp ⇒ alpha；<32bpp ⇒ mask。预乘在框架内做（★ 舍入口径见 **O2**；与 WIC 路径**可能有 ±1 差** ⇒ 测试用 `EXPECT_NEAR(±1)`） | **P5/P6** · §2.7/§2.8 |

---

## 5. 影响面

| 项 | 值 / 判据 |
|---|---|
| **公共头** | **92 → 92**（★ **头文件 0 新增**；`ImageDecoder.h` **既有头追加**） |
| **公共 API** | ★★ **+1**（`Decode::DecodeSystemIcon`）——**口径必须精确**（评审 §18 / **A4**）：**「头文件数不变」≠「API 无变化」** |
| **测试** | **252 → 252 + N**（建议 **N = 8**，§7）；★ **既有 252 一条不改** |
| **新增文件** | **3**（`ShellImageDecoder.h` · `ShellImageDecoder.cpp` · `IconDecodeTests.cpp`） |
| **改动文件** | **3**（`ImageDecoder.h` · `RunAllTests.h` · `RunAllTests.cpp`） |
| **渲染侧 / `Image` / `CMakeLists.txt` / `main.cpp`** | **零改动** |
| ★ **C1 的 grep 判据** | `HICON` / `HBITMAP` / `SHFILEINFO` 在 `include/ECDI/**` 下**代码行 0 处**（★ 排除注释，条 44） |
| ★ **非 Windows 平台的连带** | `DecodeSystemIcon` 是**公共 API** ⇒ 未来非 Windows 实现**须提供等价函数**（返回空 `Image` + `Log` 即可）——与既有 `DecodeFile` / `DecodeMemory` **同一处置** |

---

## 6. 开放决策点（O1–O4）

| # | 事项 | 倾向 | 何时定 |
|---|---|---|---|
| **O1** | 新文件是否**复用** `WicImageDecoder.cpp` 的日志辅助（或提为共享件）？ | ★ 倾向**不复用、不自带新抽象**——① 两者错误上下文不同（一个带 `HRESULT`，一个带 `GetLastError` / 返回值）② 抽取要**动既有文件**（扩大影响面）。★ 但「第二个消费者」**确已出现** ⇒ 详设可复议 | 详设 |
| **O2** | **premultiply 的舍入口径**（`+127` vs `+128` vs 截断） | 倾向与既有**像素合成**口径对齐（`Render/CoverageRaster` 先例用 `+127`）；★ **与 WIC 路径的差**记为**已知近似（±1）**，测试用 `EXPECT_NEAR` | 详设 |
| **O3** | **`<32bpp` 分支**的 mask 语义（简单 alpha vs AND/XOR 约定） | ★ **本机无样本**（P5：shell 恒给 32bpp）⇒ 详设须用 `CreateIconIndirect` **自造老式图标**补测后再定 | 详设（**须补测**） |
| **O4** | 内核命名 / 归属（`ImageFromHIcon` 放内部头）是否够清楚 | 倾向沿用「`DpiConversion.h` / `CoverageRaster.h`」的**内部头**做法；命名待详设复核 | 详设 |

---

## 7. 测试方向（T21-1..T21-8）

★ **落点**：**新建 `src/Tests/IconDecodeTests.cpp`**（△4）+ **`RunAllTests.h/.cpp` 各 +1**（△5）。
★ **两层各覆盖**——内核用**自造图标**（确定性 + 断言到像素），外壳用**真实路径**（真路径纪律）。

| # | 层 | 驱动 | 断言 |
|---|---|---|---|
| **T21-1** ★ | 内核 | `CreateIconIndirect` 自造 **32bpp + alpha**（已知 alpha 分布） | **premultiply 逐点正确** + C2 契约（`stride == width*4`）+ **alpha 值保真**（★ P6 的回归锚） |
| **T21-2** ★ | 内核 | `CreateIconIndirect` 自造 **<32bpp + 1bpp mask** | **mask 分支**：`alpha = mask_bit ? 0 : 255`（★ 依赖 **O3** 的补测结论） |
| **T21-3** | 内核 | 退化输入（1×1 / 空 mask / `GetIconInfo` 失败路径） | 不崩 + 契约保持 + 失败 ⇒ 空 `Image`（C3/C4） |
| **T21-4** | 外壳 | **真实目录**（`GetTempPathW` 结果） | 非空 `Image` + C2 契约（★ **A6-② 文件夹语义**，P2 佐证 `iIcon` 独立） |
| **T21-5** | 外壳 | **不存在 + 有扩展名**（如 `…\nope.txt`） | ★ **非空**（**Q4 / P4**：`USEFILEATTRIBUTES` 的唯一价值） |
| **T21-6** | 外壳 | **不存在 + 无扩展名** | ★ **非空**（**A6-④**） |
| **T21-7** | 外壳 | **空 / 全空白 / `nullptr` 语义**（UTF-8 空串） | ★ **空 `Image`**（**C6 / R4**；**P7** 证明不能靠 API 报错） |
| **T21-8** | 端到端 | `DecodeSystemIcon` → `PaintContext::DrawImage` → `RecordingBackend` | `DrawImageCommand` 且 `image.width != 0`（★ 沿 `ImageDecodeTests` T7 先例，B4） |

★ **可断言性说明**：内核用例的期望值**完全由测试自己构造的图标决定** ⇒ **不依赖外部环境**、不 flaky；外壳用例只用「**必然存在的目录**」与「**必然不存在的路径**」，**不依赖 explorer**（C7 / P1–P7）。

---

## 8. 验收（需求 A1–A6 的落地口径）

| 需求 | 本稿落地 |
|---|---|
| **A1** 真实路径 ⇒ 非空 `Image` 且能画 | **T21-4 / T21-5 / T21-6**（断言） + **T21-8**（端到端） + ★ 应用侧目视（详设给最小示例） |
| **A2** 像素契约与 `DecodeFile` 同口径 | **T21-1..T21-4** 逐条断言 `stride == width*4` / 尺寸 / 缓冲大小 |
| **A3** 失败路径资源全销毁 + 空 `Image` + 记 Error | ★ **§3.3 资源清单已给出第一版**（**详设冻结两栏**）+ **T21-3 / T21-7** |
| **A4** 零回归 | **252 全绿**（既有一条不改）+ **公共头 92 → 92** + ★★ **公共 API +1**（口径见 §5）+ 四工具链 |
| **A5** 无头可测 | ★★ **本稿实测证明「内核与外壳都可无头」**（§2.5）⇒ **T21-1..T21-8 全部自动化** |
| **A6** Shell 场景矩阵（5 场景） | ① 已存在普通文件 → **T21-1/T21-4 的路径覆盖 + 目视** · ② 已存在文件夹 → **T21-4** · ③ 不存在 + 有扩展名 → **T21-5** · ④ 不存在 + 无扩展名 → **T21-6** · ⑤ 异常路径 → **T21-7** |

---

## 9. 交给详细设计的五件事

> 编号用 **①–⑤**（`D0–D5` 已被需求占用，`O1–O4` 被本稿占用）。

| # | 事项 | 已有输入 |
|---|---|---|
| **①** | **资源清单两栏冻结**（成功 / 中途失败）+ RAII 形态（含两个 ownership 事实） | **§3.3** · **C4** · 评审 §17 |
| **②** | **premultiply 舍入口径** + 与 WIC 路径 ±1 差的容差写法 | **O2** · **C8** |
| **③** | **`<32bpp` 分支**：用 `CreateIconIndirect` **自造老式图标**补测（本机无样本）后冻结算法 | **O3** · **T21-2** |
| **④** | **精确签名 / 头文件落点 / `#include` 与宏防护**（条 10；`shellapi.h` 经 `Windows.h` 传递的核实） | **B9** · **△2/△3** |
| **⑤** | **测试资产构造**（`CreateIconIndirect` 的位图准备、临时目录获取）+ **逐条期望值复算** | **§7** · skill 条 47/29⑤ |

---

## 10. 修订记录

- **v1.0**（2026-09-26）初稿。**输入**：需求 **v1.1**（评审通过）＋ ★★ **本稿新建的平台语义实测 P1–P8**（独立探针，零仓库侵入，全程无 COM / 无窗口）。**内容**：§1.2 代码基线 **B1–B10** 逐条带行号 · §1.4 **P1–P8** 实测 · §2 核心定案 **9 节**（★ **D0 与 `desktopnest-roadmap` §4 判据的冲突裁决** · **Q1+Q4 由 P4/P3 解开耦合** · **D4 由 P5 降级为"一个分支"**）· §3 改动分解 **△1–△6**（含**资源清单第一版**）· §4 **契约 C1–C8** · §5 影响面（★ A4 口径）· §6 **O1–O4** · §7 **T21-1..T21-8** · §8 A1–A6 落地 · §9 交详设 **五件事**。★ §2.9 列出 **3 处回填建议**（**须授权**：roadmap §3/§4 改判 · 新记账项 · 需求稿标题版本同步）。**待评审。**
