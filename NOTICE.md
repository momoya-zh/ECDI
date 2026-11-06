# NOTICE — 第三方组件与再分发声明

本文件是 **ECDI 二进制产物**（静态库 `ECDI.lib` / `libECDI.a`、安装前缀，以及任何静态链接了
ECDI 的 exe / dll / 安装包）的**分发文档**。`cmake --install` 会把它与下面列出的许可全文一起装到
`<prefix>/share/licenses/ECDI/`；以二进制形态再分发时请保持该目录**与产物同行、原样不改**。

> 本文件是源码树根的权威版本；安装目录里的同名文件是它的副本。

## 1. ECDI 本体

ECDI 以 **MIT** 许可发布，版权声明与许可全文见同目录 `LICENSE`（源码树根 `LICENSE`）：

> Copyright (c) 2026 momoya-zh

分发 ECDI 的副本（源码或二进制）时须保留该版权声明与许可声明（MIT 的随副本要求）。

## 2. FreeType（本项目取 FTL）

ECDI 静态链接 **FreeType 2.14.3**（GL 后端文本栈），源码 vendored 于 `third_party/freetype`，
并以 **OBJECT 库**方式并入 `ECDI` 静态库——消费者只看到一个库，FreeType 的目标码在其中。

FreeType 是双许可项目（FTL / GPLv2，见 `LICENSE.TXT`），**本项目选择 FTL**（FreeType License，
BSD 风格、非 copyleft）。随本声明一同分发的许可全文：

| 安装目录下 | 源码树中 | 内容 |
|---|---|---|
| `LICENSE` | `LICENSE` | ECDI 的 MIT 许可 |
| `NOTICE.md` | `NOTICE.md` | 本文件 |
| `FreeType/LICENSE.TXT` | `third_party/freetype/LICENSE.TXT` | FreeType 许可入口（双许可说明 + 子许可指引） |
| `FreeType/FTL.TXT` | `third_party/freetype/docs/FTL.TXT` | FTL 全文（本项目所依据的许可） |
| `FreeType/GPLv2.TXT` | `third_party/freetype/docs/GPLv2.TXT` | GPLv2 全文（`LICENSE.TXT` 正文引用，随附以保持引用完整） |

### 2.1 二进制再分发（FTL §2 第二项）

This product is based in part on the work of the FreeType Team (https://freetype.org).

FreeType Team 首选致谢格式（FTL 引言，encouraged，非强制）：

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org).
> All rights reserved.

`<year>` 取所用 FreeType 版本的版权年：2.14.3 的源文件为 `Copyright (C) 1996-2026`，故取 **2026**。

### 2.2 源码再分发（FTL §2 第一项）

以源码形态再分发含 FreeType 的包时：

- `FTL.TXT` 须**原样保留**；
- 对原始文件的任何增、删、改须在随附文档中**明确说明**——本仓库的裁剪清单与「零本地改动」声明见
  `third_party/freetype/README.ecdi.md`；
- 原文件的版权声明须保留。

其正文引用的子许可（`src/bdf/`、`src/pcf/` 的 X11 风格，`src/gzip/` 的 zlib，`src/autofit/ft-hb-*`
的 Old MIT）已随 `src/` 保留，均与 FTL 兼容。

### 2.3 名称使用（FTL §3）

未经许可，不得使用 FreeType 项目名或其贡献者名为本产品做商业、广告或推广背书；
描述性提及（如「FreeType 文本栈」）不受影响。

## 3. 维护约定

1. **升级 FreeType**：同步更新本节版本号与致谢年份（`<year>` 取新版本的版权年）；
2. **许可全文只可整份替换**：用新版本归档中的 `LICENSE.TXT` / `docs/FTL.TXT` / `docs/GPLv2.TXT`
   覆盖安装目录下的同名文件，不要手改、不要删行；
3. **不得只装部分文件**：`LICENSE.TXT` 正文引用 `docs/FTL.TXT` 与 `docs/GPLv2.TXT`，缺任一份即
   引用断链（Phase 26 vendor 时修过的同型缺口）；
4. 交付单的固定字段（notice 项 + 库 SHA256 + 版本）见 `docs/usage.md` §14.2。