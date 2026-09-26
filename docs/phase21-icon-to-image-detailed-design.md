# Phase 21 · 系统图标 → Image（icon to image）—— 详细设计（v1.0）

> 来源：`phase21-icon-to-image-requirements.md` **v1.1** · `phase21-icon-to-image-preliminary-design.md` **v1.2**（评审通过 + 前置探针修正）
> 状态：**v1.0**（2026-09-26）——**待评审**（评审通过后方可实施）
> 本稿输入：需求 **R1–R5 / D0–D5 / N1–N6 / A1–A6** ＋ 初设定案 **Q1–Q4 / C1–C8 / O1–O4 / △1–△6 / 资源清单第一版** ＋ ★★ **本稿新增实测 P9–P13**（`<32bpp + mask` 补测 · 多行方向 · `DrawIconEx` 替代方案）＋ ★ **构建级核实 B11–B13**

---

## 1. 设计输入与基线

### 1.1 设计输入

| 项 | 内容 |
|---|---|
| **需求** | `docs/phase21-icon-to-image-requirements.md` **v1.1** |
| **初设** | `docs/phase21-icon-to-image-preliminary-design.md` **v1.2**（★ 含 **§2.7 的实测修正**：不透明度判据由「位深」改为「**alpha 是否携带信息**」） |
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

---

## 2. 算法规格

### 2.1 ★★ 内核 `ImageFromHIcon(HICON icon)`

**落点**：`src/Platform/Win32/ShellImageDecoder.h`（声明，**内部头**）+ `.cpp`（定义）。**平台层内部**，公共面不见 `HICON`（**C1**）。

```text
 1  ICONINFO ii{};  if (!GetIconInfo(icon, &ii))                  → LogIconError(L"GetIconInfo") + return {}
 2  ★ RAII ①  IconBitmaps 守卫接管 { ii.hbmColor, ii.hbmMask }，析构 DeleteObject（nullptr 安全）
 3  BITMAP bc{};  if (ii.hbmColor == nullptr
                   || GetObject(ii.hbmColor, sizeof(bc), &bc) != sizeof(bc))  → Log + return {}
 4  w = bc.bmWidth; h = bc.bmHeight;   if (!ValidIconSize(w, h))  → Log + return {}          // §2.4
 5  stride = w * 4;  bufferSize = stride * h
 6  Image img;  img.width = w; img.height = h; img.stride = stride; img.pixels.resize(bufferSize);
 7  ★ RAII ②  ScreenDc 守卫取 GetDC(nullptr)；失败 → Log + return {}
 8  ★ 读 color（恒 32bpp / top-down —— P10/P11/P12）
      if (GetDIBits(dc, ii.hbmColor, 0, h, img.pixels.data(), &bi32, DIB_RGB_COLORS) != (int)h)
          → Log + return {}
 9  ★★ 不透明度判据（P10/P11 —— 初设 §2.7）
      if (!AnyAlphaNonZero(img.pixels)) {                       // 「该位图不携带 alpha 信息」
          maskStride = ((w + 31) / 32) * 4
          std::vector<uint8_t> mask(maskStride * h)
          if (ii.hbmMask == nullptr
              || GetDIBits(dc, ii.hbmMask, 0, h, mask.data(), &bi1, DIB_RGB_COLORS) != (int)h)
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
      const DWORD_PTR r = SHGetFileInfoW(wide.c_str(), attrs, &sfi, sizeof(sfi),
                                         SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
      if (r == 0 || sfi.hIcon == nullptr)    → Log + return {}        // C3
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
| **⑤** | mask 位序写反（`7 - (x & 7)` vs `x & 7`） | 与 §6.1 的 T21-2 期望值逐字节对齐（**位序错了必红**） |
| **⑥** | 请求 `GetDIBits` 时 `biHeight` 用正值（bottom-up） | 两处 `BITMAPINFO` 的 `biHeight` **均为 `-h`** |
| **⑦** | premultiply 用了 `+128` 或截断 | 唯一形式 `(c * a + 127) / 255`（字符级判据） |
| **⑧** | 在公共头里写出 `HICON` | **C1 的 grep 判据**（§5） |
| **⑨** | 忘 `src/Tests` 的登记 | 登记数与 §6.4 一致；★ **不登记 = 用例静默不跑** |
| **⑩** | 新 `.cpp` 里用了 `std::numeric_limits` 而没处理 `min`/`max` 宏 | ★ **本设计不需要** `<limits>`（§2.4 用 64 位中间量）⇒ 若确要加，须按条 10 加 `#ifdef max` / `min` 的 undef |

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

void LogIconError(const wchar_t* step, unsigned long code);       // ★ 自带一份（O1 定案：不复用 B2 的匿名件）
bool ValidIconSize(int w, int h);
bool AnyAlphaNonZero(const std::uint8_t* bgra, std::size_t pixelCount);
void PremultiplyInPlace(std::uint8_t* bgra, std::size_t pixelCount);
void FillBiTopDown(BITMAPINFO& bi, int w, int h, unsigned short bpp);   // ★ 恒 biHeight = -h

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
★ **本设计不需要 `<limits>`**（§2.4 用 64 位中间量）⇒ **不触发 `min`/`max` 宏问题**（盯防 ⑩）。

### 3.4 △4 + △5 测试文件与登记

| △ | 文件 | 改动 |
|---|---|---|
| **△4** | **新建** `src/Tests/IconDecodeTests.cpp` | 承载 **T21-1..T21-9**（§6）。★ **include 顺序照 `DropFilesTests.cpp` / `DpiTests.cpp` 的模板**：`RunAllTests.h` → `TestFramework.h` → `<Windows.h>` → ★ **`#ifdef DrawText` / `#undef DrawText`**（条 10）→ 内部头 + ECDI 头 → 标准库 |
| **△5** | `RunAllTests.h`（**+1 声明**）· `RunAllTests.cpp`（**+1 调用**） | 沿 `RegisterDpiTests` 的先例（**B13**），声明带 `///< Phase 21：…` 说明 |

---

## 4. 契约（C1–C12）

> **C1–C8 承接初设**（原样或加严；★ 标注加严处）；**C9–C12 本稿新增**。

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

**共同装置**：`MakeColor32` / `MakeColor24` / `Make1bpp` / `MakeMask1`（`CreateDIBSection` + **正 `biHeight`（bottom-up，图标惯例）** + 逐行倒序写入 ⇒ **内存第 0 行 = 视觉底行**）→ `CreateIconIndirect`。

| # | 输入 | 期望（逐字节） |
|---|---|---|
| **T21-1** ★ | **2×2 · 32bpp + alpha**：全像素 `B=G=R=0x40`；alpha **视觉顶行** = `0x00, 0x40`、**底行** = `0x80, 0xFF`；mask 全清 | `width=2 height=2 stride=8 pixels.size()=16`；缓冲（行优先、top-down）＝<br>`00 00 00 00` · `10 10 10 40` · `20 20 20 80` · `40 40 40 FF`<br>★ 说明：`(0x40×A + 127) / 255` ⇒ A=0→**0x00** · A=0x40→**0x10** · A=0x80→**0x20** · A=0xFF→**0x40**；★ **同时钉住「多行方向」**（P9） |
| **T21-2** ★ | **4×1 · 24bpp color + 1bpp mask**：全像素 `B=0x10 G=0x20 R=0x30`；mask = `0xC0`（**px0 / px1 置位**） | `width=4 height=1 stride=16`；＝ `00 00 00 00` · `00 00 00 00` · `10 20 30 FF` · `10 20 30 FF`<br>★★ **反向断言**：**至少一个像素 `A == 255`**（★ 证「**没有误用 alpha 导致全透明**」——正是 C9 的存在理由）；★ 期望值**同时钉住 mask 位序**（盯防 ⑤） |
| **T21-3** | **退化输入**：① `ImageFromHIcon(nullptr)` ② 1×1 正常图标（边界尺寸） | ① `width==0 && height==0 && pixels.empty()`（**不崩**）② 契约成立（`stride == 4` · `pixels.size() == 4`） |

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
- ★ **「平台调用失败」维度（评审 §17）**：归 **C3**，但★ **在本设计下难以构造**（P1–P7：`USEFILEATTRIBUTES` 对任意**非空**路径基本都成功）⇒ **详设实施时先尝试构造**（候选：超长路径 / 含非法字符）；★ **若确不可构造，则明确记为「防御性分支、无自动化用例」——不假称已覆盖**。

---

## 7. 实现顺序与检查点（两批 + 收尾）

| 批 | 文件 | 检查点 |
|---|---|---|
| **批一** | **新建** `ShellImageDecoder.h` · `ShellImageDecoder.cpp` ＋ **△1** `ImageDecoder.h` ＋ **△5** 登记 | ★ **构建通过 + 既有 252 全绿**——此时**新函数无人调用**，且 `ImageFromHIcon` 尚无用例 ⇒ **零破坏可观测** |
| **批二** | **新建** `IconDecodeTests.cpp`（T21-1..T21-9） | ★ **261 全绿**（252 + 9）· 四工具链 |
| **收尾** | 文档回填（详设 §实施记录）+ 索引 + 记忆 | 需求 **A1–A6** 的最终判定 |

★ **批序理由 = 先让能力可构建、再让能力可验证**：批一单独check 点能证明「**新增生产文件不影响既有行为**」（这是零回归的**结构性证据**，而非"看一遍 diff"）。
★ ★ **两批都无需 CMake 改动**（**B11** + `GLOB_RECURSE`）。

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

---

## 9. 交回上游 / 待授权（O）

| # | 事项 | 处置 |
|---|---|---|
| **O1** | 新文件是否复用 `WicImageDecoder.cpp` 的日志辅助 | ★ **定案：不复用**（§3.3 三条理由）——★ 若后续出现**第三个** `Decode` 消费者，再评估提为共享件 |
| **O2** | premultiply 舍入口径 | ✅ **定案：`(c * a + 127) / 255`**（**B14** 的既有口径先例） |
| **O3** | 老式 mask 语义 | ✅ **已定案**（初设 v1.2 §2.7，由 P9–P13 补测） |
| **O4** | 内核命名 / 归属 | ✅ **定案**：`ImageFromHIcon` 放**内部头**（§3.2，沿 `DpiConversion.h` / `CoverageRaster.h` 先例） |
| **O5** ★ | **待授权（沿用初设 §2.9）**：① `desktopnest-roadmap.md` §3/§4 的 `SHGetFileInfo` 行**改判 + 补第三问** ② 「逐文件自定义图标」限制**立新记账项** | ★ **不在本阶段偷偷改**——**动手前须授权** |
| **O6** ★ | ★ **待实测**：**Per-Monitor V2 进程下 `SHGFI_LARGEICON` 的实际尺寸**（初设 P8：本机系统 DPI = 96 ⇒ 不可判定） | ★ **不阻塞设计**（**C5**：框架不假设尺寸）；★ 若日后发现随 DPI 变，也只是 `Image` 尺寸变 ⇒ **API 形态不受影响** |

---

## 10. 修订记录

- **v1.0**（2026-09-26）初稿。**输入**：需求 **v1.1** · 初设 **v1.2**（含 §2.7 的实测修正）＋ ★★ **本稿新增实测 P9–P13**（`<32bpp + mask` 补测 / 多行方向 / `DrawIconEx` 替代方案）+ ★ **构建级核实 B11–B14**（**`shell32` 已链** · `WideToUTF8` 可用 · 登记锚点 · **`CoverageRaster` 的 `+127` 口径先例**）。**内容**：§2 **算法规格**（内核 **11 步** / 外壳 **6 步** · ★ **资源与 RAII 三栏** · 尺寸校验口径 · **盯防清单 10 条**）· §3 **逐文件改动 △1–△5**（含头全文草案）· §4 **契约 C1–C12** · §5 影响面 · §6 **T21-1..T21-9 逐条输入与逐字节期望** · §7 **两批 + 收尾** · §8 A1–A6 落地（★ A1 的口径说明）· §9 **O1–O6**（O2/O3/O4 已定案；O5 待授权 · O6 待实测）。**待评审。**
