# Phase 21 · 系统图标 → Image（icon to image）—— 初步设计（v1.2）

> 来源：`phase21-icon-to-image-requirements.md` **v1.1**（评审通过）
> 状态：**v1.2**（2026-09-26）——✅ **评审通过**（外部评审结论：「**原则上通过，可以进入 Detailed Design**」）· v1.1 按其 **4 项必修 + 若干建议**逐条处置（见 **§1.5**）；★★ **v1.2 = 为详设做的前置探针（P9–P13）修正了 §2.7 的一条规则**——判据由「**位深**」改为「**alpha 是否携带信息**」，并据此**定案 O3**
> 本稿输入：需求 **R1–R5** · 决策点 **D0–D5** · 非目标 **N1–N6** · 待决 **Q1–Q4** · 验收 **A1–A6** ＋ ★★ **本稿新建的平台语义实测（§1.4 P1–P13，独立探针，2026-09-26）**

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
| **Q4** | **不存在也出图标**（按「路径类型」语义）；★ 由 **P4** 实测锁定 —— **在本稿采用的 `SHGetFileInfoW` 路径中，该 flag 是必要条件**（★ **不是**「Windows 所有途径中唯一」） | **§2.3** |
| （D0）★★ | **放框架**（并指出 roadmap §4 判据不完整） | **§2.1** |
| （D2） | 单档 `SHGFI_LARGEICON`（实测 **32×32**）；★ 框架**不假设尺寸** | **§2.6** |
| （D4）★★ | **mask 只在没有 alpha 时才是信息源**——32bpp 用 alpha、否则用 mask | **§2.7** |
| （D5） | premultiply **在框架内**做；★ 实测 `GetDIBits(32bpp)` **保 alpha** ⇒ 无绕行需求 | **§2.8** |

### 1.4 ★★ 平台语义实测（P1–P13，2026-09-26 独立探针）

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
| **P9** ★ | **多行方向正确**——3×2 自造 32bpp 图标：`GetDIBits`（top-down）与 `DrawIconEx` **两条路都给出「视觉顶行在前」** ⇒ 内核用 `biHeight = -h` 是对的 | 两路均 `row0=R · row1=G · row2=B` |
| **P10** ★★ | ★★ **`GetIconInfo` 把 color 归一化为 32bpp**——**即便传入 24bpp** 也报 **32bpp**；★★ **而该位图的 alpha 恒为 0** | color `bpp=32`；`GetDIBits(32)` → **8/8 像素 `A=00`**；mask 位模式（`0xF0`）**原样保留** |
| **P11** ★ | 同上，**传入 1bpp（单色）** 亦被归一化为 **32bpp**，alpha 恒 0 | color `bpp=32`；`A=00` |
| **P12** | `GetDIBits` 可**降位深**（32bpp 源请求 24bpp 成功）；★ **升位深未测——但已不需要**（P10/P11 使「恒请求 32bpp」成立） | `GetDIBits(req=24, src=32) OK` |
| **P13** ★★ | **`DrawIconEx` 到 32bpp DIB 产出的是「预乘 BGRA」**（`G255/A128 → 0x80`）★ **但对老式图标输出 alpha 全 0（不可用）** ⇒ **不能作为单一路径** | 32bpp 源：`A80` 保留且颜色被预乘；24bpp / 1bpp 源：**全部 `A=00`** |

★★ **P4 + P3 合起来直接解开 Q1/Q4 的耦合**：

> **在本稿采用的 `SHGetFileInfoW` 路径中，`SHGFI_USEFILEATTRIBUTES` 是「不存在路径也能按类型出图标」的必要条件**（★ **P4**：不加它则 `ret=0` / `hIcon=NULL`），**而它对已存在路径又无任何保真损失**（★ **P3**）⇒ 单一代码路径即可同时满足 Q4 与「已存在路径正确」。**无需两层递进**。
>
> ★ **范围声明（评审 §6）**：本稿**不研究 Windows 上「所有」获取图标的途径**，只裁决**本阶段选定的这一条**——因此措辞取「**在本路径中是必要条件**」，而**不写「是唯一途径」**（P4 只证明了「不加它，在我测的路径上失败」）。

★★ **P5 是本稿对 D4 的决定性输入**：mask 与 alpha **信息等价于「是否全透明」**，而 alpha 还带**中间值**（抗锯齿）⇒ **用 mask 会丢抗锯齿**（§2.7）。


★★ **P9–P13 修正了 §2.7 的一条规则（v1.2 的直接原因）**：

v1.0 / v1.1 写的是「**32bpp ⇒ 用 alpha；否则 ⇒ 用 mask**」——★★ **这条会坏掉老式图标**：**P10 / P11 实测 `GetIconInfo` 把 color 归一化为 32bpp**（无论原先是 24bpp 还是 1bpp），**而这类位图的 alpha 恒为 0** ⇒ 按位深判据会选中 alpha ⇒ **整幅图变成全透明（不可见）**。

★ **正确判据是「alpha 是否携带信息」，不是「位深」**：

> **先读 alpha；若 alpha 全为 0 ⇒ 视为「该位图不携带 alpha 信息」⇒ 改用 mask 决定不透明度。**

★ 该判据在两类图标上**都自洽**：**shell 图标**（**P5**：alpha 含 255 与中间值 ⇒ 非全 0 ⇒ 用 alpha）· **老式图标**（**P10 / P11**：alpha 全 0 ⇒ 用 mask）。**§2.7 已按此重写。**

★ **顺带排除一个更简单的方案**：P13 显示 `DrawIconEx` 对 32bpp 源**直接产出预乘 BGRA**（很有诱惑力——可省掉手写 premultiply），**但它对老式图标输出 alpha 全 0** ⇒ **不能作为单一路径**；若做成「32bpp 走 `DrawIconEx`、老式走手工」就成**两条路** ⇒ 按「**一条路径 + 可逐字节断言**」取**手工路径**（`GetDIBits` + 自算 premultiply）。
---

### 1.5 ★ 初设第一轮评审处置（2026-09-26）

> 评审结论：「**原则上通过，可以进入 Detailed Design**」，并给出 **4 项必修 + 若干建议**。

| 评审节 | 内容 | 处置 |
|---|---|---|
| §1 · §2 · §3 · §4 · §7 · §9 · §10 · §11 · §12 · §13 | 探针方法论成立 · **D0 论证成立** · **命名可冻结**（`DecodeSystemIcon`）· **Q1+Q4 单一路径赞成** · **COM 不初始化证据充分** · **内核 / 外壳分离是最重要的设计** · **不造 `IIconProvider` 正确** · **不假设 32×32 重要** · **D4 用 alpha 判断"最漂亮"** · **`<32bpp` 留 O3 正确** · premultiply 位置正确 | **采纳（未改动）** |
| **§5** ★ | `GetFileAttributesW` 失败 **≠ 一定「不存在」**——语义应写作「**无法取得属性**」 | ✅ **采纳** → **§2.3 新增语义段**（成因含权限 / 不可访问 / 路径形式不适用；表述改为「**无法取得属性 ⇒ fallback 到 `FILE_ATTRIBUTE_NORMAL`**」） |
| **§6** ★ | 「**唯一途径**」措辞过强——P4 只证明「**在本稿的 `SHGetFileInfoW` 路径中不加它失败**」 | ✅ **采纳** → **§1.4 收尾 block + §1.3 Q4 行**改为「**在本路径中是必要条件**」，并加**范围声明**（不研究 Windows 上所有途径） |
| **§15** ★★ | **T21-7 的 `nullptr` 与公共 API 签名不一致**（`const std::string&` 收不了 `nullptr`） | ✅ **采纳（本稿真实缺陷）** → **T21-8 改为仅 `""` / `"   "`**；★ **显式记明 `nullptr` 分支在本实现中不可达**（指针来自 `c_str()`）⇒ **不设用例** |
| **§16** ★★ | **A6-① 的映射不成立**——原写「已存在普通文件 → T21-1 / T21-4」，而 **T21-1 是内核自造图标** | ✅ **采纳（本稿真实缺陷）** → ★ **T21-4 改为「已存在普通文件」（测试 exe 自身路径）**、**T21-5 为「已存在目录」**，用例数 **8 → 9**；**§8 A6 映射重写** |
| **§17** | 「**异常路径**」应与「空路径」分开 | ✅ **采纳** → **A6-⑤ 拆为 ⑤a（空输入 → C6 → T21-8）/ ⑤b（平台失败 → C3）**；★ **如实标注 ⑤b 在本设计下难以构造**（P1–P7）⇒ 详设须尝试构造，**不可构造则记为防御性分支、不假称已覆盖** |
| **§14** | premultiply 舍入**不要只看与 WIC 是否一致**，还须看 4 条判据 | ✅ **采纳** → **O2 补四条判据**（既有像素口径 / 结果稳定 / 无溢出整数计算 / 测试容差） |
| **§18** | 资源清单须**进一步 RAII 化**（不是写一段 `// cleanup`） | ✅ **采纳** → **§9-① 明确「每个资源取得即进入作用域 owner」**，与 Phase 11 的 `ComPtr` / `ComScope` 同一纪律 |
| **§19** | roadmap 回填**不要在实现阶段偷偷改** | ✅ **一致（本稿已经如此处理）** → 保持 **§2.9「须授权」** |
| **§20** | 非 Windows 实现**保持谨慎**，别在 Phase 21 展开 Linux 侧设计 | ✅ **采纳** → **§5 非 Windows 行补「属未来实现策略，不在本阶段展开」** |

★ **我方核实的结论**：**§15 与 §16 两条确实成立**——`nullptr` 在 `const std::string&` 形参上**语法不可表达**；而 **T21-1 是内核层（`CreateIconIndirect`），与「已存在普通文件」无关**，原映射把两个测试维度混为一谈。两条均为**测试设计缺陷**，已按上述修正。★ **本稿的设计结论（D0 / D1 / Q1–Q4 / D2 / D4 / D5 / C1–C8）零变动**。


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
| `GetFileAttributesW` **返回 `INVALID_FILE_ATTRIBUTES`** | ★ **合成 `FILE_ATTRIBUTE_NORMAL`** | **P4**（不加该 flag 则拿不到图标） |

★★ **一处语义精确化（评审 §5，本稿原写得不严谨）**：`INVALID_FILE_ATTRIBUTES` 的语义是「**无法取得该路径的属性**」，**不等价于「路径不存在」**——理论成因还包括**权限 / 不可访问 / 路径形式不适用 / 其它 Win32 文件属性查询错误**。本阶段**不需要区分这些成因**（R1 要的是「按**路径类型**取图标」，而 shell 依扩展名即可给出类型图标）⇒ 表述取「**无法取得属性 ⇒ fallback 到 `FILE_ATTRIBUTE_NORMAL`**」，★ **不写成「失败（不存在）⇒ NORMAL」**。

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

### 2.7 ★★ D4：mask 只在「alpha 不携带信息」时才是信息源

**定案（★ v1.2 按前置探针 P9–P13 修正）**：

| 情形 | 判据 / 实测 | 处理 |
|---|---|---|
| **alpha 含非零值** | ★ **实测 shell 图标恒如此**（**P5**：255 与中间值都在） | ★★ **直接用 alpha；mask 不参与** |
| ★★ **alpha 全为 0** | ★ **实测：`GetIconInfo` 对「无 alpha 的源」一律把 color 归一化为 32bpp 且 alpha ≡ 0**（**P10 / P11**） | ★★ **用 mask**：`alpha = mask_bit ? 0 : 255` |

★★ **判据是「alpha 是否携带信息」，不是「位深」**——v1.0 / v1.1 写的「32bpp ⇒ 用 alpha」**会坏老式图标**（**P10 / P11**：老式图标也被报为 32bpp 但 alpha 恒 0 ⇒ 按位深判据得到的是**全透明、不可见**的图）。

★ **安全性论证（三处边界都自洽）**：

- 「alpha 全 0 **且** mask 有置位」⇒ 用 mask ⇒ 正确的透明区（**P10** 的情形）；
- 「alpha 全 0 **且** mask 全清」⇒ 用 mask 得 `alpha ≡ 255`（**全不透明**——正是「无透明老式图标」的正确结果）；
- ★ **「真·全透明图标」**（alpha 全 0 且 mask 全置位）⇒ 用 mask 得 `alpha ≡ 0` ⇒ **与用 alpha 同结果** ⇒ ★ 判据**不引入任何错误分支**。

★★ **另一条实测红利（P10 / P11）**：**`GetIconInfo` 归一化为 32bpp** ⇒ **内核不需要处理调色板 / 低位深**——`GetDIBits` **恒请求 32bpp** 即可（**P12** 证明同 / 降位深读取可用；★ **升位深未被需要**）。**这把「老式图标兼容」从「一套调色板转换」进一步压成「一个 alpha 全 0 的判据 + 一个 mask 分支」。**

★ 边界（**O3**）：AND/XOR 约定里「用 mask 表达高光 / 阴影」的用法**本机仍未出现样本** ⇒ 按 `mask_bit ? 0 : 255` 实现；**若消费者报出异常，再按 O3 重启**。

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
| **△4** | **新建** `src/Tests/IconDecodeTests.cpp` | 承载 **T21-1..T21-9**（§7） |
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
| **测试** | **252 → 252 + N**（建议 **N = 9**，§7）；★ **既有 252 一条不改** |
| **新增文件** | **3**（`ShellImageDecoder.h` · `ShellImageDecoder.cpp` · `IconDecodeTests.cpp`） |
| **改动文件** | **3**（`ImageDecoder.h` · `RunAllTests.h` · `RunAllTests.cpp`） |
| **渲染侧 / `Image` / `CMakeLists.txt` / `main.cpp`** | **零改动** |
| ★ **C1 的 grep 判据** | `HICON` / `HBITMAP` / `SHFILEINFO` 在 `include/ECDI/**` 下**代码行 0 处**（★ 排除注释，条 44） |
| ★ **非 Windows 平台的连带** | `DecodeSystemIcon` 是**公共 API** ⇒ 未来非 Windows 实现**须提供等价函数**（返回空 `Image` + `Log` 即可）——与既有 `DecodeFile` / `DecodeMemory` **同一处置**。★ **属未来实现策略，不在本阶段展开**（**不设计任何非 Windows 的图标来源**，如 freedesktop / XDG / desktop entry——评审 §20） |

---

## 6. 开放决策点（O1–O4）

| # | 事项 | 倾向 | 何时定 |
|---|---|---|---|
| **O1** | 新文件是否**复用** `WicImageDecoder.cpp` 的日志辅助（或提为共享件）？ | ★ 倾向**不复用、不自带新抽象**——① 两者错误上下文不同（一个带 `HRESULT`，一个带 `GetLastError` / 返回值）② 抽取要**动既有文件**（扩大影响面）。★ 但「第二个消费者」**确已出现** ⇒ 详设可复议 | 详设 |
| **O2** | **premultiply 的舍入口径**（`+127` / `+128` / 截断） | ★ 倾向与既有**像素合成**口径对齐（`Render/CoverageRaster` 先例用 `+127`）；★★ **详设须按四条判据一起定（评审 §14）**：① 与 **ECDI 既有像素算法口径**一致 ② **结果稳定** ③ **易写成无溢出的整数计算** ④ **测试容差**——★ **不只看「与 WIC 是否完全一致」**；与 WIC 路径的差记为**已知近似（±1）**，测试用 `EXPECT_NEAR` | 详设 |
| **O3** | 老式（无 alpha）图标的 mask 语义 | ★★ **✅ 已补测（本稿 v1.2，P9–P13）** ⇒ 结论并入 **§2.7**：判据是「**alpha 是否携带信息**」而非位深（**alpha 全 0 ⇒ 用 mask**）；且 `GetIconInfo` **归一化为 32bpp** ⇒ **无需处理调色板 / 低位深**。★ AND/XOR 的「高光 / 阴影」约定**本机仍无样本** ⇒ 按 `mask_bit ? 0 : 255` 实现，**消费者报异常时再按本项重启** | ✅ **已定案（v1.2）** |
| **O4** | 内核命名 / 归属（`ImageFromHIcon` 放内部头）是否够清楚 | 倾向沿用「`DpiConversion.h` / `CoverageRaster.h`」的**内部头**做法；命名待详设复核 | 详设 |

---

## 7. 测试方向（T21-1..T21-9）

★ **落点**：**新建 `src/Tests/IconDecodeTests.cpp`**（△4）+ **`RunAllTests.h/.cpp` 各 +1**（△5）。
★ **两层各覆盖**——内核用**自造图标**（确定性 + 断言到像素），外壳用**真实路径**（真路径纪律）。

★ **外壳用例的路径资产（零创建、零清理、必然成立）**：**已存在普通文件 = 测试 exe 自身路径**（`GetModuleFileNameW`）· **已存在目录 = `GetTempPathW` 的结果** · **不存在路径 = 沿用既有 `ImageDecodeTests` 的「必然不存在」形态**（`Z:/nonexistent_dir/…`）。

| # | 层 | 驱动 | 断言 |
|---|---|---|---|
| **T21-1** ★ | 内核 | `CreateIconIndirect` 自造 **32bpp + alpha**（已知 alpha 分布） | **premultiply 逐点正确** + C2 契约（`stride == width*4`）+ **alpha 值保真**（★ P6 的回归锚） |
| **T21-2** ★ | 内核 | ★ **`CreateIconIndirect` 自造「无 alpha」图标**（24bpp 或 1bpp color + 1bpp mask）——★ **实测 `GetIconInfo` 会把它归一化为 32bpp 且 alpha ≡ 0**（P10 / P11） | ★★ **mask 分支**：断言 `alpha = mask_bit ? 0 : 255`（**O3 已定案**）；★ **并断言「若误用 alpha 则全透明」不会发生**（即结果中**存在 `alpha == 255` 的不透明像素**） |
| **T21-3** | 内核 | 退化输入（1×1 / 空 mask / `GetIconInfo` 失败路径） | 不崩 + 契约保持 + 失败 ⇒ 空 `Image`（C3/C4） |
| **T21-4** ★ | 外壳 | ★ **已存在普通文件**（**测试 exe 自身路径**，`GetModuleFileNameW`） | 非空 `Image` + C2 契约（★ **A6-①**；★ 评审 §16 指出的**原映射缺口**） |
| **T21-5** | 外壳 | ★ **已存在目录**（`GetTempPathW` 结果） | 非空 `Image` + C2 契约（★ **A6-② 文件夹语义**，P2 佐证 `iIcon` 独立） |
| **T21-6** | 外壳 | **不存在 + 有扩展名**（如 `Z:/nonexistent_dir/nope.txt`） | ★ **非空**（**Q4 / P4**：`USEFILEATTRIBUTES` 的价值所在） |
| **T21-7** | 外壳 | **不存在 + 无扩展名** | ★ **非空**（**A6-④**） |
| **T21-8** ★ | 外壳 | ★ **空串 / 全空白串**（`DecodeSystemIcon("")` · `DecodeSystemIcon("   ")`） | ★ **空 `Image`** + 记 Error + **不抛异常**（**C6 入口自校验 / R4**；**P7** 证明**不能靠 API 报错**） |
| **T21-9** | 端到端 | `DecodeSystemIcon` → `PaintContext::DrawImage` → `RecordingBackend` | `DrawImageCommand` 且 `image.width != 0`（★ 沿 `ImageDecodeTests` T7 先例，B4） |

★ **可断言性说明**：内核用例的期望值**完全由测试自己构造的图标决定** ⇒ **不依赖外部环境**、不 flaky；外壳用例只用「**必然存在的文件 / 目录**」与「**必然不存在的路径**」，**不依赖 explorer**（C7 / P1–P7）。

★★ **`nullptr` 不作为用例（评审 §15 —— 本稿的真实缺陷，已修正）**：公共 API 收 **`const std::string&`** ⇒ `DecodeSystemIcon(nullptr)` **在语法上不可表达**。§1.4 的 **P7** 测的是**底层 Win32 行为**（`SHGetFileInfoW(nullptr)`）⇒ 它**不能直接变成公共 API 的测试场景**；且实现里传给 Win32 的指针来自 **`std::wstring::c_str()`（永不为 null）** ⇒ 该分支**在本实现中不可达**，**故不设用例**。

★★ **「格式合法但平台无法处理」这一维度（评审 §17）**：**⑤a 空输入** ⇒ 归 **T21-8**（**C6 入口自校验**）；**⑤b 平台调用失败** ⇒ 归 **C3**，但 ★ **在本设计下难以构造**（P1–P7 显示 `USEFILEATTRIBUTES` 对任意**非空**路径基本都成功）⇒ **详设须尝试构造**（候选：超长路径 / 含非法字符）；★ **若确不可构造，则明确记为「防御性分支、无自动化用例」**——**不假称已覆盖**。

---

## 8. 验收（需求 A1–A6 的落地口径）

| 需求 | 本稿落地 |
|---|---|
| **A1** 真实路径 ⇒ 非空 `Image` 且能画 | **T21-4 / T21-5 / T21-6 / T21-7**（断言） + **T21-9**（端到端） + ★ 应用侧目视（详设给最小示例） |
| **A2** 像素契约与 `DecodeFile` 同口径 | **T21-1..T21-5** 逐条断言 `stride == width*4` / 尺寸 / 缓冲大小 |
| **A3** 失败路径资源全销毁 + 空 `Image` + 记 Error | ★ **§3.3 资源清单已给出第一版**（**详设冻结两栏 + RAII 化**）+ **T21-3 / T21-8** |
| **A4** 零回归 | **252 全绿**（既有一条不改）+ **公共头 92 → 92** + ★★ **公共 API +1**（口径见 §5）+ 四工具链 |
| **A5** 无头可测 | ★★ **本稿实测证明「内核与外壳都可无头」**（§2.5）⇒ **T21-1..T21-9 全部自动化** |
| **A6** Shell 场景矩阵（5 场景） | ① 已存在普通文件 → **T21-4** ★（**原映射不成立，已修正**：T21-1 是**内核**自造图标，**不是普通文件路径**） · ② 已存在文件夹 → **T21-5** · ③ 不存在 + 有扩展名 → **T21-6** · ④ 不存在 + 无扩展名 → **T21-7** · ⑤ 异常 / 不支持路径 → ★ **拆为两个维度（评审 §17）**：**⑤a 空 / 全空白输入** → **T21-8**（C6）· **⑤b 平台调用失败** → **C3**，★ **当前设计下难以构造**（见 §7 说明）⇒ 详设须尝试构造，不可构造则**明确记为防御性分支** |

---

## 9. 交给详细设计的五件事

> 编号用 **①–⑤**（`D0–D5` 已被需求占用，`O1–O4` 被本稿占用）。

| # | 事项 | 已有输入 |
|---|---|---|
| **①** | **资源清单两栏冻结**（成功 / 中途失败）+ ★★ **RAII 化**：**每个资源「取得之后立即进入作用域 owner」**（`HICON` → `DestroyIcon` 的 scope owner · 两个 `HBITMAP` → `DeleteObject` 的 scope owner · DC → `ReleaseDC` 的 scope owner），从而 `GetDIBits` 中途失败 / 分配失败 / `Image` 构造失败**都天然走释放路径**——★ **不是写一段 `// cleanup`**（评审 §18；沿 Phase 11 的 `ComPtr` / `ComScope` 工程纪律） | **§3.3** · **C4** · 评审 §17 / §18 |
| **②** | **premultiply 舍入口径** + 与 WIC 路径 ±1 差的容差写法 | **O2** · **C8** |
| **③** | ✅ **已完成（本稿 v1.2 的前置探针 P9–P13）**：老式图标的 mask 语义已定案并入 **§2.7** ⇒ **详设只需按此实现 + 写 T21-2，无需再补测** | **O3（已定案）** · **T21-2** |
| **④** | **精确签名 / 头文件落点 / `#include` 与宏防护**（条 10；`shellapi.h` 经 `Windows.h` 传递的核实） | **B9** · **△2/△3** |
| **⑤** | **测试资产构造**（`CreateIconIndirect` 的位图准备 · **`GetModuleFileNameW` / `GetTempPathW` 取真实路径**）+ **逐条期望值复算** + ★ **「平台调用失败」分支的可构造性实测**（§7 说明） | **§7** · skill 条 47/29⑤ |

---

## 10. 修订记录

- **v1.2**（2026-09-26）**为详设做的前置探针（P9–P13）修正 §2.7 + 定案 O3**。① **动机**：**O3** 明写「详设须用 `CreateIconIndirect` 自造老式图标补测后再定」，且初设还缺两项基础事实（**多行方向** / **是否存在更简单的单一路径**）。② ★★ **修正 §2.7 的一条规则**：判据由「**位深**（32bpp ⇒ 用 alpha）」改为「**alpha 是否携带信息**（**alpha 全 0 ⇒ 用 mask**）」——**P10 / P11 实测 `GetIconInfo` 对无 alpha 的源一律把 color 归一化为 32bpp 且 alpha ≡ 0**，按原判据会得到**全透明、不可见**的老式图标。★ **§2.7 已重写**，并给出**三处边界的安全性论证**。③ ★★ **另一条实测红利**：`GetIconInfo` **归一化为 32bpp** ⇒ **内核无需处理调色板 / 低位深**，`GetDIBits` 恒请求 32bpp ⇒ 「老式图标兼容」由「一套调色板转换」**进一步压成「一个判据 + 一个分支」**。④ ★ **排除一个更简单的方案**：**P13** 显示 **`DrawIconEx` 对 32bpp 源直接产出预乘 BGRA**（可省手写 premultiply），**但对老式图标输出 alpha 全 0** ⇒ 不能作单一路径；做成两条路则违背「一条路径 + 可逐字节断言」⇒ **取手工路径**。⑤ ★ **补多行方向事实**：**P9** 证明 `GetDIBits`（top-down）与 `DrawIconEx` **都给出「视觉顶行在前」** ⇒ 内核用 `biHeight = -h` 正确。⑥ **三处同步**：**O3 标 ✅ 已定案** · **T21-2 更新**（含「结果中必须存在 `alpha == 255`」的反向断言）· **§9-③ 标 ✅ 已完成**。⑦ ★ **设计结论零变动**：D0 / D1 / Q1–Q4 / D2 / D5 / C1–C8 **全部保持**（★ 仅 §2.7 的**判据表述**被实测修正）。⑧ 头部 v1.1 → **v1.2**。
- **v1.1**（2026-09-26）**评审第一轮处置 —— ★ 14 项，其中 4 项必修（2 项是本稿真实缺陷）**。① **评审结论**：「**原则上通过，可以进入 Detailed Design**」；逐条处置见 **§1.5**。② ★★ **修正本稿 2 处真实缺陷**：**`nullptr` 测试在公共 API 上不可表达**（`const std::string&`）⇒ **T21-8 改为仅 `""` / `"   "`**，并记明该分支**在本实现中不可达**（指针来自 `c_str()`）· **A6-①「已存在普通文件」的映射不成立**（原指 T21-1，而那是**内核自造图标**）⇒ **T21-4 改为「已存在普通文件」**（测试 exe 自身路径）、**T21-5 为「已存在目录」**。③ ★ **两处措辞收紧**：`GetFileAttributesW` 失败 ⇒ 「**无法取得该属性**」（**不等价于「不存在」**，成因含权限 / 不可访问等）· 「**唯一途径**」⇒ 「**在本稿采用的 `SHGetFileInfoW` 路径中是必要条件**」+ 范围声明。④ ★ **A6-⑤ 拆为两维**：**⑤a 空输入 → C6**（T21-8）· **⑤b 平台失败 → C3**，★ **如实标注后者在本设计下难以构造**（P1–P7）⇒ 详设须尝试构造，不可构造则记为防御性分支。⑤ **用例数 8 → 9**（§5 / §7 / §8 / △4 同步）。⑥ **O2 补四条判据** · **§9-① 补 RAII 化要求** · **§5 非 Windows 行收敛**。⑦ **新增 §1.5**（本轮评审处置表）。⑧ ★ **顺带修一处字面转义残留**：§7 的 `…\nope.txt` → `Z:/nonexistent_dir/nope.txt`。⑨ **设计结论零变动**：D0 / D1 / Q1–Q4 / D2 / D4 / D5 / C1–C8 **全部原样保留**。⑩ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-09-26）初稿。**输入**：需求 **v1.1**（评审通过）＋ ★★ **本稿新建的平台语义实测 P1–P13**（独立探针，零仓库侵入，全程无 COM / 无窗口）。**内容**：§1.2 代码基线 **B1–B10** 逐条带行号 · §1.4 **P1–P13** 实测 · §2 核心定案 **9 节**（★ **D0 与 `desktopnest-roadmap` §4 判据的冲突裁决** · **Q1+Q4 由 P4/P3 解开耦合** · **D4 由 P5 降级为"一个分支"**）· §3 改动分解 **△1–△6**（含**资源清单第一版**）· §4 **契约 C1–C8** · §5 影响面（★ A4 口径）· §6 **O1–O4** · §7 **T21-1..T21-8** · §8 A1–A6 落地 · §9 交详设 **五件事**。★ §2.9 列出 **3 处回填建议**（**须授权**：roadmap §3/§4 改判 · 新记账项 · 需求稿标题版本同步）。**待评审。**
