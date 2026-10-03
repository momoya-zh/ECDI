# FreeType 2 — vendored（ECDI 第三方依赖）

本目录是 **FreeType 2.14.3** 的源码快照，**vendor 进仓库**（不从系统、包管理器或网络在构建时获取）。

> 引入决策与判据见 `docs/roadmap-deferred.md` **#44**（后端可替换性 · M-4 文本栅格化）与
> `docs/phase26-freetype-gl-*`；本文件只记录**这份快照本身的可核验事实**。

## 冻结信息（可核验）

| 项 | 值 |
|---|---|
| 版本 | **2.14.3**（2026-03-22 发布，维护版） |
| 上游来源 | `https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz` |
| 归档大小 | **4,134,916** bytes |
| 归档 SHA256 | `e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1` |
| 校验状态 | ★ 与 freetype.org / SourceForge 公布的 SHA256 **逐字符一致**（2026-10-03 核对） |
| 许可 | **FTL**（FreeType License，BSD 风格——可商用、非 copyleft）⇒ 保留 `LICENSE.TXT` **+ 其正文引用的 `docs/FTL.TXT` / `docs/GPLv2.TXT`**（见下「许可与归属」） |

## 为什么取 2.14.3（而非 2.13.x）

FreeType 官方在 2.14.3 的 `CHANGES` 中写明：

> I. IMPORTANT BUG FIXES — A bunch of potential security problems have been found. **All users should update.**

本项目的第三方引入判据（条 95⑦）含「**愿意承担长期跟版（含安全）义务**」⇒ 取**当前最新稳定且含安全修复**的版本。
（另：2.14.0 官方标注 "Don't use this release!"，2.14.1 是其紧急修复；本目录**不含**这两个版本。）

## ★ 本地改动：**零**

**不改动 FreeType 的任何文件**（含 `include/freetype/config/ftoption.h` 与 `ftmodule.h`）。

默认配置已满足本项目需要——逐项实测：

| 配置项 | 默认值 | 对本项目的含义 |
|---|---|---|
| `FT_CONFIG_OPTION_USE_PNG` | **关** | 不引入 libpng |
| `FT_CONFIG_OPTION_USE_BROTLI` | **关** | 不引入 libbrotli（无 WOFF2） |
| `FT_CONFIG_OPTION_USE_BZIP2` | **关** | 不引入 libbz2 |
| `FT_CONFIG_OPTION_USE_HARFBUZZ` | **关** | ★ **不做 shaping**（本项目范围外） |
| `FT_CONFIG_OPTION_SYSTEM_ZLIB` | 关 | 用 FreeType **内置** inflate（不引入 zlib） |
| `TT_CONFIG_OPTION_MAX_RUNNABLE_OPCODES` | **`1000000L`** | ★ 恶意字体解释器爆炸的**步数上限**（保留） |

★ **保持零改动是刻意的**：升级 = **整目录替换**，无需重做任何 patch ⇒ 跟版义务最轻。

★ 关于 `FT_CONFIG_OPTION_SVG`（默认**开**）：它**不引入外部依赖**（SVG 渲染需应用在运行时注册 hook，本项目不注册），
故不关闭——关闭它需同时改 `ftoption.h` **与** `ftmodule.h`（否则 `ft_svg_renderer_class` 链接失败），
**为 1 个无外部依赖的模块引入 2 处 patch 不划算**。

## 目录裁剪（相对上游归档）

**保留**：`include/` · `src/` · `builds/windows/` · `LICENSE.TXT` · `README` · **`docs/FTL.TXT` + `docs/GPLv2.TXT`（许可全文）** · `sources.ecdi.cmake`（本仓库新增） · `README.ecdi.md`（本仓库新增）

**未包含**：`builds/` 的其余部分（`unix/` / `amiga/` / `mac/` / `os2/` …）· `docs/`（**★ 例外：许可全文 `FTL.TXT` / `GPLv2.TXT` 已保留**）· `devel/` ·
`tests/` · `subprojects/` · 顶层构建脚本（`CMakeLists.txt` / `Makefile` / `configure` / `meson.build` /
`MSBuild.*` / `autogen.sh` 等）——均不参与构建。

★ **`builds/windows/` 是例外、必须保留**：Windows 平台的系统接口 `ftsystem.c`（用**宽字符 Win32 API**）
比通用 `src/base/ftsystem.c` 更适合非 ASCII 字体路径。

★ **另排除 `src/tools/`**：实测其中 **4 个 `.c` 自带 `main()`**（`test_afm.c` / `test_bbox.c` 等独立工具程序），
入库会与测试入口冲突。

★ 构建目标由**本仓库**定义（`CMakeLists.txt` 里 `include` 本目录的 `sources.ecdi.cmake`，加 `FT2_BUILD_LIBRARY`），
**不使用**上游的构建系统。

★★ **源文件列表是显式的、不能用 GLOB**：`src/**/*.c` 共 210 个，其中 **165 个是「被其他 `.c` include」的**
（如 `src/lzw/ftzopen.c` 由 `ftlzw.c` include），**不是独立 TU** —— GLOB 逐个编译**必然失败**
（实测 `ftzopen.c` 报 `expected '=', ',', ';' or 'asm' before '{' token`，根因 `FT_LOCAL` 未定义）。
⇒ `sources.ecdi.cmake` 只列**模块入口** `.c`（= 上游 `CMakeLists.txt` 的 `BASE_SRCS`，40 项）+ `builds/windows/ftsystem.c`。

## 许可与归属（合规）

FreeType 是**双许可**项目（`LICENSE.TXT` 原文）：使用方须**择一**并遵守其全部条款 ——
**FTL**（BSD 风格 + 广告条款，适合非 GPL 产品）或 **GPLv2**（适合已用 GPL 的程序）。本项目取 **FTL**。

**FTL 的核心义务**（分发源码或二进制时）：

1. **保留版权声明、条件与免责声明** —— 即须随附**完整许可全文**；
2. 不得使用 FreeType 项目名或贡献者名为自己的产品背书。

**本仓库的合规做法**：

| 分发形态 | 须随附 |
|---|---|
| **源码**（本仓库） | ✅ `LICENSE.TXT` + `docs/FTL.TXT` + `docs/GPLv2.TXT`（**三份都在库内**） |
| **二进制**（`ECDI.a` / 可执行 / 安装包） | ★ **须随附 FreeType 的版权声明与 FTL 全文** —— FreeType 的 obj 已并入 `ECDI.a`（OBJECT 库合并）⇒ 二进制分发即触发该义务 |

★ **判据**：许可文件是否齐全，**以 `LICENSE.TXT` 正文「引用到的文件」逐一核对** ——
不能只看 `LICENSE.TXT` 在不在（它只是入口，正文指向 `docs/FTL.TXT` 与 `docs/GPLv2.TXT`）。

★ **其它许可**：`src/bdf/README`、`src/pcf/README`（X11 风格）· `src/gzip/zlib.h`（zlib）·
`src/autofit/ft-hb-*`（Old MIT，来自 HarfBuzz）—— 均已随 `src/` 保留，且与 FTL / GPLv2 兼容。

## 同步义务（升级流程）

上游发布**安全相关**更新时：

1. 下载新版本归档，**核对官方 SHA256**；
2. 以同样的裁剪规则替换 `include/` · `src/`（排除 `src/tools/`）· `builds/windows/` · `LICENSE.TXT` · `docs/FTL.TXT` · `docs/GPLv2.TXT`（**保持「零本地改动」**）；
3. 更新本文件的**版本 / 大小 / SHA256** 三行；
4. 在 `docs/roadmap-deferred.md` **#44** 记一行同步日志（日期 + 旧版本 → 新版本 + 触发原因）。
