# Phase 21 · 系统图标 → Image（icon to image）—— 详细设计（v1.2）

> 来源：`phase21-icon-to-image-requirements.md` **v1.1** · `phase21-icon-to-image-preliminary-design.md` **v1.4**（评审通过 + 前置探针修正；★ **v1.3 = 回扫修正「引用旧判据的其它四处」** · **v1.4 = §2.9 回填 ①② 标 ✅ 收口**）
> 状态：**v1.2**（2026-09-27）——★ **评审通过**（v1.0 外部评审的 **2 项微调已处置** + **1 项自查修正**；逐条见 **§1.5**）⇒ ★★ **已实施并收口**（**批一 + 批二 + 三处缺陷修复**；★ 实施记录与逐条偏离见 **§7.1**，用例 **261 / 261**，三链 @125% 实测全绿 + gdb 下逐位同值）
> 本稿输入：需求 **R1–R5 / D0–D5 / N1–N6 / A1–A6** ＋ 初设定案 **Q1–Q4 / C1–C8 / O1–O4 / △1–△6 / 资源清单第一版** ＋ ★★ **本稿新增实测 P9–P13**（`<32bpp + mask` 补测 · 多行方向 · `DrawIconEx` 替代方案）＋ ★ **构建级核实 B11–B14** ＋ ★ **v1.0 外部评审**

---

## 1. 设计输入与基线

### 1.1 设计输入

| 项 | 内容 |
|---|---|
| **需求** | `docs/phase21-icon-to-image-requirements.md` **v1.1** |
| **初设** | `docs/phase21-icon-to-image-preliminary-design.md` **v1.4**（★ 含 **§2.7 的实测修正**：不透明度判据由「位深」改为「**alpha 是否携带信息**」） |
| **上游已定** | **D0** 放框架 · **D1 + 命名** `Decode::DecodeSystemIcon` · **Q1/Q4** `SHGFI_USEFILEATTRIBUTES` 一律使用 · **Q2** 不做 COM 初始化 · **Q3** 内核 / 外壳分离（两层都可无头）· **D2** 单档 `SHGFI_LARGEICON` 且**不假设尺寸** · **D5** premultiply 在框架内 |
| **本稿新增** | ★★ **P9–P13 实测**（§1.2）＋ ★ **B11–B13 构建级核实**（§1.4） |

### 1.2 本稿新增实测（P9–P13，2026-09-26 第二支独立探针）

> 动机：初设 **O3** 明写「详设须用 `CreateIconIndirect` 自造老式图标补测后再定」，且初设还缺两项基础事实（**多行方向** · **是否存在更简单的单一路径**）。方式与 §1.4 同（**零仓库侵入 · 控制台 · 无 COM · 无窗口**，输出落文件后读取）。

| # | 结论 | 原始读数 |
|---|---|---|
| **P9** ★ | **多行方向正确**——3×2 自造 32bpp 图标（视觉顶 = RED）：`GetDIBits`（top-down）与 `DrawIconEx` **两条路都给出「视觉顶行在前」** ⇒ 内核用 **`biHeight = -h`** 是对的 | 两路均 `row0=R · row1=G · row2=B` |
| **P10** ★★ | ★★ **`GetIconInfo` 把 color 归一化为 32bpp**——**传入 24bpp 也报 32bpp**；★★ **而该位图的 alpha 恒为 0** | color `bpp=32`；`GetDIBits(32)` → **8/8 像素 `A=00`**；mask 位模式（`0xF0`）**原样保留** |
| **P11** ★ | 同上，**传入 1bpp（单色）** 亦归一化为 **32bpp**，alpha 恒 0 | color `bpp=32`；`A=00` |
| **P12** | `GetDIBits` 可**降位深**（32bpp 源请求 24bpp 成功）；★ **升位深未被需要**（P10/P11 使「恒请求 32bpp」成立） | `GetDIBits(req=24, src=32) OK` |
| **P13** ★★ | ★★ **`DrawIconEx` 到 32bpp DIB 产出的是「预乘 BGRA」**（`G255/A128 → 0x80`）★ **但对老式图标输出 alpha 全 0（不可用）** ⇒ **不能作为单一路径**；★ 且它与 `GetDIBits` 的差（**straight vs 预乘**）**正好证明「必须自己预乘」** | 32bpp 源：`A80` 保留且颜色被预乘；24bpp / 1bpp 源：**全部 `A=00`** |

★★ **P9–P13 对设计的直接影响（已回填初设 v1.2 §2.7）**：

1. **不透明度判据 = 「alpha 是否携带信息」**（**alpha 全 0 ⇒ 用 mask**），★ **不是「位深」**——按位深判会让**老式图标变成全透明（不可见）**（P10 / P11）。
2. **`GetIconInfo` 归一化为 32bpp** ⇒ **内核无需处理调色板 / 低位深**，「老式图标兼容」由「一套调色板转换」压成「**一个判据 + 一个 mask 分支**」。
   ★★ **口径收窄（v1.1，评审 R-①）**：本稿的「**老式图标兼容**」**专指** `GetIconInfo` 返回 **`hbmColor != nullptr`**、**而 color 位图不携带 alpha 信息**的形态（= P10 / P11 实测到的形态）；★ **不含**真正的 **monochrome `HICON`**（`ICONINFO::hbmColor == nullptr`——此时 `hbmMask` **自带 AND / XOR 双段**、bitmap 高度为 `2h`，是**另一种表示**）⇒ **该形态不在支持范围**（**C13**）。★★ **两个概念不得混称「老式图标」**。
3. **排除 `DrawIconEx` 单路径方案**（对老式图标不可用）⇒ 取**手工路径**（`GetDIBits` + 自算 premultiply）——**一条路径 + 可逐字节断言**。
4. **多行方向**（P9）⇒ `biHeight = -h`（top-down 请求）**正确**。

### 1.3 上游结论 → 本稿落地处

| 上游 | 本稿落地 |
|---|---|
| 初设 **△1–△6**（改动分解） | **§3**（精确到行级 + 代码全文） |
| 初设 **§3.3 资源清单（第一版）** | **§2.3**（★ **两栏冻结 + RAII 形态**） |
| 初设 **O1**（是否复用 `WicImageDecoder` 的日志辅助） | **§2.5-① 定案**（**不复用**，理由见该处） |
| 初设 **O2**（premultiply 舍入口径） | **§2.1 定案**：★ **`(c * a + 127) / 255`**——**与既有 `Render/CoverageRaster.cpp:41-43` 同族口径**（**B14** 实测） |
| 初设 **O3**（老式 mask 语义） | ✅ **已由 P9–P13 定案**（并入初设 §2.7） |
| 初设 **O4**（内核命名 / 归属） | **§3.2 定案**：`ImageFromHIcon` 放**内部头** `src/Platform/Win32/ShellImageDecoder.h` |
| 初设 **§9-①–⑤**（交详设五件事） | **§2.3 / §2.1 / §3 / §6**（③ 已提前完成） |

### 1.4 构建级事实核实（B11–B14，本稿新增）

| # | 事实 | 证据 | 影响 |
|---|---|---|---|
| **B11** | ★★ **`shell32` 已在链接列表** ⇒ `SHGetFileInfoW` **零 CMake 改动** | `CMakeLists.txt:75`（`target_link_libraries(ECDI PUBLIC shell32)`——Phase 14 托盘引入） | ★ **消除一个潜在构建阻塞** |
| **B12** | `WideToUTF8` **是公共助手**（`namespace ECDI`）⇒ 测试可把宽路径转成 API 要的 UTF-8 | `include/ECDI/Core/String.h`（与 `UTF8ToWide` 同处） | 测试资产（§6.2）可直接用 |
| **B13** | **登记机制**：新测试文件 ⇒ `RunAllTests.h` 声明（现有 **25** 条）+ `RunAllTests.cpp` 调用（现有 **23** 条）**各 +1** | `RunAllTests.h:26` · `RunAllTests.cpp:24`（`RegisterDpiTests` 为最后一次插入的先例） | §3.4 |
| **B14** | ★ **既有像素合成口径 = `(c * a + 127) / 255`** ⇒ **O2 的定案有既有先例支撑** | `src/Render/CoverageRaster.cpp:41-43` | §2.1 的舍入规则 |

### 1.5 ★ v1.0 外部评审处置（2026-09-26）

> 评审结论：**总体通过、允许进入实施**，要求实施前处置 **2 个技术点**（原表 🟡 × 2）。★ 本稿**逐条处置并升 v1.1**；★ 另附 **1 条自查发现**（v1.0 自带、评审未提）。

| # | 评审意见 | 严重度 | 处置 | 落点 |
|---|---|---|---|---|
| **R-①** | ★ **真 monochrome `HICON`（`ICONINFO::hbmColor == nullptr`）未被覆盖**，而「老式图标兼容」表述偏宽 ⇒ 建议**明确「不在本阶段覆盖」**（评审**倾向方案 A：不扩范围**） | 🟡 | ✅ **采纳方案 A，并加严（不只是收窄措辞）**——① **定义收窄**（§1.2）· ② **该分支显式可观测**（§2.1 第 3 步拆两支 + **专属日志**）· ③ **立契约 C13** · ④ **记重启条件 + 备选 O7** | §1.2 · §2.1 · §2.5-⑪ · **C13** · §6.4 · **O7** |
| **R-②** | ★ **「不抛异常」（C3）与 `std::vector::resize()` 的分配异常未闭合**；★ 评审**未能定案**——「**单从当前文档无法证明哪一种才是 ECDI 的既有规则**」，须结合 Phase 11 实现 | 🟡 | ✅ **已由源码定案**（★ **不是新策略，是把既成口径写清**）——`WicImageDecoder.cpp:152` 的 `pixels.resize(vectorSize)` **同为裸调用、同样不捕获**；★ **全框架生产代码零 `try` / `catch`** ⇒ **既有口径 = 「本层不主动抛」**，`std::bad_alloc` **不在 `Decode` 层捕获** ⇒ 立 **C14**（**精确化 C3，零行为变化**） | **C14** · §2.1 |
| **S-①** | ★ **自查（v1.0 自带，评审未提）**：§3.3 声明 `LogIconError(const wchar_t*, unsigned long)`（**两参**），而 §2.1-1 / §2.2-1 的调用点是**单参** ⇒ **签名与调用点不一致，实施时必卡**；★ 同族：头部写「构建级核实 **B11–B13**」而 §1.4 已含 **B14** | 🟢 | ✅ 补默认实参 `code = 0` + 立**「日志口径」**；★ 头部范围订正为 **B11–B14** | §3.3 · §2.2 |

★ **评审明确认可、本稿原样保留的部分**：内核 / 外壳算法路径 · **三个局部 RAII 守卫**（★ 评审**赞成**不泛化 `UniqueWin32Handle<T>`）· `SHGFI_USEFILEATTRIBUTES` 单一路径 · **排除 `DrawIconEx` 单路径** · `DecodeSystemIcon` 归属 `Decode` 层 · **两批实施序** · **测试矩阵 T21-1..T21-9** · **§2.3 的三条 ownership 事实**。

---

## 2. 算法规格

### 2.1 ★★ 内核 `ImageFromHIcon(HICON icon)`

**落点**：`src/Platform/Win32/ShellImageDecoder.h`（声明，**内部头**）+ `.cpp`（定义）。**平台层内部**，公共面不见 `HICON`（**C1**）。

> ★ **编号仍为 11 步**——第 3 步**内部分两支**（**C13**），**不改编号**（避免 §1.3 / §7 / 索引的引用连锁）。★★ 第 8 / 9 步的承载是 **`BitmapInfo256`**（`BITMAPINFO` + 255 项调色板，§3.3）：`BitmapInfo256 bi32{}; FillBiTopDown(bi32.info, w, h, 32);` · `bi1` 同形——★ **`GetDIBits` 收 `&bi.info`**（**盯防 ⑫**：单槽 `BITMAPINFO` 对 1bpp **越界写 4 字节**）。

```text
 1  ICONINFO ii{};  if (!GetIconInfo(icon, &ii))                  → LogIconError(L"GetIconInfo") + return {}
 2  ★ RAII ①  IconBitmaps 守卫接管 { ii.hbmColor, ii.hbmMask }，析构 DeleteObject（nullptr 安全）
 3  ★ C13 支持边界：if (ii.hbmColor == nullptr) → LogIconError(L"monochrome HICON unsupported") + return {}
    ★ 该日志**专属**此形态（盯防 ⑪），**不复用**下一支 `GetObject` 失败的日志
    BITMAP bc{};  if (GetObject(ii.hbmColor, sizeof(bc), &bc) != static_cast<int>(sizeof(bc)))   // ★ 显式转换（消 signed/unsigned）  → Log + return {}
 4  w = bc.bmWidth; h = bc.bmHeight;   if (!ValidIconSize(w, h))  → Log + return {}          // §2.4
 5  stride = w * 4;  bufferSize = stride * h
 6  Image img;  img.width = w; img.height = h; img.stride = stride; img.pixels.resize(bufferSize);
 7  ★ RAII ②  ScreenDc 守卫取 GetDC(nullptr)；失败 → Log + return {}
 8  ★ 读 color（恒 32bpp / top-down —— P10/P11/P12）
      BitmapInfo256 bi32{};  FillBiTopDown(bi32.info, w, h, 32)          // ★ 256 项承载（盯防 ⑫）
      if (GetDIBits(dc, ii.hbmColor, 0, static_cast<UINT>(h), img.pixels.data(), &bi32.info, DIB_RGB_COLORS) != h)
          → Log + return {}
 9  ★★ 不透明度判据（P10/P11 —— 初设 §2.7）
      if (!AnyAlphaNonZero(img.pixels)) {                       // 「该位图不携带 alpha 信息」
          maskStride = ((w + 31) / 32) * 4
          std::vector<uint8_t> mask(maskStride * h)
          BitmapInfo256 bi1{};  FillBiTopDown(bi1.info, w, h, 1)          // ★ 256 项承载——1bpp 会写 2 项（盯防 ⑫）
          if (ii.hbmMask == nullptr
              || GetDIBits(dc, ii.hbmMask, 0, static_cast<UINT>(h), mask.data(), &bi1.info, DIB_RGB_COLORS) != h)
              → Log + return {}
          for each pixel (x, y):
              bit = (mask[y * maskStride + (x >> 3)] >> (7 - (x & 7))) & 1
              img.pixels[(y * w + x) * 4 + 3] = bit ? 0 : 255      // ★ mask 置位 = 全透明
      }
10  PremultiplyInPlace(img.pixels.data(), w * h)                    // ★ 见下「舍入口径」
11  return img
```

**★ 舍入口径（**O2 定案**，B14）**：

```cpp
// 与既有像素合成口径同族（Render/CoverageRaster.cpp:41-43）
out = static_cast<std::uint8_t>((c * a + 127) / 255);   // c / a 均为 0..255 的 unsigned
```

- ★ **只在 alpha 路径有舍入**：mask 路径的 `a ∈ {0, 255}` ⇒ `(c*255+127)/255 = c`、`(c*0+127)/255 = 0` ⇒ **恒等，无误差**。
- ★ **与 WIC 路径的关系**：WIC 的 `32bppPBGRA` 由 WIC 内部预乘，**±1 可能有差** ⇒ 记为**已知近似**；★ **但本阶段不对两条路径做交叉断言**（各按自身规则逐字节断言）⇒ **不引入容差依赖**。
- ★ **溢出口径**：`c` 与 `a` 均 ≤ 255 ⇒ `c*a ≤ 65025`，`+127` 后 ≤ 65152 ⇒ **`int` 域内绝不溢出**；用 `unsigned` 中间量亦可。

**★ 为什么「先判 alpha、再决定读 mask」**：alpha 可用时**省一次 `GetDIBits`**（shell 图标恒走此路，P5）；判据本身是 O(n) 扫描，一次性。

### 2.2 ★ 外壳 `DecodeSystemIcon(const std::string& utf8Path)`

**落点**：`src/Platform/Win32/ShellImageDecoder.cpp`（实现）——公共声明在 `include/ECDI/Decode/ImageDecoder.h`（**△1**）。

```text
 1  ★ C6 入口自校验（P7：空路径 API **不报错**，故不能依赖它）
      if (utf8Path.empty() || utf8Path.find_first_not_of(" \t\r\n") == std::string::npos)
          → LogIconError(L"empty path") + return {}
 2  const std::wstring wide = UTF8ToWide(utf8Path);
      if (wide.empty())                      → Log + return {}        // 转换失败
 3  ★ 属性（初设 §2.3）：DWORD attrs = GetFileAttributesW(wide.c_str());
      if (attrs == INVALID_FILE_ATTRIBUTES) attrs = FILE_ATTRIBUTE_NORMAL;
      ★ 语义 = 「**无法取得该路径的属性**」（**不等于「不存在」**）⇒ fallback，而非语义化成 nonexistent
 4  SHFILEINFOW sfi{};
      const DWORD_PTR r = SHGetFileInfoW(wide.c_str(), attrs, &sfi, static_cast<UINT>(sizeof(sfi)),   // ★ 显式转换（消 size_t→UINT）
                                         SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
      if (r == 0 || sfi.hIcon == nullptr)    → LogIconError(L"SHGetFileInfoW", GetLastError()) + return {}   // C3
 5  ★ RAII ③  UniqueIcon 守卫接管 sfi.hIcon，析构 DestroyIcon
 6  return ImageFromHIcon(sfi.hIcon);
```

★ **`SHGFI_USEFILEATTRIBUTES` 一律使用**（初设 §2.3）：P4 证明**不加它则不存在路径拿不到图标**；P3 证明**对已存在路径无保真损失**。
★ **不做 COM 初始化**（初设 §2.4 / P1–P7）：★ 因此**不存在与既有 apartment 冲突的可能**。
★ **不做两层递进**（不先试「不加 flag」）：P3/P4 已给出取舍依据，多一个分支换不到可观测收益（YAGNI）。

### 2.3 ★ 资源与 RAII 形态（**A3 的两栏冻结** + 初设 §9-①）

| 资源 | 取得处 | 释放 | 成功路径 | 中途失败路径 | ★ RAII 守卫 |
|---|---|---|---|---|---|
| `HICON`（`SHFILEINFOW::hIcon`） | `SHGetFileInfoW` | **`DestroyIcon`** | ✓ | ✓ | **`UniqueIcon`**（外壳，**取得即接管**） |
| `HBITMAP`（`ICONINFO::hbmColor`） | `GetIconInfo` | **`DeleteObject`** | ✓ | ✓ | **`IconBitmaps`**（内核，同时管两个 bitmap） |
| `HBITMAP`（`ICONINFO::hbmMask`） | `GetIconInfo` | **`DeleteObject`** | ✓ | ✓ | 同上 |
| `HDC`（屏幕 DC） | `GetDC(nullptr)` | **`ReleaseDC(nullptr, dc)`** | ✓ | ✓ | **`ScreenDc`** |
| DIB 缓冲（color） | `Image::pixels`（`std::vector`） | RAII | ✓ | ✓ | 容器自带 |
| DIB 缓冲（mask） | 局部 `std::vector` | RAII | ✓ | ✓ | 容器自带 |

★★ **三条 ownership 事实（写死，评审 §17/§18）**：

1. **`GetIconInfo` 返回的两个 bitmap 与 `HICON` 的 ownership 无关**——`DestroyIcon(icon)` **不释放**它们 ⇒ **必须各自 `DeleteObject`**。
2. **屏幕 DC 的 `ReleaseDC` 首参是 `nullptr`**（与 `GetDC(nullptr)` 配对）——★ `WicImageDecoder` 的 3 处先例即此形态。
3. ★★ **RAII 化不是「写一段 `// cleanup`」**——**每个资源「取得之后立即进入作用域 owner」** ⇒ `GetDIBits` 中途失败 / 缓冲分配失败 / `Image` 构造失败**都天然走释放路径**（评审 §18；沿 Phase 11 的 `ComPtr` / `ComScope` 工程纪律）。

### 2.4 尺寸校验口径（**不照抄 WIC**，本稿新增）

```text
ValidIconSize(w, h):
    return w >= 1 && h >= 1
        && w <= kMaxIconDim && h <= kMaxIconDim            // kMaxIconDim = 4096
        && static_cast<std::uint64_t>(w) * 4 * h <= kMaxBytes;   // kMaxBytes = 256MB（与 WIC 同源上限）
```

★ **为什么不照抄 `WicImageDecoder` 的四域检查（`int` → `stride int` → `UINT` → `size_t` → 256MB）**：
`WicImageDecoder` 面向**外部编码流**——尺寸完全不可信，必须在**四个整数域**上逐步收紧；
而图标的尺寸来自 **`GetObject` 的 `BITMAP`**（`LONG` 域，**B** 实测），**现实上界极小（≤ 256）** ⇒
**一个维度上限 + 一个 64 位中间量**已经覆盖溢出与资源滥用两条风险。★ **保留 256MB 同源上限以维持一致的资源纪律**（不新造第三种上限口径）。

### 2.5 ★ 实现盯防清单（本稿逐条给判据）

| # | 易犯 | 判据 |
|---|---|---|
| **①** | 用 `DeleteObject` 释放 `HICON`（或反之） | `DestroyIcon` / `DeleteObject` **各只出现在其对应守卫内**（各 1 处） |
| **②** | 忘记 `DeleteObject` 两个 bitmap（**最易漏**） | `IconBitmaps` 的析构**同时**管两个（`hbmColor` / `hbmMask` **各 1 次** `DeleteObject`） |
| **③** | `ReleaseDC` 首参写成窗口 hwnd | 唯一调用形如 `ReleaseDC(nullptr, dc)`（字符级判据） |
| **④** | ★ **按位深判「用不用 mask」**（会把老式图标变全透明） | 判据函数名为 `AnyAlphaNonZero`，**且不出现 `bmBitsPixel` 参与分支** |
| **⑤** | mask 位序写反（`7 - (x & 7)` vs `x & 7`） | ★ **v1.2 起自动化不再能钉它**——T21-2 已改为**位序无关**的语义断言（§6.1 + **O9**：系统 mask 副本不保持调用方写入的位映射）⇒ 判据退为**代码审阅**：与 §2.1 第 9 步的 `7 - (x & 7)` 逐字对齐 |
| **⑥** | 请求 `GetDIBits` 时 `biHeight` 用正值（bottom-up） | 两处承载（`BitmapInfo256`）的 `biHeight` **均为 `-h`** |
| **⑦** | premultiply 用了 `+128` 或截断 | 唯一形式 `(c * a + 127) / 255`（字符级判据） |
| **⑧** | 在公共头里写出 `HICON` | **C1 的 grep 判据**（§5） |
| **⑨** | 忘 `src/Tests` 的登记 | 登记数与 §6.4 一致；★ **不登记 = 用例静默不跑** |
| **⑩** | 新 `.cpp` 里用了 `std::numeric_limits` 而没处理 `min`/`max` 宏 | ★ **本设计不需要** `<limits>`（§2.4 用 64 位中间量）⇒ 若确要加，须按条 10 加 `#ifdef max` / `min` 的 undef |
| **⑪** ★ 新增 | ★ **把 monochrome `HICON`（`hbmColor == nullptr`）与「老式图标」混为一谈**（⇒ 两支并作一个 `return {}`，**边界不可观测**） | ★ 该分支**有专属日志**（`monochrome HICON unsupported`）——**字符级判据**：**实现代码里**该串**只出现 1 处**，且**就在该分支内**（★ 本稿正文因引用而另有 1 处，**不计**）（**C13**） |
| **⑫** ★ 新增（v1.2） | ★★ **`BITMAPINFO` 只带 1 个调色板槽**（`bmiColors[1]` = 4B），而 **`GetDIBits` 对调色板格式会写入完整调色板**（1bpp = 2 项 = 8B） | ★ 两处 `GetDIBits` 的承载均为 **`BitmapInfo256`** 且实参写作 **`&bi.info`**（字符级判据）——★ **实测铁证**：32bpp 请求越界 **0** 字节、**1bpp 请求越界 4 字节**；★★ 这是 **MSVC 中止的真根因**（`/RTCs` 的 `Run-Time Check Failure #2`——**只有 MSVC 有 `/RTC1`**，其余三链静默）。★ 注：`CreateDIBSection` **只读**该结构 ⇒ **建 DIB 不必补空间**，但**测试装置建 1bpp DIB 时同样要补**（它要读调色板——§6.1） |

---

## 3. 逐文件行级改动（△1–△5）

### 3.1 △1 `include/ECDI/Decode/ImageDecoder.h`（**既有头追加**）

```cpp
// ① 头注释：把「文件 / 内存」扩为三类来源，并点明与 .ico 解码的分工
/// @brief 图片与图标获取（Phase 11 文件/内存——WIC；Phase 21 系统图标——shell）
/// @details 无状态自由函数 API：失败一律返回空 Image（width==0，DrawImage 天然跳过）
///          并经 Logger 记 Error；不抛异常。
///          ★ 三个来源的语义分工：
///            - DecodeMemory / DecodeFile ⇒ 「**编码数据 / 文件内容** → Image」（WIC）
///            - DecodeSystemIcon          ⇒ 「**该路径在系统中的图标** → Image」（shell）
///          ★ 注意 DecodeFile("x.ico") 与 DecodeSystemIcon("x.ico") **不是同一件事**。

// ② 新增声明（置于 DecodeFile 之后）
/// @brief 取「该路径在系统中的图标」（Phase 21）
/// @param utf8Path 路径（UTF-8——框架公共 API 编码契约，内部 UTF8ToWide）
/// @return 成功返回填充的 Image；失败返回空 Image
/// @details 语义是「**该路径的图标**」，**不是**「该路径指向的图像内容」。
///          对**不存在**的路径亦按类型给图标（一律使用 SHGFI_USEFILEATTRIBUTES）。
///          ★ **不承诺图标尺寸**——Image.width / height 为权威（当前实测 32×32）。
[[nodiscard]] Image DecodeSystemIcon(const std::string& utf8Path);
```

★ **该头零 include 新增**（`<string>` 已在，B 级事实：`ImageDecoder.h:7`）。

### 3.2 △2 **新建** `src/Platform/Win32/ShellImageDecoder.h`（内部头）

```cpp
#pragma once

#include "ECDI/Core/Image.h"

#include <windows.h>      // HICON（★ 仅内部头——平台层边界内，公共头不得出现）

namespace ECDI::Decode
{

/// @brief HICON → Image（内核；★ 不触 shell ⇒ 可无头测试）
/// @details 直接把内核做进平台层，与 shell 调用分离（Phase 21 初设 §2.5 / D3）。
///          - color 恒按 32bpp / top-down 读（实测 GetIconInfo 会归一化为 32bpp）
///          - ★ 不透明度：alpha 含非零值 ⇒ 用 alpha；**alpha 全为 0 ⇒ 用 mask**
///          - premultiply 在内部完成（Image 契约：premultiplied BGRA）
///          - 失败 ⇒ 空 Image + Logger Error；不抛异常
[[nodiscard]] Image ImageFromHIcon(HICON icon);

}
```

★ **归属定案（O4）**：放 `src/Platform/Win32/` 的**内部头**——沿 **B9** 先例（`DpiConversion.h` / `CoverageRaster.h`）；★ 测试可直接 include（**B7**：9 个测试文件已如此）。

### 3.3 △3 **新建** `src/Platform/Win32/ShellImageDecoder.cpp`

```cpp
#include "Platform/Win32/ShellImageDecoder.h"

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Decode/ImageDecoder.h"

#include <windows.h>
#include <shellapi.h>          // SHGetFileInfoW / SHFILEINFOW（★ shell32 已链，B11）

#include <cstdint>
#include <string>
#include <vector>

namespace ECDI::Decode
{

namespace {

constexpr int kMaxIconDim = 4096;
constexpr std::uint64_t kMaxBytes = 256ull * 1024ull * 1024ull;   // 与 WIC 同源上限（§2.4）

void LogIconError(const wchar_t* step, unsigned long code = 0);   // ★ 自带一份（O1 定案：不复用 B2 的匿名件）；★ code 默认 0（v1.1，评审 S-①）
bool ValidIconSize(int w, int h);
bool AnyAlphaNonZero(const std::uint8_t* bgra, std::size_t pixelCount);
void PremultiplyInPlace(std::uint8_t* bgra, std::size_t pixelCount);
void FillBiTopDown(BITMAPINFO& bi, int w, int h, unsigned short bpp);   // ★ 恒 biHeight = -h

// ★★ GetDIBits 的承载：BITMAPINFO 只带 1 个调色板槽 ⇒ 1bpp 请求会越界写 4 字节（盯防 ⑫）
struct BitmapInfo256 { BITMAPINFO info{}; RGBQUAD palette[255]{}; };

// ── 三个 RAII 守卫（§2.3）──
struct IconBitmaps { HBITMAP color = nullptr; HBITMAP mask = nullptr; ~IconBitmaps(); };
struct ScreenDc    { HDC dc = nullptr; ~ScreenDc(); };
struct UniqueIcon  { HICON icon = nullptr; ~UniqueIcon(); };

} // anonymous namespace

Image ImageFromHIcon(HICON icon) { /* §2.1 的 11 步 */ }

Image DecodeSystemIcon(const std::string& utf8Path) { /* §2.2 的 6 步 */ }

} // namespace ECDI::Decode
```

★ **O1 定案 = 不复用 `WicImageDecoder.cpp` 的日志辅助**：① 两者错误上下文不同（一个带 `HRESULT`，一个带 `GetObject` / `GetDIBits` 的返回值与 `GetLastError`）；② 抽取要**动既有文件**（扩大影响面与回归面）；③ 「第二个消费者」虽已出现，但**收益 < 成本** ⇒ 记入 **O1** 备查。
★ **日志口径（v1.1 补，评审 S-①）**：`code == 0` ⇒ 消息 `ImageDecoder: <step> failed`；`code != 0` ⇒ `ImageDecoder: <step> failed (code=%lu)`。★★ **只在 API 明确提供错误码处传参**——`SHGetFileInfoW` 失败 ⇒ `GetLastError()`（§2.2-4 已就地写出）；★ **`GetIconInfo` / `GetDIBits` 等 GDI 函数不保证设置 last error**（其**返回值本身即诊断量**）⇒ **一律用默认 `0`，不伪造 `0x00000000`**。
★ **本设计不需要 `<limits>`**（§2.4 用 64 位中间量）⇒ **不触发 `min`/`max` 宏问题**（盯防 ⑩）。

### 3.4 △4 + △5 测试文件与登记

| △ | 文件 | 改动 |
|---|---|---|
| **△4** | **新建** `src/Tests/IconDecodeTests.cpp` | 承载 **T21-1..T21-9**（§6）。★ **include 顺序照 `DropFilesTests.cpp` / `DpiTests.cpp` 的模板**：`RunAllTests.h` → `TestFramework.h` → `<Windows.h>` → ★ **`#ifdef DrawText` / `#undef DrawText`**（条 10）→ 内部头 + ECDI 头 → 标准库 |
| **△5** | `RunAllTests.h`（**+1 声明**）· `RunAllTests.cpp`（**+1 调用**） | 沿 `RegisterDpiTests` 的先例（**B13**），声明带 `///< Phase 21：…` 说明 |

---

## 4. 契约（C1–C14）

> **C1–C8 承接初设**（原样或加严；★ 标注加严处）；**C9–C12 本稿新增（v1.0）**；**C13–C14 本稿新增（v1.1 —— 评审处置）**。

| # | 契约 | 判据 / 依据 |
|---|---|---|
| **C1** | **公共 API 零 Win32 类型**——`HICON` / `HBITMAP` / `SHFILEINFOW` 不出现在 `include/` 下任何头 | 核心不变量 · **R2**；grep 判据见 §5 |
| **C2** | 输出 = **既有 `Image` 契约**（32bpp premultiplied BGRA / top-down / `stride == width*4`） | **R3** |
| **C3** | 失败 ⇒ **空 `Image`（`width==0`）** + `Logger::Log(Error, …)` + **不抛异常** | **R4** · K5 |
| **C4** | **资源无条件释放**（入口失败 / 中途失败 / 成功三路）· ★ **加严**：**经 RAII 守卫**（§2.3），不写散落的 `// cleanup` | **R5** · 评审 §18 |
| **C5** | ★ **不假设图标尺寸**——`Image.width/height` 为权威；框架不承诺 32×32 | 初设 §2.6 |
| **C6** | ★ **入口自校验**：`utf8Path` 为空 / 全空白 ⇒ 立即失败（**不得依赖 API 报错**——**P7**） | **P7** |
| **C7** | ★ **环境无关**：**不依赖 COM 初始化**、**不依赖 explorer**（`USEFILEATTRIBUTES` ⇒ 不查外壳命名空间） | **P1–P7** |
| **C8** | ★ **像素口径**：α 路径用 alpha、mask 路径用 mask · premultiply 在框架内做 | **P5/P6** |
| **C9** ★ 新增 | ★★ **不透明度来源判据**：**alpha 含非零值 ⇒ 用 alpha；alpha 全为 0 ⇒ 用 mask**——★★ **不得按位深判**（按位深会让老式图标全透明） | **P10 / P11** · 初设 §2.7 |
| **C10** ★ 新增 | ★ **premultiply 口径**：`out = (c * a + 127) / 255`（**与 `CoverageRaster` 同族**）；★ mask 路径下**恒等无误差**；与 WIC 路径的 ±1 差记为**已知近似** | **B14** · 初设 **O2** |
| **C11** ★ 新增 | ★ **`GetDIBits` 请求口径**：color **恒 32bpp**、mask **1bpp**、**两者均 top-down（`biHeight = -h`）**；★ **不处理调色板 / 低位深**（P10/P11/P12） | **P10–P12** |
| **C12** ★ 新增 | ★ **尺寸校验**：维度 ∈ `[1, 4096]` 且总字节 ≤ 256MB（**64 位中间量**）——★ **不照抄 WIC 的四域检查**（理由见 §2.4） | §2.4 |
| **C13** ★ 新增（v1.1） | ★ **支持边界**：仅支持 **`ICONINFO::hbmColor != nullptr`** 的图标；★ **真 monochrome `HICON`（`hbmColor == nullptr`）不在本阶段范围** ⇒ 行为 = **空 `Image` + 专属 Error 日志**（**不崩 · 不泄漏 · 可观测**）——★ 定义见 **§1.2-2**，**重启条件见 O7** | **评审 R-①** · §1.2 |
| **C14** ★ 新增（v1.1） | ★ **异常边界（C3 的精确化，零行为变化）**：「不抛异常」= **本层不主动 `throw`**；平台失败 / 校验失败 / 语义边界一律 `return {}` + `Logger::Log(Error, …)`；★★ **标准容器的分配失败（`std::bad_alloc`）不在本层捕获**——**这是 Phase 11 的既成口径**（`WicImageDecoder.cpp:152` 的 `pixels.resize()` 同样不捕获；**全框架生产代码零 `try` / `catch`**，唯一的 `try` 在 `TestFramework.cpp:75` 且属**测试运行器**） | **评审 R-②** · `WicImageDecoder.cpp:152` · `TestFramework.cpp:75` |

---

## 5. 影响面

| 项 | 值 / 判据 |
|---|---|
| **公共头** | **92 → 92**（★ **头文件 0 新增**；`ImageDecoder.h` **既有头追加**） |
| **公共 API** | ★★ **+1**（`Decode::DecodeSystemIcon`）——★ 口径必须精确（需求 **A4** / 评审 §18）：**「头文件数不变」≠「API 无变化」** |
| **新增文件** | **3**（`ShellImageDecoder.h` · `ShellImageDecoder.cpp` · `IconDecodeTests.cpp`） |
| **改动文件** | **3**（`ImageDecoder.h` · `RunAllTests.h` · `RunAllTests.cpp`） |
| **测试** | **252 → 261**（**+9**，§6）· ★ **既有 252 一条不改** |
| **CMake** | ★ **零改动**（新 `.cpp` 由 `GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库；**`shell32` 已链**——**B11**） |
| **渲染侧 / `Image` / `PaintContext` / `main.cpp`** | **零改动** |
| **不新增纯虚** | ⇒ **无实现者清单要盘**（与 Phase 20 的 `GetDpiScale` 不同） |
| **C1 的 grep 判据** | `HICON` / `HBITMAP` / `SHFILEINFO` 在 `include/ECDI/**` 下**代码行 0 处**（★ 排除注释行，条 44） |
| **非 Windows 平台的连带** | `DecodeSystemIcon` 是公共 API ⇒ 未来非 Windows 实现**须提供等价函数**（空 `Image` + `Log`）。★ **属未来实现策略，本阶段不展开**（不设计任何非 Windows 图标来源） |

---

## 6. 测试实现规格（T21-1..T21-9）

> ★ 全部**无头可跑**（初设 §2.5 实测）。**内核组**用 `CreateIconIndirect` 自造图标（期望值由测试自己构造 ⇒ **零环境依赖**）；**外壳组**用**必然存在的文件 / 目录**与**必然不存在的路径**。

### 6.1 内核组（T21-1..T21-3）

**共同装置**（★ v1.2 按实现回写）：**一个参数化 maker** `MakeDibSection(w, h, bpp)` ＋ **三个写入器**（`PutPixel32` / `PutPixel24` / `PutMaskBit`）＋ **翻转的唯一落点** `RowOf` ＋ **`IconFromBitmaps(color, mask)`**（造 `HICON` → 调内核 → `DestroyIcon`，把 HICON 的 ownership 收在一处）＋ **`ExpectBytes(img, expected, n)`**（★ **先校验 `size()` 再逐字节**——越界 `operator[]` 在 `_ITERATOR_DEBUG_LEVEL=2` 下是**断言 → 直接 abort**，会把「报告失败」升级成「进程消失且 stdout 未 flush」）。

| # | 输入 | 期望（逐字节） |
|---|---|---|
| **T21-1** ★ | **2×2 · 32bpp + alpha**：全像素 `B=G=R=0x40`；alpha **视觉顶行** = `0x00, 0x40`、**底行** = `0x80, 0xFF`；mask 全清 | `width=2 height=2 stride=8 pixels.size()=16`；缓冲（行优先、top-down）＝<br>`00 00 00 00` · `10 10 10 40` · `20 20 20 80` · `40 40 40 FF`<br>★ 说明：`(0x40×A + 127) / 255` ⇒ A=0→**0x00** · A=0x40→**0x10** · A=0x80→**0x20** · A=0xFF→**0x40**；★ **同时钉住「多行方向」**（P9） |
| **T21-2** ★ | **4×1 · 24bpp color + 1bpp mask**：全像素 `B=0x10 G=0x20 R=0x30`；mask = `0xC0`（**px0 / px1 置位**） | **几何**：`width=4 height=1 stride=16 pixels.size()=16`（不符即 `return`）。★★ **像素断言（v1.2 改为语义断言）**：每个像素**必属且仅属**两种形态之一——**全透明** `00 00 00 00`（mask 置位 ⇒ A=0 ⇒ 预乘全 0）或 **不透明** `10 20 30 FF`（mask 清零 ⇒ A=255 ⇒ 原色不动）；且 **两形态都必须出现**（`nClear > 0` **且** `nSolid > 0`）。★ 一对断言同时挡住「**全不透明**（mask 没被读 / 误走 alpha 路径）」与「**全透明**（误按位深判 / 误信恒 0 的 alpha——**C9 的存在理由**）」两个极端；★ 形态归属**顺带钉住通道序**（写成 `30 20 10 FF` 会落在两形态之外）。★★ **不再逐字节钉 mask 位映射**——理由见下表后的说明 |
| **T21-3** | **退化输入**：① `ImageFromHIcon(nullptr)` ② 1×1 正常图标（边界尺寸） | ① `width==0 && height==0 && pixels.empty()`（**不崩**）② 契约成立（`stride == 4` · `pixels.size() == 4`） |

★★ **装置的语义（与原稿一致，只是形态更紧凑）**：`CreateDIBSection` · **正 `biHeight`（bottom-up，图标惯例）** · 逐行倒序写入 ⇒ **内存第 0 行 = 视觉底行**。★ 装置同样使用 **`BitmapInfo256`**——**这是盯防 ⑫ 的同一条坑在装置侧的落点**：**`CreateDIBSection` 对 1bpp 要「读」2 项调色板**，单槽会**越界读**（拿到栈上垃圾）⇒ **单色位图的调色板未定义 ⇒ 位极性不确定** ⇒ 同一份代码在同一台机器上「直接跑 `C0` / gdb 下 `30`」来回翻转。★ **修成 256 项承载后，位极性才确定、装置才可复现**。
★★ **T21-2 为什么不逐字节钉 mask 的位映射（v1.2 实测定案）**：★ 装置**确定化之后**，同一二进制在**两种环境**（直接跑 / gdb 下）**逐位同值**——而系统对「自造图标」的 mask 副本**稳定读回 `0x30`**（= **px2 / px3 置位**），**调用方写入的是 `0xC0`**（px0 / px1）⇒ ★★ **`GetIconInfo` 不保证保持调用方写入的位映射**，且该行为**与进程环境无关**（它就是**系统侧的副本语义**）。⇒ 原逐字节期望**钉在了一个我们既不控制、也不该依赖的量上**（**过度指定**）；★ 内核按系统给的 mask **忠实**渲染，「**置位 = 全透明**」这条判据**本身没写错**。★★ **对 Phase 21 的目标无实际影响**——真实 shell 图标**携带 alpha** ⇒ 走 **alpha 路径**，**根本走不到 mask 路径**（P10 / P11）。★ 该观察登记为 **O9**；★ **premultiply 的中间值精度**由 **T21-1** 钉（本用例的 A 只有 0 / 255 ⇒ 对 `(c * a + 127) / 255` 与截断版**等价**，**钉不出差别**——★ 不把「没钉住」写成「钉住了」）。

### 6.2 外壳组（T21-4..T21-8）

**共同断言**（非空组）：`width > 0 && height > 0 && stride == width*4 && pixels.size() == stride*height`。
**共同路径资产**：`GetModuleFileNameW`（**测试 exe 自身**——必然存在、**零创建零清理**）· `GetTempPathW`（**必然存在的目录**）；★ 宽路径经 **`WideToUTF8`**（**B12**）转成 API 要的 UTF-8。

| # | 输入 | 期望 |
|---|---|---|
| **T21-4** ★ | ★ **已存在普通文件** = **测试 exe 自身路径**（`GetModuleFileNameW`） | **非空** + 共同断言（★ **A6-①**；评审 §16 的原缺口） |
| **T21-5** | ★ **已存在目录** = `GetTempPathW` 结果 | **非空** + 共同断言（★ **A6-②**，P2 佐证目录 `iIcon` 独立） |
| **T21-6** | **不存在 + 有扩展名**（`Z:/nonexistent_dir/nope.txt`） | **非空**（**Q4 / P4**：`USEFILEATTRIBUTES` 的价值所在） |
| **T21-7** | **不存在 + 无扩展名**（`Z:/nonexistent_dir/nope`） | **非空**（**A6-④**） |
| **T21-8** ★ | ★ **空串 `""` · 全空白串 `"   "`** | **空 `Image`** + **不崩 / 不抛**（**C6 / R4**；**P7** 证明不能靠 API 报错） |

### 6.3 端到端（T21-9）

| # | 输入 | 期望 |
|---|---|---|
| **T21-9** | `DecodeSystemIcon(<exe 路径>)` → `PaintContext::DrawImage(Rect{0,0,16,16}, img)` → `RecordingBackend` | `commands.size() == 1` · `std::holds_alternative<DrawImageCommand>` · `cmd.image.width != 0`（★ 沿 `ImageDecodeTests` T7 先例） |

### 6.4 用例数与登记（**8 → 9 的最终口径**）

- **新增场景**：**9**（T21-1..T21-9）⇒ **用例 252 → 261**；
- **落点**：T21-1..T21-3 归 **新建 `IconDecodeTests.cpp`** ⇒ **`RunAllTests.h` / `.cpp` 各 +1**（**B13**）；T21-4..T21-9 同文件内。
- ★ **「一个场景一条注册」**（9 条 `GetTestRegistry().Add`）——沿既有全部测试文件的惯例。
- ★ **`nullptr` 不设用例**（评审 §15）：公共 API 收 `const std::string&` ⇒ **语法上不可表达**；且实现里指针来自 **`c_str()`（永不为 null）** ⇒ **该分支不可达**。
- ★ **「平台调用失败」维度（评审 §17）**：归 **C3**，但★ **在本设计下难以构造**（P1–P7：`USEFILEATTRIBUTES` 对任意**非空**路径基本都成功）⇒ **详设实施时先尝试构造**（候选：超长路径 / 含非法字符）；★ **若确不可构造，则明确记为「防御性分支、无自动化用例」——不假称已覆盖**。★ **实施结果（v1.2）**：**未构造**（T21-8 只覆盖**空串 / 全空白串**）⇒ **按本条口径如实记录**（见 §8.1 的 A6 判定）。
- ★★ **monochrome 形态（`hbmColor == nullptr`）不在自动化覆盖内**（**C13**）——★ 本阶段**不为其补用例**（**O7**）。★★ **一句诚实的话**：现有 **T21-4..T21-7** 的「非空」断言**已在测试机上间接证明**「shell 对**普通文件 / 目录 / 不存在（有扩展名 / 无扩展名）**给出的图标**均走 color 携带路径**」（★ 若 shell 真返回 monochrome，这 4 条**会直接失败**）——★ **但这不是「所有路径」的证明**（shell 输入空间不可穷举）⇒ **不当作 C13 的证据**。

---

## 7. 实现顺序与检查点（两批 + 收尾）

| 批 | 文件 | 检查点 |
|---|---|---|
| **批一** | **新建** `ShellImageDecoder.h` · `ShellImageDecoder.cpp` ＋ **△1** `ImageDecoder.h` | ★ **构建通过 + 既有 252 全绿**——此时**新函数无人调用**，且 `ImageFromHIcon` 尚无用例 ⇒ **零破坏可观测** |
| **批二** | **新建** `IconDecodeTests.cpp`（T21-1..T21-9）＋ **△5** 登记（`RunAllTests.h` / `.cpp` **各 +1**） | ★ **261 全绿**（252 + 9）· 四工具链 |
| **收尾** | 文档回填（详设 §实施记录）+ 索引 + 记忆 | 需求 **A1–A6** 的最终判定 |

★ **批序理由 = 先让能力可构建、再让能力可验证**：批一单独check 点能证明「**新增生产文件不影响既有行为**」（这是零回归的**结构性证据**，而非"看一遍 diff"）。
★ ★ **两批都无需 CMake 改动**（**B11** + `GLOB_RECURSE`）。
★★ **△5 必须随批二、不能随批一（v1.2 订正）**：`△5` 登记的是 **`RegisterIconDecodeTests()`**，而**该函数的定义在 △4 的 `IconDecodeTests.cpp`（批二）** ⇒ 若按 v1.0 / v1.1 的原表把 △5 放进批一，**批一必然 `unresolved external`（链接失败）**。★ **可推广的判据**：**「登记」与「被登记的测试文件」之间存在链接级依赖 ⇒ 必须同批**——这不是可自由排列的组合。

### 7.1 ★★ 实施记录与逐条偏离（2026-09-27，批一 + 批二 + 三处缺陷修复）

**交付物**：**新建 3**（`src/Platform/Win32/ShellImageDecoder.h` · `ShellImageDecoder.cpp` · `src/Tests/IconDecodeTests.cpp`）· **改动 3**（`include/ECDI/Decode/ImageDecoder.h` · `src/Tests/RunAllTests.h` · `.cpp`）· ★ **`CMakeLists.txt` / `main.cpp` / 渲染侧 / `Image` 零改动**（B11 + `GLOB_RECURSE`）。

**验证**（本机 1920×1080 @ DPI 120 = **125%**）：

| 工具链 | 直跑 | gdb 下 |
|---|---|---|
| clang | **261 passed / 0 failed** ✓ | IconDecode 组 **9/9 PASS**，无 trap ✓ |
| mingw | **261 / 0** ✓ | **`Tests: 261 Passed: 261 Failed: 0`** ✓ |
| clang-cl | **261 / 0** ✓ | — |
| **visual-studio** | 用户侧跑通（**261 / 0**） | — |

★ **零 warning**（含两处强制重编）· ★ **行为零变化的硬证据**：`AntiAliasing` 覆盖度积分四行数值（`0.811765 / 50.352941 / 804.262745 / 50.254902`）**与改动前逐位相同** · ★ **T21-1 的逐字节期望与实现零偏差**（P9–P13 冻结的判据可靠）。

**★★ 实施中发现并修复的三处缺陷**（均**不在原设计**内）：

| # | 落点 | 性质 | 机制 / 触发条件 |
|---|---|---|---|
| **①** | `IconDecodeTests.cpp`：`ExpectBytes` helper + 3 处 `size()` 守卫 | **防御性** | `_ITERATOR_DEBUG_LEVEL=2`（MSVC Debug 默认；clang 用 MSVC STL 时同样）下**越界 `operator[]` 是断言 → 直接 abort** ⇒ 把可诊断的失败变成不可诊断的崩溃 |
| **②** | `ShellImageDecoder.cpp`：承载改 `BitmapInfo256` | ★★ **MSVC 中止的真根因** | **`GetDIBits` 对 1bpp 目标格式会写入完整调色板**（2 项 = 8B），而 `BITMAPINFO` 只带 1 槽（4B）⇒ **越界写 4 字节** ⇒ `/RTCs` 报 **`Run-Time Check Failure #2 - Stack around the variable ... was corrupted`**。★★ **只有 MSVC 有 `/RTC1`**（= `/RTCs` + `/RTCu`；gcc / clang / clang-cl **都不实现 `/RTC`**）⇒ **一链中止、三链静默**——这正是「四个构建链只有 MSVC 这样」的结构性原因 |
| **③** | `IconDecodeTests.cpp`：装置同用 `BitmapInfo256` | ★★ **「环境敏感」的真根因** | **`CreateDIBSection` 对 1bpp 要「读」2 项调色板** ⇒ 单槽**越界读** ⇒ **调色板未定义 ⇒ 位极性不确定** ⇒ 同一二进制「直接跑 `C0` / gdb 下 `30`」两种结果。★ **修后三链 × 两环境结果完全一致** |

★ **三者的定性边界**：**②是框架代码的真缺陷**（已修）；**③是测试装置的缺陷**（已修）；**①是防御性加固**（与本条 abort 无关，独立成立）。★ **三处都是「先实测再下结论」拦下来的**——其中**两次是栈越界**（一次写、一次读），且**都在同一族（调色板承载）上**。

**★ 与草案的偏离（逐条，均为「消警告 / 形态收敛」，语义不变）**：

| # | 草案 | 实现 | 理由 |
|---|---|---|---|
| **D-1** | `GetObject(...) != sizeof(bc)` | `!= static_cast<int>(sizeof(bc))` | 消 signed/unsigned 比较警告——沿 `WicImageDecoder.cpp` 的 `static_cast<unsigned int>(size)` **先例** |
| **D-2** | `SHGetFileInfoW(..., sizeof(sfi), ...)` | `..., static_cast<UINT>(sizeof(sfi)), ...` | 消 `size_t → UINT` 窄化警告（同上先例） |
| **D-3** | `GetDIBits(..., h, ...)` | `..., static_cast<UINT>(h), ...` | `h` 是 `int` 而形参是 `UINT`；**同族、同先例** |
| **D-4** | `BITMAPINFO bi32 / bi1` | `BitmapInfo256 bi32 / bi1`（`&bi.info`） | ★★ **②（真根因）** |
| **D-5** | 4 个 maker（`MakeColor32` / `MakeColor24` / `Make1bpp` / `MakeMask1`） | **1 个参数化 `MakeDibSection(w,h,bpp)` + 3 个写入器** | 四个 near-duplicate maker 不如「一个 maker + 三个写入器」；★ 原稿要求的三条**语义全部保留**，且翻转落点唯一（`RowOf`） |
| **D-6** | （未指定装置形态） | `IconFromBitmaps(color, mask)` 封装 | 把「造 `HICON` → 内核 → `DestroyIcon`」收一处，**杜绝用例各自漏放** |
| **D-7** | （未指定） | `#include <cstring>`（`memcmp`）+ `std::size_t` 显式转换 | 语义断言需要 `memcmp`；转换同 D-1/D-2 口径 |
| **D-8** | 日志调用形如 `LogIconError(L"SHGetFileInfoW", GetLastError())` | `..., static_cast<unsigned long>(GetLastError())` | `DWORD` → `unsigned long` 的**同族显式转换** |

★ **D-1..D-3 / D-8 已在本稿 §2.1 / §2.2 就地回写**（草案文本即实施文本，**下次照抄不会再踩警告**）。★ **D-5/D-6/D-7 属装置形态**，已在 §6.1 回写。

**★ 未做（如实记录）**：**A6-⑤b（平台调用失败）无自动化用例**（§6.4 的诚实口径）· **C13 的拒绝路径无用例**（**O7**）· **`SHGFI_USEFILEATTRIBUTES` 的「先不加 flag 试一次」未实施**（**有意不扩大范围**——初设 §2.9-② 已登记为记账项 **#47**）。

---

## 8. 验收（需求 A1–A6 的落地口径）

| 需求 | 本稿落地 |
|---|---|
| **A1** 真实路径 ⇒ 非空 `Image` 且能画 | **T21-4 / T21-5 / T21-6 / T21-7** + **T21-9**（端到端）——★ **不做 demo 目视**（见下「A1 的口径说明」） |
| **A2** 像素契约与 `DecodeFile` 同口径 | **T21-1..T21-5** 逐条断言 `stride == width*4` / 尺寸 / 缓冲大小 |
| **A3** 失败路径资源全销毁 + 空 `Image` + 记 Error | ★ **§2.3 两栏 + RAII 形态已冻结** + **T21-3**（`ImageFromHIcon(nullptr)`）+ **T21-8** |
| **A4** 零回归 | **252 全绿**（既有一条不改）+ **公共头 92 → 92** + ★★ **公共 API +1** + 四工具链 |
| **A5** 无头可测 | ★★ **T21-1..T21-9 全部无头**（初设 §2.5 实测 + §6 的路径 / 图标资产均无环境依赖） |
| **A6** Shell 场景矩阵 | ① → **T21-4** · ② → **T21-5** · ③ → **T21-6** · ④ → **T21-7** · ⑤ → ★ **⑤a 空输入** = **T21-8**（C6）· **⑤b 平台失败** = **C3**（★ 见 §6.4 的诚实说明） |

★★ **A1 的口径说明（如实）**：`ModelProbe` **不消费图标**（需求 §8 已记录）⇒ 本阶段**没有现成的目视载体**。
⇒ **以「自动化断言 + 实测证据」替代目视**：**T21-4..T21-7** 已断言「真实路径出非空且契约成立」，**T21-9** 已断言「能进 `DrawImage` 命令」，**P1–P13** 已实测 shell 行为本身。
★ **若确需目视**：最小代价是在某个示例里**临时**加几行 `DrawImage`（★ **须单独授权**，且**用完即撤**，不污染 demo）。

### 8.1 ★★ A1–A6 最终判定（实施后，2026-09-27）

| 需求 | 判定 | 依据 |
|---|---|---|
| **A1** | ✅ **通过** | **T21-4 / T21-5 / T21-6 / T21-7** 实测非空且契约成立（`stride == width*4` / 尺寸 / 缓冲大小）；**T21-9** 实测能进 `DrawImageCommand`。★ **无目视**（口径见上「A1 的口径说明」——★ 若日后要目视，须单独授权且用完即撤） |
| **A2** | ✅ **通过** | **T21-1..T21-7** 的几何断言全绿（★ T21-1 逐字节；T21-2 语义断言——见 §6.1） |
| **A3** | ✅ **通过** | **T21-3**（`nullptr` / 1×1 边界）+ **T21-8**（空串 / 全空白）⇒ 空 `Image` + **不崩 · 不抛**；★ **RAII 三栏按 §2.3 落地**（`UniqueIcon` / `IconBitmaps` / `ScreenDc`） |
| **A4** | ✅ **通过** | ★★ **三链（clang / mingw / clang-cl）@125% 261 / 261**（**既有 252 一条不改**）+ **gdb 下逐位同值**；**MSVC 由用户侧跑通**（261 / 0）· **公共头 92 → 92** · **公共 API +1** |
| **A5** | ✅ **通过** | ★ **9 条用例全部无头**（含外壳组——P1–P7 的预判成立） |
| **A6** | ✅ **通过（⑤b 按诚实口径记录）** | ①→**T21-4** · ②→**T21-5** · ③→**T21-6** · ④→**T21-7** · ⑤a→**T21-8**；★ **⑤b（平台调用失败）无自动化用例** ⇒ 记为**防御性分支**（§6.4）——**不假称已覆盖** |

★ **一句话总结**：**A1–A6 六项全部达成**，其中 **A4 的「零回归」有结构性证据**（批一单独检查点 = 新增生产文件不影响既有 252 条；批二 = 新能力自身 261 条）。★★ **唯一如实标注的缺口 = A6-⑤b**（不可构造）。

---

## 9. 交回上游 / 待授权（O）

| # | 事项 | 处置 |
|---|---|---|
| **O1** | 新文件是否复用 `WicImageDecoder.cpp` 的日志辅助 | ★ **定案：不复用**（§3.3 三条理由）——★ 若后续出现**第三个** `Decode` 消费者，再评估提为共享件 |
| **O2** | premultiply 舍入口径 | ✅ **定案：`(c * a + 127) / 255`**（**B14** 的既有口径先例） |
| **O3** | 老式 mask 语义 | ✅ **已定案**（初设 v1.2 §2.7，由 P9–P13 补测） |
| **O4** | 内核命名 / 归属 | ✅ **定案**：`ImageFromHIcon` 放**内部头**（§3.2，沿 `DpiConversion.h` / `CoverageRaster.h` 先例） |
| **O5** ★ | **待授权（沿用初设 §2.9）**：① `desktopnest-roadmap.md` §3/§4 的 `SHGetFileInfo` 行**改判 + 补第三问** ② 「逐文件自定义图标」限制**立新记账项** | ★ **不在本阶段偷偷改**——**动手前须授权**。★★ ✅ **2026-09-26 已授权并执行**：`desktopnest-roadmap.md` **v1.10**（§3 / §4 的 `SHGetFileInfo` 行**改判「✅ 进框架」** + **判据补第三问** + **G-2 登记范围澄清**为**整条「路径 → `Image`」**）· `roadmap-deferred.md` **v1.39**（**新立记账 #47「逐文件自定义图标」**，含**升级路径**〔依据探针 **P3** ⇒「先不加 flag 试一次」**无保真损失**〕与**重启条件 R-1 / R-2**）⇒ **本项收口** |
| **O6** ★ | ★ **待实测**：**Per-Monitor V2 进程下 `SHGFI_LARGEICON` 的实际尺寸**（初设 P8：本机系统 DPI = 96 ⇒ 不可判定） | ★ **不阻塞设计**（**C5**：框架不假设尺寸）；★ 若日后发现随 DPI 变，也只是 `Image` 尺寸变 ⇒ **API 形态不受影响** |
| **O7** ★ 新增（v1.1） | ★ **C13 边界是否补「拒绝路径」用例**（用 `CreateIconIndirect` 造 `hbmColor == nullptr` 的 monochrome HICON，断言「返回空 `Image` + 不崩」） | ★ **倾向：本阶段不补**——① 评审明确建议**不为此扩大范围**；② ★ **`CreateIconIndirect` 是否接受 `hbmColor == nullptr` 本机未实测** ⇒ 补它**须先开一支探针 P14**，**不可凭推断写期望值**。★ 若采纳：用例 **261 → 262**。★★ **重启条件**：一旦出现**非 shell 的 `HICON` 来源**（`LoadIcon` / `LoadImage` / 应用侧自造并传入），**必须先补 P14 测量该形态**，再决定是否把 C13 从「不支持」改为「支持」 |
| **O8** ★ 新增（v1.1）· ✅ **已收口（初设 v1.3，2026-09-26）** | ★ **初设 v1.2 的两处「代码草案」仍写旧判据**（★ 本轮**为处置评审而读源码时发现**，**评审未提**）：§3.2 内部头草案的 `/// @details 32bpp ⇒ 用 alpha；<32bpp ⇒ 用 mask` · §3.3 步骤草案的 `// 3) color 32bpp ⇒ 用 alpha；否则 ⇒ …` ⇒ ★ **与它自己的 §2.7（v1.2 已修正）矛盾**，且正落在**「会被照抄」的位置**（★ 本稿 §3.2 的草案**已是正确口径**） | ★ **定案：本轮不动初设**——① 初设是**已通过**文档，改动须开 **v1.3**（连带 **4 处版本引用**：本稿来源行 · `docs/README.md` · 审计 · roadmap）；② 其**规范章节 §2.7 本身是对的**，且**实施权威已转移到本稿**（§3.2 草案正确）⇒ **不阻塞实施**。★ ✅ **2026-09-26 已授权并订正**：初设升 **v1.3** ⇒ **旧判据四处全部回扫修正**（§1.3 D4 行 · §3.2 草案 · §3.3 草案 · ★★ **§4 C8 契约**）；★★ **本项原发现范围偏窄**——只报了 §3.2 / §3.3，**漏了契约 C8**（**契约漏改最严重**）⇒ **已由 v1.3 的全面回扫补上**。★ 另修 **6 项**（悬空 `§3.4` · **setext 陷阱** · §2.5/§7 口径矛盾 · △3 引注 · §5 用例数 · §2.9 ③ 过期）· ★ **设计结论零变动** |
| **O9** ★ 新增（v1.2） | ★★ **`GetIconInfo` 返回的 mask 副本不保持调用方写入的位映射**（实测：调用方写 `0xC0` ⇒ 副本稳定读回 `0x30`；★ **与进程环境无关**，同一二进制直跑 / gdb 下逐位同值） | ★ **登记为「观察」，不是缺陷**——① 内核按系统给的 mask **忠实**渲染，「**置位 = 全透明**」这条判据**本身正确**；② ★★ **对 Phase 21 的目标无实际影响**（真实 shell 图标**携带 alpha** ⇒ 走 **alpha 路径**，**根本走不到 mask 路径**——P10 / P11）；③ 测试侧已改为**位序 / 极性无关**的语义断言（§6.1 T21-2）。★ **重启条件**：若日后出现**需要按调用方位映射可预期**的 mask 的场景（自定义图标渲染 / 图标编辑 / 逐文件图标），**须先补一支探针**测清该副本语义的适用范围，再决定是否绕行（自绘 mask） |

---

## 10. 修订记录
- **v1.2**（2026-09-27）**实施回写（批一 + 批二 + 三处缺陷修复）**。① ★★ **§7 的 △5 归批订正**：`△5`（`RunAllTests.h` / `.cpp` 各 +1 登记）原列**批一**，但 **`RegisterIconDecodeTests()` 的定义在批二** ⇒ 照原表实施批一会 **`unresolved external`** ⇒ 移入**批二**（§7 表 + 「登记与被登记文件必须同批」的判据）。② ★ **§2.1 / §2.2 的显式转换回写（3 处）**：`GetObject(...) != static_cast<int>(sizeof(bc))` · `SHGetFileInfoW(..., static_cast<UINT>(sizeof(sfi)), ...)` · `GetDIBits(..., static_cast<UINT>(h), ...)`——★ 沿 `WicImageDecoder.cpp` 的 `static_cast<unsigned int>(size)` 先例，**消窄化警告、语义不变**（逐条偏离见 §7.1 的 D-1..D-8）。③ ★★ **新增盯防 ⑫ + 承载 `BitmapInfo256`**：★★ **`GetDIBits` 对 1bpp 目标格式写入完整调色板（2 项 = 8B），而 `BITMAPINFO` 只带 1 槽（4B）⇒ 越界写 4 字节**——★★ **这是 MSVC 中止的真根因**（`/RTCs` 的 `Run-Time Check Failure #2`）；★ **只有 MSVC 有 `/RTC1`** ⇒ **一链中止、三链静默**。④ ★★ **§6.1 的 T21-2 由「逐字节钉 mask 位映射」改为「位序 / 极性无关的语义断言」**——实测证明 **`GetIconInfo` 的 mask 副本不保持调用方写入的位映射**（原期望属**过度指定**）⇒ 登记 **O9**。⑤ ★★ **装置侧的同一条坑**：**`CreateDIBSection` 对 1bpp 会「读」2 项调色板** ⇒ 单槽**越界读** ⇒ **调色板未定义 ⇒ 位极性不确定**（★ 这才是「同二进制两种环境两种结果」的真身）⇒ 装置同用 `BitmapInfo256`，**修后两种环境逐位同值**。⑥ ★ **§6.1 共同装置按实现回写**（一个参数化 `MakeDibSection` + 三个写入器 + `RowOf` + `IconFromBitmaps` + `ExpectBytes`）；★ **盯防 ⑤ 的判据相应订正**（位序**不再有自动化用例可钉** ⇒ 退为代码审阅——★ 如实标注）。⑦ ★ **§7 新增 §7.1 实施记录**（交付物 / 验证 / 三处缺陷 / 八条偏离 / 未做项）· **§8 新增 §8.1 A1–A6 最终判定**（★ **A1–A6 全部通过**，唯一缺口 = **A6-⑤b 不可构造**，按 §6.4 口径如实记为防御性分支）· **§9 新增 O9**（★ **O5 一并标 ✅ 已收口**——2026-09-26 已授权并执行）。⑧ **计数与规模**：盯防 **11 → 12 条** · 契约 **C1–C14 不变** · **O1–O8 → O1–O9** · **用例 261** · 内核 **11 步 / 外壳 6 步不变** · **公共头 92 → 92 而公共 API +1**。⑨ 头部 v1.1 → **v1.2**。


- **v1.1**（2026-09-26）**外部评审处置 —— ★ 2 项微调 + 1 项自查**（逐条表见 **§1.5**）。① **评审结论**：「**总体通过，可以进入 Implementation**」；要求实施前处置两项（🟡 × 2）。② ★★ **R-①（真 monochrome `HICON` 未被覆盖）**：★ 评审指出「**「老式图标兼容」表述过宽**」——实测覆盖的是「**有 color bitmap、但 color 不携带 alpha**」的低位深形态，**不等价于真 monochrome** ⇒ ✅ **采纳评审倾向的方案 A（不扩范围）+ 加严四步**：**定义收窄**（§1.2-2）· **该分支显式可观测**（§2.1 第 3 步拆两支 + **专属日志**）· **立 C13** · **O7 + 重启条件**。③ ★★ **R-②（「不抛异常」与 `resize()` 未闭合）**：★ 评审**未能定案**（「须结合 Phase 11 实现」）⇒ **本稿取证定案**——`WicImageDecoder.cpp:152` 的 `pixels.resize(vectorSize)` **同为裸调用、同样不捕获**，★ **全框架生产代码零 `try` / `catch`**（唯一 `try` 在 `TestFramework.cpp:75`，属**测试运行器**）⇒ **既有口径 = 「本层不主动抛」**，`std::bad_alloc` **不在 `Decode` 层捕获** ⇒ 立 **C14**（**精确化 C3，零行为变化**）。④ ★ **自查 S-①**：`LogIconError` **声明两参、调用单参** ⇒ 补默认实参 `code = 0` + 立**日志口径**（★ **不伪造 `0x00000000`**——GDI 的 `GetIconInfo` / `GetDIBits` **不保证设置 last error**，其返回值即诊断量）；★ 头部「构建级核实 **B11–B13**」订正为 **B11–B14**。⑤ **规模与结构零变动**：内核 11 步（**编号不变**）/ 外壳 6 步 · 资源与 RAII 三栏 · 算法 · 测试矩阵 **T21-1..T21-9** · 两批实施序 · **用例 252 → 261** · **公共头 92 → 92 而公共 API +1**。⑥ **清单类计数递增**：盯防清单 **10 → 11 条** · 契约 **C1–C12 → C1–C14** · **O1–O6 → O1–O8**（新增 **O7**「C13 边界是否补用例」· **O8**「初设 v1.2 代码草案的旧判据残留」）。⑦ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-09-26）初稿。**输入**：需求 **v1.1** · 初设 **v1.2**（含 §2.7 的实测修正）＋ ★★ **本稿新增实测 P9–P13**（`<32bpp + mask` 补测 / 多行方向 / `DrawIconEx` 替代方案）+ ★ **构建级核实 B11–B14**（**`shell32` 已链** · `WideToUTF8` 可用 · 登记锚点 · **`CoverageRaster` 的 `+127` 口径先例**）。**内容**：§2 **算法规格**（内核 **11 步** / 外壳 **6 步** · ★ **资源与 RAII 三栏** · 尺寸校验口径 · **盯防清单 10 条**）· §3 **逐文件改动 △1–△5**（含头全文草案）· §4 **契约 C1–C12** · §5 影响面 · §6 **T21-1..T21-9 逐条输入与逐字节期望** · §7 **两批 + 收尾** · §8 A1–A6 落地（★ A1 的口径说明）· §9 **O1–O6**（O2/O3/O4 已定案；O5 待授权 · O6 待实测）。**待评审。**
